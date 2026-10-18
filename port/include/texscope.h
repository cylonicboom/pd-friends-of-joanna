#ifndef _IN_TEXSCOPE_H
#define _IN_TEXSCOPE_H

#include <PR/ultratypes.h>

/* The texture load scope: who owns the bytes being loaded right now, and
 * which model file they belong to.
 *
 * Every path that loads textures - a model through modeldefLoad or the gun
 * loader's staged copy of it, a room's display lists, the bg section-2
 * preload, the inspector - pushes a scope first and pops it on every way
 * out. texDecompress, texFindInPool and modTextureLoad read the top of the
 * stack: it decides which mod's filetable answers for a texture number and
 * which (texturenum, owner) bucket the pool entry lands in.
 *
 * This replaces two globals, g_TexModNum and g_TexCurrentModelFileNum, that
 * were set and restored by hand at fourteen sites and forgotten at the gun
 * loader - which is how an all-in-one hand came to draw vanilla's lava tile.
 * The two names survive as read-only mirrors of the top so the readers did
 * not all have to change in the same commit; nothing outside texscope.c
 * writes them.
 *
 * Outside any scope the owner is -1 (vanilla) and the model file 0. That is
 * a legitimate state for vanilla loads with no model (explosion textures,
 * fonts, texselect); for a texture number past the ROM's table it is a bug,
 * and texScopeReportMissing says so once per number.
 */

struct texscope {
	s32 owner;        /* mod index, or -1 for vanilla / no mod */
	s32 modelFileNum; /* raw file number of the model being loaded, or 0 */
};

/* Owner from a (possibly tagged) file id, else the running stage's claim,
 * else -1; model file is the id's raw number. The rule modeldef used. */
void texScopePush(s32 fileid);
/* Owner only - a room, whose textures have no model file. */
void texScopePushOwner(s32 owner);
/* Both parts spelled out - the inspector. */
void texScopePushPair(s32 owner, s32 modelFileNum);
void texScopePop(void);

s32 texScopeOwner(void);
s32 texScopeModelFileNum(void);
s32 texScopeDepth(void);

/* A load that needed a scope and had none. Logs once per texture number. */
void texScopeReportMissing(u16 texturenum);

#endif
