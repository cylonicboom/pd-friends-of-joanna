#ifndef _IN_MOD_H
#define _IN_MOD_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_CONFIG_FNAME "modconfig.txt"

/* Mod-tagged file ids.
 *
 * A file id is either a plain vanilla id, or a mod-owned id carrying its owning
 * mod in the high bits: (modNum << 16) | rawId. The convention was open-coded
 * at a dozen sites with three different spellings of the extraction - masked to
 * 0xff, masked to 0xffff, and unmasked - so use these instead of writing the
 * shift by hand.
 *
 * rawId is 16 bits. modNum fits in 6: g_ModelStates_PerMod / g_ExplosionTypes_PerMod
 * are 64 entries and modSwitch bounds g_ModNum to < 64.
 *
 * Note there is no "is this tagged" test, deliberately: mod 0 is the boot mod,
 * so a zero modNum is a real owner, not an absence.
 */
#define MOD_FILEID_SHIFT    16
#define MOD_FILEID_RAW_MASK 0xffff
#define MOD_FILEID_MOD_MASK 0xff

#define MOD_FILEID_MOD(id)  ((s32)(((s32)(id) >> MOD_FILEID_SHIFT) & MOD_FILEID_MOD_MASK))
#define MOD_FILEID_RAW(id)  ((s32)((s32)(id) & MOD_FILEID_RAW_MASK))
#define MOD_FILEID_MAKE(modNum, rawId) \
	((s32)((((s32)(modNum) & MOD_FILEID_MOD_MASK) << MOD_FILEID_SHIFT) \
		| ((s32)(rawId) & MOD_FILEID_RAW_MASK)))

#define MOD_FLAG_FORCE_LOAD    (1 << 0)
#define MOD_FLAG_FORCE_VANILLA (1 << 1)

extern u8 g_StageModFlags[256];
extern char g_ModNames[64][64];
extern s32 g_TexModNum;
extern s32 g_TexCurrentModelFileNum;  // low 16 bits: fileSlot of the model currently being loaded/rendered; 0 when none

struct animtableentry;

void modInit(void);
s32 modConfigLoad(const char *path);
s32 modLoadAIO(void);
void modScanAllMods(void);
void modCacheAllConfigs(void);

s32 modTextureLoad(u16 num, void *dst, u32 dstSize);
#define MOD_TEXTURE_RESOLVE_MAX_ATTEMPTS 5
struct modTextureResolveAttempt {
	char name[128];
	s32 fileNum;
};
struct modTextureResolveInfo {
	s32 modNum;
	s32 modelFileNum;
	u16 requestedId;
	u16 reverseLocalId;
	u16 resolvedLocalId;
	s32 fileNum;
	s32 matchedAttempt;
	s32 attemptCount;
	struct modTextureResolveAttempt attempts[MOD_TEXTURE_RESOLVE_MAX_ATTEMPTS];
};
s32 modTextureResolveFileDetailed(s32 modNum, s32 modelFileNum, u16 textureId,
		struct modTextureResolveInfo *info);
s32 modTextureResolveFile(s32 modNum, s32 modelFileNum, u16 textureId,
		u16 *resolvedLocalId, char *resolvedName, u32 resolvedNameSize);

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim);
void *modAnimationLoadData(u16 num);

void *modSequenceLoad(u16 num, u32 *outSize);

void modLoadTextureSurfaceType(void);
void modUnloadTextureSurfaceType(void);
void modSwitch(s32 modnum, s32 stagenum);
s32 modLookupHeadByName(const char *name);
s32 modLookupBodyByName(const char *name);
s32 modLookupHandFileByName(const char *name);
s32 modLookupHeadnumByName(const char *name);
const char *modGetNameForHeadBodyIndex(s32 headBodyIndex);

#ifdef __cplusplus
}
#endif

#endif
