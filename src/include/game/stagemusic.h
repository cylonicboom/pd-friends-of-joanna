#ifndef IN_GAME_STAGEMUSIC_H
#define IN_GAME_STAGEMUSIC_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

s32 stageGetPrimaryTrack(s32 stagenum);
s32 stageGetAmbientTrack(s32 stagenum);
s32 stageGetNrgTrack(s32 stagenum);

#ifndef PLATFORM_N64
/* A modconfig's `music { }` for a stage. Consulted before g_StageTracks, so it
 * works on a row that has no entry there - which is every STAGE_EXTRA row and
 * therefore every level the loader placed. -1 for a track means what it means
 * in the table: primary -1 = mpChooseTrack, ambient/x -1 = none. */
void stageSetModTracks(s32 stagenum, s32 primary, s32 ambient, s32 x);
bool stageGetModTracks(s32 stagenum, s32 *primary, s32 *ambient, s32 *x);
#endif

#endif
