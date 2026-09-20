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
s32 extTexModelGetTextureInfo(s16 fileNum, s32 index, s32 *texNum, s8 *ownerMod, u16 *width, u16 *height);
const u8 *extTexModelLoadPixels(s16 fileNum, s32 texNum, u32 *width, u32 *height);
s32 extTexModelDir(s16 fileNum, char *dst, u32 len);
u8 extTexFontID(struct font *font);

// Lua API image loading (pd.load_image / pd.tex_override / pd.list_images).
// name is a plain file name looked up in scripts/chaos/images/ under each mod
// dir, the base dir, then the working directory. The buffer is RGBA8888 and
// is freed with extImageFree.
u8 *extImageLoad(const char *name, u32 *width, u32 *height);
void extImageFree(u8 *data);
// PNG basenames (no extension) across those dirs; returns the count written.
s32 extImageList(char out[][64], s32 maxout);

#ifdef __cplusplus
}
#endif

#endif
