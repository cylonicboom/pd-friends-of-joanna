#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <PR/ultratypes.h>
#include <PR/ultrasched.h>
#include <PR/os_message.h>

#include "lib/main.h"
#include "game/modspectate.h"
#include "bss.h"
#include "data.h"

#include "video.h"
#include "audio.h"
#include "input.h"
#include "fs.h"
#include "romdata.h"
#include "config.h"
#include "savequeue.h"
#include "game/music.h"
#include "mod.h"
#include "game/stancetuning.h"
#include "game/menugfx.h"
#include "lib/snd.h"
#include "lib/sndcue.h"
#include "system.h"
#include "utils.h"
#include "game/mplayer/setup.h"
#include "ext_tex.h"
#include "skinmatch.h"
#include "game/luaai.h"

u32 g_OsMemSize = 0;
s32 g_OsMemSizeMb = 256;
u8 g_Is4Mb = 0;
s8 g_Resetting = false;
OSSched g_Sched;

OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];

u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;

u32 g_VmNumTlbMisses = 0;
u32 g_VmNumPageMisses = 0;
u32 g_VmNumPageReplaces = 0;
u8 g_VmShowStats = 0;

s32 g_TickRateDiv = 1;
s32 g_TickExtraSleep = true;

s32 g_SkipIntro = false;
char g_DefaultProfile[64] = "";
char g_DefaultReality[64] = "";

s32 g_FileAutoSelect = -1;

// Game.LuaAiMode: 0 = off, 1 = on, 2 = auto, on exactly when a Lua script
// is detected. Not Game.LuaAi: builds that had that key wrote LuaAi=0 as
// their default, which would now read as forced off. Kept apart from
// g_LuaAiEnabled, which --lua-ai / --no-lua-ai force for one run and which
// must not be written back to pd.ini.
#define LUA_AI_CONFIG_OFF  0
#define LUA_AI_CONFIG_ON   1
#define LUA_AI_CONFIG_AUTO 2
static s32 g_LuaAiConfig = LUA_AI_CONFIG_AUTO;

bool g_DebugEndscreen = false;
bool g_DebugMenu = false;
bool g_DebugModels = false;
bool g_DebugSplit = false;

extern s32 g_StageNum;

s32 bootGetMemSize(void)
{
	return (s32)g_OsMemSize;
}

void *bootAllocateStack(s32 threadid, s32 size)
{
	static u8 bruh[0x1000];
	return bruh;
}

void bootCreateSched(void)
{
	osCreateMesgQueue(&g_MainMesgQueue, g_MainMesgBuf, ARRAYCOUNT(g_MainMesgBuf));
	if (osTvType == OS_TV_MPAL) {
		osCreateScheduler(&g_Sched, NULL, OS_VI_MPAL_LAN1, 1);
	} else {
		osCreateScheduler(&g_Sched, NULL, OS_VI_NTSC_LAN1, 1);
	}
}

static void gameInit(void)
{
	osMemSize = g_OsMemSizeMb * 1024 * 1024;

	for (s32 i = 0; i < MAX_PLAYERS; ++i) {
		struct extplayerconfig *cfg = g_PlayerExtCfg + i;
		cfg->fovzoommult = cfg->fovzoom ? cfg->fovy / 60.0f : 1.0f;
	}

	if (g_HudCenter == HUDCENTER_NORMAL) {
		g_HudAlignModeL = G_ASPECT_CENTER_EXT;
		g_HudAlignModeR = G_ASPECT_CENTER_EXT;
	} else if (g_HudCenter == HUDCENTER_WIDE) {
		g_HudAlignModeL = G_ASPECT_LEFT_EXT | G_ASPECT_WIDE_EXT;
		g_HudAlignModeR = G_ASPECT_RIGHT_EXT | G_ASPECT_WIDE_EXT;
	}
}

static void cleanup(void)
{
	sysLogPrintf(LOG_NOTE, "shutdown");
	inputSaveBinds();
	// inputSaveBinds only refreshes the in-memory bind strings the config
	// table points at, so nothing has marked pd.ini dirty. Shutdown has
	// always written it unconditionally; keep that.
	saveQueueMarkConfig();
	// Commits pd.ini and eeprom.bin together. eeprom needed nothing here
	// before, because every block write had already hit the disk.
	saveQueueFlush();
	videoShutdown();
	crashShutdown();
	// TODO: actually shut down all subsystems
}

static void handleTermSignal(int sig)
{
	exit(0);
}

int main(int argc, const char **argv)
{
	sysInitArgs(argc, argv);

	if (!sysArgCheck("--no-crash-handler")) {
		crashInit();
	}

	sysInit();
	fsInit();
	romdataInit();

	// NOTE: dynamic mod staging loading disabled for now
	if (0) {
		// mpSetArenaMode(true); // Moved to pdmain.c after config load
	}

	configInit();
	videoInit();
	inputInit();
	audioInit();
	skinmatchInit();
	if (extTexInit() > 0) {
		extern bool gfx_external_textures_enabled;
		gfx_external_textures_enabled = true;
	}

	g_ValidGbcRomFound = romdataCheckGbcRom();

	gameInit();


	atexit(cleanup);

	signal(SIGINT, handleTermSignal);
	signal(SIGTERM, handleTermSignal);

	bootCreateSched();

	g_OsMemSize = osGetMemSize();

	g_MempHeapSize = g_OsMemSize;
	g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);
	if (!g_MempHeap) {
		sysFatalError("Could not alloc %u bytes for memp heap.", g_MempHeapSize);
	}

	sysLogPrintf(LOG_NOTE, "memp heap at %p - %p", g_MempHeap, g_MempHeap + g_MempHeapSize);
	sysLogPrintf(LOG_NOTE, "rom  file at %p - %p", g_RomFile, g_RomFile + g_RomFileSize);

	g_SndDisabled = sysArgCheck("--no-sound");

	if (getenv("PD_DEBUG_ENDSCREEN")) {
		g_DebugEndscreen = true;
		sysLogPrintf(LOG_NOTE, "Endscreen debugging enabled");
	}

	if (getenv("PD_DEBUG_MENU")) {
		g_DebugMenu = true;
		sysLogPrintf(LOG_NOTE, "Menu debugging enabled");
	}

	if (getenv("PD_DEBUG_MODELS")) {
		g_DebugModels = true;
		sysLogPrintf(LOG_NOTE, "Model/mod scaling debugging enabled");
	}

	if (getenv("PD_DEBUG_SPLIT")) {
		g_DebugSplit = true;
		sysLogPrintf(LOG_NOTE, "Upper body split debugging enabled");
	}

	// Spectator from the first frame. A button press cannot happen before the
	// stage loads, and the headless runs that want this cannot press one at all.
	g_ModSpectateStart = sysArgCheck("--spectate");

	// The Lua AI path is on when there is a script for it and off otherwise,
	// unless forced. The command line beats pd.ini, and --no-lua-ai beats
	// --lua-ai. Decided once here; plan item 0.3 re-decides when the set of
	// loaded mods changes.
	{
		const char *why;

		if (sysArgCheck("--no-lua-ai")) {
			g_LuaAiEnabled = 0;
			why = "lua ai forced off: --no-lua-ai";
		} else if (sysArgCheck("--lua-ai")) {
			g_LuaAiEnabled = 1;
			why = "lua ai forced on: --lua-ai";
		} else if (g_LuaAiConfig == LUA_AI_CONFIG_OFF) {
			g_LuaAiEnabled = 0;
			why = "lua ai forced off: Game.LuaAiMode=0";
		} else if (g_LuaAiConfig == LUA_AI_CONFIG_ON) {
			g_LuaAiEnabled = 1;
			why = "lua ai forced on: Game.LuaAiMode=1";
		} else if (luaaiScriptDetected()) {
			g_LuaAiEnabled = 1;
			why = "lua ai on: script detected";
		} else {
			g_LuaAiEnabled = 0;
			why = "lua ai off: no script";
		}

		sysLogPrintf(LOG_NOTE, "%s", why);
	}

	g_StageNum = sysArgGetInt("--boot-stage", STAGE_TITLE);

	if (g_StageNum == STAGE_TITLE && (sysArgCheck("--skip-intro") || g_SkipIntro)) {
		// shorthand for --boot-stage 0x26
		g_StageNum = STAGE_CITRAINING;
	} else if (g_StageNum < 0x01 || g_StageNum >= STAGE_TITLE) {
		// stage num out of range. 0x5d was the old STAGE_4MBMENU, back when
		// the menus sat directly above the last level; the levels now run
		// up to STAGE_TITLE - 1, so --boot-stage has to as well.
		g_StageNum = STAGE_TITLE;
	}

	if (g_StageNum != STAGE_TITLE) {
		sysLogPrintf(LOG_NOTE, "boot stage set to 0x%02x", g_StageNum);
	}

	g_FileAutoSelect = sysArgGetInt("--profile", -1);
	if (g_FileAutoSelect >= 0) {
		sysLogPrintf(LOG_NOTE, "player profile set to %d", g_FileAutoSelect);
	}

	mainProc();

	// Mod Switch
	g_ModNum = 0;

	return 0;
}

PD_CONSTRUCTOR static void gameConfigInit(void)
{
	configRegisterInt("Game.MemorySize", &g_OsMemSizeMb, 4, 2048);
	configRegisterInt("Game.CenterHUD", &g_HudCenter, 0, 2);
	configRegisterInt("Game.MenuMouseControl", &g_MenuMouseControl, 0, 1);
	configRegisterInt("Game.MenuMusicDivisor", &g_MusicMenuVolumeDivisor, 1, 64);
	configRegisterFloat("Game.ScreenShakeIntensity", &g_ViShakeIntensityMult, 0.f, 10.f);
	configRegisterInt("Game.TickRateDivisor", &g_TickRateDiv, 0, 10);
	configRegisterInt("Game.ExtraSleep", &g_TickExtraSleep, 0, 1);
	configRegisterInt("Game.SkipIntro", &g_SkipIntro, 0, 1);
	configRegisterString("Game.DefaultProfile", g_DefaultProfile, sizeof(g_DefaultProfile));
	configRegisterString("Game.DefaultReality", g_DefaultReality, sizeof(g_DefaultReality));
	configRegisterInt("Game.DisableMpDeathMusic", &g_MusicDisableMpDeath, 0, 1);
	configRegisterInt("Game.MeleeCombos", &g_MeleeCombosEnabled, 0, 1);
	configRegisterFloat("Game.SpectatorSpeed", &g_ModSpectateSpeed, 1.f, 200.f);
	configRegisterInt("Game.LuaAiMode", &g_LuaAiConfig, LUA_AI_CONFIG_OFF, LUA_AI_CONFIG_AUTO);

	// The audio pool sizes Rare picked for a 1999 cartridge. Every default is 0
	// or the original number, so leaving these alone changes nothing. They are
	// read once during sndInit, so a change needs a restart; the audio panel
	// shows what the pools actually came up as.
	//
	// Raise HeapSize first and generously -- everything else here is allocated
	// out of it and alHeapAlloc does not fail, it just hands back memory it does
	// not have. AcmdListLen wants raising in step with the voice pools, not
	// ahead of them. Nothing here touches the N64 build.
	configRegisterInt("Audio.HeapSize", &g_SndHeapLenKb, 0, 65536);
	configRegisterInt("Audio.SynMaxPVoices", &g_SndSynMaxPVoices, 1, 512);
	configRegisterInt("Audio.SynMaxVVoices", &g_SndSynMaxVVoices, 1, 512);
	configRegisterInt("Audio.SynMaxUpdates", &g_SndSynMaxUpdates, 1, 1024);
	configRegisterInt("Audio.SeqpMaxVoices", &g_SndSeqpMaxVoices, 1, 512);
	configRegisterInt("Audio.SeqpMaxEvents", &g_SndSeqpMaxEvents, 1, 1024);
	configRegisterInt("Audio.SeqBufferSize", &g_SndSeqBufferKb, 0, 4096);
	configRegisterInt("Audio.AcmdListLen", &g_SndAcmdListLen, 0, 65536);

	// The cue scheduler. Off by default -- Cue.Enabled is the first thing
	// sndcueOnSfx checks, and the reason it is affordable on sndStart, which
	// the game hits for every footstep and casing.
	//
	// Quantise: 0 now, 1 next beat, 2 next bar. There is no loop-point mode; a
	// compact sequence loops by rewinding its read pointer per track while the
	// tick count keeps climbing, so a loop boundary is not observable from out
	// here and is not shared between tracks anyway. Beat mode is the one that
	// needs no meter and so cannot be wrong.
	configRegisterInt("Cue.Enabled", &g_SndCueEnabled, 0, 1);
	configRegisterInt("Cue.Quantise", &g_SndCueQuantise, 0, 2);
	configRegisterInt("Cue.BeatsPerBar", &g_SndCueBeatsPerBar, 1, 32);
	configRegisterInt("Cue.Slot", &g_SndCueSlot, 0, 2);
	configRegisterInt("Cue.TriggerSfx", &g_SndCueTriggerSfx, -1, 65535);
	configRegisterInt("Cue.MaskFull", &g_SndCueMaskFull, 0, 0xffff);
	configRegisterInt("Cue.MaskDucked", &g_SndCueMaskDucked, 0, 0xffff);
	configRegisterInt("Cue.HoldTicks", &g_SndCueHoldTicks, 0, 1000000);

	// The stance knobs. Every one of these was a guess that only play could
	// settle, so they are settable: here for where they start, and the Fojo
	// Stance panel in the overlay for moving them while the game is running.
	// stance-tuning.md says what each one does; constants.h holds the defaults
	// these fall back to.
	configRegisterInt("Stance.FojoMovement", &g_FojoMovement, 0, 1);
	configRegisterFloat("Stance.AimSpeed", &g_AimStanceSpeed, 0.1f, 1.f);
	configRegisterFloat("Stance.FlinchSpeed", &g_FlinchSpeed, 0.1f, 1.f);
	configRegisterInt("Stance.FlinchTicks", &g_FlinchBusy, 0, 600);
	configRegisterInt("Stance.FlinchTicksMax", &g_FlinchBusyMax, 0, 600);
	configRegisterFloat("Stance.MeleeReach", &g_MeleeBodyReach, 0.f, 400.f);
	configRegisterFloat("Stance.MeleeConeCos", &g_MeleeConeCos, -1.f, 1.f);
	configRegisterFloat("Stance.CamDist", &g_ThirdPersonCamDist, 0.f, 1000.f);
	configRegisterFloat("Stance.CamClearance", &g_ThirdPersonCamClearance, 0.f, 200.f);
	configRegisterFloat("Stance.CamMinDist", &g_ThirdPersonCamMinDist, 0.f, 500.f);
	configRegisterFloat("Stance.CamSide", &g_ThirdPersonCamSide, -150.f, 150.f);
	configRegisterFloat("Stance.CamForward", &g_ThirdPersonCamForward, -150.f, 150.f);
	configRegisterFloat("Stance.CamHeight", &g_ThirdPersonCamHeight, -150.f, 150.f);
	configRegisterInt("Stance.CamTether", &g_ThirdPersonCamTether, TETHER_OFF, TETHER_MAX);
	configRegisterFloat("Stance.BodyTurnSpeed", &g_TetherBodyTurnSpeed, 5.f, 90.f);
	configRegisterFloat("Stance.FadeStart", &g_BodyFadeStart, 0.f, 1000.f);
	configRegisterFloat("Stance.FadeFloor", &g_BodyFadeFloor, 0.f, 1.f);
	configRegisterInt("Stance.LowerBodyMask", &g_AnimSplitLowerMask, 0, 0x7fff);
	configRegisterFloat("Stance.ReloadSpeed", &g_ReloadSpeed, 0.01f, 1.f);
	configRegisterInt("Stance.ReloadAnim", &g_ReloadAnimEnabled, 0, 1);
	configRegisterFloat("Stance.ReloadAnimSpeed", &g_ReloadAnimSpeed, 0.1f, 4.f);
	configRegisterFloat("Stance.RollImpulse", &g_RollImpulse, 0.f, 200.f);
	// MENUBG_SUCCESS_SLOW. Colours are 0xrrggbbaa; they save as decimal, but
	// reading takes hex too, so 0x333350a0 is fine to hand-edit.
	configRegisterFloat("MenuBg.Speed", &g_MenuBgSlowStars.speed, 0.f, 20.f);
	configRegisterUInt("MenuBg.HazeTop", &g_MenuBgSlowStars.hazetop, 0, 0);
	configRegisterUInt("MenuBg.HazeBottom", &g_MenuBgSlowStars.hazebottom, 0, 0);
	configRegisterInt("MenuBg.HazeTopInset", &g_MenuBgSlowStars.hazetopinset, -2000, 2000);
	configRegisterInt("MenuBg.HazeBottomInset", &g_MenuBgSlowStars.hazebottominset, -2000, 2000);
	configRegisterUInt("MenuBg.StarCore", &g_MenuBgSlowStars.starcore, 0, 0);
	configRegisterUInt("MenuBg.StarGlowA", &g_MenuBgSlowStars.starglow1, 0, 0);
	configRegisterUInt("MenuBg.StarGlowB", &g_MenuBgSlowStars.starglow2, 0, 0);
	configRegisterInt("MenuBg.Blur", &g_MenuBgSlowStars.blur, 0, 230);
	configRegisterInt("Blur.DoseEnabled", &g_BlurDoseEnabled, 0, 1);
	configRegisterFloat("Blur.DoseFullSeconds", &g_BlurDoseFullSecs, 1.f, 600.f);
	configRegisterFloat("Blur.DoseCurve", &g_BlurDoseK, 0.5f, 8.f);
	configRegisterInt("Stance.BuildSpeed", &g_BuildSpeedEnabled, 0, 1);
	configRegisterFloat("Stance.BuildSpeedRef", &g_BuildSpeedRef, 60.f, 300.f);
	configRegisterFloat("Stance.BuildCrouchMix", &g_BuildCrouchMix, 0.f, 1.f);
	configRegisterInt("Game.GEMuzzleFlashes", &g_BgunGeMuzzleFlashes, 0, 1);
	configRegisterInt("Game.MaxExplosions", &g_MaxExplosions, 6, 96);
	for (s32 j = 0; j < MAX_PLAYERS; ++j) {
		const s32 i = j + 1;
		configRegisterFloat(strFmt("Game.Player%d.FovY", i), &g_PlayerExtCfg[j].fovy, 5.f, 175.f);
		configRegisterInt(strFmt("Game.Player%d.FovAffectsZoom", i), &g_PlayerExtCfg[j].fovzoom, 0, 1);
		configRegisterInt(strFmt("Game.Player%d.MouseAimMode", i), &g_PlayerExtCfg[j].mouseaimmode, 0, 1);
		configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedX", i), &g_PlayerExtCfg[j].mouseaimspeedx, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedY", i), &g_PlayerExtCfg[j].mouseaimspeedy, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.RadialMenuSpeed", i), &g_PlayerExtCfg[j].radialmenuspeed, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.CrosshairSway", i), &g_PlayerExtCfg[j].crosshairsway, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.CameraTilt", i), &g_PlayerExtCfg[j].cameratilt, 0.f, 4.f);
		configRegisterFloat(strFmt("Game.Player%d.CameraBob", i), &g_PlayerExtCfg[j].camerabob, 0.f, 4.f);
		configRegisterInt(strFmt("Game.Player%d.GunSwayWithBob", i), &g_PlayerExtCfg[j].gunswaywithbob, 0, 1);
		configRegisterInt(strFmt("Game.Player%d.TiltIntoRun", i), &g_PlayerExtCfg[j].tiltforward, 0, 1);
		configRegisterInt(strFmt("Game.Player%d.InvertTilt", i), &g_PlayerExtCfg[j].tiltinvert, 0, 1);
		configRegisterInt(strFmt("Game.Player%d.CodAiming", i), &g_PlayerExtCfg[j].codaiming, 0, 1);
		configRegisterInt(strFmt("Game.Player%d.CodAimLock", i), &g_PlayerExtCfg[j].codaimlock, 0, 1);
		configRegisterFloat(strFmt("Game.Player%d.CrosshairEdgeBoundary", i), &g_PlayerExtCfg[j].crosshairedgeboundary, 0.0f, 1.0f);
		configRegisterInt(strFmt("Game.Player%d.CrouchMode", i), &g_PlayerExtCfg[j].crouchmode, 0, CROUCHMODE_TOGGLE_ANALOG);
		configRegisterInt(strFmt("Game.Player%d.ExtendedControls", i), &g_PlayerExtCfg[j].extcontrols, 0, 1);
		configRegisterUInt(strFmt("Game.Player%d.CrosshairColour", i), &g_PlayerExtCfg[j].crosshaircolour, 0, 0xFFFFFFFF);
		configRegisterUInt(strFmt("Game.Player%d.CrosshairSize", i), &g_PlayerExtCfg[j].crosshairsize, 0, 4);
		configRegisterInt(strFmt("Game.Player%d.CrosshairHealth", i), &g_PlayerExtCfg[j].crosshairhealth, 0, CROSSHAIR_HEALTH_ON_WHITE);
		configRegisterInt(strFmt("Game.Player%d.UseKeyReloads", i), &g_PlayerExtCfg[j].usereloads, 0, false);
	}
}
