/**
 * PDFT encoder. See pdft_write.h.
 *
 * Everything is big endian and nothing is padded or aligned: the reader
 * dereferences u32* and u16* straight out of the buffer, so it does unaligned
 * loads and any padding we added would be read as data.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "pdft_write.h"

struct buf {
	uint8_t *data;
	uint32_t len;
	uint32_t cap;
	bool bad;
};

static void bufNeed(struct buf *b, uint32_t n)
{
	if (b->bad) {
		return;
	}

	if (b->len + n > b->cap) {
		uint32_t cap = b->cap ? b->cap * 2 : 4096;
		uint8_t *next;

		while (cap < b->len + n) {
			cap *= 2;
		}

		next = realloc(b->data, cap);

		if (!next) {
			b->bad = true;
			return;
		}

		b->data = next;
		b->cap = cap;
	}
}

static void put8(struct buf *b, uint8_t v)
{
	bufNeed(b, 1);
	if (b->bad) return;
	b->data[b->len++] = v;
}

static void put16(struct buf *b, uint16_t v)
{
	bufNeed(b, 2);
	if (b->bad) return;
	b->data[b->len++] = (uint8_t)(v >> 8);
	b->data[b->len++] = (uint8_t)v;
}

static void put32(struct buf *b, uint32_t v)
{
	bufNeed(b, 4);
	if (b->bad) return;
	b->data[b->len++] = (uint8_t)(v >> 24);
	b->data[b->len++] = (uint8_t)(v >> 16);
	b->data[b->len++] = (uint8_t)(v >> 8);
	b->data[b->len++] = (uint8_t)v;
}

static void putBytes(struct buf *b, const void *src, uint32_t n)
{
	bufNeed(b, n);
	if (b->bad) return;
	memcpy(b->data + b->len, src, n);
	b->len += n;
}

/**
 * A string as the reader wants it: the length counts the terminator, and the
 * terminator is in the file. Absent is length 1 and a lone NUL, not length 0 —
 * the reader only looks at content when the length exceeds 1, but a zero would
 * leave it reading the next field as a string.
 */
static void putStr16(struct buf *b, const char *s)
{
	uint32_t n = s ? (uint32_t)strlen(s) : 0;
	put16(b, (uint16_t)(n + 1));
	if (n) {
		putBytes(b, s, n);
	}
	put8(b, 0);
}

static void putStr8(struct buf *b, const char *s)
{
	uint32_t n = s ? (uint32_t)strlen(s) : 0;
	put8(b, (uint8_t)(n + 1));
	if (n) {
		putBytes(b, s, n);
	}
	put8(b, 0);
}

static bool fail(char *err, uint32_t errLen, const char *fmt, ...)
{
	va_list ap;

	if (err && errLen) {
		va_start(ap, fmt);
		vsnprintf(err, errLen, fmt, ap);
		va_end(ap);
	}

	return false;
}

uint32_t pdftVersionFor(const struct pdftInput *in)
{
	uint32_t i;

	if (in->numTexMap) {
		return 3;
	}

	if (in->numSources) {
		return 2;
	}

	for (i = 0; i < in->numFiles; ++i) {
		if (in->files[i].alt.romIdx >= 0) {
			return 2;
		}
	}

	return 1;
}

static bool checkInput(const struct pdftInput *in, uint32_t version, char *err, uint32_t errLen)
{
	uint32_t i;

	if (in->numSources > PDFT_MAX_ROMSOURCES) {
		return fail(err, errLen, "%u romSources; the reader keeps %d and drops the rest",
				in->numSources, PDFT_MAX_ROMSOURCES);
	}

	for (i = 0; i < in->numSources; ++i) {
		const struct pdftRomSource *rs = &in->sources[i];

		if (!rs->id || !rs->id[0]) {
			return fail(err, errLen, "romSource %u has no id", i);
		}

		if (strlen(rs->id) + 1 > PDFT_ROMSOURCE_ID) {
			return fail(err, errLen, "romSource '%s': id is %zu bytes, the reader's buffer is %d",
					rs->id, strlen(rs->id) + 1, PDFT_ROMSOURCE_ID);
		}

		if (!rs->filename || !rs->filename[0]) {
			return fail(err, errLen, "romSource '%s' has no filename", rs->id);
		}

		if (strlen(rs->filename) + 1 > PDFT_ROMSOURCE_FILE) {
			return fail(err, errLen, "romSource '%s': filename is %zu bytes, the reader's buffer is %d",
					rs->id, strlen(rs->filename) + 1, PDFT_ROMSOURCE_FILE);
		}
	}

	for (i = 0; i < in->numFiles; ++i) {
		const struct pdftFile *f = &in->files[i];
		size_t n;

		if (!f->name || !f->name[0]) {
			return fail(err, errLen, "file entry %u (id %u) has no name", i, f->id);
		}

		if (f->id >= PDFT_MAX_FILES) {
			return fail(err, errLen, "'%s' has id %u; the reader skips anything from %d up, without a word",
					f->name, f->id, PDFT_MAX_FILES);
		}

		n = strlen(f->name) + 1;

		if (n > 0xffff) {
			return fail(err, errLen, "'%s': name is %zu bytes and the length field is 16 bits", f->name, n);
		}

		if (f->path) {
			n = strlen(f->path) + 1;

			if (n > 0xffff) {
				return fail(err, errLen, "'%s': path is %zu bytes and the length field is 16 bits", f->name, n);
			}
		}

		if (f->selfSource) {
			/* The path is the whole of a self-source: there is no offset to
			 * fall back on and no image to look in. Without it the flag says
			 * "my own directory" and names nothing there, and the engine would
			 * resolve the entry by its name against the mod walk - which is
			 * exactly the accident the flag exists to stop. */
			if (!f->path || !f->path[0]) {
				return fail(err, errLen, "'%s' is self-sourced with no path", f->name);
			}

			/* Two answers to where the bytes are. Which one wins would be
			 * decided by the order the engine happens to test them in, which
			 * is the same kind of luck a declared source is meant to remove. */
			if (f->alt.romIdx >= 0) {
				return fail(err, errLen, "'%s' declares both a self source and romSource %d",
						f->name, f->alt.romIdx);
			}
		}

		if (f->alt.romIdx >= 0) {
			if (version < 2) {
				return fail(err, errLen, "'%s' has an alt-ROM tail; the reader only reads one in a v2 table "
						"and silently desyncs on the rest of a v1 one", f->name);
			}

			if ((uint32_t)f->alt.romIdx >= in->numSources) {
				return fail(err, errLen, "'%s' names romSource %d of %u", f->name, f->alt.romIdx, in->numSources);
			}

			if (f->alt.compression != 0) {
				return fail(err, errLen, "'%s': alt-ROM compression %u; the loader implements 0 only",
						f->name, f->alt.compression);
			}
		}
	}

	// The reader advances its global texture-port base by the entry COUNT and
	// maps each entry by its slot index, so the slots have to be exactly
	// 0..count-1. A hole reserves a port nothing uses and pushes the next
	// mod's range over this one's.
	if (in->numTexMap) {
		uint8_t *seen = calloc(in->numTexMap, 1);

		if (!seen) {
			return fail(err, errLen, "out of memory checking the texmap");
		}

		for (i = 0; i < in->numTexMap; ++i) {
			uint16_t slot = in->texmap[i].slotIdx;

			if (slot >= in->numTexMap) {
				free(seen);
				return fail(err, errLen, "texmap slot %u is outside 0..%u; slots must be dense",
						slot, in->numTexMap - 1);
			}

			if (seen[slot]) {
				free(seen);
				return fail(err, errLen, "texmap slot %u is used twice", slot);
			}

			seen[slot] = 1;
		}

		free(seen);
	}

	return true;
}

uint8_t *pdftWrite(const struct pdftInput *in, uint32_t *outLen, char *err, uint32_t errLen)
{
	struct buf b = { NULL, 0, 0, false };
	uint32_t version;
	uint32_t i;

	if (err && errLen) {
		err[0] = '\0';
	}

	if (!in || (!in->numFiles && !in->numSources && !in->numTexMap)) {
		fail(err, errLen, "nothing to write");
		return NULL;
	}

	version = pdftVersionFor(in);

	if (!checkInput(in, version, err, errLen)) {
		return NULL;
	}

	putBytes(&b, "PDFT", 4);
	put32(&b, version);
	put32(&b, in->numFiles);

	if (version >= 2) {
		put32(&b, in->numSources);

		for (i = 0; i < in->numSources; ++i) {
			const struct pdftRomSource *rs = &in->sources[i];

			putStr8(&b, rs->id);
			putStr8(&b, rs->filename);
			put32(&b, rs->expectedSize);
			put8(&b, (uint8_t)((rs->required ? 1 : 0) | (rs->strict ? 2 : 0)));
			put8(&b, rs->fallback);
			put8(&b, 0);
			put8(&b, 0);
		}
	}

	for (i = 0; i < in->numFiles; ++i) {
		const struct pdftFile *f = &in->files[i];
		const bool hasPath = f->path && f->path[0];
		const bool hasAlt = f->alt.romIdx >= 0;
		uint32_t flags = 0;

		if (f->romResident) {
			flags |= PDFT_F_ROMRESIDENT;
		}

		if (hasPath) {
			flags |= PDFT_F_PATH;
		}

		if (hasAlt) {
			flags |= PDFT_F_ALT;
		}

		/* No tail and no new field: the whole declaration is this bit plus the
		 * path that was already being written. That is what keeps the format
		 * at v3 and keeps an older reader merely ignorant of the flag rather
		 * than desynced by it. */
		if (f->selfSource) {
			flags |= PDFT_F_SELFSOURCE;
		}

		put32(&b, f->id);
		put32(&b, flags);
		put32(&b, f->romResident ? f->offset : 0);
		put32(&b, f->romResident ? f->size : 0);
		putStr16(&b, f->name);
		putStr16(&b, hasPath ? f->path : NULL);

		if (hasAlt) {
			put8(&b, (uint8_t)f->alt.romIdx);
			put32(&b, f->alt.offset);
			put32(&b, f->alt.size);
			put8(&b, f->alt.compression);
		}
	}

	if (version >= 3) {
		put32(&b, in->numTexMap);

		for (i = 0; i < in->numTexMap; ++i) {
			put16(&b, in->texmap[i].localTexId);
			put16(&b, in->texmap[i].slotIdx);
		}
	}

	if (b.bad) {
		free(b.data);
		fail(err, errLen, "out of memory");
		return NULL;
	}

	*outLen = b.len;
	return b.data;
}
