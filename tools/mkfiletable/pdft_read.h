/**
 * PDFT decoder — the other half of pdft_write.c.
 *
 * Bytes in, structs out, no file I/O and no policy, for the same reason the
 * encoder has none. The layout it accepts is the layout pdft_write.c emits and
 * romdataParseFileTable() (port/src/romdata.c) reads; if the three ever
 * disagree, the engine is right and these two are wrong.
 *
 * Strings point into the caller's buffer rather than being copied, so the
 * buffer has to outlive the table. pdftFree() releases only the arrays.
 */
#ifndef _IN_PDFT_READ_H
#define _IN_PDFT_READ_H

#include <stdint.h>
#include <stdbool.h>
#include "pdft_write.h"

struct pdftTable {
	uint32_t version;

	struct pdftRomSource *sources;
	uint32_t numSources;

	struct pdftFile *files;
	uint32_t numFiles;

	/* The v3 romTexMap block is read past and not kept: the engine skips it
	 * the same way, so whatever an old table holds here is dead weight. The
	 * count survives so a checker can say a table still carries one. */
	uint32_t numTexMapSkipped;

	uint32_t trailing;   /* bytes after the last field; anything but 0 is a bug */
};

/**
 * Decodes a filetable. Returns false with the reason in err, and writes
 * nothing to out. A truncated table fails rather than returning what it
 * managed, because a half-read table is how a checker comes to believe a mod
 * is smaller than it is.
 */
bool pdftRead(const uint8_t *buf, uint32_t len, struct pdftTable *out, char *err, uint32_t errLen);

void pdftFree(struct pdftTable *t);

#endif
