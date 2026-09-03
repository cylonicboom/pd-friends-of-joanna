#ifndef IN_GAME_MODELDEF_H
#define IN_GAME_MODELDEF_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void modeldef0f1a7560(struct modeldef *modeldef, s32 filenum, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5);
void modelPromoteTypeToPointer(struct modeldef *modeldef);
struct modeldef *modeldefLoad(s32 fileid, u8 *arg1, s32 arg2, struct texpool *arg3);
struct modeldef *modeldefLoadToNew(s32 fileid);
struct modeldef *modeldefLoadToAddr(s32 fileid, u8 *dst, s32 size);

#ifndef PLATFORM_N64
struct modeldefTextureUsage {
	u32 nodeoffset;
	u16 nodetype;
	u8 listtype;
	u8 textureslot;
	u32 commandindex;
	u16 textureid;
	s16 numvertices;
	s16 minS;
	s16 maxS;
	s16 minT;
	s16 maxT;
};

s32 modeldefInspectTextureUsage(s32 fileid, u16 textureid1, u16 textureid2,
		struct modeldefTextureUsage *entries, s32 maxentries, s32 *totalmatches);
#endif

#ifdef __cplusplus
}
#endif

#endif
