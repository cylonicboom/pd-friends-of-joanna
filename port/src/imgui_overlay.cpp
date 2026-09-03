#include <stdio.h>
#include <string.h>
#include <vector>

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
#include "input.h"
#include "mod.h"
#include "romdata.h"
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
static bool g_ImGuiOverlayShowLookingAt = false;
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
static bool g_ImGuiOverlayEngineProbeMetadataValid = false;
static u32 g_ImGuiOverlayEngineProbeCompressedSize = 0;
static u32 g_ImGuiOverlayEngineProbeDecodedSize = 0;
static u8 g_ImGuiOverlayEngineProbeHeader = 0;
static u8 g_ImGuiOverlayEngineProbeNativeFormat = 0xff;
static u8 g_ImGuiOverlayEngineProbeWidth = 0;
static u8 g_ImGuiOverlayEngineProbeHeight = 0;
static u8 g_ImGuiOverlayEngineProbeFormat = 0;
static u8 g_ImGuiOverlayEngineProbeDepth = 0;
static u8 g_ImGuiOverlayEngineProbeLutMode = 0;
static u8 g_ImGuiOverlayEngineProbePaletteCount = 0;
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
extern struct stagetableentry g_Stages[87];
extern "C" u32 mempGetStageFree(void);
extern "C" bool bgTestHitInRoom(struct coord *frompos, struct coord *topos, s32 roomnum, struct hitthing *hitthing);
extern "C" struct prop *propFindAimingAt(s32 handnum, bool isshooting, u32 context);
extern "C" void portal00018148(struct coord *pos, struct coord *pos2, RoomNum *rooms, RoomNum *arg3, RoomNum *arg4, s32 arg5);
extern "C" void texInitPool(struct texpool *pool, u8 *start, s32 len);
extern "C" void texLoad(texnum_t *updateword, struct texpool *pool, bool unusedarg);
extern "C" struct tex *texFindInPool(s32 texturenum, struct texpool *pool);
extern "C" Gfx *texBuildDebugLoadGdl(Gfx *gdl, struct tex *tex);

static void *imguiOverlaySettingsReadOpen(ImGuiContext *, ImGuiSettingsHandler *handler, const char *name)
{
	return strcmp(name, "Windows") == 0 ? handler : NULL;
}

static void imguiOverlaySettingsReadLine(ImGuiContext *, ImGuiSettingsHandler *, void *, const char *line)
{
	int value;

	if (sscanf(line, "Runtime=%d", &value) == 1) { g_ImGuiOverlayShowRuntime = value != 0; return; }
	if (sscanf(line, "Stage=%d", &value) == 1) { g_ImGuiOverlayShowStage = value != 0; return; }
	if (sscanf(line, "Entities=%d", &value) == 1) { g_ImGuiOverlayShowEntities = value != 0; return; }
	if (sscanf(line, "Assets=%d", &value) == 1) { g_ImGuiOverlayShowAssets = value != 0; return; }
	if (sscanf(line, "Textures=%d", &value) == 1) { g_ImGuiOverlayShowTextures = value != 0; return; }
	if (sscanf(line, "Memory=%d", &value) == 1) { g_ImGuiOverlayShowMemory = value != 0; return; }
	if (sscanf(line, "Profiler=%d", &value) == 1) { g_ImGuiOverlayShowProfiler = value != 0; return; }
	if (sscanf(line, "LookingAt=%d", &value) == 1) { g_ImGuiOverlayShowLookingAt = value != 0; }
}

static void imguiOverlaySettingsWriteAll(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buffer)
{
	buffer->appendf("[%s][Windows]\n", handler->TypeName);
	buffer->appendf("Runtime=%d\n", g_ImGuiOverlayShowRuntime);
	buffer->appendf("Stage=%d\n", g_ImGuiOverlayShowStage);
	buffer->appendf("Entities=%d\n", g_ImGuiOverlayShowEntities);
	buffer->appendf("Assets=%d\n", g_ImGuiOverlayShowAssets);
	buffer->appendf("Textures=%d\n", g_ImGuiOverlayShowTextures);
	buffer->appendf("Memory=%d\n", g_ImGuiOverlayShowMemory);
	buffer->appendf("Profiler=%d\n", g_ImGuiOverlayShowProfiler);
	buffer->appendf("LookingAt=%d\n\n", g_ImGuiOverlayShowLookingAt);
}

static u32 imguiOverlayGetWindowState(void)
{
	return (g_ImGuiOverlayShowRuntime ? 1u << 0 : 0)
		| (g_ImGuiOverlayShowStage ? 1u << 1 : 0)
		| (g_ImGuiOverlayShowEntities ? 1u << 2 : 0)
		| (g_ImGuiOverlayShowAssets ? 1u << 3 : 0)
		| (g_ImGuiOverlayShowTextures ? 1u << 4 : 0)
		| (g_ImGuiOverlayShowMemory ? 1u << 5 : 0)
		| (g_ImGuiOverlayShowProfiler ? 1u << 6 : 0)
		| (g_ImGuiOverlayShowLookingAt ? 1u << 7 : 0);
}

static void imguiOverlaySaveWindowState(void)
{
	ImGui::MarkIniSettingsDirty();
	ImGui::SaveIniSettingsToDisk(g_ImGuiOverlayIniPath);
}

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
	ImGui::Text("Head: 0x%02x %s", (u8)chr->headnum, imguiOverlayHeadBodyName((u8)chr->headnum));
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

	snprintf(text, sizeof(text), "slot:%d chr:%04x %p body:%04x %s head:%02x %s team:%02x tude:%02x action:%s actionid:%02x damage:%.3f shield:%.3f",
			index, (u16)chr->chrnum, chr, (u16)chr->bodynum, imguiOverlayHeadBodyName(chr->bodynum),
			(u8)chr->headnum, imguiOverlayHeadBodyName((u8)chr->headnum),
			chr->team, chr->tude, imguiOverlayActionName(chr->actiontype),
			(u8)chr->actiontype, chr->damage, chr->cshield);

	return g_ImGuiOverlayChrTextFilter.PassFilter(text);
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

	if (ImGui::TreeNode(prop, "%s %d (%p)", imguiOverlayPropTypeName(prop->type), index, prop)) {
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
	if (g_StageIndex >= 0 && g_StageIndex < 87) {
		const char *setupName = romdataFileGetName(g_Stages[g_StageIndex].setupfileid);
		ImGui::Text("Setup: %s", setupName ? setupName : "unregistered");
	}
	ImGui::Text("Rooms: %d", g_Vars.roomcount);

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
				if (ImGui::TreeNode(chr, "Slot %d: 0x%04x (%p)", index, (u16)chr->chrnum, chr)) {
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

static void imguiOverlayScanModelTextureIds(s32 textureMod, s32 modelFileNum)
{
	struct modeldefTextureUsage usages[512];
	s32 total = 0;
	const s32 encodedFileNum = modelFileNum | (textureMod << 16);
	const s32 usageCount = modeldefInspectTextureUsage(encodedFileNum, 0xffff, 0xffff,
		usages, ARRAYCOUNT(usages), &total, NULL, 0, NULL, NULL);

	g_ImGuiOverlayModelTextureIdCount = 0;
	g_ImGuiOverlayModelTextureIdTotal = total;
	g_ImGuiOverlayScannedTextureModelMod = textureMod;
	g_ImGuiOverlayScannedTextureModelFileNum = modelFileNum;
	for (s32 usageIndex = 0; usageIndex < usageCount; ++usageIndex) {
		const u16 textureId = usages[usageIndex].textureid;
		bool duplicate = false;
		for (s32 idIndex = 0; idIndex < g_ImGuiOverlayModelTextureIdCount; ++idIndex) {
			if (g_ImGuiOverlayModelTextureIds[idIndex] == textureId) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate && g_ImGuiOverlayModelTextureIdCount < ARRAYCOUNT(g_ImGuiOverlayModelTextureIds)) {
			g_ImGuiOverlayModelTextureIds[g_ImGuiOverlayModelTextureIdCount++] = textureId;
		}
	}
}

static void imguiOverlayDrawModelTextureFiles(s32 textureMod, s32 modelFileNum, const char *textureDirName)
{
	if (textureMod < 0 || textureMod >= (s32)g_NumModDirs || modelFileNum <= 0 || !textureDirName) {
		return;
	}

	ImGui::SeparatorText("Model Texture Files");
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
				} else {
					ImGui::TextDisabled("missing");
				}
				ImGui::TableSetColumnIndex(4);
				if (hasFile) ImGui::Text("%u", slotInfo.size); else ImGui::TextDisabled("-");
				ImGui::TableSetColumnIndex(5);
				if (hasExtTex) {
					ImGui::Text("%d", owner);
				} else {
					ImGui::TextDisabled("no");
				}
				ImGui::TableSetColumnIndex(6);
				if (hasDimensions) {
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
	ImGui::TextDisabled("GDL bindings: %d unique from %d references", g_ImGuiOverlayModelTextureIdCount,
		g_ImGuiOverlayModelTextureIdTotal);
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

static void imguiOverlayClearRenderedPixels(void)
{
	g_ImGuiOverlayRenderedTexturePixels.clear();
	g_ImGuiOverlayRenderedPixelsModelFileNum = -1;
	g_ImGuiOverlayRenderedPixelsTexId = -1;
	g_ImGuiOverlayRenderedPixelsWidth = 0;
	g_ImGuiOverlayRenderedPixelsHeight = 0;
	g_ImGuiOverlayTextureCompareValid = false;
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
	return true;
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
	u32 compressedSize = 0;
	const s32 encodedFileNum = textureFileNum | (textureMod << 16);
	u8 *compressedData = textureFileNum > 0 ? romdataFileLoad(encodedFileNum, &compressedSize) : NULL;
	const u8 header = compressedData && compressedSize > 0 ? compressedData[0] : 0;
	const u8 nativeFormat = compressedData && compressedSize > 1
		? ((header & 0x40) ? compressedData[1] : compressedData[1] >> 4) : 0xff;
	if (compressedData) {
		romdataFileFree(encodedFileNum);
	}
	texInitPool(&g_ImGuiOverlayTextureProbePool, g_ImGuiOverlayTextureProbePoolData,
		sizeof(g_ImGuiOverlayTextureProbePoolData));
	texnum_t updateword = textureId;
	texLoad(&updateword, &g_ImGuiOverlayTextureProbePool, true);
	struct tex *tex = texFindInPool(textureId, &g_ImGuiOverlayTextureProbePool);
	if (tex) {
		g_ImGuiOverlayTextureProbeData = tex->data;
		texBuildDebugLoadGdl(g_ImGuiOverlayTextureProbeGdl, tex);
		gfx_submit_debug_texture_gdl(g_ImGuiOverlayTextureProbeGdl);
		g_ImGuiOverlayEngineProbeModelFileNum = modelFileNum;
		g_ImGuiOverlayEngineProbeTexId = textureId;
		g_ImGuiOverlayEngineProbeCompressedSize = compressedSize;
		g_ImGuiOverlayEngineProbeDecodedSize = g_ImGuiOverlayTextureProbePool.leftpos - tex->data;
		g_ImGuiOverlayEngineProbeHeader = header;
		g_ImGuiOverlayEngineProbeNativeFormat = nativeFormat;
		g_ImGuiOverlayEngineProbeWidth = tex->width;
		g_ImGuiOverlayEngineProbeHeight = tex->height;
		g_ImGuiOverlayEngineProbeFormat = tex->gbiformat;
		g_ImGuiOverlayEngineProbeDepth = tex->depth;
		g_ImGuiOverlayEngineProbeLutMode = tex->lutmodeindex;
		g_ImGuiOverlayEngineProbePaletteCount = tex->lutmodeindex ? tex->numcolors + 1 : 0;
		g_ImGuiOverlayEngineProbeLodCount = tex->numlods;
		g_ImGuiOverlayEngineProbeHasLodData = tex->hasloddata;
		g_ImGuiOverlayEngineProbeMetadataValid = true;
	}
	g_TexCurrentModelFileNum = previousModelFileNum;
	g_TexModNum = previousMod;
	return tex != NULL;
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
	const s32 encodedFileNum = modelFileNum | (textureMod << 16);
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
	ImGui::BeginDisabled(!hasTextureFile);
	if (ImGui::Button("Load through engine")) {
		if (imguiOverlayRequestEngineTexture(textureMod, modelFileNum, textureFileNum, engineTexId)) {
			imguiOverlayScanTextureUsage(textureMod, modelFileNum, localTexId, portTexId);
		}
	}
	ImGui::EndDisabled();
	if (!hasTextureFile && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("No model-scoped .bin file is registered for this texture ID.");
	}
	if (g_ImGuiOverlayEngineProbeModelFileNum == modelFileNum
			&& g_ImGuiOverlayEngineProbeTexId == engineTexId) {
		ImGui::SameLine();
		ImGui::TextDisabled("private engine probe active");
	}
	if (g_ImGuiOverlayEngineProbeMetadataValid
			&& g_ImGuiOverlayEngineProbeModelFileNum == modelFileNum
			&& g_ImGuiOverlayEngineProbeTexId == engineTexId) {
		ImGui::Text("Native header: 0x%02x, %s, header LOD count %u",
			g_ImGuiOverlayEngineProbeHeader,
			(g_ImGuiOverlayEngineProbeHeader & 0x40) ? "zlib" : "native compression",
			g_ImGuiOverlayEngineProbeHeader & 0x3f);
		ImGui::Text("Decoded: %ux%u %s (GBI format %u, depth %u)",
			g_ImGuiOverlayEngineProbeWidth, g_ImGuiOverlayEngineProbeHeight,
			imguiOverlayTextureFormatName(g_ImGuiOverlayEngineProbeNativeFormat),
			g_ImGuiOverlayEngineProbeFormat, g_ImGuiOverlayEngineProbeDepth);
		ImGui::Text("PD format code: 0x%02x", g_ImGuiOverlayEngineProbeNativeFormat);
		ImGui::Text("LOD: %u, embedded LOD data: %s; palette: %u entries, LUT mode %u",
			g_ImGuiOverlayEngineProbeLodCount,
			g_ImGuiOverlayEngineProbeHasLodData ? "yes" : "no",
			g_ImGuiOverlayEngineProbePaletteCount, g_ImGuiOverlayEngineProbeLutMode);
		ImGui::Text("Bytes: compressed %u, decoded pool payload %u",
			g_ImGuiOverlayEngineProbeCompressedSize, g_ImGuiOverlayEngineProbeDecodedSize);
	}

	if (!imguiOverlayFindRenderedTexture(modelFileNum, localTexId, portTexId, &info)) {
		ImGui::TextDisabled("Not imported this frame. Load it above or keep the model visible.");
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
	if (ImGui::Button("Capture rendered pixels")) {
		imguiOverlayCaptureRenderedPixels(&info, width, height);
	}
	if (!g_ImGuiOverlayRenderedTexturePixels.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("snapshot captured");
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
		if (ImGui::Button("Compare source and rendered")) {
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
		g_ImGuiOverlayEngineProbeModelFileNum = -1;
		g_ImGuiOverlayEngineProbeTexId = -1;
		g_ImGuiOverlayTextureProbeId = 0;
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
		ImGui::Text("native texture file: %s", hasTextureFile ? "yes" : "no");
		if (textureFileNum > 0) {
			ImGui::SameLine();
			ImGui::Text("file 0x%04x", textureFileNum);
			ImGui::Text("resolved path: %s", resolvedTextureName);
			ImGui::Text("engine ID 0x%04x -> file local ID 0x%04x", engineTexId, resolvedLocalId);
		}
		ImGui::Text("ext_tex model entry: %s", hasModelExtTex ? "yes" : "no");
		ImGui::Text("ext_tex owner mod: %d", owner);
		if (hasDimensions) {
			ImGui::Text("ext_tex dimensions: %ux%u", width, height);
		} else {
			ImGui::TextUnformatted("ext_tex dimensions: unavailable");
		}
	} else if (modelFileNum > 0) {
		ImGui::TextUnformatted("native texture file: enter texture ID");
		ImGui::TextUnformatted("ext_tex model entry: enter texture ID");
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

static void imguiOverlayDrawWindowMenu(bool canOpenLookingAt)
{
	if (!ImGui::BeginPopupContextVoid("FojoWindowMenu", ImGuiPopupFlags_MouseButtonRight)) {
		return;
	}

	ImGui::SeparatorText("Fojo Windows");
	ImGui::MenuItem("Runtime", NULL, &g_ImGuiOverlayShowRuntime);
	ImGui::MenuItem("Stage", NULL, &g_ImGuiOverlayShowStage);
	ImGui::MenuItem("Entities", NULL, &g_ImGuiOverlayShowEntities);
	ImGui::MenuItem("Assets", NULL, &g_ImGuiOverlayShowAssets);
	ImGui::MenuItem("Textures", NULL, &g_ImGuiOverlayShowTextures);
	ImGui::MenuItem("Memory", NULL, &g_ImGuiOverlayShowMemory);
	ImGui::MenuItem("Profiler", NULL, &g_ImGuiOverlayShowProfiler);
	ImGui::MenuItem("Looking At", NULL, &g_ImGuiOverlayShowLookingAt, canOpenLookingAt);
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

	imguiOverlayClearTexturePreview();
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
		g_ImGuiOverlayExpandLatch = false;
		if (!imguiOverlayPropIsCurrent(g_ImGuiOverlayFocusProp)) {
			g_ImGuiOverlayFocusProp = NULL;
		}
		const bool canOpenLookingAt = imguiOverlayCanAimInspect();
		if (!canOpenLookingAt) {
			g_ImGuiOverlayShowLookingAt = false;
		}
		imguiOverlayDrawWindowMenu(canOpenLookingAt);

		if (g_ImGuiOverlayShowRuntime) {
			imguiOverlaySetNextWindowDefaults(ImVec2(360.0f, 300.0f), 0.0f, 0.0f);
			if (ImGui::Begin("Fojo Runtime", &g_ImGuiOverlayShowRuntime)) {
				imguiOverlayDrawRuntimePanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowMemory) {
			imguiOverlaySetNextWindowDefaults(ImVec2(360.0f, 300.0f), 0.0f, 1.0f);
			if (ImGui::Begin("Fojo Memory", &g_ImGuiOverlayShowMemory)) {
				imguiOverlayDrawMemoryPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowProfiler) {
			imguiOverlaySetNextWindowDefaults(ImVec2(620.0f, 360.0f), 0.5f, 1.0f);
			if (ImGui::Begin("Fojo Profiler", &g_ImGuiOverlayShowProfiler)) {
				imguiOverlayDrawProfilerPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowStage) {
			imguiOverlaySetNextWindowDefaults(ImVec2(480.0f, 560.0f), 1.0f, 0.0f);
			if (ImGui::Begin("Fojo Stage", &g_ImGuiOverlayShowStage)) {
				imguiOverlayDrawStagePanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayFocusProp) {
			g_ImGuiOverlayShowEntities = true;
		}
		if (g_ImGuiOverlayShowEntities) {
			imguiOverlaySetNextWindowDefaults(ImVec2(520.0f, 620.0f), 1.0f, 0.0f);
			if (ImGui::Begin("Fojo Entities", &g_ImGuiOverlayShowEntities)) {
				imguiOverlayDrawEntitiesPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowAssets) {
			imguiOverlaySetNextWindowDefaults(ImVec2(620.0f, 560.0f), 0.5f, 0.2f);
			if (ImGui::Begin("Fojo Assets", &g_ImGuiOverlayShowAssets)) {
				imguiOverlayDrawAssetsPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowLookingAt) {
			imguiOverlaySetNextWindowDefaults(ImVec2(430.0f, 340.0f), 1.0f, 0.0f);
			if (ImGui::Begin("Fojo Looking At", &g_ImGuiOverlayShowLookingAt)) {
				imguiOverlayDrawLookingAtPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowTextures) {
			imguiOverlaySetNextWindowDefaults(ImVec2(520.0f, 620.0f), 1.0f, 0.25f);
			if (ImGui::Begin("Fojo Textures", &g_ImGuiOverlayShowTextures)) {
				imguiOverlayDrawTexturesPanel();
			}
			ImGui::End();
		}

		if (imguiOverlayGetWindowState() != previousWindowState) {
			imguiOverlaySaveWindowState();
		}
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool imguiOverlayCapturesKeyboard(void)
{
	return g_ImGuiOverlayInitialized && g_ImGuiOverlayVisible;
}

bool imguiOverlayCapturesMouse(void)
{
	return g_ImGuiOverlayInitialized && g_ImGuiOverlayVisible;
}
