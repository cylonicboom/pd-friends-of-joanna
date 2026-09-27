#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "romdata.h"
#include "mod.h"
#include "asset.h"
#include "data.h"
#include "bss.h"
#include "game/stagetable.h"
#include "game/pad.h"
#include "constants.h"

/* ------------------------------------------------------------------------
 * drives
 * ---------------------------------------------------------------------- */

static const char *const g_AssetDriveNames[ASSET_DRIVE_COUNT] = {
	"",
	"file",
	"tex",
	"stage",
	"head",
	"body",
	"hand",
	"seg",
	"rom",
	"mod",
	"pad",
};

const char *assetDriveName(s32 drive)
{
	if (drive <= ASSET_DRIVE_NONE || drive >= ASSET_DRIVE_COUNT) {
		return NULL;
	}

	return g_AssetDriveNames[drive];
}

static const char *const g_AssetStageFieldNames[ASSET_STAGE_FIELD_COUNT] = {
	"bg", "tiles", "pads", "setup", "mpsetup",
};

static void assetRefClear(struct assetref *ref)
{
	memset(ref, 0, sizeof(*ref));
	ref->owner = -1;
	ref->id = -1;
	ref->sub = -1;
	ref->via = -1;
}

/* ------------------------------------------------------------------------
 * owners
 *
 * There is no vanilla row: every fileSlots row starts as a copy of the ROM's
 * table and a mod's fragment overlays its own row. So "vanilla" is the ROM's
 * name table (romdataRomFileName), and a slot in a mod's row is that mod's
 * own only where it differs from the ROM's entry for the same id.
 * ---------------------------------------------------------------------- */

static const char *assetBasename(const char *path)
{
	const char *slash = strrchr(path, '/');
#ifdef PLATFORM_WIN32
	const char *bslash = strrchr(path, '\\');

	if (bslash && (!slash || bslash > slash)) {
		slash = bslash;
	}
#endif
	return slash ? slash + 1 : path;
}

/* The owner segment a formatted path carries. The mod DIR's basename, not
 * the modconfig display name: measured, mod_gex_characters declares
 * "GoldenEye:X Characters", which has a space and a colon in it, and a
 * path that does not survive its own parser is not a path. The display
 * name is still accepted on the way in, as an alias. */
static const char *assetModLabel(s32 mod)
{
	if (mod == ASSET_OWNER_VANILLA) {
		return "vanilla";
	}

	if (mod >= 0 && mod < (s32)g_NumModDirs && modDirs[mod][0]) {
		return assetBasename(modDirs[mod]);
	}

	if (mod >= 0 && mod < 64 && g_ModNames[mod][0]) {
		return g_ModNames[mod];
	}

	return "(unnamed)";
}

static s32 assetSegEquals(const char *seg, u32 len, const char *s)
{
	return s && strlen(s) == len && !strncmp(s, seg, len);
}

/* A mod index from an owner segment, ASSET_OWNER_VANILLA for the literal
 * "vanilla", or -2 for no match. */
#define ASSET_OWNER_NONE (-2)

static s32 assetOwnerFromSegment(const char *seg, u32 len)
{
	if (!len) {
		return ASSET_OWNER_NONE;
	}

	if (assetSegEquals(seg, len, "vanilla")) {
		return ASSET_OWNER_VANILLA;
	}

	for (s32 mod = 0; mod < (s32)g_NumModDirs; ++mod) {
		const char *name = (mod < 64 && g_ModNames[mod][0]) ? g_ModNames[mod] : NULL;
		const char *dir = modDirs[mod][0] ? modDirs[mod] : NULL;

		if (assetSegEquals(seg, len, name)) {
			return mod;
		}

		if (dir && (assetSegEquals(seg, len, dir) || assetSegEquals(seg, len, assetBasename(dir)))) {
			return mod;
		}
	}

	return ASSET_OWNER_NONE;
}

/* Is slot fileNum in mod's row the mod's own, or the ROM's copy? */
static s32 assetFileSlotIsModOwn(s32 mod, s32 fileNum)
{
	const char *slotName = romdataFileGetSlotName(mod, fileNum);
	const char *romName = romdataRomFileName(fileNum);

	if (!slotName) {
		return 0;
	}

	return !romName || slotName != romName;
}

/* ------------------------------------------------------------------------
 * the grammar
 *
 * drive:/owner/item/sub - splits at the drive, then one optional owner
 * segment, then the item, then an optional sub segment for drives that
 * have one. The parse is the same for every drive; what an owner or a sub
 * means is the drive's business.
 * ---------------------------------------------------------------------- */

static s32 assetDriveFromPath(const char *path, const char **rest)
{
	const char *colon = strchr(path, ':');

	if (!colon || colon == path) {
		return ASSET_DRIVE_NONE;
	}

	*rest = colon + 1;

	for (s32 d = ASSET_DRIVE_FILE; d < ASSET_DRIVE_COUNT; ++d) {
		if (assetSegEquals(path, (u32)(colon - path), g_AssetDriveNames[d])) {
			return d;
		}
	}

	return ASSET_DRIVE_NONE;
}

/* After the drive: "" or "/" is the root; "/<owner>" is that owner's root
 * when the segment names one; otherwise everything after the '/' is the
 * item, which may itself contain '/'. */
static s32 assetSplitOwner(const char *rest, s32 *owner, const char **item)
{
	*owner = ASSET_OWNER_NONE;
	*item = NULL;

	if (rest[0] != '/') {
		return rest[0] == '\0' ? ASSET_OK : ASSET_BADPATH;
	}

	rest++;

	if (rest[0] == '\0') {
		return ASSET_OK;
	}

	const char *slash = strchr(rest, '/');
	s32 mod = assetOwnerFromSegment(rest, slash ? (u32)(slash - rest) : (u32)strlen(rest));

	if (mod != ASSET_OWNER_NONE) {
		*owner = mod;
		*item = (slash && slash[1]) ? slash + 1 : NULL;
		return ASSET_OK;
	}

	*item = rest;
	return ASSET_OK;
}

/* "#3412" is a literal slot number, as the bar already reads it */
static s32 assetLiteralNumber(const char *item, s32 limit)
{
	if (item[0] != '#' || item[1] == '\0') {
		return -1;
	}

	for (const char *c = item + 1; *c; ++c) {
		if (*c < '0' || *c > '9') {
			return -1;
		}
	}

	s32 num = atoi(item + 1);
	return (num >= 1 && num < limit) ? num : -1;
}

/* decimal or 0x-prefixed hex, whole string, or -1 */
static s32 assetParseNumber(const char *s)
{
	char *end = NULL;
	long v;

	if (!s || !s[0]) {
		return -1;
	}

	v = strtol(s, &end, 0);

	if (!end || *end != '\0' || v < 0 || v > 0x7fffffff) {
		return -1;
	}

	return (s32)v;
}

/* ------------------------------------------------------------------------
 * file:
 * ---------------------------------------------------------------------- */

#define ASSET_FILE_NAME_MAX 48

struct assetfileentry {
	s32 owner;
	s32 fileNum;
	char name[ASSET_FILE_NAME_MAX];
};

/* This used to be g_ImGuiOverlayFileIndex in imgui_overlay.cpp, built for
 * the debugger bar. fileSlots has no occupancy list, so the only way to
 * list a mod's files is to probe all 8191 slots of its row, and doing that
 * per keystroke was not acceptable. It is built once and invalidated on the
 * only two events that change it: a stage load and a change to the mod
 * roster. The vanilla table is listed once under its own owner rather than
 * once per mod row. */
static struct assetfileentry *g_AssetFileIndex = NULL;
static s32 g_AssetFileIndexCount = 0;
static s32 g_AssetFileIndexCap = 0;
static s32 g_AssetFileIndexMods = -1;
static s32 g_AssetFileIndexStage = -1;

static void assetFileIndexPush(s32 owner, s32 fileNum, const char *name)
{
	if (g_AssetFileIndexCount == g_AssetFileIndexCap) {
		s32 cap = g_AssetFileIndexCap ? g_AssetFileIndexCap * 2 : 4096;
		struct assetfileentry *grown = realloc(g_AssetFileIndex, (size_t)cap * sizeof(*grown));

		if (!grown) {
			return;
		}

		g_AssetFileIndex = grown;
		g_AssetFileIndexCap = cap;
	}

	struct assetfileentry *entry = &g_AssetFileIndex[g_AssetFileIndexCount++];
	entry->owner = owner;
	entry->fileNum = fileNum;
	snprintf(entry->name, sizeof(entry->name), "%s", name);
}

static void assetFileIndexBuild(void)
{
	const s32 mods = (s32)g_NumModDirs;
	const s32 stage = (s32)g_Vars.stagenum;
	const s32 romCount = romdataRomFileCount();

	if (mods == g_AssetFileIndexMods && stage == g_AssetFileIndexStage) {
		return;
	}

	g_AssetFileIndexCount = 0;

	for (s32 fileNum = 1; fileNum < romCount; ++fileNum) {
		const char *name = romdataRomFileName(fileNum);

		if (name) {
			assetFileIndexPush(ASSET_OWNER_VANILLA, fileNum, name);
		}
	}

	for (s32 mod = 0; mod < mods; ++mod) {
		for (s32 fileNum = 1; fileNum < 8192; ++fileNum) {
			if (!assetFileSlotIsModOwn(mod, fileNum)) {
				continue;
			}

			{
				const char *declared = romdataFileDeclaredName(mod, fileNum);
				assetFileIndexPush(mod, fileNum, declared ? declared : romdataFileGetSlotName(mod, fileNum));
			}
		}
	}

	g_AssetFileIndexMods = mods;
	g_AssetFileIndexStage = stage;
	sysLogPrintf(LOG_NOTE, "ASSET: file slot index built, %d entries across %d mod(s) + vanilla",
			g_AssetFileIndexCount, mods);
}

static s32 assetFileLookupVanilla(const char *item)
{
	s32 literal = assetLiteralNumber(item, 8192);
	s32 count = romdataRomFileCount();

	if (literal >= 0) {
		return romdataRomFileName(literal) ? literal : -1;
	}

	for (s32 fileNum = 1; fileNum < count; ++fileNum) {
		const char *name = romdataRomFileName(fileNum);

		if (name && !strcmp(name, item)) {
			return fileNum;
		}
	}

	return -1;
}

static s32 assetFileLookupInMod(const char *item, s32 mod)
{
	s32 literal = assetLiteralNumber(item, 8192);

	if (literal >= 0) {
		return romdataFileGetSlotName(mod, literal) ? literal : -1;
	}

	return romdataFileGetNumForNameInMod(item, mod);
}

static s32 assetFileResolve(const char *rest, struct assetref *out)
{
	s32 owner;
	const char *item;
	s32 err = assetSplitOwner(rest, &owner, &item);

	if (err != ASSET_OK) {
		return err;
	}

	if (!item || !item[0]) {
		return ASSET_NOTFOUND;   /* a root is a container, not an item */
	}

	out->drive = ASSET_DRIVE_FILE;

	if (owner == ASSET_OWNER_VANILLA) {
		s32 id = assetFileLookupVanilla(item);

		if (id < 0) {
			return ASSET_NOTFOUND;
		}

		out->owner = ASSET_OWNER_VANILLA;
		out->id = id;
		return ASSET_OK;
	}

	if (owner >= 0) {
		s32 id = assetFileLookupInMod(item, owner);

		if (id < 0) {
			return ASSET_NOTFOUND;
		}

		out->owner = (s8)owner;
		out->id = id;
		return ASSET_OK;
	}

	/* unqualified. The ROM's own names resolve to vanilla - every mod row
	 * carries them, so counting rows would call every vanilla file
	 * ambiguous. A mod's overlay of that slot is file:/<mod>/<name>.
	 * Anything else: exactly one mod resolves, more than one refuses. */
	{
		s32 id = assetFileLookupVanilla(item);

		if (id >= 0) {
			out->owner = ASSET_OWNER_VANILLA;
			out->id = id;
			return ASSET_OK;
		}
	}

	{
		s32 hitMod = -1;
		s32 hitId = -1;
		s32 hits = 0;

		for (s32 mod = 0; mod < (s32)g_NumModDirs; ++mod) {
			s32 id = assetFileLookupInMod(item, mod);

			if (id >= 0 && assetFileSlotIsModOwn(mod, id)) {
				if (hitMod < 0) {
					hitMod = mod;
					hitId = id;
				}

				hits++;
			}
		}

		if (hits == 0) {
			return ASSET_NOTFOUND;
		}

		if (hits > 1) {
			return ASSET_AMBIGUOUS;
		}

		out->owner = (s8)hitMod;
		out->id = hitId;
		return ASSET_OK;
	}
}

/* A mod file's name is what its PDFT entry declared, not what the slot
 * carries: the slot holds the path when an entry has both, and a path can
 * be the context resolver's program. Falls back to the slot for entries
 * with no declared name. */
static const char *assetFileName(const struct assetref *ref)
{
	const char *declared;

	if (ref->owner == ASSET_OWNER_VANILLA) {
		return romdataRomFileName(ref->id);
	}

	declared = romdataFileDeclaredName(ref->owner, ref->id);
	return declared ? declared : romdataFileGetSlotName(ref->owner, ref->id);
}

static s32 assetFileEnumerate(const char *rest, assetenumfn fn, void *ctx)
{
	s32 owner;
	const char *item;
	s32 err = assetSplitOwner(rest, &owner, &item);
	s32 visited = 0;

	if (err != ASSET_OK) {
		return err;
	}

	if (item && item[0]) {
		return ASSET_NOTFOUND;   /* a file is a leaf */
	}

	assetFileIndexBuild();

	for (s32 i = 0; i < g_AssetFileIndexCount; ++i) {
		const struct assetfileentry *entry = &g_AssetFileIndex[i];
		struct assetref ref;

		if (owner != ASSET_OWNER_NONE && entry->owner != owner) {
			continue;
		}

		assetRefClear(&ref);
		ref.drive = ASSET_DRIVE_FILE;
		ref.owner = (s8)entry->owner;
		ref.id = entry->fileNum;
		visited++;

		if (!fn(&ref, entry->name, ctx)) {
			break;
		}
	}

	return visited;
}

/* a file ref from a (possibly tagged) file id, the way the rest of the
 * engine carries them: untagged is vanilla, tagged carries its owner */
static s32 assetFileRefFromId(s32 fileId, struct assetref *out)
{
	s32 mod;

	if (fileId <= 0) {
		return ASSET_NOTFOUND;
	}

	mod = MOD_FILEID_MOD(fileId);
	assetRefClear(out);
	out->drive = ASSET_DRIVE_FILE;
	out->owner = (s8)(mod < 0 ? ASSET_OWNER_VANILLA : mod);
	out->id = MOD_FILEID_RAW(fileId);
	return assetFileName(out) ? ASSET_OK : ASSET_NOTFOUND;
}

/* ------------------------------------------------------------------------
 * tex:
 *
 * Two numberings per mod and neither is a file id. tex:/vanilla/<n> is a
 * ROM texnum below NUM_TEXTURES. tex:/<mod>/<local> is the number in that
 * mod's own bytes; its port id (what the loader assigned, from
 * MOD_TEX_PORT_BASE up) rides in `sub`. tex:/port/<n> is the reverse
 * lookup. An unqualified tex:/<n> is a vanilla texnum and nothing else.
 * ---------------------------------------------------------------------- */

static s32 assetTexResolve(const char *rest, struct assetref *out)
{
	s32 owner;
	const char *item;
	s32 err = assetSplitOwner(rest, &owner, &item);
	s32 n;

	if (err != ASSET_OK) {
		return err;
	}

	if (!item || !item[0]) {
		return ASSET_NOTFOUND;
	}

	out->drive = ASSET_DRIVE_TEX;

	if (owner == ASSET_OWNER_NONE && !strncmp(item, "port/", 5)) {
		n = assetParseNumber(item + 5);

		if (n < 0) {
			return ASSET_BADPATH;
		}

		for (s32 mod = 0; mod < (s32)g_NumModDirs; ++mod) {
			u16 local = modTexMapReverseLookup(mod, (u16)n);

			if (local != 0xffff) {
				out->owner = (s8)mod;
				out->id = local;
				out->sub = n;
				return ASSET_OK;
			}
		}

		return ASSET_NOTFOUND;
	}

	/* tex:/<mod>/<model>/<local>: the model is the segment before the last */
	if (owner >= 0) {
		const char *slash = strrchr(item, '/');

		if (slash && slash > item) {
			char model[96];
			u32 len = (u32)(slash - item);
			s32 modelFileNum;

			if (len >= sizeof(model)) {
				return ASSET_BADPATH;
			}

			memcpy(model, item, len);
			model[len] = '\0';
			modelFileNum = assetFileLookupInMod(model, owner);

			if (modelFileNum < 0) {
				return ASSET_NOTFOUND;
			}

			out->via = modelFileNum;
			item = slash + 1;
		}
	}

	n = assetParseNumber(item);

	if (n < 0) {
		return ASSET_BADPATH;
	}

	if (owner == ASSET_OWNER_NONE || owner == ASSET_OWNER_VANILLA) {
		if (n >= NUM_TEXTURES) {
			return ASSET_NOTFOUND;
		}

		out->owner = ASSET_OWNER_VANILLA;
		out->id = n;
		out->sub = n;
		return ASSET_OK;
	}

	{
		u16 port = modTexMapLookup(owner, (u16)n);

		/* modTexMapLookup hands the local id back unchanged when the mod
		 * has no entry for it; that is "not found", not a port of n */
		if (port == (u16)n && modTexMapReverseLookup(owner, port) != (u16)n) {
			return ASSET_NOTFOUND;
		}

		out->owner = (s8)owner;
		out->id = n;
		out->sub = port;
		return ASSET_OK;
	}
}

static s32 assetTexEnumerate(const char *rest, assetenumfn fn, void *ctx)
{
	s32 owner;
	const char *item;
	s32 err = assetSplitOwner(rest, &owner, &item);
	s32 visited = 0;
	char name[16];
	struct assetref ref;

	if (err != ASSET_OK) {
		return err;
	}

	if (item && item[0]) {
		return ASSET_NOTFOUND;
	}

	if (owner == ASSET_OWNER_NONE || owner == ASSET_OWNER_VANILLA) {
		for (s32 n = 0; n < NUM_TEXTURES; ++n) {
			assetRefClear(&ref);
			ref.drive = ASSET_DRIVE_TEX;
			ref.owner = ASSET_OWNER_VANILLA;
			ref.id = n;
			ref.sub = n;
			snprintf(name, sizeof(name), "0x%04x", n);
			visited++;

			if (!fn(&ref, name, ctx)) {
				return visited;
			}
		}
	}

	for (s32 mod = 0; mod < (s32)g_NumModDirs; ++mod) {
		u16 local;
		u16 port;

		if (owner != ASSET_OWNER_NONE && owner != mod) {
			continue;
		}

		for (s32 i = 0; modTexMapGetEntry(mod, i, &local, &port); ++i) {
			assetRefClear(&ref);
			ref.drive = ASSET_DRIVE_TEX;
			ref.owner = (s8)mod;
			ref.id = local;
			ref.sub = port;
			snprintf(name, sizeof(name), "0x%04x", local);
			visited++;

			if (!fn(&ref, name, ctx)) {
				return visited;
			}
		}
	}

	return visited;
}

/* ------------------------------------------------------------------------
 * stage:
 *
 * By stagenum (0x20, 32) or by stageGetIndexByName. The owner is whichever
 * mod's declaration is live in the stage registry, vanilla otherwise. A
 * field sub-item names one of the five file fields; assetLink follows it
 * to the file: the tagged u32 in g_Stages names, owner included.
 * ---------------------------------------------------------------------- */

/* A mod texture's file, the way modeldef and the debugger's probe find it:
 * modTextureResolveFile with the mod and the asking model, no globals. The
 * model is optional; without one only the flat and prefixed candidates are
 * tried, which is what the resolver does for a model with no per-model dir. */
static s32 assetTexLink(const struct assetref *ref, struct assetref *out)
{
	s32 fileNum;

	if (ref->owner < 0) {
		return ASSET_NOTFOUND;
	}

	fileNum = modTextureResolveFile(ref->owner, ref->via > 0 ? ref->via : 0,
			(u16)(ref->sub >= 0 ? ref->sub : ref->id), NULL, NULL, 0);

	if (fileNum <= 0) {
		return ASSET_NOTFOUND;
	}

	return assetFileRefFromId(MOD_FILEID_MAKE(ref->owner, fileNum), out);
}

static s32 assetStageFieldFromName(const char *s, u32 len)
{
	for (s32 f = 0; f < ASSET_STAGE_FIELD_COUNT; ++f) {
		if (assetSegEquals(s, len, g_AssetStageFieldNames[f])) {
			return f;
		}
	}

	return -1;
}

static s32 assetStageOwner(s32 stagenum)
{
	const struct modStageRegEntry *e = modStageRegFind(stagenum);
	return e ? e->modnum : ASSET_OWNER_VANILLA;
}

/* "0x20", "32" or "ame": the table index, and the stagenum through *stagenum */
static s32 assetStageIndexFromHead(const char *head, s32 *stagenum)
{
	s32 index;

	*stagenum = assetParseNumber(head);

	if (*stagenum >= 0) {
		index = stageGetIndex(*stagenum);
	} else {
		index = stageGetIndexByName(head);
		*stagenum = index >= 0 ? g_Stages[index].id : -1;
	}

	return index;
}

static s32 assetStageResolve(const char *rest, struct assetref *out)
{
	const char *item;
	const char *slash;
	char head[64];
	s32 stagenum;
	s32 index;

	if (rest[0] != '/' || rest[1] == '\0') {
		return rest[0] == '\0' || rest[1] == '\0' ? ASSET_NOTFOUND : ASSET_BADPATH;
	}

	item = rest + 1;
	slash = strchr(item, '/');

	if (slash) {
		u32 n = (u32)(slash - item);

		if (n >= sizeof(head)) {
			return ASSET_BADPATH;
		}

		memcpy(head, item, n);
		head[n] = '\0';
	} else {
		snprintf(head, sizeof(head), "%s", item);
	}

	index = assetStageIndexFromHead(head, &stagenum);

	if (index < 0) {
		return ASSET_NOTFOUND;
	}

	out->drive = ASSET_DRIVE_STAGE;
	out->owner = (s8)assetStageOwner(stagenum);
	out->id = stagenum;

	if (slash) {
		s32 field = assetStageFieldFromName(slash + 1, (u32)strlen(slash + 1));

		if (field < 0) {
			return ASSET_NOTFOUND;
		}

		out->sub = field;
	}

	return ASSET_OK;
}

static u32 assetStageFieldId(const struct stagetableentry *e, s32 field)
{
	switch (field) {
	case ASSET_STAGE_BG:      return e->bgfileid;
	case ASSET_STAGE_TILES:   return e->tilefileid;
	case ASSET_STAGE_PADS:    return e->padsfileid;
	case ASSET_STAGE_SETUP:   return e->setupfileid;
	case ASSET_STAGE_MPSETUP: return e->mpsetupfileid;
	default:                  return 0;
	}
}

static s32 assetStageLink(const struct assetref *ref, struct assetref *out)
{
	s32 index = stageGetIndex(ref->id);

	if (index < 0 || ref->sub < 0 || ref->sub >= ASSET_STAGE_FIELD_COUNT) {
		return ASSET_NOTFOUND;
	}

	return assetFileRefFromId((s32)assetStageFieldId(&g_Stages[index], ref->sub), out);
}

static const char *assetStageDisplayName(s32 stagenum, char *buf, u32 len)
{
	const struct modStageRegEntry *e = modStageRegFind(stagenum);
	const char *name = (e && e->name) ? e->name : stageGetName(stagenum);

	if (name && name[0]) {
		return name;
	}

	snprintf(buf, len, "0x%02x", stagenum);
	return buf;
}

static s32 assetStageEnumerate(const char *rest, assetenumfn fn, void *ctx)
{
	s32 owner;
	const char *item;
	s32 err = assetSplitOwner(rest, &owner, &item);
	s32 visited = 0;
	char buf[16];
	struct assetref ref;

	if (err != ASSET_OK) {
		return err;
	}

	if (item && item[0]) {
		return ASSET_NOTFOUND;
	}

	for (u32 i = 0; i < ARRAYCOUNT(g_Stages); ++i) {
		s32 stagenum = g_Stages[i].id;
		s32 stageOwner = assetStageOwner(stagenum);

		if (owner != ASSET_OWNER_NONE && stageOwner != owner) {
			continue;
		}

		assetRefClear(&ref);
		ref.drive = ASSET_DRIVE_STAGE;
		ref.owner = (s8)stageOwner;
		ref.id = stagenum;
		visited++;

		if (!fn(&ref, assetStageDisplayName(stagenum, buf, sizeof(buf)), ctx)) {
			break;
		}
	}

	return visited;
}

/* ------------------------------------------------------------------------
 * pad:
 *
 * A stage's pads. The rom has no name table for them: bg_<stage>_padsZ is
 * numpads packed records behind an offset table, and the PAD_<STAGE>_<HEX>
 * symbols the setups compile against are generated from the index by
 * tools/assetmgr/mkpads. So a name here is synthesized the same way, and
 * resolving one is parsing its hex tail.
 *
 * Live only. padUnpack reads g_StageSetup.padfiledata, the running stage's
 * file after preprocessPadsFile has laid it out natively; any other
 * stage's file is compressed N64-layout bytes we do not inflate here.
 * ---------------------------------------------------------------------- */

static s32 assetPadOwner(s32 stagenum)
{
	s32 index = stageGetIndex(stagenum);
	s32 owner;

	if (index < 0) {
		return ASSET_OWNER_VANILLA;
	}

	owner = MOD_FILEID_MOD((s32)g_Stages[index].padsfileid);
	return owner >= 0 ? owner : ASSET_OWNER_VANILLA;
}

static s32 assetPadIsLive(s32 stagenum)
{
	return g_Vars.stagenum == stagenum && g_StageSetup.padfiledata != NULL && g_PadsFile != NULL;
}

s32 assetPadCount(s32 stagenum)
{
	return assetPadIsLive(stagenum) ? g_PadsFile->numpads : -1;
}

/* "PAD_AME_0095" -> 0x95. Case-insensitive, stage part ignored: the path
 * already named the stage, and a symbol pasted from another stage's setup
 * is still a number. Anything not PAD_<...>_<hex> is -1. */
static s32 assetPadNumberFromSymbol(const char *item)
{
	const char *tail;

	if (strlen(item) < 5 || (item[0] != 'P' && item[0] != 'p') || (item[1] != 'A' && item[1] != 'a')
			|| (item[2] != 'D' && item[2] != 'd') || item[3] != '_') {
		return -1;
	}

	tail = strrchr(item, '_');

	if (!tail || tail == item + 3 || tail[1] == '\0') {
		return -1;
	}

	tail++;

	for (const char *c = tail; *c; ++c) {
		if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F'))) {
			return -1;
		}
	}

	return (s32)strtol(tail, NULL, 16);
}

static const char *assetPadStageTag(s32 stagenum, char *buf, u32 len)
{
	const char *name = stageGetName(stagenum);
	u32 i;

	if (!name || !name[0]) {
		snprintf(buf, len, "%02X", stagenum);
		return buf;
	}

	for (i = 0; i + 1 < len && name[i]; ++i) {
		char c = name[i];
		buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
	}

	buf[i] = '\0';
	return buf;
}

/* The synthesized symbol, mkpads' shape: PAD_<STAGE upper>_<%04X>. One
 * static buffer, like every other assetName answer this file hands out
 * from a table it does not own the strings of. */
static const char *assetPadName(const struct assetref *ref)
{
	static char buf[48];
	char tag[24];

	if (ref->sub < 0) {
		return NULL;
	}

	snprintf(buf, sizeof(buf), "PAD_%s_%04X", assetPadStageTag(ref->id, tag, sizeof(tag)), ref->sub);
	return buf;
}

static s32 assetPadResolve(const char *rest, struct assetref *out)
{
	const char *item;
	const char *slash;
	char head[64];
	s32 stagenum;
	s32 index;
	s32 padnum;

	if (rest[0] != '/' || rest[1] == '\0') {
		return rest[0] == '\0' || rest[1] == '\0' ? ASSET_NOTFOUND : ASSET_BADPATH;
	}

	item = rest + 1;
	slash = strchr(item, '/');

	if (slash) {
		u32 n = (u32)(slash - item);

		if (n >= sizeof(head)) {
			return ASSET_BADPATH;
		}

		memcpy(head, item, n);
		head[n] = '\0';
	} else {
		snprintf(head, sizeof(head), "%s", item);
	}

	index = assetStageIndexFromHead(head, &stagenum);

	if (index < 0) {
		return ASSET_NOTFOUND;
	}

	out->drive = ASSET_DRIVE_PAD;
	out->owner = (s8)assetPadOwner(stagenum);
	out->id = stagenum;

	if (!slash || slash[1] == '\0') {
		return ASSET_OK; /* the set */
	}

	padnum = assetPadNumberFromSymbol(slash + 1);

	if (padnum < 0) {
		padnum = assetParseNumber(slash + 1);
	}

	if (padnum < 0) {
		return ASSET_NOTFOUND;
	}

	/* a cold stage cannot be bounds-checked without inflating its file;
	 * the live one can, and a number past the end is not a pad */
	if (assetPadIsLive(stagenum) && padnum >= g_PadsFile->numpads) {
		return ASSET_NOTFOUND;
	}

	out->sub = padnum;
	return ASSET_OK;
}

static s32 assetPadLink(const struct assetref *ref, struct assetref *out)
{
	s32 index = stageGetIndex(ref->id);

	if (index < 0) {
		return ASSET_NOTFOUND;
	}

	return assetFileRefFromId((s32)g_Stages[index].padsfileid, out);
}

s32 assetPadUnpack(const struct assetref *ref, struct pad *out)
{
	if (!ref || !out || ref->drive != ASSET_DRIVE_PAD || ref->sub < 0) {
		return ASSET_NOTFOUND;
	}

	if (!assetPadIsLive(ref->id)) {
		return ASSET_UNSUPPORTED;
	}

	if (ref->sub >= g_PadsFile->numpads) {
		return ASSET_NOTFOUND;
	}

	padUnpack(ref->sub, PADFIELD_POS | PADFIELD_LOOK | PADFIELD_UP | PADFIELD_NORMAL
			| PADFIELD_BBOX | PADFIELD_ROOM | PADFIELD_FLAGS | PADFIELD_LIFT, out);
	return ASSET_OK;
}

/* "pad:" and "pad:/" walk the running stage; "pad:/<stage>" that stage,
 * which has to be the running one. Owners are not a level here: a stage's
 * pads all have one owner, the padsfile's. */
static s32 assetPadEnumerate(const char *rest, assetenumfn fn, void *ctx)
{
	s32 stagenum;
	s32 visited = 0;
	struct assetref ref;

	if (rest[0] == '\0' || (rest[0] == '/' && rest[1] == '\0')) {
		stagenum = g_Vars.stagenum;
	} else {
		char head[64];
		const char *slash;
		u32 n;

		if (rest[0] != '/') {
			return ASSET_BADPATH;
		}

		slash = strchr(rest + 1, '/');
		n = slash ? (u32)(slash - (rest + 1)) : (u32)strlen(rest + 1);

		if (n >= sizeof(head)) {
			return ASSET_BADPATH;
		}

		if (slash && slash[1] != '\0') {
			return ASSET_NOTFOUND; /* an item is not a container */
		}

		memcpy(head, rest + 1, n);
		head[n] = '\0';

		if (assetStageIndexFromHead(head, &stagenum) < 0) {
			return ASSET_NOTFOUND;
		}
	}

	if (!assetPadIsLive(stagenum)) {
		return ASSET_UNSUPPORTED;
	}

	for (s32 i = 0; i < g_PadsFile->numpads; ++i) {
		assetRefClear(&ref);
		ref.drive = ASSET_DRIVE_PAD;
		ref.owner = (s8)assetPadOwner(stagenum);
		ref.id = stagenum;
		ref.sub = i;
		visited++;

		if (!fn(&ref, assetPadName(&ref), ctx)) {
			break;
		}
	}

	return visited;
}

/* ------------------------------------------------------------------------
 * head: body: hand:
 *
 * Name-keyed. id is the g_HeadsAndBodies index (what carries the file id),
 * sub is the mp slot the name resolves to, owner comes off the file id's
 * tag. hand: has only the names mods declared; vanilla hands are unnamed.
 * ---------------------------------------------------------------------- */

static s32 assetHeadBodyRefFromIndex(s32 drive, s32 hbIndex, s32 mpSlot, struct assetref *out)
{
	s32 mod;

	if (hbIndex < 0 || hbIndex >= g_NumHeadsAndBodies) {
		return ASSET_NOTFOUND;
	}

	mod = MOD_FILEID_MOD((s32)g_HeadsAndBodies[hbIndex].filenum);
	assetRefClear(out);
	out->drive = (u8)drive;
	out->owner = (s8)(mod < 0 ? ASSET_OWNER_VANILLA : mod);
	out->id = hbIndex;
	out->sub = mpSlot;
	return ASSET_OK;
}

static s32 assetHeadBodyResolve(s32 drive, const char *rest, struct assetref *out)
{
	const char *name;
	s32 slot;
	s32 hbIndex;

	if (rest[0] != '/' || rest[1] == '\0') {
		return rest[0] == '\0' || rest[1] == '\0' ? ASSET_NOTFOUND : ASSET_BADPATH;
	}

	name = rest + 1;

	if (drive == ASSET_DRIVE_HEAD) {
		slot = modLookupHeadByName(name);
		hbIndex = slot >= 0 ? g_MpHeads[slot].headnum : modLookupHeadnumByName(name);
	} else {
		slot = modLookupBodyByName(name);
		hbIndex = slot >= 0 ? g_MpBodies[slot].bodynum : -1;
	}

	return assetHeadBodyRefFromIndex(drive, hbIndex, slot, out);
}

static s32 assetHeadBodyEnumerate(s32 drive, const char *rest, assetenumfn fn, void *ctx)
{
	s32 owner;
	const char *item;
	s32 err = assetSplitOwner(rest, &owner, &item);
	s32 visited = 0;
	s32 count = drive == ASSET_DRIVE_HEAD ? g_NumMpHeads : g_NumMpBodies;
	struct assetref ref;

	if (err != ASSET_OK) {
		return err;
	}

	if (item && item[0]) {
		return ASSET_NOTFOUND;
	}

	for (s32 slot = 0; slot < count; ++slot) {
		s32 hbIndex = drive == ASSET_DRIVE_HEAD ? g_MpHeads[slot].headnum : g_MpBodies[slot].bodynum;
		const char *name = modGetNameForHeadBodyIndex(hbIndex);

		if (!name || assetHeadBodyRefFromIndex(drive, hbIndex, slot, &ref) != ASSET_OK) {
			continue;
		}

		if (owner != ASSET_OWNER_NONE && ref.owner != owner) {
			continue;
		}

		visited++;

		if (!fn(&ref, name, ctx)) {
			break;
		}
	}

	return visited;
}

static s32 assetHandResolve(const char *rest, struct assetref *out)
{
	s32 fileId;
	s32 mod;

	if (rest[0] != '/' || rest[1] == '\0') {
		return rest[0] == '\0' || rest[1] == '\0' ? ASSET_NOTFOUND : ASSET_BADPATH;
	}

	fileId = modLookupHandFileByName(rest + 1);

	if (fileId < 0) {
		return ASSET_NOTFOUND;
	}

	for (s32 i = 0; i < modHandNameCount(); ++i) {
		u32 fn;

		if (modHandName(i, &fn) && (s32)fn == fileId) {
			mod = MOD_FILEID_MOD(fileId);
			out->drive = ASSET_DRIVE_HAND;
			out->owner = (s8)(mod < 0 ? ASSET_OWNER_VANILLA : mod);
			out->id = i;
			out->sub = -1;
			return ASSET_OK;
		}
	}

	return ASSET_NOTFOUND;
}

static s32 assetHandEnumerate(const char *rest, assetenumfn fn, void *ctx)
{
	s32 owner;
	const char *item;
	s32 err = assetSplitOwner(rest, &owner, &item);
	s32 visited = 0;
	struct assetref ref;

	if (err != ASSET_OK) {
		return err;
	}

	if (item && item[0]) {
		return ASSET_NOTFOUND;
	}

	for (s32 i = 0; i < modHandNameCount(); ++i) {
		u32 fileId = 0;
		const char *name = modHandName(i, &fileId);
		s32 mod = MOD_FILEID_MOD((s32)fileId);

		assetRefClear(&ref);
		ref.drive = ASSET_DRIVE_HAND;
		ref.owner = (s8)(mod < 0 ? ASSET_OWNER_VANILLA : mod);
		ref.id = i;

		if (owner != ASSET_OWNER_NONE && ref.owner != owner) {
			continue;
		}

		visited++;

		if (!fn(&ref, name, ctx)) {
			break;
		}
	}

	return visited;
}

static s32 assetHeadBodyHandLink(const struct assetref *ref, struct assetref *out)
{
	switch (ref->drive) {
	case ASSET_DRIVE_HEAD:
	case ASSET_DRIVE_BODY:
		if (ref->id < 0 || ref->id >= g_NumHeadsAndBodies) {
			return ASSET_NOTFOUND;
		}

		return assetFileRefFromId((s32)g_HeadsAndBodies[ref->id].filenum, out);
	case ASSET_DRIVE_HAND: {
		u32 fileId = 0;

		if (!modHandName(ref->id, &fileId)) {
			return ASSET_NOTFOUND;
		}

		return assetFileRefFromId((s32)fileId, out);
	}
	default:
		return ASSET_NOTFOUND;
	}
}

/* ------------------------------------------------------------------------
 * seg: rom: mod:  - flat, name-keyed, no owners
 * ---------------------------------------------------------------------- */

static s32 assetFlatResolve(s32 drive, const char *rest, struct assetref *out)
{
	const char *name;
	s32 count;

	if (rest[0] != '/' || rest[1] == '\0') {
		return rest[0] == '\0' || rest[1] == '\0' ? ASSET_NOTFOUND : ASSET_BADPATH;
	}

	name = rest + 1;
	out->drive = (u8)drive;

	switch (drive) {
	case ASSET_DRIVE_SEG:
		count = romdataSegCount();

		for (s32 i = 0; i < count; ++i) {
			if (!strcmp(romdataSegName(i), name)) {
				out->id = i;
				return ASSET_OK;
			}
		}

		return ASSET_NOTFOUND;
	case ASSET_DRIVE_ROM:
		count = romsourceCount();

		for (s32 i = 0; i < count; ++i) {
			const char *id = NULL;

			if (romsourceInfo(i, &id, NULL, NULL) && !strcmp(id, name)) {
				out->id = i;
				return ASSET_OK;
			}
		}

		return ASSET_NOTFOUND;
	case ASSET_DRIVE_MOD: {
		s32 mod = assetOwnerFromSegment(name, (u32)strlen(name));

		if (mod < 0) {
			return ASSET_NOTFOUND;
		}

		out->owner = (s8)mod;
		out->id = mod;
		return ASSET_OK;
	}
	default:
		return ASSET_BADPATH;
	}
}

static const char *assetFlatName(const struct assetref *ref)
{
	const char *id = NULL;

	switch (ref->drive) {
	case ASSET_DRIVE_SEG:
		return romdataSegName(ref->id);
	case ASSET_DRIVE_ROM:
		return romsourceInfo(ref->id, &id, NULL, NULL) ? id : NULL;
	case ASSET_DRIVE_MOD:
		return (ref->id >= 0 && ref->id < (s32)g_NumModDirs) ? assetModLabel(ref->id) : NULL;
	default:
		return NULL;
	}
}

static s32 assetFlatEnumerate(s32 drive, const char *rest, assetenumfn fn, void *ctx)
{
	s32 visited = 0;
	s32 count;
	struct assetref ref;

	if (rest[0] != '\0' && strcmp(rest, "/")) {
		return ASSET_NOTFOUND;
	}

	switch (drive) {
	case ASSET_DRIVE_SEG: count = romdataSegCount(); break;
	case ASSET_DRIVE_ROM: count = romsourceCount(); break;
	case ASSET_DRIVE_MOD: count = (s32)g_NumModDirs; break;
	default: return ASSET_BADPATH;
	}

	for (s32 i = 0; i < count; ++i) {
		const char *name;

		assetRefClear(&ref);
		ref.drive = (u8)drive;
		ref.id = i;

		if (drive == ASSET_DRIVE_MOD) {
			ref.owner = (s8)i;
		}

		name = assetFlatName(&ref);

		if (!name) {
			continue;
		}

		visited++;

		if (!fn(&ref, name, ctx)) {
			break;
		}
	}

	return visited;
}

/* ------------------------------------------------------------------------
 * the verbs
 * ---------------------------------------------------------------------- */

s32 assetResolve(const char *path, struct assetref *out)
{
	const char *rest = NULL;
	s32 drive;

	if (!path || !out) {
		return ASSET_BADPATH;
	}

	assetRefClear(out);
	drive = assetDriveFromPath(path, &rest);

	switch (drive) {
	case ASSET_DRIVE_FILE:  return assetFileResolve(rest, out);
	case ASSET_DRIVE_TEX:   return assetTexResolve(rest, out);
	case ASSET_DRIVE_STAGE: return assetStageResolve(rest, out);
	case ASSET_DRIVE_HEAD:
	case ASSET_DRIVE_BODY:  return assetHeadBodyResolve(drive, rest, out);
	case ASSET_DRIVE_HAND:  return assetHandResolve(rest, out);
	case ASSET_DRIVE_SEG:
	case ASSET_DRIVE_ROM:
	case ASSET_DRIVE_MOD:   return assetFlatResolve(drive, rest, out);
	case ASSET_DRIVE_PAD:   return assetPadResolve(rest, out);
	default:                return ASSET_BADPATH;
	}
}

const char *assetName(const struct assetref *ref)
{
	if (!ref) {
		return NULL;
	}

	switch (ref->drive) {
	case ASSET_DRIVE_FILE:
		return assetFileName(ref);
	case ASSET_DRIVE_STAGE:
		return ref->sub >= 0 ? NULL : stageGetName(ref->id);
	case ASSET_DRIVE_HEAD:
	case ASSET_DRIVE_BODY:
		return modGetNameForHeadBodyIndex(ref->id);
	case ASSET_DRIVE_HAND:
		return modHandName(ref->id, NULL);
	case ASSET_DRIVE_SEG:
	case ASSET_DRIVE_ROM:
	case ASSET_DRIVE_MOD:
		return assetFlatName(ref);
	case ASSET_DRIVE_PAD:
		return assetPadName(ref);
	default:
		return NULL;
	}
}

s32 assetFormat(const struct assetref *ref, char *dst, u32 len)
{
	const char *name;

	if (!ref || !dst || !len) {
		return -1;
	}

	switch (ref->drive) {
	case ASSET_DRIVE_FILE:
		name = assetFileName(ref);
		return name ? snprintf(dst, len, "file:/%s/%s", assetModLabel(ref->owner), name) : -1;
	case ASSET_DRIVE_TEX:
		if (ref->owner >= 0 && ref->via > 0) {
			const char *model = romdataFileGetSlotName(ref->owner, ref->via);
			const char *sep = model ? strstr(model, "::") : NULL;

			model = sep ? sep + 2 : model;

			if (model && !strncmp(model, "files/", 6)) {
				model += 6;
			}

			if (model) {
				return snprintf(dst, len, "tex:/%s/%s/0x%04x", assetModLabel(ref->owner), model, ref->id);
			}
		}

		return snprintf(dst, len, "tex:/%s/0x%04x", assetModLabel(ref->owner), ref->id);
	case ASSET_DRIVE_STAGE:
		if (stageGetIndex(ref->id) < 0) {
			return -1;
		}

		if (ref->sub >= 0 && ref->sub < ASSET_STAGE_FIELD_COUNT) {
			return snprintf(dst, len, "stage:/0x%02x/%s", ref->id, g_AssetStageFieldNames[ref->sub]);
		}

		return snprintf(dst, len, "stage:/0x%02x", ref->id);
	case ASSET_DRIVE_PAD:
		if (stageGetIndex(ref->id) < 0) {
			return -1;
		}

		if (ref->sub >= 0) {
			return snprintf(dst, len, "pad:/0x%02x/0x%04x", ref->id, ref->sub);
		}

		return snprintf(dst, len, "pad:/0x%02x", ref->id);
	case ASSET_DRIVE_HEAD:
	case ASSET_DRIVE_BODY:
	case ASSET_DRIVE_HAND:
	case ASSET_DRIVE_SEG:
	case ASSET_DRIVE_ROM:
	case ASSET_DRIVE_MOD:
		name = assetName(ref);
		return name ? snprintf(dst, len, "%s:/%s", g_AssetDriveNames[ref->drive], name) : -1;
	default:
		return -1;
	}
}

s32 assetLink(const struct assetref *ref, struct assetref *out)
{
	if (!ref || !out) {
		return ASSET_BADPATH;
	}

	assetRefClear(out);

	switch (ref->drive) {
	case ASSET_DRIVE_STAGE:
		return assetStageLink(ref, out);
	case ASSET_DRIVE_PAD:
		return assetPadLink(ref, out);
	case ASSET_DRIVE_TEX:
		return assetTexLink(ref, out);
	case ASSET_DRIVE_HEAD:
	case ASSET_DRIVE_BODY:
	case ASSET_DRIVE_HAND:
		return assetHeadBodyHandLink(ref, out);
	default:
		return ASSET_NOTFOUND;
	}
}

/* ------------------------------------------------------------------------
 * load and exists
 * ---------------------------------------------------------------------- */

#define ASSET_SLOT_UNLOADED 0   /* enum loadsource SRC_UNLOADED, romdata.c */

/* What the game would serve for this file, which for a vanilla id is NOT
 * always the ROM's bytes: romdataFileLoad walks the mounted mod dirs for
 * an untagged id too, so a mod that ships a loose file under a vanilla
 * name shadows the ROM (mod_gex_stages does this to 14 of the 16 arenas).
 * Get-Content answers with what loads. The ROM's own bytes for a slot are
 * a different question - romdataRomFileData - and not this verb's. */
static void *assetFileLoad(const struct assetref *ref, u32 *outSize)
{
	if (ref->owner == ASSET_OWNER_VANILLA) {
		if (!romdataRomFileName(ref->id)) {
			return NULL;
		}

		return romdataFileLoad(ref->id, outSize);
	}

	if (ref->owner < 0 || ref->id < 1) {
		return NULL;
	}

	return romdataFileLoad(MOD_FILEID_MAKE(ref->owner, ref->id), outSize);
}

static void *assetTexLoad(const struct assetref *ref, u32 *outSize)
{
	if (ref->owner != ASSET_OWNER_VANILLA) {
		/* the file the engine's resolver picks for (mod, model, id); the
		 * runtime path (modTextureLoad) reads the same answer off two
		 * globals, which is c-texture-provenance's problem, not this one's */
		struct assetref file;

		if (assetTexLink(ref, &file) != ASSET_OK) {
			return NULL;
		}

		return assetFileLoad(&file, outSize);
	}

	if (!g_Textures || ref->id < 0 || ref->id + 1 >= NUM_TEXTURES) {
		return NULL;
	}

	u8 *seg = romdataSegGetData("texturesdata");
	u32 segSize = romdataSegGetSize("texturesdata");
	u32 ofs = g_Textures[ref->id].dataoffset & 0xfffffff8u;
	u32 next = g_Textures[ref->id + 1].dataoffset & 0xfffffff8u;

	if (!seg || next <= ofs || next > segSize) {
		return NULL;
	}

	if (outSize) *outSize = next - ofs;
	return seg + ofs;
}

void *assetLoad(const struct assetref *ref, u32 *outSize)
{
	struct assetref target;
	u32 size = 0;
	void *data = NULL;

	if (outSize) *outSize = 0;

	if (!ref) {
		return NULL;
	}

	switch (ref->drive) {
	case ASSET_DRIVE_FILE:
		data = assetFileLoad(ref, &size);
		break;
	case ASSET_DRIVE_TEX:
		data = assetTexLoad(ref, &size);
		break;
	case ASSET_DRIVE_SEG:
		data = romdataSegGetData(romdataSegName(ref->id) ? romdataSegName(ref->id) : "");
		size = data ? romdataSegGetSize(romdataSegName(ref->id)) : 0;
		break;
	case ASSET_DRIVE_STAGE:
	case ASSET_DRIVE_PAD:
	case ASSET_DRIVE_HEAD:
	case ASSET_DRIVE_BODY:
	case ASSET_DRIVE_HAND:
		if (assetLink(ref, &target) == ASSET_OK) {
			data = assetFileLoad(&target, &size);
		}
		break;
	default:
		break;
	}

	if (data && outSize) *outSize = size;
	return data;
}

s32 assetExists(const struct assetref *ref)
{
	struct assetref target;
	struct romdatafileslotinfo info;
	s32 wasLoaded;

	if (!ref) {
		return 0;
	}

	switch (ref->drive) {
	case ASSET_DRIVE_STAGE:
		if (ref->sub < 0) {
			/* a stage row is not bytes; it exists if the table has it */
			return stageGetIndex(ref->id) >= 0;
		}
		/* fallthrough */
	case ASSET_DRIVE_PAD:
		/* a live pad exists if its number is in range; a cold one, or the
		 * set itself, exists if the file it sits in does */
		if (ref->drive == ASSET_DRIVE_PAD && ref->sub >= 0 && assetPadIsLive(ref->id)) {
			return ref->sub < g_PadsFile->numpads;
		}
		/* fallthrough */
	case ASSET_DRIVE_HEAD:
	case ASSET_DRIVE_BODY:
	case ASSET_DRIVE_HAND:
		return assetLink(ref, &target) == ASSET_OK && assetExists(&target);
	case ASSET_DRIVE_FILE: {
		/* probe through the real loader, then put the slot back the way it
		 * was: romdataFileFree frees an external load and just marks a ROM
		 * or alt-rom slot unloaded, so a probe leaves nothing resident that
		 * was not resident before. A vanilla id lives in the boot mod's row
		 * (g_ModNum), which is where romdataFileLoad puts an untagged id. */
		const s32 row = ref->owner == ASSET_OWNER_VANILLA ? g_ModNum : ref->owner;
		const s32 id = ref->owner == ASSET_OWNER_VANILLA ? ref->id : MOD_FILEID_MAKE(ref->owner, ref->id);

		wasLoaded = romdataGetFileSlotInfo(row, ref->id, &info)
			&& info.source != ASSET_SLOT_UNLOADED;

		if (!assetFileLoad(ref, NULL)) {
			return 0;
		}

		if (!wasLoaded) {
			romdataFileFree(id);
		}

		return 1;
	}
	case ASSET_DRIVE_TEX:
		if (ref->owner != ASSET_OWNER_VANILLA) {
			return assetTexLink(ref, &target) == ASSET_OK && assetExists(&target);
		}

		return assetLoad(ref, NULL) != NULL;
	case ASSET_DRIVE_SEG:
		return assetLoad(ref, NULL) != NULL;
	case ASSET_DRIVE_ROM: {
		u8 mounted = 0;
		return romsourceInfo(ref->id, NULL, NULL, &mounted) && mounted;
	}
	case ASSET_DRIVE_MOD:
		return ref->id >= 0 && ref->id < (s32)g_NumModDirs;
	default:
		return ASSET_UNSUPPORTED;
	}
}

s32 assetEnumerate(const char *path, assetenumfn fn, void *ctx)
{
	const char *rest = NULL;
	s32 drive;

	if (!path || !fn) {
		return ASSET_BADPATH;
	}

	drive = assetDriveFromPath(path, &rest);

	switch (drive) {
	case ASSET_DRIVE_FILE:  return assetFileEnumerate(rest, fn, ctx);
	case ASSET_DRIVE_TEX:   return assetTexEnumerate(rest, fn, ctx);
	case ASSET_DRIVE_STAGE: return assetStageEnumerate(rest, fn, ctx);
	case ASSET_DRIVE_HEAD:
	case ASSET_DRIVE_BODY:  return assetHeadBodyEnumerate(drive, rest, fn, ctx);
	case ASSET_DRIVE_HAND:  return assetHandEnumerate(rest, fn, ctx);
	case ASSET_DRIVE_SEG:
	case ASSET_DRIVE_ROM:
	case ASSET_DRIVE_MOD:   return assetFlatEnumerate(drive, rest, fn, ctx);
	case ASSET_DRIVE_PAD:   return assetPadEnumerate(rest, fn, ctx);
	default:                return ASSET_BADPATH;
	}
}
