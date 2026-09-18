/**
 * Test driver for pdft_write.c, used by pdft_test.py.
 *
 * Reads a flat description of a filetable and encodes it. The description is
 * deliberately dull — one record per line, decimal fields, strings hex-encoded
 * so nothing has to be quoted — because its only job is to let the test drive
 * the encoder from a table it decoded elsewhere.
 *
 *   S <hex id> <hex filename> <expectedSize> <required> <strict> <fallback>
 *   F <id> <romResident> <offset> <size> <hex name> <hex path> <altRom> <altOfs> <altSize> <altComp> [selfSource]
 *   T <localTexId> <slotIdx>
 *
 * A hex field of "-" is an absent string; altRom of -1 is no alt-ROM tail.
 * selfSource is optional and defaults to 0, so a spec written before the flag
 * existed still reads.
 * Not built by anything but the test.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdft_write.h"

#define MAXLINE 65536

static char *unhex(const char *hex)
{
	size_t n, i;
	char *out;

	if (!hex || !strcmp(hex, "-")) {
		return NULL;
	}

	n = strlen(hex);

	if (n & 1) {
		return NULL;
	}

	out = malloc(n / 2 + 1);

	if (!out) {
		return NULL;
	}

	for (i = 0; i < n / 2; ++i) {
		unsigned byte;

		if (sscanf(hex + i * 2, "%2x", &byte) != 1) {
			free(out);
			return NULL;
		}

		out[i] = (char)byte;
	}

	out[n / 2] = '\0';
	return out;
}

int main(int argc, char **argv)
{
	struct pdftFile *files = NULL;
	struct pdftRomSource *sources = NULL;
	struct pdftTexMap *texmap = NULL;
	uint32_t numFiles = 0, numSources = 0, numTexMap = 0;
	uint32_t capFiles = 0, capSources = 0, capTexMap = 0;
	struct pdftInput in;
	char line[MAXLINE];
	uint8_t *out;
	uint32_t outLen = 0;
	char err[256];
	FILE *fin, *fout;

	if (argc < 3) {
		fprintf(stderr, "usage: pdft_reencode <spec> <out.dat>\n");
		return 2;
	}

	fin = strcmp(argv[1], "-") ? fopen(argv[1], "r") : stdin;

	if (!fin) {
		fprintf(stderr, "could not read %s\n", argv[1]);
		return 2;
	}

	while (fgets(line, sizeof(line), fin)) {
		char hexA[MAXLINE], hexB[MAXLINE];
		unsigned a, b, c, d, e, f;
		int alt;

		if (line[0] == 'S') {
			if (sscanf(line, "S %s %s %u %u %u %u", hexA, hexB, &a, &b, &c, &d) != 6) continue;

			if (numSources == capSources) {
				capSources = capSources ? capSources * 2 : 8;
				sources = realloc(sources, capSources * sizeof(*sources));
			}

			sources[numSources].id = unhex(hexA);
			sources[numSources].filename = unhex(hexB);
			sources[numSources].expectedSize = a;
			sources[numSources].required = !!b;
			sources[numSources].strict = !!c;
			sources[numSources].fallback = (uint8_t)d;
			++numSources;
		} else if (line[0] == 'F') {
			unsigned altOfs, altSize, altComp, self = 0;
			int got = sscanf(line, "F %u %u %u %u %s %s %d %u %u %u %u",
					&a, &b, &c, &d, hexA, hexB, &alt, &altOfs, &altSize, &altComp, &self);

			if (got != 10 && got != 11) continue;

			if (got == 10) self = 0;

			if (numFiles == capFiles) {
				capFiles = capFiles ? capFiles * 2 : 256;
				files = realloc(files, capFiles * sizeof(*files));
			}

			memset(&files[numFiles], 0, sizeof(files[numFiles]));
			files[numFiles].id = a;
			files[numFiles].romResident = !!b;
			files[numFiles].offset = c;
			files[numFiles].size = d;
			files[numFiles].name = unhex(hexA);
			files[numFiles].path = unhex(hexB);
			files[numFiles].alt.romIdx = alt;
			files[numFiles].alt.offset = altOfs;
			files[numFiles].alt.size = altSize;
			files[numFiles].alt.compression = (uint8_t)altComp;
			files[numFiles].selfSource = !!self;
			++numFiles;
		} else if (line[0] == 'T') {
			if (sscanf(line, "T %u %u", &e, &f) != 2) continue;

			if (numTexMap == capTexMap) {
				capTexMap = capTexMap ? capTexMap * 2 : 64;
				texmap = realloc(texmap, capTexMap * sizeof(*texmap));
			}

			texmap[numTexMap].localTexId = (uint16_t)e;
			texmap[numTexMap].slotIdx = (uint16_t)f;
			++numTexMap;
		}
	}

	if (fin != stdin) {
		fclose(fin);
	}

	in.files = files;
	in.numFiles = numFiles;
	in.sources = sources;
	in.numSources = numSources;
	in.texmap = texmap;
	in.numTexMap = numTexMap;

	out = pdftWrite(&in, &outLen, err, sizeof(err));

	if (!out) {
		fprintf(stderr, "FAIL: %s\n", err);
		return 1;
	}

	fout = fopen(argv[2], "wb");

	if (!fout) {
		fprintf(stderr, "could not write %s\n", argv[2]);
		return 2;
	}

	fwrite(out, 1, outLen, fout);
	fclose(fout);
	printf("v%u %u files, %u sources, %u texmap, %u bytes\n",
			pdftVersionFor(&in), numFiles, numSources, numTexMap, outLen);
	return 0;
}
