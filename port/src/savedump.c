/*
 * --save-dump <file> [--save-json]: print what is on a save device, then exit.
 *
 * Reads the bytes straight off the file - a controller pak page (.mpk) or the
 * game pak (eeprom.bin) - without mounting it, so it shows a pak exactly as it
 * is on disk, broken records included. Each PD file record: where it is, its
 * type, id, generation, whether its header and body checksums hold, and which
 * generation of each file the game would load (live). Player and agent files
 * are decoded with the game's own wad readers' field order.
 *
 *   pd --save-dump paks/page-0000.mpk
 *   pd --save-dump eeprom.bin --save-json
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "types.h"
#include "constants.h"
#include "system.h"
#include "game/savebuffer.h"
#include "game/crc.h"
#include "savedump.h"

#define MPK_SIZE      0x8000
#define MPK_PAGE      0x100
#define MPK_NOTES     0x300
#define MPK_NUMNOTES  16
#define MPK_INODE_END 1

struct savedumpfile {
	u32 offset;
	struct pakfileheader h;
	s32 headerok;
	s32 bodyok;
	s32 live;
};

static u8 g_SaveDumpData[MPK_SIZE];
static u32 g_SaveDumpLen;
static u32 g_SaveDumpBlock;
static struct savedumpfile g_SaveDumpFiles[256];
static s32 g_SaveDumpNumFiles;

static const char *saveDumpTypeName(u32 type)
{
	switch (type) {
	case PAKFILETYPE_BLANK: return "blank";
	case PAKFILETYPE_TERMINATOR: return "terminator";
	case PAKFILETYPE_CAMERA: return "camera";
	case PAKFILETYPE_BOSS: return "boss";
	case PAKFILETYPE_MPPLAYER: return "mpplayer";
	case PAKFILETYPE_MPSETUP: return "mpsetup";
	case PAKFILETYPE_GAME: return "game";
	}

	return "unknown";
}

static u16 saveDumpBE16(const u8 *p)
{
	return (u16)(p[0] << 8 | p[1]);
}

/*
 * A controller pak page: follow the PD note's inode chain and lay its pages
 * end to end - that is the address space PD's own filesystem lives in.
 */
static s32 saveDumpFromMpk(const u8 *img, char *why, size_t whylen)
{
	for (s32 n = 0; n < MPK_NUMNOTES; n++) {
		const u8 *note = img + MPK_NOTES + n * 32;
		u16 page;
		s32 guard = 0;

		// game code NPDE (Perfect Dark, NTSC) / NPDP / NPDJ; publisher 4Y
		if (note[0] != 'N' || note[1] != 'P' || note[2] != 'D' || note[4] != '4' || note[5] != 'Y') {
			continue;
		}

		page = saveDumpBE16(note + 6);
		g_SaveDumpLen = 0;

		while (page >= 5 && page < MPK_SIZE / MPK_PAGE && guard++ < 128) {
			memcpy(g_SaveDumpData + g_SaveDumpLen, img + page * MPK_PAGE, MPK_PAGE);
			g_SaveDumpLen += MPK_PAGE;
			page = saveDumpBE16(img + MPK_PAGE + page * 2);
		}

		if (page != MPK_INODE_END) {
			snprintf(why, whylen, "inode chain ends on %d, not 1", page);
		}

		return 1;
	}

	snprintf(why, whylen, "no Perfect Dark note on this pak");
	return 0;
}

static void saveDumpWalk(void)
{
	u32 offset = 0;

	g_SaveDumpNumFiles = 0;

	while (offset + 0x10 <= g_SaveDumpLen && g_SaveDumpNumFiles < ARRAYCOUNT(g_SaveDumpFiles)) {
		struct savedumpfile *f = &g_SaveDumpFiles[g_SaveDumpNumFiles++];
		const u8 *raw = g_SaveDumpData + offset;
		u16 sum[2];

		f->offset = offset;
		memcpy(&f->h, raw, sizeof(f->h));

		crcCalculateU16Pair((u8 *)raw + 8, (u8 *)raw + 16, sum);
		f->headerok = sum[0] == f->h.headersum[0] && sum[1] == f->h.headersum[1];
		f->bodyok = -1;

		// a blank or freed record's body sum is not kept up; only check live ones
		if (f->headerok && f->h.occupied && f->h.bodylen && offset + 0x10 + f->h.bodylen <= g_SaveDumpLen) {
			crcCalculateU16Pair((u8 *)raw + 0x10, (u8 *)raw + 0x10 + f->h.bodylen, sum);
			f->bodyok = sum[0] == f->h.bodysum[0] && sum[1] == f->h.bodysum[1];
		}

		f->live = 0;

		if (!f->headerok || f->h.filetype == PAKFILETYPE_TERMINATOR || f->h.filelen == 0) {
			break;
		}

		offset += f->h.filelen;

		if (offset % g_SaveDumpBlock) {
			offset += g_SaveDumpBlock - offset % g_SaveDumpBlock;
		}
	}

	// what the game loads: per file id, the highest generation that is
	// occupied, finished writing and whole
	for (s32 i = 0; i < g_SaveDumpNumFiles; i++) {
		struct savedumpfile *f = &g_SaveDumpFiles[i];
		s32 best = 1;

		if (!f->headerok || !f->h.occupied || !f->h.writecompleted || f->bodyok == 0
				|| f->h.filetype == PAKFILETYPE_BLANK || f->h.filetype == PAKFILETYPE_TERMINATOR) {
			continue;
		}

		for (s32 j = 0; j < g_SaveDumpNumFiles; j++) {
			const struct savedumpfile *g = &g_SaveDumpFiles[j];

			if (j != i && g->headerok && g->h.occupied && g->h.writecompleted && g->bodyok != 0
					&& g->h.filetype == f->h.filetype && g->h.fileid == f->h.fileid
					&& g->h.generation > f->h.generation) {
				best = 0;
			}
		}

		f->live = best;
	}
}

static void saveDumpString(const char *s, s32 json)
{
	if (!json) {
		printf("'");
	} else {
		printf("\"");
	}

	for (; *s; s++) {
		u8 c = *s;

		if (c == '\n') {
			printf("\\n");
		} else if (c == '"' || c == '\\') {
			printf("\\%c", c);
		} else if (c < 0x20 || c >= 0x7f) {
			printf("\\u%04x", c);
		} else {
			putchar(c);
		}
	}

	printf(json ? "\"" : "'");
}

// name bytes as they sit on disk, so an empty or eaten name is visible
static void saveDumpRawName(const u8 *body, s32 json)
{
	printf(json ? "\"namebytes\":\"" : " namebytes=");

	for (s32 i = 0; i < 10; i++) {
		printf("%02x", body[i]);
	}

	if (json) {
		printf("\"");
	}
}

static void saveDumpMpPlayer(const u8 *body, u32 len, s32 json)
{
	struct savebuffer buffer;
	struct fileguid guid;
	char name[16] = {0};
	u32 v[24];
	static const char *names[] = {
		"time", "head", "body", "displayoptions", "kills", "deaths", "gamesplayed",
		"gameswon", "gameslost", "distance", "accuracy", "damagedealt", "painreceived",
		"headshots", "ammoused", "accuracymedals", "headshotmedals", "killmastermedals",
		"survivormedals", "controlmode", "options",
	};
	static const u8 bits[] = { 28, 7, 7, 8, 20, 20, 19, 19, 19, 25, 10, 26, 26, 20, 30, 18, 18, 18, 16, 2, 12 };

	savebufferClear(&buffer);
	memcpy(buffer.bytes, body, len < sizeof(buffer.bytes) ? len : sizeof(buffer.bytes));
	buffer.bitpos = 0;

	// mpplayerfileLoadWad's order
	savebufferReadString(&buffer, name, 0);
	v[0] = savebufferReadBits(&buffer, 28);
	v[1] = savebufferReadBits(&buffer, 7);
	v[2] = savebufferReadBits(&buffer, 7);
	savebufferReadGuid(&buffer, &guid);

	for (s32 i = 3; i < ARRAYCOUNT(bits); i++) {
		v[i] = savebufferReadBits(&buffer, bits[i]);
	}

	printf(json ? ",\"name\":" : " name=");
	saveDumpString(name, json);
	printf(json ? "," : "");
	saveDumpRawName(body, json);

	for (s32 i = 0; i < ARRAYCOUNT(bits); i++) {
		printf(json ? ",\"%s\":%u" : " %s=%u", names[i], v[i]);
	}

	printf(json ? ",\"headguid\":\"%x-%x\"" : " headguid=%x-%x", guid.deviceserial, guid.fileid);
}

static void saveDumpGame(const u8 *body, u32 len, s32 json)
{
	struct savebuffer buffer;
	char name[16] = {0};
	u32 stage, time, difficulty;

	savebufferClear(&buffer);
	memcpy(buffer.bytes, body, len < sizeof(buffer.bytes) ? len : sizeof(buffer.bytes));
	buffer.bitpos = 0;

	// gamefileGetOverview's order
	savebufferReadString(&buffer, name, 0);
	stage = savebufferReadBits(&buffer, 5);
	time = savebufferReadBits(&buffer, 32);
	difficulty = savebufferReadBits(&buffer, 2);

	printf(json ? ",\"name\":" : " name=");
	saveDumpString(name, json);
	printf(json ? "," : "");
	saveDumpRawName(body, json);
	printf(json ? ",\"stage\":%u,\"time\":%u,\"difficulty\":%u" : " stage=%u time=%u difficulty=%u", stage, time, difficulty);
}

static void saveDumpPrint(const char *path, s32 json, const char *why)
{
	if (json) {
		printf("{\"path\":");
		saveDumpString(path, 1);
		printf(",\"bytes\":%u,\"block\":%u", g_SaveDumpLen, g_SaveDumpBlock);
		if (why[0]) {
			printf(",\"warning\":");
			saveDumpString(why, 1);
		}
		printf(",\"files\":[");
	} else {
		printf("%s: %u bytes of PD filesystem, block 0x%x%s%s\n", path, g_SaveDumpLen, g_SaveDumpBlock,
				why[0] ? " - " : "", why);
	}

	for (s32 i = 0; i < g_SaveDumpNumFiles; i++) {
		const struct savedumpfile *f = &g_SaveDumpFiles[i];
		const u8 *body = g_SaveDumpData + f->offset + 0x10;
		const u32 bodylen = f->h.bodylen;

		if (json) {
			printf("%s{\"offset\":%u,\"type\":\"%s\",\"fileid\":%u,\"serial\":\"%x\",\"generation\":%u,"
					"\"occupied\":%u,\"writecompleted\":%u,\"headerok\":%d,\"bodyok\":%d,\"live\":%d,"
					"\"bodylen\":%u,\"filelen\":%u",
					i ? "," : "", f->offset, saveDumpTypeName(f->h.filetype), f->h.fileid, f->h.deviceserial,
					f->h.generation, f->h.occupied, f->h.writecompleted, f->headerok, f->bodyok, f->live,
					f->h.bodylen, f->h.filelen);
		} else {
			printf("%s %05x %-10s id=%-3u serial=%x gen=%-3u occ=%u done=%u header=%s body=%s len=%u",
					f->live ? "*" : " ", f->offset, f->headerok ? saveDumpTypeName(f->h.filetype) : "?",
					f->h.fileid, f->h.deviceserial, f->h.generation, f->h.occupied, f->h.writecompleted,
					f->headerok ? "ok" : "BAD", f->bodyok < 0 ? "-" : f->bodyok ? "ok" : "BAD", f->h.bodylen);
		}

		if (f->headerok && f->h.occupied && f->bodyok != 0 && f->offset + 0x10 + bodylen <= g_SaveDumpLen) {
			if (f->h.filetype == PAKFILETYPE_MPPLAYER) {
				saveDumpMpPlayer(body, bodylen, json);
			} else if (f->h.filetype == PAKFILETYPE_GAME) {
				saveDumpGame(body, bodylen, json);
			}
		}

		printf(json ? "}" : "\n");
	}

	if (json) {
		printf("]}\n");
	} else {
		printf("(* = the generation the game loads)\n");
	}
}

void saveDumpFromArgs(void)
{
	const char *path = sysArgGetString("--save-dump");
	const s32 json = sysArgCheck("--save-json");
	char why[96] = "";
	FILE *fp;
	u8 *img;
	long size;

	if (!path || !path[0]) {
		return;
	}

	fp = fopen(path, "rb");

	if (!fp) {
		fprintf(stderr, "--save-dump %s: can't open\n", path);
		_exit(2);
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (size <= 0 || size > MPK_SIZE) {
		fprintf(stderr, "--save-dump %s: %ld bytes is neither a controller pak nor an eeprom\n", path, size);
		_exit(2);
	}

	img = calloc(1, MPK_SIZE);

	if (fread(img, 1, size, fp) != (size_t)size) {
		fprintf(stderr, "--save-dump %s: short read\n", path);
		_exit(2);
	}

	fclose(fp);

	if (size == MPK_SIZE) {
		// a controller pak: 0x20 blocks inside the PD note
		g_SaveDumpBlock = 0x20;

		if (!saveDumpFromMpk(img, why, sizeof(why))) {
			fprintf(stderr, "--save-dump %s: %s\n", path, why);
			_exit(1);
		}
	} else {
		// the game pak (eeprom): PD's filesystem from byte 0, 0x10 blocks
		g_SaveDumpBlock = 0x10;
		memcpy(g_SaveDumpData, img, size);
		g_SaveDumpLen = size;
	}

	saveDumpWalk();
	saveDumpPrint(path, json, why);
	fflush(stdout);
	// before anything has touched pd.ini or the saves
	_exit(0);
}
