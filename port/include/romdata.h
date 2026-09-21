#ifndef _IN_ROMDATA_H
#define _IN_ROMDATA_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct romdatafileslotinfo {
	const char *name;
	u32 size;
	s32 source;
	s32 configuredSource;
};

extern u8 *g_RomFile;
extern u32 g_RomFileSize;

s32 romdataInit(void);

// Character-model swap overlay ROM (pd.load_model_rom / pd.model_swap), from
// Kai. g_ModelRomActive: an overlay ROM is loaded. g_ModelSwapActive:
// character models are currently sourced from it. g_ModelSwapFiles[rawid] != 0
// flags a vanilla file for redirection. The game side (body.c
// modelSwapSetActive) owns these.
extern s32 g_ModelRomActive;
extern s32 g_ModelSwapActive;
extern u8 g_ModelSwapFiles[];
extern s32 g_ModelSwapRedirects; // # of times the redirect served overlay bytes
extern s32 g_ModelSwapMisses;    // # of times it was armed but couldn't serve
// Overlay texture table/data (built when the overlay ROM loads) and the flag
// that gates per-number texture redirection while a swapped model loads.
struct texture;
extern struct texture *g_ModelSwapTexList;
extern s32 g_ModelSwapTexCount;
extern u8 *g_ModelSwapTexData;
extern u32 g_ModelSwapTexDataSize; // bytes from g_ModelSwapTexData to the end of the file
extern s32 g_ModelSwapTexActive;
#define ROMDATA_MODELSWAP_MAX_FILES 8192 // size of g_ModelSwapFiles (ROMDATA_MAX_FILES)
s32 romdataModelRomFileGetNumForName(const char *name);
s32 romdataLoadModelRom(const char *path); // path = ROM file or dir to scan; 1 = loaded

u8 *romdataFileLoad(s32 fileNum, u32 *outSize);
void romdataFilePreprocess(s32 fileNum, s32 loadType, u8 *data, u32 size, u32 *outSize);
void romdataFileFree(s32 fileNum);
void romdataResetActiveMod(void); // drop every loaded file slot for the active mod
const char *romdataFileGetName(s32 fileNum);

u8 *romdataFileGetData(s32 fileNum);
s32 romdataFileGetSize(s32 fileNum);

s32 romdataFileGetNumForName(const char *name);
s32 romdataFileGetNumForNameAnyMod(const char *name);
s32 romdataFileGetNumForNameInMod(const char *name, s32 modNum);
const char *romdataFileGetSlotName(s32 modNum, s32 fileNum);
s32 romdataGetFileSlotCount(s32 modNum);
s32 romdataGetFileSlotInfo(s32 modNum, s32 fileNum, struct romdatafileslotinfo *outInfo);
u16 modTexMapLookup(s32 modIdx, u16 localTexId);
u16 modTexMapReverseLookup(s32 modIdx, u16 portTexId);
s32 modTexMapGetCount(s32 modIdx);
s32 modTexMapGetEntry(s32 modIdx, s32 index, u16 *localTexId, u16 *portTexId);

// the ROM's own file names, untouched by mod overlays - what "vanilla" means
s32 romdataRomFileCount(void);
const char *romdataRomFileName(s32 fileNum);

u8 *romdataSegGetData(const char *segName);
u8 *romdataSegGetDataEnd(const char *segName);
u32 romdataSegGetSize(const char *segName);
s32 romdataSegCount(void);
const char *romdataSegName(s32 index);
u32 romdataFileGetEstimatedSize(const u32 size, const u32 loadtype);

s32 romdataCheckGbcRom(void);

void fileSlotsInit(u32 numMods);
void romdataResetMod(s32 modNum);
const u8 romDataFileNumExists(s32 modNum, s32 fileNum);
u8 romsourceIsMounted(const char *id);
s32 romsourceCount(void);
s32 romsourceInfo(s32 index, const char **id, const char **filename, u8 *mounted);

#ifdef __cplusplus
}
#endif

#endif
