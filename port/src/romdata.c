#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <PR/ultratypes.h>
#include "lib/rzip.h"
#include "romdata.h"
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
	u8   flags;       // bit0=required, bit1=strict
	u8   fallback;    // 0=skip, 1=vanilla, 2=error
	u8   mounted;
};

struct romaltsource {
	u8  romIdx;       // 0xff = none
	u8  compression;  // 0=raw, 1=rzip(1173)
	u32 offset;
	u32 size;
};

// MOD_TEX_MAP_MAX_MODS bounds both the per-mod texture map and the
// per-mod altSource array. modIdx 0 is the base/global table; 1..64
// correspond to --moddir entries (FS_MAXMODDIRS = 64).
#define MOD_TEX_MAP_MAX_MODS 65

static struct romsource g_RomSources[ROMSOURCES_MAX];
static u32 g_NumRomSources;
// Per-mod altSource: [modIdx][localFileId]. modIdx 0 = base/global table.
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
// Base sits above the highest known vanilla texture count across all PD ROM
// variants (JPN has 3511 textures, so anything <3512 collides with vanilla
// texids that appear in JPN-sourced models). Cap fits the 12-bit texnum
// field in G_NOOP (max 0xfff = 4095).
#define MOD_TEX_PORT_BASE 3600u
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

static void romSourcesMount(void)
{
	for (u32 i = 0; i < g_NumRomSources; i++) {
		struct romsource *rs = &g_RomSources[i];
		if (rs->mounted || !rs->filename[0]) continue;

		rs->data = fsFileLoad(rs->filename, &rs->size);
		if (!rs->data) {
			char tmp[FS_MAXPATH];
			snprintf(tmp, sizeof(tmp), "$B/roms/%s", rs->filename);
			rs->data = fsFileLoad(tmp, &rs->size);
		}
		if (!rs->data) {
			char tmp[FS_MAXPATH];
			snprintf(tmp, sizeof(tmp), "$B/%s", rs->filename);
			rs->data = fsFileLoad(tmp, &rs->size);
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

static struct romfile fileSlots[64][ROMDATA_MAX_FILES];
void fileSlotsInit(u32 numMods) {
	for (s32 i = 0; i < numMods - 1; ++i) {
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

	g_RomFile = fsFileLoad(romName, &g_RomFileSize);

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

			u16 pathLen = PD_BE16(*(u16*)p); p += 2;
			char *path = (char*)p;
			p += pathLen;

			u8  altRomIdx = 0xff;
			u32 altOffset = 0;
			u32 altSize = 0;
			u8  altCompression = 0;
			if (flags & 4) {
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

			if (isGlobal && (flags & 2) && pathLen > 1) {
				if (strstr(path, "export")) hasExport = true;
				const char *modPrefix = strstr(path, "mod:");
				if (modPrefix) modConstraint = modPrefix + 4;
				const char *separator = strstr(path, "::");
				if (separator) pathAfterDoubleColon = separator + 2;
			}

			s32 modLo = isGlobal ? 0 : ownerModIdx;
			s32 modHi = isGlobal ? (s32)g_NumModDirs : ownerModIdx;

			for (s32 mod = modLo; mod <= modHi; ++mod) {
				if (flags & 1) {
					fileSlots[mod][id].data = g_RomFile + offset;
					fileSlots[mod][id].size = fileSize;
					fileSlots[mod][id].source = SRC_UNLOADED;
				}

				if ((flags & 2) && pathLen > 1) {
					if (isGlobal && hasExport && modConstraint && pathAfterDoubleColon) {
						const char *currentModName = NULL;
						if (mod > 0 && mod <= (s32)g_NumModDirs && modDirs[mod - 1][0]) {
							currentModName = strrchr(modDirs[mod - 1], '/');
							if (currentModName) currentModName++;
							else currentModName = modDirs[mod - 1];
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

			PDFT("entry table=%s mod=%d index=%u id=0x%04x flags=0x%08x name='%.*s' path='%.*s' romOffset=0x%x romSize=%u",
			     isGlobal ? "global" : "fragment", ownerModIdx, i, id, flags,
			     nameLen > 0 ? nameLen - 1 : 0, name,
			     pathLen > 0 ? pathLen - 1 : 0, path, offset, fileSize);

			if (id >= ROMDATA_MAX_FILES) continue;

			s32 modLo = isGlobal ? 0 : ownerModIdx;
			s32 modHi = isGlobal ? (s32)g_NumModDirs : ownerModIdx;
			for (s32 mod = modLo; mod <= modHi; ++mod) {
				if (flags & 1) {
					fileSlots[mod][id].data = g_RomFile + offset;
					fileSlots[mod][id].size = fileSize;
					fileSlots[mod][id].source = SRC_UNLOADED;
				}
				if ((flags & 2) && pathLen > 1) {
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
			for (u32 i = 0; i < numTexMap; ++i) {
				u16 localId = PD_BE16(*(u16*)p); p += 2;
				u16 slotIdx = PD_BE16(*(u16*)p); p += 2;
				m->entries[i].localTexId = localId;
				m->entries[i].portTexId  = (u16)(modBase + slotIdx);
			}
			g_NextGlobalTexPort += numTexMap;
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
		PDFT("texmap table=fragment mod=%d entries=%u portBase=0x%03x nextPort=0x%03x",
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
			if (offsets + i + 1 < (u32 *)(romDataSeg + romDataSegSize)) {
				const u32 nextofs = PD_BE32(offsets[i + 1]);
				const u32 ofs = PD_BE32(offsets[i]);
				int mod;
				for (mod = 0; mod <= g_NumModDirs; ++mod) {
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
			for (s32 mod = 0; mod <= g_NumModDirs; ++mod) {
				fileSlots[mod][i].name = (const char *)nameOffsets + ofs; // ofs is relative to the start of the name table
			}
		}

		for (i = 1; i < (u32)(sizeof(fileSlots[0]) / sizeof(fileSlots[0][0])); ++i) {
			for (s32 mod = 1; mod < (g_NumModDirs - 1); ++mod) {
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
	if (romDataSeg) {
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
				if (flags & 1) {
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

	// set segments to point to the rom or load them externally
	for (struct romfile *seg = romSegs; seg->name; ++seg) {
		romdataInitSegment(seg);
	}

	// load file table from the files segment
	romdataInitFiles();

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

static void romdataResolvePath(char *dst, const char *src, size_t dstSize, const char *currentModName, const char *activeModName, bool requireExport)
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
				} else if (hasMod && !hasExport && activeModName && currentModName && strcmp(currentModName, activeModName) != 0) {
					// Private asset (not exported), and we are not the owner
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
	if (fileNum & 0xFFFF0000) {
		modNum = (fileNum >> 16) & 0xFF;
		fileNum = fileNum & 0xFFFF;
	}

	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "romdataFileGetSize: invalid file num %d", fileNum);
		return -1;
	}

	// ensure any external files are loaded and we use their size
	if (romdataFileLoad(fileNum | (modNum << 16), NULL)) {
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

u8 *romdataFileLoad(s32 fileNum, u32 *outSize)
{
	s32 modNum = g_ModNum;
	if (fileNum & 0xFFFF0000) {
		modNum = (fileNum >> 16) & 0xFF;
		fileNum = fileNum & 0xFFFF;
	}

	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		sysLogPrintf(LOG_ERROR, "romdataFileLoad: invalid file num %d", fileNum);
		return NULL;
	}

	u8 *out = NULL;

	// try to load external file
	if (fileSlots[modNum][fileNum].source == SRC_UNLOADED) {
		char tmp[FS_MAXPATH] = { 0 };
		char resolvedName[FS_MAXPATH];

		// All Solos in Multi Mod: do not load in solo, coop, counter-op
		bool allowMod = !g_NotLoadMod;
		if (g_StageNum >= 0 && g_StageNum < 256) {
			if (g_StageModFlags[g_StageNum] & MOD_FLAG_FORCE_LOAD) {
				allowMod = true;
			} else if (g_StageModFlags[g_StageNum] & MOD_FLAG_FORCE_VANILLA) {
				allowMod = false;
			}
		}


		// Always allow setup files to be modded
		const char *fileName = fileSlots[modNum][fileNum].name;
		if (fileName && strstr(fileName, "setup")) {
			allowMod = true;
		}

		u32 loadedSize = 0;

		// Get active mod name
		const char *activeModName = NULL;
		if (g_ModNum >= 0 && g_ModNum < g_NumModDirs && modDirs[g_ModNum][0]) {
			activeModName = strrchr(modDirs[g_ModNum], '/');
			if (activeModName) activeModName++;
			else activeModName = modDirs[g_ModNum];
		}

		bool requireExport = !allowMod;

		// 1. Try All Mods (Reverse Order)
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
				romdataResolvePath(resolvedName, fileSlots[modNum][fileNum].name, sizeof(resolvedName), modName, activeModName, requireExport);

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
						break;
					} else {
						sysLogPrintf(LOG_WARNING, "file %d (%s) corrupted in mod %d, skipping", fileNum, resolvedName, i);
						if (out) { sysMemFree(out); out = NULL; }
					}
				}
			}
		}

		// 2. Try Base Dir (if not found in mod or corrupted)
		if (!out) {
			// Resolve generic path (no mod constraint)
			romdataResolvePath(resolvedName, fileSlots[modNum][fileNum].name, sizeof(resolvedName), NULL, activeModName, requireExport);

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
		DEBUG_FLOAD("romdataFileLoad: file %d (%s) loaded EXTERNALLY (size=%u, context=%s, allowMod=%d, g_NotLoadMod=%d)",
			fileNum, fileSlots[modNum][fileNum].name, loadedSize, romdataGetContextPrefix(), !g_NotLoadMod, g_NotLoadMod);
	}

	// Try alternate-ROM data source if no loose file was found.
	// Mod files baked into a custom z64 (e.g. gex.z64) are pointed at
	// by g_FileAltSource[modIdx][localFileId] populated during PDFT v2/v3
	// fragment parse. modNum 0 is the global/base table.
	if (!out && fileNum >= 0 && fileNum < ROMDATA_MAX_FILES) {
		s32 modSlot = (modNum >= 0 && modNum < MOD_TEX_MAP_MAX_MODS) ? modNum : 0;
		struct romaltsource *as = &g_FileAltSource[modSlot][fileNum];
		const char *asScope = "perMod";
		if (as->romIdx == 0xff) {
			as = &g_FileAltSource[0][fileNum];
			asScope = "global";
		}
		// sysLogPrintf(LOG_NOTE, "altRom lookup: modNum=%d fileNum=0x%x scope=%s romIdx=%u offset=0x%x size=%u comp=%u numRomSources=%u",
		// 	modNum, fileNum, asScope, as->romIdx, as->offset, as->size, as->compression, g_NumRomSources);
		if (as->romIdx != 0xff && as->romIdx < g_NumRomSources) {
			struct romsource *rs = &g_RomSources[as->romIdx];
			// sysLogPrintf(LOG_NOTE, "altRom rs: id=%s mounted=%d data=%p size=%u",
			// 	rs->id, rs->mounted, rs->data, rs->size);
			if (rs->mounted && rs->data
			    && (u64)as->offset + (u64)as->size <= (u64)rs->size) {
				if (as->compression == 0) {
					fileSlots[modNum][fileNum].data = rs->data + as->offset;
					fileSlots[modNum][fileNum].size = as->size;
					fileSlots[modNum][fileNum].source = SRC_ALT_ROM;
					fileSlots[modNum][fileNum].numpatches = 0;
					out = fileSlots[modNum][fileNum].data;
					// sysLogPrintf(LOG_NOTE, "romdataFileLoad: file %d (%s) loaded from altRom '%s' at 0x%x (size=%u)",
					// 	fileNum, fileSlots[modNum][fileNum].name, rs->id, as->offset, as->size);
				} else {
					sysLogPrintf(LOG_WARNING,
						"romdataFileLoad: file %d altRom compression=%u not implemented",
						fileNum, as->compression);
				}
			}
		}
	}

	if (fileSlots[modNum][fileNum].source == SRC_UNLOADED) {
		// tried and failed, fall back to ROM
		fileSlots[modNum][fileNum].source = SRC_ROM;
		DEBUG_FLOAD("romdataFileLoad: file %d (%s) FALLBACK TO ROM (context=%s, allowMod=%d, g_NotLoadMod=%d)",
			fileNum, fileSlots[modNum][fileNum].name, romdataGetContextPrefix(), !g_NotLoadMod, g_NotLoadMod);
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
	if (fileNum & 0xFFFF0000) {
		modNum = (fileNum >> 16) & 0xFF;
		fileNum = fileNum & 0xFFFF;
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
	if (fileNum & 0xFFFF0000) {
		modNum = (fileNum >> 16) & 0xFF;
		fileNum = fileNum & 0xFFFF;
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

void romdataFileFreeForSolo(void)
{
	DEBUG_FLOAD("romdataFileFreeForSolo: Resetting files for mod %d (g_StageNum=0x%02x, restartlevel=%d)",
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

s32 romdataFileGetNumForNameAnyMod(const char *name)
{
	// printf("romdataFileGetNumForNameAnyMod: begin");
	if (!name || !name[0]) {
		// printf("romdataFileGetNumForNameAnyMod: %s != %s, ret -1", name, name[0]);
		return -1;
	}

	for (s32 mod = 0; mod <= (s32)g_NumModDirs; ++mod) {
		for (s32 i = 0; i < ROMDATA_MAX_FILES; ++i) {
			if (fileSlots[mod][i].name) {
				// printf("romdataFileGetNumForNameAnyMod: checking %s (%x) in mod %x\n", fileSlots[mod][i].name, i, mod);
				// Exact match
				if (!strcmp(fileSlots[mod][i].name, name)) {
					// printf("romdataFileGetNumForNameAnyMod: found %s (%x) in mod %s\n", fileSlots[mod][i].name, i, mod);
					return i;
				}
				// Also match against basename for mod files
				// (stored as "mod:modname::files/Filename")
				const char *slash = strrchr(fileSlots[mod][i].name, '/');
				// if (slash)
				// 	printf("romdataFileGetNumForNameAnyMod: slash %s, slash+1 %s, name %s", slash, slash+1, name);
				if (slash && !strcmp(slash + 1, name)) {
					return i;
				}
			}
		}
	}

	return -1;
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

			if (flags & 4) {
				if (p + 10 > dataEnd) break;
				p += 10; // altRomIdx(1) + altOffset(4) + altSize(4) + altCompression(1)
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
