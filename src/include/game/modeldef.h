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

struct modeldefTextureTriangle {
	u32 nodeoffset;
	u32 commandindex;
	u16 textureid;
	u8 listtype;
	u8 vertexindex[3];
	s16 s[3];
	s16 t[3];
};

struct modeldefEditorWorkspaceInfo {
	s32 fileid;
	struct modeldef *modeldef;
	u32 modelcapacity;
	u32 modelloadedsize;
	u32 texturecapacity;
	u32 texturebytesused;
};

bool modeldefEditorWorkspaceLoad(s32 fileid, u32 texturecapacity);
void modeldefEditorWorkspaceUnload(void);
bool modeldefEditorWorkspaceGetInfo(struct modeldefEditorWorkspaceInfo *info);
struct tex *modeldefEditorWorkspaceFindTexture(u16 textureid);

s32 modeldefInspectTextureUsage(s32 fileid, u16 textureid1, u16 textureid2,
		struct modeldefTextureUsage *entries, s32 maxentries, s32 *totalmatches,
		struct modeldefTextureTriangle *triangles, s32 maxtriangles,
		s32 *capturedtriangles, s32 *totaltriangles);
#endif

#ifdef __cplusplus
}
#endif

#endif
