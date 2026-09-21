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

/* ------------------------------------------------------------------------
 * the file: drive's enumerate cache
 *
 * This used to be g_ImGuiOverlayFileIndex in imgui_overlay.cpp, built for
 * the debugger bar. fileSlots has no occupancy list, so the only way to list
 * a mod's files is to probe all 8191 slots of its row, and doing that per
 * keystroke was not acceptable. It is built once and invalidated on the only
 * two events that change it: a stage load and a change to the mod roster.
 * Moving it here changes nothing the bar can see; it makes the bar the first
 * consumer instead of the only one.
 * ---------------------------------------------------------------------- */

#define ASSET_FILE_NAME_MAX 48

struct assetfileentry {
	s32 mod;
	s32 fileNum;
	char name[ASSET_FILE_NAME_MAX];
};

static struct assetfileentry *g_AssetFileIndex = NULL;
static s32 g_AssetFileIndexCount = 0;
static s32 g_AssetFileIndexCap = 0;
static s32 g_AssetFileIndexMods = -1;
static s32 g_AssetFileIndexStage = -1;

static void assetFileIndexPush(s32 mod, s32 fileNum, const char *name)
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
	entry->mod = mod;
	entry->fileNum = fileNum;
	snprintf(entry->name, sizeof(entry->name), "%s", name);
}

static void assetFileIndexBuild(void)
{
	const s32 mods = (s32)g_NumModDirs;
	const s32 stage = (s32)g_Vars.stagenum;

	if (mods == g_AssetFileIndexMods && stage == g_AssetFileIndexStage) {
		return;
	}

	g_AssetFileIndexCount = 0;

	for (s32 mod = 0; mod < mods; ++mod) {
		for (s32 fileNum = 1; fileNum < 8192; ++fileNum) {
			struct romdatafileslotinfo slotInfo;

			if (!romdataGetFileSlotInfo(mod, fileNum, &slotInfo) || !slotInfo.name) {
				continue;
			}

			assetFileIndexPush(mod, fileNum, slotInfo.name);
		}
	}

	g_AssetFileIndexMods = mods;
	g_AssetFileIndexStage = stage;
	sysLogPrintf(LOG_NOTE, "ASSET: file slot index built, %d entries across %d mod(s)",
			g_AssetFileIndexCount, mods);
}

/* ------------------------------------------------------------------------
 * owners
 * ---------------------------------------------------------------------- */

/* Same rule romdataModLabel uses for the ambiguity warning: the modconfig
 * name if the mod declared one, else its dir. */
static const char *assetModLabel(s32 mod)
{
	if (mod >= 0 && mod < 64 && g_ModNames[mod][0]) {
		return g_ModNames[mod];
	}

	if (mod >= 0 && mod < (s32)g_NumModDirs && modDirs[mod][0]) {
		return modDirs[mod];
	}

	return "(unnamed)";
}

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

static s32 assetOwnerFromSegment(const char *seg, u32 len)
{
	if (!len) {
		return -1;
	}

	for (s32 mod = 0; mod < (s32)g_NumModDirs; ++mod) {
		const char *name = (mod < 64 && g_ModNames[mod][0]) ? g_ModNames[mod] : NULL;
		const char *dir = modDirs[mod][0] ? modDirs[mod] : NULL;

		if (name && strlen(name) == len && !strncmp(name, seg, len)) {
			return mod;
		}

		if (dir) {
			const char *base = assetBasename(dir);

			if ((strlen(dir) == len && !strncmp(dir, seg, len))
					|| (strlen(base) == len && !strncmp(base, seg, len))) {
				return mod;
			}
		}
	}

	return -1;
}

/* ------------------------------------------------------------------------
 * the grammar
 *
 * drive:/owner/item  - splits at the drive, then one optional owner segment,
 * then the rest verbatim. Only file: exists yet; the parse is the same for
 * every drive that follows.
 * ---------------------------------------------------------------------- */

static s32 assetDriveFromPath(const char *path, const char **rest)
{
	const char *colon = strchr(path, ':');

	if (!colon || colon == path) {
		return ASSET_DRIVE_NONE;
	}

	*rest = colon + 1;

	if ((size_t)(colon - path) == 4 && !strncmp(path, "file", 4)) {
		return ASSET_DRIVE_FILE;
	}

	return ASSET_DRIVE_NONE;
}

/* file:/<owner>/<item> or file:/<item>. Returns the owner (or -1 for
 * unqualified) and points item at the rest. A leading '/' is required after
 * the drive so "file:" and "file:/" both read as the drive root. */
static s32 assetFileSplit(const char *rest, s32 *owner, const char **item)
{
	*owner = -1;
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

	if (mod >= 0) {
		/* file:/<owner>/<item>, or file:/<owner> which is that owner's root */
		*owner = mod;
		*item = slash ? slash + 1 : NULL;
		return ASSET_OK;
	}

	/* no owner segment matched: the whole thing is the item, and vanilla
	 * names containing '/' (bgdata/bg_*.seg) land here on purpose */
	*item = rest;
	return ASSET_OK;
}

/* "#3412" is a literal slot number, as the bar already reads it */
static s32 assetFileNumFromItem(const char *item)
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
	return (num >= 1 && num < 8192) ? num : -1;
}

static s32 assetFileLookupInMod(const char *item, s32 mod)
{
	s32 literal = assetFileNumFromItem(item);

	if (literal >= 0) {
		return romdataFileGetSlotName(mod, literal) ? literal : -1;
	}

	return romdataFileGetNumForNameInMod(item, mod);
}

s32 assetResolve(const char *path, struct assetref *out)
{
	const char *rest = NULL;
	s32 owner;
	const char *item;
	s32 err;

	if (!path || !out) {
		return ASSET_BADPATH;
	}

	memset(out, 0, sizeof(*out));
	out->owner = -1;
	out->id = -1;
	out->sub = -1;

	switch (assetDriveFromPath(path, &rest)) {
	case ASSET_DRIVE_FILE:
		err = assetFileSplit(rest, &owner, &item);

		if (err != ASSET_OK) {
			return err;
		}

		if (!item || !item[0]) {
			/* the drive root is a container, not an item */
			return ASSET_NOTFOUND;
		}

		out->drive = ASSET_DRIVE_FILE;

		if (owner >= 0) {
			s32 id = assetFileLookupInMod(item, owner);

			if (id < 0) {
				return ASSET_NOTFOUND;
			}

			out->owner = (s8)owner;
			out->id = id;
			return ASSET_OK;
		}

		/* unqualified: exactly one owner resolves, more than one refuses */
		{
			s32 hitMod = -1;
			s32 hitId = -1;
			s32 hits = 0;

			for (s32 mod = 0; mod < (s32)g_NumModDirs; ++mod) {
				s32 id = assetFileLookupInMod(item, mod);

				if (id >= 0) {
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

	default:
		return ASSET_BADPATH;
	}
}

const char *assetName(const struct assetref *ref)
{
	if (!ref) {
		return NULL;
	}

	switch (ref->drive) {
	case ASSET_DRIVE_FILE:
		return romdataFileGetSlotName(ref->owner, ref->id);
	default:
		return NULL;
	}
}

s32 assetFormat(const struct assetref *ref, char *dst, u32 len)
{
	const char *name = assetName(ref);

	if (!name || !dst || !len) {
		return -1;
	}

	switch (ref->drive) {
	case ASSET_DRIVE_FILE:
		return snprintf(dst, len, "file:/%s/%s", assetModLabel(ref->owner), name);
	default:
		return -1;
	}
}

s32 assetEnumerate(const char *path, assetenumfn fn, void *ctx)
{
	const char *rest = NULL;
	s32 owner;
	const char *item;
	s32 err;

	if (!path || !fn) {
		return ASSET_BADPATH;
	}

	switch (assetDriveFromPath(path, &rest)) {
	case ASSET_DRIVE_FILE:
		err = assetFileSplit(rest, &owner, &item);

		if (err != ASSET_OK) {
			return err;
		}

		if (item && item[0]) {
			/* file:/<owner>/<item> or file:/<something>: a file is a leaf */
			return ASSET_NOTFOUND;
		}

		assetFileIndexBuild();

		{
			s32 visited = 0;

			for (s32 i = 0; i < g_AssetFileIndexCount; ++i) {
				const struct assetfileentry *entry = &g_AssetFileIndex[i];
				struct assetref ref;

				if (owner >= 0 && entry->mod != owner) {
					continue;
				}

				ref.drive = ASSET_DRIVE_FILE;
				ref.owner = (s8)entry->mod;
				ref.id = entry->fileNum;
				ref.sub = -1;
				visited++;

				if (!fn(&ref, entry->name, ctx)) {
					break;
				}
			}

			return visited;
		}

	default:
		return ASSET_BADPATH;
	}
}
