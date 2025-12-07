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

#define ROMDATA_MAX_FILES 4096

#define GBC_ROM_NAME "pd.gbc"
#define GBC_ROM_SIZE 4194304

u8 *g_RomFile;
u32 g_RomFileSize;

extern u32 g_NumModDirs;
extern char modDirs[64][FS_MAXPATH + 1];
static u8 *romDataSeg;
static u32 romDataSegSize;
static const char *romName = ROMDATA_ROM_NAME;

enum loadsource {
	SRC_UNLOADED = 0,
	SRC_ROM,
	SRC_EXTERNAL
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

	u8 *data = externalFileTableData;
	if (memcmp(data, "PDFT", 4) != 0) {
		sysLogPrintf(LOG_ERROR, "Invalid file table magic");
		sysMemFree(externalFileTableData);
		externalFileTableData = NULL;
		return 0;
	}

	u32 version = PD_BE32(*(u32*)(data + 4));
	u32 numFiles = PD_BE32(*(u32*)(data + 8));
	u8 *p = data + 12;

	sysLogPrintf(LOG_NOTE, "Loading external file table v%d with %d files", version, numFiles);

	for (u32 i = 0; i < numFiles; ++i) {
		if (p + 16 > data + size) break;

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

		if (id < ROMDATA_MAX_FILES) {
			// Check if path contains export flag and mod constraint
			bool hasExport = false;
			const char *pathAfterDoubleColon = NULL;
			const char *modConstraint = NULL;

			if ((flags & 2) && pathLen > 1) {
				// Look for "export" keyword in path
				if (strstr(path, "export")) {
					hasExport = true;
				}
				// Look for "mod:" prefix to extract owner mod
				const char *modPrefix = strstr(path, "mod:");
				if (modPrefix) {
					modConstraint = modPrefix + 4; // points to start of mod name
				}
				// Find the "::" separator to extract the actual path
				const char *separator = strstr(path, "::");
				if (separator) {
					pathAfterDoubleColon = separator + 2;
				}
			}

			for (s32 mod = 0; mod <= g_NumModDirs; ++mod) {
				if (flags & 1) {
					fileSlots[mod][id].data = g_RomFile + offset;
					fileSlots[mod][id].size = fileSize;
					fileSlots[mod][id].source = SRC_UNLOADED;
				}

				if ((flags & 2) && pathLen > 1) {
					// If file is exported and has a mod constraint
					if (hasExport && modConstraint && pathAfterDoubleColon) {
						// Get current mod's name (basename of mod directory)
						const char *currentModName = NULL;
						if (mod > 0 && mod <= g_NumModDirs && modDirs[mod - 1][0]) {
							currentModName = strrchr(modDirs[mod - 1], '/');
							if (currentModName) {
								currentModName++; // skip the '/'
							} else {
								currentModName = modDirs[mod - 1];
							}
						}

						// Check if this mod is the owner (matches the mod constraint)
						bool isOwner = false;
						if (currentModName) {
							// Check if modConstraint starts with currentModName
							size_t modNameLen = strlen(currentModName);
							if (strncmp(modConstraint, currentModName, modNameLen) == 0) {
								// Make sure it's followed by comma, colon, or end of metadata
								char next = modConstraint[modNameLen];
								if (next == ',' || next == ':') {
									isOwner = true;
								}
							}
						}


						if (isOwner) {
							// Owner mod: use full path with metadata
							fileSlots[mod][id].name = path;
						} else {
							// Non-owner mod: use simplified path (after ::)
							fileSlots[mod][id].name = pathAfterDoubleColon;
						}
					} else {
						// No export or no mod constraint: use path as-is
						fileSlots[mod][id].name = path;
					}
				} else if (nameLen > 1) {
					fileSlots[mod][id].name = name;
				}
			}
		}
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
	if (romdataLoadExternalFileTable()) {
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
	// Check for Boot
	if (g_MainIsBooting) {
		return "BOOT";
	}

	// Check for Intro
	if (g_InCutscene) {
		return "INTRO";
	}

	// Check for CI
	if (g_StageNum == STAGE_CITRAINING) {
		return "CI";
	}

	// Check for 4MB Menu
	if (g_StageNum == STAGE_4MBMENU) {
		return "MB";
	}

	// Check for Combat Simulator (Multiplayer)
	if (g_Vars.normmplayerisrunning) {
		return "CS";
	}

	// Check for Co-op / Counter-Op / Team Missions
	if (g_Vars.mplayerisrunning) {
		if (g_MissionConfig.isteam) {
			return "TEAM";
		}
		if (g_MissionConfig.isanti) {
			return "ANTI";
		}
		return "COOP";
	}

	// Default to Solo
	return "SOLO";
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
		}

		if (fileSlots[modNum][fileNum].source == SRC_UNLOADED) {
			// tried and failed, fall back to ROM
			fileSlots[modNum][fileNum].source = SRC_ROM;
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
	// All Solos in Multi Mod: reset mod files for solo
	romdataResetMod(g_ModNum);
}

const char *romdataFileGetName(s32 fileNum)
{
	if (fileNum < 1 || fileNum >= ROMDATA_MAX_FILES) {
		return NULL;
	}
	return fileSlots[g_ModNum][fileNum].name;
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

s32 romdataFileGetNumForNameInMod(const char *name, s32 modNum)
{
	if (!name || !name[0]) {
		return -1;
	}

	if (modNum < 0 || modNum >= 64) {
		return -1;
	}

	// Try to find in external file table first (supports separate name vs path)
	if (externalFileTableData) {
		u8 *data = externalFileTableData;
		u32 numFiles = PD_BE32(*(u32*)(data + 8));
		u8 *p = data + 12;
		size_t searchLen = strlen(name);

		for (u32 i = 0; i < numFiles; ++i) {
			if (p + 16 > data + externalFileTableSize) break;

			u32 id = PD_BE32(*(u32*)p); p += 4;
			p += 12; // flags, offset, filesize

			u16 nameLen = PD_BE16(*(u16*)p); p += 2;
			char *entryName = (char*)p;
			p += nameLen;

			u16 pathLen = PD_BE16(*(u16*)p); p += 2;
			p += pathLen;

			if (nameLen == searchLen && !strncmp(entryName, name, nameLen)) {
				return id;
			}

			// Also try matching basename if the input name has a path
			if (searchLen > nameLen && name[searchLen - nameLen - 1] == '/' && !strncmp(entryName, name + searchLen - nameLen, nameLen)) {
				return id;
			}
		}
	}

	for (s32 i = 0; i < ROMDATA_MAX_FILES; ++i) {
		if (fileSlots[modNum][i].name && !strcmp(fileSlots[modNum][i].name, name)) {
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
	case LOADTYPE_BG:	   return (u32)(size * 1.1f);
	case LOADTYPE_TILES: return (u32)(size * 1.1f);
	case LOADTYPE_LANG:  return (u32)(size * 1.3f);
	case LOADTYPE_SETUP: return (u32)(size * 1.5f);
	case LOADTYPE_PADS:  return (u32)(size * 1.7f);
	case LOADTYPE_MODEL: return (u32)(size * 1.7f);
	case LOADTYPE_GUN: return (u32)(size * 1.7f);
	default:
		sysLogPrintf(LOG_WARNING, "romdataFileGetEstimatedSize: wrong loadtype %d", loadtype);
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
