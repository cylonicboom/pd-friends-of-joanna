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
	struct claim *stages;
	uint32_t numStages, capStages;

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
 * HeadsAndBodies blocks and stage claims, with the line each came from so a
 * finding can point at it. Nesting is not tracked: a `name` key only appears
 * inside HeadsAndBodies in every modconfig shipped, and a stage line carries
 * its own id, so a flat scan reads both correctly. If a future block type
 * takes a `name`, this wants the brace depth.
 */
static void readModConfig(struct mod *m)
{
	char path[JOINMAX];
	uint32_t len = 0;
	char *text, *line, *next;
	int lineNo = 0;
	bool inHeads = false;

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
		unsigned stage;
		const char *p = line;
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

		if (sscanf(p, "stage %x", &stage) == 1) {
			char id[16];

			snprintf(id, sizeof(id), "0x%02x", stage);
			pushClaim(&m->stages, &m->numStages, &m->capStages, id, lineNo);
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
static void checkNameClaims(struct mod *mods, int n, bool heads)
{
	int i, j;
	uint32_t a, b;

	for (i = 0; i < n; ++i) {
		const struct claim *ca = heads ? mods[i].heads : mods[i].stages;
		uint32_t na = heads ? mods[i].numHeads : mods[i].numStages;

		for (a = 0; a < na; ++a) {
			for (j = i + 1; j < n; ++j) {
				const struct claim *cb = heads ? mods[j].heads : mods[j].stages;
				uint32_t nb = heads ? mods[j].numHeads : mods[j].numStages;

				for (b = 0; b < nb; ++b) {
					if (strcmp(ca[a].name, cb[b].name)) {
						continue;
					}

					if (heads) {
						finding(LVL_ERROR, "head/body name \"%s\" is claimed by %s "
								"(modconfig.txt:%d) and %s (:%d). The later mount wins "
								"silently - nothing reports it and nothing logs it",
								ca[a].name, mods[i].label, ca[a].line,
								mods[j].label, cb[b].line);
					} else {
						finding(LVL_ERROR, "stage %s is claimed by %s (modconfig.txt:%d) "
								"and %s (:%d). g_Stages holds one row per stage, so the "
								"later mount replaces the earlier one",
								ca[a].name, mods[i].label, ca[a].line,
								mods[j].label, cb[b].line);
					}
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

/* -- main ------------------------------------------------------------------ */

static void usage(FILE *out)
{
	fprintf(out,
		"usage: modsetcheck <mod-dir> [<mod-dir> ...] [--base N] [--max N] [--help]\n"
		"\n"
		"Checks a set of mods the way they would mount, in the order given.\n"
		"Reads each dir's filetable.dat and modconfig.txt - the artifacts that\n"
		"actually ship, not the manifests they were built from.\n"
		"\n"
		"  --base N   the first texture slot the first mod is given (default %u)\n"
		"  --max N    the highest texture slot available to the set (default %u)\n",
		DEFAULT_SLOT_BASE, DEFAULT_SLOT_MAX);
}

int main(int argc, char **argv)
{
	struct mod *mods;
	uint32_t base = DEFAULT_SLOT_BASE, cap = DEFAULT_SLOT_MAX;
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
	checkNameClaims(mods, n, true);
	checkNameClaims(mods, n, false);
	checkRomSources(mods, n);

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
		free(mods[i].stages);
	}

	free(mods);
	return g_Errors ? 1 : 0;
}
