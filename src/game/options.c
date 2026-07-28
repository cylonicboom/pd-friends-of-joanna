#include <ultra64.h>
#include "constants.h"
#include "game/cheats.h"
#include "game/mainmenu.h"
#include "game/music.h"
#include "game/options.h"
#include "bss.h"
#include "data.h"
#include "types.h"

u8 g_InGameSubtitles = 1;
u8 g_CutsceneSubtitles = 0;
u32 var8007fa98 = 0x00000000;
u32 var8007fa9c = 0x00000001;
u32 var8007faa0 = 0x00000000;
u32 var8007faa4 = 0x00000001;
u32 var8007faa8 = 0x00000001;
u32 var8007faac = 0x00000001;
s32 g_ScreenSize = SCREENSIZE_FULL;
s32 g_ScreenRatio = SCREENRATIO_NORMAL;
u8 g_ScreenSplit = SCREENSPLIT_HORIZONTAL;

#if VERSION < VERSION_NTSC_1_0
u16 var8008231cnb = 0x7fff;
#endif

static s32 optionsGetCurrentMpChrNum(void)
{
	if (g_Vars.currentplayerstats) {
		s32 mpchrnum = g_Vars.currentplayerstats->mpindex;

		if (mpchrnum >= 0 && mpchrnum < MAX_PLAYERS) {
			return mpchrnum;
		}
	}

	if (g_Vars.bondplayernum >= 0 && g_Vars.bondplayernum < MAX_PLAYERS) {
		return g_Vars.bondplayernum;
	}

	return 0;
}

s32 optionsGetControlMode(s32 mpchrnum)
{
	return g_PlayerConfigsArray[mpchrnum].controlmode;
}

void optionsSetControlMode(s32 mpchrnum, s32 mode)
{
	g_PlayerConfigsArray[mpchrnum].controlmode = mode;
}

s32 optionsGetContpadNum1(s32 mpchrnum)
{
	return g_PlayerConfigsArray[mpchrnum].contpad1;
}

s32 optionsGetContpadNum2(s32 mpchrnum)
{
	return g_PlayerConfigsArray[mpchrnum].contpad2;
}

s32 optionsGetForwardPitch(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_FORWARDPITCH) != 0;
}

s32 optionsGetAutoAim(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_AUTOAIM) != 0;
}

s32 optionsGetLookAhead(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_LOOKAHEAD) != 0;
}

s32 optionsGetAimControl(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_AIMCONTROL) != 0;
}

s32 optionsGetSightOnScreen(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_SIGHTONSCREEN) != 0;
}

s32 optionsGetAmmoOnScreen(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_AMMOONSCREEN) != 0;
}

s32 optionsGetShowGunFunction(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_SHOWGUNFUNCTION) != 0;
}

s32 optionsGetAlwaysShowTarget(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_ALWAYSSHOWTARGET) != 0;
}

s32 optionsGetShowZoomRange(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_SHOWZOOMRANGE) != 0;
}

s32 optionsGetPaintball(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_PAINTBALL) != 0;
}

s32 optionsGetClassicSight(s32 mpchrnum) {
	return g_PlayerConfigsArray[mpchrnum].classicsight ? *g_PlayerConfigsArray[mpchrnum].classicsight : 0;
}

s32 optionsGetShowLives(s32 mpchrnum)
{
	return g_PlayerConfigsArray[mpchrnum].showlives ? *g_PlayerConfigsArray[mpchrnum].showlives : 0;
}

s32 optionsGetShowMissionTime(s32 mpchrnum)
{
	if (g_PlayerConfigsArray[mpchrnum].showmissiontime) {
		return *g_PlayerConfigsArray[mpchrnum].showmissiontime != 0;
	}

	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_SHOWMISSIONTIME) != 0;
}

u8 optionsGetInGameSubtitlesForPlayer(s32 mpchrnum)
{
	if (g_PlayerConfigsArray[mpchrnum].ingamesubtitles) {
		return *g_PlayerConfigsArray[mpchrnum].ingamesubtitles != 0;
	}

	return g_InGameSubtitles;
}

u8 optionsGetCutsceneSubtitlesForPlayer(s32 mpchrnum)
{
	if (g_PlayerConfigsArray[mpchrnum].cutscenesubtitles) {
		return *g_PlayerConfigsArray[mpchrnum].cutscenesubtitles != 0;
	}

	return g_CutsceneSubtitles;
}

u8 optionsGetEffectiveCutsceneSubtitlesForPlayer(s32 mpchrnum)
{
	if (g_Vars.stagenum == STAGE_CITRAINING) {
		return mpchrnum == g_Vars.bondplayernum;
	}

	return optionsGetCutsceneSubtitlesForPlayer(mpchrnum);
}

u8 optionsGetInGameSubtitles(void)
{
	return optionsGetInGameSubtitlesForPlayer(optionsGetCurrentMpChrNum());
}

u8 optionsGetCutsceneSubtitles(void)
{
	return optionsGetCutsceneSubtitlesForPlayer(optionsGetCurrentMpChrNum());
}

s32 optionsGetHeadRoll(s32 mpchrnum)
{
	return (g_PlayerConfigsArray[mpchrnum].options & OPTION_HEADROLL) != 0;
}

void optionsSetForwardPitch(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_FORWARDPITCH;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_FORWARDPITCH;
	}
}

void optionsSetAutoAim(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_AUTOAIM;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_AUTOAIM;
	}
}

void optionsSetLookAhead(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_LOOKAHEAD;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_LOOKAHEAD;
	}
}

void optionsSetAimControl(s32 mpchrnum, s32 index)
{
	if (index) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_AIMCONTROL;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_AIMCONTROL;
	}
}

void optionsSetSightOnScreen(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_SIGHTONSCREEN;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_SIGHTONSCREEN;
	}
}

void optionsSetAmmoOnScreen(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_AMMOONSCREEN;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_AMMOONSCREEN;
	}
}

void optionsSetShowGunFunction(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_SHOWGUNFUNCTION;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_SHOWGUNFUNCTION;
	}
}

void optionsSetAlwaysShowTarget(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_ALWAYSSHOWTARGET;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_ALWAYSSHOWTARGET;
	}
}

void optionsSetShowZoomRange(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_SHOWZOOMRANGE;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_SHOWZOOMRANGE;
	}
}

void optionsSetPaintball(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_PAINTBALL;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_PAINTBALL;
	}
}

void optionsSetClassicSight(s32 mpchrnum, bool enable)
{
	if (g_PlayerConfigsArray[mpchrnum].classicsight) {
		*g_PlayerConfigsArray[mpchrnum].classicsight = enable ? 1 : 0;
	}
}

void optionsSetShowLives(s32 mpchrnum, bool enable)
{
	if (g_PlayerConfigsArray[mpchrnum].showlives) {
		*g_PlayerConfigsArray[mpchrnum].showlives = enable ? 1 : 0;
	}
}

void optionsSetShowMissionTime(s32 mpchrnum, bool enable)
{
	if (g_PlayerConfigsArray[mpchrnum].showmissiontime) {
		*g_PlayerConfigsArray[mpchrnum].showmissiontime = enable ? 1 : 0;
	}

	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_SHOWMISSIONTIME;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_SHOWMISSIONTIME;
	}
}

void optionsSetInGameSubtitlesForPlayer(s32 mpchrnum, s32 enable)
{
	if (g_PlayerConfigsArray[mpchrnum].ingamesubtitles) {
		*g_PlayerConfigsArray[mpchrnum].ingamesubtitles = enable ? 1 : 0;
	} else {
		g_InGameSubtitles = enable ? 1 : 0;
	}
}

void optionsSetCutsceneSubtitlesForPlayer(s32 mpchrnum, s32 enable)
{
	if (g_PlayerConfigsArray[mpchrnum].cutscenesubtitles) {
		*g_PlayerConfigsArray[mpchrnum].cutscenesubtitles = enable ? 1 : 0;
	} else {
		g_CutsceneSubtitles = enable ? 1 : 0;
	}
}

void optionsSetInGameSubtitles(s32 enable)
{
	optionsSetInGameSubtitlesForPlayer(optionsGetCurrentMpChrNum(), enable);
}

void optionsSetCutsceneSubtitles(s32 enable)
{
	optionsSetCutsceneSubtitlesForPlayer(optionsGetCurrentMpChrNum(), enable);
}

void optionsSetHeadRoll(s32 mpchrnum, bool enable)
{
	if (enable) {
		g_PlayerConfigsArray[mpchrnum].options |= OPTION_HEADROLL;
	} else {
		g_PlayerConfigsArray[mpchrnum].options &= ~OPTION_HEADROLL;
	}
}

s32 optionsGetEffectiveScreenSize(void)
{
	if (IS4MB()) {
		return SCREENSIZE_FULL;
	}

	if (g_MenuData.root == MENUROOT_TRAINING) {
		g_MpPlayerNum = 0;

		if (g_Menus[g_MpPlayerNum].curdialog && g_IsModalMenuMode) {
			return SCREENSIZE_FULL;
		}
	}

	if (g_Menus[g_MpPlayerNum].curdialog) {
		if (g_Menus[g_MpPlayerNum].curdialog->definition == &g_CiControlStylePlayer2MenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_CiControlStyleMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_SoloMissionControlStyleMenuDialog) {
			return SCREENSIZE_FULL;
		}

#if VERSION >= VERSION_JPN_FINAL
		if (g_Menus[g_MpPlayerNum].curdialog->definition == &g_CheatsFunMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_CheatsGameplayMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_CheatsSoloWeaponsMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_CheatsClassicWeaponsMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_CheatsWeaponsMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_CheatsBuddiesMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_CheatsMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_AcceptMissionMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_4PAcceptMissionMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_PreAndPostMissionBriefingMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_RetryMissionMenuDialog
				|| g_Menus[g_MpPlayerNum].curdialog->definition == &g_NextMissionMenuDialog) {
			return SCREENSIZE_FULL;
		}
#endif
	}

	if (PLAYERCOUNT() >= 2 || g_MenuData.root == MENUROOT_MPSETUP) {
		return SCREENSIZE_FULL;
	}

	return g_ScreenSize;
}

#if VERSION >= VERSION_NTSC_1_0
s32 optionsGetScreenSize(void)
{
	return g_ScreenSize;
}
#endif

void optionsSetScreenSize(s32 size)
{
	g_ScreenSize = size;
}

s32 optionsGetScreenRatio(void)
{
	return g_ScreenRatio;
}

void optionsSetScreenRatio(s32 ratio)
{
#ifdef PLATFORM_N64
	g_ScreenRatio = ratio;
#else
	g_ScreenRatio = SCREENRATIO_NORMAL;
#endif
}

u8 optionsGetScreenSplit(void)
{
	return g_ScreenSplit;
}

void optionsSetScreenSplit(u8 split)
{
	g_ScreenSplit = split;
}

u16 optionsGetMusicVolume(void)
{
#if VERSION >= VERSION_NTSC_1_0
	return musicGetVolume();
#else
	if (g_Vars.stagenum == STAGE_CREDITS) {
		return 0x7fff;
	}

	return var8008231cnb;
#endif
}

void optionsSetMusicVolume(u16 volume)
{
#if VERSION >= VERSION_NTSC_1_0
	musicSetVolume(volume);
#else
	var8008231cnb = volume;
	musicSetVolume(var8008231cnb);
#endif
}
