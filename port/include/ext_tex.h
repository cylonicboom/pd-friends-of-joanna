#ifndef _IN_EXT_TEX_H
#define _IN_EXT_TEX_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MASK_FONT_OUTLINE 0x80

s32 extTexInit();
void extTexFree();
u8 *extTexLoad(u8 type, u16 id, s32 texnum, u32 *width, u32 *height);
u8 extTexExists(u8 type, u16 id, s32 texnum);
s8 extTexGetOwnerMod(u8 type, u16 id, s32 texnum);
u8 extTexGetDimensions(u8 type, u16 id, s32 texnum, u16 *width, u16 *height);

// True if the model identified by `fileNum` (low 16 bits of an encoded fileid)
// has a per-model ext_tex entry (PNG override) at `texNum`.
bool extTexModelHasEntryForTexid(s16 fileNum, s32 texNum);
s32 extTexModelGetTextureCount(s16 fileNum);
u8 extTexFontID(struct font *font);

#ifdef __cplusplus
}
#endif

#endif
