#ifndef _IN_MOD_H
#define _IN_MOD_H

#include <PR/ultratypes.h>

#define MOD_CONFIG_FNAME "modconfig.txt"

#define MOD_FLAG_FORCE_LOAD    (1 << 0)
#define MOD_FLAG_FORCE_VANILLA (1 << 1)

extern u8 g_StageModFlags[256];
extern char g_ModNames[64][64];

struct animtableentry;

void modInit(void);
s32 modConfigLoad(const char *path);
s32 modImport(char *modName, char *assetName);
s32 modLoadAIO(void);
void modScanAllMods(void);
void modCacheAllConfigs(void);

s32 modTextureLoad(u16 num, void *dst, u32 dstSize);

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim);
void *modAnimationLoadData(u16 num);

void *modSequenceLoad(u16 num, u32 *outSize);

void modLoadTextureSurfaceType(void);
void modUnloadTextureSurfaceType(void);
void modSwitch(s32 modnum, s32 stagenum);
#endif
