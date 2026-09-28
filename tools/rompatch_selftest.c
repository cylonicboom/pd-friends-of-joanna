/**
 * Standalone driver for port/src/rompatch.c, used by tools/rompatch_test.py.
 *
 * Applies a patch to a ROM and writes the result, so the Python side can
 * compare it against the target the patch was built from. Not built by cmake
 * and not part of the game; see rompatch_test.py for the build line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rompatch.h"

static u8 *loadWhole(const char *path, u32 *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	u8 *buf;

	if (!f) {
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	buf = malloc(n ? (u32)n : 1);
	if (buf && n && fread(buf, 1, n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	*len = buf ? (u32)n : 0;
	return buf;
}

int main(int argc, char **argv)
{
	u8 *rom, *patch, *out = NULL;
	u32 romlen = 0, patchlen = 0, outlen = 0;
	char err[256] = { 0 };
	s32 kind;
	FILE *f;

	if (argc < 4) {
		fprintf(stderr, "usage: rompatch_selftest <rom> <patch> <out> [--overlay]\n");
		return 2;
	}
	if (!(rom = loadWhole(argv[1], &romlen)) || !(patch = loadWhole(argv[2], &patchlen))) {
		fprintf(stderr, "could not read the rom or the patch\n");
		return 2;
	}

	if (argc > 4 && !strcmp(argv[4], "--overlay")) {
		// decode as an overlay, then compose the whole target through the
		// read path in odd-sized pieces, so a byte-identical result proves
		// both the segments and the reader
		struct romoverlay *ov = NULL;
		u32 at = 0, step = 4093;
		kind = rompatchOverlay(rom, romlen, patch, patchlen, &ov, err, sizeof(err));
		if (kind < 0) {
			fprintf(stderr, "FAIL: %s\n", err);
			return 1;
		}
		outlen = ov->size;
		out = malloc(outlen ? outlen : 1);
		while (at < outlen) {
			const u32 n = outlen - at < step ? outlen - at : step;
			if (!rompatchOverlayRead(ov, at, n, out + at)) {
				fprintf(stderr, "FAIL: overlay read at %u failed\n", at);
				return 1;
			}
			at += n;
			step = step == 4093 ? 65521 : 4093;
		}
		printf("overlay: %u segments, %u literal bytes, %u bytes held for a %u byte target; peek(0x1000,32)=%s\n",
				ov->numsegs, ov->litlen, rompatchOverlayCost(ov), ov->size,
				rompatchOverlayPeek(ov, 0x1000, 32) ? "direct" : "split");
		rompatchOverlayFree(ov);
	} else {
		kind = rompatchApply(rom, romlen, patch, patchlen, &out, &outlen, err, sizeof(err));
		if (kind < 0) {
			fprintf(stderr, "FAIL: %s\n", err);
			return 1;
		}
	}

	if (!(f = fopen(argv[3], "wb"))) {
		fprintf(stderr, "could not write %s\n", argv[3]);
		return 2;
	}
	fwrite(out, 1, outlen, f);
	fclose(f);
	printf("kind=%s outlen=%u\n", rompatchKindName(kind), outlen);
	return 0;
}
