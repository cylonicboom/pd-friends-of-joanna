#include <ultra64.h>
#include "constants.h"
#include "game/gamefile.h"
#include "game/lang.h"
#include "game/missionrow.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#ifndef PLATFORM_N64
#include "soloprogress.h"
#endif

bool missionIsModRow(s32 stageindex)
{
	return stageindex >= SOLOSTAGEINDEX_MOD_BASE;
}

s32 missionStagenum(s32 stageindex)
{
#ifndef PLATFORM_N64
	if (missionIsModRow(stageindex)) {
		return soloProgressStagenumAt(stageindex - SOLOSTAGEINDEX_MOD_BASE);
	}
#endif
	if (stageindex >= 0 && stageindex < NUM_SOLOSTAGES) {
		return g_SoloStages[stageindex].stagenum;
	}
	return -1;
}

char *missionName3(s32 stageindex)
{
#ifndef PLATFORM_N64
	if (missionIsModRow(stageindex)) {
		return (char *)soloProgressNameAt(stageindex - SOLOSTAGEINDEX_MOD_BASE);
	}
#endif
	if (stageindex >= 0 && stageindex < NUM_SOLOSTAGES) {
		return langGet(g_SoloStages[stageindex].name3);
	}
	return "";
}

s32 missionBestTime(s32 stageindex, s32 difficulty)
{
	if (difficulty < 0 || difficulty >= 3) {
		return 0;
	}
#ifndef PLATFORM_N64
	if (missionIsModRow(stageindex)) {
		s32 t = soloProgressBestTime(soloProgressStagenumAt(stageindex - SOLOSTAGEINDEX_MOD_BASE), difficulty);
		return t < 0 ? 0 : t;
	}
#endif
	if (gamefileSoloIndexOk(stageindex)) {
		return g_GameFile.besttimes[stageindex][difficulty];
	}
	return 0;
}

bool missionCoopDone(s32 stageindex, s32 difficulty)
{
	if (difficulty < 0 || difficulty >= 3) {
		return false;
	}
#ifndef PLATFORM_N64
	if (missionIsModRow(stageindex)) {
		return soloProgressCoopDone(soloProgressStagenumAt(stageindex - SOLOSTAGEINDEX_MOD_BASE), difficulty);
	}
#endif
	if (gamefileSoloIndexOk(stageindex)) {
		return (g_GameFile.coopcompletions[difficulty] & (1 << stageindex)) != 0;
	}
	return false;
}

bool missionSetBestTime(s32 stageindex, s32 difficulty, s32 secs)
{
	if (difficulty < 0 || difficulty >= 3) {
		return false;
	}
#ifndef PLATFORM_N64
	if (missionIsModRow(stageindex)) {
		return soloProgressSetBestTime(soloProgressStagenumAt(stageindex - SOLOSTAGEINDEX_MOD_BASE), difficulty, secs);
	}
#endif
	if (gamefileSoloIndexOk(stageindex)) {
		g_GameFile.besttimes[stageindex][difficulty] = secs;
		return true;
	}
	return false;
}

bool missionSetCoopDone(s32 stageindex, s32 difficulty)
{
	if (difficulty < 0 || difficulty >= 3) {
		return false;
	}
#ifndef PLATFORM_N64
	if (missionIsModRow(stageindex)) {
		return soloProgressSetCoopDone(soloProgressStagenumAt(stageindex - SOLOSTAGEINDEX_MOD_BASE), difficulty);
	}
#endif
	if (gamefileSoloIndexOk(stageindex)) {
		g_GameFile.coopcompletions[difficulty] |= (1 << stageindex);
		return true;
	}
	return false;
}
