#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h> // model-swap overlay ROM folder scan
#include <sys/stat.h>
#include <PR/ultratypes.h>
#include "gbiex.h"
#include "lib/rzip.h"
#include "romdata.h"
// The flag bits below, and the per-file name rules, shared verbatim with
// tools/mkfiletable so the builder cannot certify a table this reader
// mis-parses. Freestanding on purpose; see the header.
#include "pdftrules.h"
#include "rompatch.h"
#include "fs.h"
#include "system.h"
#include "preprocess.h"
#include "platform.h"
#include "data.h"
#include "mod.h"
#include "bss.h"

#include "constants.h"

/**
 * asset files and ROM segments can be replaced by optional external files,
 * but asset filenames still have to be either pulled from the ROM or from an
 * external file, so stuff can't be completely custom
 *
 * all data is assumed to be big endian, so it has to be byteswapped
 * at load time, which is fucking terrible
 */

#define ROMDATA_FILEDIR "files"
#define ROMDATA_SEGDIR "segs"

#define ROMDATA_ROM_NAME "pd." VERSION_ROMID ".z64"
#define ROMDATA_ROM_SIZE 33554432

#if VERSION == VERSION_NTSC_FINAL
#define ROMDATA_ROM_TITLE "Perfect Dark"
#define ROMDATA_ROM_ID "NPDE"
#define ROMDATA_ROM_DESC "NTSC v1.1"
#define ROMDATA_FILES_OFS 0x28080
#define ROMDATA_DATA_OFS 0x39850
#elif VERSION == VERSION_PAL_FINAL
#define ROMDATA_ROM_TITLE "Perfect Dark"
#define ROMDATA_ROM_ID "NPDP"
#define ROMDATA_ROM_DESC "PAL"
#define ROMDATA_FILES_OFS 0x28910
#define ROMDATA_DATA_OFS 0x39850
#elif VERSION == VERSION_JPN_FINAL
#define ROMDATA_ROM_TITLE "PERFECT DARK"
#define ROMDATA_ROM_ID "NPDJ"
#define ROMDATA_ROM_DESC "JPN"
#define ROMDATA_FILES_OFS 0x28800
#define ROMDATA_DATA_OFS 0x39850
#else
#error "This ROM version is unsupported."
#endif

#define ROMDATA_MAX_FILES 8192

#define GBC_ROM_NAME "pd.gbc"
#define GBC_ROM_SIZE 4194304

static bool g_DebugFileLoad = false;
#define DEBUG_FLOAD(...) if (g_DebugFileLoad) { sysLogPrintf(LOG_NOTE, __VA_ARGS__); }
static bool g_DebugFileTable = false;
#define PDFT(...) if (g_DebugFileTable) { sysLogPrintf(LOG_NOTE, "PDFT " __VA_ARGS__); }

#define FT_HASH_BITS  11
#define FT_HASH_SIZE  (1u << FT_HASH_BITS)          // 2048 buckets
#define FT_HASH_MASK  (FT_HASH_SIZE - 1)
#define FT_POOL_MAX   ROMDATA_MAX_FILES

struct ftEntry {
	const char *name;   // points into externalFileTableData (not owned)
	u16         nameLen;
	u16         fileId;
	s8          ownerMod;  // -1 = global/vanilla; 0..N = per-mod entry
	struct ftEntry *next;
};

static struct ftEntry  ftPool[FT_POOL_MAX];
static struct ftEntry *ftBuckets[FT_HASH_SIZE];
static u32             ftPoolUsed;

static inline u32 ftHash(const char *s, u32 len)
{
	u32 h = 0x811c9dc5u; // FNV offset basis
	for (u32 i = 0; i < len; i++) {
		h ^= (u8)s[i];
		h *= 0x01000193u; // FNV prime
	}
	return h & FT_HASH_MASK;
}

static inline void ftInsert(const char *name, u16 nameLen, u16 fileId, s8 ownerMod)
{
	if (ftPoolUsed >= FT_POOL_MAX) {
		sysLogPrintf(LOG_WARNING, "ftInsert: pool exhausted (%u entries)", ftPoolUsed);
		return;
	}
	u32 bucket = ftHash(name, nameLen);
	struct ftEntry *e = &ftPool[ftPoolUsed++];
	e->name     = name;
	e->nameLen  = nameLen;
	e->fileId   = fileId;
	e->ownerMod = ownerMod;
	e->next    = ftBuckets[bucket];
	ftBuckets[bucket] = e;
}

// Returns file ID or -1. Any owner (first match wins). Kept for future callers
// that don't need mod scoping; currently unused but marked to avoid warnings.
__attribute__((unused))
static inline s32 ftLookup(const char *name, u32 nameLen)
{
	u32 bucket = ftHash(name, nameLen);
	for (struct ftEntry *e = ftBuckets[bucket]; e; e = e->next) {
		if (e->nameLen == nameLen && !strncmp(e->name, name, nameLen)) {
			return e->fileId;
		}
	}
	return -1;
}

// Mod-scoped lookup: prefer entries owned by `modNum`; fall back to a global
// (ownerMod==-1) entry if no mod-owned match exists in the bucket.
static inline s32 ftLookupInMod(const char *name, u32 nameLen, s32 modNum)
{
	u32 bucket = ftHash(name, nameLen);
	s32 fallback = -1;
	for (struct ftEntry *e = ftBuckets[bucket]; e; e = e->next) {
		if (e->nameLen != nameLen || strncmp(e->name, name, nameLen)) continue;
		if (e->ownerMod == (s8)modNum) return e->fileId;
		if (e->ownerMod < 0 && fallback < 0) fallback = e->fileId;
	}
	return fallback;
}

static inline void ftReset(void)
{
	memset(ftBuckets, 0, sizeof(ftBuckets));
	ftPoolUsed = 0;
}

u8 *g_RomFile;
u32 g_RomFileSize;

extern u32 g_NumModDirs;
extern char modDirs[64][FS_MAXPATH + 1];
static u8 *romDataSeg;
static u32 romDataSegSize;
static const char *romName = ROMDATA_ROM_NAME;
s32 loadingFileNum;

enum loadsource {
	SRC_UNLOADED = 0,
	SRC_ROM,
	SRC_EXTERNAL,
	SRC_ALT_ROM,
};

struct romfilepatch {
	u32 ofs;
	u32 len;
	const char *src;
	const char *dst;
};

struct romfile {
	u8 **segstart;
	u8 **segend;
	const char *name;
	u8 *data;
	u32 size;
	preprocessfunc preprocess;
	s32 source; // enum loadsource
	s32 preprocessed;
	const struct romfilepatch *patches;
	u32 numpatches;
};

#define ROMSOURCES_MAX 8

struct romsource {
	char id[16];
	char filename[64];
	u8  *data;
	u32  size;
	u32  expectedSize;
	u8   flags;       // bit0=required, bit1=strict. NOT the PDFT_F_* bits:
	                  // this is a romSource's own field and shares nothing
	                  // with a file entry's flags but the word.
	u8   fallback;    // 0=skip, 1=vanilla, 2=error
	u8   mounted;

	// PDFT_RS_PATCHED: a base rom plus an xdelta, mounted as an OVERLAY over
	// the base's mapping rather than as an image of its own. data stays
	// NULL; ov answers reads. base is another source's id or
	// PDFT_ROMSOURCE_BASE for g_RomFile; patch is a path inside the owning
	// mod's directory, resolved like a self source; expectedCrc32 is what
	// the builder measured over the patched image, carried for tooling (the
	// patch's own per-window adler32 is what proves the bytes at mount).
	char base[16];
	char patch[64];
	u32  expectedCrc32;
	s32  ownerMod;
	struct romoverlay *ov;
};

// A declared source that is NOT a mounted image: the file at this entry's
// path, inside the directory of the mod that owns the entry.
//
// It sits in romIdx rather than in a field of its own because it is the same
// question - where do this file's bytes come from - and because it must cost
// nothing. g_RomSources[] is global across every mounted mod and ROMSOURCES_MAX
// is 8; six mods are mounted today, and a per-mod source that spent a slot
// would run the table out on the ninth mod, silently (romdataParseFileTable
// warns and drops, and the entry then resolves by the walk as if it had never
// declared anything). A self source spends no slot and needs no offset or
// size: the engine already knows the owning mod's directory, and the path says
// where in it.
#define ROMSOURCE_SELF 0xfe

struct romaltsource {
	u8  romIdx;       // 0xff = none, 0xfe = ROMSOURCE_SELF, else g_RomSources[]
	u8  compression;  // 0=raw, 1=rzip(1173)
	u32 offset;
	u32 size;
};

// MOD_TEX_MAP_MAX_MODS bounds both the per-mod texture map and the
// per-mod altSource array.
//
// modIdx is 0-based and is the same number as g_ModNum and the modDirs[]
// subscript: romdataLoadModFileTable() is called with i over
// [0, g_NumModDirs) and hands that straight to romdataParseFileTable() as
// ownerModIdx. A three-mod boot logs "PDFT v3 romTexMap: 20 entries (mod=0)"
// against modDirs[0]='.../mod_fojo', then mod=1 and mod=2 for the other two.
//
// This comment used to say row 0 was a reserved base table and mods ran
// 1..64. Nothing indexes these arrays that way, and mod 0 is a real mod -
// the boot mod - not an absence; see the MOD_FILEID note in
// port/include/mod.h. The 65th row is the leftover of that abandoned
// layout. Row 0 is doubly booked instead: it is mod 0's row and also the
// row the global filetable writes and romdataFileLoad() falls back to.
#define MOD_TEX_MAP_MAX_MODS 65

static struct romsource g_RomSources[ROMSOURCES_MAX];
static u32 g_NumRomSources;

// The ROM's own name table and its length, or NULL/0 when booted from a
// filenames.lst instead of a ROM. Slot i < g_RomNameCount is a vanilla file
// whatever a mod later overlays on that id in its own row.
static const u32 *g_RomNameTable = NULL;
static s32 g_RomNameCount = 0;
// The ROM's file offset table, for the same reason: the ROM's own bytes for
// a slot, whatever the active mod's row now points at.
static const u32 *g_RomFileOffsets = NULL;
static s32 g_RomFileOffsetCount = 0;
// Per-mod altSource: [modIdx][localFileId], modIdx 0-based as above. Row 0
// carries both mod 0's fragment entries and the global table's, so the two
// overwrite each other for any file id they share.
static struct romaltsource g_FileAltSource[MOD_TEX_MAP_MAX_MODS][ROMDATA_MAX_FILES];

struct modTexMapEntry {
	u16 localTexId;
	u16 portTexId;
};

struct modTexMap {
	u32 count;
	struct modTexMapEntry *entries;  // sorted ascending by localTexId
};

// Per-mod texmap allocations use mod-LOCAL slot indices in the fragment.
// The engine adds this running base to each stored portTexId at parse time
// so port IDs are globally unique without any cross-mod build-time coordination.
//
// The base is 4096 because that is one past the largest id a 12-bit texture
// slot could encode, not because it is a round number. Every id a mod authored
// under the old format is therefore below it and every id the loader assigns is
// at or above it, which makes "authored" and "assigned" tell themselves apart by
// value alone. That distinction is load-bearing: modTextureResolve() in mod.c
// treats any id >= NUM_TEXTURES as possibly-a-port and runs it through
// modTexMapReverseLookup, so while a mod's own local ids could share the
// 3503..4095 range with some mod's port window, a local id could be
// reinterpreted as a port and resolve to the wrong texture. That was not
// hypothetical - mod_aio_characters names local ids up to 0x0ffd, and all 90 of
// its texmap entries sit in that window. It was kept apart only by where each
// mod's window happened to land, and widening the slots moves the windows.
//
// It also still clears vanilla: JPN has 3511 textures, the most of any PD ROM
// variant, so anything below 3512 would collide with vanilla texids appearing
// in JPN-sourced models.
#define MOD_TEX_PORT_BASE 4096u
// The ceiling is not a policy this file chooses, it is whatever a texture slot
// inside a G_NOOP display-list command can name. That used to be 12 bits, and
// 4095 minus the base left 496 slots for every mod on disk put together, which
// the shipped set already overran. A slot is now 15 bits - see gbiex.h for
// where the extra three come from - so take the number from there rather than
// restating it and letting the two drift.
#define MOD_TEX_PORT_MAX  G_NOOP_TEXSLOT_MAX
static u32 g_NextGlobalTexPort = MOD_TEX_PORT_BASE;

static struct modTexMap g_ModTexMap[MOD_TEX_MAP_MAX_MODS];

// Look up portTexId for a given (modIdx, localTexId). Returns
// localTexId unchanged if the mod has no map or the id is absent.
u16 modTexMapLookup(s32 modIdx, u16 localTexId)
{
	if (modIdx < 0 || modIdx >= MOD_TEX_MAP_MAX_MODS) return localTexId;
	const struct modTexMap *m = &g_ModTexMap[modIdx];
	if (!m->count || !m->entries) return localTexId;
	// binary search
	s32 lo = 0;
	s32 hi = (s32)m->count - 1;
	while (lo <= hi) {
		s32 mid = (lo + hi) >> 1;
		u16 k = m->entries[mid].localTexId;
		if (k == localTexId) return m->entries[mid].portTexId;
		if (k < localTexId) lo = mid + 1;
		else hi = mid - 1;
	}
	return localTexId;
}

// Reverse lookup: portTexId -> localTexId. Linear scan since portTex space
// is not sorted. Returns 0xffff if not found.
u16 modTexMapReverseLookup(s32 modIdx, u16 portTexId)
{
	if (modIdx < 0 || modIdx >= MOD_TEX_MAP_MAX_MODS) return 0xffff;
	const struct modTexMap *m = &g_ModTexMap[modIdx];
	if (!m->count || !m->entries) return 0xffff;
	for (u32 i = 0; i < m->count; i++) {
		if (m->entries[i].portTexId == portTexId) return m->entries[i].localTexId;
	}
	return 0xffff;
}

s32 modTexMapGetCount(s32 modIdx)
{
	if (modIdx < 0 || modIdx >= MOD_TEX_MAP_MAX_MODS) return 0;
	return (s32)g_ModTexMap[modIdx].count;
}

// Entry i of a mod's texmap as (localTexId, portTexId). 0 when out of range.
s32 modTexMapGetEntry(s32 modIdx, s32 index, u16 *localTexId, u16 *portTexId)
{
	if (modIdx < 0 || modIdx >= MOD_TEX_MAP_MAX_MODS) return 0;
	const struct modTexMap *m = &g_ModTexMap[modIdx];
	if (!m->entries || index < 0 || index >= (s32)m->count) return 0;
	if (localTexId) *localTexId = m->entries[index].localTexId;
	if (portTexId) *portTexId = m->entries[index].portTexId;
	return 1;
}

// The name a PDFT entry DECLARED for (mod, id), off the name pool, or NULL
// when the slot got its name from the ROM table or has none. The slot's own
// .name field is the PATH when an entry carries both, and a path can be the
// resolver's little program ("context:SOLO::files/X|..."), which is not a
// name anyone should have to read back. The alias is inserted after the
// name, so the first pool hit is the name.
const char *romdataFileDeclaredName(s32 modNum, s32 fileNum)
{
	for (s32 i = 0; i < ftPoolUsed; ++i) {
		if (ftPool[i].fileId == fileNum && ftPool[i].ownerMod == modNum) {
			return ftPool[i].name;
		}
	}
	return NULL;
}

s32 romdataRomFileCount(void)
{
	return g_RomNameCount;
}

// The ROM's name for a slot, or NULL if the slot is not the ROM's. Reads the
// name table, not fileSlots, so a mod overlaying that id in its row does not
// change the answer.
const char *romdataRomFileName(s32 fileNum)
{
	if (!g_RomNameTable || fileNum < 1 || fileNum >= g_RomNameCount) return NULL;
	return (const char *)g_RomNameTable + PD_BE32(g_RomNameTable[fileNum]);
}

// The ROM's own bytes for a slot, or NULL. Reads the offset table, not
// fileSlots, so it is the base game's answer whatever a mod overlays.
u8 *romdataRomFileData(s32 fileNum, u32 *outSize)
{
	if (!g_RomFile || !g_RomFileOffsets || fileNum < 1 || fileNum + 1 >= g_RomFileOffsetCount) {
		return NULL;
	}

	const u32 ofs = PD_BE32(g_RomFileOffsets[fileNum]);
	const u32 next = PD_BE32(g_RomFileOffsets[fileNum + 1]);

	if (!ofs || next < ofs || next > g_RomFileSize) {
		return NULL;
	}

	if (outSize) *outSize = next - ofs;
	return g_RomFile + ofs;
}

s32 romsourceCount(void)
{
	return (s32)g_NumRomSources;
}

s32 romsourceInfo(s32 index, const char **id, const char **filename, u8 *mounted)
{
	if (index < 0 || index >= (s32)g_NumRomSources) return 0;
	if (id) *id = g_RomSources[index].id;
	if (filename) *filename = g_RomSources[index].filename;
	if (mounted) *mounted = g_RomSources[index].mounted;
	return 1;
}

static void romSourcesInit(void)
{
	g_NumRomSources = 0;
	for (u32 m = 0; m < MOD_TEX_MAP_MAX_MODS; m++) {
		for (u32 i = 0; i < ROMDATA_MAX_FILES; i++) {
			g_FileAltSource[m][i].romIdx = 0xff;
		}
	}
}

static s32 romSourceFind(const char *id)
{
	for (u32 i = 0; i < g_NumRomSources; i++) {
		if (!strcmp(g_RomSources[i].id, id)) {
			return (s32)i;
		}
	}
	return -1;
}
// romdata.c
u8 romsourceIsMounted(const char *id) {
    s32 i = romSourceFind(id);
    if (i < 0) return 0;
    return g_RomSources[i].mounted;
}

// A patched source: find its base, read the patch out of the owning mod's
// directory, and decode it as an overlay over the base's mapping. The base
// must already be mounted as an image (g_RomFile, or a plain source) - an
// overlay over an overlay is refused, one layer is the design.
static void romSourceMountPatched(struct romsource *rs)
{
	const u8 *baseData = NULL;
	u32 baseSize = 0;
	char tmp[FS_MAXPATH];
	u8 *patch = NULL;
	u32 patchLen = 0;
	char err[256] = { 0 };

	if (!strcmp(rs->base, PDFT_ROMSOURCE_BASE)) {
		// NOT g_RomFile: by the time a fragment is parsed the engine has
		// rewritten segments of it in place (preprocessTexturesList and the
		// segment preprocessors), so its bytes are no longer the file's and
		// the patch's window checksums would refuse it. A second private
		// mapping of the same file is pristine, and its untouched pages are
		// the same page-cache pages as the first mapping's.
		static u8 *pristine;
		static u32 pristineSize;
		if (!pristine) {
			pristine = fsFileMap(romName, &pristineSize);
		}
		baseData = pristine;
		baseSize = pristineSize;
	} else {
		const s32 b = romSourceFind(rs->base);
		if (b < 0) {
			sysLogPrintf(LOG_WARNING, "romSource '%s': base '%s' is not a declared source", rs->id, rs->base);
			return;
		}
		if (!g_RomSources[b].mounted) {
			return; // not yet; a later fragment may mount it, and we are called again
		}
		if (!g_RomSources[b].data) {
			sysLogPrintf(LOG_WARNING, "romSource '%s': base '%s' is itself patched; one layer only", rs->id, rs->base);
			return;
		}
		baseData = g_RomSources[b].data;
		baseSize = g_RomSources[b].size;
	}

	if (!baseData) {
		sysLogPrintf(LOG_WARNING, "romSource '%s': base '%s' is not available", rs->id, rs->base);
		return;
	}

	if (rs->ownerMod < 0 || rs->ownerMod >= (s32)g_NumModDirs || !modDirs[rs->ownerMod][0]) {
		sysLogPrintf(LOG_WARNING, "romSource '%s': no owning mod directory for patch '%s'", rs->id, rs->patch);
		return;
	}

	snprintf(tmp, sizeof(tmp), "%s/%s", modDirs[rs->ownerMod], rs->patch);
	if (fsFileSize(tmp) <= 0) {
		snprintf(tmp, sizeof(tmp), "%s/" ROMDATA_FILEDIR "/%s", modDirs[rs->ownerMod], rs->patch);
	}
	patch = fsFileLoad(tmp, &patchLen);
	if (!patch) {
		sysLogPrintf(LOG_WARNING, "romSource '%s': patch '%s' not found in %s", rs->id, rs->patch, modDirs[rs->ownerMod]);
		if (rs->flags & PDFT_RS_REQUIRED) {
			sysFatalError("Required ROM source '%s' is missing its patch (%s)", rs->id, rs->patch);
		}
		return;
	}

	if (rompatchOverlay(baseData, baseSize, patch, patchLen, &rs->ov, err, sizeof(err)) < 0) {
		sysLogPrintf(LOG_WARNING, "romSource '%s': patch '%s' does not apply to '%s': %s", rs->id, rs->patch, rs->base, err);
		sysMemFree(patch);
		if (rs->flags & PDFT_RS_REQUIRED) {
			sysFatalError("Required ROM source '%s': %s", rs->id, err);
		}
		return;
	}
	sysMemFree(patch);

	rs->data = NULL;
	rs->size = rs->ov->size;

	// The decode verified every window, which walked the whole base once.
	// Hand those pages back; files read through the overlay fault in only
	// the ranges they cover. Only for the pristine map, which nothing writes
	// - a plain source's mapping may carry COW pages and is left alone.
	if (!strcmp(rs->base, PDFT_ROMSOURCE_BASE)) {
		fsFileMapRelease((void *)baseData, baseSize);
	}

	if (rs->expectedSize && rs->size != rs->expectedSize) {
		sysLogPrintf(LOG_WARNING, "romSource '%s': patched size %u != expected %u", rs->id, rs->size, rs->expectedSize);
		if (rs->flags & PDFT_RS_STRICT) {
			sysFatalError("Strict ROM source '%s' size mismatch (%u != %u)", rs->id, rs->size, rs->expectedSize);
		}
	}

	rs->mounted = 1;
	sysLogPrintf(LOG_NOTE, "romSource '%s' overlaid on '%s' with '%s' (%u bytes; %u segments, %u literal, %u held; crc32 %08x expected)",
	             rs->id, rs->base, rs->patch, rs->size, rs->ov->numsegs, rs->ov->litlen,
	             rompatchOverlayCost(rs->ov), rs->expectedCrc32);
}

static void romSourcesMount(void)
{
	// plain images first, then overlays, so a patch whose base is declared
	// later in the same table still finds it mounted
	for (u32 i = 0; i < g_NumRomSources; i++) {
		struct romsource *rs = &g_RomSources[i];
		if (rs->mounted || !(rs->flags & PDFT_RS_PATCHED)) continue;
		romSourceMountPatched(rs);
	}

	for (u32 i = 0; i < g_NumRomSources; i++) {
		struct romsource *rs = &g_RomSources[i];
		if (rs->mounted || (rs->flags & PDFT_RS_PATCHED) || !rs->filename[0]) continue;

		// Probe with fsFileSize so a miss does not log an error per
		// candidate; the image itself is mapped, not read (see fsFileMap).
		const char *cands[3];
		char tmp1[FS_MAXPATH], tmp2[FS_MAXPATH];
		snprintf(tmp1, sizeof(tmp1), "$B/roms/%s", rs->filename);
		snprintf(tmp2, sizeof(tmp2), "$B/%s", rs->filename);
		cands[0] = rs->filename; cands[1] = tmp1; cands[2] = tmp2;
		for (u32 c = 0; c < 3 && !rs->data; c++) {
			if (fsFileSize(cands[c]) > 0) {
				rs->data = fsFileMap(cands[c], &rs->size);
			}
		}

		if (!rs->data) {
			sysLogPrintf(LOG_WARNING, "romSource '%s': file '%s' not found", rs->id, rs->filename);
			if (rs->flags & 1) {
				sysFatalError("Required ROM source '%s' (%s) is missing", rs->id, rs->filename);
			}
			continue;
		}

		if (rs->expectedSize && rs->size != rs->expectedSize) {
			sysLogPrintf(LOG_WARNING, "romSource '%s': size %u != expected %u",
			             rs->id, rs->size, rs->expectedSize);
			if (rs->flags & 2) {
				sysFatalError("Strict ROM source '%s' size mismatch (%u != %u)",
				              rs->id, rs->size, rs->expectedSize);
			}
		}

		rs->mounted = 1;
		sysLogPrintf(LOG_NOTE, "romSource '%s' mounted from '%s' (%u bytes)",
		             rs->id, rs->filename, rs->size);
	}
}

/* patches for individual files; applied on file load, before preprocFuncs, but */
/* after unzip; only applied when loading from a ROM file                       */
static const struct romfilepatch filePatches[] = {
	/* FILE_USETUPLUE: fixes Jon's double "if what" in Infiltration outro */
	{ 0x92a2, 1, "\x6c", "\x99" },
	{ 0x92b0, 1, "\x6c", "\x99" },
};

// fileSlots is [modIdx][localFileId], and a raw file id is mod-LOCAL: every
// mod's inserted files start at the same number (tools/mkfiletable
// FIRST_LOCAL_ID == 2018), so id 2019 is mod_fojo/CheadCatherineZ,
// mod_aio_characters/CbondranchZ and mod_gex_characters/Cbaronsamedi2Z at once
// on the shipped set. (modIdx, rawId) is the identity; a bare raw id is not.
//
// MOD_TEX_MAP_MAX_MODS rows rather than 64 so this array is the same shape as
// g_FileAltSource[] and g_ModTexMap[], which are declared with that constant
// and subscripted by the same mod number. While they disagreed,
// romdataFileGetSlotName(64, ...) and romdataGetFileSlotInfo(64, ...) passed
// their `< MOD_TEX_MAP_MAX_MODS` guard and read row 64 off the end of this one.
//
// The 65th row is a deliberate spare, not slack for an off-by-one. No mod
// index can reach it: getModDirCount() caps --moddir at ARRAYCOUNT(modDirs)
// == 64 (port/src/fs.c) and every loop that fills this array is exclusive over
// g_NumModDirs. It is the row reserved for the vanilla owner that the
// MOD_FILEID tag now distinguishes from mod 0 - MOD_FILEID_MOD() already
// answers -1 for vanilla and 0 for mod 0, but row 0 here is still doubly
// booked as mod 0's row and the global table's fallback. Nothing writes or
// reads row 64 today; growing the array is what makes the guards honest, not
// what makes the loops correct.
static struct romfile fileSlots[MOD_TEX_MAP_MAX_MODS][ROMDATA_MAX_FILES];
void fileSlotsInit(u32 numMods) {
	// `i < numMods - 1` skipped the last mod row, and with the shipped default
	// of one mod dir the bound was 0, so no row was given the patch list at
	// all. numMods is unsigned, so the old form also wrapped to ~4 billion
	// iterations over a 64-row array if it were ever called with zero.
	for (s32 i = 0; i < (s32)numMods; ++i) {
		for (s32 j = 0; j < ROMDATA_MAX_FILES; ++j) {
				fileSlots[i][j].patches = filePatches;
				fileSlots[i][j].numpatches = 2;
		}
	}
}

#define ROMSEG_START(n) _ ## n ## SegmentRomStart
#define ROMSEG_END(n) _ ## n ## SegmentRomEnd

/* segment table for ntsc-final                                                     */
/* size will get calculated automatically if it is 0                                */
/* if there are replacement files in the data dir, they will be loaded instead      */
/* offsets are specified for ntsc-final, pal-final and jpn-final in that order      */
#define ROMSEG_LIST() \
	ROMSEG_DECL_SEG(fontjpnsingle,      0x194b20,  0x180330,  0x0,       0x0,      preprocessJpnFont       ) \
	ROMSEG_DECL_SEG(fontjpnmulti,       0x19fb40,  0x18b340,  0x0,       0x0,      preprocessJpnFont       ) \
	ROMSEG_DECL_SEG(animations,         0x1a15c0,  0x18cdc0,  0x190c50,  0x0,      preprocessAnimations    ) \
	ROMSEG_DECL_SEG(mpconfigs,          0x7d0a40,  0x7bc240,  0x7c00d0,  0x11e0,   preprocessMpConfigs     ) \
	ROMSEG_DECL_SEG(mpstringsE,         0x7d1c20,  0x7bd420,  0x7c12b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsJ,         0x7d5320,  0x7c0b20,  0x7c49b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsP,         0x7d8a20,  0x7c4220,  0x7c80b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsG,         0x7dc120,  0x7c7920,  0x7cb7b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsF,         0x7df820,  0x7cb020,  0x7ceeb0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsS,         0x7e2f20,  0x7ce720,  0x7d25b0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(mpstringsI,         0x7e6620,  0x7d1e20,  0x7d5cb0,  0x3700,   NULL                    ) \
	ROMSEG_DECL_SEG(firingrange,        0x7e9d20,  0x7d5520,  0x7d93b0,  0x1550,   NULL                    ) \
	ROMSEG_DECL_SEG(fonttahoma,         0x7f7860,  0x7e3060,  0x7e6ef0,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fontnumeric,        0x7f8b20,  0x7e4320,  0x7e81b0,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fonthandelgothicsm, 0x7f9d30,  0x7e5530,  0x7e93c0,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fonthandelgothicxs, 0x7fbfb0,  0x7e87b0,  0x7ec640,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fonthandelgothicmd, 0x7fdd80,  0x7eae20,  0x7eecb0,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(fonthandelgothiclg, 0x8008e0,  0x7eee70,  0x7f2d00,  0x0,      preprocessFont          ) \
	ROMSEG_DECL_SEG(sfxctl,             0x80a250,  0x7f87e0,  0x7fc670,  0x2fb80,  preprocessALBankFile    ) \
	ROMSEG_DECL_SEG(sfxtbl,             0x839dd0,  0x828360,  0x82c1f0,  0x4c2160, NULL                    ) \
	ROMSEG_DECL_SEG(seqctl,             0xcfbf30,  0xcea4c0,  0xcee350,  0xa060,   preprocessALBankFile    ) \
	ROMSEG_DECL_SEG(seqtbl,             0xd05f90,  0xcf4520,  0xcf83b0,  0x17c070, NULL                    ) \
	ROMSEG_DECL_SEG(sequences,          0xe82000,  0xe70590,  0xe74420,  0x563a0,  preprocessSequences     ) \
	ROMSEG_DECL_SEG(texturesdata,       0x1d65f40, 0x1d5ca20, 0x1d61f90, 0x0,      NULL                    ) \
	ROMSEG_DECL_SEG(textureslist,       0x1ff7ca0, 0x1fee780, 0x1ff68f0, 0x0,      preprocessTexturesList  ) \
	ROMSEG_DECL_SEG(copyright,          0x1ffea20, 0x1ff5500, 0x1ffd6b0, 0xb30,    NULL                    ) \
	ROMSEG_DECL_SEG(fontjpn,            0x0,       0x0,       0x178c40,  0x17920,  preprocessJpnFont       )

// declare the vars first

#undef ROMSEG_DECL_SEG
#define ROMSEG_DECL_SEG(name, ofs_ntsc, ofs_pal, ofs_jpn, size, preproc) u8 *ROMSEG_START(name), *ROMSEG_END(name);
ROMSEG_LIST()

// this is part of the animations seg and as such does not follow the naming convention
// these are set in preprocessAnimations
u8 *_animationsTableRomStart;
u8 *_animationsTableRomEnd;

// then build the table

#undef ROMSEG_DECL_SEG

#if VERSION == VERSION_NTSC_FINAL
#define ROMSEG_DECL_SEG(name, ofs_ntsc, ofs_pal, ofs_jpn, size, preproc) { &ROMSEG_START(name), &ROMSEG_END(name), #name, (u8 *)ofs_ntsc, size, preproc },
#elif VERSION == VERSION_PAL_FINAL
#define ROMSEG_DECL_SEG(name, ofs_ntsc, ofs_pal, ofs_jpn, size, preproc) { &ROMSEG_START(name), &ROMSEG_END(name), #name, (u8 *)ofs_pal, size, preproc },
#elif VERSION == VERSION_JPN_FINAL
#define ROMSEG_DECL_SEG(name, ofs_ntsc, ofs_pal, ofs_jpn, size, preproc) { &ROMSEG_START(name), &ROMSEG_END(name), #name, (u8 *)ofs_jpn, size, preproc },
#endif

static struct romfile romSegs[] = {
	ROMSEG_LIST()
	{ NULL, NULL, NULL, NULL, 0, NULL },
};

/* the game sets g_LoadType to the type of file it expects,              */
/* so we can hijack that in fileLoad and automatically byteswap the file */
static preprocessfunc filePreprocFuncs[] = {
	/* LOADTYPE_NONE  */ NULL,
	/* LOADTYPE_BG    */ NULL, // loaded in parts
	/* LOADTYPE_TILES */ preprocessTilesFile,
	/* LOADTYPE_LANG  */ preprocessLangFile,
	/* LOADTYPE_SETUP */ preprocessSetupFile,
	/* LOADTYPE_PADS  */ preprocessPadsFile,
	/* LOADTYPE_MODEL */ preprocessModelFile,
	/* LOADTYPE_GUN   */ preprocessGunFile,
};

static inline void romdataWrongRomError(const char *fmt, ...)
{
	char reason[1024];
	reason[0] = '\0';

	va_list args;
	va_start(args, fmt);
	vsnprintf(reason, sizeof(reason), fmt, args);
	va_end(args);

	sysFatalError("Wrong ROM file.\n%s\nEnsure that you have the correct " ROMDATA_ROM_DESC " ROM in z64 format.", reason);
}

static inline void romdataLoadRom(void)
{
	sysLogPrintf(LOG_NOTE, "ROM file: %s", romName);

	g_RomFile = fsFileMap(romName, &g_RomFileSize);

	if (!g_RomFile) {
		sysFatalError("Could not open ROM file %s.\nEnsure that it is in the %s directory.", romName, fsFullPath(""));
	}

	// zips are not guaranteed to start with PK, but might as well at least try
	if (g_RomFileSize > 2 && (!memcmp(g_RomFile, "PK", 2) || !memcmp(g_RomFile, "Rar", 3) || !memcmp(g_RomFile, "7z", 2))) {
		romdataWrongRomError("Your ROM is in an archive file. Please extract it.");
	}

	if (g_RomFileSize != ROMDATA_ROM_SIZE) {
		romdataWrongRomError("ROM size does not match: expected: %u, got: %u.", ROMDATA_ROM_SIZE, g_RomFileSize);
	}

	if (memcmp(g_RomFile + 0x3b, ROMDATA_ROM_ID, 4) || memcmp(g_RomFile + 0x20, ROMDATA_ROM_TITLE, sizeof(ROMDATA_ROM_TITLE) - 1)) {
		romdataWrongRomError("ROM header does not match.");
	}

	// inflate the compressed data segment since that's where some useful stuff is

	u8 *zipped = g_RomFile + ROMDATA_DATA_OFS;
	if (!rzipIs1173(zipped)) {
		romdataWrongRomError("Data segment is not 1173-compressed.");
	}

	const u32 dataSegLen = ((u32)zipped[2] << 16) | ((u32)zipped[3] << 8) | (u32)zipped[4];
	if (dataSegLen < ROMDATA_FILES_OFS) {
		romdataWrongRomError("Data segment too small (%u), need at least %u.", dataSegLen, ROMDATA_FILES_OFS);
	}

	u8 *dataSeg = sysMemAlloc(dataSegLen);
	if (!dataSeg) {
		sysFatalError("Could not allocate %u bytes for data segment.", dataSegLen);
	}

	u8 scratch[5 * 1024];
	if (rzipInflate(zipped, dataSeg, scratch) < 0) {
		free(dataSeg);
		sysFatalError("Could not inflate data segment.");
	}

	romDataSeg = dataSeg;
	romDataSegSize = dataSegLen;
}

static inline void romdataUpdateSegStartEnd(struct romfile* seg)
{
	if (seg->segstart) {
		*seg->segstart = seg->data;
	}

	if (seg->segend) {
		*seg->segend = seg->data + seg->size;
	}
}

static inline void romdataInitSegment(struct romfile *seg)
{
	if (!seg->data) {
		// unused in this ROM, skip it
		sysLogPrintf(LOG_NOTE, "skipping segment %s", seg->name);
		return;
	}

	if (!seg->size) {
		// size unknown
		if (seg[1].name) {
			// use next segment's base to calculate
			seg->size = seg[1].data - seg->data;
		} else {
			// this is the last segment, calculate based on rom size
			seg->size = (uintptr_t)g_RomFileSize - (uintptr_t)seg->data;
		}
	}

	// check if we have an external replacement and load it if so
	char tmp[FS_MAXPATH];
	snprintf(tmp, sizeof(tmp), ROMDATA_SEGDIR "/%s", seg->name);
	u8 *newData = NULL;
	const s32 extFileSize = fsFileSize(tmp);
	if (extFileSize > 0) {
		newData = fsFileLoad(tmp, &seg->size);
	}

	if (!newData) {
		// no external data, just make it point to the rom
		if (g_RomFile) {
			newData = g_RomFile + (uintptr_t)seg->data;
			seg->source = SRC_ROM;
			sysLogPrintf(LOG_NOTE, "loading segment %s from ROM (offset %08x pointer %p)", seg->name, (uintptr_t)seg->data, newData);
		} else {
			sysFatalError("No ROM or external file for segment:\n%s", seg->name);
		}
	} else {
		// loaded external data
		seg->source = SRC_EXTERNAL;
		sysLogPrintf(LOG_NOTE, "loading segment %s from file (pointer %p)", seg->name, newData);
	}

	seg->data = newData;

	romdataUpdateSegStartEnd(seg);

	// call the post load function if any
	if (seg->preprocess && !seg->preprocessed) {
		newData = seg->preprocess(seg->data, seg->size, &seg->size, g_ModNum);

		if (newData) {
			if (seg->source == SRC_EXTERNAL)
				sysMemFree(seg->data);
			seg->data = newData;
			romdataUpdateSegStartEnd(seg);
		}

		seg->preprocessed = 1;
	}
}

static inline s32 romdataLoadExternalFileList(void)
{
	romDataSeg = fsFileLoad("filenames.lst", &romDataSegSize); // this null terminates the file by itself
	if (!romDataSeg || !romDataSegSize) {
		return 0;
	}

	s32 n = 1;
	char *p = (char *)romDataSeg;
	while (*p && n < ROMDATA_MAX_FILES) {
		// skip whitespace
		while (*p && isspace(*p)) ++p;
		if (*p) {
			const char *start = p;
			// skip to next whitespace or end of file
			while (*p && !isspace(*p)) ++p;
			// null terminate the name if needed
			if (*p) {
				*p++ = '\0';
			}
			fileSlots[g_ModNum][n++].name = start;
		}
	}

	return n - 1;
}

static u8 *externalFileTableData = NULL;
static u32 externalFileTableSize = 0;

// Storage for per-mod fragment buffers so engine pointers (names,
// paths) into them stay valid for the program's lifetime.
static u8 *g_ModFileTableData[MOD_TEX_MAP_MAX_MODS];

static s32 romdataParseFileTable(u8 *data, u32 size, s32 ownerModIdx)
{
	if (!data || size < 12) return 0;

	const bool isGlobal = (ownerModIdx < 0);
	if (!isGlobal && (ownerModIdx < 0 || ownerModIdx >= MOD_TEX_MAP_MAX_MODS)) {
		sysLogPrintf(LOG_ERROR, "romdataParseFileTable: invalid ownerModIdx=%d", ownerModIdx);
		return 0;
	}

	u8 *dataEnd = data + size;
	if (memcmp(data, "PDFT", 4) != 0) {
		sysLogPrintf(LOG_ERROR, "Invalid file table magic (mod=%d)", ownerModIdx);
		return 0;
	}

	u32 version = PD_BE32(*(u32*)(data + 4));
	u32 numFiles = PD_BE32(*(u32*)(data + 8));
	u8 *p = data + 12;

	u32 numRomSources = 0;
	if (version >= 2) {
		if (p + 4 > dataEnd) {
			sysLogPrintf(LOG_ERROR, "PDFT v2 header truncated (mod=%d)", ownerModIdx);
			return 0;
		}
		numRomSources = PD_BE32(*(u32*)p); p += 4;
	}

	sysLogPrintf(LOG_NOTE, "Loading file table v%u: %u files, %u romSources (mod=%d, %s)",
	             version, numFiles, numRomSources, ownerModIdx,
	             isGlobal ? "global" : "per-mod");
	PDFT("parse table=%s mod=%d bytes=%u version=%u entries=%u romSources=%u",
	     isGlobal ? "global" : "fragment", ownerModIdx, size, version,
	     numFiles, numRomSources);

	if (version >= 2) {
		u8 fragRomIdxMap[256];
		memset(fragRomIdxMap, 0xff, sizeof(fragRomIdxMap));
		(void)isGlobal;

		for (u32 i = 0; i < numRomSources; ++i) {
			if (p + 1 > dataEnd) break;
			u8 idLen = *p++;
			if (p + idLen > dataEnd) break;
			char *rsId = (char*)p;
			p += idLen;
			if (p + 1 > dataEnd) break;
			u8 fnLen = *p++;
			if (p + fnLen > dataEnd) break;
			char *rsFn = (char*)p;
			p += fnLen;
			if (p + 4 + 1 + 1 + 2 > dataEnd) break;
			u32 expectedSize = PD_BE32(*(u32*)p); p += 4;
			u8 rsFlags = *p++;
			u8 rsFallback = *p++;
			p += 2; // reserved

			// PDFT_RS_PATCHED moves bytes, so it needs v5; below that the
			// writer never emits it, and a table claiming otherwise is
			// refused here rather than misread.
			char *rsBase = NULL, *rsPatch = NULL;
			u32 rsCrc = 0;
			if (rsFlags & PDFT_RS_PATCHED) {
				if (version < 5) {
					sysLogPrintf(LOG_ERROR, "PDFT v%u table declares a patched romSource; that needs v5", version);
					break;
				}
				if (p + 1 > dataEnd) break;
				u8 bLen = *p++;
				if (p + bLen > dataEnd) break;
				rsBase = (char*)p;
				p += bLen;
				if (p + 1 > dataEnd) break;
				u8 pLen = *p++;
				if (p + pLen > dataEnd) break;
				rsPatch = (char*)p;
				p += pLen;
				if (p + 4 > dataEnd) break;
				rsCrc = PD_BE32(*(u32*)p); p += 4;
			}

			s32 existing = -1;
			for (u32 k = 0; k < g_NumRomSources; ++k) {
				if (!strcmp(g_RomSources[k].id, rsId)) {
					existing = (s32)k;
					break;
				}
			}
			s32 globalIdx;
			if (existing >= 0) {
				globalIdx = existing;
			} else if (g_NumRomSources < ROMSOURCES_MAX) {
				globalIdx = (s32)g_NumRomSources;
				struct romsource *rs = &g_RomSources[globalIdx];
				memset(rs, 0, sizeof(*rs));
				strncpy(rs->id, rsId, sizeof(rs->id) - 1);
				strncpy(rs->filename, rsFn, sizeof(rs->filename) - 1);
				rs->expectedSize = expectedSize;
				rs->flags = rsFlags;
				rs->fallback = rsFallback;
				rs->ownerMod = ownerModIdx;
				if (rsFlags & PDFT_RS_PATCHED) {
					strncpy(rs->base, rsBase ? rsBase : "", sizeof(rs->base) - 1);
					strncpy(rs->patch, rsPatch ? rsPatch : "", sizeof(rs->patch) - 1);
					rs->expectedCrc32 = rsCrc;
				}
				g_NumRomSources++;
			} else {
				sysLogPrintf(LOG_WARNING, "Too many romSources (max %d), dropping '%s'",
				             ROMSOURCES_MAX, rsId);
				globalIdx = -1;
			}
			if (i < 256 && globalIdx >= 0) {
				fragRomIdxMap[i] = (u8)globalIdx;
			}
			PDFT("romsource table=%s mod=%d index=%u id='%s' file='%s' expected=%u flags=0x%02x fallback=%u mapped=%d",
			     isGlobal ? "global" : "fragment", ownerModIdx, i, rsId, rsFn,
			     expectedSize, rsFlags, rsFallback, globalIdx);
		}

		if (g_NumRomSources > 0) {
			romSourcesMount();
		}

		static u8 *s_fragRomIdxMapPtr;
		s_fragRomIdxMapPtr = fragRomIdxMap;
		(void)s_fragRomIdxMapPtr;

		if (isGlobal) {
			ftReset();
		}

		for (u32 i = 0; i < numFiles; ++i) {
			if (p + 16 > dataEnd) break;

			u32 id = PD_BE32(*(u32*)p); p += 4;
			u32 flags = PD_BE32(*(u32*)p); p += 4;
			u32 offset = PD_BE32(*(u32*)p); p += 4;
			u32 fileSize = PD_BE32(*(u32*)p); p += 4;

			u16 nameLen = PD_BE16(*(u16*)p); p += 2;
			char *name = (char*)p;
			p += nameLen;

			// The load-time half of the name rules in pdftrules.h. Only the
			// two that the WIRE can support: nameLen counts the terminator, so
			// a name of 127 characters arrives as 128 and is the longest that
			// survives the resolver's candidate[128] intact.
			//
			// The texture-shape rule cannot be checked here and deliberately
			// is not faked. It turns on the manifest's `type: texture`, and the
			// wire format carries no type - an entry is a texture only in the
			// sense that something will one day compose its name from an id.
			// Making that rule transferable needs a flag bit saying so, which
			// by the note in pdftrules.h costs no bytes and no version, and is
			// the obvious next step if this is to move to load time properly.
			//
			// Reported, not refused: by the time this reads the row the table
			// is already mounted and the alternative is dropping a file the
			// player has. A warning naming the cause is worth a great deal
			// anyway, because the symptom is otherwise an unexplained missing
			// file a long way from here.
			if (nameLen <= 1) {
				sysLogPrintf(LOG_WARNING,
					"PDFT: id %u (mod=%d) has an empty name, and an entry is "
					"reachable only by name", id, ownerModIdx);
			} else if (nameLen > PDFT_NAME_MAX) {
				sysLogPrintf(LOG_WARNING,
					"PDFT: id %u (mod=%d) has a %u-byte name, over the %d-byte "
					"buffer modTextureResolveFileDetailed() composes into, so "
					"any lookup of it will be truncated and miss: %.*s",
					id, ownerModIdx, nameLen, PDFT_NAME_MAX, (int)nameLen - 1, name);
			}

			u16 pathLen = PD_BE16(*(u16*)p); p += 2;
			char *path = (char*)p;
			p += pathLen;

			u8  altRomIdx = 0xff;
			u32 altOffset = 0;
			u32 altSize = 0;
			u8  altCompression = 0;
			if (flags & PDFT_F_ALT) {
				if (p + 1 + 4 + 4 + 1 > dataEnd) {
					sysLogPrintf(LOG_ERROR, "PDFT v2 source tail truncated for id %u (mod=%d)",
					             id, ownerModIdx);
					return 0;
				}
				u8 fragIdx = *p++;
				altRomIdx = (fragIdx < 256) ? fragRomIdxMap[fragIdx] : 0xff;
				altOffset = PD_BE32(*(u32*)p); p += 4;
				altSize   = PD_BE32(*(u32*)p); p += 4;
				altCompression = *p++;

				if (id < ROMDATA_MAX_FILES && altRomIdx != 0xff) {
					s32 altSlot = isGlobal ? 0 : ownerModIdx;
					struct romaltsource *as = &g_FileAltSource[altSlot][id];
					as->romIdx = altRomIdx;
					as->offset = altOffset;
					as->size = altSize;
					as->compression = altCompression;
				}
			}

			// flag 0x10: a second name for this same id - the vanilla name
			// this file replaces. Unlike every flag above it has a TAIL, so it
			// only appears in a v4 table; an older reader refuses v4 outright
			// rather than walking off the end of this entry.
			u16 aliasLen = 0;
			const char *aliasName = NULL;
			if (flags & PDFT_F_ALIAS) {
				if (p + 2 > dataEnd) {
					sysLogPrintf(LOG_ERROR, "PDFT alias tail truncated for id %u (mod=%d)", id, ownerModIdx);
					return 0;
				}
				aliasLen = PD_BE16(*(u16*)p); p += 2;
				if (p + aliasLen > dataEnd) {
					sysLogPrintf(LOG_ERROR, "PDFT alias string truncated for id %u (mod=%d)", id, ownerModIdx);
					return 0;
				}
				aliasName = (const char *)p;
				p += aliasLen;
			}

			// flag 0x8: the bytes are the loose file at `path`, inside the
			// directory of the mod that owns this entry, and nowhere else.
			// Recorded against the OWNER's row only. Row 0 is doubly booked as
			// mod 0's row and the row an isGlobal table writes, and "the
			// owning mod's directory" means nothing for a table that has no
			// owning mod, so a global table's self flag is dropped rather than
			// given mod 0's directory by accident.
			if ((flags & PDFT_F_SELFSOURCE) && id < ROMDATA_MAX_FILES) {
				if (isGlobal) {
					sysLogPrintf(LOG_WARNING,
						"PDFT: id %u is self-sourced in the global table, which has no "
						"owning mod; ignoring the flag", id);
				} else if (pathLen <= 1) {
					sysLogPrintf(LOG_WARNING,
						"PDFT: id %u (mod=%d) is self-sourced with no path; ignoring the flag",
						id, ownerModIdx);
				} else if (g_FileAltSource[ownerModIdx][id].romIdx != 0xff) {
					// Both a mounted image and this mod's own directory. Which
					// won would be decided by the order the lanes are tested
					// in, which is the luck a declaration exists to remove.
					sysLogPrintf(LOG_WARNING,
						"PDFT: id %u (mod=%d) declares both romSource %u and a self source; "
						"keeping the romSource",
						id, ownerModIdx, g_FileAltSource[ownerModIdx][id].romIdx);
				} else {
					struct romaltsource *as = &g_FileAltSource[ownerModIdx][id];
					as->romIdx = ROMSOURCE_SELF;
					as->compression = 0;
					as->offset = 0;
					as->size = 0;
				}
			}

			PDFT("entry table=%s mod=%d index=%u id=0x%04x flags=0x%08x name='%.*s' path='%.*s' romOffset=0x%x romSize=%u altRom=%d altOffset=0x%x altSize=%u altCompression=%u",
			     isGlobal ? "global" : "fragment", ownerModIdx, i, id, flags,
			     nameLen > 0 ? nameLen - 1 : 0, name,
			     pathLen > 0 ? pathLen - 1 : 0, path,
			     offset, fileSize, altRomIdx, altOffset, altSize, altCompression);

			if (id >= ROMDATA_MAX_FILES) {
				continue;
			}

			bool hasExport = false;
			const char *pathAfterDoubleColon = NULL;
			const char *modConstraint = NULL;

			if (isGlobal && (flags & PDFT_F_PATH) && pathLen > 1) {
				if (strstr(path, "export")) hasExport = true;
				const char *modPrefix = strstr(path, "mod:");
				if (modPrefix) modConstraint = modPrefix + 4;
				const char *separator = strstr(path, "::");
				if (separator) pathAfterDoubleColon = separator + 2;
			}

			s32 modLo = isGlobal ? 0 : ownerModIdx;
			// Exclusive end. The rows a mod index can name are
			// [0, g_NumModDirs), so `mod <= g_NumModDirs` wrote one row past
			// the last mod on every global-table entry - fileSlots[64] with
			// the 64 --moddir entries getModDirCount() still accepts.
			s32 modEnd = isGlobal ? (s32)g_NumModDirs : ownerModIdx + 1;

			for (s32 mod = modLo; mod < modEnd; ++mod) {
				if (flags & PDFT_F_ROMRESIDENT) {
					fileSlots[mod][id].data = g_RomFile + offset;
					fileSlots[mod][id].size = fileSize;
					fileSlots[mod][id].source = SRC_UNLOADED;
				}

				if ((flags & PDFT_F_PATH) && pathLen > 1) {
					if (isGlobal && hasExport && modConstraint && pathAfterDoubleColon) {
						// `mod` is a fileSlots[] row, and a row is read back
						// as fileSlots[g_ModNum] - the same 0-based number the
						// fragment path writes with (romdataLoadModFileTable
						// passes i over [0, g_NumModDirs)). The old
						// modDirs[mod - 1] therefore matched every row against
						// the previous mod's name: row 1 (gex) was tested
						// against mod_fojo, and row 0 was skipped entirely.
						const char *currentModName = NULL;
						if (mod >= 0 && mod < (s32)g_NumModDirs && modDirs[mod][0]) {
							currentModName = strrchr(modDirs[mod], '/');
							if (currentModName) currentModName++;
							else currentModName = modDirs[mod];
						}
						bool isOwner = false;
						if (currentModName) {
							size_t modNameLen = strlen(currentModName);
							if (strncmp(modConstraint, currentModName, modNameLen) == 0) {
								char next = modConstraint[modNameLen];
								if (next == ',' || next == ':') isOwner = true;
							}
						}
						fileSlots[mod][id].name = isOwner ? path : pathAfterDoubleColon;
					} else {
						fileSlots[mod][id].name = path;
					}
				} else if (nameLen > 1) {
					fileSlots[mod][id].name = name;
				}
			}

			if (nameLen > 0) {
				ftInsert(name, nameLen, (u16)id, isGlobal ? -1 : (s8)ownerModIdx);
			}

			// The alias is a SECOND hash entry pointing at the same file id and
			// the same owner, which is why nothing downstream has to learn
			// about aliases: all three lookup passes go through this table, so
			// they all get it for free.
			if (aliasLen > 1 && aliasName) {
				ftInsert(aliasName, aliasLen, (u16)id, isGlobal ? -1 : (s8)ownerModIdx);
			}
		}

		sysLogPrintf(LOG_NOTE, "File table hash: %u entries in %u buckets", ftPoolUsed, FT_HASH_SIZE);
	} else {
		// version < 2: legacy v1 layout
		ftReset();
		for (u32 i = 0; i < numFiles; ++i) {
			if (p + 16 > dataEnd) break;
			u32 id = PD_BE32(*(u32*)p); p += 4;
			u32 flags = PD_BE32(*(u32*)p); p += 4;
			u32 offset = PD_BE32(*(u32*)p); p += 4;
			u32 fileSize = PD_BE32(*(u32*)p); p += 4;
			u16 nameLen = PD_BE16(*(u16*)p); p += 2;
			char *name = (char*)p; p += nameLen;
			u16 pathLen = PD_BE16(*(u16*)p); p += 2;
			char *path = (char*)p; p += pathLen;

			// flag 0x8: the bytes are the loose file at `path`, inside the
			// directory of the mod that owns this entry, and nowhere else.
			// Recorded against the OWNER's row only. Row 0 is doubly booked as
			// mod 0's row and the row an isGlobal table writes, and "the
			// owning mod's directory" means nothing for a table that has no
			// owning mod, so a global table's self flag is dropped rather than
			// given mod 0's directory by accident.
			if ((flags & PDFT_F_SELFSOURCE) && id < ROMDATA_MAX_FILES) {
				if (isGlobal) {
					sysLogPrintf(LOG_WARNING,
						"PDFT: id %u is self-sourced in the global table, which has no "
						"owning mod; ignoring the flag", id);
				} else if (pathLen <= 1) {
					sysLogPrintf(LOG_WARNING,
						"PDFT: id %u (mod=%d) is self-sourced with no path; ignoring the flag",
						id, ownerModIdx);
				} else if (g_FileAltSource[ownerModIdx][id].romIdx != 0xff) {
					// Both a mounted image and this mod's own directory. Which
					// won would be decided by the order the lanes are tested
					// in, which is the luck a declaration exists to remove.
					sysLogPrintf(LOG_WARNING,
						"PDFT: id %u (mod=%d) declares both romSource %u and a self source; "
						"keeping the romSource",
						id, ownerModIdx, g_FileAltSource[ownerModIdx][id].romIdx);
				} else {
					struct romaltsource *as = &g_FileAltSource[ownerModIdx][id];
					as->romIdx = ROMSOURCE_SELF;
					as->compression = 0;
					as->offset = 0;
					as->size = 0;
				}
			}

			PDFT("entry table=%s mod=%d index=%u id=0x%04x flags=0x%08x name='%.*s' path='%.*s' romOffset=0x%x romSize=%u",
			     isGlobal ? "global" : "fragment", ownerModIdx, i, id, flags,
			     nameLen > 0 ? nameLen - 1 : 0, name,
			     pathLen > 0 ? pathLen - 1 : 0, path, offset, fileSize);

			if (id >= ROMDATA_MAX_FILES) continue;

			s32 modLo = isGlobal ? 0 : ownerModIdx;
			// Exclusive end, as in the v2/v3 path above.
			s32 modEnd = isGlobal ? (s32)g_NumModDirs : ownerModIdx + 1;
			for (s32 mod = modLo; mod < modEnd; ++mod) {
				if (flags & PDFT_F_ROMRESIDENT) {
					fileSlots[mod][id].data = g_RomFile + offset;
					fileSlots[mod][id].size = fileSize;
					fileSlots[mod][id].source = SRC_UNLOADED;
				}
				if ((flags & PDFT_F_PATH) && pathLen > 1) {
					fileSlots[mod][id].name = path;
				} else if (nameLen > 1) {
					fileSlots[mod][id].name = name;
				}
			}
			if (nameLen > 0) {
				ftInsert(name, nameLen, (u16)id, isGlobal ? -1 : (s8)ownerModIdx);
			}
		}
	}

	if (version >= 3) {
		if (p + 4 > dataEnd) {
			sysLogPrintf(LOG_ERROR, "PDFT v3 romTexMap header truncated (mod=%d)", ownerModIdx);
			return 1;
		}
		u32 numTexMap = PD_BE32(*(u32*)p); p += 4;
		if ((size_t)(dataEnd - p) < (size_t)numTexMap * 4) {
			sysLogPrintf(LOG_ERROR, "PDFT v3 romTexMap body truncated (count=%u, mod=%d)",
			             numTexMap, ownerModIdx);
			return 1;
		}

		if (isGlobal) {
			p += (size_t)numTexMap * 4;
			sysLogPrintf(LOG_NOTE, "PDFT v3 romTexMap: %u entries (global, ignored)", numTexMap);
			PDFT("texmap table=global mod=%d entries=%u action=ignored", ownerModIdx, numTexMap);
			return 1;
		}

		struct modTexMap *m = &g_ModTexMap[ownerModIdx];
		u32 modBase = g_NextGlobalTexPort;
		if (m->entries) {
			sysMemFree(m->entries);
			m->entries = NULL;
			m->count = 0;
		}
		if (numTexMap > 0) {
			m->entries = sysMemAlloc(sizeof(struct modTexMapEntry) * numTexMap);
			if (!m->entries) {
				sysLogPrintf(LOG_ERROR, "Failed to allocate romTexMap (%u entries, mod=%d)",
				             numTexMap, ownerModIdx);
				return 1;
			}
			u32 maxSlot = 0;
			for (u32 i = 0; i < numTexMap; ++i) {
				u16 localId = PD_BE16(*(u16*)p); p += 2;
				u16 slotIdx = PD_BE16(*(u16*)p); p += 2;
				m->entries[i].localTexId = localId;
				m->entries[i].portTexId  = (u16)(modBase + slotIdx);
				if (slotIdx > maxSlot) {
					maxSlot = slotIdx;
				}
			}

			// The next mod has to start above the highest port this one
			// actually used, which is not the same as the number of entries it
			// carried. A fragment whose slots have holes maps above its own
			// count, so advancing by the count hands the next mod ports this
			// one is already answering to. mkfiletable emits dense slots; a
			// fragment built by anything else may not.
			g_NextGlobalTexPort = modBase + maxSlot + 1;

			if (maxSlot + 1 > numTexMap) {
				sysLogPrintf(LOG_WARNING,
				             "PDFT v3 romTexMap: mod %d has %u entries but uses slots up to %u; "
				             "%u port(s) are reserved and unused",
				             ownerModIdx, numTexMap, maxSlot, maxSlot + 1 - numTexMap);
			}

			if (modBase + maxSlot > MOD_TEX_PORT_MAX) {
				sysLogPrintf(LOG_ERROR,
				             "PDFT v3 romTexMap: mod %d maps to port %u, past the %u a texnum can name; "
				             "its textures above that will not draw",
				             ownerModIdx, modBase + maxSlot, MOD_TEX_PORT_MAX);
			}
			m->count = numTexMap;
			for (u32 i = 1; i < m->count; ++i) {
				struct modTexMapEntry e = m->entries[i];
				s32 j = (s32)i - 1;
				while (j >= 0 && m->entries[j].localTexId > e.localTexId) {
					m->entries[j + 1] = m->entries[j];
					--j;
				}
				m->entries[j + 1] = e;
			}
		}

		sysLogPrintf(LOG_NOTE, "PDFT v3 romTexMap: %u entries (mod=%d)", numTexMap, ownerModIdx);
		PDFT("texmap table=fragment mod=%d entries=%u portBase=0x%04x nextPort=0x%04x",
		     ownerModIdx, numTexMap, modBase, g_NextGlobalTexPort);
	}

	return 1;
}

static s32 romdataLoadModFileTable(s32 modIdx)
{
	if (modIdx < 0 || (u32)modIdx >= g_NumModDirs) return 0;
	if (!modDirs[modIdx][0]) return 0;
	if (modIdx >= MOD_TEX_MAP_MAX_MODS) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModFileTable: modIdx %d exceeds MOD_TEX_MAP_MAX_MODS", modIdx);
		return 0;
	}

	char relPath[FS_MAXPATH + 32];
	snprintf(relPath, sizeof(relPath), "%s/filetable.dat", modDirs[modIdx]);
	char fullPath[FS_MAXPATH + 32];
	strncpy(fullPath, fsFullPath(relPath), sizeof(fullPath) - 1);
	fullPath[sizeof(fullPath) - 1] = '\0';

	FILE *f = fopen(fullPath, "rb");
	if (!f) {
		sysLogPrintf(LOG_NOTE, "romdataLoadModFileTable: no fragment at %s", fullPath);
		return 0;
	}
	fseek(f, 0, SEEK_END);
	long fsz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsz < 12) {
		fclose(f);
		return 0;
	}
	u8 *buf = sysMemZeroAlloc((u32)fsz + 1);
	if (!buf) {
		fclose(f);
		return 0;
	}
	fread(buf, 1, (size_t)fsz, f);
	fclose(f);

	sysLogPrintf(LOG_NOTE, "Loading per-mod filetable: %s (%ld bytes)", fullPath, fsz);

	if (!romdataParseFileTable(buf, (u32)fsz, modIdx)) {
		sysLogPrintf(LOG_ERROR, "Failed to parse per-mod filetable: %s", fullPath);
		sysMemFree(buf);
		return 0;
	}

	// Retain ownership; names/paths are referenced into this buffer.
	if (g_ModFileTableData[modIdx]) {
		sysMemFree(g_ModFileTableData[modIdx]);
	}
	g_ModFileTableData[modIdx] = buf;
	return 1;
}

static inline s32 romdataLoadExternalFileTable(void)
{
	u32 size = 0;
	externalFileTableData = fsFileLoad("filetable.dat", &size);
	if (!externalFileTableData || size < 12) {
		if (externalFileTableData) {
			sysMemFree(externalFileTableData);
			externalFileTableData = NULL;
		}
		return 0;
	}
	externalFileTableSize = size;

	if (!romdataParseFileTable(externalFileTableData, size, -1)) {
		sysLogPrintf(LOG_ERROR, "Failed to parse external file table");
		sysMemFree(externalFileTableData);
		externalFileTableData = NULL;
		return 0;
	}

	return 1;
}

static inline void romdataInitFiles(void)
{
	// First load from ROM if available
	if (g_RomFile) {
		// the file offset table is in the data seg
		const u32 *offsets = (u32 *)(romDataSeg + ROMDATA_FILES_OFS);
		u32 i;
		for (i = 1; offsets[i]; ++i) {
			g_RomFileOffsetCount = (s32)i + 1;
			if (offsets + i + 1 < (u32 *)(romDataSeg + romDataSegSize)) {
				const u32 nextofs = PD_BE32(offsets[i + 1]);
				const u32 ofs = PD_BE32(offsets[i]);
				int mod;
				// [0, g_NumModDirs). The inclusive bound wrote a row no mod
				// index can name, and g_NumModDirs is unsigned so `mod` was
				// converted rather than the bound.
				for (mod = 0; mod < (s32)g_NumModDirs; ++mod) {
					fileSlots[mod][i].data = g_RomFile + ofs;
					fileSlots[mod][i].size = nextofs - ofs;
					fileSlots[mod][i].source = SRC_UNLOADED;
					fileSlots[mod][i].preprocessed = 0;
				}
			}
		}

		// last offset is to the name table
		const u32 *nameOffsets = (u32 *)(g_RomFile + PD_BE32(offsets[i - 1]));
		for (i = 1; nameOffsets[i]; ++i) {
			const u32 ofs = PD_BE32(nameOffsets[i]);
			for (s32 mod = 0; mod < (s32)g_NumModDirs; ++mod) {
				fileSlots[mod][i].name = (const char *)nameOffsets + ofs; // ofs is relative to the start of the name table
			}
		}

		// Kept so "vanilla" can be named after the per-mod fragments have
		// overlaid the rows: there is no vanilla row, every row starts as a
		// copy of this table. See romdataRomFileName.
		g_RomNameTable = nameOffsets;
		g_RomNameCount = (s32)i;
		g_RomFileOffsets = offsets;

		for (i = 1; i < (u32)(sizeof(fileSlots[0]) / sizeof(fileSlots[0][0])); ++i) {
			// `mod < g_NumModDirs - 1` skipped the last mod row, so the
			// last mod never inherited the ROM's file names and sizes.
			for (s32 mod = 1; mod < (s32)g_NumModDirs; ++mod) {
				fileSlots[mod][i] = fileSlots[0][i];
			}
		}
	} else {
		// no ROM; try to load the file name list from disk
		if (!romdataLoadExternalFileList()) {
			// If no ROM and no file list, we rely entirely on external file table
			// But we can't error out yet if external table exists
		}
	}

	// Then overlay external file table
	s32 globalLoaded = romdataLoadExternalFileTable();

	// Per-mod PDFT v3 fragments: each --moddir may ship its own
	// filetable.dat. They overlay on top of the global table and
	// only populate fileSlots[modIdx][...] for their own mod row
	// (modIdx is 0-based, matching modDirs[] / g_ModNum).
	for (u32 i = 0; i < g_NumModDirs; ++i) {
		if (i >= MOD_TEX_MAP_MAX_MODS) break;
		romdataLoadModFileTable((s32)i);
	}

	if (globalLoaded) {
		return;
	}

	if (!g_RomFile && !fileSlots[0][1].name) { // Check if we have anything
		sysFatalError("No ROM file or external filename table found.");
	}
}

static inline void romdataResetFile(s32 modNum, s32 fileNum)
{
	// 1. Load from ROM table first (default)
	//
	// Only for slots the ROM's table actually has. The table is terminated by
	// a zero offset and the name table follows it in the same segment, so
	// for a mod-local id (>= FIRST_LOCAL_ID) the two reads below landed in
	// the names and came back as plausible-looking offsets. Measured with
	// --asset-probe: a mod head whose declared rom source was not mounted
	// "loaded" 32768 bytes from the base ROM and reported success.
	if (romDataSeg && fileNum + 1 < g_RomFileOffsetCount) {
		const u32 *offsets = (u32 *)(romDataSeg + ROMDATA_FILES_OFS);
		if (offsets + fileNum + 1 < (u32 *)(romDataSeg + romDataSegSize)) {
			const u32 nextofs = PD_BE32(offsets[fileNum + 1]);
			const u32 ofs = PD_BE32(offsets[fileNum]);

			// Validate offsets to ensure we aren't reading garbage beyond the actual file table
			if (ofs < g_RomFileSize && nextofs <= g_RomFileSize && nextofs >= ofs) {
				fileSlots[modNum][fileNum].data = g_RomFile + ofs;
				fileSlots[modNum][fileNum].size = nextofs - ofs;
			} else {
				fileSlots[modNum][fileNum].data = NULL;
				fileSlots[modNum][fileNum].size = 0;
			}
			fileSlots[modNum][fileNum].source = SRC_UNLOADED;
			fileSlots[modNum][fileNum].preprocessed = 0;
		}
	} else if (romDataSeg) {
		// a mod-local slot: nothing in the ROM to fall back to, so the next
		// load has to come from a declared source, the mod dirs or the base
		// dir, or fail honestly
		fileSlots[modNum][fileNum].data = NULL;
		fileSlots[modNum][fileNum].size = 0;
		fileSlots[modNum][fileNum].source = SRC_UNLOADED;
		fileSlots[modNum][fileNum].preprocessed = 0;
	}

	// 2. Override with External table if present
	if (externalFileTableData) {
		u8 *data = externalFileTableData;
		u32 numFiles = PD_BE32(*(u32*)(data + 8));
		u8 *p = data + 12;

		for (u32 i = 0; i < numFiles; ++i) {
			u32 id = PD_BE32(*(u32*)p); p += 4;
			u32 flags = PD_BE32(*(u32*)p); p += 4;
			u32 offset = PD_BE32(*(u32*)p); p += 4;
			u32 fileSize = PD_BE32(*(u32*)p); p += 4;
			u16 nameLen = PD_BE16(*(u16*)p); p += 2 + nameLen;
			u16 pathLen = PD_BE16(*(u16*)p); p += 2 + pathLen;

			if (id == fileNum) {
				if (flags & PDFT_F_ROMRESIDENT) {
					fileSlots[modNum][fileNum].data = g_RomFile + offset;
					fileSlots[modNum][fileNum].size = fileSize;
					fileSlots[modNum][fileNum].source = SRC_UNLOADED;
				}
				fileSlots[modNum][fileNum].preprocessed = 0;
				return;
			}
		}
	}
}

void romdataResetMod(s32 modNum)
{
	if (modNum < 0 || modNum >= 64) {
		return;
	}

	for (s32 i = 1; i < ROMDATA_MAX_FILES; ++i) {
		if (fileSlots[modNum][i].source == SRC_EXTERNAL) {
			sysMemFree(fileSlots[modNum][i].data);
			fileSlots[modNum][i].data = NULL;
		}
		romdataResetFile(modNum, i);
	}
}

static inline struct romfile *romdataGetSeg(const char *name)
{
	struct romfile *seg = romSegs;
	while (seg->name && strcmp(name, seg->name)) {
		++seg;
	}
	return seg;
}

// ---------------------------------------------------------------------------
// Character-model swap overlay ROM (pd.load_model_rom / pd.model_swap)
//
// From the Perfect Dark Kai fork (be46717), where the overlay shared the
// MOD_CHAINROM file slot of Kai's --mod-rom chain loader. This build has no
// chain loader and its fileSlots rows belong to the modloader, so the overlay
// keeps a table of its own. A second PD ROM is loaded WITHOUT switching any
// active slot. While g_ModelSwapActive is set, character-model files flagged
// in g_ModelSwapFiles are served from the overlay by NAME (so a mod that
// reordered its file table still resolves), falling back to the same index.
// Model textures come from the overlay's own texture table (texdecompress.c)
// while a swapped model loads (g_ModelSwapTexActive).
// ---------------------------------------------------------------------------

s32 g_ModelRomActive = 0;                      // overlay ROM loaded
s32 g_ModelSwapActive = 0;                     // sourcing flagged models from the overlay now
u8 g_ModelSwapFiles[ROMDATA_MAX_FILES] = { 0 }; // per raw vanilla file id: 1 = redirect when active
// Diagnostics: bumped each time the redirect actually serves overlay bytes, and
// each time it was armed for a file but couldn't (name miss / null data).
s32 g_ModelSwapRedirects = 0;
s32 g_ModelSwapMisses = 0;

// Overlay TEXTURE table: a copy of the overlay's textureslist (per-texture
// dataoffset only) and a pointer to its texturesdata, so texLoad can serve
// overlay textures BY NUMBER while a swapped model loads, without touching the
// base game's textures.
struct texture *g_ModelSwapTexList = NULL;
s32 g_ModelSwapTexCount = 0;
u8 *g_ModelSwapTexData = NULL;
u32 g_ModelSwapTexDataSize = 0;
s32 g_ModelSwapTexActive = 0; // set only while a swapped model's textures load

struct modelromfile {
	u8 *data;
	u32 size;
	const char *name;
};

static u8 *modelRomFile;
static u32 modelRomFileSize;
static u8 *modelRomDataSeg;
static u32 modelRomDataSegSize;
static struct modelromfile *modelRomFiles; // ROMDATA_MAX_FILES entries once loaded

// Raw ROM offset of the stock texturesdata segment, captured before
// romdataInitSegment turns it into a pointer. The overlay's texturesdata is
// located by correlating texture bytes against the base ROM there.
static u32 g_StockTexDataOfs = 0;

// Load and validate an overlay PD ROM, inflating its compressed data segment.
// Kai's romdataLoadRomFile with fatal=false and requireHeader=false: any
// problem logs a warning, frees what it allocated and returns false. Total
// conversions commonly EXPAND the ROM past the stock 32MB, so anything at
// least stock-sized is accepted; a changed title/cart id only warns. The data
// segment and file table are still read at their stock offsets, so a mod that
// relocated those fails the 1173 check and is skipped.
static bool romdataLoadOverlayRomFile(const char *name, u8 **outRom, u32 *outSize, u8 **outSeg, u32 *outSegSize)
{
	sysLogPrintf(LOG_NOTE, "model-swap ROM file: %s", name);

	u32 romSize = 0;
	u8 *rom = fsFileLoad(name, &romSize);

	if (!rom) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: could not open %s", name);
		return false;
	}

	if (romSize > 2 && (!memcmp(rom, "PK", 2) || !memcmp(rom, "Rar", 3) || !memcmp(rom, "7z", 2))) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: %s is an archive, skipping", name);
		sysMemFree(rom);
		return false;
	}

	if (romSize < ROMDATA_ROM_SIZE) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: %s too small (%u), skipping", name, romSize);
		sysMemFree(rom);
		return false;
	} else if (romSize != ROMDATA_ROM_SIZE) {
		sysLogPrintf(LOG_WARNING, "model-swap ROM is %u bytes (stock %u) — expanded mod, loading anyway", romSize, ROMDATA_ROM_SIZE);
	}

	if (memcmp(rom + 0x3b, ROMDATA_ROM_ID, 4) || memcmp(rom + 0x20, ROMDATA_ROM_TITLE, sizeof(ROMDATA_ROM_TITLE) - 1)) {
		sysLogPrintf(LOG_WARNING, "model-swap ROM header does not match stock %s; loading anyway (mod/total conversion)", ROMDATA_ROM_DESC);
	}

	u8 *zipped = rom + ROMDATA_DATA_OFS;
	if (!rzipIs1173(zipped)) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: %s data segment not 1173-compressed at 0x%x (relocated/incompatible layout), skipping", name, ROMDATA_DATA_OFS);
		sysMemFree(rom);
		return false;
	}

	const u32 dataSegLen = ((u32)zipped[2] << 16) | ((u32)zipped[3] << 8) | (u32)zipped[4];
	if (dataSegLen < ROMDATA_FILES_OFS) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: %s data segment too small (%u), skipping", name, dataSegLen);
		sysMemFree(rom);
		return false;
	}

	u8 *dataSeg = sysMemAlloc(dataSegLen);
	if (!dataSeg) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: could not alloc %u for data seg, skipping", dataSegLen);
		sysMemFree(rom);
		return false;
	}

	u8 scratch[5 * 1024];
	if (rzipInflate(zipped, dataSeg, scratch) < 0) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: could not inflate %s data seg, skipping", name);
		sysMemFree(dataSeg);
		sysMemFree(rom);
		return false;
	}

	*outRom = rom;
	*outSize = romSize;
	*outSeg = dataSeg;
	*outSegSize = dataSegLen;
	return true;
}

// Build the overlay file table from the overlay ROM's own offset and name
// tables (Kai's romdataInitChainFiles). The overlay is an arbitrary user file,
// and a mod that relocated its file table leaves this region pointing at
// unrelated inflated data, so both loops carry hard bounds.
static bool romdataInitOverlayFiles(void)
{
	const u32 *offsets = (const u32 *)(modelRomDataSeg + ROMDATA_FILES_OFS);
	u32 i;

	modelRomFiles = sysMemAlloc(ROMDATA_MAX_FILES * sizeof(*modelRomFiles));
	if (!modelRomFiles) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: could not alloc the overlay file table");
		return false;
	}
	memset(modelRomFiles, 0, ROMDATA_MAX_FILES * sizeof(*modelRomFiles));

	for (i = 1; i < ROMDATA_MAX_FILES; ++i) {
		// break, don't skip: once the table runs past the data seg every later
		// entry is out of bounds too.
		if ((u8 *)(offsets + i + 1) + sizeof(u32) > modelRomDataSeg + modelRomDataSegSize) {
			break;
		}
		if (!offsets[i]) {
			break;
		}

		const u32 nextofs = PD_BE32(offsets[i + 1]);
		const u32 ofs = PD_BE32(offsets[i]);
		// Only keep entries that lie inside the ROM; the rest stay NULL and
		// fall through to the base file.
		if (ofs < modelRomFileSize && nextofs >= ofs && nextofs <= modelRomFileSize) {
			modelRomFiles[i].data = modelRomFile + ofs;
			modelRomFiles[i].size = nextofs - ofs;
		}
	}

	if (i < 2) {
		return true;
	}

	// last offset is to the name table: validate it lands inside the ROM
	// before dereferencing, then bound the walk the same way.
	const u32 nametableofs = PD_BE32(offsets[i - 1]);

	if (nametableofs >= modelRomFileSize) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: name table offset 0x%x past end of overlay ROM (0x%x)",
				nametableofs, modelRomFileSize);
		return true;
	}

	const u32 *nameOffsets = (const u32 *)(modelRomFile + nametableofs);
	const u32 namemax = (modelRomFileSize - nametableofs) / sizeof(u32);

	for (i = 1; i < ROMDATA_MAX_FILES && i < namemax && nameOffsets[i]; ++i) {
		const u32 ofs = PD_BE32(nameOffsets[i]);
		// ofs is relative to the start of the name table
		if ((u64)nametableofs + ofs < modelRomFileSize
				&& memchr((const u8 *)nameOffsets + ofs, 0, modelRomFileSize - nametableofs - ofs)) {
			modelRomFiles[i].name = (const char *)nameOffsets + ofs;
		}
	}

	return true;
}

// read the 24-bit big-endian dataoffset of textureslist entry n
static inline u32 romdataTexListDofs(const u8 *rom, u32 listOfs, u32 n)
{
	const u8 *e = rom + listOfs + n * 8;
	return ((u32)e[1] << 16) | ((u32)e[2] << 8) | e[3];
}

// Locate the overlay ROM's texture table and data (Kai's
// romdataChainRelocateTexSegments, overlay path only).
//
// Total conversions commonly grow the texture data, which shifts the trailing
// texturesdata/textureslist/copyright segments away from their stock offsets.
// The textureslist is found by signature: 8-byte entries with a non-decreasing
// 24-bit big-endian dataoffset in bytes 1-3 and zeroes in bytes 4-7, first
// entry at dataoffset 0; the final (terminator) entry holds the total
// texturesdata size. texturesdata itself is NOT reliably adjacent to the list,
// so its base is found by correlation against the base ROM: PD-derived mods
// keep many stock textures byte-identical, so sample entries across the list,
// take each texture's first bytes from the base ROM and search for them in the
// overlay ROM; every hit votes for an implied base offset, majority wins. On an
// unmodified ROM all of this reproduces the stock offsets exactly.
//
// Best-effort: if either cannot be found, geometry still swaps and textures
// stay base.
static void romdataOverlayLocateTextures(void)
{
	const u8 *rom = modelRomFile;
	u32 bestOfs = 0, bestCount = 0, bestTerm = 0;
	u32 runOfs = 0, runCount = 0, runRises = 0, prevDofs = 0;
	// diagnostics: largest run seen regardless of whether it qualified
	u32 dbgBestRun = 0, dbgBestRunOfs = 0, dbgBestRunRises = 0, dbgBestRunTerm = 0;

	for (u32 o = 0; o + 8 <= modelRomFileSize; o += 8) {
		const u32 dofs = ((u32)rom[o + 1] << 16) | ((u32)rom[o + 2] << 8) | rom[o + 3];
		const s32 entryok = rom[o + 4] == 0 && rom[o + 5] == 0 && rom[o + 6] == 0 && rom[o + 7] == 0
			&& (runCount == 0 ? dofs == 0 : dofs >= prevDofs);

		if (entryok) {
			if (runCount == 0) {
				runOfs = o;
				runRises = 0;
			} else if (dofs > prevDofs) {
				++runRises;
			}
			prevDofs = dofs;
			++runCount;
			continue;
		}

		if (runCount) {
			// trim fake leading entries: zero padding right before the real
			// list can parse as extra zero-dataoffset entries. The real first
			// entry is the only zero-dataoffset entry whose successor has a
			// nonzero dataoffset.
			while (runCount > 1) {
				const u32 second = ((u32)rom[runOfs + 9] << 16) | ((u32)rom[runOfs + 10] << 8) | rom[runOfs + 11];
				if (second != 0) {
					break;
				}
				runOfs += 8;
				--runCount;
			}

			if (runCount > dbgBestRun) {
				dbgBestRun = runCount;
				dbgBestRunOfs = runOfs;
				dbgBestRunRises = runRises;
				dbgBestRunTerm = prevDofs;
			}
			// 8-byte aligned, plenty of entries, mostly increasing (rejects
			// zero-filled regions).
			if ((runOfs & 7) == 0 && runCount >= 1024 && runRises >= runCount / 2
					&& prevDofs >= 0x10000 && runCount > bestCount) {
				bestOfs = runOfs;
				bestCount = runCount;
				bestTerm = prevDofs;
			}
			runCount = 0;
			// the entry that broke the run may start a new one (the loop's
			// o += 8 brings it back to this entry)
			o -= 8;
		}
	}

	if (!bestCount) {
		sysLogPrintf(LOG_WARNING, "model-swap: could not locate a textureslist; overlay textures disabled "
			"(best run: %u entries at 0x%x, rises=%u, term=0x%x)",
			dbgBestRun, dbgBestRunOfs, dbgBestRunRises, dbgBestRunTerm);
		return;
	}

	// The stock list is read from the preprocessed textureslist segment:
	// preprocessTexturesList rewrites it in place inside g_RomFile, so the raw
	// bytes there are no longer the big-endian entries. (Kai read the raw
	// bytes, which after the rewrite only matched by chance.)
	const struct romfile *segList = romdataGetSeg("textureslist");
	const struct texture *stockList = (const struct texture *)segList->data;
	const u32 stockDataOfs = g_StockTexDataOfs;
	const u32 stockCount = stockList ? segList->size / sizeof(struct texture) : 0;

	// The stock texture data must sit inside the base ROM, or the correlation
	// below would read a wild address.
	if (!g_RomFile || stockDataOfs >= g_RomFileSize || stockCount < 10) {
		sysLogPrintf(LOG_WARNING, "model-swap: base texture table invalid (data 0x%x, %u entries, rom 0x%x); overlay textures disabled",
			stockDataOfs, stockCount, g_RomFileSize);
		return;
	}

	// Vote for the texturesdata base by correlating texture bytes with the
	// base ROM. A texture-replacing conversion shares few textures with the
	// base, so sample many, and reject any base whose data region would
	// overlap the textureslist (coincidental matches). One surviving 16-byte
	// exact match at a geometrically valid base is enough.
	enum { CORR_SAMPLES = 256, CORR_PATLEN = 16, CORR_MAXCAND = 64, CORR_MAXHITS = 16, CORR_MINVOTES = 1 };
	struct { u32 base; u32 votes; } cand[CORR_MAXCAND];
	u32 numCand = 0;

	const u32 maxn = (bestCount < stockCount ? bestCount : stockCount) - 1;

	// Not in Kai: first try the textures whose compressed SIZE is the same in
	// both lists. A texture a mod kept is almost always one of these, so this
	// finds the survivors directly instead of hoping an evenly spread sample
	// lands on them (GoldenEye X keeps only about a dozen, and the spread
	// sample found none of them and settled on a coincidental match). Kai's
	// spread sample below still runs when this finds nothing.
	u32 samples[CORR_SAMPLES];
	u32 numSamples = 0;

	for (u32 n = 0; n < maxn && numSamples < CORR_SAMPLES; ++n) {
		const u32 sThis = stockList[n].dataoffset;
		const u32 sNext = stockList[n + 1].dataoffset;
		const u32 cThis = romdataTexListDofs(rom, bestOfs, n);
		const u32 cNext = romdataTexListDofs(rom, bestOfs, n + 1);
		if (sNext > sThis && sNext - sThis >= CORR_PATLEN && cNext > cThis
				&& sNext - sThis == cNext - cThis) {
			samples[numSamples++] = n;
		}
	}

	for (u32 pass = 0; pass < 2; ++pass) {
		const u32 passSamples = pass == 0 ? numSamples : CORR_SAMPLES;

		if (pass == 1 && numCand > 0) {
			break;
		}
		for (u32 s = 0; s < passSamples; ++s) {
			const u32 n = pass == 0 ? samples[s] : 8 + (u32)((u64)(maxn - 8) * s / CORR_SAMPLES);
			const u32 sThis = stockList[n].dataoffset;
			const u32 sNext = stockList[n + 1].dataoffset;
			const u32 cThis = romdataTexListDofs(rom, bestOfs, n);
			const u32 cNext = romdataTexListDofs(rom, bestOfs, n + 1);
			if (sThis >= sNext || cThis >= cNext) {
				continue; // no data for this texture in one of the ROMs
			}
			if ((u64)stockDataOfs + sThis + CORR_PATLEN > g_RomFileSize) {
				continue;
			}

			const u8 *pat = g_RomFile + stockDataOfs + sThis;
			const u8 *p = rom;
			const u8 *end = rom + modelRomFileSize - CORR_PATLEN;
			u32 hits = 0;

			while (p <= end && hits < CORR_MAXHITS) {
				p = memchr(p, pat[0], end - p + 1);
				if (!p) {
					break;
				}
				if (memcmp(p, pat, CORR_PATLEN) == 0) {
					++hits;
					const u32 pos = (u32)(p - rom);
					if (pos >= cThis) {
						const u32 base = pos - cThis;
						// the overlay's texturesdata sits BEFORE its textureslist
						if ((u64)base + bestTerm <= bestOfs) {
							u32 c;
							for (c = 0; c < numCand && cand[c].base != base; ++c);
							if (c < numCand) {
								++cand[c].votes;
							} else if (numCand < CORR_MAXCAND) {
								cand[numCand].base = base;
								cand[numCand].votes = 1;
								++numCand;
							}
						}
					}
				}
				++p;
			}
		}
	}

	u32 dataOfs = 0, dataVotes = 0;
	for (u32 c = 0; c < numCand; ++c) {
		if (cand[c].votes > dataVotes) {
			dataOfs = cand[c].base;
			dataVotes = cand[c].votes;
		}
	}

	if (dataVotes < CORR_MINVOTES || (u64)dataOfs + bestTerm > modelRomFileSize) {
		sysLogPrintf(LOG_WARNING, "model-swap: could not locate texturesdata (list at 0x%x, best base 0x%x with %u votes); overlay textures disabled",
			bestOfs, dataOfs, dataVotes);
		return;
	}

	sysLogPrintf(LOG_NOTE, "model-swap: textureslist at 0x%x (%u entries), texturesdata at 0x%x size 0x%x (%u votes; stock data 0x%x)",
		bestOfs, bestCount, dataOfs, bestTerm, dataVotes, stockDataOfs);

	// Keep only the dataoffset of each entry; it is all loading pixels needs.
	struct texture *list = sysMemAlloc(bestCount * sizeof(struct texture));
	if (!list) {
		sysLogPrintf(LOG_WARNING, "model-swap: could not alloc %u-entry overlay texture table", bestCount);
		return;
	}
	memset(list, 0, bestCount * sizeof(struct texture));
	for (u32 i = 0; i < bestCount; ++i) {
		const u8 *e = rom + bestOfs + i * 8;
		list[i].dataoffset = ((u32)e[1] << 16) | ((u32)e[2] << 8) | e[3];
	}
	g_ModelSwapTexList = list;
	g_ModelSwapTexCount = (s32)bestCount;
	g_ModelSwapTexData = modelRomFile + dataOfs;
	g_ModelSwapTexDataSize = modelRomFileSize - dataOfs;
	sysLogPrintf(LOG_NOTE, "model-swap: overlay texture table ready (%u entries, data at 0x%x)", bestCount, dataOfs);
}

// The overlay file whose name matches, or -1.
s32 romdataModelRomFileGetNumForName(const char *name)
{
	if (!modelRomFiles || !name || !name[0]) {
		return -1;
	}

	for (s32 i = 1; i < ROMDATA_MAX_FILES; ++i) {
		if (modelRomFiles[i].name && !strcmp(modelRomFiles[i].name, name)) {
			return i;
		}
	}

	return -1;
}

// Serve a flagged vanilla file from the overlay, or NULL. name is the base
// file's name; rawFileNum its raw id.
static u8 *romdataModelRomRedirect(s32 rawFileNum, const char *name, u32 *outSize)
{
	// Prefer matching by NAME (handles a mod that reordered its file table).
	s32 cn = romdataModelRomFileGetNumForName(name);

	if (cn <= 0 || !modelRomFiles[cn].data) {
		// Name lookup failed. PD-derived total conversions keep the stock file
		// NUMBERING but often carry a name table the stock-offset parse can't
		// resolve, while the file-offset table is correct. So fall back to the
		// SAME index, guarded on a real 1173 header so a garbage slot can never
		// be served.
		if (modelRomFiles[rawFileNum].data && modelRomFiles[rawFileNum].size > 5
				&& rzipIs1173(modelRomFiles[rawFileNum].data)) {
			cn = rawFileNum;
		}
	}

	if (cn > 0 && modelRomFiles[cn].data) {
		g_ModelSwapRedirects++;
		if (outSize) {
			*outSize = modelRomFiles[cn].size;
		}
		return modelRomFiles[cn].data;
	}

	// armed but couldn't serve
	g_ModelSwapMisses++;
	return NULL;
}

// Largest overlay ROM a script may load: a 512 Mbit cartridge.
#define ROMDATA_MODELROM_MAXSIZE (64 * 1024 * 1024)

// pd.load_model_rom takes its path from a script, so it only reaches the
// game's own folders: a relative path, or one starting with `$B/`, `$S/` or
// `$M/`. No absolute paths, no other `$` prefixes, no `.` or `..` components,
// no backslashes or colons.
static s32 romdataModelRomPathIsSafe(const char *path)
{
	const char *p = path;

	if (strlen(path) >= FS_MAXPATH - 2 || fsPathIsAbsolute(path)) {
		return 0;
	}

	if (strchr(path, '\\') || strchr(path, ':')) {
		return 0;
	}

	if (path[0] == '$') {
		if ((path[1] != 'B' && path[1] != 'S' && path[1] != 'M') || path[2] != '/') {
			return 0;
		}
		p = path + 3;
	}

	while (*p) {
		const char *slash = strchr(p, '/');
		size_t n = slash ? (size_t)(slash - p) : strlen(p);

		if (n == 0 || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) {
			return 0;
		}

		p += n;

		if (*p == '/') {
			p++;
		}
	}

	return 1;
}

// A file the overlay loader may read whole: a regular file, ROM-sized, and no
// bigger than a cart. `path` is already resolved (no `$` prefix).
static s32 romdataModelRomSizeOk(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 && S_ISREG(st.st_mode)
		&& (long long)st.st_size >= (long long)ROMDATA_ROM_SIZE
		&& (long long)st.st_size <= (long long)ROMDATA_MODELROM_MAXSIZE;
}

// Load a model-swap overlay ROM at runtime. `path` may be a ROM FILE or a
// DIRECTORY (scanned for the first ROM-sized file, so a script can point at a
// folder and the user drops any-named z64 in it). A bare relative path is
// anchored to the working directory, like scripts/init.lua, not to the data
// dir. Returns 1 on success (or if one is already loaded), 0 if no usable ROM
// was found; nothing here is fatal.
s32 romdataLoadModelRom(const char *path)
{
	char filepath[FS_MAXPATH + 1] = { 0 };

	if (g_ModelRomActive) {
		return 1; // already loaded (idempotent)
	}
	if (!path || !path[0]) {
		return 0;
	}
	if (!romdataModelRomPathIsSafe(path)) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: refusing path %s", path);
		return 0;
	}

	const char *base = path;
	char anchored[FS_MAXPATH + 1];
	if (path[0] == '$') {
		// opendir and stat don't know fs.c's prefixes
		snprintf(anchored, sizeof(anchored), "%s", fsFullPath(path));
		base = anchored;
	} else if (!fsPathIsAbsolute(path) && path[0] != '.') {
		snprintf(anchored, sizeof(anchored), "./%s", path);
		base = anchored;
	}

	if (romdataModelRomSizeOk(base)) {
		snprintf(filepath, sizeof(filepath), "%s", base);
	} else {
		DIR *dr = opendir(base);
		struct dirent *de;

		if (!dr) {
			sysLogPrintf(LOG_NOTE, "romdataLoadModelRom: no folder %s", base);
			return 0;
		}

		while ((de = readdir(dr)) != NULL) {
			char cand[FS_MAXPATH + 1];
			if (de->d_name[0] == '.') {
				continue;
			}
			snprintf(cand, sizeof(cand), "%s/%s", base, de->d_name);
			if (romdataModelRomSizeOk(cand)) {
				snprintf(filepath, sizeof(filepath), "%s", cand);
				break;
			}
		}

		closedir(dr);

		if (!filepath[0]) {
			sysLogPrintf(LOG_NOTE, "romdataLoadModelRom: no ROM-sized file in %s", base);
			return 0;
		}
	}

	sysLogPrintf(LOG_NOTE, "romdataLoadModelRom: loading model overlay ROM: %s", filepath);
	if (!romdataLoadOverlayRomFile(filepath, &modelRomFile, &modelRomFileSize, &modelRomDataSeg, &modelRomDataSegSize)) {
		sysLogPrintf(LOG_WARNING, "romdataLoadModelRom: %s could not be loaded as an overlay (see above); model swap disabled", filepath);
		return 0;
	}
	if (!romdataInitOverlayFiles()) {
		sysMemFree(modelRomDataSeg);
		sysMemFree(modelRomFile);
		modelRomDataSeg = NULL;
		modelRomFile = NULL;
		return 0;
	}
	romdataOverlayLocateTextures();
	g_ModelRomActive = 1;
	return 1;
}

s32 romdataInit(void)
{
	if (getenv("PD_DEBUG_FILELOAD")) {
		g_DebugFileLoad = true;
		sysLogPrintf(LOG_NOTE, "File loading debugging enabled");
	}
	if (getenv("PD_DEBUG_FILETABLE")) {
		g_DebugFileTable = true;
		sysLogPrintf(LOG_NOTE, "PDFT tracing enabled");
	}

	romSourcesInit();

	const char *altRomName = sysArgGetString("--rom-file");
	if (altRomName) {
		romName = altRomName;
	}

	romdataLoadRom();

	// Raw stock offset of the texture data, for the model-swap overlay
	// (romdataOverlayLocateTextures), before romdataInitSegment below turns
	// it into a pointer.
	g_StockTexDataOfs = (u32)(uintptr_t)romdataGetSeg("texturesdata")->data;

	// set segments to point to the rom or load them externally
	for (struct romfile *seg = romSegs; seg->name; ++seg) {
		romdataInitSegment(seg);
	}

	// load file table from the files segment
	romdataInitFiles();

	// Model-swap overlay ROM via the --model-rom launch arg (optional; scripts
	// can load one at runtime with pd.load_model_rom instead).
	{
		const char *modelRomName = sysArgGetString("--model-rom");
		if (modelRomName) {
			romdataLoadModelRom(modelRomName);
		}
	}

	sysLogPrintf(LOG_NOTE, "romdataInit: loaded rom, size = %u", g_RomFileSize);

	return 0;
}

static inline bool romdataCheckGbcRomContents(const u8 *gbcRomFile, const u32 gbcRomSize)
{
	if (gbcRomSize != GBC_ROM_SIZE) {
		return false;
	}

	// ROM title
	if (memcmp(gbcRomFile + 0x134, "PerfDark   VPDE", 15) != 0) {
		return false;
	}

	// Licensee code
	if (memcmp(gbcRomFile + 0x144, "4Y", 2) != 0) {
		return false;
	}

	// Header and global checksums
	if (gbcRomFile[0x14D] != 0xA1 || gbcRomFile[0x14E] != 0xAD || gbcRomFile[0x14F] != 0x0F) {
		return false;
	}

	return true;
}

s32 romdataCheckGbcRom(void)
{
	if (fsFileSize(GBC_ROM_NAME) < 0) {
		// bail early if it doesn't exist to avoid generating error messages
		return false;
	}

	u32 gbcRomSize = 0;
	u8 *gbcRomFile = fsFileLoad(GBC_ROM_NAME, &gbcRomSize);
	if (!gbcRomFile) {
		return false;
	}

	const bool ret = romdataCheckGbcRomContents(gbcRomFile, gbcRomSize);
	sysMemFree(gbcRomFile);

	if (ret) {
		sysLogPrintf(LOG_NOTE, "romdataCheckGbcRom: valid GBC rom found");
	}

	return ret;
}

static char g_FileLoadModPrefix[32] = "MOD_FOJO";


static const char *romdataGetContextPrefix(void)
{
	const char *context;

	// Check for Boot
	if (g_MainIsBooting) {
		context = "BOOT";
		goto done;
	}

	// Check for Intro
	if (g_InCutscene) {
		context = "INTRO";
		goto done;
	}

	// Check for CI
	if (g_StageNum == STAGE_CITRAINING) {
		context = "CI";
		goto done;
	}

	// Check for 4MB Menu
	if (g_StageNum == STAGE_4MBMENU) {
		context = "MB";
		goto done;
	}

	// Check for Combat Simulator (Multiplayer)
	if (g_Vars.normmplayerisrunning) {
		context = "CS";
		goto done;
	}

	// Check for Co-op / Counter-Op / Team Missions
	if (g_Vars.mplayerisrunning) {
		if (g_MissionConfig.isteam) {
			context = "TEAM";
			goto done;
		}
		if (g_MissionConfig.isanti) {
			context = "ANTI";
			goto done;
		}
		context = "COOP";
		goto done;
	}

	// Default to Solo
	context = "SOLO";

done:
	DEBUG_FLOAD("romdataGetContextPrefix: stage=%d, normmplayerisrunning=%d, mplayerisrunning=%d, isteam=%d, isanti=%d -> context=%s\n",
		g_StageNum, g_Vars.normmplayerisrunning, g_Vars.mplayerisrunning, g_MissionConfig.isteam, g_MissionConfig.isanti, context);
	return context;
}

// ownerModName: the mod that OWNS the file being resolved (romdataFileLoad
// takes it from the file id's tag, falling back to g_ModNum for an untagged
// one). It used to be the active mod, which made the private-asset test below
// ask the wrong question once file ids started carrying owners.
static void romdataResolvePath(char *dst, const char *src, size_t dstSize, const char *currentModName, const char *ownerModName, bool requireExport)
{
	dst[0] = '\0';
	if (!src) {
		return;
	}

	const char *contextPrefix = romdataGetContextPrefix();

	// We need to parse the string. It might have pipes.
	// Format: metadata::path|metadata2::path2
	// Metadata: mod:mod_name,context:context,export

	char srcCopy[4096];
	strncpy(srcCopy, src, sizeof(srcCopy));
	srcCopy[sizeof(srcCopy) - 1] = '\0';

	char *bestMatch = NULL;
	int bestScore = -1;

	char *cursor = srcCopy;
	while (*cursor) {
		// Find end of current token (pipe or end of string)
		char *pipe = strchr(cursor, '|');
		if (pipe) {
			*pipe = '\0';
		}

		char *token = cursor;
		char *sep = strstr(token, "::");
		int score = -1;
		char *pathStart = token;

		if (sep) {
			*sep = '\0'; // Terminate metadata
			pathStart = sep + 2;
			char *metadata = token;

			// Parse metadata
			bool modMatch = false;
			bool contextMatch = false;
			bool hasMod = false;
			bool hasContext = false;
			bool hasExport = false;

			if (metadata[0] == '\0') {
				// Empty metadata = default
				score = 0;
			} else {
				// Parse comma-separated metadata
				char *metaCursor = metadata;
				while (*metaCursor) {
					char *comma = strchr(metaCursor, ',');
					if (comma) {
						*comma = '\0';
					}

					char *metaToken = metaCursor;
					char *valSep = strchr(metaToken, ':');
					if (valSep) {
						*valSep = '\0';
						char *key = metaToken;
						char *val = valSep + 1;

						if (strcmp(key, "mod") == 0) {
							hasMod = true;
							if (currentModName && strcmp(val, currentModName) == 0) {
								modMatch = true;
							}
						} else if (strcmp(key, "context") == 0) {
							hasContext = true;
							if (strcmp(val, contextPrefix) == 0) {
								contextMatch = true;
							}
						}
					} else {
						// Flag only (e.g. export)
						if (strcmp(metaToken, "export") == 0) {
							hasExport = true;
						}
					}

					if (comma) {
						metaCursor = comma + 1;
					} else {
						break;
					}
				}

				// Scoring logic
				if (requireExport && !hasExport) {
					score = -1; // Export required but not present
				} else if (hasMod && !modMatch && !hasExport) {
					score = -1; // Wrong mod (provider mismatch) and not exported
				} else if (hasContext && !contextMatch) {
					score = -1; // Wrong context
				} else if (hasMod && !hasExport && ownerModName && currentModName && strcmp(currentModName, ownerModName) != 0) {
					// Private asset (not exported), and this directory is not
					// the owning mod's
					score = -1;
				} else {
					// Matches constraints
					score = 0;
					if (hasMod && modMatch) score += 2;
					if (hasContext) score += 1;
				}
			}
		} else {
			// No :: separator, assume simple path (default)
			score = 0;
			if (requireExport) {
				score = -1; // Default path is not exported
			}
		}

		if (score > bestScore) {
			bestScore = score;
			bestMatch = pathStart;
		}

		if (pipe) {
			cursor = pipe + 1;
		} else {
			break;
		}
	}

	if (bestMatch) {
		strncpy(dst, bestMatch, dstSize);
		dst[dstSize - 1] = '\0';
	}
}

s32 romdataFileGetSize(s32 fileNum)
{
	s32 modNum = g_ModNum;
	const s32 fileOwner = MOD_FILEID_MOD(fileNum);
	if (fileOwner >= 0) {
		modNum = fileOwner;
		fileNum = MOD_FILEID_RAW(fileNum);
	}

	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "romdataFileGetSize: invalid file num %d", fileNum);
		return -1;
	}

	// ensure any external files are loaded and we use their size
	if (romdataFileLoad(MOD_FILEID_MAKE(modNum, fileNum), NULL)) {
		return fileSlots[modNum][fileNum].size;
	}

	sysLogPrintf(LOG_ERROR, "romdataFileGetSize: could not load file num %d", fileNum);
	return -1;
}

u8 *romdataFileGetData(s32 fileNum)
{
	return romdataFileLoad(fileNum, NULL);
}

static bool romdataValidate(void *data, u32 size)
{
	if (!data || size == 0) {
		return false;
	}
	// TODO: Add more robust validation (e.g. rzip header check)
	return true;
}

// Resolve a file from a declared alternate ROM source - a PDFT entry that names
// the mounted image its bytes live in, and where inside it.
//
// The bytes returned are a pointer INTO that image, never an allocation. The
// slot is marked SRC_ALT_ROM for exactly that reason: romdataResetMod() and
// romdataFileFree() free a slot's data if and only if its source is
// SRC_EXTERNAL, so an alt slot is never handed to sysMemFree(). numpatches is
// zeroed for the same reason romdataFilePreprocess() matters here at all - it
// patches in place, and a patch applied to these bytes would write into the
// mounted image and corrupt it for every other file pointing at the same
// buffer.
//
// *outUnreadable is set when the declaration resolved to real bytes inside a
// mounted image that this build cannot decode. That is an engine gap, not a
// missing file, and the caller treats it differently from a miss.
static u8 *romdataFileLoadAltSource(s32 modNum, s32 fileNum,
		const struct romaltsource *as, bool *outUnreadable)
{
	if (as->romIdx == 0xff || as->romIdx >= g_NumRomSources) {
		return NULL;
	}

	struct romsource *rs = &g_RomSources[as->romIdx];

	// Declared, but the image is not mounted or the extent does not fit inside
	// it. Nothing is claimed; the caller falls back exactly as before.
	if (!rs->mounted || (!rs->data && !rs->ov)
			|| (u64)as->offset + (u64)as->size > (u64)rs->size) {
		return NULL;
	}

	if (as->compression != 0) {
		sysLogPrintf(LOG_WARNING,
			"romdataFileLoad: file %d altRom compression=%u not implemented",
			fileNum, as->compression);
		if (outUnreadable) {
			*outUnreadable = true;
		}
		return NULL;
	}

	if (rs->ov) {
		// An overlay: a pointer when one segment covers the whole extent -
		// into the base mapping for a file the patch left alone, into the
		// literal pool for one it wrote whole - and a composed copy, owned
		// like any external load, only when the extent crosses an edit.
		const u8 *direct = rompatchOverlayPeek(rs->ov, as->offset, as->size);
		if (direct) {
			fileSlots[modNum][fileNum].data = (u8 *)direct;
			fileSlots[modNum][fileNum].source = SRC_ALT_ROM;
		} else {
			u8 *buf = sysMemAlloc(as->size);
			if (!buf || !rompatchOverlayRead(rs->ov, as->offset, as->size, buf)) {
				sysMemFree(buf);
				if (outUnreadable) {
					*outUnreadable = true;
				}
				return NULL;
			}
			fileSlots[modNum][fileNum].data = buf;
			fileSlots[modNum][fileNum].source = SRC_EXTERNAL;
		}
	} else {
		fileSlots[modNum][fileNum].data = rs->data + as->offset;
		fileSlots[modNum][fileNum].source = SRC_ALT_ROM;
	}
	fileSlots[modNum][fileNum].size = as->size;
	fileSlots[modNum][fileNum].numpatches = 0;

	// sysLogPrintf(LOG_NOTE, "romdataFileLoad: file %d (%s) loaded from altRom '%s' at 0x%x (size=%u)",
	// 	fileNum, fileSlots[modNum][fileNum].name, rs->id, as->offset, as->size);

	return fileSlots[modNum][fileNum].data;
}

// Resolve a file from a declared SELF source - a PDFT entry carrying flag 0x8,
// which says its bytes are the loose file at its own path inside the directory
// of the mod that owns it.
//
// This is the file-based twin of an alt-ROM source and it is deliberately not
// folded into that lane, because the two differ in the one way that matters for
// memory: an alt-ROM hit hands back a pointer INTO a mounted image and must be
// marked SRC_ALT_ROM so romdataResetMod() and romdataFileFree() leave it alone,
// while this hands back an fsFileLoad() allocation and is SRC_EXTERNAL like any
// other loose file. Sharing one function would put an allocation and a borrowed
// pointer behind the same source tag.
//
// What it does NOT do is walk. That is the whole point: the walk scans every
// mounted mod dir highest index first, so two mods shipping the same relative
// path resolve by roster order, and a mod that says exactly where its bytes
// live should not be at the mercy of that. A miss here falls through to the
// walk rather than to the ROM, because unlike an alt-ROM extent this
// declaration can be wrong about a file that simply is not on disk, and the old
// behaviour is the safer floor.
//
// The slot is bound HERE rather than by the caller's shared `if (out)` block,
// even though that block would do the same thing today. The block sits between
// the walk and the ROM fallback, and wt/aliasfirst moves it inside a guard that
// a declared-source hit skips - correctly, for an alt-ROM hit, which borrows a
// pointer into a mounted image and must stay SRC_ALT_ROM. A self hit is an
// allocation and does want SRC_EXTERNAL, so it cannot depend on where that
// block ends up sitting.
static u8 *romdataFileLoadSelfSource(s32 modNum, s32 fileNum, u32 *outLoadedSize)
{
	char resolved[FS_MAXPATH];
	char tmp[FS_MAXPATH];
	const char *modName;
	u32 loadedSize = 0;
	u8 *out;

	if (modNum < 0 || modNum >= (s32)g_NumModDirs || !modDirs[modNum][0]) {
		return NULL;
	}

	modName = strrchr(modDirs[modNum], '/');
	modName = modName ? modName + 1 : modDirs[modNum];

	// mkfiletable refuses a self source whose path carries the pipe/:: variant
	// grammar, so this is a plain path for anything it built. Resolved anyway,
	// with the owner as the current mod, so a hand-written table behaves rather
	// than having its metadata handed to fopen().
	romdataResolvePath(resolved, fileSlots[modNum][fileNum].name, sizeof(resolved),
			modName, modName, false);

	if (resolved[0] == '\0') {
		return NULL;
	}

	// The same three-way join the walk uses, against the owner's directory only.
	if (!strncmp(resolved, ROMDATA_FILEDIR "/", strlen(ROMDATA_FILEDIR) + 1)) {
		snprintf(tmp, sizeof(tmp), "%s/%s", modDirs[modNum], resolved);
	} else if (!strncmp(resolved, "textures/", 9)) {
		snprintf(tmp, sizeof(tmp), "%s/" ROMDATA_FILEDIR "/%s", modDirs[modNum], resolved);
		if (fsFileSize(tmp) <= 0) {
			snprintf(tmp, sizeof(tmp), "%s/%s", modDirs[modNum], resolved);
		}
	} else {
		snprintf(tmp, sizeof(tmp), "%s/" ROMDATA_FILEDIR "/%s", modDirs[modNum], resolved);
	}

	if (fsFileSize(tmp) <= 0) {
		sysLogPrintf(LOG_WARNING,
				"romdataFileLoad: file %d (%s) is self-sourced in mod %d (%s), and %s is not "
				"there; falling back to the walk",
				fileNum, resolved, modNum, modName, tmp);
		return NULL;
	}

	out = fsFileLoad(tmp, &loadedSize);

	if (!romdataValidate(out, loadedSize)) {
		sysLogPrintf(LOG_WARNING, "file %d (%s) corrupted in its own mod %d (%s)",
				fileNum, resolved, modNum, modName);
		if (out) {
			sysMemFree(out);
		}
		return NULL;
	}

	fileSlots[modNum][fileNum].data = out;
	fileSlots[modNum][fileNum].size = loadedSize;
	fileSlots[modNum][fileNum].source = SRC_EXTERNAL;
	fileSlots[modNum][fileNum].numpatches = 0;

	sysLogPrintf(LOG_NOTE, "file %d (%s) loaded from its own mod %d (%s)",
			fileNum, resolved, modNum, modName);

	*outLoadedSize = loadedSize;
	return out;
}

u8 *romdataFileLoad(s32 fileNum, u32 *outSize)
{
	// The tagged test used to be `fileNum & 0xFFFF0000` written out by hand.
	// It agreed with the macro by luck rather than by construction, and it
	// answered "tagged" for a negative fileNum, whose owner is -1 and which
	// would then subscript fileSlots[-1].
	//
	// Asking MOD_FILEID_MOD unconditionally rather than testing
	// MOD_FILEID_IS_TAGGED first: the answer is the same either way, but only
	// the decoder sees the id, and only the decoder can report one carrying
	// mod bits with no tag. These four sites are the busiest consumers of file
	// ids in the port, so they are where a hand-rolled id is most likely to
	// arrive, and skipping the decoder would be the one place it could arrive
	// unremarked. An untagged or bogus id still falls back to the active mod
	// as before and is caught by the range check below.
	s32 modNum = g_ModNum;
	const s32 fileOwner = MOD_FILEID_MOD(fileNum);
	if (fileOwner >= 0) {
		modNum = fileOwner;
		fileNum = MOD_FILEID_RAW(fileNum);
	}

	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "romdataFileLoad: invalid file num %d", fileNum);
		return NULL;
	}

	u8 *out = NULL;

	// resolve the file's bytes: declared source, then loose files, then ROM
	if (fileSlots[modNum][fileNum].source == SRC_UNLOADED) {
		char tmp[FS_MAXPATH] = { 0 };
		char resolvedName[FS_MAXPATH];

		// Mod files are always eligible. The All-Solos-in-Multi switch
		// (g_NotLoadMod) and its per-stage overrides are retired; suppression
		// by context is not something the ownership model expresses.
		bool allowMod = true;

		// Always allow setup files to be modded
		const char *fileName = fileSlots[modNum][fileNum].name;
		if (fileName && strstr(fileName, "setup")) {
			allowMod = true;
		}

		u32 loadedSize = 0;

		// The name of the mod this file BELONGS to, not the one that happens to
		// be active. romdataResolvePath uses it for exactly one decision: a
		// path variant tagged `mod:X` and not `export` scores -1 when the
		// directory being scanned is not this mod, which is how a private asset
		// stays private. Built from g_ModNum, that test asked "is the active mod
		// the owner", so a correctly-owned id - mod A's stage file loaded while
		// mod B is active - had mod A's own private variant scored out and fell
		// through to a default path or to another mod's copy. modNum is the
		// owner resolved from the id above, and is identical to g_ModNum for an
		// untagged id, so nothing vanilla moves.
		const char *ownerModName = NULL;
		if (modNum >= 0 && modNum < g_NumModDirs && modDirs[modNum][0]) {
			ownerModName = strrchr(modDirs[modNum], '/');
			if (ownerModName) ownerModName++;
			else ownerModName = modDirs[modNum];
		}

		bool requireExport = !allowMod;

		// 1. Try the file's own declared SELF source.
		//
		// An entry carrying PDFT flag 0x8 has said its bytes are the loose file
		// at its own path inside its own mod's directory. That claim has to be
		// tested BEFORE the walk or it is worth nothing: the walk scans every
		// mounted mod dir highest index first, so the mod latest in the roster
		// that happens to ship a matching relative path wins, and reordering
		// the roster silently changes which mod's level a stage loads.
		//
		// Only the owner's own row is read - never the row-0 fallback the
		// alt-ROM lane below uses. File ids are mod-local and row 0 is doubly
		// booked as mod 0's row and the global table's, so a self source found
		// there is not a declaration BY the mod being loaded, and "its own
		// directory" would mean mod 0's.
		if (modNum >= 0 && modNum < MOD_TEX_MAP_MAX_MODS
				&& g_FileAltSource[modNum][fileNum].romIdx == ROMSOURCE_SELF) {
			out = romdataFileLoadSelfSource(modNum, fileNum, &loadedSize);
		}

		// 2. Try the file's own declared ROM source.
		//
		// This lane used to run LAST, after the walk and the base dir, guarded
		// by `!out`. That made a declaration the WEAKEST claim on a file's
		// bytes: the walk scans every mod dir highest-index-first, so any mod
		// later in the roster that happened to ship a matching relative path
		// overrode a mod that had said exactly where its bytes come from. A
		// declared source is authoritative now and the walk is the fallback.
		//
		// Mod files baked into a custom z64 (e.g. gex.z64) are pointed at by
		// g_FileAltSource[modIdx][localFileId], populated during PDFT v2/v3
		// fragment parse; modIdx is 0-based. Only the OWNER's own row is
		// promoted here - the `romIdx == 0xff` fallback to row 0 stays at its
		// old rank, below the walk. See the note on it further down.
		bool altUnreadable = false;

		s32 modSlot = (modNum >= 0 && modNum < MOD_TEX_MAP_MAX_MODS) ? modNum : 0;

		// Guarded, though nothing sets `out` above it today. This is the first
		// lane, so the test is dead on this branch - and it is the invariant
		// every lane below already keeps. Leaving it off would make this the
		// one assignment in the chain that CLEARS a hit rather than skipping:
		// romdataFileLoadAltSource() answers NULL for anything that is not an
		// index into g_RomSources, so a second declared-source lane added above
		// it would have its result overwritten with NULL and fall through to
		// the walk - silently, and only for files that had declared a source,
		// which is the failure this whole ordering exists to prevent.
		if (!out) {
			out = romdataFileLoadAltSource(modNum, fileNum,
					&g_FileAltSource[modSlot][fileNum], &altUnreadable);
		}

		// A declaration that resolved to bytes we cannot decode is not a miss
		// and does not fall through to the loose-file lanes. The point of
		// declaring a source is to say where the bytes come from; quietly
		// serving some other mod's file instead is the failure this ordering
		// exists to prevent. Fall through to the ROM.
		if (!out && !altUnreadable) {
			// 3. Try All Mods (Reverse Order)
			for (s32 i = g_NumModDirs - 1; i >= 0; --i) {
				if (modDirs[i][0]) {
					// Extract mod name from path (basename)
					const char *modName = strrchr(modDirs[i], '/');
					if (modName) {
						modName++; // Skip '/'
					} else {
						modName = modDirs[i];
					}

						// Resolve path specifically for this mod
					romdataResolvePath(resolvedName, fileSlots[modNum][fileNum].name, sizeof(resolvedName), modName, ownerModName, requireExport);

					if (resolvedName[0] == '\0') {
						continue; // No match for this mod
					}

					// If resolvedName already starts with "files/", don't prepend ROMDATA_FILEDIR
					if (strncmp(resolvedName, ROMDATA_FILEDIR "/", strlen(ROMDATA_FILEDIR) + 1) == 0) {
						snprintf(tmp, sizeof(tmp), "%s/%s", modDirs[i], resolvedName);
					} else if (strncmp(resolvedName, "textures/", 9) == 0) {
						// Special case for textures: check both files/textures and just textures
						snprintf(tmp, sizeof(tmp), "%s/" ROMDATA_FILEDIR "/%s", modDirs[i], resolvedName);
						if (fsFileSize(tmp) <= 0) {
							snprintf(tmp, sizeof(tmp), "%s/%s", modDirs[i], resolvedName);
						}
					} else {
						snprintf(tmp, sizeof(tmp), "%s/" ROMDATA_FILEDIR "/%s", modDirs[i], resolvedName);
					}

					// if (fileNum == FILE_CHEADGREY) {
					// 	sysLogPrintf(LOG_NOTE, "DEBUG: Checking for FILE_CHEADGREY at '%s'", tmp);
					// }

					if (fsFileSize(tmp) > 0) {
						out = fsFileLoad(tmp, &loadedSize);
						if (romdataValidate(out, loadedSize)) {
							sysLogPrintf(LOG_NOTE, "file %d (%s) loaded from mod %d (%s)", fileNum, resolvedName, i, modName);

							// The name came out of fileSlots[modNum], the bytes came
							// out of modDirs[i], and the result is about to be cached
							// as modNum's file. When those differ, one mod's asset is
							// being served as another's - which is how a stage ends up
							// loading a foreign setup or background without anything
							// being said. Whether this lane is a deliberate override
							// or a leak is d-ownership; until that is settled, say it
							// happened.
							if (i != modNum) {
								static s32 s_crossModWarnings = 0;

								if (s_crossModWarnings < 64) {
									++s_crossModWarnings;
									sysLogPrintf(LOG_WARNING,
											"romdataFileLoad: file %d (%s) is mod %d's, but its bytes came from mod %d (%s)"
											"%s",
											fileNum, resolvedName, modNum, i, modName,
											s_crossModWarnings == 64 ? " [further cross-mod loads not logged]" : "");
								}
							}

							break;
						} else {
							sysLogPrintf(LOG_WARNING, "file %d (%s) corrupted in mod %d, skipping", fileNum, resolvedName, i);
							if (out) { sysMemFree(out); out = NULL; }
						}
					}
				}
			}

			// 4. Try Base Dir (if not found in mod or corrupted)
			if (!out) {
				// Resolve generic path (no mod constraint)
				romdataResolvePath(resolvedName, fileSlots[modNum][fileNum].name, sizeof(resolvedName), NULL, ownerModName, requireExport);

				if (resolvedName[0] != '\0') {
					snprintf(tmp, sizeof(tmp), "$B/" ROMDATA_FILEDIR "/%s", resolvedName);
					if (fsFileSize(tmp) > 0) {
						out = fsFileLoad(tmp, &loadedSize);
						if (romdataValidate(out, loadedSize)) {
							sysLogPrintf(LOG_NOTE, "file %d (%s) loaded from base", fileNum, resolvedName);
						} else {
							sysLogPrintf(LOG_WARNING, "file %d (%s) corrupted in base, falling back", fileNum, resolvedName);
							if (out) { sysMemFree(out); out = NULL; }
						}
					}
				}
			}

			if (out) {
				fileSlots[modNum][fileNum].data = out;
				fileSlots[modNum][fileNum].size = loadedSize;
				fileSlots[modNum][fileNum].source = SRC_EXTERNAL;
				// external file; do not apply patches to this
				fileSlots[modNum][fileNum].numpatches = 0;
				DEBUG_FLOAD("romdataFileLoad: file %d (%s) loaded EXTERNALLY (size=%u, context=%s, allowMod=%d)",
					fileNum, fileSlots[modNum][fileNum].name, loadedSize, romdataGetContextPrefix(), allowMod);
			}

			// 5. Row 0 as a global alt-source table, at its old rank.
			//
			// g_FileAltSource row 0 is doubly booked - it is mod 0's own row AND
			// the row an isGlobal filetable.dat writes (see the note at the array
			// declaration). The old lane fell back to it whenever the per-mod row
			// said 0xff, and it stays HERE, below the walk, deliberately.
			//
			// Promoting it alongside the per-mod lane would change what it can
			// shadow. File ids are mod-local, so rows collide by construction:
			// measured on the shipped roster, mod_fojo's 46 declared ids (0x7e5
			// upwards) are ids the four unsourced mods also use, for unrelated
			// textures they ship as loose files. Promoted, row 0 would serve
			// Mikado head bytes for 46 ids in each of mod_aio_characters,
			// mod_gex_stages, mod_aio_stages and mod_kakariko_stages - 184
			// resolutions, every one of them wrong. A row-0 entry is not a
			// declaration BY the mod being loaded, so it does not get a
			// declaration's authority.
			if (!out && g_FileAltSource[modSlot][fileNum].romIdx == 0xff) {
				out = romdataFileLoadAltSource(modNum, fileNum,
						&g_FileAltSource[0][fileNum], NULL);
			}
		}

		if (fileSlots[modNum][fileNum].source == SRC_UNLOADED) {
			// tried and failed, fall back to ROM
			fileSlots[modNum][fileNum].source = SRC_ROM;
			DEBUG_FLOAD("romdataFileLoad: file %d (%s) FALLBACK TO ROM (context=%s, allowMod=%d)",
				fileNum, fileSlots[modNum][fileNum].name, romdataGetContextPrefix(), allowMod);
		}
	}

	// Model swap (pd.model_swap): serve a flagged vanilla character-model file
	// from the overlay ROM. The caller inflates and preprocesses the bytes
	// under the BASE file id, so texture linkage stays correct. Only files the
	// base ROM would serve are redirected; loose and alt-ROM files win.
	if (!out && g_ModelSwapActive && g_ModelRomActive && fileOwner < 0
			&& g_ModelSwapFiles[fileNum]
			&& fileSlots[modNum][fileNum].source == SRC_ROM) {
		u8 *swapped = romdataModelRomRedirect(fileNum, fileSlots[modNum][fileNum].name, outSize);
		if (swapped) {
			return swapped;
		}
	}

	if (!out) {
		out = fileSlots[modNum][fileNum].data;
	}

	if (out && outSize) {
		*outSize = fileSlots[modNum][fileNum].size;
	}

	return out;
}


void romdataFilePreprocess(s32 fileNum, s32 loadType, u8 *data, u32 size, u32 *outSize)
{
	s32 modNum = g_ModNum;
	const s32 fileOwner = MOD_FILEID_MOD(fileNum);
	if (fileOwner >= 0) {
		modNum = fileOwner;
		fileNum = MOD_FILEID_RAW(fileNum);
	}

	loadingFileNum = fileNum;
	// const char *fname = (fileNum >= 1 && fileNum < ROMDATA_MAX_FILES) ? fileSlots[modNum][fileNum].name : "???";
	// sysLogPrintf(LOG_NOTE, "romdataFilePreprocess: fileNum=%04x modNum=%d name=%s loadType=%d size=%u",
	// 	fileNum, modNum, fname ? fname : "(null)", loadType, size);
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "romdataFilePreprocess: invalid file num %d", fileNum);
		return;
	}

	if (data && size /* && !fileSlots[modNum][fileNum].preprocessed*/) {
		if (loadType && loadType < (u32)ARRAYCOUNT(filePreprocFuncs) && filePreprocFuncs[loadType]) {
			// apply patches
			for (u32 i = 0; i < fileSlots[modNum][fileNum].numpatches; ++i) {
				const struct romfilepatch *p = &fileSlots[modNum][fileNum].patches[i];
				if (!memcmp(data + p->ofs, p->src, p->len)) {
					memcpy(data + p->ofs, p->dst, p->len);
					sysLogPrintf(LOG_NOTE, "file %d (%s) patched at offset 0x%x", fileNum, fileSlots[modNum][fileNum].name, p->ofs);
				}
			}
			// then preprocess
			filePreprocFuncs[loadType](data, size, outSize, modNum);
			// fileSlots[modNum][fileNum].preprocessed = 1;
		}
	}
}

void romdataFileFree(s32 fileNum)
{
	s32 modNum = g_ModNum;
	const s32 fileOwner = MOD_FILEID_MOD(fileNum);
	if (fileOwner >= 0) {
		modNum = fileOwner;
		fileNum = MOD_FILEID_RAW(fileNum);
	}

	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "fsFileFree: invalid file num %d", fileNum);
		return;
	}

	if (fileSlots[modNum][fileNum].source == SRC_EXTERNAL) {
		sysMemFree(fileSlots[modNum][fileNum].data);
		fileSlots[modNum][fileNum].data = NULL;
	}

	fileSlots[modNum][fileNum].source = SRC_UNLOADED;
}

void romdataResetActiveMod(void)
{
	DEBUG_FLOAD("romdataResetActiveMod: Resetting files for mod %d (g_StageNum=0x%02x, restartlevel=%d)",
		g_ModNum, g_StageNum, g_Vars.restartlevel);
	romdataResetMod(g_ModNum);
}

const char *romdataFileGetName(s32 fileNum)
{
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		return NULL;
	}
	return fileSlots[g_ModNum][fileNum].name;
}

const u8 romDataFileNumExists(s32 modNum, s32 fileNum) {
	return fileSlots[modNum][fileNum].segstart != NULL && fileSlots[modNum][fileNum].segend != NULL;
}

const char *romdataFileGetSlotName(s32 modNum, s32 fileNum)
{
	if (modNum < 0 || modNum >= (s32)MOD_TEX_MAP_MAX_MODS) return NULL;
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) return NULL;
	return fileSlots[modNum][fileNum].name;
}

s32 romdataGetFileSlotCount(s32 modNum)
{
	if (modNum < 0 || modNum >= (s32)MOD_TEX_MAP_MAX_MODS) return 0;

	s32 count = 0;
	for (s32 fileNum = 1; fileNum < ROMDATA_MAX_FILES; ++fileNum) {
		if (fileSlots[modNum][fileNum].name) {
			count++;
		}
	}
	return count;
}

s32 romdataGetFileSlotInfo(s32 modNum, s32 fileNum, struct romdatafileslotinfo *outInfo)
{
	if (!outInfo || modNum < 0 || modNum >= (s32)MOD_TEX_MAP_MAX_MODS
			|| fileNum < 1 || fileNum >= ROMDATA_MAX_FILES
			|| !fileSlots[modNum][fileNum].name) {
		return 0;
	}

	outInfo->name = fileSlots[modNum][fileNum].name;
	outInfo->size = fileSlots[modNum][fileNum].size;
	outInfo->source = fileSlots[modNum][fileNum].source;
	outInfo->configuredSource = g_FileAltSource[modNum][fileNum].romIdx != 0xff
		? SRC_ALT_ROM : SRC_ROM;
	return 1;
}

s32 romdataFileGetNumForName(const char *name)
{
	if (!name || !name[0]) {
		return -1;
	}

	for (s32 i = 0; i < ROMDATA_MAX_FILES; ++i) {
		if (fileSlots[g_ModNum][i].name && !strcmp(fileSlots[g_ModNum][i].name, name)) {
			return i;
		}
	}

	return -1;
}

/*
 * Resolve a file name in any mod, tagging the result with the mod that owns it.
 *
 * Raw file ids are mod-LOCAL - see the note on fileSlots - so the bare slot
 * index this used to return named nothing on its own. Measured on the shipped
 * set: id 2019 is mod_fojo/CheadCatherineZ, mod_aio_characters/CbondranchZ and
 * mod_gex_characters/Cbaronsamedi2Z all at once, and 26 ids in all are claimed
 * by more than one mod under different names. The owner now travels with the
 * number as MOD_FILEID_MAKE(mod, id).
 *
 * Narrowing the result to s16 still yields exactly the id the old function
 * returned: raw ids stop at ROMDATA_MAX_FILES (8192), so the low 16 bits are
 * never negative and the tag and mod bits sit above them. A miss is still -1.
 *
 * The scan no longer stops at the first hit, but it still RETURNS the first
 * hit. Which mod wins is a separate decision and callers depend on today's
 * answer; the scan continues only so the ambiguity can be reported. The
 * warning is the point of this change - the tag is what makes the answer
 * checkable afterwards.
 *
 * What the warning catches: one name claimed by two or more mods, and the
 * sharper case where the hit being returned matched only on the basename of a
 * "mod:modname::files/Filename" slot while another mod matches the whole name
 * exactly. Precedence here is mod scan order, not match quality, so that case
 * is returning the weaker claim - it is reported, not silently reordered.
 *
 * What it cannot catch: fileSlots row 0 is mod 0's row and the row the global
 * (vanilla) file table writes, so a vanilla name resolves as mod 0 and a
 * vanilla/mod-0 collision is invisible from here. Nor does it notice two slots
 * inside one mod row carrying the same name; the lower id still wins silently,
 * as before.
 */
#define ANYMOD_AMBIG_MAX_REPORTS 16
#define ANYMOD_AMBIG_MAX_LISTED  8

static const char *romdataModLabel(s32 mod)
{
	if (mod >= 0 && mod < 64 && g_ModNames[mod][0]) {
		return g_ModNames[mod];
	}
	if (mod >= 0 && mod < (s32)g_NumModDirs && modDirs[mod][0]) {
		return modDirs[mod];
	}
	return "(unnamed)";
}

s32 romdataFileGetNumForNameAnyMod(const char *name)
{
	if (!name || !name[0]) {
		return -1;
	}

	s32 firstMod = -1;
	s32 firstId = -1;
	bool firstExact = false;

	s32 claimMod[ANYMOD_AMBIG_MAX_LISTED];
	s32 claimId[ANYMOD_AMBIG_MAX_LISTED];
	bool claimExact[ANYMOD_AMBIG_MAX_LISTED];
	s32 numListed = 0;
	s32 numClaims = 0;
	s32 numExact = 0;

	// Exclusive over g_NumModDirs. The inclusive bound read fileSlots one row
	// past the last mod, which was off the end of the array before it grew.
	for (s32 mod = 0; mod < (s32)g_NumModDirs; ++mod) {
		for (s32 i = 0; i < ROMDATA_MAX_FILES; ++i) {
			const char *slot = fileSlots[mod][i].name;

			if (!slot) {
				continue;
			}

			bool exact = !strcmp(slot, name);
			bool basename = false;

			if (!exact) {
				// Mod files are stored as "mod:modname::files/Filename".
				const char *slash = strrchr(slot, '/');
				basename = slash && !strcmp(slash + 1, name);
			}

			if (!exact && !basename) {
				continue;
			}

			if (firstMod < 0) {
				firstMod = mod;
				firstId = i;
				firstExact = exact;
			}

			if (numListed < ANYMOD_AMBIG_MAX_LISTED) {
				claimMod[numListed] = mod;
				claimId[numListed] = i;
				claimExact[numListed] = exact;
				numListed++;
			}

			numClaims++;
			if (exact) {
				numExact++;
			}

			// One claim per mod: the old code returned here, so the lowest id
			// in a row is still this row's answer.
			break;
		}
	}

	if (firstMod < 0) {
		return -1;
	}

	if (numClaims > 1) {
		// Capped so a pathological mod set cannot flood the log. 16 distinct
		// ambiguous names is far past the point the set needs looking at.
		static u32 reported = 0;

		if (reported < ANYMOD_AMBIG_MAX_REPORTS) {
			char list[512];
			u32 used = 0;

			reported++;

			list[0] = '\0';

			for (s32 c = 0; c < numListed; ++c) {
				s32 n = snprintf(list + used, sizeof(list) - used,
						"%smod %d '%s' id 0x%04x (%s)",
						used ? ", " : "", claimMod[c], romdataModLabel(claimMod[c]),
						claimId[c], claimExact[c] ? "exact" : "basename");

				if (n < 0 || (u32)n >= sizeof(list) - used) {
					break;
				}

				used += (u32)n;
			}

			sysLogPrintf(LOG_WARNING,
					"romdataFileGetNumForNameAnyMod: '%s' is claimed by %d mods: %s%s. "
					"Returning mod %d id 0x%04x (%s) - raw file ids are mod-local, so the "
					"name alone does not pick an owner.",
					name, numClaims, list, numClaims > numListed ? ", ..." : "",
					firstMod, firstId, firstExact ? "exact" : "basename");

			if (!firstExact && numExact > 0) {
				sysLogPrintf(LOG_WARNING,
						"romdataFileGetNumForNameAnyMod: '%s' resolved to a basename match in "
						"mod %d while %d mod(s) match it exactly - precedence is mod scan "
						"order, not match quality",
						name, firstMod, numExact);
			}
		}
	}

	return MOD_FILEID_MAKE(firstMod, firstId);
}

s32 romdataFileGetNumForNameInMod(const char *name, s32 modNum)
{
	if (!name || !name[0]) {
		return -1;
	}

	if (modNum < 0 || modNum >= 64) {
		return -1;
	}

	size_t searchLen = strlen(name);

	if (ftPoolUsed > 0) {
		s32 id = ftLookupInMod(name, (u32)searchLen + 1, modNum);
		if (id >= 0 && id < ROMDATA_MAX_FILES) {
			return id;
		}

		const char *slash = strrchr(name, '/');
		if (slash) {
			u32 baseLen = (u32)(searchLen - (slash + 1 - name));
			id = ftLookupInMod(slash + 1, baseLen + 1, modNum);
			if (id >= 0 && id < ROMDATA_MAX_FILES) {
				return id;
			}
		}
	}

	// Try to find in external file table first (supports separate name vs path)
	if (externalFileTableData) {
		u8 *data = externalFileTableData;
		u8 *dataEnd = data + externalFileTableSize;
		u32 version = PD_BE32(*(u32*)(data + 4));
		u32 numFiles = PD_BE32(*(u32*)(data + 8));
		u8 *p = data + 12;

		// Skip romSources block (v2+)
		if (version >= 2) {
			if (p + 4 > dataEnd) goto extDone;
			u32 numRomSources = PD_BE32(*(u32*)p); p += 4;
			for (u32 i = 0; i < numRomSources; ++i) {
				if (p + 1 > dataEnd) goto extDone;
				u8 idLen = *p++;
				if (p + idLen > dataEnd) goto extDone;
				p += idLen;
				if (p + 1 > dataEnd) goto extDone;
				u8 fnLen = *p++;
				if (p + fnLen + 4 + 1 + 1 + 2 > dataEnd) goto extDone;
				p += fnLen + 4 + 1 + 1 + 2; // size, flags, fallback, reserved
			}
		}

		for (u32 i = 0; i < numFiles; ++i) {
			if (p + 16 > dataEnd) break;

			u32 id = PD_BE32(*(u32*)p); p += 4;
			u32 flags = PD_BE32(*(u32*)p); p += 4;
			p += 8; // offset, filesize

			u16 nameLen = PD_BE16(*(u16*)p); p += 2;
			char *entryName = (char*)p;
			p += nameLen;

			u16 pathLen = PD_BE16(*(u16*)p); p += 2;
			p += pathLen;

			if (flags & PDFT_F_ALT) {
				if (p + 10 > dataEnd) break;
				p += 10; // altRomIdx(1) + altOffset(4) + altSize(4) + altCompression(1)
			}

			// v4 alias tail. This scanner only needs to STEP OVER it - the hash
			// pass carries the alias as its own entry, and this walk is the
			// fallback for when that missed.
			if (flags & PDFT_F_ALIAS) {
				if (p + 2 > dataEnd) break;
				u16 aliasSkip = PD_BE16(*(u16*)p); p += 2;
				if (p + aliasSkip > dataEnd) break;
				p += aliasSkip;
			}

			// nameLen on the wire includes the trailing null terminator;
			// compare against searchLen + 1 (or just use strcmp since
			// entryName is null-terminated).
			if (nameLen > 0 && entryName[nameLen - 1] == '\0' && !strcmp(entryName, name)) {
				return id;
			}

			// Also try matching basename if the input name has a path
			size_t entryNameStrLen = (nameLen > 0) ? nameLen - 1 : 0;
			if (searchLen > entryNameStrLen && entryNameStrLen > 0 &&
					name[searchLen - entryNameStrLen - 1] == '/' &&
					!strncmp(entryName, name + searchLen - entryNameStrLen, entryNameStrLen)) {
				return id;
			}
		}
	}
extDone:

	for (s32 i = 0; i < ROMDATA_MAX_FILES; ++i) {
		const char *slotName = fileSlots[modNum][i].name;
		if (!slotName) continue;
		if (!strcmp(slotName, name)) {
			return i;
		}
		// Match basename if slot stores a full/prefixed path
		const char *slash = strrchr(slotName, '/');
		if (slash && !strcmp(slash + 1, name)) {
			return i;
		}
	}

	return -1;
}

u8 *romdataSegGetData(const char *segName)
{
	return romdataGetSeg(segName)->data;
}

u8 *romdataSegGetDataEnd(const char *segName)
{
	struct romfile *seg = romdataGetSeg(segName);
	return seg->data + seg->size;
}

u32 romdataSegGetSize(const char *segName)
{
	return romdataGetSeg(segName)->size;
}

s32 romdataSegCount(void)
{
	s32 n = 0;
	while (romSegs[n].name) ++n;
	return n;
}

const char *romdataSegName(s32 index)
{
	if (index < 0) return NULL;
	for (s32 n = 0; romSegs[n].name; ++n) {
		if (n == index) return romSegs[n].name;
	}
	return NULL;
}

u32 romdataFileGetEstimatedSize(const u32 size, const u32 loadtype)
{
#ifdef PLATFORM_64BIT
	switch (loadtype) {
	case LOADTYPE_BG:	 return (u32)(size * 1.1f);
	case LOADTYPE_TILES: return (u32)(size * 1.1f);
	case LOADTYPE_LANG:  return (u32)(size * 1.3f);
	case LOADTYPE_SETUP: return (u32)(size * 1.5f);
	case LOADTYPE_PADS:  return (u32)(size * 1.7f);
	case LOADTYPE_MODEL: return (u32)(size * 1.7f);
	case LOADTYPE_GUN: return (u32)(size * 1.7f);
	default:
		sysLogPrintf(LOG_WARNING, "romdataFileGetEstimatedSize: wrong loadtype %d", loadtype);
	}
#else
	if (loadtype == LOADTYPE_MODEL) {
		return (u32)(size * 1.1f);
	}
#endif
	return size;
}

// DEBUG helper function to check fileSlots integrity
void romdataDebugCheckFileSlot(s32 modNum, s32 fileNum) {
	if (modNum >= 0 && modNum < 64 && fileNum >= 0 && fileNum < ROMDATA_MAX_FILES) {
		if (fileSlots[modNum][fileNum].name) {
			sysLogPrintf(LOG_NOTE, "DEBUG: fileSlots[%d][%d].name = %s (source=%d)",
				modNum, fileNum, fileSlots[modNum][fileNum].name, fileSlots[modNum][fileNum].source);
		} else {
			sysLogPrintf(LOG_NOTE, "DEBUG: fileSlots[%d][%d].name = NULL", modNum, fileNum);
		}
	}
}
