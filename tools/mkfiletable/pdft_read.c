/**
 * PDFT decoder. See pdft_read.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdft_read.h"

struct rd {
	const uint8_t *buf;
	uint32_t len;
	uint32_t pos;
	bool bad;
};

static uint8_t rd8(struct rd *r)
{
	if (r->bad || r->pos + 1 > r->len) {
		r->bad = true;
		return 0;
	}

	return r->buf[r->pos++];
}

static uint16_t rd16(struct rd *r)
{
	uint16_t v;

	if (r->bad || r->pos + 2 > r->len) {
		r->bad = true;
		return 0;
	}

	v = (uint16_t)((r->buf[r->pos] << 8) | r->buf[r->pos + 1]);
	r->pos += 2;
	return v;
}

static uint32_t rd32(struct rd *r)
{
	uint32_t v;

	if (r->bad || r->pos + 4 > r->len) {
		r->bad = true;
		return 0;
	}

	v = ((uint32_t)r->buf[r->pos] << 24) | ((uint32_t)r->buf[r->pos + 1] << 16)
			| ((uint32_t)r->buf[r->pos + 2] << 8) | (uint32_t)r->buf[r->pos + 3];
	r->pos += 4;
	return v;
}

/**
 * A counted string: the length includes its own terminator, and the writer
 * always emits the terminator, so a zero length means an absent string and any
 * other length has a NUL as its last byte. Returned as a pointer into the
 * buffer, never copied.
 */
static const char *rdStr(struct rd *r, uint32_t n)
{
	const char *s;

	if (r->bad || !n) {
		return NULL;
	}

	if (r->pos + n > r->len) {
		r->bad = true;
		return NULL;
	}

	s = (const char *)r->buf + r->pos;
	r->pos += n;

	if (s[n - 1] != '\0') {
		r->bad = true;
		return NULL;
	}

	return s;
}

bool pdftRead(const uint8_t *buf, uint32_t len, struct pdftTable *out, char *err, uint32_t errLen)
{
	struct rd r = { buf, len, 0, false };
	uint32_t i;

	memset(out, 0, sizeof(*out));

	if (len < 12 || memcmp(buf, "PDFT", 4)) {
		snprintf(err, errLen, "not a PDFT table (no magic)");
		return false;
	}

	r.pos = 4;
	out->version = rd32(&r);
	out->numFiles = rd32(&r);

	if (out->version < 1 || out->version > 5) {
		snprintf(err, errLen, "version %u is not one of 1 to 5", out->version);
		return false;
	}

	if (out->numFiles > PDFT_MAX_FILES) {
		snprintf(err, errLen, "%u files; the reader keeps %d", out->numFiles, PDFT_MAX_FILES);
		return false;
	}

	if (out->version >= 2) {
		out->numSources = rd32(&r);

		if (r.bad || out->numSources > PDFT_MAX_ROMSOURCES) {
			snprintf(err, errLen, "%u romSources; the reader keeps %d",
					out->numSources, PDFT_MAX_ROMSOURCES);
			return false;
		}

		out->sources = calloc(out->numSources ? out->numSources : 1, sizeof(*out->sources));

		for (i = 0; i < out->numSources; ++i) {
			struct pdftRomSource *rs = &out->sources[i];
			uint8_t flags;

			rs->id = rdStr(&r, rd8(&r));
			rs->filename = rdStr(&r, rd8(&r));
			rs->expectedSize = rd32(&r);
			flags = rd8(&r);
			rs->required = (flags & PDFT_RS_REQUIRED) != 0;
			rs->strict = (flags & PDFT_RS_STRICT) != 0;
			rs->fallback = rd8(&r);
			rd8(&r);
			rd8(&r);

			if (flags & PDFT_RS_PATCHED) {
				if (out->version < 5) {
					snprintf(err, errLen, "romSource '%s' is patched in a v%u table; the reader would desync here",
							rs->id ? rs->id : "?", out->version);
					return false;
				}
				rs->base = rdStr(&r, rd8(&r));
				rs->patch = rdStr(&r, rd8(&r));
				rs->expectedCrc32 = rd32(&r);
			}
		}
	}

	out->files = calloc(out->numFiles ? out->numFiles : 1, sizeof(*out->files));

	for (i = 0; i < out->numFiles; ++i) {
		struct pdftFile *f = &out->files[i];
		uint32_t flags;

		f->id = rd32(&r);
		flags = rd32(&r);
		f->offset = rd32(&r);
		f->size = rd32(&r);
		f->romResident = (flags & PDFT_F_ROMRESIDENT) != 0;
		f->selfSource = (flags & PDFT_F_SELFSOURCE) != 0;
		f->name = rdStr(&r, rd16(&r));
		f->path = rdStr(&r, rd16(&r));
		f->alt.romIdx = -1;

		if (flags & PDFT_F_ALT) {
			f->alt.romIdx = rd8(&r);
			f->alt.offset = rd32(&r);
			f->alt.size = rd32(&r);
			f->alt.compression = rd8(&r);
		}

		if (flags & PDFT_F_ALIAS) {
			f->alias = rdStr(&r, rd16(&r));
		}

		if (flags & PDFT_F_PATCH) {
			if (out->version < 5) {
				snprintf(err, errLen, "'%s' carries a patch tail in a v%u table; the reader would desync here",
						f->name ? f->name : "?", out->version);
				return false;
			}
			f->patch = rdStr(&r, rd16(&r));
		}

		if (r.bad) {
			break;
		}
	}

	if (out->version >= 3) {
		out->numTexMap = rd32(&r);

		if (!r.bad && out->numTexMap > PDFT_MAX_FILES) {
			snprintf(err, errLen, "%u texmap entries is not plausible", out->numTexMap);
			pdftFree(out);
			return false;
		}

		if (!r.bad) {
			out->texmap = calloc(out->numTexMap ? out->numTexMap : 1, sizeof(*out->texmap));

			for (i = 0; i < out->numTexMap; ++i) {
				out->texmap[i].localTexId = rd16(&r);
				out->texmap[i].slotIdx = rd16(&r);
			}
		}
	}

	if (r.bad) {
		snprintf(err, errLen, "truncated at byte %u of %u", r.pos, len);
		pdftFree(out);
		return false;
	}

	out->trailing = len - r.pos;
	return true;
}

void pdftFree(struct pdftTable *t)
{
	free(t->sources);
	free(t->files);
	free(t->texmap);
	memset(t, 0, sizeof(*t));
}
