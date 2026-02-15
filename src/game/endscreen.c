#include <ultra64.h>
#include "constants.h"
#include "game/bossfile.h"
#include "game/cheats.h"
#include "game/game_006900.h"
#include "game/title.h"
#include "game/pdmode.h"
#include "game/objectives.h"
#include "game/bondgun.h"
#include "game/debug.h"
#include "game/game_0b0fd0.h"
#include "game/playermgr.h"
#include "game/player.h"
#include "game/savebuffer.h"
#include "game/menugfx.h"
#include "game/menu.h"
#include "game/mainmenu.h"
#include "game/filemgr.h"
#include "game/endscreen.h"
#include "game/stagetable.h"
#include "game/lv.h"
#include "game/mplayer/ingame.h"
#include "game/challenge.h"
#include "game/gamefile.h"
#include "game/lang.h"
#include "game/options.h"
#include "game/mpstats.h"
#include "game/mplayer/mplayer.h"
#include "bss.h"
#include "lib/vi.h"
#include "lib/main.h"
#include "lib/str.h"
#include "data.h"
#include "types.h"

#define DEBUG_ENDSCREEN(fmt, ...) \
	do { if (g_DebugEndscreen) printf(fmt, ##__VA_ARGS__); } while (0)

// Store the last completed mission before transitioning to credits
static u8 g_LastCompletedMission = STAGE_CITRAINING;

u8 endscreenGetLastCompletedMission(void)
{
	return g_LastCompletedMission;
}

MenuItemHandlerResult endscreenHandleDeclineMission(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		menuPopDialog();
		menuPopDialog();
		mpSetPaused(MPPAUSEMODE_UNPAUSED);

		// Reset mission config for team/MP missions
		if (g_MissionConfig.isteam) {
			g_Vars.mplayerisrunning = false;
			g_MissionConfig.iscoop = false;
			g_MissionConfig.isanti = false;
			g_MissionConfig.isteam = false;
			g_MissionConfig.pdmode = false;
			g_Vars.normmplayerisrunning = false;
			g_Vars.lvmpbotlevel = 0;

			if (g_BossFile.locktype == MPLOCKTYPE_CHALLENGE) {
				g_BossFile.locktype = MPLOCKTYPE_NONE;
			}
		}

		// Return to title screen
		if (IS8MB()) {
			titleSetNextStage(STAGE_CITRAINING);
			setNumPlayers(1);
			titleSetNextMode(TITLEMODE_SKIP);
			mainChangeToStage(STAGE_CITRAINING);
		} else {
			titleSetNextStage(STAGE_4MBMENU);
			setNumPlayers(1);
			titleSetNextMode(TITLEMODE_SKIP);
			mainChangeToStage(STAGE_4MBMENU);
		}
	}

	return 0;
}

MenuDialogHandlerResult endscreenHandleRetryMission(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_TICK:
		{
			/**
			 * NTSC Final adds this check to make sure the given dialog is
			 * either the one being displayed or its sibling. This most likely
			 * fixes a bug - perhaps there is some way that this handler is run
			 * when the dialog is not on screen?
			 */
#if VERSION >= VERSION_NTSC_FINAL
			if (g_Menus[g_MpPlayerNum].curdialog) {
				if (dialogdef == g_Menus[g_MpPlayerNum].curdialog->definition
						|| (dialogdef->nextsibling && dialogdef->nextsibling == g_Menus[g_MpPlayerNum].curdialog->definition)) {
#endif
					struct menuinputs *inputs = data->dialog2.inputs;
					bool accept = false;

					if (inputs->back) {
						menuPopDialog();
						menuPopDialog();

						if (g_MissionConfig.isteam) {
							mpSetPaused(MPPAUSEMODE_UNPAUSED);
							g_Vars.mplayerisrunning = false;
							g_MissionConfig.iscoop = false;
							g_MissionConfig.isanti = false;
							g_MissionConfig.isteam = false;
							g_MissionConfig.pdmode = false;
							g_Vars.normmplayerisrunning = false;
							g_Vars.lvmpbotlevel = 0;

							if (g_BossFile.locktype == MPLOCKTYPE_CHALLENGE) {
								g_BossFile.locktype = MPLOCKTYPE_NONE;
							}

							if (IS8MB()) {
								titleSetNextStage(STAGE_CITRAINING);
								setNumPlayers(1);
								titleSetNextMode(TITLEMODE_SKIP);
								mainChangeToStage(STAGE_CITRAINING);
							} else {
								titleSetNextStage(STAGE_4MBMENU);
								setNumPlayers(1);
								titleSetNextMode(TITLEMODE_SKIP);
								mainChangeToStage(STAGE_4MBMENU);
							}

						}
					}

					inputs->back = false;

					if (inputs->start) {
						accept = true;
					}

					inputs->start = false;

					if (inputs->select
							&& g_Menus[g_MpPlayerNum].curdialog
							&& dialogdef->nextsibling
							&& dialogdef->nextsibling == g_Menus[g_MpPlayerNum].curdialog->definition) {
						accept = true;
						inputs->select = false;
					}

					if (accept) {
						union handlerdata data2;
						menuhandlerAcceptMission(MENUOP_SET, &dialogdef->items[1], &data2);
					}
#if VERSION >= VERSION_NTSC_FINAL
				}
			}
#endif
		}
	}

	return menudialog00103608(operation, dialogdef, data);
}

char *endscreenMenuTitleRetryMission(struct menudialogdef *dialogdef)
{
	char *name;
	char *prefix;

	if (g_Menus[g_MpPlayerNum].curdialog->definition != dialogdef) {
		return langGet(L_OPTIONS_300); // "Objectives"
	}

	prefix = langGet(L_OPTIONS_296); // "Retry"
	name = langGet(g_SoloStages[g_MissionConfig.stageindex].name3);

	sprintf(g_StringPointer, "%s: %s\n", prefix, name);

	return g_StringPointer;
}

char *endscreenMenuTitleNextMission(struct menudialogdef *dialogdef)
{
	char *name;
	char *prefix;

	if (g_Menus[g_MpPlayerNum].curdialog->definition != dialogdef) {
		return langGet(L_OPTIONS_300); // "Objectives"
	}

	prefix = langGet(L_OPTIONS_297); // "Next Mission"
	name = langGet(g_SoloStages[g_MissionConfig.stageindex].name3);

	sprintf(g_StringPointer, "%s: %s\n", prefix, name);

	return g_StringPointer;
}

MenuItemHandlerResult endscreenHandleReplayPreviousMission(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
#ifndef PLATFORM_N64
		if (getenv("PD_DEBUG_FILELOAD")) {
			printf("endscreenHandleReplayPreviousMission: BEFORE decrement - stageindex=%d, g_MissionConfig.stagenum=0x%02x, g_Vars.stagenum=0x%02x\n", 
				g_MissionConfig.stageindex, g_MissionConfig.stagenum, g_Vars.stagenum);
		}
#endif
		g_MissionConfig.stageindex--;
		g_MissionConfig.stagenum = g_SoloStages[g_MissionConfig.stageindex].stagenum;
#ifndef PLATFORM_N64
		if (getenv("PD_DEBUG_FILELOAD")) {
			printf("endscreenHandleReplayPreviousMission: AFTER decrement - stageindex=%d, g_MissionConfig.stagenum=0x%02x, g_Vars.stagenum=0x%02x\n",
				g_MissionConfig.stageindex, g_MissionConfig.stagenum, g_Vars.stagenum);
		}
#endif
	}

	return menuhandlerAcceptMission(operation, NULL, data);
}

struct menuitem g_RetryMissionMenuItems[] = {
	{
		MENUITEMTYPE_OBJECTIVES,
		1,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		0,
		L_OPTIONS_298, // "Accept"
		0,
		menuhandlerAcceptMission,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		0,
		L_OPTIONS_299, // "Decline"
		0,
		endscreenHandleDeclineMission,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_RetryMissionMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)&endscreenMenuTitleRetryMission,
	g_RetryMissionMenuItems,
	endscreenHandleRetryMission,
	MENUDIALOGFLAG_STARTSELECTS | MENUDIALOGFLAG_DISABLEITEMSCROLL,
	&g_PreAndPostMissionBriefingMenuDialog,
};

struct menuitem g_NextMissionMenuItems[] = {
	{
		MENUITEMTYPE_OBJECTIVES,
		1,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		0,
		L_OPTIONS_298, // "Accept"
		0,
		menuhandlerAcceptMission,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		0,
		L_OPTIONS_299, // "Decline"
		0,
		endscreenHandleDeclineMission,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		0,
		L_MISC_470, // "Replay Previous Mission"
		0,
		endscreenHandleReplayPreviousMission,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_NextMissionMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)&endscreenMenuTitleNextMission,
	g_NextMissionMenuItems,
	endscreenHandleRetryMission,
	MENUDIALOGFLAG_STARTSELECTS | MENUDIALOGFLAG_DISABLEITEMSCROLL,
	&g_PreAndPostMissionBriefingMenuDialog,
};

char *endscreenMenuTextNumKills(struct menuitem *item)
{
	sprintf(g_StringPointer, "%d", mpstatsGetPlayerKillCount());
	return g_StringPointer;
}

char *endscreenMenuTextNumShots(struct menuitem *item)
{
	sprintf(g_StringPointer, "%d", mpstatsGetPlayerShotCountByRegion(SHOTREGION_TOTAL));
	return g_StringPointer;
}

char *endscreenMenuTextNumHeadShots(struct menuitem *item)
{
	sprintf(g_StringPointer, "%d", mpstatsGetPlayerShotCountByRegion(SHOTREGION_HEAD));
	return g_StringPointer;
}

char *endscreenMenuTextNumBodyShots(struct menuitem *item)
{
	sprintf(g_StringPointer, "%d", mpstatsGetPlayerShotCountByRegion(SHOTREGION_BODY));
	return g_StringPointer;
}

char *endscreenMenuTextNumLimbShots(struct menuitem *item)
{
	sprintf(g_StringPointer, "%d", mpstatsGetPlayerShotCountByRegion(SHOTREGION_LIMB));
	return g_StringPointer;
}

char *endscreenMenuTextNumOtherShots(struct menuitem *item)
{
	u32 total = mpstatsGetPlayerShotCountByRegion(SHOTREGION_GUN) + mpstatsGetPlayerShotCountByRegion(SHOTREGION_HAT);
	sprintf(g_StringPointer, "%d", total);
	return g_StringPointer;
}

char *endscreenMenuTextAccuracy(struct menuitem *item)
{
	s32 total = mpstatsGetPlayerShotCountByRegion(SHOTREGION_TOTAL);
	s32 numhead = mpstatsGetPlayerShotCountByRegion(SHOTREGION_HEAD);
	s32 numbody = mpstatsGetPlayerShotCountByRegion(SHOTREGION_BODY);
	s32 numlimb = mpstatsGetPlayerShotCountByRegion(SHOTREGION_LIMB);
	s32 numgun = mpstatsGetPlayerShotCountByRegion(SHOTREGION_GUN);
	s32 numhat = mpstatsGetPlayerShotCountByRegion(SHOTREGION_HAT);
	s32 numobject = mpstatsGetPlayerShotCountByRegion(SHOTREGION_OBJECT);
	f32 accuracy;

	if (total > 0) {
		s32 hits = numhead + numbody + numlimb + numgun + numhat + numobject;
		accuracy = hits * 100.0f / total;
	} else {
		accuracy = 0;
	}

	if (accuracy > 100.0f) {
		accuracy = 100.0f;
	}

	sprintf(g_StringPointer, "%s%s%.1f%%", "", "", accuracy);
	return g_StringPointer;
}

char *endscreenMenuTextMissionStatus(struct menuitem *item)
{
	if (g_CheatsActiveBank0 || g_CheatsActiveBank1) {
		return langGet(L_MPWEAPONS_135); // "Cheated"
	}

	bool coopaborted = false;
	bool antiaborted = false;

	// HACK: need to better handle coop + bond dying
	// for now we'll just roll with the existing
	// behavior and fix it later when I
	// implement the respawn logic
	bool coopisdead = false;

	// avoid wierd flickering of the status text
	// during counter + co op endscreen scenarios
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.coopplayers[i] && g_Vars.coopplayers[i]->aborted) {
			coopaborted = true;
		}

		if (g_Vars.coopplayers[i] && g_Vars.coopplayers[i]->isdead) {
			coopisdead = true;
		}

		if (g_Vars.antiplayers[i] && g_Vars.antiplayers[i]->aborted) {
			antiaborted = true;
		}
	}

	if (g_Vars.coopplayers[g_Vars.currentplayernum] || g_Vars.currentplayernum == g_Vars.bondplayernum) {
		if (g_Vars.bond->aborted || coopaborted || antiaborted) {
			return langGet(L_OPTIONS_295); // "Aborted"
		}

		if (g_Vars.bond->isdead && coopisdead) {
			return langGet(L_OPTIONS_293); // "Failed"
		}
	} else if (g_Vars.antiplayers[g_Vars.currentplayernum]) {
		if (g_Vars.bond->aborted || antiaborted || coopaborted) {
			return langGet(L_OPTIONS_295); // "Aborted"
		}

		if (g_Vars.bond->isdead) {
			return langGet(L_OPTIONS_293); // "Failed"
		}

		if (!g_Vars.bond->aborted && !g_Vars.bond->isdead) {
			return langGet(L_OPTIONS_293); // "Failed"
		}
	} else {
		if (g_Vars.bond->aborted) {
			return langGet(L_OPTIONS_295); // "Aborted"
		}

		if (g_Vars.bond->isdead) {
			return langGet(L_OPTIONS_293); // "Failed"
		}
	}

	if (objectiveIsAllComplete() == false) {
		return langGet(L_OPTIONS_293); // "Failed"
	}

	if (g_StageIndex == STAGEINDEX_DEFENSE) {
		return langGet(L_MPWEAPONS_062); // "Unknown"
	}

	return langGet(L_OPTIONS_294); // "Completed"
}

char *endscreenMenuTextAgentStatus(struct menuitem *item)
{
	if (g_CheatsActiveBank0 || g_CheatsActiveBank1) {
		return langGet(L_MPWEAPONS_134); // "Dishonored"
	}

	if (g_Vars.currentplayer->aborted) {
		return langGet(L_OPTIONS_292); // "Disavowed"
	}

	if (g_Vars.currentplayer->isdead) {
		return langGet(L_OPTIONS_290); // "Deceased"
	}

	if (g_StageIndex == STAGEINDEX_DEFENSE) {
		return langGet(L_MPWEAPONS_063); // "Missing"
	}

	return langGet(L_OPTIONS_291); // "Active"
}

char *endscreenMenuTitleStageCompleted(struct menuitem *item)
{
#if VERSION >= VERSION_NTSC_1_0
	sprintf(g_StringPointer, "%s: %s\n",
			langGet(g_SoloStages[g_Menus[g_MpPlayerNum].endscreen.stageindex].name3),
			langGet(L_OPTIONS_276)); // "Completed"
#else
	sprintf(g_StringPointer, "%s: %s\n",
			langGet(g_SoloStages[g_MissionConfig.stageindex].name3),
			langGet(L_OPTIONS_276)); // "Completed"
#endif

	return g_StringPointer;
}

#if VERSION >= VERSION_NTSC_1_0
char *endscreenMenuTextCurrentStageName3(struct menuitem *item)
{
	char *name = langGet(g_SoloStages[g_MissionConfig.stageindex].name3);
	sprintf(g_StringPointer, "%s\n", name);

	return g_StringPointer;
}
#endif

char *endscreenMenuTitleStageFailed(struct menuitem *item)
{
	sprintf(g_StringPointer, "%s: %s\n",
			langGet(g_SoloStages[g_MissionConfig.stageindex].name3),
			langGet(L_OPTIONS_277)); // "Failed"

	return g_StringPointer;
}

char *endscreenMenuTextMissionTime(struct menuitem *item)
{
	formatTime(g_StringPointer, playerGetMissionTime(), TIMEPRECISION_SECONDS);
	strcat(g_StringPointer, "\n");

	return g_StringPointer;
}

struct menudialogdef *endscreenAdvance(void)
{

	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (!g_Vars.antiplayers[i] && g_Vars.players[i] && g_Vars.players[i]->advancedendscreen) {
			// If any player has already advanced the endscreen, just return
			// the next mission dialog.
			return &g_NextMissionMenuDialog;
		}
	}

	g_MissionConfig.stageindex++;
	g_MissionConfig.stagenum = g_SoloStages[g_MissionConfig.stageindex].stagenum;
	g_Vars.currentplayer->advancedendscreen = true;
	return &g_NextMissionMenuDialog;
}

void endscreenResetModels(void)
{
	menuResetModel(&g_Menus[0].menumodel, bgunCalculateGunMemCapacity() - menugfxGetParticleArraySize(), false);
	g_Menus[0].menumodel.allocstart = bgunGetGunMem() + menugfxGetParticleArraySize();

	menuResetModel(&g_Menus[1].menumodel, bgunCalculateGunMemCapacity() - menugfxGetParticleArraySize(), false);
	g_Menus[1].menumodel.allocstart = bgunGetGunMem() + menugfxGetParticleArraySize();

	menuResetModel(&g_Menus[2].menumodel, bgunCalculateGunMemCapacity() - menugfxGetParticleArraySize(), false);
	g_Menus[2].menumodel.allocstart = bgunGetGunMem() + menugfxGetParticleArraySize();

	menuResetModel(&g_Menus[3].menumodel, bgunCalculateGunMemCapacity() - menugfxGetParticleArraySize(), false);
	g_Menus[3].menumodel.allocstart = bgunGetGunMem() + menugfxGetParticleArraySize();
}

#if VERSION >= VERSION_NTSC_1_0
MenuItemHandlerResult endscreenHandleReplayLastLevel(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_CHECKDISABLED:
	case MENUOP_CHECKHIDDEN:
		return 0;
	case MENUOP_SET:
		g_MissionConfig.stagenum = g_SoloStages[g_MissionConfig.stageindex].stagenum;
		return menuhandlerAcceptMission(operation, NULL, data);
	}

	return 0;
}
#endif

struct menuitem g_2PMissionEndscreenObjectivesVMenuItems[] = {
	{
		MENUITEMTYPE_OBJECTIVES,
		2,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		L_OPTIONS_301, // "Press START"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

struct menuitem g_SoloEndscreenObjectivesMenuItems[] = {
	{
		MENUITEMTYPE_OBJECTIVES,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		L_OPTIONS_301, // "Press START"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_SoloEndscreenObjectivesFailedMenuDialog = {
	MENUDIALOGTYPE_DANGER,
	L_OPTIONS_300, // "Objectives"
	g_SoloEndscreenObjectivesMenuItems,
	soloMenuDialogPauseStatus,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	NULL,
};

struct menudialogdef g_SoloEndscreenObjectivesCompletedMenuDialog = {
	MENUDIALOGTYPE_SUCCESS,
	L_OPTIONS_300, // "Objectives"
	g_SoloEndscreenObjectivesMenuItems,
	soloMenuDialogPauseStatus,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	NULL,
};

struct menudialogdef g_2PMissionEndscreenObjectivesFailedVMenuDialog = {
	MENUDIALOGTYPE_DANGER,
	L_OPTIONS_300, // "Objectives"
	g_2PMissionEndscreenObjectivesVMenuItems,
	soloMenuDialogPauseStatus,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	NULL,
};

struct menudialogdef g_2PMissionEndscreenObjectivesCompletedVMenuDialog = {
	MENUDIALOGTYPE_SUCCESS,
	L_OPTIONS_300, // "Objectives"
	g_2PMissionEndscreenObjectivesVMenuItems,
	soloMenuDialogPauseStatus,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	NULL,
};

#if VERSION >= VERSION_NTSC_1_0
/**
 * Displayed after Defense and Skedar Ruins completion screens.
 */
MenuItemHandlerResult endscreenHandleContinueMission(s32 operation, struct menuitem *item, union handlerdata *data)
{
	DEBUG_ENDSCREEN("endscreenHandleContinueMission: ENTER - operation=%d\n", operation);
	DEBUG_ENDSCREEN("endscreenHandleContinueMission: menuroot=%d, bg=%d\n", g_MenuData.root, g_MenuData.bg);
	switch (operation) {
	case MENUOP_CHECKDISABLED:
	case MENUOP_CHECKHIDDEN:
		DEBUG_ENDSCREEN("endscreenHandleContinueMission: CHECK operation - returning 0\n");
		return 0;
	case MENUOP_SET:
		DEBUG_ENDSCREEN("endscreenHandleContinueMission: MENUOP_SET - calling endscreenContinue(2)\n");
		endscreenContinue(2);
		break;
	}

	DEBUG_ENDSCREEN("endscreenHandleContinueMission: EXIT - returning 0\n");
	return 0;
}
#endif

#if VERSION >= VERSION_NTSC_1_0
struct menuitem g_MissionContinueOrReplyMenuItems[] = {
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		L_MPWEAPONS_244, // "Continue"
		0,
		endscreenHandleContinueMission,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		L_MPWEAPONS_245, // "Replay Last Level"
		0,
		endscreenHandleReplayLastLevel,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_MissionContinueOrReplyMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)&endscreenMenuTextCurrentStageName3,
	g_MissionContinueOrReplyMenuItems,
	NULL,
	MENUDIALOGFLAG_STARTSELECTS,
	NULL,
};
#endif

/**
 * Context is:
 *
 * 0 when closing a completed endscreen. This is called when the dialogbouncebacktimer reaches 0 within endScreenHandle2PCompleted's tick handler.
 * 1 is invoked directlly by endscreenDecideAndPushNextTeam
 * 2 when pressing continue. This is invoked by endscreenHandleContinueMission.
 */
void endscreenContinue(s32 context)
{
	DEBUG_ENDSCREEN("endscreenContinue: ENTER - context=%d, stagenum=%d, stageindex=%d, difficulty=%d\n",
		context, g_Vars.stagenum, g_MissionConfig.stageindex, g_MissionConfig.difficulty);
	DEBUG_ENDSCREEN("endscreenContinue: isteam=%d, iscoop=%d, isanti=%d, PLAYERCOUNT=%d\n",
		g_MissionConfig.isteam, g_MissionConfig.iscoop, g_MissionConfig.isanti, PLAYERCOUNT());
	DEBUG_ENDSCREEN("endscreenContinue: menuroot=%d, bg=%d\n", g_MenuData.root, g_MenuData.bg);

	if (context == 0 || context == 1) {
		DEBUG_ENDSCREEN("endscreenContinue: Branch context 0 or 1 (context=%d)\n", context);
		switch (g_Vars.stagenum) {
			case STAGE_DEEPSEA:
			case STAGE_MBR:
			case STAGE_WAR:
			case STAGE_MAIANSOS:
			case STAGE_DUEL:
			case STAGE_SKEDARRUINS:
				DEBUG_ENDSCREEN("endscreenContinue: Special stage case - stagenum=%d\n", g_Vars.stagenum);
				// If we are on Deep Sea or Skedar Ruins, we need to push the continue/reply dialog
				// so that the player can choose to continue or reply.
				if (context == 1) {
					DEBUG_ENDSCREEN("endscreenContinue: context==1, pushing continue/reply dialog\n");
					menuPushRootDialog(&g_MissionContinueOrReplyMenuDialog, MENUROOT_COOPCONTINUE);
				} else {
					DEBUG_ENDSCREEN("endscreenContinue: context==0 on special stage, no action\n");
				}
				break;
			default:
				DEBUG_ENDSCREEN("endscreenContinue: Default stage case - stagenum=%d\n", g_Vars.stagenum);
				// If we are not on Deep Sea or Skedar Ruins, we just pop the dialog.
				DEBUG_ENDSCREEN("endscreenContinue: Popping dialog\n");
				menuPopDialog();
				if (context == 1)  {
					DEBUG_ENDSCREEN("endscreenContinue: context==1, calling endscreenAdvance\n");
					struct menudialogdef *definition = endscreenAdvance();

					if (definition) {
						DEBUG_ENDSCREEN("endscreenContinue: endscreenAdvance returned valid definition, resetting models and pushing dialog\n");
						endscreenResetModels();
						menuPushRootDialog(definition, MENUROOT_COOPCONTINUE);
					} else {
						DEBUG_ENDSCREEN("endscreenContinue: endscreenAdvance returned NULL, no dialog to push\n");
					}
				} else {
					DEBUG_ENDSCREEN("endscreenContinue: context==0, dialog popped only\n");
				}
				break;
		}
	// we pressed continue. ie context is 2
	} else {
		DEBUG_ENDSCREEN("endscreenContinue: Branch context 2 (pressed continue) - context=%d\n", context);
		DEBUG_ENDSCREEN("endscreenContinue: Branch context 2 (pressed continue) - context=%d\n", context);
		switch (g_Vars.stagenum) {
			case STAGE_DEEPSEA:
				DEBUG_ENDSCREEN("endscreenContinue: STAGE_DEEPSEA case\n");
				DEBUG_ENDSCREEN("endscreenContinue: Checking isStageDifficultyUnlocked for stageindex+1=%d, difficulty=%d\n",
					g_MissionConfig.stageindex + 1, g_MissionConfig.difficulty);
				if (!isStageDifficultyUnlocked(g_MissionConfig.stageindex + 1, g_MissionConfig.difficulty)) {
					DEBUG_ENDSCREEN("endscreenContinue: Next stage NOT unlocked, popping dialogs twice\n");
					menuPopDialog();
					menuPopDialog();
				} else {
					DEBUG_ENDSCREEN("endscreenContinue: Next stage IS unlocked, advancing to next stage\n");
					// Commit to starting next stage
					g_MissionConfig.stageindex++;
					g_MissionConfig.stagenum = g_SoloStages[g_MissionConfig.stageindex].stagenum;
					DEBUG_ENDSCREEN("endscreenContinue: NEW stageindex=%d, stagenum=%d\n",
						g_MissionConfig.stageindex, g_MissionConfig.stagenum);

					titleSetNextStage(g_MissionConfig.stagenum);

					if (g_MissionConfig.isteam) {
						DEBUG_ENDSCREEN("endscreenContinue: isteam=true, resetting team players\n");
						playermgrResetTeamPlayers();
						s32 teamPlayers = getNumTeamPlayerRoleAssignments();
						DEBUG_ENDSCREEN("endscreenContinue: Setting num players to %d\n", teamPlayers);
						setNumPlayers(teamPlayers);
					}
					else {
						DEBUG_ENDSCREEN("endscreenContinue: isteam=false, disabling team players, setting 1 player\n");
						playermgrDisableTeamPlayers(false);
						setNumPlayers(1);
					}

					DEBUG_ENDSCREEN("endscreenContinue: Starting next stage %d\n", g_MissionConfig.stagenum);
					lvSetDifficulty(g_MissionConfig.difficulty);
					titleSetNextMode(TITLEMODE_SKIP);
					mainChangeToStage(g_MissionConfig.stagenum);
					viBlack(true);
				}
				break;
			case STAGE_SKEDARRUINS:
				DEBUG_ENDSCREEN("endscreenContinue: STAGE_SKEDARRUINS case - starting credits\n");
				// Save the last completed mission before transitioning to credits
				g_LastCompletedMission = g_MissionConfig.stagenum;
				// Commit to starting credits
				g_MissionConfig.stagenum = STAGE_CREDITS;
				DEBUG_ENDSCREEN("endscreenContinue: Setting stage to CREDITS (%d)\n", STAGE_CREDITS);
				titleSetNextStage(g_MissionConfig.stagenum);
				lvSetDifficulty(g_MissionConfig.difficulty);
				titleSetNextMode(TITLEMODE_SKIP);
				mainChangeToStage(g_MissionConfig.stagenum);
				viBlack(true);
				break;
			case STAGE_MBR:
			case STAGE_WAR:
			case STAGE_MAIANSOS:
			case STAGE_DUEL:
				DEBUG_ENDSCREEN("endscreenContinue: Team mission/Duel case - resetting to training\n");
				viBlack(true);

				while (g_Menus[g_MpPlayerNum].depth > 0) {
					menuPopDialog();
				}

				menuUpdateCurFrame();

				menuResetToTraining();
				break;
			default:
				DEBUG_ENDSCREEN("endscreenContinue: DEFAULT case for stagenum=%d\n", g_Vars.stagenum);
				DEBUG_ENDSCREEN("endscreenContinue: Popping dialog\n");
				menuPopDialog();

				s32 nextStageUnlocked = isStageDifficultyUnlocked(g_MissionConfig.stageindex + 1, g_MissionConfig.difficulty);
				s32 stageIdx = stageGetIndex(g_MissionConfig.stagenum);
				DEBUG_ENDSCREEN("endscreenContinue: nextStageUnlocked=%d, stageGetIndex=%d, STAGE_CITRAINING=%d, stageindex=%d, SOLOSTAGEINDEX_MBR=%d\n",
					nextStageUnlocked, stageIdx, STAGE_CITRAINING, g_MissionConfig.stageindex, SOLOSTAGEINDEX_MBR);

				if (isStageDifficultyUnlocked(g_MissionConfig.stageindex + 1, g_MissionConfig.difficulty) == 0) {
					DEBUG_ENDSCREEN("endscreenContinue: Next stage not unlocked, resetting to training\n");
					while (g_Menus[g_MpPlayerNum].depth > 0) {
						menuPopDialog();
					}
					menuResetToTraining();
				} else if (stageGetIndex(g_MissionConfig.stagenum) < 0
						|| g_Vars.stagenum == STAGE_CITRAINING
						|| g_MissionConfig.stageindex >= SOLOSTAGEINDEX_MBR) {
					DEBUG_ENDSCREEN("endscreenContinue: Invalid stage or training or past MBR, resetting to training\n");
					while (g_Menus[g_MpPlayerNum].depth > 0) {
						menuPopDialog();
					}
					menuResetToTraining();
				} else {
					DEBUG_ENDSCREEN("endscreenContinue: Valid progression, advancing endscreen\n");
					endscreenResetModels();
					menuPushDialog(endscreenAdvance());
				}
				break;
		}
	}
	DEBUG_ENDSCREEN("endscreenContinue: EXIT\n");
}

MenuDialogHandlerResult endscreenHandle2PCompleted(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data)
{
	DEBUG_ENDSCREEN("endscreenHandle2PCompleted: ENTER - operation=%d, g_MpPlayerNum=%d, stagenum=%d\n",
		operation, g_MpPlayerNum, g_Vars.stagenum);
	DEBUG_ENDSCREEN("endscreenHandle2PCompleted: PLAYERCOUNT=%d, isteam=%d, iscoop=%d, isanti=%d\n",
		PLAYERCOUNT(), g_MissionConfig.isteam, g_MissionConfig.iscoop, g_MissionConfig.isanti);
	DEBUG_ENDSCREEN("endscreenHandle2PCompleted: menuroot=%d, bg=%d\n", g_MenuData.root, g_MenuData.bg);

	if (operation == MENUOP_OPEN) {
		DEBUG_ENDSCREEN("endscreenHandle2PCompleted: MENUOP_OPEN - resetting dialogbouncebacktimer to 0\n");
		g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer = 0;
	}

	if (operation == MENUOP_TICK) {
		DEBUG_ENDSCREEN("endscreenHandle2PCompleted: MENUOP_TICK\n");
		if (g_Menus[g_MpPlayerNum].curdialog) {
			DEBUG_ENDSCREEN("endscreenHandle2PCompleted: curdialog exists\n");
			if (g_Menus[g_MpPlayerNum].curdialog->definition == dialogdef
					|| (dialogdef->nextsibling && dialogdef->nextsibling == g_Menus[g_MpPlayerNum].curdialog->definition)) {
				DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Dialog definition matches\n");
				struct menuinputs *inputs = data->dialog2.inputs;

				DEBUG_ENDSCREEN("endscreenHandle2PCompleted: inputs - select=%d, back=%d, start=%d\n",
					inputs->select, inputs->back, inputs->start);
				if (inputs->select || inputs->back || inputs->start) {
					s32 newTimer = VERSION >= VERSION_NTSC_1_0 ? 6 : 3;
					DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Input detected, setting timer to %d (VERSION=%d)\n",
						newTimer, VERSION);
					g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer = newTimer;
				}

				DEBUG_ENDSCREEN("endscreenHandle2PCompleted: dialogbouncebacktimer=%d\n",
					g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer);

				if (g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer) {
					DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Timer is active\n");
					// decrement the timer
					if (g_IsModalMenuMode) {
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: g_IsModalMenuMode=true, decrementing timer from %d\n",
							g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer);
						g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer--;
					} else {
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: g_IsModalMenuMode=false, NOT decrementing timer\n");
					}

					if (g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer == 0) {
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Timer reached 0, calling endscreenContinue(0)\n");
						endscreenContinue(0);

						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Checking antiplayer aborts\n");
						bool antiaborted = false;
						for (s32 i = 0; i < MAX_PLAYERS; i++) {
							if (g_Vars.antiplayers[i]) {
								DEBUG_ENDSCREEN("endscreenHandle2PCompleted: antiplayer[%d] exists, aborted=%d\n",
									i, g_Vars.antiplayers[i]->aborted);
								if (g_Vars.antiplayers[i]->aborted) {
									antiaborted = true;
									break;
								}
							}
						}
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: antiaborted=%d\n", antiaborted);

						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Checking coop/bond player aborts\n");
						bool p1p2aborted = false;
						for (s32 i = 0; i < MAX_PLAYERS; i++) {
							if (g_Vars.coopplayers[i]) {
								DEBUG_ENDSCREEN("endscreenHandle2PCompleted: coopplayer[%d] exists, aborted=%d\n",
									i, g_Vars.coopplayers[i]->aborted);
								if (g_Vars.coopplayers[i]->aborted) {
									p1p2aborted = true;
									break;
								}
							}
							if (g_Vars.bond->aborted) {
								DEBUG_ENDSCREEN("endscreenHandle2PCompleted: bond aborted=true\n");
								p1p2aborted = true;
								break;
							}
						}
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: p1p2aborted=%d\n", p1p2aborted);

						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Checking objectives complete\n");
						bool objectivescomplete = objectiveIsAllComplete();
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: objectivescomplete=%d\n", objectivescomplete);

						// special endscreen logic: don't progress if anyone aborted or objectives not complete
						bool progress = !antiaborted && !p1p2aborted && objectivescomplete;
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: progress=%d (!antiaborted && !p1p2aborted && objectivescomplete)\n", progress);

						// these stages have special endscreen logic
						// they should show a contunue or retry dialog
						// when completed
						// or advance after closing the dialogs
						// when the level was completed normally
						// because we might be in a p1p2 dialog
						// and anti aborted, we could have a green completed
						// dialog without actually beating the level
						// we need to treat it like a failed level for the purposes of
						// endscreen flow logic
						bool isspecialstage = g_Vars.stagenum == STAGE_DEEPSEA
												|| g_Vars.stagenum == STAGE_DUEL
												|| g_Vars.stagenum == STAGE_MBR
												|| g_Vars.stagenum == STAGE_WAR
												|| g_Vars.stagenum == STAGE_MAIANSOS
												|| g_Vars.stagenum == STAGE_SKEDARRUINS;
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: isspecialstage=%d (stagenum=%d)\n", isspecialstage, g_Vars.stagenum);

						bool isAntiPlayer = g_Vars.antiplayers[g_MpPlayerNum] != NULL;
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: isAntiPlayer=%d\n", isAntiPlayer);

						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Evaluating final branches...\n");
						if (!g_Vars.antiplayers[g_MpPlayerNum] && !progress && isspecialstage) {
							DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Branch 1: NOT antiplayer, NOT progress, IS specialstage -> menuPopDialog\n");
							menuPopDialog();
						}
						else if (PLAYERCOUNT() == 1 && progress && isspecialstage) {
							DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Branch 2: PLAYERCOUNT==1, progress, IS specialstage -> push continue/reply dialog\n");
							menuPushDialog(&g_MissionContinueOrReplyMenuDialog);
						}
						else if (g_Vars.antiplayers[g_MpPlayerNum]) {
							DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Branch 3: IS antiplayer -> menuPopDialog\n");
							menuPopDialog();
						}
						else {
							DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Branch 4: DEFAULT -> reset models and pop dialog\n");
							endscreenResetModels();
							menuPopDialog();
						}
					} else {
						DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Timer not yet 0 (timer=%d)\n",
							g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer);
					}
				}

				DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Clearing input flags\n");
				inputs->select = inputs->back = inputs->start = false;
			} else {
				DEBUG_ENDSCREEN("endscreenHandle2PCompleted: Dialog definition does NOT match\n");
			}
		} else {
			DEBUG_ENDSCREEN("endscreenHandle2PCompleted: curdialog is NULL\n");
		}
	}

	DEBUG_ENDSCREEN("endscreenHandle2PCompleted: EXIT - returning 0\n");
	return 0;
}

MenuDialogHandlerResult endscreenHandle2PFailed(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data)
{
	if (operation == MENUOP_OPEN) {
		g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer = 0;
	}

	if (operation == MENUOP_TICK) {
		if (g_Menus[g_MpPlayerNum].curdialog) {
			if (g_Menus[g_MpPlayerNum].curdialog->definition == dialogdef
					|| (dialogdef->nextsibling && dialogdef->nextsibling == g_Menus[g_MpPlayerNum].curdialog->definition)) {
				struct menuinputs *inputs = data->dialog2.inputs;

				if (inputs->select || inputs->back || inputs->start) {
					g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer = VERSION >= VERSION_NTSC_1_0 ? 6 : 3;
				}

				if (g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer) {
					// decrement the timer
					if (g_IsModalMenuMode) {
						g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer--;
					}

					// if the timer has reached zero, do the handling here
					if (g_Menus[g_MpPlayerNum].endscreen.dialogbouncebacktimer == 0) {
						if (g_Vars.antiplayernum >= 0
								|| (g_Vars.coopplayernum >= 0 && PLAYERCOUNT() >= 2)
								|| stageGetIndex(g_MissionConfig.stagenum) < 0
								|| g_Vars.stagenum == STAGE_CITRAINING) {
							menuPopDialog();
						} else {
							endscreenResetModels();
							menuPushDialog(&g_RetryMissionMenuDialog);
						}
					}
				}

				inputs->select = inputs->back = inputs->start = false;
			}
		}
	}

	return 0;
}

struct menuitem g_2PMissionEndscreenVMenuItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_SMALLFONT,
		L_OPTIONS_278, // "Mission Status:"
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		(uintptr_t)&endscreenMenuTextMissionStatus,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_SMALLFONT,
		L_OPTIONS_279, // "Agent Status:"
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		(uintptr_t)&endscreenMenuTextAgentStatus,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_SMALLFONT,
		L_OPTIONS_280, // "Mission Time:"
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		(uintptr_t)&endscreenMenuTextMissionTime,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_SMALLFONT,
		L_MPWEAPONS_129, // "Difficulty:"
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		(uintptr_t)soloMenuTextDifficulty,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_SMALLFONT,
		L_OPTIONS_281, // "Weapon of Choice:"
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		(uintptr_t)&mpMenuTextWeaponOfChoiceName,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_282, // "Kills:"
		(uintptr_t)&endscreenMenuTextNumKills,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_283, // "Accuracy:"
		(uintptr_t)&endscreenMenuTextAccuracy,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_284, // "Shot Total:"
		(uintptr_t)&endscreenMenuTextNumShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SMALLFONT,
		L_OPTIONS_285, // "Head Shots:"
		(uintptr_t)&endscreenMenuTextNumHeadShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SMALLFONT,
		L_OPTIONS_286, // "Body Shots:"
		(uintptr_t)&endscreenMenuTextNumBodyShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SMALLFONT,
		L_OPTIONS_287, // "Limb Shots:"
		(uintptr_t)&endscreenMenuTextNumLimbShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_SMALLFONT,
		L_OPTIONS_288, // "Others:"
		(uintptr_t)&endscreenMenuTextNumOtherShots,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		L_OPTIONS_289, // "Press START"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

#if VERSION >= VERSION_NTSC_1_0
/**
 * This function is re-used for several values on the endscreen.
 * item->param is used to determine which value it is. Values are:
 *
 * 0 = mission time
 * 1 = target time
 * 2 = separator and new cheat available
 * 3 = completion cheat name
 * 4 = others (shots)
 * 5 = timed cheat name
 * 6 = limb shots
 */
MenuItemHandlerResult endscreenHandleCheatInfo(s32 operation, struct menuitem *item, union handlerdata *data)
{
	static u32 cheatcolour = 0xff7f7fff;

	if (operation == MENUOP_GETCOLOUR
			&& ((g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x200) || item->param == 5)) {
		// Timed cheat just got unlocked, and this item is the timed cheat name
		u32 weight = menuGetSinOscFrac(40) * 255;

		mainOverrideVariable("ctcol", &cheatcolour);

		if (item->param == 0
				&& cheatGetTime(g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0xff) == 0) {
			return 0;
		}

		data->label.colour2 = colourBlend(data->label.colour2, cheatcolour, weight);

		if (item->param == 3) { // completion cheat name
			data->label.colour1 = colourBlend(data->label.colour1, cheatcolour, weight);
		}

		if (item->param == 5) { // timed cheat name
			data->label.colour1 = colourBlend(data->label.colour1, cheatcolour, weight);
		}
	}

	if (operation == MENUOP_CHECKHIDDEN) {
		if (item->param == 1) { // target time
			u32 info = g_Menus[g_MpPlayerNum].endscreen.cheatinfo;

			if (info & 0x800) { // completion cheat just got unlocked
				return true;
			}

			// (has timed cheat)
			// and (timed cheat just got unlocked or timed cheat already unlocked) == 0
			// and cheat has a target time configured
			if ((info & 0x100) && (info & 0x600) == 0 && cheatGetTime(info & 0xff) > 0) {
				return false;
			}

			return true;
		} else if (item->param == 2 && (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0xa00) == 0) {
			// new cheat available
			return true;
		} else if (item->param == 3 && (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x200) == 0) {
			// completion cheat name
			return true;
		} else if (item->param == 4 && (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0xa00)) {
			// others (shots)
			return true;
		} else if (item->param == 6 && (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0xa00) == 0xa00) {
			// limb shots
			return true;
		} else if (item->param == 5 && (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x800) == 0) {
			// timed cheat name
			return true;
		}
	}

	return false;
}
#endif

struct menuitem g_MissionEndscreenMenuItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_278, // "Mission Status:"
		(uintptr_t)&endscreenMenuTextMissionStatus,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_279, // "Agent Status:"
		(uintptr_t)&endscreenMenuTextAgentStatus,
		NULL,
	},
#if VERSION >= VERSION_NTSC_1_0
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LABEL_CUSTOMCOLOUR,
		L_OPTIONS_280, // "Mission Time:"
		(uintptr_t)&endscreenMenuTextMissionTime,
		endscreenHandleCheatInfo,
	},
	{
		MENUITEMTYPE_LABEL,
		1,
		MENUITEMFLAG_LABEL_CUSTOMCOLOUR,
		L_MPWEAPONS_242, // "Target Time:"
		(uintptr_t)&endscreenMenuTextTargetTime,
		endscreenHandleCheatInfo,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_MPWEAPONS_129, // "Difficulty:"
		(uintptr_t)&soloMenuTextDifficulty,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		2,
		0,
		0,
		0,
		endscreenHandleCheatInfo,
	},
	{
		MENUITEMTYPE_LABEL,
		2,
		0,
		L_MPWEAPONS_243, // "New Cheat Available!:"
		0,
		endscreenHandleCheatInfo,
	},
	{
		MENUITEMTYPE_LABEL,
		3,
		MENUITEMFLAG_SELECTABLE_CENTRE | MENUITEMFLAG_LABEL_CUSTOMCOLOUR,
		(uintptr_t)&endscreenMenuTextTimedCheatName,
		0,
		endscreenHandleCheatInfo,
	},
	{
		MENUITEMTYPE_LABEL,
		5,
		MENUITEMFLAG_SELECTABLE_CENTRE | MENUITEMFLAG_LABEL_CUSTOMCOLOUR,
		(uintptr_t)&endscreenMenuTextCompletionCheatName,
		0,
		endscreenHandleCheatInfo,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_281, // "Weapon of Choice:"
		(uintptr_t)&mpMenuTextWeaponOfChoiceName,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_282, // "Kills:"
		(uintptr_t)&endscreenMenuTextNumKills,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_283, // "Accuracy:"
		(uintptr_t)&endscreenMenuTextAccuracy,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_284, // "Shot Total:"
		(uintptr_t)&endscreenMenuTextNumShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_285, // "Head Shots:"
		(uintptr_t)&endscreenMenuTextNumHeadShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_286, // "Body Shots:"
		(uintptr_t)&endscreenMenuTextNumBodyShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		6,
		0,
		L_OPTIONS_287, // "Limb Shots:"
		(uintptr_t)&endscreenMenuTextNumLimbShots,
		endscreenHandleCheatInfo,
	},
	{
		MENUITEMTYPE_LABEL,
		4,
		0,
		L_OPTIONS_288, // "Others:"
		(uintptr_t)&endscreenMenuTextNumOtherShots,
		endscreenHandleCheatInfo,
	},
#else
	// NTSC beta's endscreen dialog lacks cheat information
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_280, // "Mission Time:"
		(uintptr_t)&endscreenMenuTextMissionTime,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_MPWEAPONS_129, // "Difficulty:"
		(uintptr_t)&soloMenuTextDifficulty,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_281, // "Weapon of Choice:"
		(uintptr_t)&mpMenuTextWeaponOfChoiceName,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_282, // "Kills:"
		(uintptr_t)&endscreenMenuTextNumKills,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_283, // "Accuracy:"
		(uintptr_t)&endscreenMenuTextAccuracy,
		NULL,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_284, // "Shot Total:"
		(uintptr_t)&endscreenMenuTextNumShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_285, // "Head Shots:"
		(uintptr_t)&endscreenMenuTextNumHeadShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_286, // "Body Shots:"
		(uintptr_t)&endscreenMenuTextNumBodyShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_287, // "Limb Shots:"
		(uintptr_t)&endscreenMenuTextNumLimbShots,
		NULL,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		0,
		L_OPTIONS_288, // "Others:"
		(uintptr_t)&endscreenMenuTextNumOtherShots,
		NULL,
	},
#endif
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		L_OPTIONS_289, // "Press START"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

#if VERSION >= VERSION_NTSC_1_0
char *endscreenMenuTextTimedCheatName(struct menuitem *item)
{
	if (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x00000300) {
		return cheatGetName(g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0xff);
	}

	return NULL;
}
#endif

#if VERSION >= VERSION_NTSC_1_0
char *endscreenMenuTextCompletionCheatName(struct menuitem *item)
{
	if (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x00000800) {
		return cheatGetName((g_Menus[g_MpPlayerNum].endscreen.cheatinfo >> 16) & 0xff);
	}

	return NULL;
}
#endif

#if VERSION >= VERSION_NTSC_1_0
char *endscreenMenuTextTargetTime(struct menuitem *item)
{
	s32 time;
	s32 time2;

	if ((g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x00000100) == 0) {
		return NULL;
	}

	time = g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0xff;
	time = cheatGetTime(time);

	if (!time) {
		return NULL;
	}

	formatTime(g_StringPointer, time * 60, TIMEPRECISION_SECONDS);
	strcat(g_StringPointer, "\n");
	return g_StringPointer;
}
#endif

void endscreenSetCoopCompleted(void)
{
	if (g_CheatsActiveBank0 == 0 && g_CheatsActiveBank1 == 0) {
#if VERSION >= VERSION_NTSC_1_0
		if (g_GameFile.coopcompletions[g_MissionConfig.difficulty] & (1 << g_MissionConfig.stageindex)) {
			g_Menus[g_MpPlayerNum].endscreen.isfirstcompletion = true;
		}
#endif

		g_GameFile.coopcompletions[g_MissionConfig.difficulty] |= (1 << g_MissionConfig.stageindex);
	}
}

struct menudialogdef g_SoloMissionEndscreenCompletedMenuDialog = {
	MENUDIALOGTYPE_SUCCESS,
	(uintptr_t)&endscreenMenuTitleStageCompleted,
	g_MissionEndscreenMenuItems,
	endscreenHandle2PCompleted,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	&g_SoloEndscreenObjectivesCompletedMenuDialog,
};

struct menudialogdef g_SoloMissionEndscreenFailedMenuDialog = {
	MENUDIALOGTYPE_DANGER,
	(uintptr_t)&endscreenMenuTitleStageFailed,
	g_MissionEndscreenMenuItems,
	endscreenHandle2PFailed,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	&g_SoloEndscreenObjectivesFailedMenuDialog,
};

/**
 * Prepare the endscreen by calculating unlocked cheats, setting the new best
 * time, choosing the default stage and difficulty and saving the game file.
 *
 * NTSC beta doesn't have cheats implemented, and has a different autostageindex
 * and thumbnail calculation.
 */
void endscreenPrepare(void)
{
	s32 timedcheatid;
	s32 complcheatid;
	s32 d;
	s32 s;
	u32 secs;
	s32 timedalreadyunlocked;
	s32 complalreadyunlocked;
	u16 prevbest;
	bool nowunlocked;

#if VERSION >= VERSION_NTSC_1_0
	g_Menus[g_MpPlayerNum].endscreen.stageindex = g_MissionConfig.stageindex;
#endif

	if (g_MenuData.root != MENUROOT_ENDSCREEN && g_Vars.mplayerisrunning == false) {
#if VERSION >= VERSION_NTSC_1_0
		g_Menus[g_MpPlayerNum].endscreen.cheatinfo = 0;
		g_Menus[g_MpPlayerNum].endscreen.isfirstcompletion = false;
		g_Menus[g_MpPlayerNum].playernum = 0;

		// Set cheat info
		if (g_MissionConfig.iscoop == false
				&& g_MissionConfig.isanti == false
				&& g_MissionConfig.pdmode == false) {
			timedcheatid = cheatGetByTimedStageIndex(g_MissionConfig.stageindex, g_MissionConfig.difficulty);
			complcheatid = cheatGetByCompletedStageIndex(g_MissionConfig.stageindex);

			if (timedcheatid >= 0) {
				g_Menus[g_MpPlayerNum].endscreen.cheatinfo = 0x0100 | timedcheatid;
			}

			if (complcheatid >= 0) {
				g_Menus[g_MpPlayerNum].endscreen.cheatinfo |= 0x1000 | (complcheatid << 16);
			}
		}
#else
		g_Menus[g_MpPlayerNum].playernum = 0;
#endif

		// Push the endscreen
#if VERSION >= VERSION_NTSC_1_0 && defined(DEBUG)
		if ((g_Vars.currentplayer->isdead || g_Vars.currentplayer->aborted || !objectiveIsAllComplete()) && !debugIsSetCompleteEnabled())
#else
		if (g_Vars.currentplayer->isdead || g_Vars.currentplayer->aborted || !objectiveIsAllComplete())
#endif
		{
			menuPushRootDialog(&g_SoloMissionEndscreenFailedMenuDialog, MENUROOT_ENDSCREEN);
		} else {
			menuPushRootDialog(&g_SoloMissionEndscreenCompletedMenuDialog, MENUROOT_ENDSCREEN);

			if (g_MissionConfig.isteam){
				endscreenSetCoopCompleted();
			}
		}

		if (g_MissionConfig.iscoop == false && g_MissionConfig.isanti == false) {
#if VERSION >= VERSION_NTSC_1_0
			timedalreadyunlocked = false;
			complalreadyunlocked = false;

			// If there's a timed cheat for this stage + difficulty
			if (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x100) {
				timedalreadyunlocked = cheatIsUnlocked(g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0xff);

				if (timedalreadyunlocked) {
					g_Menus[g_MpPlayerNum].endscreen.cheatinfo |= 0x400;
				}
			}

			// If there's a completion cheat for this stage (ie. not a special stage)
			if (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x1000) {
				complalreadyunlocked = cheatIsUnlocked((g_Menus[g_MpPlayerNum].endscreen.cheatinfo >> 16) & 0xff);
			}
#else
			playerGetMissionTime();
#endif

			// Update total mission time
			secs = playerGetMissionTime() / 60;

			if (secs != 0) {
				if (secs >= S32_MAX || S32_MAX - secs <= g_GameFile.totaltime) {
					g_GameFile.totaltime = S32_MAX;
				} else {
					g_GameFile.totaltime += secs;
				}
			}

			g_GameFile.autostageindex = g_MissionConfig.stageindex;
			g_GameFile.autodifficulty = g_MissionConfig.difficulty;

#if VERSION >= VERSION_NTSC_1_0 && defined(DEBUG)
			if (g_CheatsActiveBank0 == 0
					&& g_CheatsActiveBank1 == 0
					&& g_MissionConfig.pdmode == false
					&& ((g_Vars.currentplayer->isdead == false
							&& g_Vars.currentplayer->aborted == false
							&& objectiveIsAllComplete())
						|| debugIsSetCompleteEnabled()))
#elif VERSION >= VERSION_NTSC_1_0
			if (g_CheatsActiveBank0 == 0
					&& g_CheatsActiveBank1 == 0
					&& g_MissionConfig.pdmode == false
					&& g_Vars.currentplayer->isdead == false
					&& g_Vars.currentplayer->aborted == false
					&& objectiveIsAllComplete())
#else
			if (g_Vars.currentplayer->isdead == false
					&& g_Vars.currentplayer->aborted == false
					&& objectiveIsAllComplete()
					&& g_CheatsActiveBank0 == 0
					&& g_CheatsActiveBank1 == 0)
#endif
			{
				secs = playerGetMissionTime() / 60;

				// The save file allows 12 bits per time, which is up to
				// 1h 8m 16s. If the timer is higher than this, reduce it.
				if (secs > 0xfff) {
					secs = 0xfff;
				}

#if VERSION >= VERSION_NTSC_1_0
				// Zero is used as an indicator that the stage is not completed,
				// so if the player managed to legitly complete a stage in 0:00
				// adjust it to 0:01.
				if (secs == 0) {
					secs = 1;
				}

				// Set best time
				prevbest = g_GameFile.besttimes[g_MissionConfig.stageindex][g_MissionConfig.difficulty];

				if (prevbest == 0) {
					g_Menus[g_MpPlayerNum].endscreen.isfirstcompletion = true;
				}

				if (secs < prevbest || prevbest == 0) {
					g_GameFile.besttimes[g_MissionConfig.stageindex][g_MissionConfig.difficulty] = secs;
				}
#else
				prevbest = g_GameFile.besttimes[g_MissionConfig.stageindex][g_MissionConfig.difficulty];

				if (secs < prevbest || prevbest == 0) {
					g_GameFile.besttimes[g_MissionConfig.stageindex][g_MissionConfig.difficulty] = secs;
				}
#endif

				// Recalculate thumbnail for file select screen
				if (g_MissionConfig.stageindex <= SOLOSTAGEINDEX_SKEDARRUINS) {
					g_GameFile.autostageindex = g_MissionConfig.stageindex + 1;

					if (g_GameFile.autostageindex > SOLOSTAGEINDEX_SKEDARRUINS) {
						g_GameFile.autostageindex = SOLOSTAGEINDEX_SKEDARRUINS;
					}

					for (d = 0; d != 3; d++) {
						for (s = 0; s <= SOLOSTAGEINDEX_SKEDARRUINS; s++) {
							if (g_GameFile.besttimes[s][d]) {
								g_GameFile.thumbnail = s + 1;
							}
						}
					}
				}

				if (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x100) {
					nowunlocked = cheatIsUnlocked(g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0xff);

					if (!timedalreadyunlocked && nowunlocked) {
						g_Menus[g_MpPlayerNum].endscreen.cheatinfo |= 0x0200;
					}
				}

				if (g_Menus[g_MpPlayerNum].endscreen.cheatinfo & 0x1000) {
					nowunlocked = cheatIsUnlocked((g_Menus[g_MpPlayerNum].endscreen.cheatinfo >> 16) & 0xff);

					if (!complalreadyunlocked && nowunlocked) {
						g_Menus[g_MpPlayerNum].endscreen.cheatinfo |= 0x0800;
					}
				}

				challengeDetermineUnlockedFeatures();

				if (g_MissionConfig.stagenum == STAGE_SKEDARRUINS && g_AltTitleUnlocked == false) {
					g_AltTitleUnlocked = true;
#if VERSION >= VERSION_NTSC_1_0
					*(s8 *)&g_AltTitleEnabled = true;
#else
					g_AltTitleEnabled = true;
#endif
					bossfileSave();
				}
			}
		}

		filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_SAVE_GAME_000, 0);
		if (g_MissionConfig.isteam) {
			filemgrSaveMpPlayers();
		}
	}

	if (g_MenuData.root == MENUROOT_ENDSCREEN) {
		lvSetPaused(true);
		g_Vars.currentplayer->pausemode = PAUSEMODE_PAUSED;
	}
}

struct menudialogdef g_2PMissionEndscreenCompletedHMenuDialog = {
	MENUDIALOGTYPE_SUCCESS,
	(uintptr_t)&endscreenMenuTitleStageCompleted,
	g_MissionEndscreenMenuItems,
	endscreenHandle2PCompleted,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	&g_SoloEndscreenObjectivesCompletedMenuDialog,
};

struct menudialogdef g_2PMissionEndscreenFailedHMenuDialog = {
	MENUDIALOGTYPE_DANGER,
	(uintptr_t)&endscreenMenuTitleStageFailed,
	g_MissionEndscreenMenuItems,
	endscreenHandle2PFailed,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	&g_SoloEndscreenObjectivesFailedMenuDialog,
};

struct menudialogdef g_2PMissionEndscreenCompletedVMenuDialog = {
	MENUDIALOGTYPE_SUCCESS,
	L_OPTIONS_276, // "Completed"
	g_2PMissionEndscreenVMenuItems,
	endscreenHandle2PCompleted,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	&g_2PMissionEndscreenObjectivesCompletedVMenuDialog,
};

struct menudialogdef g_2PMissionEndscreenFailedVMenuDialog = {
	MENUDIALOGTYPE_DANGER,
	L_OPTIONS_277, // "Failed"
	g_2PMissionEndscreenVMenuItems,
	endscreenHandle2PFailed,
	MENUDIALOGFLAG_DISABLEITEMSCROLL | MENUDIALOGFLAG_SMOOTHSCROLLABLE,
	&g_2PMissionEndscreenObjectivesFailedVMenuDialog,
};

/**
 * chooseEndScreenFailedDialog - Selects and pushes the appropriate end screen dialog
 * when a mission is failed, based on the current player's anti status and the
 * desired dialog orientation (vertical or horizontal).
 *
 * If the current player is not an anti-player, the function pushes the "failed"
 * dialog. If the player is an anti-player, it treats the mission as completed
 * and pushes the "completed" dialog instead.
 *
 * @param usevertical: If true, selects the vertical dialog; otherwise, selects the horizontal dialog.
 */
static void chooseEndScreenFailedDialog(bool usevertical){
	if (usevertical) {
		menuPushRootDialog(&g_2PMissionEndscreenFailedVMenuDialog, MENUROOT_MPENDSCREEN);
	} else {
		menuPushRootDialog(&g_2PMissionEndscreenFailedHMenuDialog, MENUROOT_MPENDSCREEN);
	}
}

/**
 * chooseEndScreenCompletedDialog - Selects and pushes the appropriate end screen dialog
 * when a mission is completed, based on the current player's anti status and the
 * desired dialog orientation (vertical or horizontal).
 *
 * If the current player is not an anti-player, the function treats the mission as
 * completed and pushes the "completed" dialog. If the player is an anti-player,
 * it treats the mission as failed and pushes the "failed" dialog instead.
 *
 * @param usevertical: If true, selects the vertical dialog; otherwise, selects the horizontal dialog.
 */
static void chooseEndScreenCompletedDialog(bool usevertical){
	if (usevertical) {
		menuPushRootDialog(&g_2PMissionEndscreenCompletedVMenuDialog, MENUROOT_MPENDSCREEN);
	} else {
		menuPushRootDialog(&g_2PMissionEndscreenCompletedHMenuDialog, MENUROOT_MPENDSCREEN);
	}
}

/**
 * endscreenPushTeam - Handles the logic for displaying the end screen in team-based
 * cooperative or multiplayer missions. This function determines the mission outcome
 * (completed, failed, or aborted) for the current player, selects the appropriate
 * end screen dialog, and manages saving game or multiplayer player data as needed.
 *
 * The function consolidates the logic for both Coop and Anti missions:
 * - Pauses the game.
 * - Sets up end screen state for the current multiplayer player.
 * - Checks if Bond or any coop player is dead or has aborted.
 * - Determines if all objectives are complete.
 * - Chooses and displays the appropriate end screen dialog (failed or completed),
 *   handling both Coop and Anti mission types.
 * - Saves game or multiplayer player data.
 * - Restores the previous multiplayer player number.
 */
void endscreenPushTeam(void)
{
	u32 prevplayernum = g_MpPlayerNum;

	DEBUG_ENDSCREEN("endscreenPushTeam\n");
	DEBUG_ENDSCREEN("g_Vars.currentplayer: %p\n", g_Vars.currentplayer);

	lvSetPaused(true);

	g_MpPlayerNum = g_Vars.currentplayerstats->mpindex;
	DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);

	g_Menus[g_MpPlayerNum].endscreen.cheatinfo = 0;
	g_Menus[g_MpPlayerNum].endscreen.isfirstcompletion = false;
	g_Menus[g_MpPlayerNum].endscreen.stageindex = g_MissionConfig.stageindex;

	bool bondisdead = 0, coopisdead = 0, bondaborted = 0, coopaborted = 0, antiaborted = 0, allcomplete = 0;

	bondisdead = g_Vars.bond->isdead;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.coopplayers[i] && g_Vars.coopplayers[i]->isdead) {
			coopisdead = true;
		}
	}

	bondaborted = g_Vars.bond->aborted;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.coopplayers[i] && g_Vars.coopplayers[i]->aborted) {
			coopaborted = true;
		}
	}

	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.antiplayers[i] && g_Vars.antiplayers[i]->aborted) {
			antiaborted = true;
		}
	}

	allcomplete = objectiveIsAllComplete();

	g_Menus[g_MpPlayerNum].playernum = g_Vars.currentplayernum;

	bool usevertical = optionsGetScreenSplit() == SCREENSPLIT_VERTICAL
		|| PLAYERCOUNT() >= 3;


	if (antiaborted && g_Vars.antiplayers[g_Vars.currentplayernum]) {
		DEBUG_ENDSCREEN("anti aborted and currently anti\n");
		DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
		DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
		chooseEndScreenFailedDialog(usevertical);
	}
	else if (antiaborted && g_Vars.players[g_Vars.currentplayernum]) {
		// anti aborted: bond or coop
		DEBUG_ENDSCREEN("anti aborted and currently bond or coop\n");
		DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
		DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
		chooseEndScreenCompletedDialog(usevertical);
	}
	else if (g_Vars.antiplayers[g_Vars.currentplayernum]){
		// currentplayer: is currently anti
		//
		// bond or coop dead, failed or aborted
		bool p1p2failed = false;
		if (bondaborted || coopaborted) {
			DEBUG_ENDSCREEN("bond or coop aborted and currently anti\n");
			DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
			DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
			chooseEndScreenCompletedDialog(usevertical);
			p1p2failed = true;
		}
		if (bondisdead && coopisdead) {
			DEBUG_ENDSCREEN("bond and coop dead and currently anti\n");
			DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
			DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
			chooseEndScreenCompletedDialog(usevertical);
			p1p2failed = true;
		}
		if (!allcomplete && !antiaborted) {
			DEBUG_ENDSCREEN("not all objectives complete and currently anti\n");
			DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
			DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
			chooseEndScreenCompletedDialog(usevertical);
			p1p2failed = true;
		}

		if (!p1p2failed) {
			DEBUG_ENDSCREEN("anti did not fail, showing failed dialog\n");
			DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
			DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
			chooseEndScreenFailedDialog(usevertical);
		}

	} else if (!g_Vars.antiplayers[g_Vars.currentplayernum] && g_Vars.players[g_Vars.currentplayernum]) {
		// currentplayer: is currently bond or coop and p1p2 failed
		bool p1p2failed = false;
		if (bondaborted || coopaborted) {
			DEBUG_ENDSCREEN("bond or coop aborted and currently bond/coop\n");
			DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
			DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
			chooseEndScreenFailedDialog(usevertical);
			p1p2failed = true;
		}
		if (bondisdead && coopisdead) {
			DEBUG_ENDSCREEN("bond and coop dead and currently bond/coop\n");
			DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
			DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
			chooseEndScreenFailedDialog(usevertical);
			p1p2failed = true;
		}
		if (!allcomplete && !antiaborted) {
			DEBUG_ENDSCREEN("not all objectives complete and currently bond/coop\n");
			DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
			DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
			chooseEndScreenFailedDialog(usevertical);
			p1p2failed = true;
		}
		if (!p1p2failed) {
			DEBUG_ENDSCREEN("bond/coop did not fail, showing completed dialog\n");
			DEBUG_ENDSCREEN("g_MpPlayerNum: %d\n", g_MpPlayerNum);
			DEBUG_ENDSCREEN("playernum: %d\n", g_Vars.currentplayernum);
			chooseEndScreenCompletedDialog(usevertical);
			endscreenSetCoopCompleted();
		}
	}

	if (g_Vars.currentplayer == g_Vars.bond) {
		filemgrSaveOrLoad(&g_GameFileGuid, FILEOP_SAVE_GAME_000, 0);
		if (g_MissionConfig.isteam) {
			filemgrSaveMpPlayers();
		}
	}

	g_MpPlayerNum = prevplayernum;
}

/* This funciton pushes the menu after the endscreen
 * (ie. retry, next mission or continue), for team missions only
 *
 * This function is only called from menuTick, which is a bit weird...
 */
void endscreenDecideAndPushNextTeam(void)
{
	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: ENTER - stagenum=%d, isteam=%d, iscoop=%d\n",
		g_Vars.stagenum, g_MissionConfig.isteam, g_MissionConfig.iscoop);
	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: menuroot=%d, bg=%d\n", g_MenuData.root, g_MenuData.bg);

	u32 prevplayernum = g_MpPlayerNum;
	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: Saving prevplayernum=%d, setting g_MpPlayerNum to 0\n", prevplayernum);

	g_MpPlayerNum = 0;
	g_Menus[g_MpPlayerNum].playernum = 0;

	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: Checking coop player status...\n");
	bool coopisdead = false, coopaborted = false;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.coopplayers[i]) {
			DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: coopplayer[%d] exists - isdead=%d, aborted=%d\n",
				i, g_Vars.coopplayers[i]->isdead, g_Vars.coopplayers[i]->aborted);
			if (g_Vars.coopplayers[i]->isdead) {
				coopisdead = true;
			}
			if (g_Vars.coopplayers[i]->aborted) {
				coopaborted = true;
			}
		}
	}
	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: coopisdead=%d, coopaborted=%d\n", coopisdead, coopaborted);
	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: bond->isdead=%d, bond->aborted=%d\n",
		g_Vars.bond->isdead, g_Vars.bond->aborted);
	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: objectiveIsAllComplete=%d\n", objectiveIsAllComplete());

	if ((g_Vars.bond->isdead && coopisdead)
			|| g_Vars.bond->aborted
			|| coopaborted
			|| !objectiveIsAllComplete())
	{
		DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: Level failed or aborted - coopaborted=%d, objectives=%d\n",
			coopaborted, objectiveIsAllComplete());
		// Failed or aborted
		endscreenResetModels();
		menuPushRootDialog(&g_RetryMissionMenuDialog, MENUROOT_COOPCONTINUE);
	} else {
		DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: Level completed successfully - calling endscreenContinue(1)\n");
		// Completed
		endscreenContinue(1);
	}

	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: Restoring g_MpPlayerNum to %d\n", prevplayernum);
	g_MpPlayerNum = prevplayernum;
	DEBUG_ENDSCREEN("endscreenDecideAndPushNextTeam: EXIT\n");
}

