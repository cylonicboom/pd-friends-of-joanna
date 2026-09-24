#include <stdio.h>
#include <math.h>
#include <string.h>
#include <vector>
#include <map>
#include <string>
#include <zlib.h>

#include <SDL.h>
#include <PR/os_thread.h>

#include "../fast3d/glad/glad.h"
#include "../fast3d/gfx_pc.h"
#include "bss.h"
#include "data.h"
#undef bool
#undef true
#undef false
#include "ext_tex.h"
#include "fs.h"
#include "game/modeldef.h"
#include "imgui_overlay.h"
#include "imgui_skinmatch.h"
#include "input.h"
#include "mod.h"
#include "romdata.h"
#include "asset.h"
#include "system.h"
#include "lib/profile.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

static bool g_ImGuiOverlayInitialized = false;
static bool g_ImGuiOverlayVisible = false;
static bool g_ImGuiOverlayRestoreMouseLock = false;
static bool g_ImGuiOverlayShowRuntime = false;
static bool g_ImGuiOverlayShowStage = false;
static bool g_ImGuiOverlayShowEntities = false;
static bool g_ImGuiOverlayShowAssets = false;
static bool g_ImGuiOverlayShowMemory = false;
static bool g_ImGuiOverlayShowProfiler = false;
static bool g_ImGuiOverlayShowTextures = false;
static bool g_ImGuiOverlayShowSkinMatch = false;
// The chr the skin match panel edits. Separate from g_ImGuiOverlayFocusChr,
// which is a one-shot scroll target the Characters list clears as soon as it
// has scrolled to it - a panel reading it saw NULL on the very next frame.
static struct chrdata *g_ImGuiOverlaySkinChr = NULL;
static bool g_ImGuiOverlayShowLookingAt = false;
static bool g_ImGuiOverlayShowProportions = false;
static bool g_ImGuiOverlayShowStance = false;
static bool g_ImGuiOverlayShowPauseBlur = false;
static bool g_ImGuiOverlayShowMenuBg = false;
static bool g_ImGuiOverlayShowAudio = false;
static bool g_ImGuiOverlayShowLua = false;
static bool g_ImGuiOverlayShowSaves = false;
static struct chrdata *g_ImGuiPropChr = NULL;
static s32 g_ImGuiPropChrnum = -1;
static bool g_ImGuiPropApply = true;
static bool g_ImGuiPropLinkScale = true;

// Concept art is drawn at its own scale, and it is never the engine's: a chart
// where the lead is 6'4" has to land somewhere in a game whose tallest body is
// 5'11.7". This is the ratio between the two -- engine centimetres per chart
// centimetre -- so a height can be read in the units it was designed in and
// typed in the units the engine wants. 1.0 means the chart is already engine
// scale. It persists, because it is a property of a project rather than a view.
static f32 g_ImGuiPropLoreScale = 1.0f;
static bool g_ImGuiPropTypeValues = false;

// A hitpart's joint, when the model's own tree gives the wrong answer.
// HITPART_ values run to 201, so the array is indexed by hitpart directly and
// -1 means "believe the model". Cleared on every latch, because it is a fact
// about one body rather than about the game.
#define kFojoMaxHitPart 210
static s16 g_ImGuiPropPartJoint[kFojoMaxHitPart];
static bool g_ImGuiPropMirror = true;
static bool g_ImGuiPropDrawBox = true;
static s32 g_ImGuiPropMarkJoint = -1;
static f32 g_ImGuiPropHeight = 0.0f;
static f32 g_ImGuiPropScale = 1.0f;
static s32 g_ImGuiPropBaseHeight = 0;
static f32 g_ImGuiPropBaseScale = 1.0f;
static bool g_ImGuiOverlayShowActivePropsOnly = true;
static bool g_ImGuiOverlayExpandLatch = false;
static bool g_ImGuiOverlayExpandValue = false;
static struct prop *g_ImGuiOverlayFocusProp = NULL;
static struct chrdata *g_ImGuiOverlayFocusChr = NULL;
static s32 g_ImGuiOverlaySlotMod = -1;
static s32 g_ImGuiOverlayPropFilter = 0;
static s32 g_ImGuiOverlayObjFilter = 0;
static s32 g_ImGuiOverlayTextureProbeId = 0;
static s32 g_ImGuiOverlayTextureModelMod = -1;
static s32 g_ImGuiOverlayTextureModelFileNum = -1;
static GLuint g_ImGuiOverlayTexturePreview = 0;
static const u8 *g_ImGuiOverlayTexturePreviewPixels = NULL;
static std::vector<u8> g_ImGuiOverlayTexturePreviewPixelStorage;
static s32 g_ImGuiOverlayTexturePreviewModelFileNum = -1;
static s32 g_ImGuiOverlayTexturePreviewTexId = -1;
static u32 g_ImGuiOverlayTexturePreviewWidth = 0;
static u32 g_ImGuiOverlayTexturePreviewHeight = 0;
static s32 g_ImGuiOverlayTexturePreviewZoom = 4;
static s32 g_ImGuiOverlayRenderedTextureZoom = 4;
static bool g_ImGuiOverlayRenderedTextureFlipY = true;
static std::vector<u8> g_ImGuiOverlayRenderedTexturePixels;
static s32 g_ImGuiOverlayRenderedPixelsModelFileNum = -1;
static s32 g_ImGuiOverlayRenderedPixelsTexId = -1;
static u32 g_ImGuiOverlayRenderedPixelsWidth = 0;
static u32 g_ImGuiOverlayRenderedPixelsHeight = 0;
static std::vector<u8> g_ImGuiOverlayReferenceTexturePixels;
static u32 g_ImGuiOverlayReferencePixelsWidth = 0;
static u32 g_ImGuiOverlayReferencePixelsHeight = 0;
static s32 g_ImGuiOverlayReferenceModelFileNum = -1;
static s32 g_ImGuiOverlayReferenceTexId = -1;
static char g_ImGuiOverlayReferenceModelName[128];
static u64 g_ImGuiOverlayNativeCompareDifferentPixels = 0;
static u64 g_ImGuiOverlayNativeCompareChannelDelta = 0;
static u32 g_ImGuiOverlayNativeCompareMaxDelta = 0;
static bool g_ImGuiOverlayNativeCompareValid = false;
static char g_ImGuiOverlayTextureExportStatus[FS_MAXPATH + 64];
static u64 g_ImGuiOverlayTextureCompareDifferentPixels = 0;
static u64 g_ImGuiOverlayTextureCompareChannelDelta = 0;
static u32 g_ImGuiOverlayTextureCompareMaxDelta = 0;
static bool g_ImGuiOverlayTextureCompareValid = false;
alignas(16) static u8 g_ImGuiOverlayTextureProbePoolData[64 * 1024];
static struct texpool g_ImGuiOverlayTextureProbePool;
static Gfx g_ImGuiOverlayTextureProbeGdl[64];
static const u8 *g_ImGuiOverlayTextureProbeData = NULL;
static s32 g_ImGuiOverlayEngineProbeModelFileNum = -1;
static s32 g_ImGuiOverlayEngineProbeTexId = -1;
static bool g_ImGuiOverlayEngineProbeAttempted = false;
static bool g_ImGuiOverlayEngineProbeDecoded = false;
static bool g_ImGuiOverlayEngineProbeMetadataValid = false;
static u32 g_ImGuiOverlayEngineProbeCompressedSize = 0;
static u32 g_ImGuiOverlayEngineProbeDecodedSize = 0;
static u8 g_ImGuiOverlayEngineProbeHeader = 0;
static u8 g_ImGuiOverlayEngineProbeNativeFormat = 0xff;
static bool g_ImGuiOverlayEngineProbeHeaderAvailable = false;
static u8 g_ImGuiOverlayEngineProbeWidth = 0;
static u8 g_ImGuiOverlayEngineProbeHeight = 0;
static u8 g_ImGuiOverlayEngineProbeFormat = 0;
static u8 g_ImGuiOverlayEngineProbeDepth = 0;
static u8 g_ImGuiOverlayEngineProbeLutMode = 0;
static u16 g_ImGuiOverlayEngineProbePaletteCount = 0;
static u8 g_ImGuiOverlayEngineProbeLodCount = 0;
static bool g_ImGuiOverlayEngineProbeHasLodData = false;
static struct modeldefTextureUsage g_ImGuiOverlayTextureUsage[64];
static s32 g_ImGuiOverlayTextureUsageCount = 0;
static s32 g_ImGuiOverlayTextureUsageTotal = 0;
static struct modeldefTextureTriangle g_ImGuiOverlayTextureTriangles[512];
static s32 g_ImGuiOverlayTextureTriangleCount = 0;
static s32 g_ImGuiOverlayTextureTriangleTotal = 0;
static s32 g_ImGuiOverlayTextureUsageModelFileNum = -1;
static s32 g_ImGuiOverlayTextureUsageLocalId = -1;
static s32 g_ImGuiOverlayTextureUsagePortId = -1;
static bool g_ImGuiOverlayShowTextureUvOverlay = true;
static bool g_ImGuiOverlayWrapTextureUvs = true;
static u16 g_ImGuiOverlayModelTextureIds[512];
static s32 g_ImGuiOverlayModelTextureIdCount = 0;
static s32 g_ImGuiOverlayModelTextureIdTotal = 0;
static s32 g_ImGuiOverlayScannedTextureModelMod = -1;
static s32 g_ImGuiOverlayScannedTextureModelFileNum = -1;
static ImGuiTextFilter g_ImGuiOverlayPropTextFilter;
static ImGuiTextFilter g_ImGuiOverlayChrTextFilter;
static ImGuiTextFilter g_ImGuiOverlaySlotFilter;
static ImGuiTextFilter g_ImGuiOverlayModelFilter;
static char g_ImGuiOverlayIniPath[FS_MAXPATH + 1];

extern s32 g_StageNum;
extern s32 g_ModNum;
extern u32 g_OsMemSize;
extern s32 g_StageIndex;
// Size deliberately unspecified: data.h is the one declaration that carries
// the row count, and a second copy of the number here went stale the first
// time the table grew.
extern struct stagetableentry g_Stages[];
extern "C" s32 g_BgRoomsLoaded;
extern "C" s32 g_BgRoomsRefused;

// Mirrors the g_Stages length in src/include/data.h. Kept in step by hand,
// like kFojoMaxJointOverrides. The literal here was 87 and went stale the day
// the table grew to 119, which silently hid the Setup line for every stage on
// a new row.
static const s32 kFojoStageTableLen = 119;
extern "C" u32 mempGetStageFree(void);
extern "C" bool bgTestHitInRoom(struct coord *frompos, struct coord *topos, s32 roomnum, struct hitthing *hitthing);
extern "C" struct prop *propFindAimingAt(s32 handnum, bool isshooting, u32 context);
extern "C" void portal00018148(struct coord *pos, struct coord *pos2, RoomNum *rooms, RoomNum *arg3, RoomNum *arg4, s32 arg5);
extern "C" void texInitPool(struct texpool *pool, u8 *start, s32 len);
extern "C" void texLoad(texnum_t *updateword, struct texpool *pool, bool unusedarg);
extern "C" struct tex *texFindInPool(s32 texturenum, struct texpool *pool);
extern "C" Gfx *texBuildDebugLoadGdl(Gfx *gdl, struct tex *tex);
extern "C" void cam0f0b4eb8(struct coord *arg0, f32 arg1[2], f32 zoom, f32 aspect);
extern "C" void mtx4TransformVecInPlace(Mtxf *mtx, struct coord *vec);
extern "C" s16 viGetWidth(void);
extern "C" s16 viGetHeight(void);
extern "C" s32 texGetSizeInBytes(struct tex *tex, s32 lod);

// fojo audio panel. every naudio reach lives behind snddebug* in src/lib/snd.c
// so this file does not have to include PR/n_libaudio.h.
extern "C" s32 snddebugNumSlots(void);
extern "C" bool snddebugGetSlot(s32 slot, s32 *tracktype, s32 *tracknum, s32 *volume, s32 *state);
extern "C" s32 snddebugGetUspt(s32 slot);
extern "C" void snddebugSetUspt(s32 slot, s32 uspt);
extern "C" u16 snddebugGetChanMask(s32 slot);
extern "C" void snddebugSetChanMask(s32 slot, u16 mask);
extern "C" void snddebugSetChanVolume(s32 slot, s32 chan, s32 volume, s32 rate);
extern "C" s32 snddebugGetChanVolume(s32 slot, s32 chan);
extern "C" f32 snddebugGetWetBias(s32 slot);
extern "C" void snddebugSetWetBias(s32 slot, f32 bias);
extern "C" f32 snddebugGetWetScale(s32 slot);
extern "C" void snddebugSetWetScale(s32 slot, f32 scale);
extern "C" bool snddebugSetFxParam(s32 bus, s32 section, s32 param, s32 value);
extern "C" void snddebugSetVoiceCap(s32 slot, s32 cap);
extern "C" s32 snddebugCountSfxVoices(s32 *numfree, s32 *numalloced);

// Pool sizes, as they came up at boot. Declared here rather than by including
// lib/snd.h, same as everything else this file reaches for -- the game headers
// carry no __cplusplus guard.
struct snddebugpools {
	s32 heapused;
	s32 heaptotal;
	s32 pvoices;
	s32 vvoices;
	s32 seqbuffer;
	s32 acmdlen;
};
extern "C" void snddebugGetPools(struct snddebugpools *out);

// The cue scheduler. Same hand-declared shape as everything else this file
// reaches for -- the game headers carry no __cplusplus guard.
struct sndcuestate {
	s32 nowtick;
	s32 tickspq;
	s32 pending;
	s32 attick;
	s32 action;
	s32 arg;
	s32 ducked;
	s32 holdleft;
	s32 lastsfx;
	s32 mask;
};
extern "C" void sndcueGetState(struct sndcuestate *out);
extern "C" void sndcueTrigger(void);
extern "C" s32 g_SndCueEnabled;
extern "C" s32 g_SndCueQuantise;
extern "C" s32 g_SndCueBeatsPerBar;
extern "C" s32 g_SndCueSlot;
extern "C" s32 g_SndCueTriggerSfx;
extern "C" s32 g_SndCueMaskFull;
extern "C" s32 g_SndCueMaskDucked;
extern "C" s32 g_SndCueHoldTicks;
extern "C" u16 snddebugGetSfxVolume(void);
extern "C" void sndSetSfxVolume(u16 volume);
extern "C" void musicSetVolume(u16 volume);
extern "C" u16 musicGetVolume(void);
extern "C" u16 musicGetMenuVolume(void);
extern "C" s32 g_MusicMenuVolumeDivisor;
extern "C" void modelSetScale(struct model *model, f32 scale);
extern "C" void playerSetHeight(s32 eyeheight, s32 headnum);

// The stance knobs, defined in src/game/stancetuning.c. Declared by hand for
// the same reason as everything above: the game headers are not extern "C"
// wrapped. stance-tuning.md says what each one does.
extern "C" s32 g_FojoMovement;
extern "C" f32 g_AimStanceSpeed;
extern "C" f32 g_FlinchSpeed;
extern "C" s32 g_FlinchBusy;
extern "C" s32 g_FlinchBusyMax;
extern "C" f32 g_MeleeBodyReach;
extern "C" f32 g_MeleeConeCos;
extern "C" f32 g_ThirdPersonCamDist;
extern "C" f32 g_ThirdPersonCamClearance;
extern "C" f32 g_ThirdPersonCamMinDist;
extern "C" f32 g_ThirdPersonCamSide;
extern "C" f32 g_ThirdPersonCamForward;
extern "C" f32 g_ThirdPersonCamHeight;
extern "C" s32 g_ThirdPersonCamTether;
extern "C" f32 g_TetherBodyTurnSpeed;
extern "C" f32 g_BodyFadeStart;
extern "C" f32 g_BodyFadeFloor;
extern "C" s32 g_AnimSplitLowerMask;
extern "C" f32 g_ReloadSpeed;
extern "C" s32 g_ReloadAnimEnabled;
extern "C" f32 g_ReloadAnimSpeed;
extern "C" f32 g_RollImpulse;
extern "C" s32 g_BlurDoseEnabled;
extern "C" f32 g_BlurDoseFullSecs;
extern "C" f32 g_BlurDoseK;
extern "C" struct menubgstars g_MenuBgSlowStars;
extern "C" const struct menubgstars g_MenuBgSlowStarsDefaults;
extern "C" void menugfxResetBgSuccessSlow(void);
extern "C" s32 g_BuildSpeedEnabled;
extern "C" f32 g_BuildSpeedRef;
extern "C" f32 g_BuildCrouchMix;
extern "C" void stanceTuningReset(void);

// Lua scripting. The switch is in src/game/luaai.c, the readout flags in
// src/game/chaosstate.c, the rest in src/game/luaai_api_menus.c. Declared by
// hand for the same reason as everything above.
extern "C" s32 g_LuaAiEnabled;
extern "C" s32 g_LuaShowFps;
extern "C" s32 g_LuaShowMem;
extern "C" struct lua_State *luaaiGetState(void);
extern "C" s32 luaMenuCount(void);
extern "C" void luaaiReload(void);
extern "C" s32 luaMenusRunString(const char *src, char *out, u32 outlen);

// Reload and run are asked for while the panel draws and done once the
// overlay has rendered, between two game frames.
static bool g_ImGuiLuaReloadPending = false;
static bool g_ImGuiLuaRunPending = false;
static char g_ImGuiLuaInput[256];
static char g_ImGuiLuaResult[512];
static bool g_ImGuiLuaResultOk = true;

// The flush is asked for while the panel draws and done once the overlay has
// rendered, between two game frames - a pak write has no business running
// half way through a draw list.
static bool g_ImGuiSavesFlushPending = false;
static char g_ImGuiSavesResult[512];
static bool g_ImGuiSavesResultOk = true;

// Save and profile state, defined in src/game/mplayer/mplayer.c, src/game/pak.c
// and port/src/config.c. Declared by hand for the same reason as everything
// above: those headers are not extern "C"-wrapped.
extern "C" struct extplayerprofile g_ExtendedProfiles[];
extern "C" s32 saveQueueIsDirty(void);
extern "C" u32 saveQueueFramesPending(void);
extern "C" u32 saveQueueDeadlineFrames(void);
extern "C" u32 saveQueueFlushCount(void);
extern "C" void saveQueueFlush(void);
extern "C" s32 mpProfileDebugPropCount(void);
extern "C" const char *mpProfileDebugPropName(s32 propindex);
extern "C" bool mpProfileDebugPropIsS32(s32 propindex);
extern "C" void mpProfileDebugSlug(s32 profileindex, char *out, s32 outlen);
extern "C" void mpProfileFlushSlotHashes(s32 playernum);
extern "C" s32 mpplayerfileSave(s32 playernum, s32 device, s32 fileid, u16 deviceserial);
extern "C" void iniProcessPendingProfiles(void);
extern "C" s8 pakFindBySerial(s32 deviceserial);

// Mirrors MOD_MAX_PERSISTABLE_SLOT, which is private to port/src/mod.c. Kept
// in step by hand, like kFojoMaxJointOverrides below.
static const s32 kFojoMaxPersistableSlot = 127;

// A synthetic guid: iniBindProfileProperties hands one of these to a player
// with no controller pak file, so its settings still get an ini section.
static const u16 kFojoLocalDeviceSerial = 0xFFFF;

// Proportion editor overrides, defined in src/game/chr.c. Declared by hand
// rather than included, for the same reason as everything above: the game
// headers are not extern "C"-wrapped.
// The engine's own node-to-matrix resolver. Declared by hand for the same
// reason as everything above: the game headers are not extern "C" wrapped.
extern "C" s32 modelFindNodeMtxIndex(struct modelnode *node, s32 arg1);

extern "C" struct chrdata *g_JointScaleChr;
extern "C" f32 g_JointScaleOverride[][3];

// Mirrors MAX_JOINT_OVERRIDES in src/include/game/chr.h. Kept in step by
// hand; the panel never indexes past it.
static const s32 kFojoMaxJointOverrides = 40;

static bool imguiOverlayHasRomTexture(u16 textureId);
static void imguiPropLatch(struct chrdata *chr);
static bool imguiOverlayChrIsCurrent(struct chrdata *chr);
static void imguiOverlayFocusChr(struct chrdata *chr);
static void imguiOverlayFocusProp(struct prop *prop);
static void imguiOverlayBringFlagToCurrentWorkspace(bool *open);

#define CASE_NAME(x) case x: return #x;

static const char *imguiOverlayActionName(s8 action)
{
	switch (action) {
	CASE_NAME(ACT_INIT)
	CASE_NAME(ACT_STAND)
	CASE_NAME(ACT_KNEEL)
	CASE_NAME(ACT_ANIM)
	CASE_NAME(ACT_DIE)
	CASE_NAME(ACT_DEAD)
	CASE_NAME(ACT_ARGH)
	CASE_NAME(ACT_PREARGH)
	CASE_NAME(ACT_ATTACK)
	CASE_NAME(ACT_ATTACKWALK)
	CASE_NAME(ACT_ATTACKROLL)
	CASE_NAME(ACT_SIDESTEP)
	CASE_NAME(ACT_JUMPOUT)
	CASE_NAME(ACT_RUNPOS)
	CASE_NAME(ACT_PATROL)
	CASE_NAME(ACT_GOPOS)
	CASE_NAME(ACT_SURRENDER)
	CASE_NAME(ACT_LOOKATTARGET)
	CASE_NAME(ACT_SURPRISED)
	CASE_NAME(ACT_STARTALARM)
	CASE_NAME(ACT_THROWGRENADE)
	CASE_NAME(ACT_TURNDIR)
	CASE_NAME(ACT_TEST)
	CASE_NAME(ACT_BONDINTRO)
	CASE_NAME(ACT_BONDDIE)
	CASE_NAME(ACT_BONDMULTI)
	CASE_NAME(ACT_NULL)
	CASE_NAME(ACT_BOT_ATTACKSTAND)
	CASE_NAME(ACT_BOT_ATTACKKNEEL)
	CASE_NAME(ACT_BOT_ATTACKSTRAFE)
	CASE_NAME(ACT_DRUGGEDDROP)
	CASE_NAME(ACT_DRUGGEDKO)
	CASE_NAME(ACT_DRUGGEDCOMINGUP)
	CASE_NAME(ACT_ATTACKAMOUNT)
	CASE_NAME(ACT_ROBOTATTACK)
	CASE_NAME(ACT_SKJUMP)
	CASE_NAME(ACT_PUNCH)
	CASE_NAME(ACT_CUTFIRE)
	default: return "ACT";
	}
}

static const char *imguiOverlayPropTypeName(u8 type)
{
	switch (type) {
	CASE_NAME(PROPTYPE_OBJ)
	CASE_NAME(PROPTYPE_DOOR)
	CASE_NAME(PROPTYPE_CHR)
	CASE_NAME(PROPTYPE_WEAPON)
	CASE_NAME(PROPTYPE_EYESPY)
	CASE_NAME(PROPTYPE_PLAYER)
	CASE_NAME(PROPTYPE_EXPLOSION)
	CASE_NAME(PROPTYPE_SMOKE)
	default: return "Prop";
	}
}

static const char *imguiOverlayObjTypeName(u8 type)
{
	switch (type) {
	CASE_NAME(OBJTYPE_DOOR)
	CASE_NAME(OBJTYPE_DOORSCALE)
	CASE_NAME(OBJTYPE_BASIC)
	CASE_NAME(OBJTYPE_KEY)
	CASE_NAME(OBJTYPE_ALARM)
	CASE_NAME(OBJTYPE_CCTV)
	CASE_NAME(OBJTYPE_AMMOCRATE)
	CASE_NAME(OBJTYPE_WEAPON)
	CASE_NAME(OBJTYPE_CHR)
	CASE_NAME(OBJTYPE_SINGLEMONITOR)
	CASE_NAME(OBJTYPE_MULTIMONITOR)
	CASE_NAME(OBJTYPE_HANGINGMONITORS)
	CASE_NAME(OBJTYPE_AUTOGUN)
	CASE_NAME(OBJTYPE_LINKGUNS)
	CASE_NAME(OBJTYPE_DEBRIS)
	CASE_NAME(OBJTYPE_10)
	CASE_NAME(OBJTYPE_HAT)
	CASE_NAME(OBJTYPE_GRENADEPROB)
	CASE_NAME(OBJTYPE_LINKLIFTDOOR)
	CASE_NAME(OBJTYPE_MULTIAMMOCRATE)
	CASE_NAME(OBJTYPE_SHIELD)
	CASE_NAME(OBJTYPE_TAG)
	CASE_NAME(OBJTYPE_BEGINOBJECTIVE)
	CASE_NAME(OBJTYPE_ENDOBJECTIVE)
	CASE_NAME(OBJECTIVETYPE_DESTROYOBJ)
	CASE_NAME(OBJECTIVETYPE_COMPFLAGS)
	CASE_NAME(OBJECTIVETYPE_FAILFLAGS)
	CASE_NAME(OBJECTIVETYPE_COLLECTOBJ)
	CASE_NAME(OBJECTIVETYPE_THROWOBJ)
	CASE_NAME(OBJECTIVETYPE_HOLOGRAPH)
	CASE_NAME(OBJECTIVETYPE_1F)
	CASE_NAME(OBJECTIVETYPE_ENTERROOM)
	CASE_NAME(OBJECTIVETYPE_THROWINROOM)
	CASE_NAME(OBJTYPE_22)
	CASE_NAME(OBJTYPE_BRIEFING)
	CASE_NAME(OBJTYPE_GASBOTTLE)
	CASE_NAME(OBJTYPE_RENAMEOBJ)
	CASE_NAME(OBJTYPE_PADLOCKEDDOOR)
	CASE_NAME(OBJTYPE_TRUCK)
	CASE_NAME(OBJTYPE_HELI)
	CASE_NAME(OBJTYPE_29)
	CASE_NAME(OBJTYPE_GLASS)
	CASE_NAME(OBJTYPE_SAFE)
	CASE_NAME(OBJTYPE_SAFEITEM)
	CASE_NAME(OBJTYPE_TANK)
	CASE_NAME(OBJTYPE_CAMERAPOS)
	CASE_NAME(OBJTYPE_TINTEDGLASS)
	CASE_NAME(OBJTYPE_LIFT)
	CASE_NAME(OBJTYPE_CONDITIONALSCENERY)
	CASE_NAME(OBJTYPE_BLOCKEDPATH)
	CASE_NAME(OBJTYPE_HOVERBIKE)
	CASE_NAME(OBJTYPE_END)
	CASE_NAME(OBJTYPE_HOVERPROP)
	CASE_NAME(OBJTYPE_FAN)
	CASE_NAME(OBJTYPE_HOVERCAR)
	CASE_NAME(OBJTYPE_PADEFFECT)
	CASE_NAME(OBJTYPE_CHOPPER)
	CASE_NAME(OBJTYPE_MINE)
	CASE_NAME(OBJTYPE_ESCASTEP)
	default: return "Obj";
	}
}

#undef CASE_NAME

static const char *imguiOverlayCoordString(const struct coord *coord)
{
	static char buffers[4][64];
	static u32 index = 0;
	char *buffer = buffers[index++ % 4];
	snprintf(buffer, 64, "(%.2f, %.2f, %.2f)", coord->x, coord->y, coord->z);
	return buffer;
}

static const char *imguiOverlayRoomListString(RoomNum *rooms, s32 maxRooms)
{
	static char buffer[96];
	s32 offset = 0;
	buffer[0] = '\0';

	for (s32 i = 0; i < maxRooms && rooms[i] >= 0 && offset < (s32)sizeof(buffer); ++i) {
		offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%03x ", rooms[i]);
	}

	return buffer[0] ? buffer : "none";
}

static void imguiOverlayDescribeProp(struct prop *prop);

static const char *imguiOverlayHeadBodyName(s32 index)
{
	const char *name = modGetNameForHeadBodyIndex(index);
	return name ? name : "unknown";
}

static bool imguiOverlayPropIsCurrent(struct prop *prop)
{
	return prop && g_Vars.props && g_Vars.maxprops > 0
		&& prop >= g_Vars.props && prop < g_Vars.props + g_Vars.maxprops;
}

static bool imguiOverlayChrIsCurrent(struct chrdata *chr)
{
	return chr && g_ChrSlots && g_NumChrSlots > 0
		&& chr >= g_ChrSlots && chr < g_ChrSlots + g_NumChrSlots && chr->chrnum >= 0;
}

static void imguiOverlayFocusProp(struct prop *prop)
{
	if (!imguiOverlayPropIsCurrent(prop)) {
		return;
	}

	g_ImGuiOverlayFocusProp = prop;
	g_ImGuiOverlayShowActivePropsOnly = false;
	g_ImGuiOverlayPropFilter = 0;
	g_ImGuiOverlayObjFilter = 0;
	g_ImGuiOverlayPropTextFilter.Clear();
}

static void imguiOverlayFocusChr(struct chrdata *chr)
{
	if (!imguiOverlayChrIsCurrent(chr)) {
		return;
	}

	g_ImGuiOverlayFocusChr = chr;
	g_ImGuiOverlayChrTextFilter.Clear();
}

static void imguiOverlayFocusTextureId(s32 textureId)
{
	if (textureId < 0 || textureId > 0xffff) {
		return;
	}

	g_ImGuiOverlayTextureProbeId = textureId;
	g_ImGuiOverlayShowTextures = true;
}

static void imguiOverlayPropJumpLine(const char *label, struct prop *prop)
{
	char text[96];
	if (!imguiOverlayPropIsCurrent(prop)) {
		return;
	}

	snprintf(text, sizeof(text), "%s: %p", label, prop);
	if (ImGui::Selectable(text, false, ImGuiSelectableFlags_AllowDoubleClick)
			&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		imguiOverlayFocusProp(prop);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Double-click to show this prop in Props");
	}
}

static void imguiOverlayChrJumpLine(const char *label, struct chrdata *chr)
{
	char text[96];
	if (!imguiOverlayChrIsCurrent(chr)) {
		return;
	}

	snprintf(text, sizeof(text), "%s: %p", label, chr);
	if (ImGui::Selectable(text, false, ImGuiSelectableFlags_AllowDoubleClick)
			&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		imguiOverlayFocusChr(chr);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Double-click to show this character in Characters");
	}
}

static void imguiOverlayDescribeProjectile(struct projectile *projectile)
{
	ImGui::Text("Drop type: 0x%04x", (u16)projectile->droptype);
	ImGui::Text("Flags: 0x%08x", projectile->flags);
	ImGui::Text("Velocity: %s", imguiOverlayCoordString(&projectile->speed));
	ImGui::Text("Flight time: %d", projectile->flighttime240);
}

static void imguiOverlayDescribeObj(struct defaultobj *obj)
{
	ImGui::Text("Type: %s (0x%02x)", imguiOverlayObjTypeName(obj->type), obj->type);
	ImGui::Text("Model: 0x%04x", (u16)obj->modelnum);
	ImGui::Text("Pad: 0x%04x", (u16)obj->pad);
	ImGui::Text("Flags 1: 0x%08x", obj->flags);
	ImGui::Text("Flags 2: 0x%08x", obj->flags2);
	ImGui::Text("Flags 3: 0x%08x", obj->flags3);
	ImGui::Text("Hidden 1: 0x%08x", obj->hidden);
	ImGui::Text("Hidden 2: 0x%02x", obj->hidden2);
	ImGui::Text("Damage: %d/%d", obj->damage, obj->maxdamage);

	if ((obj->hidden & OBJHFLAG_PROJECTILE) && obj->projectile) {
		if (ImGui::TreeNode(obj->projectile, "Projectile (%p)", obj->projectile)) {
			imguiOverlayDescribeProjectile(obj->projectile);
			ImGui::TreePop();
		}
	}
}

static void imguiOverlayDescribeWeapon(struct weaponobj *weapon)
{
	ImGui::Text("Gun num: 0x%02x", weapon->weaponnum);
	ImGui::Text("Gun func: 0x%02x", weapon->gunfunc);
	ImGui::Text("Fade out timer: %d", weapon->fadeouttimer60);
	ImGui::Text("Timer: %d", weapon->timer240);
}

static void imguiOverlayDescribeHand(struct hand *hand)
{
	ImGui::Text("Gun num: 0x%02x", hand->gset.weaponnum);
	ImGui::Text("Gun func: 0x%02x", hand->gset.weaponfunc);
	ImGui::Text("Mode: 0x%02x -> 0x%02x", hand->mode, hand->modenext);
	ImGui::Text("In use: %s", hand->inuse ? "yes" : "no");
	ImGui::Text("Firing: %s", hand->firing ? "yes" : "no");
	ImGui::Text("Aim pos: %s", imguiOverlayCoordString(&hand->aimpos));
	ImGui::Text("Hit pos: %s", imguiOverlayCoordString(&hand->hitpos));
}

static bool imguiOverlayPlayerIsCurrent(struct player *player)
{
	if (!player) {
		return false;
	}

	for (s32 i = 0; i < MAX_PLAYERS; ++i) {
		if (g_Vars.players[i] == player) {
			return true;
		}
	}

	return false;
}

static bool imguiOverlayCanAimInspect(void)
{
	return g_Vars.currentplayer
		&& g_Vars.tickmode == TICKMODE_NORMAL
		&& imguiOverlayPlayerIsCurrent(g_Vars.currentplayer)
		&& imguiOverlayPropIsCurrent(g_Vars.currentplayer->prop)
		&& g_Rooms
		&& g_Vars.roomcount > 0
		&& g_Vars.currentplayer->cam_room >= 0
		&& g_Vars.currentplayer->cam_room < g_Vars.roomcount;
}

static void imguiOverlayDescribePlayer(struct player *player)
{
	if (!imguiOverlayPlayerIsCurrent(player)) {
		ImGui::TextUnformatted("Player is no longer available");
		return;
	}

	ImGui::Text("Camera pos: %s", imguiOverlayCoordString(&player->cam_pos));
	ImGui::Text("Camera look: %s", imguiOverlayCoordString(&player->cam_look));
	ImGui::Text("Camera room: 0x%03x", (u32)player->cam_room);
	ImGui::Text("Health: %.3f", player->bondhealth);
	ImGui::Text("Shield: %.3f", player->apparentarmour);
	ImGui::Text("Dead: %s", player->isdead ? "yes" : "no");

	if (imguiOverlayPropIsCurrent(player->prop)) {
		ImGui::Separator();
		imguiOverlayPropJumpLine("Prop", player->prop);
		if (ImGui::TreeNode(player->prop, "Prop details (%p)", player->prop)) {
			imguiOverlayDescribeProp(player->prop);
			ImGui::TreePop();
		}
	}

	ImGui::Separator();
	if (ImGui::TreeNode("Right hand")) {
		imguiOverlayDescribeHand(&player->hands[HAND_RIGHT]);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Left hand")) {
		imguiOverlayDescribeHand(&player->hands[HAND_LEFT]);
		ImGui::TreePop();
	}
}

static void imguiOverlayDescribeChr(struct chrdata *chr)
{
	if (!imguiOverlayChrIsCurrent(chr)) {
		ImGui::TextUnformatted("Character is no longer available");
		return;
	}

	ImGui::Text("Number: 0x%04x", (u16)chr->chrnum);
	ImGui::Text("Body: 0x%04x %s", (u16)chr->bodynum, imguiOverlayHeadBodyName(chr->bodynum));
	ImGui::Text("Head: 0x%04x %s", (u16)chr->headnum, imguiOverlayHeadBodyName(chr->headnum));
	ImGui::Text("Team: 0x%02x", chr->team);
	ImGui::Text("Tude: 0x%02x", chr->tude);
	ImGui::Text("Action: %s (0x%02x)", imguiOverlayActionName(chr->actiontype), (u8)chr->actiontype);
	ImGui::Text("Damage: %.3f", chr->damage);
	ImGui::Text("Shield: %.3f", chr->cshield);
	ImGui::Text("Flags 1: 0x%08x", chr->flags);
	ImGui::Text("Flags 2: 0x%08x", chr->flags2);
	ImGui::Text("Hidden 1: 0x%08x", chr->hidden);
	ImGui::Text("Hidden 2: 0x%04x", chr->hidden2);
	ImGui::Text("Chr flags: 0x%08x", chr->chrflags);

	for (s32 handIndex = 0; handIndex < 3; ++handIndex) {
		if (imguiOverlayPropIsCurrent(chr->weapons_held[handIndex])) {
			const char *label = handIndex == HAND_RIGHT ? "Right hand prop"
				: handIndex == HAND_LEFT ? "Left hand prop" : "Hat prop";
			if (g_ImGuiOverlayExpandLatch) {
				ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
			}
			const bool open = ImGui::TreeNode(chr->weapons_held[handIndex], "%s (%p)", label,
						chr->weapons_held[handIndex]);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				imguiOverlayFocusProp(chr->weapons_held[handIndex]);
			}
			if (open) {
				imguiOverlayDescribeProp(chr->weapons_held[handIndex]);
				ImGui::TreePop();
			}
		}
	}
}

static void imguiOverlayDescribeProp(struct prop *prop)
{
	if (!imguiOverlayPropIsCurrent(prop)) {
		ImGui::TextUnformatted("Prop is no longer available");
		return;
	}

	ImGui::TextUnformatted(prop->active ? "Active" : "Inactive");
	ImGui::Text("Type: %s (0x%02x)", imguiOverlayPropTypeName(prop->type), prop->type);
	ImGui::Text("Flags: 0x%02x", prop->flags);
	ImGui::Text("Position: %s", imguiOverlayCoordString(&prop->pos));
	ImGui::Text("Rooms: %s", imguiOverlayRoomListString(prop->rooms, 8));

	if (!prop->active) {
		return;
	}

	if ((prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR || prop->type == PROPTYPE_WEAPON) && prop->obj) {
		if (g_ImGuiOverlayExpandLatch) {
			ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
		}
		if (ImGui::TreeNode(prop->obj, "%s (%p)", imguiOverlayObjTypeName(prop->obj->type), prop->obj)) {
			imguiOverlayDescribeObj(prop->obj);
			ImGui::TreePop();
		}
	}

	if ((prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER || prop->type == PROPTYPE_EYESPY)
			&& imguiOverlayChrIsCurrent(prop->chr)) {
		if (g_ImGuiOverlayExpandLatch) {
			ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
		}
		imguiOverlayChrJumpLine("Chr", prop->chr);
		if (ImGui::TreeNode(prop->chr, "Chr (%p)", prop->chr)) {
			imguiOverlayDescribeChr(prop->chr);
			ImGui::TreePop();
		}
	} else if (prop->type == PROPTYPE_WEAPON && prop->weapon) {
		if (g_ImGuiOverlayExpandLatch) {
			ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
		}
		if (ImGui::TreeNode(prop->weapon, "Weapon (%p)", prop->weapon)) {
			imguiOverlayDescribeWeapon(prop->weapon);
			ImGui::TreePop();
		}
	}
}

static bool imguiOverlayPropPassesFilters(struct prop *prop)
{
	return imguiOverlayPropIsCurrent(prop)
		&& (!g_ImGuiOverlayPropFilter || prop->type == g_ImGuiOverlayPropFilter)
		&& (!g_ImGuiOverlayObjFilter || (prop->active && (prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR
				|| prop->type == PROPTYPE_WEAPON) && prop->obj && prop->obj->type == g_ImGuiOverlayObjFilter));
}

static bool imguiOverlayPropPassesTextFilter(struct prop *prop, s32 index)
{
	char text[256];
	const char *objTypeName = "";
	s32 objType = 0;
	s32 chrNum = -1;

	if (prop->active && (prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR || prop->type == PROPTYPE_WEAPON) && prop->obj) {
		objTypeName = imguiOverlayObjTypeName(prop->obj->type);
		objType = prop->obj->type;
	}

	if (prop->active && (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER || prop->type == PROPTYPE_EYESPY)
			&& imguiOverlayChrIsCurrent(prop->chr)) {
		chrNum = prop->chr->chrnum;
	}

	snprintf(text, sizeof(text), "%s %d %p type:%02x flags:%02x obj:%s objtype:%02x chr:%04x pos:%.2f %.2f %.2f rooms:%s",
			imguiOverlayPropTypeName(prop->type), index, prop, prop->type, prop->flags,
			objTypeName, objType, (u16)chrNum, prop->pos.x, prop->pos.y, prop->pos.z,
			imguiOverlayRoomListString(prop->rooms, 8));

	return g_ImGuiOverlayPropTextFilter.PassFilter(text);
}

static bool imguiOverlayChrPassesTextFilter(struct chrdata *chr, s32 index)
{
	char text[256];
	if (!imguiOverlayChrIsCurrent(chr)) {
		return false;
	}

	snprintf(text, sizeof(text), "slot:%d chr:%04x %p body:%04x %s head:%04x %s team:%02x tude:%02x action:%s actionid:%02x damage:%.3f shield:%.3f",
			index, (u16)chr->chrnum, chr, (u16)chr->bodynum, imguiOverlayHeadBodyName(chr->bodynum),
			(u16)chr->headnum, imguiOverlayHeadBodyName(chr->headnum),
			chr->team, chr->tude, imguiOverlayActionName(chr->actiontype),
			(u8)chr->actiontype, chr->damage, chr->cshield);

	return g_ImGuiOverlayChrTextFilter.PassFilter(text);
}


/**
 * Right-click menu shared by the Props and Characters lists.
 *
 * Attaches to whatever item was drawn last, so call it immediately after the
 * TreeNode and before its children. Either argument may be NULL; a prop that
 * carries a chr gets the character entries too.
 */
static void imguiOverlayDrawEntityContextMenu(struct prop *prop, struct chrdata *chr)
{
	char buf[64];

	if (!chr && prop && (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER)
			&& imguiOverlayChrIsCurrent(prop->chr)) {
		chr = prop->chr;
	}

	if (!ImGui::BeginPopupContextItem()) {
		return;
	}

	if (chr && imguiOverlayChrIsCurrent(chr)) {
		ImGui::SeparatorText("Character");

		// TODO(catherine): label is a placeholder.
		if (ImGui::MenuItem("Latch in proportions")) {
			imguiPropLatch(chr);
			g_ImGuiOverlayShowProportions = true;
		}

		if (ImGui::MenuItem("Show in Characters")) {
			imguiOverlayFocusChr(chr);
		}

		if (ImGui::MenuItem("Skin match this chr")) {
			g_ImGuiOverlaySkinChr = chr;
			g_ImGuiOverlayShowSkinMatch = true;
		}

		if (chr->prop && imguiOverlayPropIsCurrent(chr->prop)
				&& ImGui::MenuItem("Show its prop")) {
			imguiOverlayFocusProp(chr->prop);
		}

		if (ImGui::MenuItem("Copy chrnum")) {
			snprintf(buf, sizeof(buf), "0x%04x", (u32)(u16)chr->chrnum);
			ImGui::SetClipboardText(buf);
		}

		if (ImGui::MenuItem("Copy chr pointer")) {
			snprintf(buf, sizeof(buf), "%p", (void *)chr);
			ImGui::SetClipboardText(buf);
		}
	}

	if (prop && imguiOverlayPropIsCurrent(prop)) {
		ImGui::SeparatorText("Prop");

		if (ImGui::MenuItem("Show in Props")) {
			imguiOverlayFocusProp(prop);
		}

		if (ImGui::MenuItem("Copy prop pointer")) {
			snprintf(buf, sizeof(buf), "%p", (void *)prop);
			ImGui::SetClipboardText(buf);
		}
	}

	ImGui::EndPopup();
}

static void imguiOverlayDrawPropNode(struct prop *prop, s32 index)
{
	if (!imguiOverlayPropPassesFilters(prop) || !imguiOverlayPropPassesTextFilter(prop, index)) {
		return;
	}

	const bool focus = prop == g_ImGuiOverlayFocusProp;
	if (g_ImGuiOverlayExpandLatch || focus) {
		ImGui::SetNextItemOpen(focus ? true : g_ImGuiOverlayExpandValue);
	}

	const bool open = ImGui::TreeNode(prop, "%s %d (%p)",
			imguiOverlayPropTypeName(prop->type), index, prop);

	imguiOverlayDrawEntityContextMenu(prop, NULL);

	if (open) {
		if (focus) {
			ImGui::SetScrollHereY(0.25f);
			g_ImGuiOverlayFocusProp = NULL;
		}
		imguiOverlayDescribeProp(prop);
		ImGui::TreePop();
	}
	if (focus && g_ImGuiOverlayFocusProp) {
		ImGui::SetScrollHereY(0.25f);
		g_ImGuiOverlayFocusProp = NULL;
	}
}

static const char *imguiOverlayFileSourceName(s32 source)
{
	switch (source) {
	case 0: return "unloaded";
	case 1: return "rom";
	case 2: return "external";
	case 3: return "alt rom";
	default: return "unknown";
	}
}

static f32 imguiOverlayCyclesToMs(u32 cycles)
{
	return (f32)((double)cycles * 1000.0 / (double)OS_CPU_COUNTER);
}

static f32 imguiOverlayProfileSpanMs(const struct profileframerecord *record,
		enum profilemarkerslot start, enum profilemarkerslot end)
{
	const u32 mask = (1 << start) | (1 << end);
	if ((record->markermask & mask) != mask) {
		return 0.0f;
	}

	return imguiOverlayCyclesToMs(record->markers[end] - record->markers[start]);
}

static void imguiOverlayDrawProfilerGraph(f32 *frameTimes, s32 count, f32 maxMs)
{
	const f32 graphHeight = 150.0f;
	const f32 graphWidth = ImGui::GetContentRegionAvail().x;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 size(graphWidth > 1.0f ? graphWidth : 1.0f, graphHeight);
	ImDrawList *drawList = ImGui::GetWindowDrawList();
	const f32 scaleMax = maxMs > 33.33f ? maxMs : 33.33f;
	const f32 barWidth = size.x / (f32)(count > 0 ? count : 1);
	const f32 line16 = origin.y + size.y - (16.6667f / scaleMax) * size.y;
	const f32 line33 = origin.y + size.y - (33.3333f / scaleMax) * size.y;

	drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 15, 18, 190));
	drawList->AddLine(ImVec2(origin.x, line16), ImVec2(origin.x + size.x, line16), IM_COL32(70, 160, 90, 150));
	drawList->AddLine(ImVec2(origin.x, line33), ImVec2(origin.x + size.x, line33), IM_COL32(200, 140, 55, 150));

	for (s32 i = 0; i < count; ++i) {
		const f32 value = frameTimes[i] > scaleMax ? scaleMax : frameTimes[i];
		const f32 height = (value / scaleMax) * size.y;
		const f32 x0 = origin.x + i * barWidth;
		const f32 x1 = origin.x + (i + 1) * barWidth - 1.0f;
		const f32 y0 = origin.y + size.y - height;
		const ImU32 colour = frameTimes[i] > 33.3333f ? IM_COL32(220, 80, 70, 230)
			: frameTimes[i] > 16.6667f ? IM_COL32(220, 170, 60, 230)
			: IM_COL32(85, 185, 115, 230);
		drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1 > x0 ? x1 : x0 + 1.0f, origin.y + size.y), colour);
	}

	drawList->AddText(ImVec2(origin.x + 4.0f, line16 - 14.0f), IM_COL32(150, 220, 165, 220), "16.7 ms");
	drawList->AddText(ImVec2(origin.x + 4.0f, line33 - 14.0f), IM_COL32(235, 190, 120, 220), "33.3 ms");
	ImGui::Dummy(size);
}

struct imguiOverlayProfilerTopRow {
	const char *name;
	f32 latest;
	f32 avg;
	f32 max;
	f32 pct;
};

static void imguiOverlayAddProfilerTopRow(const struct imguiOverlayProfilerTopRow *row)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::TextUnformatted(row->name);
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%6.3f", row->latest);
	ImGui::TableSetColumnIndex(2);
	ImGui::Text("%6.3f", row->avg);
	ImGui::TableSetColumnIndex(3);
	ImGui::Text("%6.3f", row->max);
	ImGui::TableSetColumnIndex(4);
	ImGui::ProgressBar(row->pct / 100.0f, ImVec2(-1.0f, 0.0f), "");
	ImGui::TableSetColumnIndex(5);
	ImGui::Text("%5.1f", row->pct);
}

static void imguiOverlayDrawProfilerPanel(void)
{
	struct profileframerecord record;
	struct profileframerecord latest;
	const s32 count = profileGetFrameHistoryCount();
	f32 frameTimes[PROFILE_HISTORY_LEN];
	f32 latestSpans[4] = { 0.0f };
	f32 sumSpans[4] = { 0.0f };
	f32 maxSpans[4] = { 0.0f };
	s32 spanCounts[4] = { 0 };
	f32 minMs = 0.0f;
	f32 maxMs = 0.0f;
	f32 sumMs = 0.0f;
	s32 valid = 0;

	if (count <= 0) {
		ImGui::TextUnformatted("Waiting for frame samples...");
		return;
	}

	for (s32 i = 0; i < count; ++i) {
		profileGetFrameHistoryRecord(i, &record);
		const f32 frameMs = record.diffframe60f * (1000.0f / 60.0f);
		const f32 spans[4] = {
			imguiOverlayProfileSpanMs(&record, PROFILE_SLOT_MAINTICK_START, PROFILE_SLOT_MAINTICK_END),
			imguiOverlayProfileSpanMs(&record, PROFILE_SLOT_AUDIOFRAME_START, PROFILE_SLOT_AUDIOFRAME_END),
			imguiOverlayProfileSpanMs(&record, PROFILE_SLOT_RSP_START, PROFILE_SLOT_RSP_END),
			imguiOverlayProfileSpanMs(&record, PROFILE_SLOT_RDP_START, PROFILE_SLOT_RDP_END),
		};
		frameTimes[i] = frameMs;
		if (valid == 0 || frameMs < minMs) {
			minMs = frameMs;
		}
		if (valid == 0 || frameMs > maxMs) {
			maxMs = frameMs;
		}
		sumMs += frameMs;
		valid++;

		for (s32 span = 0; span < 4; ++span) {
			if (spans[span] > 0.0f) {
				sumSpans[span] += spans[span];
				if (spanCounts[span] == 0 || spans[span] > maxSpans[span]) {
					maxSpans[span] = spans[span];
				}
				spanCounts[span]++;
			}
		}
	}

	profileGetFrameHistoryRecord(count - 1, &latest);
	const f32 avgMs = valid > 0 ? sumMs / valid : 0.0f;
	const f32 latestMs = latest.diffframe60f * (1000.0f / 60.0f);
	latestSpans[0] = imguiOverlayProfileSpanMs(&latest, PROFILE_SLOT_MAINTICK_START, PROFILE_SLOT_MAINTICK_END);
	latestSpans[1] = imguiOverlayProfileSpanMs(&latest, PROFILE_SLOT_AUDIOFRAME_START, PROFILE_SLOT_AUDIOFRAME_END);
	latestSpans[2] = imguiOverlayProfileSpanMs(&latest, PROFILE_SLOT_RSP_START, PROFILE_SLOT_RSP_END);
	latestSpans[3] = imguiOverlayProfileSpanMs(&latest, PROFILE_SLOT_RDP_START, PROFILE_SLOT_RDP_END);
	struct imguiOverlayProfilerTopRow topRows[] = {
		{ "main", latestSpans[0], spanCounts[0] ? sumSpans[0] / spanCounts[0] : 0.0f, maxSpans[0], latestMs > 0.0f ? latestSpans[0] * 100.0f / latestMs : 0.0f },
		{ "audio", latestSpans[1], spanCounts[1] ? sumSpans[1] / spanCounts[1] : 0.0f, maxSpans[1], latestMs > 0.0f ? latestSpans[1] * 100.0f / latestMs : 0.0f },
		{ "rsp", latestSpans[2], spanCounts[2] ? sumSpans[2] / spanCounts[2] : 0.0f, maxSpans[2], latestMs > 0.0f ? latestSpans[2] * 100.0f / latestMs : 0.0f },
		{ "rdp", latestSpans[3], spanCounts[3] ? sumSpans[3] / spanCounts[3] : 0.0f, maxSpans[3], latestMs > 0.0f ? latestSpans[3] * 100.0f / latestMs : 0.0f },
	};

	for (s32 pass = 0; pass < (s32)ARRAYCOUNT(topRows) - 1; ++pass) {
		for (s32 i = 0; i < (s32)ARRAYCOUNT(topRows) - 1 - pass; ++i) {
			if (topRows[i].latest < topRows[i + 1].latest) {
				struct imguiOverlayProfilerTopRow tmp = topRows[i];
				topRows[i] = topRows[i + 1];
				topRows[i + 1] = tmp;
			}
		}
	}

	ImGui::Text("pd top - frame %u  stage 0x%02x  samples %d/%d",
			latest.frame, (unsigned int)latest.stage, count, PROFILE_HISTORY_LEN);
	ImGui::Separator();
	if (ImGui::BeginTable("pd top header", 4, ImGuiTableFlags_SizingFixedFit)) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("frame %6.2f ms", latestMs);
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("avg %6.2f", avgMs);
		ImGui::TableSetColumnIndex(2);
		ImGui::Text("min %6.2f", minMs);
		ImGui::TableSetColumnIndex(3);
		ImGui::Text("max %6.2f", maxMs);
		ImGui::EndTable();
	}
	imguiOverlayDrawProfilerGraph(frameTimes, count, maxMs);

	if (ImGui::BeginTable("pd top", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("phase", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("latest", ImGuiTableColumnFlags_WidthFixed, 72.0f);
		ImGui::TableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 72.0f);
		ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 72.0f);
		ImGui::TableSetupColumn("load", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("%frame", ImGuiTableColumnFlags_WidthFixed, 58.0f);
		ImGui::TableHeadersRow();
		for (s32 i = 0; i < (s32)ARRAYCOUNT(topRows); ++i) {
			imguiOverlayAddProfilerTopRow(&topRows[i]);
		}
		ImGui::EndTable();
	}

	if (ImGui::BeginTable("pd counters", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("rooms\n%d", latest.rooms);
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("chrs\n%d", latest.chrs);
		ImGui::TableSetColumnIndex(2);
		ImGui::Text("props\n%d/%d", latest.onscreenprops, latest.maxprops);
		ImGui::TableSetColumnIndex(3);
		ImGui::Text("stage free\n%u KiB", latest.stagefree / 1024);
		ImGui::EndTable();
	}
	ImGui::TextDisabled("gfx pending: %u", latest.gfxpending);

	if (ImGui::CollapsingHeader("Markers")) {
		static const char *markerNames[PROFILE_MARKER_SLOT_COUNT] = {
			"main start", "main end", "audio start", "audio end",
			"rsp start", "rsp end", "rdp start", "rdp end",
		};

		for (s32 i = 0; i < PROFILE_MARKER_SLOT_COUNT; ++i) {
			if (latest.markermask & (1 << i)) {
				ImGui::Text("%s: 0x%08x", markerNames[i], latest.markers[i]);
			} else {
				ImGui::TextDisabled("%s: missing", markerNames[i]);
			}
		}
	}
}

static void imguiOverlayDrawRuntimePanel(void)
{
	ImGui::Text("Stage: 0x%02x", (unsigned int)g_StageNum);
	ImGui::Text("Active mod: %d", g_ModNum);
	ImGui::Text("Window: %ux%u", gfx_current_window_dimensions.width,
			gfx_current_window_dimensions.height);
	ImGui::Text("Framebuffers: %s", gfx_framebuffers_enabled ? "enabled" : "disabled");
	ImGui::SeparatorText("Time");
	ImGui::Text("Level frame: %d", g_Vars.lvframenum);
	ImGui::Text("Level tick: %d (60 Hz), %d (240 Hz)",
			g_Vars.lvframe60, g_Vars.lvframe240);
	ImGui::Text("Frame time: %.2f (60 Hz), %.2f (240 Hz)",
			g_Vars.diffframe60f, g_Vars.diffframe240f);
	ImGui::Text("Lost time: %d (60 Hz), %d (240 Hz)",
			g_Vars.lostframetime60t, g_Vars.lostframetime240t);
}

static void imguiOverlayDrawMemoryPanel(void)
{
	ImGui::Text("Emulated heap: %u MiB", g_OsMemSize / (1024 * 1024));
	ImGui::Text("Stage pool free: %u KiB", mempGetStageFree() / 1024);
}

static void imguiOverlayDrawStagePanel(void)
{
	ImGui::Text("Stage: 0x%02x, table index: 0x%02x",
			(unsigned int)g_Vars.stagenum, (unsigned int)g_StageIndex);
	if (g_StageIndex >= 0 && g_StageIndex < kFojoStageTableLen) {
		// romdataFileGetName reads fileSlots[g_ModNum] and rejects anything at
		// or past ROMDATA_MAX_FILES, so a stage setup id now that it carries an
		// owner tag (bit 24) failed its range check and the panel read
		// "unregistered" for every modded stage. Ask the owner's row directly.
		const s32 setupId = (s32)g_Stages[g_StageIndex].setupfileid;
		const s32 setupOwner = MOD_FILEID_MOD(setupId);
		const char *setupName = romdataFileGetSlotName(
				setupOwner >= 0 ? setupOwner : g_ModNum, MOD_FILEID_RAW(setupId));
		ImGui::Text("Setup: %s", setupName ? setupName : "unregistered");
	}
	ImGui::Text("Rooms: %d", g_Vars.roomcount);

	// What actually made it into memory, and where the player is standing. A
	// level that draws as a void is one of these three numbers being wrong,
	// and they are not otherwise visible from inside the game.
	if (g_BgRoomsRefused > 0) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		ImGui::Text("Loaded: %d   refused: %d", g_BgRoomsLoaded, g_BgRoomsRefused);
		ImGui::PopStyleColor();

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("bgLoadRoom turned rooms away. The log says which and why.");
		}
	} else {
		ImGui::Text("Loaded: %d   refused: 0", g_BgRoomsLoaded);
	}

	{
		struct prop *pprop = g_Vars.currentplayer ? g_Vars.currentplayer->prop : NULL;
		s32 room = pprop ? pprop->rooms[0] : -1;

		if (room > 0 && room < g_Vars.roomcount) {
			ImGui::Text("Player room: %d", room);
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.35f, 1.0f));
			ImGui::Text("Player room: %d", room);
			ImGui::PopStyleColor();

			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Room 0 is outside the geometry, and -1 is no player.\n"
						"Nothing draws from there, however well the rooms loaded.");
			}
		}
	}

	if (g_Rooms && ImGui::TreeNode("Rooms")) {
		if (ImGui::BeginChild("Stage rooms", ImVec2(0.0f, 320.0f), ImGuiChildFlags_Borders)) {
			if (ImGui::BeginTable("Stage room table", 5,
					ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY)) {
				ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed, 58.0f);
				ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Portals", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("Centre", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				for (s32 roomNum = 1; roomNum < g_Vars.roomcount; ++roomNum) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("0x%03x", roomNum);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%d", g_Rooms[roomNum].loaded240);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%d", g_Rooms[roomNum].numportals);
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("0x%04x", g_Rooms[roomNum].flags);
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(imguiOverlayCoordString(&g_Rooms[roomNum].centre));
				}
				ImGui::EndTable();
			}
		}
		ImGui::EndChild();
		ImGui::TreePop();
	}
}

static void imguiOverlayDrawEntitiesPanel(void)
{
	if (ImGui::Button("Expand all")) {
		g_ImGuiOverlayExpandLatch = true;
		g_ImGuiOverlayExpandValue = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Close all")) {
		g_ImGuiOverlayExpandLatch = true;
		g_ImGuiOverlayExpandValue = false;
	}
	ImGui::Separator();

	if (ImGui::CollapsingHeader("Players", ImGuiTreeNodeFlags_DefaultOpen)) {
		for (s32 i = 0; i < MAX_PLAYERS; ++i) {
			struct player *player = g_Vars.players[i];
			if (!player) {
				continue;
			}

			if (ImGui::TreeNode(player, "Player %d (%p)", i + 1, player)) {
				imguiOverlayDescribePlayer(player);
				ImGui::TreePop();
			}
		}
	}

	if (g_ImGuiOverlayFocusProp) {
		ImGui::SetNextItemOpen(true);
	}
	if (ImGui::CollapsingHeader("Props", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Visible: %d", g_Vars.numonscreenprops);
		ImGui::Text("Allocated slots: %d", g_Vars.maxprops);
		g_ImGuiOverlayPropTextFilter.Draw("Search props", 180.0f);
		ImGui::Checkbox("Active props only", &g_ImGuiOverlayShowActivePropsOnly);

		if (ImGui::BeginCombo("Filter prop", g_ImGuiOverlayPropFilter
				? imguiOverlayPropTypeName((u8)g_ImGuiOverlayPropFilter) : "Any prop")) {
			for (s32 type = 0; type <= PROPTYPE_SMOKE; ++type) {
				const bool selected = g_ImGuiOverlayPropFilter == type;
				if (ImGui::Selectable(type ? imguiOverlayPropTypeName((u8)type) : "Any prop", selected)) {
					g_ImGuiOverlayPropFilter = type;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (ImGui::BeginCombo("Filter obj", g_ImGuiOverlayObjFilter
				? imguiOverlayObjTypeName((u8)g_ImGuiOverlayObjFilter) : "Any obj")) {
			for (s32 type = 0; type <= OBJTYPE_ESCASTEP; ++type) {
				const bool selected = g_ImGuiOverlayObjFilter == type;
				if (ImGui::Selectable(type ? imguiOverlayObjTypeName((u8)type) : "Any obj", selected)) {
					g_ImGuiOverlayObjFilter = type;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (ImGui::Button("Expand all")) {
			g_ImGuiOverlayExpandLatch = true;
			g_ImGuiOverlayExpandValue = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Close all")) {
			g_ImGuiOverlayExpandLatch = true;
			g_ImGuiOverlayExpandValue = false;
		}

		if (ImGui::BeginChild("Prop list", ImVec2(0.0f, 300.0f), ImGuiChildFlags_Borders)) {
			if (g_ImGuiOverlayShowActivePropsOnly) {
				struct prop *prop = g_Vars.activeprops;
				for (s32 index = 0; prop && prop != g_Vars.pausedprops && index <= g_Vars.maxprops; ++index) {
					if (!imguiOverlayPropIsCurrent(prop)) {
						break;
					}
					struct prop *next = prop->next;
					imguiOverlayDrawPropNode(prop, index);
					prop = next;
				}
			} else if (g_Vars.props) {
				for (s32 index = 0; index < g_Vars.maxprops; ++index) {
					imguiOverlayDrawPropNode(&g_Vars.props[index], index);
				}
			}
		}
		ImGui::EndChild();
	}

	if (g_ImGuiOverlayFocusChr) {
		ImGui::SetNextItemOpen(true);
	}
	if (g_ChrSlots && g_NumChrSlots && ImGui::CollapsingHeader("Characters", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Count: %d/%d", g_NumChrs, g_NumChrSlots);
		g_ImGuiOverlayChrTextFilter.Draw("Search characters", 180.0f);
		if (ImGui::BeginChild("Character list", ImVec2(0.0f, 280.0f), ImGuiChildFlags_Borders)) {
			for (s32 index = 0; index < g_NumChrSlots; ++index) {
				struct chrdata *chr = &g_ChrSlots[index];
				if (chr->chrnum < 0 || !imguiOverlayChrPassesTextFilter(chr, index)) {
					continue;
				}
				const bool focus = chr == g_ImGuiOverlayFocusChr;
				if (g_ImGuiOverlayExpandLatch || focus) {
					ImGui::SetNextItemOpen(focus ? true : g_ImGuiOverlayExpandValue);
				}
				const bool open = ImGui::TreeNode(chr, "Slot %d: 0x%04x (%p)",
						index, (u16)chr->chrnum, chr);

				imguiOverlayDrawEntityContextMenu(chr->prop, chr);

				if (open) {
					if (focus) {
						ImGui::SetScrollHereY(0.25f);
						g_ImGuiOverlayFocusChr = NULL;
					}
					imguiOverlayDescribeChr(chr);
					if (chr->prop) {
						imguiOverlayPropJumpLine("Prop", chr->prop);
					}
					ImGui::TreePop();
				}
				if (focus && g_ImGuiOverlayFocusChr) {
					ImGui::SetScrollHereY(0.25f);
					g_ImGuiOverlayFocusChr = NULL;
				}
			}
		}
		ImGui::EndChild();
	}
}

static void imguiOverlayDrawAssetsPanel(void)
{
	if (ImGui::CollapsingHeader("Mods", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Loaded directories: %u", g_NumModDirs);
		for (u32 modIndex = 0; modIndex < g_NumModDirs; ++modIndex) {
			ImGui::BulletText("%u: %s", modIndex, modDirs[modIndex]);
		}
	}

	if (ImGui::CollapsingHeader("Filesystem")) {
		ImGui::TextWrapped("Base: %s", fsGetBaseDir());
		ImGui::TextWrapped("Save: %s", fsGetSaveDir());
	}

	if (ImGui::CollapsingHeader("ROM Data", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("ROM: %u MiB", g_RomFileSize / (1024 * 1024));
		if (g_ImGuiOverlaySlotMod < 0 || g_ImGuiOverlaySlotMod >= (s32)g_NumModDirs) {
			g_ImGuiOverlaySlotMod = g_ModNum;
		}

		if (ImGui::BeginCombo("Slot mod", g_ImGuiOverlaySlotMod >= 0
				&& g_ImGuiOverlaySlotMod < (s32)g_NumModDirs
				? modDirs[g_ImGuiOverlaySlotMod] : "none")) {
			for (u32 modIndex = 0; modIndex < g_NumModDirs; ++modIndex) {
				const bool selected = g_ImGuiOverlaySlotMod == (s32)modIndex;
				if (ImGui::Selectable(modDirs[modIndex], selected)) {
					g_ImGuiOverlaySlotMod = (s32)modIndex;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		const s32 slotCount = romdataGetFileSlotCount(g_ImGuiOverlaySlotMod);
		ImGui::SameLine();
		ImGui::Text("%d registered slots", slotCount);
		g_ImGuiOverlaySlotFilter.Draw("Filter", 180.0f);

		if (ImGui::BeginChild("File slots", ImVec2(0.0f, 360.0f), ImGuiChildFlags_Borders)) {
			if (ImGui::BeginTable("File slot table", 4,
					ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY)) {
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 62.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 76.0f);
				ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 76.0f);
				ImGui::TableHeadersRow();

				for (s32 fileNum = 1; fileNum < 8192; ++fileNum) {
					struct romdatafileslotinfo slotInfo;
					if (!romdataGetFileSlotInfo(g_ImGuiOverlaySlotMod, fileNum, &slotInfo)
							|| !g_ImGuiOverlaySlotFilter.PassFilter(slotInfo.name)) {
						continue;
					}
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("0x%04x", fileNum);
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(slotInfo.name);
					ImGui::TableSetColumnIndex(2);
					const s32 displayedSource = slotInfo.source == 0
						? slotInfo.configuredSource : slotInfo.source;
					ImGui::TextUnformatted(imguiOverlayFileSourceName(displayedSource));
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Configured: %s\nLoaded: %s",
							imguiOverlayFileSourceName(slotInfo.configuredSource),
							imguiOverlayFileSourceName(slotInfo.source));
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%u", slotInfo.size);
				}

				ImGui::EndTable();
			}
		}
		ImGui::EndChild();
	}
}

static bool imguiOverlayGetSurfaceInfo(struct hitthing *hit)
{
	if (!hit || !imguiOverlayCanAimInspect()) {
		return false;
	}

	struct coord endpos;
	endpos.x = g_Vars.currentplayer->cam_pos.x + g_Vars.currentplayer->cam_look.x * 10000.0f;
	endpos.y = g_Vars.currentplayer->cam_pos.y + g_Vars.currentplayer->cam_look.y * 10000.0f;
	endpos.z = g_Vars.currentplayer->cam_pos.z + g_Vars.currentplayer->cam_look.z * 10000.0f;

	RoomNum outrooms[17];
	RoomNum tmprooms[8];
	RoomNum srcrooms[2];
	srcrooms[0] = g_Vars.currentplayer->cam_room;
	srcrooms[1] = -1;
	outrooms[16] = -1;
	portal00018148(&g_Vars.currentplayer->cam_pos, &endpos, srcrooms, tmprooms, outrooms, 16);

	for (s32 i = 0; outrooms[i] != -1; ++i) {
		if (bgTestHitInRoom(&g_Vars.currentplayer->cam_pos, &endpos, outrooms[i], hit)) {
			return true;
		}
	}

	return false;
}

static void imguiOverlayDrawLookingAtPanel(void)
{
	if (!imguiOverlayCanAimInspect()) {
		ImGui::TextUnformatted("Aim inspection unavailable");
		ImGui::TextDisabled("Requires normal gameplay with live player, room, and camera state.");
		return;
	}

	ImGui::Text("Camera: %s", imguiOverlayCoordString(&g_Vars.currentplayer->cam_pos));
	ImGui::Text("Look: %s", imguiOverlayCoordString(&g_Vars.currentplayer->cam_look));
	ImGui::Text("Room: 0x%03x", (u32)g_Vars.currentplayer->cam_room);
	ImGui::Separator();

	struct prop *prop = propFindAimingAt(HAND_RIGHT, false, FINDPROPCONTEXT_QUERY);
	if (imguiOverlayPropIsCurrent(prop)) {
		ImGui::TextUnformatted("Looking at prop");
		imguiOverlayPropJumpLine("Prop", prop);
		if ((prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER || prop->type == PROPTYPE_EYESPY)
				&& imguiOverlayChrIsCurrent(prop->chr)) {
			imguiOverlayChrJumpLine("Chr", prop->chr);
		}
		if (ImGui::TreeNode(prop, "Details (%p)", prop)) {
			imguiOverlayDescribeProp(prop);
			ImGui::TreePop();
		}
		return;
	}

	struct hitthing hit = {};
	if (imguiOverlayGetSurfaceInfo(&hit)) {
		ImGui::TextUnformatted("Looking at background surface");
		ImGui::Text("Hit pos: %s", imguiOverlayCoordString(&hit.pos));
		ImGui::Text("Texture: 0x%04x", (u16)hit.texturenum);
		if (hit.texturenum >= 0 && ImGui::Button("Probe texture")) {
			imguiOverlayFocusTextureId(hit.texturenum);
		}
		return;
	}

	ImGui::TextUnformatted("Looking at nothing");
}

static bool imguiOverlayIsModelSlotName(const char *name)
{
	if (!name || !name[0]) {
		return false;
	}

	const char *base = strrchr(name, '/');
	base = base ? base + 1 : name;
	return base[0] == 'C' || base[0] == 'P' || base[0] == 'G';
}

static const char *imguiOverlayModelTextureDirName(const char *modelName)
{
	if (!modelName) {
		return NULL;
	}

	const char *nameStart = strstr(modelName, "::");
	nameStart = nameStart ? nameStart + 2 : modelName;
	if (strncmp(nameStart, "files/", 6) == 0) {
		nameStart += 6;
	}
	return nameStart;
}

static s32 imguiOverlayCountModelTextureFiles(s32 modNum, const char *modelName)
{
	const char *textureDirName = imguiOverlayModelTextureDirName(modelName);
	s32 count = 0;
	char prefix[96];
	s32 prefixLen;

	if (!textureDirName || modNum < 0 || modNum >= (s32)g_NumModDirs) {
		return 0;
	}

	snprintf(prefix, sizeof(prefix), "%s/", textureDirName);
	prefixLen = strlen(prefix);

	for (s32 fileNum = 1; fileNum < 8192; ++fileNum) {
		struct romdatafileslotinfo slotInfo;
		if (romdataGetFileSlotInfo(modNum, fileNum, &slotInfo)
				&& slotInfo.name
				&& strncmp(slotInfo.name, prefix, prefixLen) == 0
				&& strstr(slotInfo.name + prefixLen, ".bin")) {
			count++;
		}
	}

	return count;
}

static void imguiOverlayAddModelTextureId(u16 textureId)
{
	for (s32 idIndex = 0; idIndex < g_ImGuiOverlayModelTextureIdCount; ++idIndex) {
		if (g_ImGuiOverlayModelTextureIds[idIndex] == textureId) return;
	}
	if (g_ImGuiOverlayModelTextureIdCount < ARRAYCOUNT(g_ImGuiOverlayModelTextureIds)) {
		g_ImGuiOverlayModelTextureIds[g_ImGuiOverlayModelTextureIdCount++] = textureId;
	}
}

static void imguiOverlayMergeWorkspaceTextureIds(s32 textureMod)
{
	struct modeldefEditorWorkspaceInfo workspaceInfo;
	if (!modeldefEditorWorkspaceGetInfo(&workspaceInfo)) return;
	for (s32 index = 0; index < workspaceInfo.texturecount; ++index) {
		struct modeldefEditorTextureInfo textureInfo;
		if (!modeldefEditorWorkspaceGetTextureInfo(index, &textureInfo)) continue;
		const u16 localId = modTexMapReverseLookup(textureMod, textureInfo.textureid);
		imguiOverlayAddModelTextureId(localId != 0xffff ? localId : textureInfo.textureid);
	}
}

static bool imguiOverlayGetWorkspaceTextureInfo(u16 textureId, struct modeldefEditorTextureInfo *result)
{
	struct modeldefEditorWorkspaceInfo workspaceInfo;
	if (!result || !modeldefEditorWorkspaceGetInfo(&workspaceInfo)) return false;
	for (s32 index = 0; index < workspaceInfo.texturecount; ++index) {
		if (modeldefEditorWorkspaceGetTextureInfo(index, result) && result->textureid == textureId) return true;
	}
	return false;
}

/*
 * Build a file id for the overlay's (mod, file) selection.
 *
 * This was six copies of `modelFileNum | (textureMod << 16)`, which is the
 * encoding MOD_FILEID_MAKE owns. Hand-rolled, they now produce an id with mod
 * bits and no owner tag - which modFileIdMod answers as vanilla and reports.
 * A negative textureMod is the overlay's "no mod context" selection, and an
 * untagged id is exactly how that is spelled.
 */
static inline s32 imguiOverlayEncodeFileId(s32 textureMod, s32 fileNum)
{
	return textureMod >= 0 ? MOD_FILEID_MAKE(textureMod, fileNum) : MOD_FILEID_RAW(fileNum);
}

static void imguiOverlayScanModelTextureIds(s32 textureMod, s32 modelFileNum)
{
	struct modeldefTextureUsage usages[512];
	s32 total = 0;
	const s32 encodedFileNum = imguiOverlayEncodeFileId(textureMod, modelFileNum);
	const s32 usageCount = modeldefInspectTextureUsage(encodedFileNum, 0xffff, 0xffff,
		usages, ARRAYCOUNT(usages), &total, NULL, 0, NULL, NULL);

	g_ImGuiOverlayModelTextureIdCount = 0;
	g_ImGuiOverlayModelTextureIdTotal = total;
	g_ImGuiOverlayScannedTextureModelMod = textureMod;
	g_ImGuiOverlayScannedTextureModelFileNum = modelFileNum;
	for (s32 usageIndex = 0; usageIndex < usageCount; ++usageIndex) {
		imguiOverlayAddModelTextureId(usages[usageIndex].textureid);
	}
}

static void imguiOverlayDrawModelTextureFiles(s32 textureMod, s32 modelFileNum, const char *textureDirName)
{
	if (textureMod < 0 || textureMod >= (s32)g_NumModDirs || modelFileNum <= 0 || !textureDirName) {
		return;
	}

	ImGui::SeparatorText("Model Textures");
	if (ImGui::BeginChild("Model texture files", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders)) {
		if (ImGui::BeginTable("Model texture file table", 8,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY)) {
			ImGui::TableSetupColumn("modelTexId", ImGuiTableColumnFlags_WidthFixed, 78.0f);
			ImGui::TableSetupColumn("resolvedTexId", ImGuiTableColumnFlags_WidthFixed, 86.0f);
			ImGui::TableSetupColumn("fileSlot", ImGuiTableColumnFlags_WidthFixed, 62.0f);
			ImGui::TableSetupColumn("source", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 62.0f);
			ImGui::TableSetupColumn("png", ImGuiTableColumnFlags_WidthFixed, 42.0f);
			ImGui::TableSetupColumn("dims", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 62.0f);
			ImGui::TableHeadersRow();

			for (s32 idIndex = 0; idIndex < g_ImGuiOverlayModelTextureIdCount; ++idIndex) {
				const u16 localTexId = g_ImGuiOverlayModelTextureIds[idIndex];
				const u16 portTexId = modTexMapLookup(textureMod, localTexId);
				const bool mapped = portTexId != localTexId;
				u16 resolvedLocalId = portTexId;
				char resolvedName[128] = {0};
				const s32 fileNum = modTextureResolveFile(textureMod, modelFileNum,
					mapped ? portTexId : localTexId, &resolvedLocalId, resolvedName, sizeof(resolvedName));
				struct romdatafileslotinfo slotInfo = {0};
				const bool hasFile = fileNum > 0 && romdataGetFileSlotInfo(textureMod, fileNum, &slotInfo);
				const u16 engineTexId = mapped ? portTexId : localTexId;
				const bool hasRomTexture = imguiOverlayHasRomTexture(engineTexId);
				struct modeldefEditorTextureInfo workspaceTextureInfo;
				const bool inWorkspace = imguiOverlayGetWorkspaceTextureInfo(engineTexId, &workspaceTextureInfo);
				const bool hasExtTex = extTexModelHasEntryForTexid((s16)modelFileNum, localTexId);
				const s8 owner = extTexGetOwnerMod(1, (u16)modelFileNum, localTexId);
				u16 width = 0;
				u16 height = 0;
				const u8 hasDimensions = extTexGetDimensions(1, (u16)modelFileNum, localTexId, &width, &height);

				ImGui::PushID(idIndex);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%04x", localTexId);
				ImGui::TableSetColumnIndex(1);
				if (mapped) {
					ImGui::Text("%04x", portTexId);
				} else {
					ImGui::TextDisabled("same");
				}
				ImGui::TableSetColumnIndex(2);
				if (hasFile) {
					ImGui::Text("%04x", fileNum);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", resolvedName);
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(3);
				if (hasFile) {
					const s32 displayedSource = slotInfo.source == 0 ? slotInfo.configuredSource : slotInfo.source;
					ImGui::TextUnformatted(imguiOverlayFileSourceName(displayedSource));
				} else if (hasRomTexture) {
					ImGui::TextUnformatted("ROM bank");
				} else {
					ImGui::TextDisabled("missing");
				}
				ImGui::TableSetColumnIndex(4);
				if (hasFile) {
					ImGui::Text("%u", slotInfo.size);
				} else if (hasRomTexture) {
					ImGui::Text("%u", g_Textures[engineTexId + 1].dataoffset - g_Textures[engineTexId].dataoffset);
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(5);
				if (hasExtTex) {
					ImGui::Text("%d", owner);
				} else {
					ImGui::TextDisabled("no");
				}
				ImGui::TableSetColumnIndex(6);
				if (inWorkspace) {
					ImGui::Text("%ux%u", workspaceTextureInfo.width, workspaceTextureInfo.height);
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("private workspace\nformat %u, depth %u\npalette %u, LODs %u\ndecoded %u bytes",
							workspaceTextureInfo.gbiformat, workspaceTextureInfo.depth,
							workspaceTextureInfo.palettecount, workspaceTextureInfo.lodcount,
							workspaceTextureInfo.decodedsize);
					}
				} else if (hasDimensions) {
					ImGui::Text("%ux%u", width, height);
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(7);
				if (ImGui::SmallButton("Probe")) {
					g_ImGuiOverlayTextureProbeId = localTexId;
				}
				ImGui::PopID();
			}

			const s32 extTexCount = extTexModelGetTextureCount((s16)modelFileNum);
			for (s32 index = 0; index < extTexCount; ++index) {
				s32 texNum = 0;
				s8 owner = -1;
				u16 width = 0;
				u16 height = 0;
				char textureFileName[128];

				if (!extTexModelGetTextureInfo((s16)modelFileNum, index, &texNum, &owner, &width, &height)) {
					continue;
				}

				snprintf(textureFileName, sizeof(textureFileName), "%s/%04x.bin", textureDirName, (u16)texNum);
				if (romdataFileGetNumForNameInMod(textureFileName, textureMod) > 0) {
					continue;
				}

				const u16 portTexId = modTexMapLookup(textureMod, (u16)texNum);
				const bool mapped = portTexId != (u16)texNum;
				ImGui::PushID(0x10000 + index);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%04x", (u16)texNum);
				ImGui::TableSetColumnIndex(1);
				if (mapped) {
					ImGui::Text("%04x", portTexId);
				} else {
					ImGui::TextDisabled("same");
				}
				ImGui::TableSetColumnIndex(2);
				ImGui::TextDisabled("-");
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted("png");
				ImGui::TableSetColumnIndex(4);
				ImGui::TextDisabled("-");
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%d", owner);
				ImGui::TableSetColumnIndex(6);
				if (width > 0 && height > 0) {
					ImGui::Text("%ux%u", width, height);
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(7);
				if (ImGui::SmallButton("Probe")) {
					g_ImGuiOverlayTextureProbeId = texNum;
				}
				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}
	ImGui::EndChild();
	ImGui::TextDisabled("Inspectable textures: %d; pre-expansion GDL references: %d",
		g_ImGuiOverlayModelTextureIdCount, g_ImGuiOverlayModelTextureIdTotal);
	ImGui::TextDisabled("modelTexId = model GDL texture ID; resolvedTexId = texMap rewrite target; fileSlot = filetable slot for <ModelName>/<modelTexId>.bin");
	ImGui::TextDisabled("png/ext_tex rows are metadata only for now; PNG texture preview/loading is not implemented in this panel yet.");
}

static void imguiOverlayDrawTextureModelSearch(s32 currentModelMod, s32 currentModelFileNum)
{
	if (g_ImGuiOverlayTextureModelMod < 0 || g_ImGuiOverlayTextureModelMod >= (s32)g_NumModDirs) {
		g_ImGuiOverlayTextureModelMod = currentModelMod >= 0 ? currentModelMod : g_ModNum;
	}

	ImGui::SeparatorText("Model Search");
	if (ImGui::BeginCombo("Model mod", g_ImGuiOverlayTextureModelMod >= 0
			&& g_ImGuiOverlayTextureModelMod < (s32)g_NumModDirs
			? modDirs[g_ImGuiOverlayTextureModelMod] : "none")) {
		for (u32 modIndex = 0; modIndex < g_NumModDirs; ++modIndex) {
			const bool selected = g_ImGuiOverlayTextureModelMod == (s32)modIndex;
			if (ImGui::Selectable(modDirs[modIndex], selected)) {
				g_ImGuiOverlayTextureModelMod = (s32)modIndex;
				g_ImGuiOverlayTextureModelFileNum = -1;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	g_ImGuiOverlayModelFilter.Draw("Search models", 180.0f);
	ImGui::SameLine();
	if (ImGui::Button("Use current") && currentModelMod >= 0 && currentModelFileNum > 0) {
		g_ImGuiOverlayTextureModelMod = currentModelMod;
		g_ImGuiOverlayTextureModelFileNum = currentModelFileNum;
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear")) {
		g_ImGuiOverlayTextureModelFileNum = -1;
	}

	if (ImGui::BeginChild("Model slots", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders)) {
		if (ImGui::BeginTable("Model slot table", 6,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY)) {
			ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 62.0f);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("scoped files", ImGuiTableColumnFlags_WidthFixed, 82.0f);
			ImGui::TableSetupColumn("ext_tex", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableHeadersRow();

			for (s32 fileNum = 1; fileNum < 8192; ++fileNum) {
				struct romdatafileslotinfo slotInfo;
				if (!romdataGetFileSlotInfo(g_ImGuiOverlayTextureModelMod, fileNum, &slotInfo)
						|| !imguiOverlayIsModelSlotName(slotInfo.name)
						|| !g_ImGuiOverlayModelFilter.PassFilter(slotInfo.name)) {
					continue;
				}

				const bool selected = g_ImGuiOverlayTextureModelFileNum == fileNum;
				const bool current = g_ImGuiOverlayTextureModelMod == currentModelMod && fileNum == currentModelFileNum;
				const s32 textureFileCount = imguiOverlayCountModelTextureFiles(g_ImGuiOverlayTextureModelMod, slotInfo.name);
				const s32 extTexCount = extTexModelGetTextureCount((s16)fileNum);
				ImGui::PushID(fileNum);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("0x%04x", fileNum);
				ImGui::TableSetColumnIndex(1);
				if (ImGui::Selectable(slotInfo.name, selected, ImGuiSelectableFlags_SpanAllColumns)) {
					g_ImGuiOverlayTextureModelFileNum = fileNum;
				}
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%d", textureFileCount);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%d", extTexCount);
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(current ? "current" : selected ? "selected" : "");
				ImGui::TableSetColumnIndex(5);
				if (ImGui::SmallButton("Select")) {
					g_ImGuiOverlayTextureModelFileNum = fileNum;
				}
				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}
	ImGui::EndChild();
}

static void imguiOverlayClearTexturePreview(void)
{
	if (g_ImGuiOverlayTexturePreview) {
		glDeleteTextures(1, &g_ImGuiOverlayTexturePreview);
		g_ImGuiOverlayTexturePreview = 0;
	}

	g_ImGuiOverlayTexturePreviewPixels = NULL;
	g_ImGuiOverlayTexturePreviewPixelStorage.clear();
	g_ImGuiOverlayTexturePreviewModelFileNum = -1;
	g_ImGuiOverlayTexturePreviewTexId = -1;
	g_ImGuiOverlayTexturePreviewWidth = 0;
	g_ImGuiOverlayTexturePreviewHeight = 0;
	g_ImGuiOverlayTextureCompareValid = false;
}

static bool imguiOverlayLoadTexturePreview(s32 modelFileNum, u16 localTexId)
{
	u32 width = 0;
	u32 height = 0;
	const u8 *pixels = extTexModelLoadPixels((s16)modelFileNum, localTexId, &width, &height);
	if (!pixels || width == 0 || height == 0) {
		return false;
	}

	imguiOverlayClearTexturePreview();
	GLint previousBinding = 0;
	GLint previousUnpackAlignment = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousBinding);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
	glGenTextures(1, &g_ImGuiOverlayTexturePreview);
	glBindTexture(GL_TEXTURE_2D, g_ImGuiOverlayTexturePreview);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
	glBindTexture(GL_TEXTURE_2D, previousBinding);

	g_ImGuiOverlayTexturePreviewPixelStorage.assign(pixels, pixels + (size_t)width * height * 4);
	g_ImGuiOverlayTexturePreviewPixels = g_ImGuiOverlayTexturePreviewPixelStorage.data();
	g_ImGuiOverlayTexturePreviewModelFileNum = modelFileNum;
	g_ImGuiOverlayTexturePreviewTexId = localTexId;
	g_ImGuiOverlayTexturePreviewWidth = width;
	g_ImGuiOverlayTexturePreviewHeight = height;
	return true;
}

static void imguiOverlayDrawTexturePreview(s32 modelFileNum, u16 localTexId, bool hasModelExtTex)
{
	if (g_ImGuiOverlayTexturePreview
			&& (g_ImGuiOverlayTexturePreviewModelFileNum != modelFileNum
				|| g_ImGuiOverlayTexturePreviewTexId != localTexId)) {
		imguiOverlayClearTexturePreview();
	}

	ImGui::SeparatorText("PNG Preview");
	if (!hasModelExtTex) {
		ImGui::TextDisabled("No model-scoped ext_tex image for this texture ID.");
		return;
	}

	if (ImGui::Button(g_ImGuiOverlayTexturePreview ? "Reload PNG preview" : "Load PNG preview")) {
		imguiOverlayLoadTexturePreview(modelFileNum, localTexId);
	}

	if (!g_ImGuiOverlayTexturePreview || !g_ImGuiOverlayTexturePreviewPixels) {
		return;
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderInt("Zoom", &g_ImGuiOverlayTexturePreviewZoom, 1, 16, "%dx");
	ImGui::Text("%ux%u RGBA8", g_ImGuiOverlayTexturePreviewWidth, g_ImGuiOverlayTexturePreviewHeight);

	const ImVec2 imageSize(
		(float)g_ImGuiOverlayTexturePreviewWidth * g_ImGuiOverlayTexturePreviewZoom,
		(float)g_ImGuiOverlayTexturePreviewHeight * g_ImGuiOverlayTexturePreviewZoom);
	if (ImGui::BeginChild("Texture preview", ImVec2(0.0f, ImMin(imageSize.y + 12.0f, 520.0f)),
			ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
		ImGui::Image(ImTextureRef((ImTextureID)g_ImGuiOverlayTexturePreview), imageSize,
			ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
		if (ImGui::IsItemHovered()) {
			const ImVec2 imageMin = ImGui::GetItemRectMin();
			const ImVec2 mousePos = ImGui::GetIO().MousePos;
			const u32 x = ImMin((u32)((mousePos.x - imageMin.x) * g_ImGuiOverlayTexturePreviewWidth / imageSize.x),
				g_ImGuiOverlayTexturePreviewWidth - 1);
			const u32 y = ImMin((u32)((mousePos.y - imageMin.y) * g_ImGuiOverlayTexturePreviewHeight / imageSize.y),
				g_ImGuiOverlayTexturePreviewHeight - 1);
			const u32 storedY = g_ImGuiOverlayTexturePreviewHeight - 1 - y;
			const u8 *pixel = &g_ImGuiOverlayTexturePreviewPixels[(storedY * g_ImGuiOverlayTexturePreviewWidth + x) * 4];
			ImGui::SetTooltip("(%u, %u)\nRGBA %u, %u, %u, %u\n#%02X%02X%02X%02X",
				x, y, pixel[0], pixel[1], pixel[2], pixel[3], pixel[0], pixel[1], pixel[2], pixel[3]);
		}
	}
	ImGui::EndChild();
}

static bool imguiOverlayFindRenderedTexture(s32 modelFileNum, u16 localTexId, u16 portTexId,
		struct GfxTextureDebugInfo *result)
{
	s32 bestScore = -1;
	const u32 count = gfx_get_debug_texture_count();

	for (u32 index = 0; index < count; ++index) {
		struct GfxTextureDebugInfo info;
		if (!gfx_get_debug_texture(index, &info)
				|| (info.texnum != localTexId && info.texnum != portTexId)) {
			continue;
		}

		s32 score = info.texnum == localTexId ? 2 : 1;
		if (info.id == modelFileNum) {
			score += 4;
		} else if (info.id != 0) {
			continue;
		}

		if (score > bestScore) {
			bestScore = score;
			*result = info;
		}
	}

	return bestScore >= 0;
}

static const char *imguiOverlayTextureFormatName(u8 nativeFormat)
{
	switch (nativeFormat) {
	case TEXFORMAT_RGBA32: return "RGBA32";
	case TEXFORMAT_RGBA16: return "RGBA16";
	case TEXFORMAT_RGB24: return "RGB24";
	case TEXFORMAT_RGB15: return "RGB15";
	case TEXFORMAT_IA16: return "IA16";
	case TEXFORMAT_IA8: return "IA8";
	case TEXFORMAT_IA4: return "IA4";
	case TEXFORMAT_I8: return "I8";
	case TEXFORMAT_I4: return "I4";
	case TEXFORMAT_RGBA16_CI8: return "RGBA16 CI8";
	case TEXFORMAT_RGBA16_CI4: return "RGBA16 CI4";
	case TEXFORMAT_IA16_CI8: return "IA16 CI8";
	case TEXFORMAT_IA16_CI4: return "IA16 CI4";
	default: return "unknown";
	}
}

static bool imguiOverlayHasRomTexture(u16 textureId)
{
	return textureId < NUM_TEXTURES && g_Textures
		&& g_Textures[textureId].dataoffset != g_Textures[textureId + 1].dataoffset;
}

static void imguiOverlayClearRenderedPixels(void)
{
	g_ImGuiOverlayRenderedTexturePixels.clear();
	g_ImGuiOverlayRenderedPixelsModelFileNum = -1;
	g_ImGuiOverlayRenderedPixelsTexId = -1;
	g_ImGuiOverlayRenderedPixelsWidth = 0;
	g_ImGuiOverlayRenderedPixelsHeight = 0;
	g_ImGuiOverlayTextureCompareValid = false;
	g_ImGuiOverlayNativeCompareValid = false;
}

static bool imguiOverlayCaptureRenderedPixels(const struct GfxTextureDebugInfo *info, s32 width, s32 height)
{
	if (!info || width <= 0 || height <= 0) {
		return false;
	}

	GLint previousBinding = 0;
	GLint previousPackAlignment = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousBinding);
	glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
	g_ImGuiOverlayRenderedTexturePixels.resize((size_t)width * height * 4);
	glBindTexture(GL_TEXTURE_2D, info->texture_id);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		g_ImGuiOverlayRenderedTexturePixels.data());
	glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
	glBindTexture(GL_TEXTURE_2D, previousBinding);

	g_ImGuiOverlayRenderedPixelsModelFileNum = info->id;
	g_ImGuiOverlayRenderedPixelsTexId = info->texnum;
	g_ImGuiOverlayRenderedPixelsWidth = width;
	g_ImGuiOverlayRenderedPixelsHeight = height;
	g_ImGuiOverlayTextureCompareValid = false;
	g_ImGuiOverlayNativeCompareValid = false;
	return true;
}

static void imguiOverlayPinRenderedReference(const struct GfxTextureDebugInfo *info,
		s32 modelFileNum, u16 textureId, const char *modelName, s32 width, s32 height)
{
	if (!imguiOverlayCaptureRenderedPixels(info, width, height)) return;
	g_ImGuiOverlayReferenceTexturePixels = g_ImGuiOverlayRenderedTexturePixels;
	g_ImGuiOverlayReferencePixelsWidth = width;
	g_ImGuiOverlayReferencePixelsHeight = height;
	g_ImGuiOverlayReferenceModelFileNum = modelFileNum;
	g_ImGuiOverlayReferenceTexId = textureId;
	snprintf(g_ImGuiOverlayReferenceModelName, sizeof(g_ImGuiOverlayReferenceModelName), "%s",
		modelName ? modelName : "unknown model");
	g_ImGuiOverlayNativeCompareValid = false;
}

static void imguiOverlayCompareNativeTextures(void)
{
	g_ImGuiOverlayNativeCompareDifferentPixels = 0;
	g_ImGuiOverlayNativeCompareChannelDelta = 0;
	g_ImGuiOverlayNativeCompareMaxDelta = 0;
	g_ImGuiOverlayNativeCompareValid = false;
	if (g_ImGuiOverlayReferenceTexturePixels.empty() || g_ImGuiOverlayRenderedTexturePixels.empty()
			|| g_ImGuiOverlayReferencePixelsWidth != g_ImGuiOverlayRenderedPixelsWidth
			|| g_ImGuiOverlayReferencePixelsHeight != g_ImGuiOverlayRenderedPixelsHeight) return;

	const size_t pixelCount = (size_t)g_ImGuiOverlayRenderedPixelsWidth * g_ImGuiOverlayRenderedPixelsHeight;
	for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
		bool different = false;
		for (size_t channel = 0; channel < 4; ++channel) {
			const size_t offset = pixelIndex * 4 + channel;
			const u8 reference = g_ImGuiOverlayReferenceTexturePixels[offset];
			const u8 current = g_ImGuiOverlayRenderedTexturePixels[offset];
			const u32 delta = reference > current ? reference - current : current - reference;
			g_ImGuiOverlayNativeCompareChannelDelta += delta;
			g_ImGuiOverlayNativeCompareMaxDelta = ImMax(g_ImGuiOverlayNativeCompareMaxDelta, delta);
			different |= delta != 0;
		}
		g_ImGuiOverlayNativeCompareDifferentPixels += different ? 1 : 0;
	}
	g_ImGuiOverlayNativeCompareValid = true;
}

static bool imguiOverlayWritePngChunk(FILE *file, const char type[4], const u8 *data, u32 size)
{
	u8 encodedSize[4] = {
		(u8)(size >> 24), (u8)(size >> 16), (u8)(size >> 8), (u8)size,
	};
	uLong crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, (const Bytef *)type, 4);
	if (data && size > 0) crc = crc32(crc, data, size);
	u8 encodedCrc[4] = {
		(u8)(crc >> 24), (u8)(crc >> 16), (u8)(crc >> 8), (u8)crc,
	};
	return fwrite(encodedSize, 1, sizeof(encodedSize), file) == sizeof(encodedSize)
		&& fwrite(type, 1, 4, file) == 4
		&& (!data || size == 0 || fwrite(data, 1, size, file) == size)
		&& fwrite(encodedCrc, 1, sizeof(encodedCrc), file) == sizeof(encodedCrc);
}

static bool imguiOverlayWriteRgbaPng(const char *path, const u8 *pixels, u32 width, u32 height)
{
	if (!path || !pixels || width == 0 || height == 0) return false;
	const size_t rowSize = (size_t)width * 4;
	std::vector<u8> scanlines((rowSize + 1) * height);
	for (u32 y = 0; y < height; ++y) {
		u8 *row = &scanlines[(rowSize + 1) * y];
		row[0] = 0;
		memcpy(row + 1, pixels + rowSize * (height - 1 - y), rowSize);
	}

	uLongf compressedSize = compressBound(scanlines.size());
	std::vector<u8> compressed(compressedSize);
	if (compress2(compressed.data(), &compressedSize, scanlines.data(), scanlines.size(), Z_BEST_COMPRESSION) != Z_OK) {
		return false;
	}
	compressed.resize(compressedSize);

	FILE *file = fopen(path, "wb");
	if (!file) return false;
	const u8 signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
	u8 header[13] = {
		(u8)(width >> 24), (u8)(width >> 16), (u8)(width >> 8), (u8)width,
		(u8)(height >> 24), (u8)(height >> 16), (u8)(height >> 8), (u8)height,
		8, 6, 0, 0, 0,
	};
	const bool ok = fwrite(signature, 1, sizeof(signature), file) == sizeof(signature)
		&& imguiOverlayWritePngChunk(file, "IHDR", header, sizeof(header))
		&& imguiOverlayWritePngChunk(file, "IDAT", compressed.data(), compressed.size())
		&& imguiOverlayWritePngChunk(file, "IEND", NULL, 0);
	fclose(file);
	return ok;
}

static void imguiOverlayExportTexturePng(const char *modelName, u16 textureId,
		const u8 *pixels, u32 width, u32 height, const char *suffix)
{
	char safeName[96];
	const char *nameStart = modelName ? strstr(modelName, "::") : NULL;
	nameStart = nameStart ? nameStart + 2 : modelName ? modelName : "model";
	const char *slash = strrchr(nameStart, '/');
	if (slash) nameStart = slash + 1;
	s32 nameLength = 0;
	while (nameStart[nameLength] && nameLength < (s32)sizeof(safeName) - 1) {
		const char c = nameStart[nameLength];
		safeName[nameLength] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_' || c == '-' ? c : '_';
		++nameLength;
	}
	safeName[nameLength] = '\0';

	char directory[FS_MAXPATH + 1];
	char path[FS_MAXPATH + 1];
	snprintf(directory, sizeof(directory), "%s/fojo-textures", fsGetSaveDir());
	fsCreateDir(directory);
	snprintf(path, sizeof(path), "%s/%s_%04x_%s.png", directory, safeName, textureId, suffix);
	if (imguiOverlayWriteRgbaPng(path, pixels, width, height)) {
		snprintf(g_ImGuiOverlayTextureExportStatus, sizeof(g_ImGuiOverlayTextureExportStatus), "Exported %s", path);
	} else {
		snprintf(g_ImGuiOverlayTextureExportStatus, sizeof(g_ImGuiOverlayTextureExportStatus), "Export failed: %s", path);
	}
}

static void imguiOverlayCompareTexturePixels(void)
{
	g_ImGuiOverlayTextureCompareDifferentPixels = 0;
	g_ImGuiOverlayTextureCompareChannelDelta = 0;
	g_ImGuiOverlayTextureCompareMaxDelta = 0;
	g_ImGuiOverlayTextureCompareValid = false;

	if (!g_ImGuiOverlayTexturePreviewPixels || g_ImGuiOverlayRenderedTexturePixels.empty()
			|| g_ImGuiOverlayTexturePreviewWidth != g_ImGuiOverlayRenderedPixelsWidth
			|| g_ImGuiOverlayTexturePreviewHeight != g_ImGuiOverlayRenderedPixelsHeight) {
		return;
	}

	const u32 width = g_ImGuiOverlayRenderedPixelsWidth;
	const u32 height = g_ImGuiOverlayRenderedPixelsHeight;
	for (u32 y = 0; y < height; ++y) {
		const u32 sourceY = height - 1 - y;
		const u32 renderedY = g_ImGuiOverlayRenderedTextureFlipY ? height - 1 - y : y;
		for (u32 x = 0; x < width; ++x) {
			const u8 *source = &g_ImGuiOverlayTexturePreviewPixels[(sourceY * width + x) * 4];
			const u8 *rendered = &g_ImGuiOverlayRenderedTexturePixels[(renderedY * width + x) * 4];
			bool different = false;
			for (u32 channel = 0; channel < 4; ++channel) {
				const u32 delta = source[channel] > rendered[channel]
					? source[channel] - rendered[channel] : rendered[channel] - source[channel];
				g_ImGuiOverlayTextureCompareChannelDelta += delta;
				g_ImGuiOverlayTextureCompareMaxDelta = ImMax(g_ImGuiOverlayTextureCompareMaxDelta, delta);
				different |= delta != 0;
			}
			g_ImGuiOverlayTextureCompareDifferentPixels += different ? 1 : 0;
		}
	}
	g_ImGuiOverlayTextureCompareValid = true;
}

static bool imguiOverlayRequestEngineTexture(s32 textureMod, s32 modelFileNum,
		s32 textureFileNum, u16 textureId)
{
	gfx_submit_debug_texture_gdl(NULL);
	g_ImGuiOverlayEngineProbeMetadataValid = false;
	g_ImGuiOverlayEngineProbeAttempted = true;
	g_ImGuiOverlayEngineProbeDecoded = false;
	g_ImGuiOverlayEngineProbeModelFileNum = modelFileNum;
	g_ImGuiOverlayEngineProbeTexId = textureId;
	if (textureMod < 0 || textureMod >= (s32)g_NumModDirs || modelFileNum <= 0) {
		return false;
	}

	if (g_ImGuiOverlayTextureProbeData) {
		gfx_forget_debug_texture_data(g_ImGuiOverlayTextureProbeData);
		g_ImGuiOverlayTextureProbeData = NULL;
	}

	const s32 previousMod = g_TexModNum;
	const s32 previousModelFileNum = g_TexCurrentModelFileNum;
	g_TexModNum = textureMod;
	g_TexCurrentModelFileNum = modelFileNum;
	struct modeldefEditorWorkspaceInfo workspaceInfo;
	const s32 encodedModelFileNum = imguiOverlayEncodeFileId(textureMod, modelFileNum);
	const bool workspaceMatches = modeldefEditorWorkspaceGetInfo(&workspaceInfo)
		&& workspaceInfo.fileid == encodedModelFileNum;
	struct tex *tex = workspaceMatches ? modeldefEditorWorkspaceFindTexture(textureId) : NULL;
	u32 compressedSize = 0;
	const s32 encodedFileNum = imguiOverlayEncodeFileId(textureMod, textureFileNum);
	u8 *compressedData = textureFileNum > 0 ? romdataFileLoad(encodedFileNum, &compressedSize) : NULL;
	const u8 header = compressedData && compressedSize > 0 ? compressedData[0] : 0;
	const u8 nativeFormat = compressedData && compressedSize > 1
		? ((header & 0x40) ? compressedData[1] : compressedData[1] >> 4) : 0xff;
	if (compressedData) {
		romdataFileFree(encodedFileNum);
	}
	if (!tex) {
		texInitPool(&g_ImGuiOverlayTextureProbePool, g_ImGuiOverlayTextureProbePoolData,
			sizeof(g_ImGuiOverlayTextureProbePoolData));
		texnum_t updateword = textureId;
		texLoad(&updateword, &g_ImGuiOverlayTextureProbePool, true);
		tex = texFindInPool(textureId, &g_ImGuiOverlayTextureProbePool);
	}
	if (tex) {
		g_ImGuiOverlayTextureProbeData = tex->data;
		texBuildDebugLoadGdl(g_ImGuiOverlayTextureProbeGdl, tex);
		gfx_submit_debug_texture_gdl(g_ImGuiOverlayTextureProbeGdl);
		g_ImGuiOverlayEngineProbeModelFileNum = modelFileNum;
		g_ImGuiOverlayEngineProbeTexId = textureId;
		g_ImGuiOverlayEngineProbeDecoded = true;
		g_ImGuiOverlayEngineProbeCompressedSize = compressedSize;
		struct modeldefEditorTextureInfo workspaceTextureInfo;
		g_ImGuiOverlayEngineProbeDecodedSize = workspaceMatches
			&& imguiOverlayGetWorkspaceTextureInfo(textureId, &workspaceTextureInfo)
			? workspaceTextureInfo.decodedsize : g_ImGuiOverlayTextureProbePool.leftpos - tex->data;
		g_ImGuiOverlayEngineProbeHeader = header;
		g_ImGuiOverlayEngineProbeNativeFormat = nativeFormat;
		g_ImGuiOverlayEngineProbeHeaderAvailable = compressedData != NULL;
		g_ImGuiOverlayEngineProbeWidth = tex->width;
		g_ImGuiOverlayEngineProbeHeight = tex->height;
		g_ImGuiOverlayEngineProbeFormat = tex->gbiformat;
		g_ImGuiOverlayEngineProbeDepth = tex->depth;
		g_ImGuiOverlayEngineProbeLutMode = tex->lutmodeindex;
		g_ImGuiOverlayEngineProbePaletteCount = tex->lutmodeindex ? tex->numcolors + 1 : 0;
		g_ImGuiOverlayEngineProbeLodCount = tex->numlods ? tex->numlods : 1;
		g_ImGuiOverlayEngineProbeHasLodData = tex->hasloddata;
		g_ImGuiOverlayEngineProbeMetadataValid = true;
	}
	g_TexCurrentModelFileNum = previousModelFileNum;
	g_TexModNum = previousMod;
	return tex != NULL;
}

static void imguiOverlayDrawModelWorkspace(s32 textureMod, s32 modelFileNum)
{
	ImGui::SeparatorText("Private Model Workspace");
	const s32 encodedFileNum = imguiOverlayEncodeFileId(textureMod, modelFileNum);
	struct modeldefEditorWorkspaceInfo info;
	const bool loaded = modeldefEditorWorkspaceGetInfo(&info);
	const bool selectedLoaded = loaded && info.fileid == encodedFileNum;

	if (ImGui::Button(selectedLoaded ? "Reload selected model privately" : "Load selected model privately")) {
		gfx_submit_debug_texture_gdl(NULL);
		if (g_ImGuiOverlayTextureProbeData) {
			gfx_forget_debug_texture_data(g_ImGuiOverlayTextureProbeData);
			g_ImGuiOverlayTextureProbeData = NULL;
		}
		if (modeldefEditorWorkspaceLoad(encodedFileNum, 512 * 1024)) {
			imguiOverlayMergeWorkspaceTextureIds(textureMod);
		}
	}
	if (loaded) {
		ImGui::SameLine();
		if (ImGui::Button("Unload private model")) {
			gfx_submit_debug_texture_gdl(NULL);
			if (g_ImGuiOverlayTextureProbeData) {
				gfx_forget_debug_texture_data(g_ImGuiOverlayTextureProbeData);
				g_ImGuiOverlayTextureProbeData = NULL;
			}
			modeldefEditorWorkspaceUnload();
		}
	}

	if (modeldefEditorWorkspaceGetInfo(&info)) {
		ImGui::Text("Loaded model: mod %d, file 0x%04x%s",
			MOD_FILEID_MOD(info.fileid), MOD_FILEID_RAW(info.fileid),
			info.fileid == encodedFileNum ? " (selected)" : "");
		ImGui::Text("Model memory: %u / %u bytes; texture pool: %u / %u bytes (%d textures)",
			info.modelloadedsize, info.modelcapacity,
			info.texturebytesused, info.texturecapacity, info.texturecount);
	} else {
		ImGui::TextDisabled("No private model loaded.");
	}
}

static void imguiOverlayDrawWorkspacePalette(u16 textureId)
{
	struct modeldefEditorPaletteEntry entries[256];
	const s32 count = modeldefEditorWorkspaceGetPalette(textureId, entries, ARRAYCOUNT(entries));
	if (count <= 0) return;

	s32 unusedCount = 0;
	s32 duplicateCount = 0;
	for (s32 index = 0; index < count; ++index) {
		unusedCount += entries[index].usagecount == 0 ? 1 : 0;
		duplicateCount += entries[index].duplicateof >= 0 ? 1 : 0;
	}

	ImGui::SeparatorText("Native Palette");
	ImGui::Text("%d entries; %d unused in first LOD; %d duplicate values",
		count, unusedCount, duplicateCount);
	if (ImGui::BeginTable("Native palette entries", 6,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY,
			ImVec2(0.0f, 260.0f))) {
		ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 48.0f);
		ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 46.0f);
		ImGui::TableSetupColumn("Raw", ImGuiTableColumnFlags_WidthFixed, 58.0f);
		ImGui::TableSetupColumn("RGBA", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 54.0f);
		ImGui::TableSetupColumn("Duplicate", ImGuiTableColumnFlags_WidthFixed, 68.0f);
		ImGui::TableHeadersRow();
		for (s32 index = 0; index < count; ++index) {
			const struct modeldefEditorPaletteEntry *entry = &entries[index];
			ImGui::PushID(index);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%02x", index);
			ImGui::TableSetColumnIndex(1);
			const ImVec4 color(entry->red / 255.0f, entry->green / 255.0f,
				entry->blue / 255.0f, entry->alpha / 255.0f);
			ImGui::ColorButton("##palette", color,
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
				ImVec2(24.0f, 16.0f));
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%04x", entry->rawvalue);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%u, %u, %u, %u", entry->red, entry->green, entry->blue, entry->alpha);
			ImGui::TableSetColumnIndex(4);
			if (entry->usagecount > 0) ImGui::Text("%u", entry->usagecount); else ImGui::TextDisabled("unused");
			ImGui::TableSetColumnIndex(5);
			if (entry->duplicateof >= 0) ImGui::Text("0x%02x", entry->duplicateof); else ImGui::TextDisabled("-");
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}

static void imguiOverlayDrawWorkspaceLods(u16 textureId)
{
	struct modeldefEditorTextureLodInfo lods[8];
	const s32 count = modeldefEditorWorkspaceGetTextureLods(textureId, lods, ARRAYCOUNT(lods));
	if (count <= 0) return;

	ImGui::SeparatorText("Native LODs");
	if (ImGui::BeginTable("Native texture LODs", 7,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
		ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed, 36.0f);
		ImGui::TableSetupColumn("Dimensions", ImGuiTableColumnFlags_WidthFixed, 76.0f);
		ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 66.0f);
		ImGui::TableSetupColumn("TMEM off", ImGuiTableColumnFlags_WidthFixed, 66.0f);
		ImGui::TableSetupColumn("TMEM units", ImGuiTableColumnFlags_WidthFixed, 76.0f);
		ImGui::TableSetupColumn("Byte off", ImGuiTableColumnFlags_WidthFixed, 66.0f);
		ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();
		for (s32 index = 0; index < count; ++index) {
			const struct modeldefEditorTextureLodInfo *lod = &lods[index];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%u", lod->lod);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%ux%u", lod->width, lod->height);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(lod->embedded ? "embedded" : lod->lod == 0 ? "base" : "generated");
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%u", lod->tmemoffset);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%u", lod->tmemunits);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%u", lod->decodedoffset);
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%u", lod->decodedsize);
		}
		ImGui::EndTable();
	}
}

static void imguiOverlayDiagnosticRow(const char *stage, const char *result, const char *detail)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::TextUnformatted(stage);
	ImGui::TableSetColumnIndex(1);
	ImGui::TextUnformatted(result);
	ImGui::TableSetColumnIndex(2);
	ImGui::TextWrapped("%s", detail);
}

static void imguiOverlayDrawTextureRoutingDiagnostics(s32 textureMod, s32 modelFileNum,
		u16 localTexId, u16 portTexId)
{
	const u16 engineTexId = portTexId != localTexId ? portTexId : localTexId;
	struct modTextureResolveInfo resolution;
	modTextureResolveFileDetailed(textureMod, modelFileNum, engineTexId, &resolution);
	const bool hasExtTex = extTexModelHasEntryForTexid((s16)modelFileNum, localTexId);
	const bool hasRomTexture = imguiOverlayHasRomTexture(engineTexId);
	struct modeldefEditorWorkspaceInfo workspace;
	const bool workspaceMatches = modeldefEditorWorkspaceGetInfo(&workspace)
		&& workspace.fileid == imguiOverlayEncodeFileId(textureMod, modelFileNum);
	const bool workspaceTexture = workspaceMatches
		&& modeldefEditorWorkspaceFindTexture(engineTexId) != NULL;
	const bool probeMatches = g_ImGuiOverlayEngineProbeAttempted
		&& g_ImGuiOverlayEngineProbeModelFileNum == modelFileNum
		&& g_ImGuiOverlayEngineProbeTexId == engineTexId;
	struct GfxTextureDebugInfo submitted;
	const bool gpuImported = probeMatches && gfx_get_submitted_debug_texture(&submitted);
	char detail[256];

	ImGui::SeparatorText("Texture Routing Diagnostics");
	if (ImGui::BeginTable("Texture routing diagnostics", 3,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
		ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 108.0f);
		ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 76.0f);
		ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		snprintf(detail, sizeof(detail), "local 0x%04x -> engine 0x%04x%s",
			localTexId, engineTexId, engineTexId != localTexId ? " via texMap" : " (identity)");
		imguiOverlayDiagnosticRow("ID mapping", "ok", detail);

		snprintf(detail, sizeof(detail), "model 0x%04x, local 0x%04x%s",
			modelFileNum, localTexId, hasExtTex ? "; PNG-first path wins" : "; native path continues");
		imguiOverlayDiagnosticRow("ext_tex", hasExtTex ? "override" : "none", detail);

		for (s32 attemptIndex = 0; attemptIndex < resolution.attemptCount; ++attemptIndex) {
			const struct modTextureResolveAttempt *attempt = &resolution.attempts[attemptIndex];
			snprintf(detail, sizeof(detail), "%s%s", attempt->name,
				attempt->fileNum > 0 ? "" : " (not in mod filetable)");
			imguiOverlayDiagnosticRow(attemptIndex == 0 ? "Native lookup" : "", attempt->fileNum > 0 ? "hit" : "miss", detail);
		}
		if (resolution.attemptCount == 0) {
			imguiOverlayDiagnosticRow("Native lookup", "skipped", "invalid mod/model context");
		}

		if (resolution.fileNum > 0) {
			struct romdatafileslotinfo slotInfo;
			const bool hasSlot = romdataGetFileSlotInfo(textureMod, resolution.fileNum, &slotInfo);
			const s32 source = hasSlot && slotInfo.source != 0 ? slotInfo.source
				: hasSlot ? slotInfo.configuredSource : 0;
			snprintf(detail, sizeof(detail), "slot 0x%04x, local 0x%04x, source %s, %u bytes%s",
				resolution.fileNum, resolution.resolvedLocalId,
				hasSlot ? imguiOverlayFileSourceName(source) : "unknown",
				hasSlot ? slotInfo.size : 0,
				hasSlot && slotInfo.size > 4096 ? "; exceeds 4096-byte texture staging buffer" : "");
			imguiOverlayDiagnosticRow("Payload",
				hasSlot && slotInfo.size > 4096 ? "oversized" : "mod file", detail);
		} else if (hasRomTexture) {
			snprintf(detail, sizeof(detail), "g_Textures[0x%04x], %u compressed bytes",
				engineTexId, g_Textures[engineTexId + 1].dataoffset - g_Textures[engineTexId].dataoffset);
			imguiOverlayDiagnosticRow("Payload", "ROM bank", detail);
		} else {
			imguiOverlayDiagnosticRow("Payload", "missing", "no mod filetable match and no vanilla ROM texture span");
		}

		snprintf(detail, sizeof(detail), "%s%s", workspaceMatches ? "selected model loaded" : "selected model not loaded",
			workspaceMatches ? (workspaceTexture ? "; texture decoded in workspace pool" : "; texture absent from workspace pool") : "");
		imguiOverlayDiagnosticRow("Workspace", workspaceTexture ? "resident" : workspaceMatches ? "absent" : "not loaded", detail);

		imguiOverlayDiagnosticRow("CPU decode",
			!probeMatches ? "not run" : g_ImGuiOverlayEngineProbeDecoded ? "ok" : "failed",
			!probeMatches ? "click Load through engine" : g_ImGuiOverlayEngineProbeDecoded
				? "struct tex created and texture-only GDL queued" : "texLoad produced no matching struct tex");

		if (gpuImported) {
			snprintf(detail, sizeof(detail), "GL texture %u; type %u, model 0x%04x, texture 0x%04x",
				submitted.texture_id, submitted.type, submitted.id, submitted.texnum);
			imguiOverlayDiagnosticRow("Fast3D import", "ok", detail);
		} else {
			imguiOverlayDiagnosticRow("Fast3D import", probeMatches && g_ImGuiOverlayEngineProbeDecoded ? "pending" : "not run",
				probeMatches && g_ImGuiOverlayEngineProbeDecoded ? "waiting for the next renderer frame" : "CPU decode has not queued a texture GDL");
		}
		ImGui::EndTable();
	}
}

static float imguiOverlayWrapTextureCoord(float value, float size)
{
	if (size <= 0.0f) return 0.0f;
	value = fmodf(value, size);
	return value < 0.0f ? value + size : value;
}

static void imguiOverlayDrawTextureUvOverlay(const ImVec2 &imageMin, const ImVec2 &imageSize,
		s32 modelFileNum, u16 localTexId, u16 portTexId, s32 width, s32 height)
{
	if (!g_ImGuiOverlayShowTextureUvOverlay
			|| g_ImGuiOverlayTextureUsageModelFileNum != modelFileNum
			|| g_ImGuiOverlayTextureUsageLocalId != localTexId
			|| g_ImGuiOverlayTextureUsagePortId != portTexId
			|| width <= 0 || height <= 0) {
		return;
	}

	ImDrawList *drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true);
	for (s32 triangleIndex = 0; triangleIndex < g_ImGuiOverlayTextureTriangleCount; ++triangleIndex) {
		const struct modeldefTextureTriangle *triangle = &g_ImGuiOverlayTextureTriangles[triangleIndex];
		ImVec2 points[3];
		for (s32 vertex = 0; vertex < 3; ++vertex) {
			float s = triangle->s[vertex] / 32.0f;
			float t = triangle->t[vertex] / 32.0f;
			if (g_ImGuiOverlayWrapTextureUvs) {
				s = imguiOverlayWrapTextureCoord(s, width);
				t = imguiOverlayWrapTextureCoord(t, height);
			}
			if (!g_ImGuiOverlayRenderedTextureFlipY) {
				t = height - t;
			}
			points[vertex].x = imageMin.x + s / width * imageSize.x;
			points[vertex].y = imageMin.y + t / height * imageSize.y;
		}
		const ImU32 colour = triangle->listtype == 1
			? IM_COL32(80, 210, 255, 230) : IM_COL32(255, 210, 40, 230);
		drawList->AddPolyline(points, 3, colour, ImDrawFlags_Closed, 1.5f);
	}
	drawList->PopClipRect();
}

static void imguiOverlayScanTextureUsage(s32 textureMod, s32 modelFileNum,
		u16 localTexId, u16 portTexId)
{
	const s32 encodedFileNum = imguiOverlayEncodeFileId(textureMod, modelFileNum);
	g_ImGuiOverlayTextureUsageCount = modeldefInspectTextureUsage(encodedFileNum,
		localTexId, portTexId, g_ImGuiOverlayTextureUsage,
		ARRAYCOUNT(g_ImGuiOverlayTextureUsage), &g_ImGuiOverlayTextureUsageTotal,
		g_ImGuiOverlayTextureTriangles, ARRAYCOUNT(g_ImGuiOverlayTextureTriangles),
		&g_ImGuiOverlayTextureTriangleCount, &g_ImGuiOverlayTextureTriangleTotal);
	g_ImGuiOverlayTextureUsageModelFileNum = modelFileNum;
	g_ImGuiOverlayTextureUsageLocalId = localTexId;
	g_ImGuiOverlayTextureUsagePortId = portTexId;
}

static void imguiOverlayDrawRenderedTexturePreview(s32 textureMod, s32 modelFileNum,
		u16 localTexId, u16 portTexId, s32 textureFileNum)
{
	struct GfxTextureDebugInfo info;
	ImGui::SeparatorText("Engine Rendered Preview");
	const u16 engineTexId = portTexId != localTexId ? portTexId : localTexId;
	const bool hasTextureFile = textureFileNum > 0;
	const bool hasRomTexture = imguiOverlayHasRomTexture(engineTexId);
	const bool canEngineLoad = hasTextureFile || hasRomTexture;
	ImGui::BeginDisabled(!canEngineLoad);
	if (ImGui::Button("Load through engine")) {
		if (imguiOverlayRequestEngineTexture(textureMod, modelFileNum, textureFileNum, engineTexId)) {
			imguiOverlayScanTextureUsage(textureMod, modelFileNum, localTexId, portTexId);
		}
	}
	ImGui::EndDisabled();
	if (!canEngineLoad && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("No mod texture payload or ROM texture-bank entry exists for this ID.");
	}
	if (g_ImGuiOverlayEngineProbeModelFileNum == modelFileNum
			&& g_ImGuiOverlayEngineProbeTexId == engineTexId) {
		ImGui::SameLine();
		ImGui::TextDisabled(g_ImGuiOverlayEngineProbeDecoded
			? "private engine probe active" : "private engine probe failed to decode");
	}
	if (g_ImGuiOverlayEngineProbeMetadataValid
			&& g_ImGuiOverlayEngineProbeModelFileNum == modelFileNum
			&& g_ImGuiOverlayEngineProbeTexId == engineTexId) {
		if (g_ImGuiOverlayEngineProbeHeaderAvailable) {
			ImGui::Text("Native header: 0x%02x, %s, header LOD count %u",
				g_ImGuiOverlayEngineProbeHeader,
				(g_ImGuiOverlayEngineProbeHeader & 0x40) ? "zlib" : "native compression",
				g_ImGuiOverlayEngineProbeHeader & 0x3f);
		} else {
			ImGui::TextUnformatted("Native header: ROM texture bank (decoded metadata below)");
		}
		ImGui::Text("Decoded: %ux%u %s (GBI format %u, depth %u)",
			g_ImGuiOverlayEngineProbeWidth, g_ImGuiOverlayEngineProbeHeight,
			g_ImGuiOverlayEngineProbeHeaderAvailable
				? imguiOverlayTextureFormatName(g_ImGuiOverlayEngineProbeNativeFormat) : "engine native",
			g_ImGuiOverlayEngineProbeFormat, g_ImGuiOverlayEngineProbeDepth);
		if (g_ImGuiOverlayEngineProbeHeaderAvailable) {
			ImGui::Text("PD format code: 0x%02x", g_ImGuiOverlayEngineProbeNativeFormat);
		}
		ImGui::Text("LOD: %u, embedded LOD data: %s; palette: %u entries, LUT mode %u",
			g_ImGuiOverlayEngineProbeLodCount,
			g_ImGuiOverlayEngineProbeHasLodData ? "yes" : "no",
			g_ImGuiOverlayEngineProbePaletteCount, g_ImGuiOverlayEngineProbeLutMode);
		if (g_ImGuiOverlayEngineProbeHeaderAvailable) {
			ImGui::Text("Bytes: compressed %u, decoded pool payload %u",
				g_ImGuiOverlayEngineProbeCompressedSize, g_ImGuiOverlayEngineProbeDecodedSize);
		} else {
			ImGui::Text("Bytes: decoded pool payload %u", g_ImGuiOverlayEngineProbeDecodedSize);
		}
		imguiOverlayDrawWorkspaceLods(engineTexId);
		imguiOverlayDrawWorkspacePalette(engineTexId);
	}

	const bool privateProbeMatches = g_ImGuiOverlayEngineProbeAttempted
		&& g_ImGuiOverlayEngineProbeModelFileNum == modelFileNum
		&& g_ImGuiOverlayEngineProbeTexId == engineTexId;
	const bool hasRenderedTexture = privateProbeMatches && g_ImGuiOverlayEngineProbeDecoded
		? gfx_get_submitted_debug_texture(&info)
		: imguiOverlayFindRenderedTexture(modelFileNum, localTexId, portTexId, &info);
	if (!hasRenderedTexture) {
		if (privateProbeMatches && !g_ImGuiOverlayEngineProbeDecoded) {
			ImGui::TextDisabled("CPU texture load/decode failed.");
		} else if (privateProbeMatches) {
			ImGui::TextDisabled("CPU decode succeeded; waiting for Fast3D import.");
		} else {
			ImGui::TextDisabled("Not imported this frame. Load it above or keep the model visible.");
		}
		return;
	}
	if (!g_ImGuiOverlayRenderedTexturePixels.empty()
			&& (g_ImGuiOverlayRenderedPixelsModelFileNum != info.id
				|| g_ImGuiOverlayRenderedPixelsTexId != (s32)info.texnum)) {
		imguiOverlayClearRenderedPixels();
	}

	GLint previousBinding = 0;
	GLint width = 0;
	GLint height = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousBinding);
	glBindTexture(GL_TEXTURE_2D, info.texture_id);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
	glBindTexture(GL_TEXTURE_2D, previousBinding);

	if (width <= 0 || height <= 0) {
		ImGui::TextDisabled("Renderer cache entry has no uploaded image.");
		return;
	}

	ImGui::Text("type %u, model 0x%04x, tex 0x%04x, GL %u, %dx%d",
		info.type, info.id, info.texnum, info.texture_id, width, height);
	const char *selectedModelName = romdataFileGetSlotName(textureMod, modelFileNum);
	ImGui::SeparatorText("Compare Engine Output");
	ImGui::TextDisabled("Raw RGBA A/B comparison; operands must have matching dimensions.");
	if (g_ImGuiOverlayReferenceTexturePixels.empty()) {
		ImGui::TextDisabled("Reference A: not set");
	} else {
		ImGui::Text("Reference A: %s | texture 0x%04x | %ux%u",
			g_ImGuiOverlayReferenceModelName, g_ImGuiOverlayReferenceTexId,
			g_ImGuiOverlayReferencePixelsWidth, g_ImGuiOverlayReferencePixelsHeight);
	}
	ImGui::Text("Current B: %s | local 0x%04x | engine 0x%04x | %dx%d",
		selectedModelName ? selectedModelName : "unknown model", localTexId, info.texnum, width, height);
	if (ImGui::Button("Set current as reference A")) {
		imguiOverlayPinRenderedReference(&info, modelFileNum, localTexId, selectedModelName, width, height);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(g_ImGuiOverlayReferenceTexturePixels.empty());
	if (ImGui::Button("Compare A with current B")) {
		imguiOverlayCaptureRenderedPixels(&info, width, height);
		imguiOverlayCompareNativeTextures();
	}
	ImGui::EndDisabled();
	if (!g_ImGuiOverlayReferenceTexturePixels.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear A")) {
			g_ImGuiOverlayReferenceTexturePixels.clear();
			g_ImGuiOverlayReferencePixelsWidth = 0;
			g_ImGuiOverlayReferencePixelsHeight = 0;
			g_ImGuiOverlayReferenceModelFileNum = -1;
			g_ImGuiOverlayReferenceTexId = -1;
			g_ImGuiOverlayReferenceModelName[0] = '\0';
			g_ImGuiOverlayNativeCompareValid = false;
		}
	}
	if (!g_ImGuiOverlayReferenceTexturePixels.empty()
			&& (g_ImGuiOverlayReferencePixelsWidth != (u32)width
				|| g_ImGuiOverlayReferencePixelsHeight != (u32)height)) {
		ImGui::Text("Cannot compare pixels: A is %ux%u, B is %dx%d",
			g_ImGuiOverlayReferencePixelsWidth, g_ImGuiOverlayReferencePixelsHeight, width, height);
	} else if (g_ImGuiOverlayNativeCompareValid) {
		const u64 pixelCount = (u64)g_ImGuiOverlayRenderedPixelsWidth * g_ImGuiOverlayRenderedPixelsHeight;
		const double meanDelta = pixelCount > 0
			? (double)g_ImGuiOverlayNativeCompareChannelDelta / (double)(pixelCount * 4) : 0.0;
		ImGui::Text("A/B result: %llu / %llu pixels differ (%.2f%%); mean delta %.3f, max %u",
			(unsigned long long)g_ImGuiOverlayNativeCompareDifferentPixels,
			(unsigned long long)pixelCount,
			pixelCount > 0 ? 100.0 * g_ImGuiOverlayNativeCompareDifferentPixels / pixelCount : 0.0,
			meanDelta, g_ImGuiOverlayNativeCompareMaxDelta);
	}
	if (ImGui::Button("Capture B pixels for hover inspection")) {
		imguiOverlayCaptureRenderedPixels(&info, width, height);
	}
	ImGui::SameLine();
	if (ImGui::Button("Export current B PNG")) {
		if (imguiOverlayCaptureRenderedPixels(&info, width, height)) {
			imguiOverlayExportTexturePng(selectedModelName, localTexId,
				g_ImGuiOverlayRenderedTexturePixels.data(), width, height, "B");
		}
	}
	if (!g_ImGuiOverlayReferenceTexturePixels.empty()) {
		ImGui::SameLine();
		if (ImGui::Button("Export reference A PNG")) {
			imguiOverlayExportTexturePng(g_ImGuiOverlayReferenceModelName,
				g_ImGuiOverlayReferenceTexId, g_ImGuiOverlayReferenceTexturePixels.data(),
				g_ImGuiOverlayReferencePixelsWidth, g_ImGuiOverlayReferencePixelsHeight, "A");
		}
	}
	if (g_ImGuiOverlayTextureExportStatus[0]) {
		ImGui::TextWrapped("%s", g_ImGuiOverlayTextureExportStatus);
	}
	ImGui::SetNextItemWidth(140.0f);
	ImGui::SliderInt("Rendered zoom", &g_ImGuiOverlayRenderedTextureZoom, 1, 16, "%dx");
	ImGui::SameLine();
	if (ImGui::Checkbox("Flip Y", &g_ImGuiOverlayRenderedTextureFlipY)) {
		g_ImGuiOverlayTextureCompareValid = false;
	}
	ImGui::Checkbox("UV overlay", &g_ImGuiOverlayShowTextureUvOverlay);
	ImGui::SameLine();
	ImGui::Checkbox("Wrap UVs", &g_ImGuiOverlayWrapTextureUvs);
	const bool usageMatches = g_ImGuiOverlayTextureUsageModelFileNum == modelFileNum
		&& g_ImGuiOverlayTextureUsageLocalId == localTexId
		&& g_ImGuiOverlayTextureUsagePortId == portTexId;
	if (!usageMatches) {
		ImGui::TextDisabled("UV overlay: model usage has not been scanned.");
	} else if (g_ImGuiOverlayTextureTriangleCount <= 0) {
		ImGui::TextDisabled("UV overlay: no safely attributed triangles found (%d references).",
			g_ImGuiOverlayTextureUsageTotal);
	} else if (g_ImGuiOverlayShowTextureUvOverlay) {
		ImGui::TextDisabled("UV overlay: opaque yellow, translucent cyan; wrapping normalizes each vertex.");
	}

	const ImVec2 imageSize((float)width * g_ImGuiOverlayRenderedTextureZoom,
		(float)height * g_ImGuiOverlayRenderedTextureZoom);
	if (ImGui::BeginChild("Rendered texture preview", ImVec2(0.0f, ImMin(imageSize.y + 12.0f, 520.0f)),
			ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
		const ImVec2 uv0 = g_ImGuiOverlayRenderedTextureFlipY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
		const ImVec2 uv1 = g_ImGuiOverlayRenderedTextureFlipY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
		ImGui::Image(ImTextureRef((ImTextureID)info.texture_id), imageSize, uv0, uv1);
		imguiOverlayDrawTextureUvOverlay(ImGui::GetItemRectMin(), imageSize,
			modelFileNum, localTexId, portTexId, width, height);
		if (ImGui::IsItemHovered() && !g_ImGuiOverlayRenderedTexturePixels.empty()) {
			const ImVec2 imageMin = ImGui::GetItemRectMin();
			const ImVec2 mousePos = ImGui::GetIO().MousePos;
			const u32 x = ImMin((u32)((mousePos.x - imageMin.x) * width / imageSize.x), (u32)width - 1);
			const u32 y = ImMin((u32)((mousePos.y - imageMin.y) * height / imageSize.y), (u32)height - 1);
			const u32 storedY = g_ImGuiOverlayRenderedTextureFlipY ? height - 1 - y : y;
			const u8 *pixel = &g_ImGuiOverlayRenderedTexturePixels[(storedY * width + x) * 4];
			ImGui::SetTooltip("(%u, %u)\nRGBA %u, %u, %u, %u\n#%02X%02X%02X%02X",
				x, y, pixel[0], pixel[1], pixel[2], pixel[3], pixel[0], pixel[1], pixel[2], pixel[3]);
		}
	}
	ImGui::EndChild();

	if (g_ImGuiOverlayTexturePreviewPixels && !g_ImGuiOverlayRenderedTexturePixels.empty()) {
		ImGui::SeparatorText("PNG Pipeline Check");
		if (ImGui::Button("Compare source PNG with engine output")) {
			imguiOverlayCompareTexturePixels();
		}
		if (g_ImGuiOverlayTexturePreviewWidth != g_ImGuiOverlayRenderedPixelsWidth
				|| g_ImGuiOverlayTexturePreviewHeight != g_ImGuiOverlayRenderedPixelsHeight) {
			ImGui::Text("Dimension mismatch: source %ux%u, rendered %ux%u",
				g_ImGuiOverlayTexturePreviewWidth, g_ImGuiOverlayTexturePreviewHeight,
				g_ImGuiOverlayRenderedPixelsWidth, g_ImGuiOverlayRenderedPixelsHeight);
		} else if (g_ImGuiOverlayTextureCompareValid) {
			const u64 pixelCount = (u64)g_ImGuiOverlayRenderedPixelsWidth * g_ImGuiOverlayRenderedPixelsHeight;
			const double meanDelta = pixelCount > 0
				? (double)g_ImGuiOverlayTextureCompareChannelDelta / (double)(pixelCount * 4) : 0.0;
			ImGui::Text("Different pixels: %llu / %llu (%.2f%%)",
				(unsigned long long)g_ImGuiOverlayTextureCompareDifferentPixels,
				(unsigned long long)pixelCount,
				pixelCount > 0 ? 100.0 * g_ImGuiOverlayTextureCompareDifferentPixels / pixelCount : 0.0);
			ImGui::Text("Channel delta: mean %.3f, max %u", meanDelta, g_ImGuiOverlayTextureCompareMaxDelta);
		}
	}
}

static void imguiOverlayDrawTextureUsage(s32 textureMod, s32 modelFileNum, u16 localTexId, u16 portTexId)
{
	ImGui::SeparatorText("Model GDL Usage");
	if (ImGui::Button("Scan model GDL usage")) {
		imguiOverlayScanTextureUsage(textureMod, modelFileNum, localTexId, portTexId);
	}

	if (g_ImGuiOverlayTextureUsageModelFileNum != modelFileNum
			|| g_ImGuiOverlayTextureUsageLocalId != localTexId
			|| g_ImGuiOverlayTextureUsagePortId != portTexId) {
		ImGui::TextDisabled("Scan the selected model for pre-expansion texture bindings.");
		return;
	}

	ImGui::Text("References: %d total, %d shown; triangles: %d total, %d shown",
		g_ImGuiOverlayTextureUsageTotal, g_ImGuiOverlayTextureUsageCount,
		g_ImGuiOverlayTextureTriangleTotal, g_ImGuiOverlayTextureTriangleCount);
	ImGui::TextDisabled("UV bounds cover all vertices owned by the matching node; raw S/T use 5 fractional bits.");
	if (g_ImGuiOverlayTextureUsageCount <= 0) {
		return;
	}

	if (ImGui::BeginTable("Model texture usage", 7,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY,
			ImVec2(0.0f, 240.0f))) {
		ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 72.0f);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 50.0f);
		ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthFixed, 42.0f);
		ImGui::TableSetupColumn("Cmd", ImGuiTableColumnFlags_WidthFixed, 48.0f);
		ImGui::TableSetupColumn("Slot/ID", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Vertices", ImGuiTableColumnFlags_WidthFixed, 58.0f);
		ImGui::TableSetupColumn("Node UV envelope", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (s32 index = 0; index < g_ImGuiOverlayTextureUsageCount; ++index) {
			const struct modeldefTextureUsage *usage = &g_ImGuiOverlayTextureUsage[index];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("+0x%04x", usage->nodeoffset);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("0x%02x", usage->nodetype & 0xff);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(usage->listtype == 0 ? "opa" : usage->listtype == 1 ? "xlu" : "other");
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%u", usage->commandindex);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%u/%03x", usage->textureslot, usage->textureid);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%d", usage->numvertices);
			ImGui::TableSetColumnIndex(6);
			if (usage->numvertices > 0) {
				ImGui::Text("S %.2f..%.2f, T %.2f..%.2f",
					usage->minS / 32.0f, usage->maxS / 32.0f,
					usage->minT / 32.0f, usage->maxT / 32.0f);
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("raw S %d..%d\nraw T %d..%d",
						usage->minS, usage->maxS, usage->minT, usage->maxT);
				}
			} else {
				ImGui::TextDisabled("unavailable");
			}
		}
		ImGui::EndTable();
	}
}

// --- off-screen texture access for the skin match panel -----------------
// Texture ids a model uses, read from the modeldef rather than from what the
// renderer happened to draw this frame. Mod-local ids are mapped to their port
// ids, which is what the draw stamps and what the sidecars key on.
s32 imguiOverlayModelTextureIds(s32 fileid, u16 *out, s32 max)
{
	struct modeldefTextureUsage usages[512];
	s32 total = 0;
	const s32 textureMod = MOD_FILEID_MOD(fileid);
	const s32 count = modeldefInspectTextureUsage(fileid, 0xffff, 0xffff, usages, ARRAYCOUNT(usages), &total, NULL, 0, NULL, NULL);
	s32 n = 0;

	for (s32 i = 0; i < count && n < max; i++) {
		u16 id = usages[i].textureid;
		if (textureMod >= 0) {
			const u16 port = modTexMapLookup(textureMod, id);
			if (port != 0xffff && port != 0) id = port;
		}
		bool seen = false;
		for (s32 k = 0; k < n; k++) if (out[k] == id) { seen = true; break; }
		if (!seen) out[n++] = id;
	}

	return n;
}

// Get a GL texture for (model, texture id) whether or not the model is on
// screen: what the renderer drew this frame if it did, otherwise the private
// engine probe (texLoad into the probe pool, a one-texture display list the
// renderer runs next frame). The probe goes through texWriteLoadToTmemAddr
// with g_TexCurrentModelFileNum set, so it is stamped with the model's file
// number and skin match measures it exactly as a live draw. One probe lives at
// a time and the result is a frame late; callers just ask again.
bool imguiOverlayProbeModelTexture(s32 fileid, u16 texId, struct GfxTextureDebugInfo *out)
{
	const s32 raw = MOD_FILEID_RAW(fileid);
	s32 textureMod = MOD_FILEID_MOD(fileid);

	if (imguiOverlayFindRenderedTexture(raw, texId, texId, out)) {
		return true;
	}

	if (g_ImGuiOverlayEngineProbeModelFileNum == raw && g_ImGuiOverlayEngineProbeTexId == texId) {
		if (g_ImGuiOverlayEngineProbeDecoded && gfx_get_submitted_debug_texture(out)) {
			return true;
		}
		if (g_ImGuiOverlayEngineProbeAttempted) {
			return false; // in flight, or failed - do not spam the pool
		}
	}

	if (textureMod < 0) {
		textureMod = g_ModNum >= 0 ? g_ModNum : 0;
	}

	u16 resolvedLocal = texId;
	char name[128] = { 0 };
	const s32 textureFileNum = raw > 0 ? modTextureResolveFile(textureMod, raw, texId, &resolvedLocal, name, sizeof(name)) : 0;
	if (textureFileNum <= 0 && !imguiOverlayHasRomTexture(texId)) {
		return false;
	}

	imguiOverlayRequestEngineTexture(textureMod, raw, textureFileNum, texId);
	return false;
}

static void imguiOverlayDrawTexturesPanel(void)
{
	const s32 currentTextureMod = g_TexModNum;
	const s32 currentModelFileNum = g_TexCurrentModelFileNum & 0xffff;
	s32 textureMod = currentTextureMod;
	s32 modelFileNum = currentModelFileNum;
	const char *modelName = NULL;

	if (g_ImGuiOverlayTextureModelMod >= 0 && g_ImGuiOverlayTextureModelFileNum > 0) {
		textureMod = g_ImGuiOverlayTextureModelMod;
		modelFileNum = g_ImGuiOverlayTextureModelFileNum;
	}
	if (textureMod >= 0 && modelFileNum > 0
			&& (g_ImGuiOverlayScannedTextureModelMod != textureMod
				|| g_ImGuiOverlayScannedTextureModelFileNum != modelFileNum)) {
		gfx_submit_debug_texture_gdl(NULL);
		if (g_ImGuiOverlayTextureProbeData) {
			gfx_forget_debug_texture_data(g_ImGuiOverlayTextureProbeData);
			g_ImGuiOverlayTextureProbeData = NULL;
		}
		imguiOverlayClearTexturePreview();
		imguiOverlayClearRenderedPixels();
		g_ImGuiOverlayEngineProbeMetadataValid = false;
		g_ImGuiOverlayEngineProbeAttempted = false;
		g_ImGuiOverlayEngineProbeDecoded = false;
		g_ImGuiOverlayEngineProbeModelFileNum = -1;
		g_ImGuiOverlayEngineProbeTexId = -1;
		g_ImGuiOverlayTextureProbeId = 0;
		modeldefEditorWorkspaceUnload();
		imguiOverlayScanModelTextureIds(textureMod, modelFileNum);
	}

	if (textureMod >= 0 && modelFileNum > 0) {
		modelName = romdataFileGetSlotName(textureMod, modelFileNum);
	}
	const char *textureDirName = imguiOverlayModelTextureDirName(modelName);

	ImGui::SeparatorText("Current Texture Context");
	ImGui::Text("Runtime texture mod: %d", currentTextureMod);
	ImGui::Text("Runtime model file: 0x%04x", currentModelFileNum);
	ImGui::Text("Probe texture mod: %d", textureMod);
	ImGui::Text("Probe model file: 0x%04x", modelFileNum);
	ImGui::Text("Model name: %s", modelName ? modelName : "none");
	ImGui::Text("Texture dir: %s", textureDirName ? textureDirName : "none");
	ImGui::Text("Mod texMap entries: %d", modTexMapGetCount(textureMod));

	imguiOverlayDrawTextureModelSearch(currentTextureMod, currentModelFileNum);
	if (textureMod >= 0 && modelFileNum > 0) {
		imguiOverlayDrawModelWorkspace(textureMod, modelFileNum);
	}

	ImGui::SeparatorText("Texture ID Probe");
	ImGui::SetNextItemWidth(110.0f);
	ImGui::InputInt("Local/GDL texture ID", &g_ImGuiOverlayTextureProbeId, 1, 16, ImGuiInputTextFlags_CharsHexadecimal);
	if (g_ImGuiOverlayTextureProbeId < 0) {
		g_ImGuiOverlayTextureProbeId = 0;
	}
	if (g_ImGuiOverlayTextureProbeId > 0xffff) {
		g_ImGuiOverlayTextureProbeId = 0xffff;
	}

	const u16 localTexId = (u16)g_ImGuiOverlayTextureProbeId;
	const u16 portTexId = textureMod >= 0 ? modTexMapLookup(textureMod, localTexId) : 0xffff;
	const u16 reverseLocalTexId = textureMod >= 0 ? modTexMapReverseLookup(textureMod, localTexId) : 0xffff;
	const bool hasProbeId = g_ImGuiOverlayTextureProbeId > 0;
	const bool localMapped = hasProbeId && textureMod >= 0 && portTexId != localTexId;
	const bool reverseMapped = hasProbeId && textureMod >= 0 && reverseLocalTexId != 0xffff;
	const bool hasModelExtTex = modelFileNum > 0 && hasProbeId
		&& extTexModelHasEntryForTexid((s16)modelFileNum, localTexId);
	const u16 engineTexId = localMapped ? portTexId : localTexId;
	u16 resolvedLocalId = engineTexId;
	char resolvedTextureName[128] = { 0 };
	const s32 textureFileNum = modelFileNum > 0 && hasProbeId
		? modTextureResolveFile(textureMod, modelFileNum, engineTexId,
			&resolvedLocalId, resolvedTextureName, sizeof(resolvedTextureName))
		: 0;
	const bool hasTextureFile = textureFileNum > 0;
	ImGui::Text("local -> port: %s", !hasProbeId ? "enter texture ID" : localMapped ? "mapped" : "unmapped");
	if (localMapped) {
		ImGui::SameLine();
		ImGui::Text("0x%04x", portTexId);
	}
	ImGui::Text("port -> local: %s", !hasProbeId ? "enter texture ID" : reverseMapped ? "mapped" : "unmapped");
	if (reverseMapped) {
		ImGui::SameLine();
		ImGui::Text("0x%04x", reverseLocalTexId);
	}

	if (modelFileNum > 0 && hasProbeId) {
		const s8 owner = extTexGetOwnerMod(1, (u16)modelFileNum, localTexId);
		u16 width = 0;
		u16 height = 0;
		const u8 hasDimensions = extTexGetDimensions(1, (u16)modelFileNum, localTexId, &width, &height);
		const bool hasRomTexture = imguiOverlayHasRomTexture(engineTexId);
		ImGui::Text("texture source: %s", hasTextureFile ? "mod filetable" : hasRomTexture ? "ROM texture bank" : "missing");
		if (textureFileNum > 0) {
			ImGui::SameLine();
			ImGui::Text("file 0x%04x", textureFileNum);
			ImGui::Text("resolved path: %s", resolvedTextureName);
			ImGui::Text("engine ID 0x%04x -> file local ID 0x%04x", engineTexId, resolvedLocalId);
		} else if (hasRomTexture) {
			ImGui::Text("engine ID 0x%04x, ROM bytes %u", engineTexId,
				g_Textures[engineTexId + 1].dataoffset - g_Textures[engineTexId].dataoffset);
		}
		ImGui::Text("ext_tex model entry: %s", hasModelExtTex ? "yes" : "no");
		ImGui::Text("ext_tex owner mod: %d", owner);
		if (hasDimensions) {
			ImGui::Text("ext_tex dimensions: %ux%u", width, height);
		} else {
			ImGui::TextUnformatted("ext_tex dimensions: unavailable");
		}
	} else if (modelFileNum > 0) {
		ImGui::TextUnformatted("texture source: enter texture ID");
		ImGui::TextUnformatted("ext_tex model entry: enter texture ID");
	}
	if (modelFileNum > 0 && hasProbeId) {
		imguiOverlayDrawTextureRoutingDiagnostics(textureMod, modelFileNum, localTexId, portTexId);
	}
	imguiOverlayDrawTexturePreview(modelFileNum, localTexId, hasModelExtTex);
	if (modelFileNum > 0 && hasProbeId) {
		imguiOverlayDrawRenderedTexturePreview(textureMod, modelFileNum, localTexId, portTexId, textureFileNum);
		imguiOverlayDrawTextureUsage(textureMod, modelFileNum, localTexId, portTexId);
	}

	imguiOverlayDrawModelTextureFiles(textureMod, modelFileNum, textureDirName);

	ImGui::SeparatorText("Authoring Checks");
	ImGui::BulletText("Use GDL runtime texture IDs for textureId, .bin names, and texMap keys.");
	ImGui::BulletText("Do not use texconfig ptr_raw except when debugging ROM texture-bank layout.");
	ImGui::BulletText("Prefer per-model texture paths: textures/<ModelName>/<localTexId>.bin.");
	ImGui::BulletText("PNG previews use model-scoped ext_tex ownership, nearest-neighbor sampling, and GL_UNPACK_ALIGNMENT=1.");
	ImGui::BulletText("Rendered snapshots read the exact Fast3D cache texture; comparison isolates decode/upload differences from UV placement.");

	ImGui::SeparatorText("Screenshot Alignment Plan");
	ImGui::TextWrapped("First compare source and rendered pixels. If they match, continue with screenshot crops and diagnostic X/Y offsets before changing UV code.");
}

static void imguiOverlaySetVisible(bool visible)
{
	if (g_ImGuiOverlayVisible == visible) {
		return;
	}

	g_ImGuiOverlayVisible = visible;
	ImGuiIO &io = ImGui::GetIO();
	io.MouseDrawCursor = visible;

	if (visible) {
		g_ImGuiOverlayRestoreMouseLock = inputMouseIsLocked() != 0;
		inputLockMouse(0);
		inputMouseShowCursor(1);
	} else {
		ImGui::SaveIniSettingsToDisk(g_ImGuiOverlayIniPath);
		if (g_ImGuiOverlayRestoreMouseLock) {
			inputLockMouse(1);
			g_ImGuiOverlayRestoreMouseLock = false;
		}
	}
}

// ---------------------------------------------------------------------------
// Proportions panel
//
// Latch onto a spawned chr and mutate her height, model scale and joint scales
// live. Everything here writes per-chr overrides rather than the shared
// g_HeadsAndBodies row, because bodies are shared -- writing the row while
// latched onto a guard would resize every guard wearing it.
//
// The three knobs have three different latencies. Joint scales are read every
// frame in chrHandleJointPositioned, so they are live for free. Model scale is
// a one-line setter on the live model. Height was copied into vv_eyeheight when
// the chr body was built, so it has to be re-applied through playerSetHeight,
// and only means anything when the latched chr is the player -- AI use a flat
// chr->height and never consult the body row.
// ---------------------------------------------------------------------------

static void imguiPropRelease(void)
{
	g_ImGuiPropChr = NULL;
	g_ImGuiPropChrnum = -1;
	g_JointScaleChr = NULL;
}

/**
 * The latch survives the panel being closed -- only losing the chr should
 * end it. A slot pointer alone is not enough to say the chr is still the
 * same one: chrnum goes to -1 when a slot is freed and a fresh number is
 * assigned when it is reused, and g_ChrSlots is reallocated per stage, so a
 * stale pointer can land in bounds on somebody else entirely. Pin the chrnum
 * at latch and compare it, or edits meant for one character silently follow
 * the slot to whoever spawns into it next.
 */
static bool imguiPropLatchIsLive(void)
{
	return g_ImGuiPropChr
		&& imguiOverlayChrIsCurrent(g_ImGuiPropChr)
		&& (s32)g_ImGuiPropChr->chrnum == g_ImGuiPropChrnum;
}

static bool imguiPropChrIsPlayer(struct chrdata *chr)
{
	return chr && g_Vars.currentplayer
		&& g_Vars.currentplayer->prop
		&& g_Vars.currentplayer->prop->chr == chr;
}

static s32 imguiPropCountSharingBody(s32 bodynum)
{
	s32 n = 0;

	if (!g_ChrSlots) {
		return 0;
	}

	for (s32 i = 0; i < g_NumChrSlots; i++) {
		if (g_ChrSlots[i].chrnum >= 0 && g_ChrSlots[i].bodynum == bodynum) {
			n++;
		}
	}

	return n;
}

static s32 imguiPropJointCount(struct chrdata *chr)
{
	if (!chr || !chr->model || !chr->model->definition || !chr->model->definition->skel) {
		return 0;
	}

	s32 n = (s32)chr->model->definition->skel->numthings;

	return n > kFojoMaxJointOverrides ? kFojoMaxJointOverrides : n;
}

static s32 imguiPropJointMirror(struct chrdata *chr, s32 joint)
{
	if (!chr || !chr->model || !chr->model->definition || !chr->model->definition->skel) {
		return joint;
	}

	struct skeleton *skel = chr->model->definition->skel;

	if (joint < 0 || joint >= (s32)skel->numthings) {
		return joint;
	}

	return (s32)skel->things[joint][1];
}

static void imguiPropLatch(struct chrdata *chr)
{
	if (!imguiOverlayChrIsCurrent(chr)) {
		return;
	}

	g_ImGuiPropChr = chr;
	g_ImGuiPropChrnum = (s32)chr->chrnum;

	s32 bodynum = (s32)chr->bodynum;
	bool bodyok = bodynum >= 0 && bodynum < g_NumHeadsAndBodies;

	g_ImGuiPropBaseHeight = bodyok ? (s32)g_HeadsAndBodies[bodynum].height : 0;
	g_ImGuiPropBaseScale = bodyok ? g_HeadsAndBodies[bodynum].scale : 1.0f;
	for (s32 i = 0; i < kFojoMaxHitPart; i++) {
		g_ImGuiPropPartJoint[i] = -1;
	}

	g_ImGuiPropHeight = (f32)g_ImGuiPropBaseHeight;
	g_ImGuiPropScale = g_ImGuiPropBaseScale;
	g_ImGuiPropMarkJoint = -1;

	// The override table is uninitialised until something latches. Fill it with
	// identity rather than relying on the engine-side zero-is-unset guard, so
	// the values the panel shows are the values the engine reads.
	for (s32 i = 0; i < kFojoMaxJointOverrides; i++) {
		g_JointScaleOverride[i][0] = 1.0f;
		g_JointScaleOverride[i][1] = 1.0f;
		g_JointScaleOverride[i][2] = 1.0f;
	}
}

static void imguiPropApplyLive(struct chrdata *chr)
{
	s32 bodynum = (s32)chr->bodynum;
	bool bodyok = bodynum >= 0 && bodynum < g_NumHeadsAndBodies;

	if (!g_ImGuiPropApply) {
		g_JointScaleChr = NULL;

		if (chr->model && bodyok) {
			modelSetScale(chr->model, g_ImGuiPropBaseScale * 0.10000001f);
		}

		if (imguiPropChrIsPlayer(chr)) {
			playerSetHeight(g_ImGuiPropBaseHeight, (s32)chr->headnum);
		}

		return;
	}

	g_JointScaleChr = chr;

	if (chr->model) {
		modelSetScale(chr->model, g_ImGuiPropScale * 0.10000001f);
	}

	if (imguiPropChrIsPlayer(chr)) {
		playerSetHeight((s32)(g_ImGuiPropHeight + 0.5f), (s32)chr->headnum);
	}
}

// ----------------------------------------------------------------------------
// Stance panel
//
// The numbers behind the stance system, live. Every one of them started as a
// guess that only play could settle, so the point of this panel is to settle
// them without a rebuild - move a slider, walk into a room, decide. What sticks
// goes into pd.ini under [Stance]; stance-tuning.md says what each one does and
// what it interacts with.
//
// Everything here is read every frame by the thing it governs, so a change
// takes effect on the next one. Nothing here is saved by the panel itself.
// ----------------------------------------------------------------------------

static void imguiOverlayStanceKnob(const char *label, f32 *value, f32 min, f32 max,
		const char *fmt, const char *help)
{
	ImGui::SliderFloat(label, value, min, max, fmt);

	if (help && ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", help);
	}
}

static void imguiOverlayDrawStancePanel(void)
{
	f32 degrees;

	ImGui::TextDisabled("Defaults live in constants.h. pd.ini [Stance] sets where these start.");
	ImGui::Separator();

	{
		bool fojo = g_FojoMovement != 0;

		if (ImGui::Checkbox("Fojo movement", &fojo)) {
			g_FojoMovement = fojo ? 1 : 0;
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("The two-stance system, and only that: third person forced,\n"
					"the aim button as the door into the low ready, the low ready\n"
					"and reload speed penalties, and the stance lock.\n"
					"\n"
					"Off is the port's shape - third person is a view on its own\n"
					"bind and aiming is free. Jump, the roll, the flinch and the\n"
					"melee combos are not part of this and are always on.\n"
					"\n"
					"Off by default. Takes effect on the next frame; a body that\n"
					"exists already is taken down the way it was built.");
		}

		if (!fojo) {
			ImGui::TextDisabled("(stance knobs below do nothing while this is off)");
		}
	}

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Stances", ImGuiTreeNodeFlags_DefaultOpen)) {
		imguiOverlayStanceKnob("Low ready speed", &g_AimStanceSpeed, 0.1f, 1.0f, "%.2f",
				"What fraction of her walk she keeps while aiming.\n"
				"Stacks with the crouch multipliers: 0.5 ducked, 0.35 squatting.");
	}

	if (ImGui::CollapsingHeader("Flinch", ImGuiTreeNodeFlags_DefaultOpen)) {
		imguiOverlayStanceKnob("Flinch speed", &g_FlinchSpeed, 0.1f, 1.0f, "%.2f",
				"What she is cut to at the moment a shot lands.\n"
				"Recovers linearly to full across the flinch.");

		ImGui::SliderInt("Flinch ticks", &g_FlinchBusy, 0, 240);

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("The window used when a flinch starts no animation -\n"
					"no body to play one, or a body already busy with a roll.\n"
					"When an animation does play, its own length is used instead.");
		}

		ImGui::SliderInt("Flinch ticks max", &g_FlinchBusyMax, 0, 600);

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("The ceiling no reel may exceed, whatever its animation says.\n"
					"This is what stops a frozen animation pinning her.");
		}
	}

	if (ImGui::CollapsingHeader("Melee", ImGuiTreeNodeFlags_DefaultOpen)) {
		imguiOverlayStanceKnob("Body reach", &g_MeleeBodyReach, 0.0f, 400.0f, "%.0f",
				"How far a swing reaches past the weapon's own melee range,\n"
				"measured from her rather than from the camera.\n"
				"60 plus a bare hand's own 60 is 120 - a human guard's punch.");

		degrees = acosf(g_MeleeConeCos) * 180.0f / 3.14159265f;

		if (ImGui::SliderFloat("Cone (deg either side)", &degrees, 5.0f, 90.0f, "%.0f")) {
			g_MeleeConeCos = cosf(degrees * 3.14159265f / 180.0f);
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("How far off the direction she is looking a target may stand.\n"
					"Horizontal only, so looking at the floor does not stop a punch.\n"
					"Stored as a cosine; the sweep compares against that directly.");
		}
	}

	if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		imguiOverlayStanceKnob("Camera distance", &g_ThirdPersonCamDist, 0.0f, 1000.0f, "%.0f",
				"How far behind her the camera wants to be.");
		imguiOverlayStanceKnob("Wall clearance", &g_ThirdPersonCamClearance, 0.0f, 200.0f, "%.0f",
				"How far short of a wall the camera stops.");
		imguiOverlayStanceKnob("Give up under", &g_ThirdPersonCamMinDist, 0.0f, 500.0f, "%.0f",
				"Below this the camera sits on the eye instead.\n"
				"The body is still drawn, which is what the fade is for.");
		imguiOverlayStanceKnob("Shoulder (right +)", &g_ThirdPersonCamSide, -150.0f, 150.0f, "%.0f",
				"Units to her right of the eye; negative for the left shoulder.\n"
				"The crosshair stays honest, but the shot starts beside her,\n"
				"so a camera leaned past a corner shoots past it.");
		imguiOverlayStanceKnob("Forward (front -)", &g_ThirdPersonCamForward, -150.0f, 150.0f, "%.0f",
				"Units along her level facing, holding height whatever the pitch.\n"
				"Negative brings the camera round in front of her.");
		imguiOverlayStanceKnob("Height", &g_ThirdPersonCamHeight, -150.0f, 150.0f, "%.0f",
				"Units straight up in the world, not along the camera's up.");

		{
			static const char *tethers[] = { "Off", "Loose", "Normal", "Tight" };
			int tether = g_ThirdPersonCamTether;

			if (tether < 0 || tether > 3) {
				tether = 0;
			}

			if (ImGui::Combo("Camera tether", &tether, tethers, 4)) {
				g_ThirdPersonCamTether = tether;
			}

			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("The camera on a pole that pivots about her.\n"
						"Right stick orbits the camera; she turns to face where\n"
						"the left stick takes her, and faces the camera while firing.\n"
						"Loose swings to 60 degrees, Normal 45, Tight 30.\n"
						"Off is the rigid camera, body facing the aim.");
			}
		}

		imguiOverlayStanceKnob("Body turn (deg/tick)", &g_TetherBodyTurnSpeed, 5.0f, 90.0f, "%.0f",
				"With the tether on: how fast she turns to face her travel.\n"
				"30 is an about-turn in a tenth of a second; 90 is instant.");
	}

	if (ImGui::CollapsingHeader("Body fade", ImGuiTreeNodeFlags_DefaultOpen)) {
		imguiOverlayStanceKnob("Fade starts at", &g_BodyFadeStart, 0.0f, 1000.0f, "%.0f",
				"The camera distance the body starts going translucent at.\n"
				"Fully faded by the give-up distance above.");
		imguiOverlayStanceKnob("Fade depth", &g_BodyFadeFloor, 0.0f, 1.0f, "%.2f",
				"How much of her alpha the fade takes at its deepest.\n"
				"1.0 would remove her outright, which reads as a bug.");
	}

	if (ImGui::CollapsingHeader("Body split", ImGuiTreeNodeFlags_DefaultOpen)) {
		s32 i;

		ImGui::TextWrapped("Parts ticked here keep walking while a punch, reload, flinch "
				"or throw plays on everything above them. A roll and a death are whole "
				"body moves and ignore this.");
		ImGui::TextDisabled("The chr skeleton is 15 joints and they have no names in this "
				"tree yet, so the default is a guess: tick until the legs stop following "
				"the punch. If the ARMS freeze instead, the guess was inverted.");

		for (i = 0; i < 15; i++) {
			char label[32];
			bool on = (g_AnimSplitLowerMask & (1 << i)) != 0;

			snprintf(label, sizeof(label), "joint %d", i);

			if (ImGui::Checkbox(label, &on)) {
				if (on) {
					g_AnimSplitLowerMask |= 1 << i;
				} else {
					g_AnimSplitLowerMask &= ~(1 << i);
				}
			}

			if (i % 3 != 2 && i != 14) {
				ImGui::SameLine();
			}
		}

		ImGui::Text("mask 0x%04x", (unsigned)g_AnimSplitLowerMask);

		ImGui::Separator();
		ImGui::TextDisabled("Live");

		// A split that is not working looks the same from the outside whichever
		// way it broke - the legs stop. These four numbers say which: a mask
		// with no slot two behind it, a mask still set after the one shot ended,
		// or a blend that never reached the end of its crossfade.
		if (imguiOverlayCanAimInspect() && g_Vars.currentplayer->prop
				&& g_Vars.currentplayer->prop->chr
				&& g_Vars.currentplayer->prop->chr->model
				&& g_Vars.currentplayer->prop->chr->model->anim) {
			struct chrdata *bond = g_Vars.currentplayer->prop->chr;
			struct anim *anim = bond->model->anim;

			ImGui::Text("anim %d  slot2 %d  fracmerge %.2f",
					(s32)anim->animnum, (s32)anim->animnum2, anim->fracmerge);
			ImGui::Text("live mask 0x%04x  oneshot %d",
					(unsigned)anim->splitmask, (s32)bond->oneshotanim);

			if (anim->splitmask && !anim->animnum2) {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
						"split with no second slot - legs have nothing to play");
			} else if (anim->splitmask && !bond->oneshotanim) {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
						"split outlived its one shot - legs are stuck on slot two");
			}
		} else {
			ImGui::TextDisabled("(no live player)");
		}
	}

	if (ImGui::CollapsingHeader("Reload", ImGuiTreeNodeFlags_DefaultOpen)) {
		imguiOverlayStanceKnob("Reload speed", &g_ReloadSpeed, 0.01f, 1.0f, "%.2f",
				"What is left of her walk while she is reloading.\n"
				"The heaviest of the four multipliers on purpose - a reload\n"
				"already costs the stance and the trigger, and this is what\n"
				"makes choosing when to do one a decision. Stacks with the rest.");

		bool reloadAnim = g_ReloadAnimEnabled != 0;

		if (ImGui::Checkbox("Play the reload animation", &reloadAnim)) {
			g_ReloadAnimEnabled = reloadAnim ? 1 : 0;
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Off by default. The animation is stock and already in\n"
					"the ROM; what is new is a player body to play it on, and\n"
					"it is off until it has been looked at against each gun.\n"
					"Turning it off mid-reload is safe - the reload is the\n"
					"gun's, and the body was never being waited on.");
		}

		imguiOverlayStanceKnob("Reload anim speed", &g_ReloadAnimSpeed, 0.1f, 4.0f, "%.2f",
				"How fast the body plays its reload animation.\n"
				"The reload's own length is the weapon's, and the two do not\n"
				"agree - the body gives up as soon as the gun is loaded, so this\n"
				"is about making the reach look like it belongs to that gun.");
	}

	if (ImGui::CollapsingHeader("Roll", ImGuiTreeNodeFlags_DefaultOpen)) {
		imguiOverlayStanceKnob("Roll impulse", &g_RollImpulse, 0.0f, 200.0f, "%.1f",
				"The push a combat roll gets, for players and simulants alike.");
	}

	if (ImGui::CollapsingHeader("Build", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool enabled = g_BuildSpeedEnabled != 0;

		if (ImGui::Checkbox("Build affects movement", &enabled)) {
			g_BuildSpeedEnabled = enabled ? 1 : 0;
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Off is vanilla: everyone walks the same, and the\n"
					"simulant path keeps the multiply that flattens the\n"
					"whole body table to within a rounding error of 1.");
		}

		imguiOverlayStanceKnob("Reference height", &g_BuildSpeedRef, 60.0f, 300.0f, "%.0f",
				"The body height that walks at exactly 1. Everything else is a\n"
				"delta from it, so a stock roster is untouched while this sits\n"
				"at 159. Move it if the roster is normalised to something else.");

		imguiOverlayStanceKnob("Crouch discount", &g_BuildCrouchMix, 0.0f, 1.0f, "%.2f",
				"How much of the crouch price scales with build. 0 keeps the\n"
				"flat multipliers; 1 charges each body for the fraction of\n"
				"herself she folds away, which the crouch already computes.\n"
				"A short character is slower standing and faster crouched.");
	}

	ImGui::Separator();

	if (ImGui::Button("Reset to defaults")) {
		stanceTuningReset();
	}
}

// One game unit is about one centimetre -- docs/heights.md -- which is the only
// reason a feet-and-inches readout means anything. Nothing in the engine works
// in these units. This is here so a number can be recognised as a person: 159
// is a value, 5'2.6" is somebody.
// The sixteen body parts the engine already has names for. They come off the
// bbox nodes, which is where damage location is decided, so these are not
// labels invented for this panel -- they are what the game itself calls the
// pieces of a body when it works out where a bullet landed.
static const char *imguiPropHitPartName(s32 hitpart)
{
	switch (hitpart) {
	case HITPART_LFOOT:       return "foot L";
	case HITPART_LSHIN:       return "shin L";
	case HITPART_LTHIGH:      return "thigh L";
	case HITPART_RFOOT:       return "foot R";
	case HITPART_RSHIN:       return "shin R";
	case HITPART_RTHIGH:      return "thigh R";
	case HITPART_PELVIS:      return "pelvis";
	case HITPART_HEAD:        return "head";
	case HITPART_LHAND:       return "hand L";
	case HITPART_LFOREARM:    return "forearm L";
	case HITPART_LBICEP:      return "bicep L";
	case HITPART_RHAND:       return "hand R";
	case HITPART_RFOREARM:    return "forearm R";
	case HITPART_RBICEP:      return "bicep R";
	case HITPART_TORSO:       return "torso";
	case HITPART_TAIL:        return "tail";
	case HITPART_GUN:         return "gun";
	case HITPART_HAT:         return "hat";
	case HITPART_GENERAL:     return "general";
	case HITPART_GENERALHALF: return "general half";
	}

	return NULL;
}

struct imguiPropPartRow {
	s32 hitpart;
	s32 joint;
	f32 ymin;
	f32 ymax;
	f32 xmin;
	f32 xmax;
};

/**
 * Pair every named body part with the joint that carries it.
 *
 * A bbox node knows what part of a body it is -- that is how damage location
 * works -- and a chrinfo node knows which matrix a subtree follows. Neither
 * knows the other, but the tree does: a bbox's nearest chrinfo ANCESTOR names
 * the joint that part rides on. Walking that relation is the whole bridge from
 * a part with a name to a matrix the scale hook can reach, and it is read off
 * the model rather than guessed, so it is right for whatever body is latched
 * instead of right for the one that was measured.
 */
static s32 imguiPropCollectParts(struct chrdata *chr, struct imguiPropPartRow *out, s32 max)
{
	struct modelnode *node;
	s32 count = 0;

	if (chr == NULL || chr->model == NULL || chr->model->definition == NULL) {
		return 0;
	}

	node = chr->model->definition->rootnode;

	while (node && count < max) {
		if ((node->type & 0xff) == MODELNODETYPE_BBOX && node->rodata) {
			// modelFindNodeMtxIndex rather than a walk of our own. The first
			// version here looked only for CHRINFO, and on a chr model CHRINFO
			// is the ROOT node -- so every part in the body resolved to the same
			// joint and editing any of them moved one limb. The engine's own
			// resolver also accepts POSITION and POSITIONHELD, and POSITION is
			// what actually carries the per-part matrix. Calling it means this
			// cannot drift from what the renderer believes.
			s32 joint = modelFindNodeMtxIndex(node, 0);

			{
				s32 hp = node->rodata->bbox.hitpart;

				if (hp >= 0 && hp < kFojoMaxHitPart && g_ImGuiPropPartJoint[hp] >= 0) {
					joint = g_ImGuiPropPartJoint[hp];
				}
			}

			if (joint >= 0 && joint < kFojoMaxJointOverrides) {
				// The box itself, kept so a row can be checked against what it
				// claims to be. A pelvis and an arm do not occupy the same
				// space, so if two rows resolve to one joint the extents say
				// whether that is the model or a mistake in here.
				out[count].hitpart = node->rodata->bbox.hitpart;
				out[count].joint = joint;
				out[count].ymin = node->rodata->bbox.ymin;
				out[count].ymax = node->rodata->bbox.ymax;
				out[count].xmin = node->rodata->bbox.xmin;
				out[count].xmax = node->rodata->bbox.xmax;
				count++;
			}
		}

		if (node->child) {
			node = node->child;
		} else {
			while (node) {
				if (node->next) {
					node = node->next;
					break;
				}

				node = node->parent;
			}
		}
	}

	return count;
}

static void imguiOverlayFormatUsHeight(f32 units, char *buf, size_t len)
{
	f32 inches = units / 2.54f;
	s32 feet = (s32)(inches / 12.0f);
	f32 rem = inches - (f32)feet * 12.0f;

	// Otherwise 11.96 inches prints as 5'12.0".
	if (rem >= 11.95f) {
		feet += 1;
		rem = 0.0f;
	}

	snprintf(buf, len, "%d'%.1f\"", feet, rem);
}

static void imguiOverlayDrawProportionsPanel(void)
{
	// Switching stages, or anything else that takes the chr out of memory, is
	// the only thing that ends a latch -- closing this window does not. The
	// engine drops its own pointer in the chr free path and in lvReset; this
	// catches the case where the panel was closed while that happened.
	if (g_ImGuiPropChr && !imguiPropLatchIsLive()) {
		imguiPropRelease();
	}

	struct prop *aimed = imguiOverlayCanAimInspect()
		? propFindAimingAt(HAND_RIGHT, false, FINDPROPCONTEXT_QUERY)
		: NULL;
	struct chrdata *aimedchr = NULL;

	if (aimed && imguiOverlayPropIsCurrent(aimed)
			&& (aimed->type == PROPTYPE_CHR || aimed->type == PROPTYPE_PLAYER)
			&& imguiOverlayChrIsCurrent(aimed->chr)) {
		aimedchr = aimed->chr;
	}

	struct chrdata *self = (g_Vars.currentplayer && g_Vars.currentplayer->prop)
		? g_Vars.currentplayer->prop->chr
		: NULL;

	// TODO(catherine): strings throughout this panel are placeholders.
	if (ImGui::Button("Latch")) {
		imguiPropLatch(aimedchr ? aimedchr : self);
	}

	ImGui::SameLine();

	if (aimedchr) {
		ImGui::Text("aiming at chr 0x%04x", (u32)(u16)aimedchr->chrnum);
	} else if (self) {
		ImGui::TextDisabled("latches you");
	} else {
		ImGui::TextDisabled("no live player");
	}

	// The aim path reads the player's gun direction, which is frozen while
	// the overlay has the mouse -- so it only finds whoever you were already
	// pointing at. Right-clicking a row in Entities is the reliable way in.
	// A picker, because both other ways in have failed in play. The aim path
	// reads a gun direction that is frozen while the overlay holds the mouse,
	// and the Entities context menu depends on finding the right row in another
	// window. This one needs neither: every chr in the level, in one list, with
	// the button next to it.
	if (ImGui::CollapsingHeader("Pick a character", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (g_ChrSlots == NULL || g_NumChrSlots <= 0) {
			ImGui::TextDisabled("no chr slots");
		} else if (ImGui::BeginTable("fojoproplatchlist", 4,
				ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
				ImVec2(0.0f, 170.0f))) {
			ImGui::TableSetupColumn("chr");
			ImGui::TableSetupColumn("body");
			ImGui::TableSetupColumn("who");
			ImGui::TableSetupColumn("");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (s32 i = 0; i < g_NumChrSlots; i++) {
				struct chrdata *c = &g_ChrSlots[i];

				if (!imguiOverlayChrIsCurrent(c) || c->prop == NULL || c->model == NULL) {
					continue;
				}

				bool isself = imguiPropChrIsPlayer(c);
				bool islatched = g_ImGuiPropChr == c;

				ImGui::TableNextRow();
				ImGui::PushID(20000 + i);

				ImGui::TableNextColumn();

				if (islatched) {
					ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "%04x", (u32)(u16)c->chrnum);
				} else {
					ImGui::Text("%04x", (u32)(u16)c->chrnum);
				}

				ImGui::TableNextColumn();
				ImGui::Text("%d", (s32)c->bodynum);

				ImGui::TableNextColumn();

				if (isself) {
					ImGui::TextDisabled("you");
				} else if (c->aibot) {
					ImGui::TextDisabled("simulant");
				} else {
					ImGui::TextDisabled("ai");
				}

				ImGui::TableNextColumn();

				if (ImGui::SmallButton("latch")) {
					imguiPropLatch(c);
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}

	ImGui::TextDisabled("or right-click a character in Entities");

	if (!g_ImGuiPropChr) {
		ImGui::Separator();
		ImGui::TextDisabled("Nothing latched.");
		return;
	}

	struct chrdata *chr = g_ImGuiPropChr;
	s32 bodynum = (s32)chr->bodynum;
	bool bodyok = bodynum >= 0 && bodynum < g_NumHeadsAndBodies;
	bool isplayer = imguiPropChrIsPlayer(chr);

	ImGui::Text("latched chr 0x%04x%s", (u32)(u16)chr->chrnum, isplayer ? " (you)" : "");
	ImGui::SameLine();

	if (ImGui::Button("Release")) {
		imguiPropRelease();
		return;
	}

	ImGui::SameLine();
	ImGui::Checkbox("apply", &g_ImGuiPropApply);

	ImGui::Separator();

	s32 sharing = imguiPropCountSharingBody(bodynum);

	ImGui::Text("body 0x%02x", (u32)bodynum);
	ImGui::SameLine();

	if (sharing > 1) {
		ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "%d live chrs share this row", sharing);
	} else {
		ImGui::TextDisabled("only chr on this row");
	}

	s32 jointcount = imguiPropJointCount(chr);

	ImGui::Text("skeleton: %d joints", jointcount);

	// The head row's own height stacks on the body's to make the crown, which is
	// the number a person recognises as a height -- the body row alone is eye
	// level. Vanilla uses 13 for every human head and 27 for every Maian one.
	s32 propheadnum = (s32)chr->headnum;
	s32 headadd = (propheadnum >= 0 && propheadnum < g_NumHeadsAndBodies)
		? (s32)g_HeadsAndBodies[propheadnum].height
		: 13;

	// --- height ----------------------------------------------------------
	ImGui::SeparatorText("Height");

	ImGui::Checkbox("link model scale to height", &g_ImGuiPropLinkScale);

	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Height moves the camera and the collision volume; model scale\n"
				"moves what is drawn. They are independent fields, and setting\n"
				"one alone is how a character ends up with her eyes in one\n"
				"place and her body drawn at another size.\n\n"
				"Linked, either one drives the other off the shipped row, so\n"
				"the body you see and the body the game collides with stay the\n"
				"same person.");
	}

	if (!bodyok) {
		ImGui::TextDisabled("body row out of range");
	} else {
		f32 h = g_ImGuiPropHeight;
		s32 basecrown = g_ImGuiPropBaseHeight + headadd;
		s32 cap = (s32)g_HeadsAndBodies[BODY_MRBLONDE].height + (s32)g_HeadsAndBodies[HEAD_MRBLONDE].height;
		s32 crown;
		bool changed = false;

		if (!isplayer) {
			// An AI's camera does not exist and its collision volume comes from
			// a flat chr->height, so the body row's height is inert for one.
			// The controls still earn their place: model scale is the only
			// lever an AI has on how big it looks, and these drive it.
			ImGui::TextDisabled("AI ignore the body row, so this drives model scale only.");
			ImGui::TextDisabled("Latch yourself to move a camera and a collision volume.");
		}

		// Three ways in to one field. The slider is what the engine stores, and
		// the other two are what a person thinks in -- centimetres because a
		// unit is one, feet and inches because that is how the heights arrive.
		// All of them write g_ImGuiPropHeight, and all of them are read back
		// from it every frame, so they cannot drift apart.
		if (ImGui::DragFloat("eye (units)", &h, 0.25f, 40.0f, 255.0f, "%.0f")) {
			g_ImGuiPropHeight = h;
			changed = true;
		}

		{
			// The body row is EYE LEVEL. The head row's own height comes off
			// the top before a stature is stored, and goes back on before one
			// is shown; forgetting it makes everybody a head too tall.
			f32 cm = g_ImGuiPropHeight + (f32)headadd;
			f32 totalinches = cm / 2.54f;
			s32 feet = (s32)(totalinches / 12.0f);
			s32 inches = (s32)(totalinches - (f32)feet * 12.0f + 0.5f);
			bool fromfi = false;

			if (inches >= 12) {
				feet += 1;
				inches = 0;
			}

			if (ImGui::DragFloat("stands (cm)", &cm, 0.25f, 60.0f, 280.0f, "%.0f")) {
				g_ImGuiPropHeight = cm - (f32)headadd;
				changed = true;
			}

			ImGui::PushItemWidth(ImGui::GetFontSize() * 3.5f);

			if (ImGui::DragInt("##propfeet", &feet, 0.03f, 2, 9, "%d'")) {
				fromfi = true;
			}

			ImGui::SameLine(0.0f, 4.0f);

			if (ImGui::DragInt("stands", &inches, 0.08f, 0, 11, "%d\"")) {
				fromfi = true;
			}

			ImGui::PopItemWidth();

			if (fromfi) {
				g_ImGuiPropHeight = (f32)(feet * 12 + inches) * 2.54f - (f32)headadd;
				changed = true;
			}
		}

		if (changed) {
			if (g_ImGuiPropHeight < 40.0f) {
				g_ImGuiPropHeight = 40.0f;
			}

			if (g_ImGuiPropHeight > 255.0f) {
				g_ImGuiPropHeight = 255.0f;
			}

			// Off the SHIPPED row rather than off the last value, so dragging
			// back and forth lands exactly where it started instead of walking
			// away on accumulated rounding.
			//
			// Forced for an AI: unlinked, these controls would move a field
			// nothing reads and appear to do nothing at all.
			if ((g_ImGuiPropLinkScale || !isplayer) && basecrown > 0) {
				g_ImGuiPropScale = g_ImGuiPropBaseScale
					* ((g_ImGuiPropHeight + (f32)headadd) / (f32)basecrown);
			}
		}

		crown = (s32)(g_ImGuiPropHeight + 0.5f) + headadd;

		ImGui::Text("shipped %d  ->  %d   %+d",
				g_ImGuiPropBaseHeight,
				(s32)(g_ImGuiPropHeight + 0.5f),
				(s32)(g_ImGuiPropHeight + 0.5f) - g_ImGuiPropBaseHeight);

		// The cap is a clamp on the PLAYER's vv_headheight. An AI never goes
		// through playerSetHeight, so saying it clamps would be a lie.
		if (isplayer && crown > cap) {
			ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
					"crown %d clamps to %d (collision top)", crown, cap);
		} else if (isplayer) {
			ImGui::TextDisabled("crown %d, cap %d", crown, cap);
		} else {
			ImGui::TextDisabled("crown %d. The cap of %d is a player clamp.", crown, cap);
		}

		{
			char eyeus[24];
			char crownus[24];
			char baseus[24];

			imguiOverlayFormatUsHeight(g_ImGuiPropHeight, eyeus, sizeof(eyeus));
			imguiOverlayFormatUsHeight((f32)crown, crownus, sizeof(crownus));
			imguiOverlayFormatUsHeight((f32)(g_ImGuiPropBaseHeight + headadd), baseus, sizeof(baseus));

			ImGui::Text("stands %d cm / %s   (eye %d cm / %s)",
					crown, crownus, (s32)(g_ImGuiPropHeight + 0.5f), eyeus);
			ImGui::TextDisabled("shipped %d cm / %s. 1 unit is about 1 cm.",
					g_ImGuiPropBaseHeight + headadd, baseus);
		}

		if (isplayer) {
			ImGui::TextDisabled("also changes movement speed, weapon sway and crouch depth.");
		}
	}

	// --- lore scale ------------------------------------------------------
	//
	// Two spaces, one ratio. Concept art is drawn to whatever scale the drawing
	// wanted; the engine has a hard ceiling at the tallest body plus the tallest
	// head. Nothing reconciles them automatically, and doing it by hand once per
	// character is how a roster ends up inconsistent. Set the ratio once from a
	// character whose height in both spaces is known, and every other reading
	// here follows.
	ImGui::SeparatorText("Lore scale");

	if (bodyok) {
		s32 refcap2 = (s32)g_HeadsAndBodies[BODY_MRBLONDE].height + (s32)g_HeadsAndBodies[HEAD_MRBLONDE].height;
		f32 crowncm = g_ImGuiPropHeight + (f32)headadd;
		f32 lorecm = g_ImGuiPropLoreScale > 0.0001f ? crowncm / g_ImGuiPropLoreScale : crowncm;
		f32 capcm = g_ImGuiPropLoreScale > 0.0001f ? (f32)refcap2 / g_ImGuiPropLoreScale : (f32)refcap2;
		char loreus[24];
		char capus[24];
		f32 ratio = g_ImGuiPropLoreScale;

		imguiOverlayFormatUsHeight(lorecm, loreus, sizeof(loreus));
		imguiOverlayFormatUsHeight(capcm, capus, sizeof(capus));

		ImGui::Text("lore %s / %d cm", loreus, (s32)(lorecm + 0.5f));

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("What this character stands in chart space, at the ratio below.");
		}

		ImGui::TextDisabled("the cap of %d is lore %s -- nothing above that fits.",
				refcap2, capus);

		if (ImGui::DragFloat("chart shrink", &ratio, 0.0005f, 0.2f, 3.0f, "%.4f")) {
			if (ratio > 0.0001f) {
				g_ImGuiPropLoreScale = ratio;
			}
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("How much the whole chart shrinks to fit the engine: engine\n"
					"centimetres per chart centimetre. Below 1 means the chart is\n"
					"drawn bigger than the game can hold, which it usually is.\n\n"
					"Set it ONCE, from whoever is tallest in the chart: pick the\n"
					"value that puts them on the cap above. Everyone else then\n"
					"reads off the same ruler.");
		}

		// A lore height behaves like the stature fields above: type what the
		// character is in the chart and she becomes that, through the ratio.
		// This is the daily operation. The ratio itself is set once.
		{
			f32 lorecmedit = lorecm;
			f32 loretotalinches = lorecmedit / 2.54f;
			s32 lorefeet = (s32)(loretotalinches / 12.0f);
			s32 loreinches = (s32)(loretotalinches - (f32)lorefeet * 12.0f + 0.5f);
			bool lorechanged = false;

			if (loreinches >= 12) {
				lorefeet += 1;
				loreinches = 0;
			}

			if (ImGui::DragFloat("lore (cm)", &lorecmedit, 0.25f, 60.0f, 280.0f, "%.0f")) {
				lorechanged = true;
			}

			ImGui::PushItemWidth(ImGui::GetFontSize() * 3.5f);

			if (ImGui::DragInt("##lorefeet", &lorefeet, 0.03f, 2, 9, "%d'")) {
				lorecmedit = (f32)(lorefeet * 12 + loreinches) * 2.54f;
				lorechanged = true;
			}

			ImGui::SameLine(0.0f, 4.0f);

			if (ImGui::DragInt("lore", &loreinches, 0.08f, 0, 11, "%d\"")) {
				lorecmedit = (f32)(lorefeet * 12 + loreinches) * 2.54f;
				lorechanged = true;
			}

			ImGui::PopItemWidth();

			if (lorechanged) {
				f32 wanted = lorecmedit * g_ImGuiPropLoreScale - (f32)headadd;

				if (wanted < 40.0f) {
					wanted = 40.0f;
				}

				if (wanted > 255.0f) {
					wanted = 255.0f;
				}

				g_ImGuiPropHeight = wanted;

				if (g_ImGuiPropLinkScale || !isplayer) {
					s32 bc = g_ImGuiPropBaseHeight + headadd;

					if (bc > 0) {
						g_ImGuiPropScale = g_ImGuiPropBaseScale
							* ((g_ImGuiPropHeight + (f32)headadd) / (f32)bc);
					}
				}
			}
		}

		if (ImGui::Button("chart is engine scale (1.0)")) {
			g_ImGuiPropLoreScale = 1.0f;
		}
	} else {
		ImGui::TextDisabled("body row out of range");
	}

	// --- scale reference -------------------------------------------------
	//
	// Indexed by LORE height, because that is the number she arrives with. The
	// first version of this table was indexed by in-game stature, which meant
	// reading it backwards: find the answer, then check it was the question.
	//
	// Built from the LATCHED character's own head row rather than from a
	// constant, so the body row column is the number to type for THIS
	// character -- a Maian head adds 27 where a human one adds 13, and a fixed
	// table would be wrong by half a foot on half the roster.
	if (ImGui::CollapsingHeader("Scale reference")) {
		s32 refcap = (s32)g_HeadsAndBodies[BODY_MRBLONDE].height + (s32)g_HeadsAndBodies[HEAD_MRBLONDE].height;
		s32 nowcrown = bodyok ? (s32)(g_ImGuiPropHeight + 0.5f) + headadd : -1;
		f32 shrink = g_ImGuiPropLoreScale > 0.0001f ? g_ImGuiPropLoreScale : 1.0f;
		s32 loreinches;

		ImGui::TextDisabled("Left is what she is in the chart. Right is what to type.");

		if (ImGui::BeginTable("pd stature reference", 3, ImGuiTableFlags_SizingFixedFit
				| ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
			ImGui::TableSetupColumn("in the chart");
			ImGui::TableSetupColumn("in the game");
			ImGui::TableSetupColumn("body row");
			ImGui::TableHeadersRow();

			for (loreinches = 58; loreinches <= 80; loreinches += 2) {
				f32 lorecmrow = (f32)loreinches * 2.54f;
				s32 crownunits = (s32)(lorecmrow * shrink + 0.5f);
				s32 bodyunits = crownunits - headadd;
				bool over = crownunits > refcap;
				bool here = nowcrown >= 0 && nowcrown >= crownunits - 1 && nowcrown <= crownunits + 1;
				char ingame[24];

				imguiOverlayFormatUsHeight((f32)crownunits, ingame, sizeof(ingame));

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				if (here) {
					ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "%d\'%d\"",
							loreinches / 12, loreinches % 12);
				} else if (over) {
					ImGui::TextDisabled("%d\'%d\"", loreinches / 12, loreinches % 12);
				} else {
					ImGui::Text("%d\'%d\"", loreinches / 12, loreinches % 12);
				}

				ImGui::TableSetColumnIndex(1);

				if (over) {
					ImGui::TextDisabled("%s", ingame);
				} else {
					ImGui::Text("%s", ingame);
				}

				ImGui::TableSetColumnIndex(2);

				if (over) {
					ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%d too tall", bodyunits);
				} else {
					ImGui::Text("%d", bodyunits);
				}
			}

			ImGui::EndTable();
		}

		ImGui::TextDisabled("Green is where she is now. Red will not fit: the game stops");
		ImGui::TextDisabled("growing anyone past %d units, or %d\'%d\", and draws the rest",
				refcap, (s32)(refcap / 2.54f) / 12, (s32)(refcap / 2.54f) % 12);
		ImGui::TextDisabled("of the body somewhere the camera is not.");
	}

	// --- model scale -----------------------------------------------------
	ImGui::SeparatorText("Model scale");

	if (!bodyok) {
		ImGui::TextDisabled("body row out of range");
	} else {
		f32 s = g_ImGuiPropScale;

		if (ImGui::DragFloat("scale", &s, 0.0005f, 0.05f, 4.0f, "%.5f")) {
			g_ImGuiPropScale = s;

			// The same relation as the height section uses, run backwards, so
			// the link reads the same whichever end of it is dragged. Only the
			// player has a height field worth moving -- an AI ignores the body
			// row and carries a flat chr->height instead.
			if (g_ImGuiPropLinkScale && isplayer && g_ImGuiPropBaseScale != 0.0f) {
				f32 basecrown = (f32)(g_ImGuiPropBaseHeight + headadd);
				f32 wanted = basecrown * (g_ImGuiPropScale / g_ImGuiPropBaseScale)
					- (f32)headadd;

				if (wanted < 40.0f) {
					wanted = 40.0f;
				}

				if (wanted > 255.0f) {
					wanted = 255.0f;
				}

				g_ImGuiPropHeight = wanted;
			}
		}

		f32 ratio = g_ImGuiPropBaseScale != 0.0f ? g_ImGuiPropScale / g_ImGuiPropBaseScale : 1.0f;
		f32 drawn = (f32)(g_ImGuiPropBaseHeight + headadd) * ratio;
		char drawnus[24];

		ImGui::Text("shipped %.5f  ->  %.5f   x%.4f",
				g_ImGuiPropBaseScale, g_ImGuiPropScale, ratio);

		imguiOverlayFormatUsHeight(drawn, drawnus, sizeof(drawnus));

		// The height field is what the camera and the collision volume believe;
		// this is what the eye is shown. They are independent fields and moving
		// one without the other is the classic way to end up with a character
		// whose eyes are in one place and whose body is drawn at another size.
		ImGui::Text("drawn body reads as %s", drawnus);

		if (isplayer) {
			f32 crownf = (f32)((s32)(g_ImGuiPropHeight + 0.5f) + headadd);

			if (fabsf(drawn - crownf) > 3.0f) {
				ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
						"body and camera disagree by %.0f cm", fabsf(drawn - crownf));
			}
		}
	}

	// --- parts -----------------------------------------------------------
	//
	// The joints table addresses a body by matrix index, which is honest and
	// unreadable. This one addresses it by the names the engine already uses
	// for the pieces of a body when it decides where a shot landed, and both
	// write the same overrides -- a part IS a joint, seen from the other end.
	ImGui::SeparatorText("Parts");

	{
		struct imguiPropPartRow parts[48];
		s32 partcount = imguiPropCollectParts(chr, parts, 48);

		if (partcount <= 0) {
			ImGui::TextDisabled("no named parts on this model");
		} else if (ImGui::BeginTable("fojoproportionparts", 8,
				ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
				ImVec2(0.0f, 240.0f))) {
			ImGui::TableSetupColumn("part");
			ImGui::TableSetupColumn("joint");
			ImGui::TableSetupColumn("box y");
			ImGui::TableSetupColumn("box x");
			ImGui::TableSetupColumn("mark");
			ImGui::TableSetupColumn("x");
			ImGui::TableSetupColumn("y");
			ImGui::TableSetupColumn("z");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (s32 p = 0; p < partcount; p++) {
				s32 joint = parts[p].joint;
				const char *name = imguiPropHitPartName(parts[p].hitpart);
				s32 mirror = imguiPropJointMirror(chr, joint);

				ImGui::TableNextRow();
				ImGui::PushID(1000 + p);

				ImGui::TableNextColumn();

				if (name) {
					ImGui::Text("%s", name);
				} else {
					// An unnamed hitpart is still worth listing -- it is a real
					// piece of the body, just one vanilla never had to name.
					ImGui::TextDisabled("part %d", parts[p].hitpart);
				}

				// A joint claimed by more than one part is the thing to look at
				// first when a row moves the wrong limb.
				bool shared = false;

				for (s32 q = 0; q < partcount; q++) {
					if (q != p && parts[q].joint == joint) {
						shared = true;
						break;
					}
				}

				// Editable, because the model's own tree has proved an
				// unreliable narrator about which joint a part rides on. Mark a
				// joint, watch where the dot lands, type the number that is
				// actually right. Calibration beats inference when the
				// inference keeps being wrong.
				ImGui::TableNextColumn();
				ImGui::SetNextItemWidth(52.0f);

				{
					s32 jedit = joint;
					s32 hp = parts[p].hitpart;

					if (shared) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
					}

					if (ImGui::InputInt("##pj", &jedit, 0, 0)) {
						if (jedit >= 0 && jedit < kFojoMaxJointOverrides
								&& hp >= 0 && hp < kFojoMaxHitPart) {
							g_ImGuiPropPartJoint[hp] = (s16)jedit;
						}
					}

					if (shared) {
						ImGui::PopStyleColor();
					}
				}

				ImGui::TableNextColumn();
				ImGui::TextDisabled("%.0f..%.0f", parts[p].ymin, parts[p].ymax);

				ImGui::TableNextColumn();
				ImGui::TextDisabled("%.0f..%.0f", parts[p].xmin, parts[p].xmax);

				ImGui::TableNextColumn();

				{
					bool partmarked = g_ImGuiPropMarkJoint == joint;

					if (ImGui::RadioButton("##pmark", partmarked)) {
						g_ImGuiPropMarkJoint = partmarked ? -1 : joint;
					}
				}

				for (s32 axis = 0; axis < 3; axis++) {
					ImGui::TableNextColumn();
					ImGui::PushID(axis);
					ImGui::SetNextItemWidth(72.0f);

					f32 v = g_JointScaleOverride[joint][axis];
					bool edited;

					if (g_ImGuiPropTypeValues) {
						edited = ImGui::InputFloat("##pv", &v, 0.0f, 0.0f, "%.3f");
					} else {
						edited = ImGui::DragFloat("##pv", &v, 0.002f, 0.05f, 4.0f, "%.3f");
					}

					if (edited) {
						if (v < 0.05f) {
							v = 0.05f;
						}

						if (v > 4.0f) {
							v = 4.0f;
						}

						g_JointScaleOverride[joint][axis] = v;

						if (g_ImGuiPropMirror && mirror != joint
								&& mirror >= 0 && mirror < kFojoMaxJointOverrides) {
							g_JointScaleOverride[mirror][axis] = v;
						}
					}

					ImGui::PopID();
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		if (partcount > 0) {
			if (ImGui::Button("copy mapping")) {
				char buf[1600];
				s32 off = 0;

				off += snprintf(buf + off, sizeof(buf) - off, "# part = joint (box y, box x)\n");

				for (s32 p = 0; p < partcount && off < (s32)sizeof(buf) - 96; p++) {
					const char *nm = imguiPropHitPartName(parts[p].hitpart);

					off += snprintf(buf + off, sizeof(buf) - off,
							"%-12s = %2d   (%.0f..%.0f, %.0f..%.0f)\n",
							nm ? nm : "?", parts[p].joint,
							parts[p].ymin, parts[p].ymax, parts[p].xmin, parts[p].xmax);
				}

				ImGui::SetClipboardText(buf);
			}

			ImGui::SameLine();

			if (ImGui::Button("forget corrections")) {
				for (s32 i = 0; i < kFojoMaxHitPart; i++) {
					g_ImGuiPropPartJoint[i] = -1;
				}
			}
		}

		ImGui::TextDisabled("The joint column is EDITABLE. Where the model's tree gets a part");
		ImGui::TextDisabled("wrong, mark a joint, watch the dot, and type the right number in.");
		ImGui::TextDisabled("Amber means two parts claim one joint. Corrections last until relatch.");
	}

	// --- joints ----------------------------------------------------------
	ImGui::SeparatorText("Joints");
	ImGui::Checkbox("mirror", &g_ImGuiPropMirror);
	ImGui::SameLine();
	ImGui::Checkbox("draw marker", &g_ImGuiPropDrawBox);
	ImGui::SameLine();
	ImGui::Checkbox("type values", &g_ImGuiPropTypeValues);

	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Swaps the drags for fields you type into, here and in Parts.\n"
				"A drag is for finding a number; a field is for repeating one\n"
				"you already found, which is what baking a row needs.");
	}

	if (jointcount <= 0) {
		ImGui::TextDisabled("no skeleton");
	} else if (ImGui::BeginTable("fojoproportionjoints", 6,
			ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
			ImVec2(0.0f, 260.0f))) {
		ImGui::TableSetupColumn("#");
		ImGui::TableSetupColumn("mir");
		ImGui::TableSetupColumn("x");
		ImGui::TableSetupColumn("y");
		ImGui::TableSetupColumn("z");
		ImGui::TableSetupColumn("mark");
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();

		for (s32 j = 0; j < jointcount; j++) {
			s32 mirror = imguiPropJointMirror(chr, j);

			ImGui::TableNextRow();
			ImGui::PushID(j);

			ImGui::TableNextColumn();
			ImGui::Text("%d", j);

			ImGui::TableNextColumn();

			if (mirror == j) {
				ImGui::TextDisabled("--");
			} else {
				ImGui::Text("%d", mirror);
			}

			for (s32 axis = 0; axis < 3; axis++) {
				ImGui::TableNextColumn();
				ImGui::PushID(axis);
				ImGui::SetNextItemWidth(72.0f);

				f32 v = g_JointScaleOverride[j][axis];
				bool edited;

				if (g_ImGuiPropTypeValues) {
					edited = ImGui::InputFloat("##v", &v, 0.0f, 0.0f, "%.3f");
				} else {
					edited = ImGui::DragFloat("##v", &v, 0.002f, 0.05f, 4.0f, "%.3f");
				}

				if (edited) {
					if (v < 0.05f) {
						v = 0.05f;
					}

					if (v > 4.0f) {
						v = 4.0f;
					}

					g_JointScaleOverride[j][axis] = v;

					if (g_ImGuiPropMirror && mirror != j
							&& mirror >= 0 && mirror < kFojoMaxJointOverrides) {
						g_JointScaleOverride[mirror][axis] = v;
					}
				}

				ImGui::PopID();
			}

			ImGui::TableNextColumn();

			bool marked = g_ImGuiPropMarkJoint == j;

			if (ImGui::RadioButton("##mark", marked)) {
				g_ImGuiPropMarkJoint = marked ? -1 : j;
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	if (ImGui::Button("Reset joints")) {
		for (s32 i = 0; i < kFojoMaxJointOverrides; i++) {
			g_JointScaleOverride[i][0] = 1.0f;
			g_JointScaleOverride[i][1] = 1.0f;
			g_JointScaleOverride[i][2] = 1.0f;
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("Reset all")) {
		imguiPropLatch(chr);
	}

	imguiPropApplyLive(chr);
}

static void imguiOverlaySetNextWindowDefaults(const ImVec2 &size, float xAnchor, float yAnchor)
{
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const float availableX = viewport->WorkSize.x > size.x ? viewport->WorkSize.x - size.x : 0.0f;
	const float availableY = viewport->WorkSize.y > size.y ? viewport->WorkSize.y - size.y : 0.0f;
	ImGui::SetNextWindowPos(ImVec2(
		viewport->WorkPos.x + availableX * xAnchor,
		viewport->WorkPos.y + availableY * yAnchor), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
}

static const char *imguiOverlayTrackTypeName(s32 tracktype)
{
	switch (tracktype) {
	case 0:  return "none";
	case 1:  return "primary";
	case 2:  return "nrg";
	case 3:  return "menu";
	case 4:  return "death";
	case 5:  return "ambient";
	default: return "?";
	}
}

static void imguiOverlayDrawAudioPanel(void)
{
	static int fxbus = 0;
	static int fxsection = 0;
	static int fxparam = 2;
	static int fxvalue = 0;
	static int chanrate = 255;
	static int sfxvol = -1;
	static int voicecap[4] = { 0, 0, 0, 0 };
	static bool fxsent = false;
	static bool fxok = false;

	static const char *fxparamnames[8] = {
		"input (ms)", "output (ms)", "fbcoef", "ffcoef",
		"gain", "chorusrate", "chorusdepth (dead)", "lpfilt",
	};

	if (ImGui::CollapsingHeader("Master", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool muted = g_SndDisabled;

		if (ImGui::Checkbox("Mute everything", &muted)) {
			g_SndDisabled = muted;
		}

		ImGui::SameLine();
		ImGui::TextDisabled("g_SndDisabled");

		int musicvol = (int)musicGetVolume();

		if (ImGui::SliderInt("Music", &musicvol, 0, 0x5000)) {
			musicSetVolume((u16)musicvol);
		}

		if (sfxvol < 0) {
			sfxvol = (int)snddebugGetSfxVolume();
		}

		if (ImGui::SliderInt("SFX", &sfxvol, 0, 0x5000)) {
			sndSetSfxVolume((u16)sfxvol);
		}

		int divisor = (int)g_MusicMenuVolumeDivisor;

		if (ImGui::SliderInt("Menu music divisor", &divisor, 1, 32)) {
			g_MusicMenuVolumeDivisor = (s32)divisor;
		}

		ImGui::SameLine();
		ImGui::TextDisabled("-> %d", (int)musicGetMenuVolume());

		s32 numfree = 0;
		s32 numalloced = 0;
		s32 total = snddebugCountSfxVoices(&numfree, &numalloced);
		ImGui::Text("sfx voices: %d alloced, %d free, %d total",
				(int)numalloced, (int)numfree, (int)total);
	}

	if (ImGui::CollapsingHeader("Pools")) {
		struct snddebugpools pools;
		snddebugGetPools(&pools);

		ImGui::TextWrapped("What the pools came up as at boot. The sizes are "
				"config knobs (Audio.*) but sndInit reads them once, so editing "
				"one and not restarting shows up as these not matching what you "
				"set.");

		const float frac = pools.heaptotal > 0
			? (float)pools.heapused / (float)pools.heaptotal
			: 0.0f;
		char overlay[64];
		snprintf(overlay, sizeof(overlay), "%d / %d KB",
				(int)(pools.heapused / 1024), (int)(pools.heaptotal / 1024));
		ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay);
		ImGui::SameLine();
		ImGui::Text("sound heap");

		if (frac >= 1.0f) {
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
					"heap is full -- alHeapAlloc does not fail, it hands back "
					"memory it does not own. Raise Audio.HeapSize.");
		}

		ImGui::Text("voices: %d physical, %d virtual",
				(int)pools.pvoices, (int)pools.vvoices);
		ImGui::TextDisabled("virtual over physical means the synth demotes by "
				"priority rather than hard-cutting");
		ImGui::Text("seq buffer: %d bytes each, x%d slots",
				(int)pools.seqbuffer, (int)snddebugNumSlots());
		ImGui::Text("acmd list: %d commands", (int)pools.acmdlen);
	}

	// Open by default: this is the thing that is meant to be played with, and a
	// collapsed header between two expanded ones is easy to scroll straight past.
	if (ImGui::CollapsingHeader("Cue", ImGuiTreeNodeFlags_DefaultOpen)) {
		struct sndcuestate cue;
		sndcueGetState(&cue);

		ImGui::TextWrapped("A sound comes in, a channel-mask change goes out -- "
				"but not until the music reaches a position where the change "
				"will not sound like a mistake. Everything here is a guess "
				"until you have heard it in a firefight.");

		bool enabled = g_SndCueEnabled != 0;
		if (ImGui::Checkbox("enabled", &enabled)) {
			g_SndCueEnabled = enabled ? 1 : 0;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("checked first in sndStart");

		const char *quant[] = { "now", "next beat", "next bar" };
		int q = (int)g_SndCueQuantise;
		if (ImGui::Combo("quantise", &q, quant, 3)) {
			g_SndCueQuantise = q;
		}

		if (g_SndCueQuantise == 0) {
			ImGui::TextDisabled("the control condition -- without hearing this "
					"you cannot tell the waiting is doing anything");
		} else if (g_SndCueQuantise == 2) {
			int bpb = (int)g_SndCueBeatsPerBar;
			if (ImGui::SliderInt("beats/bar", &bpb, 1, 16)) {
				g_SndCueBeatsPerBar = bpb;
			}
			ImGui::TextDisabled("a guess. if it is wrong the jumps land off the "
					"beat, which sounds like a broken mechanism rather than a "
					"wrong number -- next beat needs no meter at all");
		}

		int slot = (int)g_SndCueSlot;
		if (ImGui::SliderInt("slot", &slot, 0, 2)) {
			g_SndCueSlot = slot;
		}

		int trig = (int)g_SndCueTriggerSfx;
		if (ImGui::InputInt("trigger sfx", &trig)) {
			g_SndCueTriggerSfx = trig < -1 ? -1 : trig;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("-1 = any");
		ImGui::Text("last sound seen: %d", (int)cue.lastsfx);

		int full = (int)g_SndCueMaskFull;
		int duck = (int)g_SndCueMaskDucked;
		if (ImGui::InputInt("mask full", &full, 1, 16,
				ImGuiInputTextFlags_CharsHexadecimal)) {
			g_SndCueMaskFull = full & 0xffff;
		}
		if (ImGui::InputInt("mask ducked", &duck, 1, 16,
				ImGuiInputTextFlags_CharsHexadecimal)) {
			g_SndCueMaskDucked = duck & 0xffff;
		}

		int hold = (int)g_SndCueHoldTicks;
		if (ImGui::InputInt("hold ticks", &hold)) {
			g_SndCueHoldTicks = hold < 0 ? 0 : hold;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("0 = stay ducked");

		if (ImGui::Button("Fire now")) {
			sndcueTrigger();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("test it without a gun");

		ImGui::Separator();
		ImGui::Text("tick %d  ppq %d  mask %04x",
				(int)cue.nowtick, (int)cue.tickspq, (unsigned)cue.mask);

		if (cue.tickspq <= 0) {
			ImGui::TextDisabled("no sequence on this slot, so no grid to land on "
					"-- cues fire immediately");
		}

		if (cue.pending) {
			ImGui::Text("pending: mask %04x at tick %d (in %d)",
					(unsigned)cue.arg, (int)cue.attick,
					(int)(cue.attick - cue.nowtick));
		} else {
			ImGui::TextDisabled("nothing pending");
		}

		ImGui::Text("state: %s", cue.ducked ? "ducked" : "full");
		if (cue.ducked && g_SndCueHoldTicks > 0) {
			ImGui::SameLine();
			ImGui::TextDisabled("(%d ticks left)", (int)cue.holdleft);
		}
	}

	if (ImGui::CollapsingHeader("Music tracks", ImGuiTreeNodeFlags_DefaultOpen)) {
		int numslots = (int)snddebugNumSlots();

		for (int slot = 0; slot < numslots; slot++) {
			s32 tracktype = 0;
			s32 tracknum = 0;
			s32 volume = 0;
			s32 state = 0;

			ImGui::PushID(slot);

			if (!snddebugGetSlot((s32)slot, &tracktype, &tracknum, &volume, &state)) {
				ImGui::TextDisabled("slot %d - empty", slot);
				ImGui::PopID();
				continue;
			}

			ImGui::Separator();
			ImGui::Text("slot %d - %s, track %d, vol %d, state %d",
					slot, imguiOverlayTrackTypeName(tracktype),
					(int)tracknum, (int)volume, (int)state);

			// uspt is microseconds per tick, so speed is its reciprocal. show
			// the multiplier and store the raw value. 488 is the init value.
			int uspt = (int)snddebugGetUspt((s32)slot);

			if (uspt > 0) {
				float speed = 488.0f / (float)uspt;

				if (ImGui::SliderFloat("speed", &speed, 0.25f, 4.0f, "%.2fx")) {
					if (speed > 0.01f) {
						snddebugSetUspt((s32)slot, (s32)(488.0f / speed));
					}
				}

				ImGui::SameLine();
				ImGui::TextDisabled("uspt %d", uspt);
			}

			float bias = snddebugGetWetBias((s32)slot);

			if (ImGui::SliderFloat("reverb bias", &bias, 0.0f, 1.0f)) {
				snddebugSetWetBias((s32)slot, bias);
			}

			float scale = snddebugGetWetScale((s32)slot);

			if (ImGui::SliderFloat("reverb scale", &scale, 0.0f, 2.0f)) {
				snddebugSetWetScale((s32)slot, scale);
			}

			if (slot < 4) {
				if (ImGui::SliderInt("voice cap", &voicecap[slot], 0, 32)) {
					snddebugSetVoiceCap((s32)slot, (s32)voicecap[slot]);
				}
			}

			if (ImGui::TreeNode("MIDI channels")) {
				u16 mask = snddebugGetChanMask((s32)slot);
				u16 newmask = mask;

				for (int chan = 0; chan < 16; chan++) {
					bool on = (mask & (1 << chan)) != 0;
					char label[8];
					snprintf(label, sizeof(label), "%d", chan);

					ImGui::PushID(chan);

					if (ImGui::Checkbox(label, &on)) {
						if (on) {
							newmask = (u16)(newmask | (1 << chan));
						} else {
							newmask = (u16)(newmask & ~(1 << chan));
						}
					}

					if ((chan % 8) != 7) {
						ImGui::SameLine();
					}

					ImGui::PopID();
				}

				if (newmask != mask) {
					snddebugSetChanMask((s32)slot, newmask);
				}

				ImGui::SliderInt("ramp rate", &chanrate, 1, 255);

				for (int chan = 0; chan < 16; chan++) {
					int vol = (int)snddebugGetChanVolume((s32)slot, (s32)chan);
					char label[24];
					snprintf(label, sizeof(label), "ch %d vol", chan);

					ImGui::PushID(256 + chan);

					if (ImGui::SliderInt(label, &vol, 0, 255)) {
						snddebugSetChanVolume((s32)slot, (s32)chan, (s32)vol, (s32)chanrate);
					}

					ImGui::PopID();
				}

				ImGui::TreePop();
			}

			ImGui::PopID();
		}
	}

	if (ImGui::CollapsingHeader("Reverb")) {
		ImGui::TextWrapped(
				"Aux bus 0 is the eight-section reverb, bus 1 has one section. "
				"naudio keeps no getter, so this is send-on-press rather than a "
				"readback, and the values do not reflect what is currently set.");

		ImGui::SliderInt("bus", &fxbus, 0, 1);

		int maxsection = (fxbus == 0) ? 7 : 0;

		// bus 1 has one section. n_alFxParamHdl refuses s >= section_count
		// silently, so clamp here rather than let Send look like it worked.
		if (fxsection > maxsection) {
			fxsection = maxsection;
		}

		ImGui::SliderInt("section", &fxsection, 0, maxsection);
		ImGui::Combo("param", &fxparam, fxparamnames, 8);
		ImGui::InputInt("value", &fxvalue);

		if (ImGui::Button("Send")) {
			fxok = snddebugSetFxParam((s32)fxbus, (s32)fxsection, (s32)fxparam, (s32)fxvalue);
			fxsent = true;
		}

		if (fxsent) {
			ImGui::SameLine();

			if (fxok) {
				ImGui::TextDisabled("sent");
			} else {
				ImGui::TextDisabled("no fx on that bus");
			}
		}

		if (fxparam == 6) {
			ImGui::TextDisabled("chorusdepth writes a discarded local in n_reverb.c - no effect");
		}
	}
}

static void imguiOverlayDrawPauseBlurPanel(void)
{
	ImGui::TextDisabled("Menu time is a dose. pd.ini [Blur] sets where these start.");
	ImGui::Separator();

	{
		bool on = g_BlurDoseEnabled != 0;

		if (ImGui::Checkbox("Charge menu time", &on)) {
			g_BlurDoseEnabled = on ? 1 : 0;
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(
					"Blur.DoseEnabled. Off means the pause menu is free and the\n"
					"drug blur only comes from drugs. Nothing else changes - a\n"
					"blur already standing keeps decaying, and every gameplay\n"
					"threshold keeps its timing.");
		}
	}

	ImGui::BeginDisabled(!g_BlurDoseEnabled);

	imguiOverlayStanceKnob("Seconds to the cap", &g_BlurDoseFullSecs, 1.0f, 120.0f, "%.0f s",
			"How long in the pause menu takes the blur to its ceiling.\n"
			"The world keeps running behind the menu, so menu time is a\n"
			"resource you spend - this is the exchange rate.");

	imguiOverlayStanceKnob("Curve", &g_BlurDoseK, 0.5f, 8.0f, "%.2f",
			"The k of (e^kd - 1)/(e^k - 1). Higher makes the early\n"
			"seconds cheaper and the late ones dearer; at 0.5 it is\n"
			"nearly a straight line. At 3 the knee lands near the\n"
			"head-sway threshold, which is what it was picked for.");

	// What those two are actually worth, at this moment, rather than in
	// the abstract: the curve is only tunable by feel and feel needs the
	// number the shot is costing you.
	{
		f32 k = g_BlurDoseK;
		f32 denom = expf(k) - 1.0f;
		s32 cap = TICKS(5000);
		s32 sway = TICKS(1000);
		f32 knee = -1.0f;

		if (denom > 0.0001f) {
			// the dose at which the floor reaches the head-sway threshold,
			// inverted straight out of the curve rather than searched for
			f32 d = logf(((f32)sway / (f32)cap) * denom + 1.0f) / k;

			if (d > 0.0f && d <= 1.0f) {
				knee = d * g_BlurDoseFullSecs;
			}
		}

		if (knee >= 0.0f) {
			ImGui::Text("head sway at %.1f s in the menu", knee);
		} else {
			ImGui::TextDisabled("head sway is unreachable on this curve");
		}
	}

	ImGui::EndDisabled();

	if (imguiOverlayCanAimInspect() && g_Vars.currentplayer->prop
			&& g_Vars.currentplayer->prop->chr) {
		struct chrdata *bond = g_Vars.currentplayer->prop->chr;
		f32 dose = g_Vars.currentplayer->blurdose;
		s32 amt = bond->blurdrugamount;

		ImGui::Text("dose %.2f of 1   blur %d   %.1f s to clear",
				dose, (s32)amt,
				amt > 0 ? (f32)amt / (60.0f * (f32)(bond->blurnumtimesdied + 1)) : 0.0f);

		if (ImGui::Button("Clear the dose")) {
			g_Vars.currentplayer->blurdose = 0.0f;
		}

		ImGui::SameLine();

		if (ImGui::Button("Clear the blur")) {
			bond->blurdrugamount = 0;
		}
	} else {
		ImGui::TextDisabled("(no live player)");
	}

	ImGui::Separator();

	if (ImGui::Button("Reset to defaults")) {
		stanceTuningReset();
	}
}

// 0xrrggbbaa <-> ImGui's float colour
static void imguiOverlayRgbaToFloat(u32 rgba, float out[4])
{
	out[0] = ((rgba >> 24) & 0xff) / 255.0f;
	out[1] = ((rgba >> 16) & 0xff) / 255.0f;
	out[2] = ((rgba >> 8) & 0xff) / 255.0f;
	out[3] = (rgba & 0xff) / 255.0f;
}

static u32 imguiOverlayFloatToRgba(const float in[4])
{
	u32 c[4];

	for (s32 i = 0; i < 4; i++) {
		f32 v = in[i] < 0.0f ? 0.0f : (in[i] > 1.0f ? 1.0f : in[i]);
		c[i] = (u32)(v * 255.0f + 0.5f);
	}

	return c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3];
}

static void imguiOverlayMenuBgColour(const char *label, u32 *rgba, bool alpha, const char *help)
{
	float f[4];

	imguiOverlayRgbaToFloat(*rgba, f);

	bool changed = alpha
		? ImGui::ColorEdit4(label, f, ImGuiColorEditFlags_AlphaBar)
		: ImGui::ColorEdit3(label, f);

	if (changed) {
		// the star colours keep their alpha byte clear; the renderer owns it
		if (!alpha) {
			f[3] = 0.0f;
		}

		*rgba = imguiOverlayFloatToRgba(f);
	}

	if (help && ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", help);
	}
}

// one haze plane: swatch, a slider per channel, and how far it's pulled in
static void imguiOverlayMenuBgHaze(const char *label, u32 *rgba, s32 *inset, const char *insethelp)
{
	static const char *names[4] = { "R", "G", "B", "A" };
	// in as far as the middle, out as far as a whole screen. the plane recedes
	// towards the centre, so pushed off the edge its far side still shows
	s32 maxinset = viGetHeight() / 2 + 10;
	s32 mininset = -viGetHeight();

	ImGui::PushID(label);
	ImGui::SeparatorText(label);

	// the sliders below are the inputs; the swatch is just a picker
	{
		float f[4];

		imguiOverlayRgbaToFloat(*rgba, f);

		if (ImGui::ColorEdit4("##swatch", f, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
			*rgba = imguiOverlayFloatToRgba(f);
		}
	}

	for (s32 i = 0; i < 4; i++) {
		s32 shift = 24 - i * 8;
		int v = (*rgba >> shift) & 0xff;

		if (ImGui::SliderInt(names[i], &v, 0, 255)) {
			*rgba = (*rgba & ~(0xffu << shift)) | ((u32)v << shift);
		}
	}

	{
		int v = *inset;

		if (ImGui::SliderInt("Inset", &v, mininset, maxinset, "%d px")) {
			*inset = v;
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", insethelp);
		}
	}

	ImGui::PopID();
}

static void imguiOverlayDrawMenuBgPanel(void)
{
	struct menubgstars *cfg = &g_MenuBgSlowStars;

	ImGui::TextDisabled("MENUBG_SUCCESS_SLOW. Live only, nothing saves yet.");

	if (g_MenuData.bg == MENUBG_SUCCESS_SLOW || g_MenuData.nextbg == MENUBG_SUCCESS_SLOW) {
		ImGui::Text("showing (bg %d, next %d)", g_MenuData.bg, g_MenuData.nextbg);
	} else {
		ImGui::TextDisabled("not showing (bg %d, next %d)", g_MenuData.bg, g_MenuData.nextbg);
	}

	ImGui::Separator();

	imguiOverlayStanceKnob("Speed", &cfg->speed, 0.0f, 10.0f, "%.2f",
			"How fast the stars come at you.\n"
			"Vanilla success is 5, Defense is 2.");

	imguiOverlayMenuBgHaze("Haze top", &cfg->hazetop, &cfg->hazetopinset,
			"Pulls the top plane down from the screen edge.\n"
			"0 is vanilla, negative pushes it off the screen.\n"
			"At the max it meets the middle.\n"
			"Colour vanilla 0x0000947f.");
	imguiOverlayMenuBgHaze("Haze bottom", &cfg->hazebottom, &cfg->hazebottominset,
			"Pulls the bottom plane up from the screen edge.\n"
			"0 is vanilla, negative pushes it off the screen.\n"
			"At the max it meets the middle.\n"
			"Colour vanilla 0x6200947f.");

	ImGui::SeparatorText("Stars");
	imguiOverlayMenuBgColour("Core", &cfg->starcore, false, "Star centre. Alpha is the depth falloff, not yours.");
	imguiOverlayMenuBgColour("Glow A", &cfg->starglow1, false, "Rim on even stars.");
	imguiOverlayMenuBgColour("Glow B", &cfg->starglow2, false, "Rim on odd stars.");

	ImGui::SeparatorText("Drug blur");
	{
		int blur = cfg->blur;

		if (ImGui::SliderInt("Amount", &blur, 0, 230)) {
			cfg->blur = blur;
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(
					"bviewDrawMotionBlur over the stars, last frame fed back\n"
					"at this alpha. 230 is the engine's cap. The feedback\n"
					"starts clean whenever the bg starts over.");
		}
	}

	ImGui::Separator();

	if (ImGui::Button("Start over")) {
		menugfxResetBgSuccessSlow();
	}

	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Fresh particles and a clean blur, same knobs.");
	}

	ImGui::SameLine();

	if (ImGui::Button("Reset to defaults")) {
		*cfg = g_MenuBgSlowStarsDefaults;
		menugfxResetBgSuccessSlow();
	}
}

static void imguiOverlayDrawLuaPanel(void)
{
	if (!g_LuaAiEnabled) {
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("Lua is off: there is no scripts/init.lua, or Game.LuaAiMode is 0. "
				"It is decided at startup.");
		ImGui::PopTextWrapPos();
		return;
	}

	ImGui::Text("State: %s", luaaiGetState() ? "loaded" : "not built yet");
	ImGui::Text("Director entries: %d", luaMenuCount());

	if (ImGui::Button("Reload scripts")) {
		g_ImGuiLuaReloadPending = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Close the Lua state and run scripts/init.lua again.\n"
				"Effects the old scripts turned on stay on until the next stage.");
	}

	ImGui::SeparatorText("Readouts");
	{
		bool fps = g_LuaShowFps != 0;
		bool mem = g_LuaShowMem != 0;

		if (ImGui::Checkbox("Frame rate", &fps)) {
			g_LuaShowFps = fps;
		}
		if (ImGui::Checkbox("Vertex pool", &mem)) {
			g_LuaShowMem = mem;
		}
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("Drawn on the HUD. pd.perf() reports both switches.");
		ImGui::PopTextWrapPos();
	}

	ImGui::SeparatorText("Run");
	ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Run").x - ImGui::GetStyle().FramePadding.x * 2.0f
			- ImGui::GetStyle().ItemSpacing.x);
	if (ImGui::InputText("##LuaInput", g_ImGuiLuaInput, sizeof(g_ImGuiLuaInput),
				ImGuiInputTextFlags_EnterReturnsTrue)) {
		g_ImGuiLuaRunPending = true;
		ImGui::SetKeyboardFocusHere(-1);
	}
	ImGui::SameLine();
	if (ImGui::Button("Run")) {
		g_ImGuiLuaRunPending = true;
	}
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("One line, under the AI instruction budget. An expression shows its value.");
	ImGui::PopTextWrapPos();

	if (g_ImGuiLuaResult[0] != '\0') {
		if (g_ImGuiLuaResultOk) {
			ImGui::TextWrapped("%s", g_ImGuiLuaResult);
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::TextWrapped("%s", g_ImGuiLuaResult);
			ImGui::PopStyleColor();
		}
	}
}

// Runs after the overlay has rendered: the game frame is finished and the
// next has not started, so no Lua call or AI tick is under way.
static void imguiOverlayRunLuaRequests(void)
{
	if (g_ImGuiLuaReloadPending) {
		g_ImGuiLuaReloadPending = false;
		luaaiReload();
		snprintf(g_ImGuiLuaResult, sizeof(g_ImGuiLuaResult), "reloaded scripts/init.lua");
		g_ImGuiLuaResultOk = true;
	}

	if (g_ImGuiLuaRunPending) {
		g_ImGuiLuaRunPending = false;
		if (g_ImGuiLuaInput[0] != '\0') {
			g_ImGuiLuaResultOk = luaMenusRunString(g_ImGuiLuaInput, g_ImGuiLuaResult,
					sizeof(g_ImGuiLuaResult)) != 0;
		}
	}
}

static void imguiOverlaySlotCell(s32 slot, const char *name)
{
	if (slot > kFojoMaxPersistableSlot) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.35f, 1.0f));
		ImGui::Text("%d %s", slot, name ? name : "?");
		ImGui::PopStyleColor();

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Past the 7-bit save field. Only the name hash brings this back.");
		}
	} else {
		ImGui::Text("%d %s", slot, name ? name : "-");
	}
}

static void imguiOverlayDrawSavesPanel(void)
{
	s32 i;

	ImGui::SeparatorText("Queue");
	{
		u32 pending = saveQueueFramesPending();

		if (saveQueueIsDirty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.35f, 1.0f));
			ImGui::Text("dirty - waiting %u frames (deadline %u)", pending, saveQueueDeadlineFrames());
			ImGui::PopStyleColor();
		} else {
			ImGui::TextDisabled("clean");
		}

		ImGui::SameLine();
		ImGui::TextDisabled("| %u flushes this session", saveQueueFlushCount());
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Writes are marked, not performed, and committed on a frame\n"
				"that had budget to spare - or at the deadline if none does.");
	}

	ImGui::SeparatorText("Flush");

	if (ImGui::Button("Flush MP profile saves")) {
		g_ImGuiSavesFlushPending = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Capture every player's current head and body as name hashes,\n"
				"write each pak-bound profile back to its slot, then write pd.ini.\n"
				"What a clean exit would do, without the exit.");
	}

	if (g_ImGuiSavesResult[0] != '\0') {
		if (g_ImGuiSavesResultOk) {
			ImGui::TextWrapped("%s", g_ImGuiSavesResult);
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::TextWrapped("%s", g_ImGuiSavesResult);
			ImGui::PopStyleColor();
		}
	}

	ImGui::SeparatorText("Loaded saves");

	if (ImGui::BeginTable("FojoSaveSlots", 7,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("P");
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Guid");
		ImGui::TableSetupColumn("Pak");
		ImGui::TableSetupColumn("Profile");
		ImGui::TableSetupColumn("Head");
		ImGui::TableSetupColumn("Body");
		ImGui::TableHeadersRow();

		for (i = 0; i < MAX_PLAYERS; i++) {
			const struct mpplayerconfig *cfg = &g_PlayerConfigsArray[i];
			const struct fileguid *guid = &cfg->fileguid;
			bool bound = guid->fileid != 0 || guid->deviceserial != 0;
			bool local = guid->deviceserial == kFojoLocalDeviceSerial;
			s32 device = (bound && !local) ? pakFindBySerial(guid->deviceserial) : -1;
			char name[sizeof(cfg->base.name) + 1];

			// base.name is a fixed field and is not guaranteed terminated.
			memcpy(name, cfg->base.name, sizeof(cfg->base.name));
			name[sizeof(cfg->base.name)] = '\0';

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::Text("%d", i + 1);

			ImGui::TableNextColumn();
			if (name[0] != '\0') {
				ImGui::TextUnformatted(name);
			} else {
				ImGui::TextDisabled("-");
			}

			ImGui::TableNextColumn();
			if (!bound) {
				ImGui::TextDisabled("none");
			} else {
				ImGui::Text("%x-%x", guid->deviceserial, guid->fileid);
			}

			ImGui::TableNextColumn();
			if (device >= 0) {
				ImGui::Text("device %d", device);
			} else if (local) {
				ImGui::TextDisabled("ini only");
			} else if (bound) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
				ImGui::TextUnformatted("no device");
				ImGui::PopStyleColor();
			} else {
				ImGui::TextDisabled("-");
			}

			// Which extended profile this player writes to, and whether that
			// profile is really its own. configindex starts at zero and is only
			// set by a profile file load or save, so a player that has done
			// neither points at profile 0 - someone else's row.
			ImGui::TableNextColumn();
			{
				s32 idx = cfg->configindex;
				bool owns = idx >= 0 && idx < g_NumProfiles && idx < CONFIG_MAX_PROFILES
						&& g_ExtendedProfiles[idx].fileguid.fileid == guid->fileid
						&& g_ExtendedProfiles[idx].fileguid.deviceserial == guid->deviceserial;

				if (owns) {
					char slug[128];
					mpProfileDebugSlug(idx, slug, sizeof(slug));
					ImGui::Text("%d %s", idx, slug[0] != '\0' ? slug : "");
				} else {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
					ImGui::Text("%d unbound", idx);
					ImGui::PopStyleColor();

					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("This player has never loaded or saved a profile file,\n"
								"so its index still names another player's profile.\n"
								"A flush skips it rather than writing there.");
					}
				}
			}

			ImGui::TableNextColumn();
			imguiOverlaySlotCell(cfg->base.mpheadnum, modHeadSlotName(cfg->base.mpheadnum));

			ImGui::TableNextColumn();
			imguiOverlaySlotCell(cfg->base.mpbodynum, modBodySlotName(cfg->base.mpbodynum));
		}

		ImGui::EndTable();
	}

	ImGui::SeparatorText("Extended ini data");
	ImGui::Text("Profiles registered: %d of %d", g_NumProfiles, CONFIG_MAX_PROFILES);

	for (i = 0; i < g_NumProfiles && i < CONFIG_MAX_PROFILES; i++) {
		char slug[128];
		char label[160];
		s32 k;

		mpProfileDebugSlug(i, slug, sizeof(slug));
		snprintf(label, sizeof(label), "[%s]###FojoProfile%d", slug[0] != '\0' ? slug : "unnamed", i);

		if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
			continue;
		}

		ImGui::PushID(i);

		if (ImGui::BeginTable("FojoProfileProps", 2,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			for (k = 0; k < mpProfileDebugPropCount(); k++) {
				const char *propname = mpProfileDebugPropName(k);

				if (!propname) {
					continue;
				}

				// ptr[0] is the fileguid, so the properties start at 1 - the
				// same offset iniBindProfileProperties registers them at.
				const extplayerprop *prop = &g_ExtendedProfiles[i].ptr[k + 1];

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(propname);
				ImGui::TableNextColumn();

				if (!mpProfileDebugPropIsS32(k)) {
					ImGui::Text("%u", prop->u8);
					continue;
				}

				ImGui::Text("%d", prop->s32);

				// The two hash rows are the reason this panel exists: the
				// number alone says nothing about whether the head comes back.
				bool ishead = strcmp(propname, "HeadNameHash") == 0;
				bool isbody = strcmp(propname, "BodyNameHash") == 0;

				if ((!ishead && !isbody) || prop->s32 == 0) {
					continue;
				}

				s32 slot = ishead ? modHeadSlotForHash((u32) prop->s32)
						: modBodySlotForHash((u32) prop->s32);

				ImGui::SameLine();

				if (slot >= 0) {
					const char *slotname = ishead ? modHeadSlotName(slot) : modBodySlotName(slot);
					ImGui::TextDisabled("-> %s (%d)", slotname ? slotname : "?", slot);
				} else {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
					ImGui::TextUnformatted("-> unresolved");
					ImGui::PopStyleColor();

					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("No loaded mod reserves that name, so the saved\n"
								"index is left as it is rather than guessed at.");
					}
				}
			}

			ImGui::EndTable();
		}

		ImGui::PopID();
	}

	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("Sections and keys are named exactly as they appear in pd.ini.");
	ImGui::PopTextWrapPos();
}

// Runs after the overlay has rendered, for the same reason as the Lua requests:
// the game frame is finished and the next has not started.
static void imguiOverlayRunSaveRequests(void)
{
	s32 saved = 0;
	s32 inionly = 0;
	s32 failed = 0;
	s32 i;

	if (!g_ImGuiSavesFlushPending) {
		return;
	}

	g_ImGuiSavesFlushPending = false;

	for (i = 0; i < MAX_PLAYERS; i++) {
		const struct fileguid *guid = &g_PlayerConfigsArray[i].fileguid;
		s32 device;

		if (!guid->fileid && !guid->deviceserial) {
			continue;
		}

		// mpplayerfileSave does this itself, but a player with no pak file
		// never reaches it, and its ini section still wants the current head.
		mpProfileFlushSlotHashes(i);

		if (guid->deviceserial == kFojoLocalDeviceSerial) {
			inionly++;
			continue;
		}

		device = pakFindBySerial(guid->deviceserial);

		if (device < 0) {
			failed++;
			continue;
		}

		if (mpplayerfileSave(i, device, guid->fileid, guid->deviceserial) == 0) {
			saved++;
		} else {
			failed++;
		}
	}

	iniProcessPendingProfiles();

	// Commit eeprom too, not just the ini - the pak writes above only marked
	// it dirty. saveQueueFlush is a no-op when nothing is pending.
	saveQueueFlush();

	// configSave returns 1 on success and 0 when it cannot open the file.
	if (configSave(CONFIG_PATH)) {
		snprintf(g_ImGuiSavesResult, sizeof(g_ImGuiSavesResult),
				"%d to pak, %d ini-only, %d failed; wrote pd.ini", saved, inionly, failed);
		g_ImGuiSavesResultOk = failed == 0;
	} else {
		snprintf(g_ImGuiSavesResult, sizeof(g_ImGuiSavesResult),
				"%d to pak, %d ini-only, %d failed; pd.ini could not be written",
				saved, inionly, failed);
		g_ImGuiSavesResultOk = false;
	}
}

/**
 * Ctrl+click on a chr in the world: latch it everywhere at once - Characters
 * scrolls to it, Skin Match takes it, Textures probes its body's first
 * texture. Picking is by projection, the same recipe chraction.c uses for the
 * debug name labels: prop pos through worldtoscreenmtx and cam0f0b4eb8 into
 * the game's screen space, then scaled from viGetWidth/Height to the window.
 * Each chr is a vertical segment from the ground to the top of the head; the
 * nearest segment within a radius scaled by distance wins. Ctrl is there so
 * a plain click still fires the gun.
 */
static bool imguiOverlayProjectToWindow(const struct coord *world, ImVec2 *out)
{
	struct player *player = g_Vars.currentplayer;
	struct coord p = *world;

	if (!player || !player->worldtoscreenmtx) {
		return false;
	}

	mtx4TransformVecInPlace(player->worldtoscreenmtx, &p);

	if (p.z > -1.0f) {
		return false; // behind the camera
	}

	f32 screen[2];
	cam0f0b4eb8(&p, screen, player->c_perspfovy, player->c_perspaspect);

	const f32 sw = viGetWidth() > 0 ? (f32)viGetWidth() : 1.0f;
	const f32 sh = viGetHeight() > 0 ? (f32)viGetHeight() : 1.0f;
	const ImVec2 disp = ImGui::GetIO().DisplaySize;
	out->x = screen[0] * disp.x / sw;
	out->y = screen[1] * disp.y / sh;
	return true;
}

// Every prop on screen this tick, as a screen-space pick shape: chrs are a
// feet-to-head segment, everything else a point at the prop position with a
// radius from a projected 30-unit offset. Nearest by distance over radius
// wins, so a small object next to a chr can still be picked.
struct imguiOverlayPickShape {
	struct prop *prop;
	ImVec2 a, b;   // segment (a == b for a point)
	float radius;
};

static s32 imguiOverlayCollectPickShapes(imguiOverlayPickShape *out, s32 max)
{
	s32 n = 0;

	if (!imguiOverlayCanAimInspect()) {
		return 0;
	}

	struct prop *prop = g_Vars.activeprops;
	for (s32 index = 0; prop && prop != g_Vars.pausedprops && index <= g_Vars.maxprops && n < max; ++index) {
		if (!imguiOverlayPropIsCurrent(prop)) {
			break;
		}
		struct prop *next = prop->next;

		if ((prop->flags & PROPFLAG_ONTHISSCREENTHISTICK)
				&& (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER
					|| prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR || prop->type == PROPTYPE_WEAPON)) {
			imguiOverlayPickShape sh;
			sh.prop = prop;
			struct coord side = prop->pos;
			side.x += 30.0f;
			ImVec2 c;
			bool ok;

			if ((prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) && imguiOverlayChrIsCurrent(prop->chr)) {
				struct coord feet = prop->pos, head = prop->pos;
				feet.y = prop->chr->ground;
				head.y = prop->chr->ground + prop->chr->height;
				ok = imguiOverlayProjectToWindow(&feet, &sh.a) && imguiOverlayProjectToWindow(&head, &sh.b)
					&& imguiOverlayProjectToWindow(&side, &c);
				if (ok) sh.radius = fabsf(c.x - sh.a.x) * 1.5f;
			} else {
				ok = imguiOverlayProjectToWindow(&prop->pos, &sh.a) && imguiOverlayProjectToWindow(&side, &c);
				sh.b = sh.a;
				if (ok) sh.radius = fabsf(c.x - sh.a.x) * 1.2f;
			}

			if (ok) {
				if (sh.radius < 20.0f) sh.radius = 20.0f;
				out[n++] = sh;
			}
		}

		prop = next;
	}

	return n;
}

static struct prop *imguiOverlayPickPropAtMouse(const ImVec2 &mouse)
{
	imguiOverlayPickShape shapes[256];
	const s32 n = imguiOverlayCollectPickShapes(shapes, 256);
	struct prop *best = NULL;
	f32 bestscore = 1.0f; // distance / radius, must be inside

	for (s32 i = 0; i < n; i++) {
		const imguiOverlayPickShape &sh = shapes[i];
		const f32 vx = sh.b.x - sh.a.x, vy = sh.b.y - sh.a.y;
		const f32 len2 = vx * vx + vy * vy;
		f32 t = len2 > 0.0f ? ((mouse.x - sh.a.x) * vx + (mouse.y - sh.a.y) * vy) / len2 : 0.0f;
		t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
		const f32 dx = mouse.x - (sh.a.x + vx * t), dy = mouse.y - (sh.a.y + vy * t);
		const f32 score = sqrtf(dx * dx + dy * dy) / sh.radius;

		if (score <= bestscore) {
			bestscore = score;
			best = sh.prop;
		}
	}

	return best;
}

static void imguiOverlayProbeModelFileTexture(u16 fileNum)
{
	const u32 count = gfx_get_debug_texture_count();
	for (u32 i = 0; i < count; i++) {
		GfxTextureDebugInfo info;
		if (gfx_get_debug_texture(i, &info) && info.type != G_TEXTYPE_NONE && info.id == fileNum) {
			imguiOverlayFocusTextureId((s32)info.texnum);
			return;
		}
	}
}

// Latch a picked prop everywhere it can go: the prop in Entities, and for a
// chr also Characters, Skin Match and the body's first texture; for an object
// its model's first texture.
static void imguiOverlayLatchPropEverywhere(struct prop *prop)
{
	g_ImGuiOverlayFocusProp = prop;
	g_ImGuiOverlayShowEntities = true;
	imguiOverlayBringFlagToCurrentWorkspace(&g_ImGuiOverlayShowEntities);

	if ((prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) && imguiOverlayChrIsCurrent(prop->chr)) {
		struct chrdata *chr = prop->chr;
		g_ImGuiOverlaySkinChr = chr;
		g_ImGuiOverlayShowSkinMatch = true;
		imguiOverlayBringFlagToCurrentWorkspace(&g_ImGuiOverlayShowSkinMatch);
		imguiOverlayFocusChr(chr);
		if (chr->bodynum >= 0) {
			imguiOverlayProbeModelFileTexture((u16)(g_HeadsAndBodies[chr->bodynum].filenum & 0xffff));
		}
		return;
	}

	if (prop->obj && prop->obj->modelnum >= 0 && prop->obj->modelnum < NUM_MODELS) {
		imguiOverlayProbeModelFileTexture((u16)(g_ModelStates[prop->obj->modelnum].fileid & 0xffff));
	}
}

// ---------------------------------------------------------------------------
// The window table.
//
// Every fojo window is one row: its ini key, its title, the flag that opens it,
// the body that draws it, where it lands the first time it is seen, and an
// optional gate -- "can this window be open at all right now". The settings
// handler, the change mask, the panel menu and the render loop below are all
// loops over this table, so adding a window means adding a row and nothing
// else. It used to mean touching five separate lists and a render block.
//
// Row order is menu order. Two orderings in it are load-bearing: Entities
// before Skin Match, because the Entities panel clears g_ImGuiOverlayFocusChr
// once it has scrolled to it and Skin Match reads it; and Textures before
// Skin Match, which share the debug texture probe.
// ---------------------------------------------------------------------------

struct imguiOverlayWindowDef {
	const char *key;        // fojo-imgui.ini key under [Fojo][Windows]
	const char *title;      // ImGui window name, always "Fojo <menu label>"
	bool *open;
	void (*draw)(void);
	f32 defaultW;
	f32 defaultH;
	f32 xAnchor;
	f32 yAnchor;
	bool (*gate)(void);     // NULL when the window is always available
};

// The skin match panel takes its chr as an argument and re-latches it from the
// focus chr first, so it gets a wrapper rather than a special case in the loop.
static void imguiOverlayDrawSkinMatchPanel(void)
{
	if (g_ImGuiOverlaySkinChr && !imguiOverlayChrIsCurrent(g_ImGuiOverlaySkinChr)) {
		g_ImGuiOverlaySkinChr = NULL;
	}
	// a fresh focus (Show in Characters) also latches, so either menu item works
	if (g_ImGuiOverlayFocusChr && imguiOverlayChrIsCurrent(g_ImGuiOverlayFocusChr)) {
		g_ImGuiOverlaySkinChr = g_ImGuiOverlayFocusChr;
	}

	imguiSkinMatchDrawPanel(g_ImGuiOverlaySkinChr);
}

static const struct imguiOverlayWindowDef g_ImGuiOverlayWindowDefs[] = {
	{ "Runtime",     "Fojo Runtime",     &g_ImGuiOverlayShowRuntime,     imguiOverlayDrawRuntimePanel,     360.0f, 300.0f, 0.0f, 0.00f, NULL },
	{ "Stage",       "Fojo Stage",       &g_ImGuiOverlayShowStage,       imguiOverlayDrawStagePanel,       480.0f, 560.0f, 1.0f, 0.00f, NULL },
	{ "Entities",    "Fojo Entities",    &g_ImGuiOverlayShowEntities,    imguiOverlayDrawEntitiesPanel,    520.0f, 620.0f, 1.0f, 0.00f, NULL },
	{ "Assets",      "Fojo Assets",      &g_ImGuiOverlayShowAssets,      imguiOverlayDrawAssetsPanel,      620.0f, 560.0f, 0.5f, 0.20f, NULL },
	{ "Textures",    "Fojo Textures",    &g_ImGuiOverlayShowTextures,    imguiOverlayDrawTexturesPanel,    520.0f, 620.0f, 1.0f, 0.25f, NULL },
	{ "SkinMatch",   "Fojo Skin Match",  &g_ImGuiOverlayShowSkinMatch,   imguiOverlayDrawSkinMatchPanel,   760.0f, 640.0f, 1.0f, 0.50f, NULL },
	{ "Memory",      "Fojo Memory",      &g_ImGuiOverlayShowMemory,      imguiOverlayDrawMemoryPanel,      360.0f, 300.0f, 0.0f, 1.00f, NULL },
	{ "Profiler",    "Fojo Profiler",    &g_ImGuiOverlayShowProfiler,    imguiOverlayDrawProfilerPanel,    620.0f, 360.0f, 0.5f, 1.00f, NULL },
	{ "LookingAt",   "Fojo Looking At",  &g_ImGuiOverlayShowLookingAt,   imguiOverlayDrawLookingAtPanel,   430.0f, 340.0f, 1.0f, 0.00f, imguiOverlayCanAimInspect },
	{ "Audio",       "Fojo Audio",       &g_ImGuiOverlayShowAudio,       imguiOverlayDrawAudioPanel,       440.0f, 560.0f, 0.5f, 0.50f, NULL },
	{ "Proportions", "Fojo Proportions", &g_ImGuiOverlayShowProportions, imguiOverlayDrawProportionsPanel, 460.0f, 620.0f, 0.0f, 0.50f, NULL },
	{ "Stance",      "Fojo Stance",      &g_ImGuiOverlayShowStance,      imguiOverlayDrawStancePanel,      420.0f, 560.0f, 0.5f, 0.50f, NULL },
	{ "PauseBlur",   "Fojo Pause Blur",  &g_ImGuiOverlayShowPauseBlur,   imguiOverlayDrawPauseBlurPanel,   400.0f, 300.0f, 0.5f, 0.50f, NULL },
	{ "MenuBg",      "Fojo Menu Bg",     &g_ImGuiOverlayShowMenuBg,      imguiOverlayDrawMenuBgPanel,      380.0f, 600.0f, 0.5f, 0.50f, NULL },
	{ "Lua",         "Fojo Lua",         &g_ImGuiOverlayShowLua,         imguiOverlayDrawLuaPanel,         420.0f, 300.0f, 0.0f, 0.75f, NULL },
	{ "Saves",       "Fojo Saves",       &g_ImGuiOverlayShowSaves,       imguiOverlayDrawSavesPanel,       520.0f, 420.0f, 0.5f, 0.50f, NULL },
};

// The change mask below is one bit per row, so the table has a ceiling of 32.
static const s32 kFojoWindowCount =
	(s32)(sizeof(g_ImGuiOverlayWindowDefs) / sizeof(g_ImGuiOverlayWindowDefs[0]));

// ---------------------------------------------------------------------------
// Where a window lands when it comes back.
//
// Fojo owns placement now. ImGui still handles the live drag and resize -- that
// is what it is good at -- but the record of where a window BELONGS lives here,
// one row per row of the table above. Three things used to answer that question
// separately and one of them was wrong: the viewport defaults, ImGui's ini, and
// a partial std::map kept only for the minimise stack.
//
// The one mechanism is `live`: a window that was not submitted last frame is
// reappearing -- reopened from the menu, un-minimised, or (next) arriving from
// another workspace -- and gets its geometry reapplied exactly once. Every other
// frame applies nothing, so dragging still works.
//
// ImGui's own ini is deliberately NOT disabled. It costs a duplicate block and
// it buys migration for free: a window fojo has no record for yet keeps
// whatever ImGui remembered, and the read-back below adopts it on the first
// frame. Where both have an opinion, fojo's is the one applied.
// ---------------------------------------------------------------------------

struct imguiOverlayWindowRt {
	ImVec2 pos;
	ImVec2 size;          // SizeFull, never the collapsed title-bar rect
	bool haveGeometry;    // false until seen once or loaded from the ini
	bool collapsed;
	bool minimised;       // parked in the lower-left stack
	bool live;            // was submitted last frame
	s32 ws;               // which floor it lives on, 0..kFojoWorkspaces-1
	bool sticky;          // shows on every floor
};

// Four floors of the institute. A window lives on exactly one, unless it is
// sticky, in which case it shows on all of them. A window that is neither on
// the visible floor nor sticky is simply NOT SUBMITTED -- no Begin, no cost --
// and the `live` flag from the geometry record is what puts it back where it
// belongs when you switch to its floor. That is the whole of it.
static const s32 kFojoWorkspaces = 4;
static s32 g_ImGuiOverlayWorkspace = 0;
static s32 g_ImGuiOverlayCtxWindow = -1;
static bool g_ImGuiOverlayCtxOpen = false;

static struct imguiOverlayWindowRt g_ImGuiOverlayWindowRt[kFojoWindowCount];
static s32 g_ImGuiOverlayMinimisedOrder[kFojoWindowCount];
static s32 g_ImGuiOverlayMinimisedCount = 0;

static bool imguiOverlayWindowOnThisFloor(s32 index)
{
	const struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[index];

	return rt.sticky || rt.ws == g_ImGuiOverlayWorkspace;
}

/**
 * Opening a window brings it to the floor you are on; focusing an already-open
 * one takes you to its floor instead. Without the first half, force-opens like
 * imguiOverlayLatchPropEverywhere would open windows you cannot see.
 */
static void imguiOverlayBringToCurrentWorkspace(s32 index)
{
	struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[index];

	if (!rt.sticky) {
		rt.ws = g_ImGuiOverlayWorkspace;
	}
}

static void imguiOverlayBringFlagToCurrentWorkspace(bool *open)
{
	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		if (g_ImGuiOverlayWindowDefs[i].open == open) {
			imguiOverlayBringToCurrentWorkspace(i);
			return;
		}
	}
}

static float imguiOverlayCollapsedTitleWidth(const char *name)
{
	const ImGuiStyle &style = ImGui::GetStyle();
	// title text, the collapse arrow and the close button, each a frame high
	return ImGui::CalcTextSize(name).x + style.FramePadding.x * 2.0f + ImGui::GetFrameHeight() * 2.0f + style.ItemInnerSpacing.x * 2.0f;
}

static ImVec2 imguiOverlayMinimisedSlotPos(s32 slot)
{
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const float titleH = ImGui::GetFrameHeight();
	const float gap = 4.0f;
	return ImVec2(viewport->WorkPos.x + 8.0f,
		viewport->WorkPos.y + viewport->WorkSize.y - 8.0f - (slot + 1) * (titleH + gap) + gap);
}

static s32 imguiOverlayMinimisedSlot(s32 index)
{
	for (s32 i = 0; i < g_ImGuiOverlayMinimisedCount; ++i) {
		if (g_ImGuiOverlayMinimisedOrder[i] == index) {
			return i;
		}
	}

	return -1;
}

/**
 * Where a parked window sits in the pile you can actually see. Counts only the
 * windows minimised on the visible floor, so floor 3's stack does not leave
 * holes for floor 1's windows.
 */
static s32 imguiOverlayMinimisedVisibleSlot(s32 index)
{
	s32 slot = 0;

	for (s32 i = 0; i < g_ImGuiOverlayMinimisedCount; ++i) {
		const s32 other = g_ImGuiOverlayMinimisedOrder[i];

		if (!imguiOverlayWindowOnThisFloor(other)) {
			continue;
		}

		if (other == index) {
			return slot;
		}

		slot++;
	}

	return -1;
}

static void imguiOverlayMinimisedPush(s32 index)
{
	if (imguiOverlayMinimisedSlot(index) < 0 && g_ImGuiOverlayMinimisedCount < kFojoWindowCount) {
		g_ImGuiOverlayMinimisedOrder[g_ImGuiOverlayMinimisedCount++] = index;
	}
}

static void imguiOverlayMinimisedErase(s32 index)
{
	const s32 slot = imguiOverlayMinimisedSlot(index);

	if (slot < 0) {
		return;
	}

	for (s32 i = slot; i + 1 < g_ImGuiOverlayMinimisedCount; ++i) {
		g_ImGuiOverlayMinimisedOrder[i] = g_ImGuiOverlayMinimisedOrder[i + 1];
	}

	g_ImGuiOverlayMinimisedCount--;
}

/**
 * Begin a Fojo window by table index. Returns what ImGui::Begin returned; the
 * caller still owns the matching ImGui::End.
 *
 * Collapsing a window (the title-bar arrow, or a double-click on the title) is
 * the minimise: it shrinks to its title and stacks in the lower-left corner in
 * the order it was collapsed. Expanding puts it back where it was, at the size
 * it had -- which is now true across a restart as well, because the record
 * below is persisted.
 */
static bool imguiOverlayBeginWindow(s32 index)
{
	const struct imguiOverlayWindowDef *def = &g_ImGuiOverlayWindowDefs[index];
	struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[index];
	const ImVec2 defaultSize(def->defaultW, def->defaultH);

	imguiOverlaySetNextWindowDefaults(defaultSize, def->xAnchor, def->yAnchor);

	// reappearing: put it back, once
	if (!rt.live && rt.haveGeometry) {
		ImGui::SetNextWindowPos(rt.pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(rt.size, ImGuiCond_Always);
		ImGui::SetNextWindowCollapsed(rt.collapsed, ImGuiCond_Always);
	}

	if (rt.minimised) {
		const s32 slot = imguiOverlayMinimisedVisibleSlot(index);
		ImGui::SetNextWindowPos(imguiOverlayMinimisedSlotPos(slot < 0 ? 0 : slot), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(imguiOverlayCollapsedTitleWidth(def->title), rt.size.y), ImGuiCond_Always);
	}

	const bool wasMinimised = rt.minimised;
	const bool visible = ImGui::Begin(def->title, def->open);
	const ImGuiWindow *w = ImGui::GetCurrentWindow();
	const bool collapsed = ImGui::IsWindowCollapsed();

	// Read back BEFORE the transition below, and only while the window is not
	// parked: a parked window has just had its pos and width forced to the stack
	// slot, so reading them here would overwrite its real home with the slot.
	// SizeFull, never w->Size -- for a collapsed window Size is the title bar.
	if (!wasMinimised) {
		rt.pos = w->Pos;
		rt.size = w->SizeFull;
		if (rt.size.y < 40.0f) {
			rt.size.y = defaultSize.y;
		}
		rt.haveGeometry = true;
	}

	rt.collapsed = collapsed;
	rt.live = true;

	// Right-click the TITLE BAR specifically. BeginPopupContextWindow would fire
	// anywhere in the window and collide with imguiOverlayDrawEntityContextMenu
	// on every tree node in Entities. A collapsed window still runs Begin, so a
	// parked window can be sent away too. The popup itself is opened at top
	// level in imguiOverlayRender -- a window-scoped popup never opens while
	// SkipItems is set, which is exactly the collapsed case.
	{
		ImGuiContext &g = *ImGui::GetCurrentContext();
		const ImRect title = w->TitleBarRect();

		if (g.HoveredWindow == w && ImGui::IsMouseClicked(ImGuiMouseButton_Right)
				&& ImGui::IsMouseHoveringRect(title.Min, title.Max, false)) {
			g_ImGuiOverlayCtxWindow = index;
			g_ImGuiOverlayCtxOpen = true;
		}
	}

	if (collapsed && !rt.minimised) {
		rt.minimised = true;
		imguiOverlayMinimisedPush(index);
	} else if (!collapsed && rt.minimised) {
		rt.minimised = false;
		imguiOverlayMinimisedErase(index);
		ImGui::SetWindowPos(rt.pos, ImGuiCond_Always);
		ImGui::SetWindowSize(rt.size, ImGuiCond_Always);
	}

	return visible;
}

// Menu label. Every title is "Fojo <label>"; fall back to the whole title if a
// row ever breaks that rule rather than chopping five characters off blindly.
static const char *imguiOverlayWindowLabel(const struct imguiOverlayWindowDef *def)
{
	return strncmp(def->title, "Fojo ", 5) == 0 ? def->title + 5 : def->title;
}

// Close any window whose gate has stopped holding. Runs before the menu draws
// so a gated-shut window does not show a stale tick for a frame.
static void imguiOverlayApplyWindowGates(void)
{
	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		const struct imguiOverlayWindowDef *def = &g_ImGuiOverlayWindowDefs[i];
		if (def->gate && !def->gate()) {
			*def->open = false;
		}
	}
}

static void *imguiOverlaySettingsReadOpen(ImGuiContext *, ImGuiSettingsHandler *handler, const char *name)
{
	return strcmp(name, "Windows") == 0 ? handler : NULL;
}

static void imguiOverlaySettingsReadLine(ImGuiContext *, ImGuiSettingsHandler *, void *, const char *line)
{
	int value;

	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		const struct imguiOverlayWindowDef *def = &g_ImGuiOverlayWindowDefs[i];
		const size_t keyLen = strlen(def->key);
		if (strncmp(line, def->key, keyLen) != 0 || line[keyLen] != '=') {
			continue;
		}

		const char *rest = line + keyLen + 1;
		if (sscanf(rest, "%d", &value) == 1) {
			*def->open = value != 0;
		}

		// Geometry is optional on the line, so a fojo-imgui.ini written before
		// this existed still loads -- it just has no record yet, and the first
		// frame adopts whatever ImGui remembered instead.
		struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[i];
		const char *tok;
		float px, py, sx, sy;
		int flag;
		bool havePos = false;
		bool haveSize = false;

		if ((tok = strstr(rest, "pos=")) != NULL && sscanf(tok, "pos=%f,%f", &px, &py) == 2) {
			rt.pos = ImVec2(px, py);
			havePos = true;
		}

		if ((tok = strstr(rest, "size=")) != NULL && sscanf(tok, "size=%f,%f", &sx, &sy) == 2
				&& sx > 0.0f && sy > 0.0f) {
			rt.size = ImVec2(sx, sy);
			haveSize = true;
		}

		rt.haveGeometry = havePos && haveSize;

		if ((tok = strstr(rest, "ws=")) != NULL && sscanf(tok, "ws=%d", &flag) == 1
				&& flag >= 0 && flag < kFojoWorkspaces) {
			rt.ws = flag;
		}

		if ((tok = strstr(rest, "sticky=")) != NULL && sscanf(tok, "sticky=%d", &flag) == 1) {
			rt.sticky = flag != 0;
		}

		if ((tok = strstr(rest, "collapsed=")) != NULL && sscanf(tok, "collapsed=%d", &flag) == 1) {
			rt.collapsed = flag != 0;
			// a window saved collapsed rejoins the stack in load order, so the
			// lower-left pile is the same one you left
			if (rt.collapsed && rt.haveGeometry) {
				rt.minimised = true;
				imguiOverlayMinimisedPush(i);
			}
		}

		return;
	}

	{
		int ws;

		if (sscanf(line, "Workspace=%d", &ws) == 1 && ws >= 0 && ws < kFojoWorkspaces) {
			g_ImGuiOverlayWorkspace = ws;
		}
	}

	{
		float fvalue;

		if (sscanf(line, "LoreScale=%f", &fvalue) == 1 && fvalue > 0.05f && fvalue < 20.0f) {
			g_ImGuiPropLoreScale = fvalue;
		}
	}
}

static void imguiOverlaySettingsWriteAll(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buffer)
{
	buffer->appendf("[%s][Windows]\n", handler->TypeName);

	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		const struct imguiOverlayWindowDef *def = &g_ImGuiOverlayWindowDefs[i];
		const struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[i];

		if (!rt.haveGeometry) {
			buffer->appendf("%s=%d ws=%d sticky=%d\n", def->key, *def->open, rt.ws, rt.sticky);
			continue;
		}

		buffer->appendf("%s=%d pos=%.0f,%.0f size=%.0f,%.0f collapsed=%d ws=%d sticky=%d\n",
				def->key, *def->open, rt.pos.x, rt.pos.y, rt.size.x, rt.size.y,
				rt.collapsed, rt.ws, rt.sticky);
	}

	buffer->appendf("Workspace=%d\n", g_ImGuiOverlayWorkspace);
	buffer->appendf("LoreScale=%.5f\n\n", g_ImGuiPropLoreScale);
}

/**
 * A bit per open window, compared against itself frame to frame to notice a
 * window opening or closing. Never persisted, so the bit assignment is free to
 * follow the table.
 */
static u32 imguiOverlayGetWindowState(void)
{
	u32 state = 0;

	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		if (*g_ImGuiOverlayWindowDefs[i].open) {
			state |= 1u << i;
		}
	}

	return state;
}

/**
 * A cheap hash of every window's geometry, compared against itself frame to
 * frame to notice a drag or a resize. Never persisted; it only decides whether
 * the ini is worth marking dirty.
 */
static u32 imguiOverlayGeometryFingerprint(void)
{
	u32 h = 2166136261u;

	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		const struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[i];
		const s32 v[5] = {
			(s32)rt.pos.x, (s32)rt.pos.y, (s32)rt.size.x, (s32)rt.size.y,
			(rt.collapsed ? 1 : 0) | (rt.minimised ? 2 : 0)
				| (rt.sticky ? 4 : 0) | (rt.ws << 3),
		};

		for (s32 k = 0; k < 5; ++k) {
			h = (h ^ (u32)v[k]) * 16777619u;
		}
	}

	return h;
}

static void imguiOverlaySaveWindowState(void)
{
	ImGui::MarkIniSettingsDirty();
	ImGui::SaveIniSettingsToDisk(g_ImGuiOverlayIniPath);
}

/**
 * The menu a right-click on a window's title bar opens: which floor it lives
 * on, whether it follows you everywhere, and the two housekeeping items.
 */
static void imguiOverlayDrawWindowContextMenu(void)
{
	if (g_ImGuiOverlayCtxOpen) {
		ImGui::OpenPopup("FojoWindowCtx");
		g_ImGuiOverlayCtxOpen = false;
	}

	if (!ImGui::BeginPopup("FojoWindowCtx")) {
		return;
	}

	if (g_ImGuiOverlayCtxWindow < 0 || g_ImGuiOverlayCtxWindow >= kFojoWindowCount) {
		ImGui::EndPopup();
		return;
	}

	const s32 index = g_ImGuiOverlayCtxWindow;
	const struct imguiOverlayWindowDef *def = &g_ImGuiOverlayWindowDefs[index];
	struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[index];

	ImGui::SeparatorText(imguiOverlayWindowLabel(def));

	for (s32 ws = 0; ws < kFojoWorkspaces; ++ws) {
		char label[32];
		snprintf(label, sizeof(label), "Send to floor %d", ws + 1);

		if (ImGui::MenuItem(label, NULL, !rt.sticky && rt.ws == ws, !rt.sticky)) {
			rt.ws = ws;
		}
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Sticky (all floors)", NULL, rt.sticky)) {
		rt.sticky = !rt.sticky;
		if (!rt.sticky) {
			// landing it where you are beats landing it on whichever floor it
			// happened to be on before it went sticky
			rt.ws = g_ImGuiOverlayWorkspace;
		}
	}

	if (ImGui::MenuItem("Reset position")) {
		rt.haveGeometry = false;
		rt.minimised = false;
		imguiOverlayMinimisedErase(index);
		rt.live = false;
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Close")) {
		*def->open = false;
	}

	ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// The awesome bar. Ctrl+P (Super+P too, where the WM lets it through).
//
// Type and it searches everything, ranked. A leading sigil forces one corpus:
//   @  entities -- props and chrs        >  commands
//   `  windows and panels
// A bare sigil is a mode: ` on its own lists every window, which is the window
// switcher with no query. \ at position 0 escapes the next character, for the
// one corpus whose names are mod-supplied and so not guaranteed sigil-free.
//
// Sigils bind at position 0 ONLY. Measured against the ROM file table: across
// all 2013 names the set of leading characters is A L P b C U G o, so no name
// starts with a sigil -- but 181 of them CONTAIN '/', so a sigil must never be
// treated as a delimiter inside a query.
//
// Stages, file slots and textures are not here yet: those want an index built
// once on stage/mod change rather than a walk per keystroke, because the file
// slot corpus alone is 8192 romdataGetFileSlotInfo calls per mod.
// ---------------------------------------------------------------------------

// File slots come from the file: drive in asset.c, which owns the index the
// bar used to build here (8192 romdataGetFileSlotInfo calls per mod, built
// once and invalidated on stage load and roster change). The bar maps its #
// sigil to file: and is one consumer of that index, not its owner.

enum {
	kFojoBarWindow,
	kFojoBarEntity,
	kFojoBarCommand,
	kFojoBarFileSlot,
	kFojoBarStage,
	kFojoBarTexture,
};

struct imguiOverlayBarHit {
	s32 kind;
	s32 index;              // window row, or command row
	struct prop *prop;      // entity
	struct assetref ref;    // file slot, stage, texture
	s32 score;
	char label[80];
	char detail[32];
};

static const s32 kFojoBarMaxHits = 48;
static bool g_ImGuiOverlayBarOpen = false;
static bool g_ImGuiOverlayBarJustOpened = false;
static char g_ImGuiOverlayBarQuery[128];
static s32 g_ImGuiOverlayBarSel = 0;
static struct imguiOverlayBarHit g_ImGuiOverlayBarHits[kFojoBarMaxHits];
static s32 g_ImGuiOverlayBarHitCount = 0;

struct imguiOverlayCommandDef {
	const char *name;
	void (*run)(void);
};

static void imguiOverlayCmdFloor1(void) { g_ImGuiOverlayWorkspace = 0; }
static void imguiOverlayCmdFloor2(void) { g_ImGuiOverlayWorkspace = 1; }
static void imguiOverlayCmdFloor3(void) { g_ImGuiOverlayWorkspace = 2; }
static void imguiOverlayCmdFloor4(void) { g_ImGuiOverlayWorkspace = 3; }

static void imguiOverlayCmdCloseAll(void)
{
	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		*g_ImGuiOverlayWindowDefs[i].open = false;
	}
}

static void imguiOverlayCmdResetPositions(void)
{
	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[i];
		rt.haveGeometry = false;
		rt.minimised = false;
		rt.live = false;
	}

	g_ImGuiOverlayMinimisedCount = 0;
}

static void imguiOverlayCmdReloadLua(void)   { g_ImGuiLuaReloadPending = true; }
static void imguiOverlayCmdFlushSaves(void)  { g_ImGuiSavesFlushPending = true; }
static void imguiOverlayCmdHide(void)        { imguiOverlaySetVisible(false); }

static const struct imguiOverlayCommandDef g_ImGuiOverlayCommandDefs[] = {
	{ "Go to floor 1",            imguiOverlayCmdFloor1 },
	{ "Go to floor 2",            imguiOverlayCmdFloor2 },
	{ "Go to floor 3",            imguiOverlayCmdFloor3 },
	{ "Go to floor 4",            imguiOverlayCmdFloor4 },
	{ "Close all windows",        imguiOverlayCmdCloseAll },
	{ "Reset window positions",   imguiOverlayCmdResetPositions },
	{ "Reload Lua",               imguiOverlayCmdReloadLua },
	{ "Flush saves",              imguiOverlayCmdFlushSaves },
	{ "Hide the debugger",        imguiOverlayCmdHide },
};

static const s32 kFojoCommandCount =
	(s32)(sizeof(g_ImGuiOverlayCommandDefs) / sizeof(g_ImGuiOverlayCommandDefs[0]));

static char imguiOverlayBarLower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/**
 * Subsequence match with a score. Every query character must appear in the
 * candidate, in order, but not adjacently -- "sknm" finds "Skin Match".
 *
 * ImGuiTextFilter, which the panels use, is substring-only and carries no
 * score, so it cannot rank. Ranking is the whole difference between a filter
 * box and a bar you can type three letters into and hit enter.
 */
static bool imguiOverlayBarFuzzy(const char *cand, const char *query, s32 *outScore)
{
	s32 score = 0;
	s32 run = 0;
	s32 ci = 0;
	s32 firstHit = -1;

	if (!query[0]) {
		*outScore = 0;
		return true;
	}

	for (s32 qi = 0; query[qi]; ++qi) {
		const char q = imguiOverlayBarLower(query[qi]);
		bool found = false;

		if (q == ' ') {
			continue;
		}

		while (cand[ci]) {
			const char c = imguiOverlayBarLower(cand[ci]);
			const bool wordStart = ci == 0
				|| (cand[ci - 1] == ' ' || cand[ci - 1] == '_' || cand[ci - 1] == '-'
					|| cand[ci - 1] == '/' || cand[ci - 1] == '.');

			if (c == q) {
				if (firstHit < 0) {
					firstHit = ci;
				}

				score += 1 + run * 5 + (wordStart ? 8 : 0);
				run++;
				ci++;
				found = true;
				break;
			}

			run = 0;
			ci++;
		}

		if (!found) {
			return false;
		}
	}

	// a hit near the front, in a short name, beats the same hit buried in a
	// long one -- "Stage" should outrank "Skin Match" for "sta"
	score -= firstHit;
	score += 40 - (s32)strlen(cand);
	*outScore = score;
	return true;
}

static void imguiOverlayBarPush(s32 kind, s32 index, struct prop *prop, s32 score,
		const char *label, const char *detail, const struct assetref *ref = NULL)
{
	s32 slot;

	if (g_ImGuiOverlayBarHitCount < kFojoBarMaxHits) {
		slot = g_ImGuiOverlayBarHitCount++;
	} else {
		// full: replace the current worst IN PLACE, keeping the count. an
		// earlier version set the count to `worst` and appended, which quietly
		// threw away every result after it.
		s32 worst = 0;

		for (s32 i = 1; i < g_ImGuiOverlayBarHitCount; ++i) {
			if (g_ImGuiOverlayBarHits[i].score < g_ImGuiOverlayBarHits[worst].score) {
				worst = i;
			}
		}

		if (g_ImGuiOverlayBarHits[worst].score >= score) {
			return;
		}

		slot = worst;
	}

	struct imguiOverlayBarHit &hit = g_ImGuiOverlayBarHits[slot];
	hit.kind = kind;
	hit.index = index;
	hit.prop = prop;
	hit.score = score;
	if (ref) {
		hit.ref = *ref;
	} else {
		memset(&hit.ref, 0, sizeof(hit.ref));
		hit.ref.owner = -1;
		hit.ref.id = -1;
		hit.ref.sub = -1;
	}
	snprintf(hit.label, sizeof(hit.label), "%s", label);
	snprintf(hit.detail, sizeof(hit.detail), "%s", detail ? detail : "");
}

struct imguiOverlayBarAssetCtx {
	const char *q;
	s32 kind;       // kFojoBarFileSlot / kFojoBarStage / kFojoBarTexture
	s32 wanted;     // literal number from a "#3412" style query, else -1
};

static const char *imguiOverlayBarOwnerLabel(s8 owner)
{
	return owner == ASSET_OWNER_VANILLA ? "vanilla" : (owner >= 0 && owner < 64 && g_ModNames[owner][0]) ? g_ModNames[owner] : "?";
}

// One visitor for the three asset corpora. The bar's sigils map onto drives
// (# file:, / stage:, % tex:) before this is ever called; the drive layer
// never sees a sigil.
static s32 imguiOverlayBarAssetVisit(const struct assetref *ref, const char *name, void *vctx)
{
	const struct imguiOverlayBarAssetCtx *ctx = (const struct imguiOverlayBarAssetCtx *)vctx;
	char detail[32];
	s32 score;

	if (ctx->wanted >= 0) {
		const s32 number = ctx->kind == kFojoBarTexture ? ref->sub : ref->id;

		if (number != ctx->wanted) {
			return 1;
		}

		score = 1000;
	} else if (!imguiOverlayBarFuzzy(name, ctx->q, &score)) {
		return 1;
	}

	switch (ctx->kind) {
	case kFojoBarFileSlot:
		snprintf(detail, sizeof(detail), "file %d  %s", ref->id, imguiOverlayBarOwnerLabel(ref->owner));
		break;
	case kFojoBarStage:
		snprintf(detail, sizeof(detail), "stage 0x%02x  %s", ref->id, imguiOverlayBarOwnerLabel(ref->owner));
		break;
	case kFojoBarTexture:
		if (ref->owner == ASSET_OWNER_VANILLA) {
			snprintf(detail, sizeof(detail), "tex %d  vanilla", ref->id);
		} else {
			snprintf(detail, sizeof(detail), "port %d  %s", ref->sub, imguiOverlayBarOwnerLabel(ref->owner));
		}
		break;
	default:
		detail[0] = '\0';
		break;
	}

	imguiOverlayBarPush(ctx->kind, -1, NULL, score, name, detail, ref);
	return 1;
}

static void imguiOverlayBarSearchAssets(const char *q, s32 kind, const char *drivePath)
{
	// "#3412" / "%3412" / "/32" mean that number, not a name containing it
	bool numeric = q[0] != '\0';

	for (const char *c = q; *c; ++c) {
		if (*c < '0' || *c > '9') {
			numeric = false;
			break;
		}
	}

	struct imguiOverlayBarAssetCtx ctx;
	ctx.q = q;
	ctx.kind = kind;
	ctx.wanted = numeric ? atoi(q) : -1;
	assetEnumerate(drivePath, imguiOverlayBarAssetVisit, &ctx);
}

static void imguiOverlayBarSearch(void)
{
	const char *q = g_ImGuiOverlayBarQuery;
	bool wantWindows = true;
	bool wantEntities = true;
	bool wantCommands = true;
	bool wantFiles = false;   // opt-in: thousands of rows would swamp a bare query
	bool wantStages = false;
	bool wantTextures = false;
	s32 score;

	g_ImGuiOverlayBarHitCount = 0;

	// sigils bind at position 0 only
	if (q[0] == '`')      { wantEntities = wantCommands = false; q++; }
	else if (q[0] == '@') { wantWindows = wantCommands = false;  q++; }
	else if (q[0] == '>') { wantWindows = wantEntities = false;  q++; }
	else if (q[0] == '#') { wantWindows = wantEntities = wantCommands = false; wantFiles = true; q++; }
	else if (q[0] == '/') { wantWindows = wantEntities = wantCommands = false; wantStages = true; q++; }
	else if (q[0] == '%') { wantWindows = wantEntities = wantCommands = false; wantTextures = true; q++; }
	else if (q[0] == '\\') { q++; }

	while (*q == ' ') {
		q++;
	}

	if (wantWindows) {
		for (s32 i = 0; i < kFojoWindowCount; ++i) {
			const struct imguiOverlayWindowDef *def = &g_ImGuiOverlayWindowDefs[i];
			const struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[i];
			const char *label = imguiOverlayWindowLabel(def);
			char detail[32];

			if (def->gate && !def->gate()) {
				continue;
			}

			if (!imguiOverlayBarFuzzy(label, q, &score)) {
				continue;
			}

			if (rt.sticky) {
				snprintf(detail, sizeof(detail), "window  all floors");
			} else {
				snprintf(detail, sizeof(detail), "window  floor %d", rt.ws + 1);
			}

			imguiOverlayBarPush(kFojoBarWindow, i, NULL, score + (*def->open ? 2 : 0),
					label, detail);
		}
	}

	if (wantCommands) {
		for (s32 i = 0; i < kFojoCommandCount; ++i) {
			if (imguiOverlayBarFuzzy(g_ImGuiOverlayCommandDefs[i].name, q, &score)) {
				imguiOverlayBarPush(kFojoBarCommand, i, NULL, score,
						g_ImGuiOverlayCommandDefs[i].name, "command");
			}
		}
	}

	// Entities are walked live, not indexed: props come and go. Only while the
	// bar is open, and only the active list, which the Entities panel already
	// walks once a frame.
	if (wantEntities && q[0]) {
		char label[80];

		if (g_ChrSlots && g_NumChrSlots) {
			for (s32 i = 0; i < g_NumChrSlots; ++i) {
				struct chrdata *chr = &g_ChrSlots[i];

				if (chr->chrnum < 0 || !chr->prop) {
					continue;
				}

				snprintf(label, sizeof(label), "%s", imguiOverlayHeadBodyName(chr->bodynum));

				if (imguiOverlayBarFuzzy(label, q, &score)) {
					char detail[32];
					snprintf(detail, sizeof(detail), "chr 0x%04x", (u16)chr->chrnum);
					imguiOverlayBarPush(kFojoBarEntity, i, chr->prop, score, label, detail);
				}
			}
		}

		struct prop *prop = g_Vars.activeprops;

		for (s32 i = 0; prop && prop != g_Vars.pausedprops && i <= g_Vars.maxprops; ++i) {
			struct prop *next = prop->next;

			if (!imguiOverlayPropIsCurrent(prop)) {
				break;
			}

			if (prop->type != PROPTYPE_CHR && prop->type != PROPTYPE_PLAYER) {
				snprintf(label, sizeof(label), "%s", imguiOverlayPropTypeName(prop->type));

				if (imguiOverlayBarFuzzy(label, q, &score)) {
					imguiOverlayBarPush(kFojoBarEntity, i, prop, score - 6, label, "prop");
				}
			}

			prop = next;
		}
	}

	if (wantFiles) {
		imguiOverlayBarSearchAssets(q, kFojoBarFileSlot, "file:/");
	}

	if (wantStages) {
		imguiOverlayBarSearchAssets(q, kFojoBarStage, "stage:/");
	}

	if (wantTextures) {
		imguiOverlayBarSearchAssets(q, kFojoBarTexture, "tex:/");
	}

	// insertion sort: at most kFojoBarMaxHits, and almost always far fewer
	for (s32 i = 1; i < g_ImGuiOverlayBarHitCount; ++i) {
		const struct imguiOverlayBarHit key = g_ImGuiOverlayBarHits[i];
		s32 j = i - 1;

		while (j >= 0 && g_ImGuiOverlayBarHits[j].score < key.score) {
			g_ImGuiOverlayBarHits[j + 1] = g_ImGuiOverlayBarHits[j];
			j--;
		}

		g_ImGuiOverlayBarHits[j + 1] = key;
	}

	if (g_ImGuiOverlayBarSel >= g_ImGuiOverlayBarHitCount) {
		g_ImGuiOverlayBarSel = g_ImGuiOverlayBarHitCount ? g_ImGuiOverlayBarHitCount - 1 : 0;
	}
}

static void imguiOverlayBarActivate(const struct imguiOverlayBarHit *hit)
{
	switch (hit->kind) {
	case kFojoBarWindow:
		if (*g_ImGuiOverlayWindowDefs[hit->index].open) {
			// focusing an open window takes you to its floor; opening one
			// brings it to yours
			const struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[hit->index];
			if (!rt.sticky) {
				g_ImGuiOverlayWorkspace = rt.ws;
			}
			ImGui::SetWindowFocus(g_ImGuiOverlayWindowDefs[hit->index].title);
		} else {
			*g_ImGuiOverlayWindowDefs[hit->index].open = true;
			imguiOverlayBringToCurrentWorkspace(hit->index);
		}
		break;
	case kFojoBarEntity:
		// this one was already written: latch means "focus it in every panel"
		if (hit->prop && imguiOverlayPropIsCurrent(hit->prop)) {
			imguiOverlayLatchPropEverywhere(hit->prop);
		}
		break;
	case kFojoBarCommand:
		g_ImGuiOverlayCommandDefs[hit->index].run();
		break;
	case kFojoBarStage:
		// the Stage panel shows the running stage; there is no row to scroll
		// to yet, so this opens the panel and leaves the path in the log
		if (hit->ref.drive == ASSET_DRIVE_STAGE) {
			char path[64];
			assetFormat(&hit->ref, path, sizeof(path));
			sysLogPrintf(LOG_NOTE, "IMGUI: bar -> %s", path);
			g_ImGuiOverlayShowStage = true;
			imguiOverlayBringFlagToCurrentWorkspace(&g_ImGuiOverlayShowStage);
		}
		break;
	case kFojoBarTexture:
		// the probe wants the engine-side number: the texnum for vanilla, the
		// assigned port id for a mod texture - which is what ref.sub carries
		if (hit->ref.drive == ASSET_DRIVE_TEX) {
			imguiOverlayFocusTextureId(hit->ref.sub);
			imguiOverlayBringFlagToCurrentWorkspace(&g_ImGuiOverlayShowTextures);
		}
		break;
	case kFojoBarFileSlot:
		// open Assets on the right mod with the slot table already filtered to
		// the name, which is where you were going to end up anyway. A vanilla
		// file is in every mod's row, so the selected mod is left alone.
		if (hit->ref.drive == ASSET_DRIVE_FILE) {
			if (hit->ref.owner >= 0) {
				g_ImGuiOverlaySlotMod = hit->ref.owner;
			}
			snprintf(g_ImGuiOverlaySlotFilter.InputBuf,
					sizeof(g_ImGuiOverlaySlotFilter.InputBuf), "%s", hit->label);
			g_ImGuiOverlaySlotFilter.Build();
			g_ImGuiOverlayShowAssets = true;
			imguiOverlayBringFlagToCurrentWorkspace(&g_ImGuiOverlayShowAssets);
		}
		break;
	}

	g_ImGuiOverlayBarOpen = false;
}

static void imguiOverlayDrawAwesomeBar(void)
{
	if (!g_ImGuiOverlayBarOpen) {
		return;
	}

	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float width = vp->WorkSize.x * 0.5f < 420.0f ? 420.0f : vp->WorkSize.x * 0.5f;

	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - width) * 0.5f,
			vp->WorkPos.y + vp->WorkSize.y * 0.18f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowFocus();

	if (!ImGui::Begin("Fojo Find", &g_ImGuiOverlayBarOpen,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	if (g_ImGuiOverlayBarJustOpened) {
		ImGui::SetKeyboardFocusHere();
		g_ImGuiOverlayBarJustOpened = false;
	}

	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##fojofind", "find a window, an entity, a command, #a file slot",
			g_ImGuiOverlayBarQuery, sizeof(g_ImGuiOverlayBarQuery));

	imguiOverlayBarSearch();

	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && g_ImGuiOverlayBarHitCount) {
		g_ImGuiOverlayBarSel = (g_ImGuiOverlayBarSel + 1) % g_ImGuiOverlayBarHitCount;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) && g_ImGuiOverlayBarHitCount) {
		g_ImGuiOverlayBarSel = (g_ImGuiOverlayBarSel + g_ImGuiOverlayBarHitCount - 1)
			% g_ImGuiOverlayBarHitCount;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		g_ImGuiOverlayBarOpen = false;
	}

	const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false)
		|| ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

	ImGui::Separator();

	if (!g_ImGuiOverlayBarHitCount) {
		ImGui::TextDisabled("no match");
	}

	for (s32 i = 0; i < g_ImGuiOverlayBarHitCount; ++i) {
		const struct imguiOverlayBarHit *hit = &g_ImGuiOverlayBarHits[i];
		char row[128];

		snprintf(row, sizeof(row), "%s##fojohit%d", hit->label, i);

		if (ImGui::Selectable(row, i == g_ImGuiOverlayBarSel)) {
			imguiOverlayBarActivate(hit);
			break;
		}

		if (hit->detail[0]) {
			ImGui::SameLine();
			ImGui::TextDisabled("  %s", hit->detail);
		}

		if (i == g_ImGuiOverlayBarSel && (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)
				|| ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))) {
			ImGui::SetScrollHereY(0.5f);
		}
	}

	if (enter && g_ImGuiOverlayBarHitCount
			&& g_ImGuiOverlayBarSel < g_ImGuiOverlayBarHitCount) {
		imguiOverlayBarActivate(&g_ImGuiOverlayBarHits[g_ImGuiOverlayBarSel]);
	}

	ImGui::End();
}

static void imguiOverlayOpenAwesomeBar(void)
{
	g_ImGuiOverlayBarOpen = true;
	g_ImGuiOverlayBarJustOpened = true;
	g_ImGuiOverlayBarSel = 0;
	g_ImGuiOverlayBarQuery[0] = '\0';
}

static void imguiOverlayDrawWindowMenu(void)
{
	if (!ImGui::BeginPopupContextVoid("FojoWindowMenu", ImGuiPopupFlags_MouseButtonRight)) {
		return;
	}

	ImGui::SeparatorText("Floor");

	for (s32 ws = 0; ws < kFojoWorkspaces; ++ws) {
		char label[24];
		snprintf(label, sizeof(label), "%d", ws + 1);

		if (ws) {
			ImGui::SameLine();
		}

		if (ImGui::RadioButton(label, g_ImGuiOverlayWorkspace == ws)) {
			g_ImGuiOverlayWorkspace = ws;
		}
	}

	ImGui::SeparatorText("Fojo Windows");

	for (s32 i = 0; i < kFojoWindowCount; ++i) {
		const struct imguiOverlayWindowDef *def = &g_ImGuiOverlayWindowDefs[i];
		const struct imguiOverlayWindowRt &rt = g_ImGuiOverlayWindowRt[i];
		const bool wasOpen = *def->open;
		char label[64];

		// say where it is, so a window that is open but on another floor does
		// not read as broken
		if (rt.sticky) {
			snprintf(label, sizeof(label), "%s\t*", imguiOverlayWindowLabel(def));
		} else {
			snprintf(label, sizeof(label), "%s\t%d", imguiOverlayWindowLabel(def), rt.ws + 1);
		}

		ImGui::MenuItem(label, NULL, def->open, def->gate == NULL || def->gate());

		if (!wasOpen && *def->open) {
			imguiOverlayBringToCurrentWorkspace(i);
		}
	}

	ImGui::EndPopup();
}

void imguiOverlayInit(void *window)
{
	if (g_ImGuiOverlayInitialized || !window) {
		return;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	snprintf(g_ImGuiOverlayIniPath, sizeof(g_ImGuiOverlayIniPath), "%s/fojo-imgui.ini", fsGetSaveDir());
	io.IniFilename = g_ImGuiOverlayIniPath;
	ImGuiSettingsHandler settingsHandler;
	settingsHandler.TypeName = "Fojo";
	settingsHandler.TypeHash = ImHashStr(settingsHandler.TypeName);
	settingsHandler.ReadOpenFn = imguiOverlaySettingsReadOpen;
	settingsHandler.ReadLineFn = imguiOverlaySettingsReadLine;
	settingsHandler.WriteAllFn = imguiOverlaySettingsWriteAll;
	ImGui::AddSettingsHandler(&settingsHandler);

	ImGui::StyleColorsDark();
	ImGui_ImplSDL2_InitForOpenGL((SDL_Window *)window, SDL_GL_GetCurrentContext());
	ImGui_ImplOpenGL3_Init("#version 150");
	g_ImGuiOverlayInitialized = true;
	sysLogPrintf(LOG_NOTE, "IMGUI: single-window overlay initialized; layout=%s", g_ImGuiOverlayIniPath);
}

void imguiOverlayShutdown(void)
{
	if (!g_ImGuiOverlayInitialized) {
		return;
	}

	gfx_submit_debug_texture_gdl(NULL);
	if (g_ImGuiOverlayTextureProbeData) {
		gfx_forget_debug_texture_data(g_ImGuiOverlayTextureProbeData);
		g_ImGuiOverlayTextureProbeData = NULL;
	}
	modeldefEditorWorkspaceUnload();
	imguiOverlayClearTexturePreview();
	imguiOverlayClearRenderedPixels();
	ImGui::SaveIniSettingsToDisk(g_ImGuiOverlayIniPath);
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	g_ImGuiOverlayInitialized = false;
}

void imguiOverlayProcessEvent(const SDL_Event *event)
{
	if (!g_ImGuiOverlayInitialized || !event) {
		return;
	}

	ImGui_ImplSDL2_ProcessEvent(event);
	if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12
			&& event->key.repeat == 0) {
		imguiOverlaySetVisible(!g_ImGuiOverlayVisible);
	}

	// Ctrl+P has to be caught HERE as well as in the frame, or it only works
	// once the overlay is already up: imguiOverlayCapturesKeyboard returns false
	// while hidden, so input.c hands the keyboard to the game and ImGui's own
	// Shortcut() never sees it. One keystroke from gameplay should enter
	// debugger mode AND focus the bar. Super is bound too and will usually be
	// eaten by the window manager on Linux before SDL ever sees it.
	if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_p
			&& event->key.repeat == 0
			&& (event->key.keysym.mod & (KMOD_CTRL | KMOD_GUI))) {
		if (!g_ImGuiOverlayVisible) {
			imguiOverlaySetVisible(true);
		}

		imguiOverlayOpenAwesomeBar();
	}
}

void imguiOverlayStartFrame(void)
{
	if (!g_ImGuiOverlayInitialized) {
		return;
	}

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
}

void imguiOverlayRender(void)
{
	if (!g_ImGuiOverlayInitialized) {
		return;
	}

	if (g_ImGuiOverlayVisible) {
		const u32 previousWindowState = imguiOverlayGetWindowState();
		const u32 previousGeometry = imguiOverlayGeometryFingerprint();
		g_ImGuiOverlayExpandLatch = false;
		if (!imguiOverlayPropIsCurrent(g_ImGuiOverlayFocusProp)) {
			g_ImGuiOverlayFocusProp = NULL;
		}
		imguiOverlayApplyWindowGates();

		// Ctrl+1..4 switches floor. RouteGlobal so it works wherever focus is,
		// including inside a panel.
		for (s32 ws = 0; ws < kFojoWorkspaces; ++ws) {
			if (ImGui::Shortcut(ImGuiMod_Ctrl | (ImGuiKey)(ImGuiKey_1 + ws),
					ImGuiInputFlags_RouteGlobal)) {
				g_ImGuiOverlayWorkspace = ws;
			}
		}

		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)
				|| ImGui::Shortcut(ImGuiMod_Super | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
			imguiOverlayOpenAwesomeBar();
		}

		imguiOverlayDrawWindowMenu();
		imguiOverlayDrawWindowContextMenu();

		{
			const ImGuiIO &io = ImGui::GetIO();
			if (io.KeyCtrl) {
				// show where the picker thinks everything is while ctrl is held
				imguiOverlayPickShape shapes[256];
				const s32 n = imguiOverlayCollectPickShapes(shapes, 256);
				ImDrawList *fg = ImGui::GetForegroundDrawList();
				for (s32 i = 0; i < n; i++) {
					const imguiOverlayPickShape &sh = shapes[i];
					const bool ischr = sh.prop->type == PROPTYPE_CHR || sh.prop->type == PROPTYPE_PLAYER;
					const ImU32 col = ischr ? IM_COL32(79, 216, 255, 220) : IM_COL32(255, 210, 79, 200);
					if (ischr) {
						fg->AddLine(sh.a, sh.b, col, 2.0f);
						fg->AddCircle(sh.b, 6.0f, col);
					} else {
						fg->AddCircle(sh.a, sh.radius, col, 0, 1.0f);
					}
					char label[32];
					if (ischr && sh.prop->chr) snprintf(label, sizeof(label), "chr %d", sh.prop->chr->chrnum);
					else snprintf(label, sizeof(label), "%s", imguiOverlayPropTypeName(sh.prop->type));
					fg->AddText(ImVec2(sh.b.x + 8.0f, sh.b.y - 8.0f), IM_COL32(255, 255, 255, 255), label);
				}
			}
			if (!io.WantCaptureMouse && io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				struct prop *picked = imguiOverlayPickPropAtMouse(io.MousePos);
				if (picked) {
					imguiOverlayLatchPropEverywhere(picked);
				}
			}
		}

		// a latched prop always pulls Entities up, whatever the menu says
		if (g_ImGuiOverlayFocusProp) {
			g_ImGuiOverlayShowEntities = true;
		}

		for (s32 i = 0; i < kFojoWindowCount; ++i) {
			const struct imguiOverlayWindowDef *def = &g_ImGuiOverlayWindowDefs[i];
			if (!*def->open || !imguiOverlayWindowOnThisFloor(i)) {
				// closed, or living on a floor you are not standing on. either
				// way it is not submitted this frame, so the next time it is,
				// it is reappearing and gets its geometry put back
				g_ImGuiOverlayWindowRt[i].live = false;
				continue;
			}

			if (imguiOverlayBeginWindow(i)) {
				def->draw();
			}

			ImGui::End();
		}

		imguiOverlayDrawAwesomeBar();

		if (imguiOverlayGetWindowState() != previousWindowState) {
			// opening or closing a window is worth writing out now
			imguiOverlaySaveWindowState();
		} else if (imguiOverlayGeometryFingerprint() != previousGeometry) {
			// dragging one is not: just mark it dirty and let ImGui's own
			// IniSavingRate (5s) coalesce the write, or this would hit the
			// disk on every frame of a drag
			ImGui::MarkIniSettingsDirty();
		}
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	imguiOverlayRunLuaRequests();
	imguiOverlayRunSaveRequests();
}

bool imguiOverlayCapturesKeyboard(void)
{
	return g_ImGuiOverlayInitialized && g_ImGuiOverlayVisible;
}

bool imguiOverlayCapturesMouse(void)
{
	return g_ImGuiOverlayInitialized && g_ImGuiOverlayVisible;
}
