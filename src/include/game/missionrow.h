#ifndef _IN_GAME_MISSIONROW_H
#define _IN_GAME_MISSIONROW_H

#include "types.h"

/*
 * What a mission's list position stands for.
 *
 * g_MissionConfig.stageindex indexes g_SoloStages and the save's besttimes /
 * coopcompletions for the 21 campaign rows. A mod mission has no row in either:
 * its index is SOLOSTAGEINDEX_MOD_BASE + n, and these answer for it from the
 * sidecar (port/src/soloprogress.c). Every reader of stageindex that used to
 * index g_SoloStages or the save directly goes through here instead.
 */
bool missionIsModRow(s32 stageindex);
// The level this mission loads.
s32 missionStagenum(s32 stageindex);
// "dataDyne Defection" for a campaign row; the mod mission's display name.
char *missionName3(s32 stageindex);
// Best time in seconds (0 = not done) and the coop pip, from the save or the
// sidecar as the row dictates. Out-of-range difficulties read 0 / false.
s32 missionBestTime(s32 stageindex, s32 difficulty);
bool missionCoopDone(s32 stageindex, s32 difficulty);
// Record a completion where the row keeps its progress. Returns false when
// nothing could hold it (a mod row with no sidecar entry this session).
bool missionSetBestTime(s32 stageindex, s32 difficulty, s32 secs);
bool missionSetCoopDone(s32 stageindex, s32 difficulty);

#endif
