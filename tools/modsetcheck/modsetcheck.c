/**
 * modsetcheck — check a SET of mods before shipping it.
 *
 *   modsetcheck <mod-dir> [<mod-dir> ...]
 *
 * The dirs go in the order they would be mounted, because order decides both
 * texture slot allocation and who wins a repeated name.
 *
 * WHY THIS EXISTS. A mod author sees their own mod; the person assembling a
 * distribution sees the combination, and is therefore the only one who CAN
 * catch the things below. Every other tool here takes one mod: mkfiletable
 * takes one --workspace, and every pdt subcommand is single-mod too. Until
 * this, the only whole-set check was booting the game and reading the log,
 * which is literally how the texture slot overflow was found.
 *
 * WHAT IT DOES NOT CHECK, deliberately. Two mods using the same local texture
 * id, or the same file id, is NOT a collision and never was: a mod's bytes
 * carry local ids and the loader remaps them per mod through
 * modTexMapLookup(modIdx, localTexId), while file ids carry their owner in the
 * high 16 bits. Addressing is solved by design. Do not add a check for it.
 *
 * What is not solved is NAMES - heads, bodies, stages and ROM source ids are
 * matched by string with no owner column, so a repeat is a silent replace -
 * and BUDGET, the one shared texture slot pool, which no amount of coordination
 * between authors can fix because it depends on what else is loaded.
 *
 * STAGES are the second budget. A stage block either names the row it wants
 * (`stage 0x26`, `stage "STAGE_28"`) or gives the level its own name
 * (`stage "gex_arec"`) and lets the loader find it a STAGE_EXTRA row, recorded
 * in pd.ini under [MpStageSlots]. This tool resolves all three spellings the
 * way the loader does, reports rows with two occupants whichever spelling
 * reached them, checks the free STAGE_EXTRA rows against the names that need
 * one, and - with --ini - reads what the last boot actually allocated.
 * --stage-table writes the whole picture out as JSON: the stage twin of
 * mpHeadsAndBodiesTable.json, generated from the packs rather than typed.
 *
 * NOTE ON A WORD: the code calls a slot a texture "port" (MOD_TEX_PORT_BASE,
 * g_NextGlobalTexPort). This tool says SLOT, and its output says slot.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pdft_read.h"

#define PATHMAX 4096
/* Room for PATHMAX plus the longest name joined onto it, so a composed path is
 * never silently shortened into a file that exists. */
#define JOINMAX (PATHMAX + 64)

/* MOD_TEX_PORT_BASE in romdata.c, and the width a texture slot has inside a
 * G_NOOP display-list command, which is where the ceiling actually comes from.
 * Taken from gbiex.h rather than restated, so this tool cannot tell a mod
 * author the set fits when the engine will disagree. That width was 12 bits,
 * with a base of 3600, leaving 496 slots above it for every mod on disk
 * together; it is now 15 bits based at 4096, for 28672.
 *
 * 4096 is one past the largest id a 12-bit slot could encode, so it is the
 * line between an id a mod authored and an id the loader assigned. See the
 * comment on MOD_TEX_PORT_BASE for why that line has to exist. */
#include "../../src/include/gbiex.h"

#define DEFAULT_SLOT_BASE 4096u
#define DEFAULT_SLOT_MAX  G_NOOP_TEXSLOT_MAX

/* romdata.c:180. The table is global across every mounted mod, not per mod. */
#define ROMSOURCES_MAX 8

/* romdata.c: struct romsource carries char id[16], filled with strncpy at
 * sizeof - 1, so an id is compared on its first 15 characters and no more. */
#define ROMSOURCE_ID_CHARS 15

#define MAX_MODS 64

enum level { LVL_NOTE, LVL_WARN, LVL_ERROR };

static int g_Errors;
static int g_Warnings;

static void finding(enum level lvl, const char *fmt, ...)
{
	va_list ap;

	switch (lvl) {
	case LVL_ERROR: printf("ERROR   "); ++g_Errors; break;
	case LVL_WARN:  printf("WARNING "); ++g_Warnings; break;
	default:        printf("note    "); break;
	}

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "modsetcheck: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(2);
}

/* -- one mod --------------------------------------------------------------- */

struct claim {
	char name[128];
	int mod;        /* index into the set */
	int line;       /* modconfig.txt line, 0 if it came from the filetable */
};

/* -- stage declarations ---------------------------------------------------- */

/* The STAGE_* names and values, which rows exist, and the vanilla file names,
 * generated at build time from the engine's own tables so this tool resolves a
 * stage spec exactly as modConfigParseStage does. See gen-stagenames.sh. */
#include "stagenames.h"

/**
 * The three spellings of `stage <spec> {`, in the order the loader tries them
 * (port/src/mod.c modConfigParseStage):
 *   a number      `stage 0x26`          the modconfig picked the row
 *   a row name    `stage "STAGE_28"`    the modconfig picked the row, by name
 *   its own name  `stage "gex_arec"`    the loader picks a free STAGE_EXTRA row
 *                                       and records name -> row in pd.ini
 *                                       under [MpStageSlots]
 * The first two are POSITIONAL: the block says which row it wants. Only the
 * third says "this is a stage, find it a slot".
 */
enum stagespec { SPEC_NUMBER, SPEC_ROWNAME, SPEC_NAME };

static const char *const g_SpecWords[] = { "stagenum", "rowname", "name" };

#define OWNER_NONE    (-1)   /* resolved nowhere */
#define OWNER_VANILLA (-2)   /* a stock file from the base ROM table */

struct fileref {
	char name[128];      /* as written, unquoted; empty when the key is absent */
	int owner;           /* mod index it resolved in, or OWNER_* */
	uint32_t id;         /* file id inside that owner's table */
	long bytes;          /* size of the loose file, -1 when there is none to measure */
	int others;          /* how many OTHER mods also carry this name */
};

enum { FK_BG, FK_TILES, FK_PADS, FK_SETUP, FK_MPSETUP, FK_COUNT };

static const char *const g_FileKeys[FK_COUNT] = {
	"bgfile", "tilesfile", "padsfile", "setupfile", "mpsetupfile"
};

struct stageblock {
	char spec[128];
	enum stagespec kind;
	int stagenum;         /* -1 when the loader allocates it */
	const char *rowname;  /* the STAGE_* row it lands on; NULL when allocated */
	bool extra;           /* that row is a STAGE_EXTRA row: placeholder content */
	int line;
	char kindstr[16];     /* solo | mp | both | none; empty when absent */
	char arenaname[128];
	struct fileref files[FK_COUNT];
	int reserved;         /* row [MpStageSlots] holds for this spec, -1 if none */
};

/* One [MpStageSlots] line from pd.ini. */
struct reservation {
	char name[128];
	int slot;
	int block;            /* index of the declaring block in the flattened set, -1 if none */
	int mod;
};

static const struct stagename *stageByName(const char *name)
{
	size_t i;

	for (i = 0; i < MSC_NUM_STAGENAMES; ++i) {
		const char *a = g_StageNameTable[i].name, *b = name;

		while (*a && *b) {
			int ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
			int cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;

			if (ca != cb) {
				break;
			}

			++a;
			++b;
		}

		if (!*a && !*b) {
			return &g_StageNameTable[i];
		}
	}

	return NULL;
}

/* First row carrying this id, as stageGetIndex returns the first match. */
static const struct stagename *stageByNum(int stagenum)
{
	size_t i;

	for (i = 0; i < MSC_NUM_STAGENAMES; ++i) {
		if (g_StageNameTable[i].hasrow && g_StageNameTable[i].stagenum == stagenum) {
			return &g_StageNameTable[i];
		}
	}

	return NULL;
}

/* The loader's own test (modStageSlotUsable): the prefix AND a digit, because
 * STAGE_EXTRACTION shares the first eleven characters and is a real mission. */
static bool isExtraRow(const char *rowname)
{
	return rowname && !strncmp(rowname, "STAGE_EXTRA", 11)
			&& rowname[11] >= '0' && rowname[11] <= '9';
}

/* The FILE_* token a g_Stages row carries in one of the five file columns. */
static const char *rowFile(const char *rowname, int key)
{
	size_t i;

	if (!rowname) {
		return "file, whichever row the loader allocates";
	}

	for (i = 0; i < MSC_NUM_STAGEROWS; ++i) {
		if (!strcmp(g_StageRows[i].name, rowname)) {
			return g_StageRows[i].files[key];
		}
	}

	return "file";
}

struct mod {
	char dir[PATHMAX];
	const char *label;    /* the dir's last component, which is what a reader recognises */
	uint8_t *blob;
	uint32_t blobLen;
	struct pdftTable ft;
	bool haveFt;

	/* from modconfig.txt. Grown rather than fixed: gex declares 112 heads and
	 * a fixed array big enough for anything put megabytes per mod on the
	 * stack, which is a segfault and not a limit. */
	char modname[128];
	struct claim *heads;
	uint32_t numHeads, capHeads;
	struct stageblock *blocks;
	uint32_t numBlocks, capBlocks;

	/* worked out */
	uint32_t slotBase;
	uint32_t slotMax;     /* highest slot index the fragment uses */
	bool hasSlots;
};

static void pushClaim(struct claim **arr, uint32_t *n, uint32_t *cap, const char *name, int line)
{
	if (*n == *cap) {
		*cap = *cap ? *cap * 2 : 32;
		*arr = realloc(*arr, (size_t)*cap * sizeof(**arr));

		if (!*arr) {
			die("out of memory reading a modconfig");
		}
	}

	snprintf((*arr)[*n].name, sizeof((*arr)[0].name), "%s", name);
	(*arr)[*n].line = line;
	++*n;
}

static const char *baseName(const char *path)
{
	const char *s = strrchr(path, '/');

	if (s && s[1]) {
		return s + 1;
	}

	return path;
}

static uint8_t *slurp(const char *path, uint32_t *outLen)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	long len;

	if (!f) {
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}

	buf = malloc((size_t)len + 1);

	if (!buf) {
		fclose(f);
		die("out of memory reading %s", path);
	}

	if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
		free(buf);
		fclose(f);
		return NULL;
	}

	buf[len] = '\0';
	fclose(f);
	*outLen = (uint32_t)len;
	return buf;
}

/**
 * The one quoted argument of a modconfig line, unquoted. modconfig.txt is a
 * flat brace format and this only needs the handful of keys below, so it is
 * read line by line rather than properly parsed - a real parser lives in
 * port/src/mod.c and is not worth a second copy for four keys.
 */
static bool quotedArg(const char *line, const char *key, char *out, size_t outLen)
{
	const char *p = line;
	const char *q, *e;
	size_t n = strlen(key);

	while (*p == ' ' || *p == '\t') {
		++p;
	}

	if (strncmp(p, key, n) || (p[n] != ' ' && p[n] != '\t')) {
		return false;
	}

	q = strchr(p + n, '"');

	if (!q) {
		return false;
	}

	e = strchr(q + 1, '"');

	if (!e || (size_t)(e - q - 1) >= outLen) {
		return false;
	}

	memcpy(out, q + 1, (size_t)(e - q - 1));
	out[e - q - 1] = '\0';
	return true;
}

/**
 * The value of `key` on this line, quoted or bare, with a trailing `{` ignored
 * so `stage "gex_arec" {` and `stage 0x26 {` both yield the spec. Returns false
 * when the line is not that key.
 */
static bool keyValue(const char *line, const char *key, char *out, size_t outLen)
{
	const char *p = line;
	size_t n = strlen(key), len;

	while (*p == ' ' || *p == '\t') {
		++p;
	}

	if (strncmp(p, key, n) || (p[n] != ' ' && p[n] != '\t')) {
		return false;
	}

	p += n;

	while (*p == ' ' || *p == '\t') {
		++p;
	}

	if (*p == '"') {
		const char *e = strchr(p + 1, '"');

		if (!e) {
			return false;
		}

		++p;
		len = (size_t)(e - p);
	} else {
		len = 0;

		while (p[len] && p[len] != ' ' && p[len] != '\t' && p[len] != '{'
				&& p[len] != '\r' && p[len] != '#') {
			++len;
		}
	}

	if (!len || len >= outLen) {
		return false;
	}

	memcpy(out, p, len);
	out[len] = '\0';
	return true;
}

static struct stageblock *pushBlock(struct mod *m)
{
	struct stageblock *b;
	int k;

	if (m->numBlocks == m->capBlocks) {
		m->capBlocks = m->capBlocks ? m->capBlocks * 2 : 16;
		m->blocks = realloc(m->blocks, (size_t)m->capBlocks * sizeof(*m->blocks));

		if (!m->blocks) {
			die("out of memory reading a modconfig");
		}
	}

	b = &m->blocks[m->numBlocks++];
	memset(b, 0, sizeof(*b));
	b->stagenum = -1;
	b->reserved = -1;

	for (k = 0; k < FK_COUNT; ++k) {
		b->files[k].owner = OWNER_NONE;
		b->files[k].bytes = -1;
	}

	return b;
}

/**
 * Resolve a spec the way modConfigParseStage does. A number or a row name is
 * positional and must have a g_Stages row behind it; anything else is the
 * mod's own name for a level and the loader will find it a STAGE_EXTRA row.
 */
static void resolveSpec(struct mod *m, struct stageblock *b)
{
	const struct stagename *sn;

	if (b->spec[0] >= '0' && b->spec[0] <= '9') {
		char *end = NULL;
		long v = strtol(b->spec, &end, 0);

		b->kind = SPEC_NUMBER;

		if (!end || *end || v <= 0x01 || v >= MSC_STAGE_TITLE) {
			finding(LVL_ERROR, "%s: modconfig.txt:%d: stage %s is not a level number "
					"(0x02..0x%02x); the loader rejects the block", m->label, b->line,
					b->spec, MSC_STAGE_TITLE - 1);
			return;
		}

		sn = stageByNum((int)v);

		if (!sn) {
			finding(LVL_ERROR, "%s: modconfig.txt:%d: stage %s has no g_Stages row; the "
					"loader skips the block", m->label, b->line, b->spec);
			return;
		}

		b->stagenum = (int)v;
		b->rowname = sn->name;
		b->extra = isExtraRow(sn->name);
		return;
	}

	sn = stageByName(b->spec);

	if (sn && sn->hasrow) {
		b->kind = SPEC_ROWNAME;
		b->stagenum = sn->stagenum;
		b->rowname = sn->name;
		b->extra = isExtraRow(sn->name);
		return;
	}

	b->kind = SPEC_NAME;

	if (sn) {
		/* STAGE_TITLE, STAGE_MP_RANDOM and friends: a constant with no row.
		 * stageGetIndexByName misses it, so the loader treats it as a NEW
		 * level name and hands it an extra row - almost certainly not what
		 * the author meant by spelling a STAGE_ constant. */
		finding(LVL_WARN, "%s: modconfig.txt:%d: stage \"%s\" is a STAGE_ constant with no "
				"stage table row, so the loader will treat it as a new level name and "
				"allocate a STAGE_EXTRA row for it", m->label, b->line, b->spec);
	}
}

/**
 * HeadsAndBodies blocks and stage blocks, with the line each came from so a
 * finding can point at it. The head scan is flat, as before: a `name` key
 * only appears inside HeadsAndBodies in every modconfig shipped. Stage blocks
 * are followed by brace depth, because their keys (bgfile, kind, ...) have to
 * be attributed to the block they sit in.
 */
static void readModConfig(struct mod *m)
{
	char path[JOINMAX];
	uint32_t len = 0;
	char *text, *line, *next;
	int lineNo = 0;
	bool inHeads = false;
	struct stageblock *cur = NULL;
	int depth = 0;

	snprintf(path, sizeof(path), "%s/modconfig.txt", m->dir);
	text = (char *)slurp(path, &len);

	if (!text) {
		finding(LVL_WARN, "%s: no modconfig.txt, so its head, body and stage claims "
				"cannot be checked", m->label);
		return;
	}

	/* Split on newlines by hand. strtok_r is POSIX and strtok is not
	 * reentrant; this has to build wherever the rest of tools/ does. */
	for (line = text; line && *line; line = next) {
		char arg[128];
		const char *p = line;
		const char *c;
		char *nl = strchr(line, '\n');

		if (nl) {
			*nl = '\0';
			next = nl + 1;
		} else {
			next = NULL;
		}

		++lineNo;

		while (*p == ' ' || *p == '\t') {
			++p;
		}

		if (*p == '#') {
			continue;
		}

		if (!cur && keyValue(line, "stage", arg, sizeof(arg))) {
			cur = pushBlock(m);
			snprintf(cur->spec, sizeof(cur->spec), "%s", arg);
			cur->line = lineNo;
			resolveSpec(m, cur);
			depth = 0;
			/* Fall through: the brace, and on a one-line block the keys, may
			 * sit on this same line. If neither does, depth stays 0 and the
			 * following lines belong to this block until its brace closes. */
		}

		if (cur) {
			int k;

			for (k = 0; k < FK_COUNT; ++k) {
				if (keyValue(line, g_FileKeys[k], arg, sizeof(arg))) {
					snprintf(cur->files[k].name, sizeof(cur->files[k].name), "%s", arg);
				}
			}

			if (keyValue(line, "kind", arg, sizeof(arg))) {
				snprintf(cur->kindstr, sizeof(cur->kindstr), "%.15s", arg);
			}

			if (keyValue(line, "arenaname", arg, sizeof(arg))) {
				snprintf(cur->arenaname, sizeof(cur->arenaname), "%s", arg);
			}

			for (c = p; *c && *c != '#'; ++c) {
				if (*c == '{') {
					++depth;
				} else if (*c == '}' && --depth <= 0) {
					cur = NULL;
					break;
				}
			}

			continue;
		}

		if (!strncmp(p, "HeadsAndBodies", 14)) {
			inHeads = true;
			continue;
		}

		if (*p == '}') {
			inHeads = false;
			continue;
		}

		if (quotedArg(line, "modname", arg, sizeof(arg))) {
			snprintf(m->modname, sizeof(m->modname), "%s", arg);
			continue;
		}

		if (inHeads && quotedArg(line, "name", arg, sizeof(arg))) {
			pushClaim(&m->heads, &m->numHeads, &m->capHeads, arg, lineNo);
			continue;
		}
	}

	free(text);
}

static void readFileTable(struct mod *m)
{
	char path[JOINMAX];
	char err[256];

	snprintf(path, sizeof(path), "%s/filetable.dat", m->dir);
	m->blob = slurp(path, &m->blobLen);

	if (!m->blob) {
		finding(LVL_ERROR, "%s: no filetable.dat. A mod dir without one contributes no "
				"files and no textures; build it with mkfiletable", m->label);
		return;
	}

	if (!pdftRead(m->blob, m->blobLen, &m->ft, err, sizeof(err))) {
		finding(LVL_ERROR, "%s: filetable.dat will not decode: %s", m->label, err);
		return;
	}

	m->haveFt = true;

	if (m->ft.trailing) {
		finding(LVL_WARN, "%s: %u bytes after the end of the table. The reader stops where "
				"the fields stop, so they are ignored - but nothing should write them",
				m->label, m->ft.trailing);
	}
}

/* -- the checks ------------------------------------------------------------ */

/**
 * Every texmap slot index a mod uses, checked the way the reader treats them.
 *
 * Two entries on one slot are two different things and this used to call them
 * both aliasing. If the local ids DIFFER, two textures answer to one slot and
 * one draws for both - a real fault in the mod. If the local id is the SAME,
 * the row is simply written twice: modTexMapLookup() finds the same answer
 * either way and nothing draws wrong. Only the first is an error.
 *
 * The distinction is not academic. mod_gex_characters carries 184 repeated
 * rows over 92 slots, every one of them the same id twice and not one a real
 * collision, and calling that an ERROR meant the shipped roster failed its own
 * whole-set check for a non-reason - which is the fastest way to teach
 * everyone to ignore the tool.
 *
 * Repeated rows are still worth saying, because pdftWrite() refuses any slot
 * used twice whatever the ids are, so a table carrying them cannot be rebuilt
 * by mkfiletable without collapsing them first.
 *
 * A hole reserves a slot nobody uses, because the reader advances the next
 * mod's base by maxSlot + 1 and not by the entry count (romdata.c:909).
 */
static void checkTexMap(struct mod *m)
{
	uint32_t i, maxSlot = 0, distinct = 0;
	uint32_t repeatRows = 0, repeatSlots = 0;
	uint32_t aliasRows = 0, aliasSlots = 0;
	uint32_t firstAliasSlot = 0;
	bool haveAliasSlot = false;
	uint8_t *seen;
	uint16_t *firstId;

	if (!m->haveFt || !m->ft.numTexMap) {
		return;
	}

	for (i = 0; i < m->ft.numTexMap; ++i) {
		if (m->ft.texmap[i].slotIdx > maxSlot) {
			maxSlot = m->ft.texmap[i].slotIdx;
		}
	}

	seen = calloc((size_t)maxSlot + 1, 1);
	firstId = calloc((size_t)maxSlot + 1, sizeof(*firstId));

	if (!seen || !firstId) {
		die("out of memory checking %s", m->label);
	}

	for (i = 0; i < m->ft.numTexMap; ++i) {
		uint32_t s = m->ft.texmap[i].slotIdx;
		uint16_t id = m->ft.texmap[i].localTexId;

		if (!seen[s]) {
			++distinct;
			firstId[s] = id;
		} else if (id == firstId[s]) {
			++repeatRows;

			if (seen[s] == 1) {
				++repeatSlots;
			}
		} else {
			++aliasRows;

			if (seen[s] == 1) {
				++aliasSlots;
			}

			if (!haveAliasSlot) {
				firstAliasSlot = s;
				haveAliasSlot = true;
			}
		}

		if (seen[s] < 255) {
			++seen[s];
		}
	}

	m->slotMax = maxSlot;
	m->hasSlots = true;

	if (aliasRows) {
		finding(LVL_ERROR, "%s: %u texmap entr%s put a DIFFERENT local id on a slot "
				"another already uses, over %u slot(s), first at slot %u. One texture "
				"draws for both and the mod is wrong, not just redundant",
				m->label, aliasRows, aliasRows == 1 ? "y" : "ies", aliasSlots,
				firstAliasSlot);
	}

	if (repeatRows) {
		finding(LVL_WARN, "%s: %u texmap row(s) repeat a mapping already made, over %u "
				"slot(s) - %u distinct slots for %u rows. Harmless to draw, because the "
				"id maps to the same slot either way, but pdftWrite() refuses any slot "
				"used twice, so this table cannot be rebuilt by mkfiletable until they "
				"are collapsed",
				m->label, repeatRows, repeatSlots, distinct, m->ft.numTexMap);
	}

	if (maxSlot + 1 > m->ft.numTexMap) {
		finding(LVL_WARN, "%s: %u texmap entries but slots run up to %u, so %u slot(s) are "
				"reserved and unused. The next mod starts above the highest slot used, "
				"not above the count",
				m->label, m->ft.numTexMap, maxSlot, maxSlot + 1 - m->ft.numTexMap);
	}

	free(seen);
	free(firstId);
}

/**
 * The shared slot pool, walked exactly as romdataLoadModFileTable does: each
 * mod's base is the running cursor, its slots map to base + slotIdx, and the
 * cursor then moves to base + maxSlot + 1.
 */
static void checkSlotBudget(struct mod *mods, int n, uint32_t base, uint32_t cap)
{
	uint32_t cursor = base;
	uint32_t total = 0;
	int i;

	printf("\ntexture slots, in mount order (base %u, ceiling %u, %u available)\n",
			base, cap, cap - base + 1);

	for (i = 0; i < n; ++i) {
		struct mod *m = &mods[i];
		uint32_t last;

		if (!m->hasSlots) {
			printf("  %-24s no textures\n", m->label);
			continue;
		}

		m->slotBase = cursor;
		last = cursor + m->slotMax;
		total += m->slotMax + 1;

		printf("  %-24s %5u..%-5u  %5u slot(s)%s\n", m->label, cursor, last,
				m->slotMax + 1, last > cap ? "   PAST THE CEILING" : "");

		if (last > cap) {
			if (cursor > cap) {
				finding(LVL_ERROR, "%s: every one of its slots is past %u, so none of its "
						"textures can draw. It is not first over the line - something "
						"before it already filled the pool", m->label, cap);
			} else {
				finding(LVL_ERROR, "%s: crosses the ceiling at slot %u; its last %u "
						"texture(s) cannot draw", m->label, cap,
						last - cap);
			}
		}

		cursor = last + 1;
	}

	printf("  %-24s %20u total demanded, %u available\n", "", total, cap - base + 1);

	if (total > cap - base + 1) {
		finding(LVL_ERROR, "the set demands %u slots and there are %u. This is not an "
				"ordering problem - reordering only chooses which mod loses",
				total, cap - base + 1);
	}
}

/** A repeated head or body name is a silent replace: g_ModHeadNames has no owner column. */
static void checkNameClaims(struct mod *mods, int n)
{
	int i, j;
	uint32_t a, b;

	for (i = 0; i < n; ++i) {
		for (a = 0; a < mods[i].numHeads; ++a) {
			for (j = i + 1; j < n; ++j) {
				for (b = 0; b < mods[j].numHeads; ++b) {
					if (strcmp(mods[i].heads[a].name, mods[j].heads[b].name)) {
						continue;
					}

					finding(LVL_ERROR, "head/body name \"%s\" is claimed by %s "
							"(modconfig.txt:%d) and %s (:%d). The later mount wins "
							"silently - nothing reports it and nothing logs it",
							mods[i].heads[a].name, mods[i].label, mods[i].heads[a].line,
							mods[j].label, mods[j].heads[b].line);
				}
			}
		}
	}
}

/**
 * ROM source ids across the whole set. Three separate failures: the table is
 * global and holds eight; ids are compared on their first 15 characters; and
 * when a later fragment declares an id that already exists, the parser takes
 * the existing entry and never applies the second declaration's filename. The
 * second mod's entries then read the right offsets out of the wrong ROM, with
 * nothing logged.
 */
static void checkRomSources(struct mod *mods, int n)
{
	struct { char id[64]; char trunc[ROMSOURCE_ID_CHARS + 1]; const char *file; int mod; } seen[64];
	int numSeen = 0;
	int i;
	uint32_t k;

	for (i = 0; i < n; ++i) {
		if (!mods[i].haveFt) {
			continue;
		}

		for (k = 0; k < mods[i].ft.numSources; ++k) {
			const struct pdftRomSource *rs = &mods[i].ft.sources[k];
			char trunc[ROMSOURCE_ID_CHARS + 1];
			int j;
			bool merged = false;

			if (!rs->id) {
				continue;
			}

			snprintf(trunc, sizeof(trunc), "%s", rs->id);

			if (strlen(rs->id) > ROMSOURCE_ID_CHARS) {
				finding(LVL_WARN, "%s: romSource id \"%s\" is longer than 15 characters and "
						"is stored as \"%s\". Everything past that is not compared",
						mods[i].label, rs->id, trunc);
			}

			for (j = 0; j < numSeen; ++j) {
				if (strcmp(seen[j].trunc, trunc)) {
					continue;
				}

				merged = true;

				if (seen[j].file && rs->filename && strcmp(seen[j].file, rs->filename)) {
					finding(LVL_ERROR, "romSource \"%s\" is declared by %s as %s and by %s "
							"as %s. The first declaration wins outright - the second mod's "
							"entries resolve against the first mod's ROM, at offsets "
							"computed for a different file, and nothing is logged",
							trunc, mods[seen[j].mod].label, seen[j].file,
							mods[i].label, rs->filename);
				} else {
					finding(LVL_NOTE, "romSource \"%s\" is shared by %s and %s, same file. "
							"One entry, which is what the reader intends",
							trunc, mods[seen[j].mod].label, mods[i].label);
				}

				break;
			}

			if (!merged && numSeen < (int)(sizeof(seen) / sizeof(seen[0]))) {
				snprintf(seen[numSeen].id, sizeof(seen[0].id), "%s", rs->id);
				snprintf(seen[numSeen].trunc, sizeof(seen[0].trunc), "%s", trunc);
				seen[numSeen].file = rs->filename;
				seen[numSeen].mod = i;
				++numSeen;
			}
		}
	}

	if (numSeen > ROMSOURCES_MAX) {
		finding(LVL_ERROR, "the set declares %d distinct romSources and the table holds %d. "
				"The ones past the limit are dropped with a warning at load",
				numSeen, ROMSOURCES_MAX);
	}
}

/** Files whose bytes come from a romSource this mod never declared. */
static void checkAltRefs(struct mod *m)
{
	uint32_t i;

	if (!m->haveFt) {
		return;
	}

	for (i = 0; i < m->ft.numFiles; ++i) {
		const struct pdftFile *f = &m->ft.files[i];

		if (f->alt.romIdx < 0) {
			continue;
		}

		if ((uint32_t)f->alt.romIdx >= m->ft.numSources) {
			finding(LVL_ERROR, "%s: '%s' sources from romSource %d and the fragment declares "
					"only %u", m->label, f->name ? f->name : "?", f->alt.romIdx,
					m->ft.numSources);
		} else if (!f->alt.size) {
			finding(LVL_WARN, "%s: '%s' sources from a ROM with a size of zero, so it will "
					"load nothing", m->label, f->name ? f->name : "?");
		}
	}
}

/* -- stages ---------------------------------------------------------------- */

static long fileBytes(const char *path)
{
	FILE *f = fopen(path, "rb");
	long len = -1;

	if (!f) {
		return -1;
	}

	if (fseek(f, 0, SEEK_END) == 0) {
		len = ftell(f);
	}

	fclose(f);
	return len;
}

static const char *afterSlash(const char *name)
{
	const char *s = strrchr(name, '/');

	return s ? s + 1 : name;
}

/**
 * A name against one mod's filetable, the way romdataFileGetNumForNameInMod
 * reads it: the name or its alias, in full and then by the part after the
 * last slash.
 */
static const struct pdftFile *ftFind(const struct mod *m, const char *name)
{
	const char *base = afterSlash(name);
	uint32_t i;
	int pass;

	if (!m->haveFt) {
		return NULL;
	}

	for (pass = 0; pass < 2; ++pass) {
		const char *want = pass ? base : name;

		if (pass && base == name) {
			break;
		}

		for (i = 0; i < m->ft.numFiles; ++i) {
			const struct pdftFile *f = &m->ft.files[i];

			if ((f->name && !strcmp(f->name, want)) || (f->alias && !strcmp(f->alias, want))) {
				return f;
			}
		}
	}

	return NULL;
}

static bool vanillaHas(const char *name)
{
	const char *base = afterSlash(name);
	size_t i;

	for (i = 0; i < MSC_NUM_VANILLA_FILES; ++i) {
		if (!strcmp(g_VanillaFileNames[i], name) || !strcmp(g_VanillaFileNames[i], base)) {
			return true;
		}
	}

	return false;
}

/**
 * Where each file a stage block names would come from. The loader's order is
 * own mod, then any mounted mod (with a note, and a warning when more than one
 * has it), then the base ROM table. Only a loose file in a mod dir has a size
 * to measure; that is what the stub check needs.
 */
static void resolveStageFiles(struct mod *mods, int n)
{
	int i, j, k;
	uint32_t b;

	for (i = 0; i < n; ++i) {
		for (b = 0; b < mods[i].numBlocks; ++b) {
			struct stageblock *blk = &mods[i].blocks[b];

			for (k = 0; k < FK_COUNT; ++k) {
				struct fileref *fr = &blk->files[k];
				const struct pdftFile *f;
				int owner = OWNER_NONE;

				if (!fr->name[0]) {
					continue;
				}

				f = ftFind(&mods[i], fr->name);

				if (f) {
					owner = i;
				}

				for (j = 0; j < n; ++j) {
					const struct pdftFile *g;

					if (j == i) {
						continue;
					}

					g = ftFind(&mods[j], fr->name);

					if (g) {
						++fr->others;

						if (!f) {
							f = g;
							owner = j;
						}
					}
				}

				if (f) {
					char path[JOINMAX];

					fr->owner = owner;
					fr->id = f->id;

					if (f->path && f->path[0]) {
						snprintf(path, sizeof(path), "%s/%s", mods[owner].dir, f->path);
						fr->bytes = fileBytes(path);

						if (fr->bytes < 0) {
							snprintf(path, sizeof(path), "%s/files/%s", mods[owner].dir, f->path);
							fr->bytes = fileBytes(path);
						}
					}
				} else if (vanillaHas(fr->name)) {
					fr->owner = OWNER_VANILLA;
				}
			}
		}
	}
}

static const char *ownerLabel(const struct mod *mods, int owner)
{
	if (owner == OWNER_VANILLA) {
		return "vanilla";
	}

	if (owner == OWNER_NONE) {
		return "nowhere";
	}

	return mods[owner].label;
}

/* -- [MpStageSlots] --------------------------------------------------------- */

static struct reservation *g_Reservations;
static int g_NumReservations;

/**
 * The [MpStageSlots] section of a pd.ini: one `spec=row` per line, where spec
 * is the stage spec exactly as the modconfig wrote it (`0x26`, `STAGE_28`,
 * `gex_arec`) and row is the stagenum it holds. modSlotReserve returns an
 * existing reservation BEFORE the usable gate, so what this file says is what
 * the next boot does.
 */
static void readIni(const char *path)
{
	uint32_t len = 0;
	char *text = (char *)slurp(path, &len);
	char *line, *next;
	bool in = false;
	int cap = 0;

	if (!text) {
		die("cannot read %s", path);
	}

	for (line = text; line && *line; line = next) {
		char *nl = strchr(line, '\n');
		char *eq;

		if (nl) {
			*nl = '\0';
			next = nl + 1;
		} else {
			next = NULL;
		}

		if (line[0] == '[') {
			in = !strncmp(line, "[MpStageSlots]", 14);
			continue;
		}

		if (!in) {
			continue;
		}

		eq = strchr(line, '=');

		if (!eq) {
			continue;
		}

		*eq = '\0';

		while (eq > line && (eq[-1] == ' ' || eq[-1] == '\t')) {
			*--eq = '\0';
		}

		if (!line[0]) {
			continue;
		}

		if (g_NumReservations == cap) {
			cap = cap ? cap * 2 : 64;
			g_Reservations = realloc(g_Reservations, (size_t)cap * sizeof(*g_Reservations));

			if (!g_Reservations) {
				die("out of memory reading %s", path);
			}
		}

		snprintf(g_Reservations[g_NumReservations].name,
				sizeof(g_Reservations[0].name), "%s", line);
		g_Reservations[g_NumReservations].slot = (int)strtol(eq + 1, NULL, 0);
		g_Reservations[g_NumReservations].block = -1;
		g_Reservations[g_NumReservations].mod = -1;
		++g_NumReservations;
	}

	free(text);
}

/* Does this ini key name this block? The loader records the spec as written,
 * so a numeric block matches on value and the other two on the string. */
static bool reservationNames(const struct reservation *r, const struct stageblock *b)
{
	if (b->kind == SPEC_NUMBER) {
		char *end = NULL;
		long v = strtol(r->name, &end, 0);

		return r->name[0] >= '0' && r->name[0] <= '9' && end && !*end && v == b->stagenum;
	}

	if (!strcmp(r->name, b->spec)) {
		return true;
	}

	if (b->kind == SPEC_ROWNAME) {
		const struct stagename *sn = stageByName(r->name);

		return sn && sn->hasrow && sn->stagenum == b->stagenum;
	}

	return false;
}

/**
 * Everything about the set's stage declarations that a boot log would only
 * tell you after the fact - and three things it would not tell you at all.
 */
static void checkStages(struct mod *mods, int n, bool haveIni)
{
	int i, j, k, r;
	uint32_t a, b;
	int byKind[3] = { 0, 0, 0 };
	int total = 0, extras = 0, extrasClaimed = 0, named = 0;
	size_t s;

	for (s = 0; s < MSC_NUM_STAGENAMES; ++s) {
		if (g_StageNameTable[s].hasrow && isExtraRow(g_StageNameTable[s].name)) {
			++extras;
		}
	}

	/* 1. The census: who says "put me here" and who says "find me a row". */
	for (i = 0; i < n; ++i) {
		for (a = 0; a < mods[i].numBlocks; ++a) {
			const struct stageblock *blk = &mods[i].blocks[a];

			++total;
			++byKind[blk->kind];

			if (blk->kind == SPEC_NAME) {
				++named;
			} else if (blk->extra) {
				++extrasClaimed;
			}
		}
	}

	printf("stages: %d declared - %d by number, %d by row name, %d by the mod's own name\n",
			total, byKind[SPEC_NUMBER], byKind[SPEC_ROWNAME], byKind[SPEC_NAME]);
	printf("        %d STAGE_EXTRA rows exist; %d are claimed outright and %d names need one, "
			"leaving %d; the save field holds %d stagenums\n",
			extras, extrasClaimed, named, extras - extrasClaimed - named, MSC_NUM_STAGENUMS);

	if (named > extras - extrasClaimed) {
		finding(LVL_ERROR, "%d levels ask the loader for a row and only %d STAGE_EXTRA rows are "
				"free after the explicit claims; the last %d get 'no free index left' and "
				"their blocks are skipped", named, extras - extrasClaimed,
				named - (extras - extrasClaimed));
	}

	for (i = 0; i < n; ++i) {
		for (a = 0; a < mods[i].numBlocks; ++a) {
			const struct stageblock *blk = &mods[i].blocks[a];

			/* 1b. The spellings the loader still accepts but nobody should
			 * write. A number says nothing a row name does not, and a claim
			 * of a STAGE_EXTRA row is a NEW level picking its own row - the
			 * one thing the allocator exists to do for it. */
			if (blk->kind == SPEC_NUMBER && blk->rowname) {
				finding(LVL_WARN, "%s: modconfig.txt:%d: stage %s names a row by number; "
						"spell it stage \"%s\"", mods[i].label, blk->line, blk->spec, blk->rowname);
			} else if (blk->kind != SPEC_NAME && blk->extra) {
				finding(LVL_WARN, "%s: modconfig.txt:%d: stage %s claims a STAGE_EXTRA row "
						"outright. That row is placeholder content, so this is a new level - "
						"give it the mod's own name and let the loader place it", mods[i].label,
						blk->line, blk->spec);
			}

			/* 2. A row with two occupants. The registry is last-writer-wins on
			 * the owning mod (modStageRegRecord) and g_Stages has one row per
			 * stage, whichever spelling reached it. */
			if (blk->stagenum >= 0) {
				for (j = i; j < n; ++j) {
					for (b = (j == i) ? a + 1 : 0; b < mods[j].numBlocks; ++b) {
						const struct stageblock *o = &mods[j].blocks[b];

						if (o->stagenum == blk->stagenum) {
							finding(LVL_ERROR, "stage 0x%02x (%s) is claimed by %s "
									"(modconfig.txt:%d, as %s) and %s (:%d, as %s). One row, "
									"two writers, and the later parse wins each field",
									blk->stagenum, blk->rowname, mods[i].label, blk->line,
									blk->spec, mods[j].label, o->line, o->spec);
						}
					}
				}
			} else if (blk->kind == SPEC_NAME) {
				/* 3. The same own-name in two mods is one reservation shared by
				 * both, which is the head/body collision again. */
				for (j = i + 1; j < n; ++j) {
					for (b = 0; b < mods[j].numBlocks; ++b) {
						const struct stageblock *o = &mods[j].blocks[b];

						if (o->kind == SPEC_NAME && !strcmp(o->spec, blk->spec)) {
							finding(LVL_ERROR, "stage \"%s\" is declared by %s "
									"(modconfig.txt:%d) and %s (:%d). [MpStageSlots] keys on "
									"the name, so both land on one row", blk->spec,
									mods[i].label, blk->line, mods[j].label, o->line);
						}
					}
				}
			}

			/* 4. Files. Unresolved is fatal for the block in the loader;
			 * cross-mod is legal and noted, as the loader notes it. */
			for (k = 0; k < FK_COUNT; ++k) {
				const struct fileref *fr = &blk->files[k];

				if (!fr->name[0]) {
					continue;
				}

				if (fr->owner == OWNER_NONE) {
					finding(LVL_ERROR, "%s: modconfig.txt:%d: stage %s: %s \"%s\" is in no "
							"mounted mod and is not a vanilla file; the loader drops the "
							"block", mods[i].label, blk->line, blk->spec, g_FileKeys[k],
							fr->name);
				} else if (fr->owner >= 0 && fr->owner != i) {
					finding(fr->others > 1 ? LVL_WARN : LVL_NOTE,
							"%s: modconfig.txt:%d: stage %s: %s \"%s\" is not in this mod; it "
							"resolves to %s%s", mods[i].label, blk->line, blk->spec,
							g_FileKeys[k], fr->name, mods[fr->owner].label,
							fr->others > 1 ? ", and other mods carry the name too" : "");
				}

				/* A 512-byte placeholder .seg is a level nobody built. The
				 * loader fatals on entry with "overflow when trying to
				 * preprocess a bg file", not at parse. */
				if (k == FK_BG && fr->bytes >= 0 && fr->bytes < 2048) {
					finding(LVL_ERROR, "%s: modconfig.txt:%d: stage %s: bgfile \"%s\" is %ld "
							"bytes - a stub, not a background. Entering the stage is a "
							"fatal", mods[i].label, blk->line, blk->spec, fr->name, fr->bytes);
				}
			}

			/* 5. A placeholder row carries placeholder geometry. Omitting a
			 * key does not fall back to vanilla; it inherits whatever the
			 * row has - another level's tiles under this level's floor. */
			if (blk->extra || blk->kind == SPEC_NAME) {
				static const int need[] = { FK_BG, FK_TILES, FK_PADS };
				size_t q;

				if (!blk->files[FK_BG].name[0]) {
					finding(LVL_WARN, "%s: modconfig.txt:%d: stage %s sits on a STAGE_EXTRA row "
							"and names no bgfile, so it draws that row's placeholder geometry",
							mods[i].label, blk->line, blk->spec);
				}

				/* Omitting a key does not mean "vanilla": the row keeps what
				 * it had, which on an EXTRA row is some other level's file.
				 * Sometimes that is the point (gex redirects a stub's
				 * geometry onto a row whose pads and setup are the real
				 * level's); once it was a black void you fell out of (a
				 * bg_lee over bg_ref tiles). Say which file it is and let the
				 * reader judge. */
				for (q = 0; q < sizeof(need) / sizeof(need[0]); ++q) {
					if (!blk->files[need[q]].name[0] && blk->files[FK_BG].name[0]) {
						finding(LVL_WARN, "%s: modconfig.txt:%d: stage %s names a bgfile but no "
								"%s, so it inherits the row's %s", mods[i].label, blk->line,
								blk->spec, g_FileKeys[need[q]], rowFile(blk->rowname, need[q]));
					}
				}

				if (!strcmp(blk->kindstr, "mp") || !strcmp(blk->kindstr, "both")) {
					if (!blk->files[FK_MPSETUP].name[0]) {
						finding(LVL_WARN, "%s: modconfig.txt:%d: stage %s is kind %s with no "
								"mpsetupfile, so the arena runs the row's %s",
								mods[i].label, blk->line, blk->spec, blk->kindstr,
								rowFile(blk->rowname, FK_MPSETUP));
					}
				}

				if (!strcmp(blk->kindstr, "solo") || !strcmp(blk->kindstr, "both")) {
					if (!blk->files[FK_SETUP].name[0]) {
						finding(LVL_WARN, "%s: modconfig.txt:%d: stage %s is kind %s with no "
								"setupfile, so the mission runs the row's %s",
								mods[i].label, blk->line, blk->spec, blk->kindstr,
								rowFile(blk->rowname, FK_SETUP));
					}
				}
			}

			/* 6. The same level twice in one mod: two blocks whose bgfile AND
			 * padsfile are the same files. Same geometry under different pads
			 * is a different arena (gex does that on purpose); same geometry
			 * under the same pads is one arena listed twice, which is how
			 * Suburb turned up in the menu two times. */
			if (blk->files[FK_BG].owner >= 0) {
				for (b = a + 1; b < mods[i].numBlocks; ++b) {
					const struct stageblock *o = &mods[i].blocks[b];

					if (o->files[FK_BG].owner == blk->files[FK_BG].owner
							&& o->files[FK_BG].id == blk->files[FK_BG].id
							&& !strcmp(o->files[FK_PADS].name, blk->files[FK_PADS].name)) {
						finding(LVL_WARN, "%s: stage %s (modconfig.txt:%d) and stage %s (:%d) "
								"both use bgfile \"%s\" - the same level declared twice",
								mods[i].label, blk->spec, blk->line, o->spec, o->line,
								blk->files[FK_BG].name);
					}
				}
			}
		}
	}

	if (!haveIni) {
		return;
	}

	/* 7. What the ini holds against what the set declares. Report only: a
	 * reservation with no block is a row held for a level that is gone, and
	 * whether to release it is a policy call, not this tool's. */
	{
		int orphans = 0, highest = -1;

		for (r = 0; r < g_NumReservations; ++r) {
			struct reservation *rv = &g_Reservations[r];

			if (rv->slot > highest) {
				highest = rv->slot;
			}

			for (i = 0; i < n && rv->block < 0; ++i) {
				for (a = 0; a < mods[i].numBlocks; ++a) {
					if (reservationNames(rv, &mods[i].blocks[a])) {
						rv->block = (int)a;
						rv->mod = i;
						mods[i].blocks[a].reserved = rv->slot;
						break;
					}
				}
			}

			if (rv->slot >= MSC_NUM_STAGENUMS) {
				finding(LVL_ERROR, "[MpStageSlots] '%s' holds row %d, past the %d the save "
						"field can store", rv->name, rv->slot, MSC_NUM_STAGENUMS - 1);
			}

			if (rv->block < 0) {
				const struct stagename *held = stageByNum(rv->slot);

				++orphans;
				/* A stale claim of a vanilla row holds nothing - the allocator
				 * never hands those out - so it is only worth a note. A stale
				 * reservation of a STAGE_EXTRA row is a row nobody can have. */
				finding(held && !isExtraRow(held->name) ? LVL_NOTE : LVL_WARN,
						"[MpStageSlots] '%s' holds row %d (%s) and nothing in this "
						"set declares it - the row stays held across boots", rv->name,
						rv->slot, held ? held->name : "no row");
				continue;
			}

			/* A persisted allocation that now sits on a row some block claims
			 * outright. modSlotReserve returns the reservation before the
			 * usable gate, so both will write the row; the loader only warns
			 * that the index is 'already reserved by another name'. */
			for (i = 0; i < n; ++i) {
				for (a = 0; a < mods[i].numBlocks; ++a) {
					const struct stageblock *o = &mods[i].blocks[a];

					if (o->kind != SPEC_NAME && o->stagenum == rv->slot
							&& !(i == rv->mod && (int)a == rv->block)) {
						finding(LVL_ERROR, "[MpStageSlots] '%s' (declared by %s) is reserved "
								"at row 0x%02x, which %s claims outright at modconfig.txt:%d. "
								"The reservation is honoured first, so both write that row",
								rv->name, mods[rv->mod].label, rv->slot, mods[i].label, o->line);
					}
				}
			}
		}

		for (i = 0; i < n; ++i) {
			for (a = 0; a < mods[i].numBlocks; ++a) {
				const struct stageblock *blk = &mods[i].blocks[a];

				if (blk->kind == SPEC_NAME && blk->reserved < 0) {
					finding(LVL_NOTE, "%s: stage \"%s\" has no [MpStageSlots] entry yet; the "
							"next boot allocates it", mods[i].label, blk->spec);
				}
			}
		}

		printf("        [MpStageSlots]: %d reservations, %d orphaned, highest row %d of %d\n",
				g_NumReservations, orphans, highest, MSC_NUM_STAGENUMS - 1);
	}
}

static void listStages(const struct mod *mods, int n)
{
	int i;
	uint32_t a;

	printf("\n  %-22s %-22s %-8s %-5s %-16s %-5s %s\n",
			"mod", "spec", "how", "row", "rowname", "kind", "bgfile <- owner");

	for (i = 0; i < n; ++i) {
		for (a = 0; a < mods[i].numBlocks; ++a) {
			const struct stageblock *blk = &mods[i].blocks[a];
			char row[16] = "-";

			if (blk->stagenum >= 0) {
				snprintf(row, sizeof(row), "0x%02x", blk->stagenum);
			} else if (blk->reserved >= 0) {
				snprintf(row, sizeof(row), "0x%02x*", blk->reserved);
			}

			printf("  %-22s %-22s %-8s %-5s %-16s %-5s %s%s%s\n", mods[i].label, blk->spec,
					g_SpecWords[blk->kind], row,
					blk->rowname ? blk->rowname
						: (blk->reserved >= 0 && stageByNum(blk->reserved)
							? stageByNum(blk->reserved)->name : "-"),
					blk->kindstr[0] ? blk->kindstr : "-",
					blk->files[FK_BG].name[0] ? blk->files[FK_BG].name : "(row's own)",
					blk->files[FK_BG].name[0] ? " <- " : "",
					blk->files[FK_BG].name[0] ? ownerLabel(mods, blk->files[FK_BG].owner) : "");
		}
	}

	printf("\n  * = row held for it in [MpStageSlots]; the loader allocated it on an earlier boot\n");
}

/* -- the stage table ------------------------------------------------------- */

static void jsonString(FILE *out, const char *s)
{
	fputc('"', out);

	for (; *s; ++s) {
		if (*s == '"' || *s == '\\') {
			fputc('\\', out);
			fputc(*s, out);
		} else if ((unsigned char)*s < 0x20) {
			fprintf(out, "\\u%04x", (unsigned char)*s);
		} else {
			fputc(*s, out);
		}
	}

	fputc('"', out);
}

static void jsonFileRef(FILE *out, const struct mod *mods, const char *key, const struct fileref *fr)
{
	fprintf(out, "      \"%s\": ", key);

	if (!fr->name[0]) {
		fprintf(out, "null");
		return;
	}

	fprintf(out, "{ \"name\": ");
	jsonString(out, fr->name);
	fprintf(out, ", \"owner\": ");

	if (fr->owner == OWNER_NONE) {
		fprintf(out, "null");
	} else {
		jsonString(out, ownerLabel(mods, fr->owner));
	}

	if (fr->owner >= 0) {
		fprintf(out, ", \"id\": \"0x%04x\"", fr->id);
	}

	if (fr->bytes >= 0) {
		fprintf(out, ", \"bytes\": %ld", fr->bytes);
	}

	fprintf(out, " }");
}

/**
 * The distribution-wide stage table: what mpHeadsAndBodiesTable.json is for
 * heads and bodies, for stages. It DESCRIBES what the mounted modconfigs
 * declare - it is generated from them, not the other way round - so
 * regenerating it and diffing against the committed copy is the check that
 * the packs still say what the table says. Whether it should one day be the
 * source the modconfigs are generated from is a separate decision, and
 * writing it this way forecloses nothing.
 *
 * Deterministic: mount order, then declaration order; no timestamps.
 */
static void writeStageTable(const char *path, const struct mod *mods, int n, bool haveIni)
{
	FILE *out = fopen(path, "w");
	int i, k, r;
	uint32_t a;
	bool first = true;
	size_t s;

	if (!out) {
		die("cannot write %s", path);
	}

	fprintf(out, "{\n  \"generatedBy\": \"modsetcheck --stage-table\",\n");
	fprintf(out, "  \"regenerate\": \"tools/checkmods --stage-table pd-fojo-basedir/data/mpStagesTable.json "
			"(add --ini pd-fojo-basedir/pd.ini for this install's allocations)\",\n");
	fprintf(out, "  \"numStagenums\": %d,\n  \"stageTitle\": \"0x%02x\",\n",
			MSC_NUM_STAGENUMS, MSC_STAGE_TITLE);
	fprintf(out, "  \"mods\": [");

	for (i = 0; i < n; ++i) {
		fprintf(out, "%s", i ? ", " : "");
		jsonString(out, mods[i].label);
	}

	fprintf(out, "],\n  \"rows\": [\n");

	for (s = 0; s < MSC_NUM_STAGENAMES; ++s) {
		const struct stagename *sn = &g_StageNameTable[s];

		if (!sn->hasrow) {
			continue;
		}

		fprintf(out, "%s    { \"stagenum\": \"0x%02x\", \"row\": \"%s\", \"extra\": %s }",
				first ? "" : ",\n", sn->stagenum, sn->name, isExtraRow(sn->name) ? "true" : "false");
		first = false;
	}

	fprintf(out, "\n  ],\n  \"stages\": [\n");
	first = true;

	for (i = 0; i < n; ++i) {
		for (a = 0; a < mods[i].numBlocks; ++a) {
			const struct stageblock *blk = &mods[i].blocks[a];
			int row = blk->stagenum >= 0 ? blk->stagenum : blk->reserved;

			fprintf(out, "%s    {\n      \"source\": ", first ? "" : ",\n");
			jsonString(out, mods[i].label);
			fprintf(out, ",\n      \"spec\": ");
			jsonString(out, blk->spec);
			fprintf(out, ",\n      \"declaredBy\": \"%s\",\n", g_SpecWords[blk->kind]);

			if (row >= 0) {
				fprintf(out, "      \"stagenum\": \"0x%02x\",\n", row);
				fprintf(out, "      \"row\": \"%s\",\n",
						blk->rowname ? blk->rowname : (stageByNum(row) ? stageByNum(row)->name : ""));
			} else {
				fprintf(out, "      \"stagenum\": null,\n      \"row\": null,\n");
			}

			fprintf(out, "      \"stagenumFrom\": \"%s\",\n",
					blk->stagenum >= 0 ? "modconfig" : (blk->reserved >= 0 ? "MpStageSlots" : "unallocated"));
			fprintf(out, "      \"kind\": ");

			if (blk->kindstr[0]) {
				jsonString(out, blk->kindstr);
			} else {
				fprintf(out, "null");
			}

			fprintf(out, ",\n      \"arenaname\": ");

			if (blk->arenaname[0]) {
				jsonString(out, blk->arenaname);
			} else {
				fprintf(out, "null");
			}

			fprintf(out, ",\n");

			for (k = 0; k < FK_COUNT; ++k) {
				jsonFileRef(out, mods, g_FileKeys[k], &blk->files[k]);
				fprintf(out, ",\n");
			}

			fprintf(out, "      \"line\": %d\n    }", blk->line);
			first = false;
		}
	}

	fprintf(out, "\n  ]");

	if (haveIni) {
		fprintf(out, ",\n  \"reservations\": [\n");

		for (r = 0; r < g_NumReservations; ++r) {
			const struct reservation *rv = &g_Reservations[r];

			fprintf(out, "%s    { \"name\": ", r ? ",\n" : "");
			jsonString(out, rv->name);
			fprintf(out, ", \"stagenum\": \"0x%02x\", \"declaredBy\": ", rv->slot);

			if (rv->mod >= 0) {
				jsonString(out, mods[rv->mod].label);
			} else {
				fprintf(out, "null");
			}

			fprintf(out, " }");
		}

		fprintf(out, "\n  ]");
	}

	fprintf(out, "\n}\n");
	fclose(out);
	printf("wrote %s\n", path);
}

/* -- main ------------------------------------------------------------------ */

static void usage(FILE *out)
{
	fprintf(out,
		"usage: modsetcheck <mod-dir> [<mod-dir> ...] [--base N] [--max N]\n"
		"                   [--ini pd.ini] [--stage-table out.json] [--list-stages] [--help]\n"
		"\n"
		"Checks a set of mods the way they would mount, in the order given.\n"
		"Reads each dir's filetable.dat and modconfig.txt - the artifacts that\n"
		"actually ship, not the manifests they were built from.\n"
		"\n"
		"  --base N   the first texture slot the first mod is given (default %u)\n"
		"  --max N    the highest texture slot available to the set (default %u)\n"
		"  --ini F    a pd.ini whose [MpStageSlots] says which row each named stage\n"
		"             was allocated; reports orphaned and conflicting reservations\n"
		"  --stage-table F\n"
		"             write the set's stage table as JSON - every stage block, the row\n"
		"             it lands on and where its files come from\n"
		"  --list-stages\n"
		"             print that table to stdout as well\n",
		DEFAULT_SLOT_BASE, DEFAULT_SLOT_MAX);
}

int main(int argc, char **argv)
{
	struct mod *mods;
	uint32_t base = DEFAULT_SLOT_BASE, cap = DEFAULT_SLOT_MAX;
	const char *iniPath = NULL, *tablePath = NULL;
	bool list = false;
	int n = 0, i;

	mods = calloc(MAX_MODS, sizeof(*mods));

	if (!mods) {
		die("out of memory");
	}

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--base") && i + 1 < argc) {
			base = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "--max") && i + 1 < argc) {
			cap = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "--ini") && i + 1 < argc) {
			iniPath = argv[++i];
		} else if (!strcmp(argv[i], "--stage-table") && i + 1 < argc) {
			tablePath = argv[++i];
		} else if (!strcmp(argv[i], "--list-stages")) {
			list = true;
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			usage(stdout);
			return 0;
		} else if (argv[i][0] == '-') {
			die("unknown argument %s", argv[i]);
		} else if (n < MAX_MODS) {
			snprintf(mods[n].dir, sizeof(mods[n].dir), "%s", argv[i]);

			/* A trailing slash would make the label empty. */
			while (mods[n].dir[0] && mods[n].dir[strlen(mods[n].dir) - 1] == '/') {
				mods[n].dir[strlen(mods[n].dir) - 1] = '\0';
			}

			mods[n].label = baseName(mods[n].dir);
			++n;
		} else {
			die("more than %d mod dirs", MAX_MODS);
		}
	}

	if (!n) {
		usage(stderr);
		return 2;
	}

	if (base > cap) {
		die("--base %u is above --max %u", base, cap);
	}

	printf("modsetcheck: %d mod(s), in mount order\n\n", n);

	for (i = 0; i < n; ++i) {
		readFileTable(&mods[i]);
		readModConfig(&mods[i]);

		printf("  %d  %-24s v%u, %u file(s), %u texmap, %u romSource(s)%s%s\n",
				i, mods[i].label,
				mods[i].haveFt ? mods[i].ft.version : 0,
				mods[i].haveFt ? mods[i].ft.numFiles : 0,
				mods[i].haveFt ? mods[i].ft.numTexMap : 0,
				mods[i].haveFt ? mods[i].ft.numSources : 0,
				mods[i].modname[0] ? ", modname " : "",
				mods[i].modname[0] ? mods[i].modname : "");
	}

	printf("\n");

	for (i = 0; i < n; ++i) {
		checkTexMap(&mods[i]);
		checkAltRefs(&mods[i]);
	}

	checkSlotBudget(mods, n, base, cap);

	printf("\n");
	checkNameClaims(mods, n);
	checkRomSources(mods, n);

	printf("\n");
	resolveStageFiles(mods, n);

	if (iniPath) {
		readIni(iniPath);
	}

	checkStages(mods, n, iniPath != NULL);

	if (list) {
		listStages(mods, n);
	}

	if (tablePath) {
		writeStageTable(tablePath, mods, n, iniPath != NULL);
	}

	printf("\n%d error(s), %d warning(s)\n", g_Errors, g_Warnings);

	if (!g_Errors && !g_Warnings) {
		printf("this set mounts without stepping on itself\n");
	}

	for (i = 0; i < n; ++i) {
		if (mods[i].haveFt) {
			pdftFree(&mods[i].ft);
		}

		free(mods[i].blob);
		free(mods[i].heads);
		free(mods[i].blocks);
	}

	free(mods);
	free(g_Reservations);
	return g_Errors ? 1 : 0;
}
