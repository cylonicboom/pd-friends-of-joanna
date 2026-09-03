#include <stdio.h>
#include <string.h>

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
#include "imgui_overlay.h"
#include "input.h"
#include "mod.h"
#include "romdata.h"
#include "system.h"
#include "lib/profile.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

static bool g_ImGuiOverlayInitialized = false;
static bool g_ImGuiOverlayVisible = false;
static bool g_ImGuiOverlayRestoreMouseLock = false;
static bool g_ImGuiOverlayShowRuntime = true;
static bool g_ImGuiOverlayShowStage = true;
static bool g_ImGuiOverlayShowEntities = true;
static bool g_ImGuiOverlayShowAssets = true;
static bool g_ImGuiOverlayShowMemory = true;
static bool g_ImGuiOverlayShowProfiler = true;
static bool g_ImGuiOverlayShowTextures = true;
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
static ImGuiTextFilter g_ImGuiOverlayPropTextFilter;
static ImGuiTextFilter g_ImGuiOverlayChrTextFilter;
static ImGuiTextFilter g_ImGuiOverlaySlotFilter;
static ImGuiTextFilter g_ImGuiOverlayModelFilter;

extern s32 g_StageNum;
extern s32 g_ModNum;
extern u32 g_OsMemSize;
extern s32 g_StageIndex;
extern struct stagetableentry g_Stages[87];
extern "C" u32 mempGetStageFree(void);
extern "C" bool bgTestHitInRoom(struct coord *frompos, struct coord *topos, s32 roomnum, struct hitthing *hitthing);
extern "C" struct prop *propFindAimingAt(s32 handnum, bool isshooting, u32 context);
extern "C" void portal00018148(struct coord *pos, struct coord *pos2, RoomNum *rooms, RoomNum *arg3, RoomNum *arg4, s32 arg5);

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

static bool imguiOverlayParseModelTextureId(const char *slotName, const char *textureDirName, u16 *localTexId)
{
	char *end = NULL;
	char prefix[96];
	s32 prefixLen;
	u32 value;

	if (!slotName || !textureDirName || !localTexId) {
		return false;
	}

	snprintf(prefix, sizeof(prefix), "%s/", textureDirName);
	prefixLen = strlen(prefix);
	if (strncmp(slotName, prefix, prefixLen) != 0) {
		return false;
	}

	value = strtoul(slotName + prefixLen, &end, 16);
	if (!end || strcmp(end, ".bin") != 0 || value > 0xffff) {
		return false;
	}

	*localTexId = (u16)value;
	return true;
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

			for (s32 fileNum = 1; fileNum < 8192; ++fileNum) {
				struct romdatafileslotinfo slotInfo;
				u16 localTexId = 0;
				if (!romdataGetFileSlotInfo(textureMod, fileNum, &slotInfo)
						|| !imguiOverlayParseModelTextureId(slotInfo.name, textureDirName, &localTexId)) {
					continue;
				}

				const u16 portTexId = modTexMapLookup(textureMod, localTexId);
				const bool mapped = portTexId != localTexId;
				const bool hasExtTex = extTexModelHasEntryForTexid((s16)modelFileNum, localTexId);
				const s8 owner = extTexGetOwnerMod(1, (u16)modelFileNum, localTexId);
				u16 width = 0;
				u16 height = 0;
				const u8 hasDimensions = extTexGetDimensions(1, (u16)modelFileNum, localTexId, &width, &height);

				ImGui::PushID(fileNum);
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
				ImGui::Text("%04x", fileNum);
				ImGui::TableSetColumnIndex(3);
				const s32 displayedSource = slotInfo.source == 0 ? slotInfo.configuredSource : slotInfo.source;
				ImGui::TextUnformatted(imguiOverlayFileSourceName(displayedSource));
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", slotInfo.size);
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
			ImGui::TableSetupColumn("tex files", ImGuiTableColumnFlags_WidthFixed, 68.0f);
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
		char textureFileName[128];
		const s32 textureFileNum = textureDirName
			? (snprintf(textureFileName, sizeof(textureFileName), "%s/%04x.bin", textureDirName, localTexId),
				romdataFileGetNumForNameInMod(textureFileName, textureMod))
			: -1;
		const bool hasModelExtTex = extTexModelHasEntryForTexid((s16)modelFileNum, localTexId);
		const s8 owner = extTexGetOwnerMod(1, (u16)modelFileNum, localTexId);
		u16 width = 0;
		u16 height = 0;
		const u8 hasDimensions = extTexGetDimensions(1, (u16)modelFileNum, localTexId, &width, &height);
		ImGui::Text("model texture file: %s", textureFileNum > 0 ? "yes" : "no");
		if (textureFileNum > 0) {
			ImGui::SameLine();
			ImGui::Text("file 0x%04x", textureFileNum);
		}
		ImGui::Text("ext_tex model entry: %s", hasModelExtTex ? "yes" : "no");
		ImGui::Text("ext_tex owner mod: %d", owner);
		if (hasDimensions) {
			ImGui::Text("ext_tex dimensions: %ux%u", width, height);
		} else {
			ImGui::TextUnformatted("ext_tex dimensions: unavailable");
		}
	} else if (modelFileNum > 0) {
		ImGui::TextUnformatted("model texture file: enter texture ID");
		ImGui::TextUnformatted("ext_tex model entry: enter texture ID");
	}

	imguiOverlayDrawModelTextureFiles(textureMod, modelFileNum, textureDirName);

	ImGui::SeparatorText("Authoring Checks");
	ImGui::BulletText("Use GDL runtime texture IDs for textureId, .bin names, and texMap keys.");
	ImGui::BulletText("Do not use texconfig ptr_raw except when debugging ROM texture-bank layout.");
	ImGui::BulletText("Prefer per-model texture paths: textures/<ModelName>/<localTexId>.bin.");
	ImGui::BulletText("PNG/ext_tex metadata is visible, but PNG preview/loading support is still pending.");
	ImGui::BulletText("Pixel alignment debugging should start with nearest-neighbor preview and GL_UNPACK_ALIGNMENT=1.");

	ImGui::SeparatorText("Screenshot Alignment Plan");
	ImGui::TextWrapped("Next steps: decode and preview the probed texture, show hover pixel coordinates/RGBA, then compare against screenshot crops with diagnostic X/Y offsets before changing decode or UV code.");
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
	} else if (g_ImGuiOverlayRestoreMouseLock) {
		inputLockMouse(1);
		g_ImGuiOverlayRestoreMouseLock = false;
	}
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
	io.IniFilename = NULL;

	ImGui::StyleColorsDark();
	ImGui_ImplSDL2_InitForOpenGL((SDL_Window *)window, SDL_GL_GetCurrentContext());
	ImGui_ImplOpenGL3_Init("#version 150");
	g_ImGuiOverlayInitialized = true;
	sysLogPrintf(LOG_NOTE, "IMGUI: single-window overlay initialized");
}

void imguiOverlayShutdown(void)
{
	if (!g_ImGuiOverlayInitialized) {
		return;
	}

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
		g_ImGuiOverlayExpandLatch = false;
		if (!imguiOverlayPropIsCurrent(g_ImGuiOverlayFocusProp)) {
			g_ImGuiOverlayFocusProp = NULL;
		}
		const bool canOpenLookingAt = imguiOverlayCanAimInspect();
		if (!canOpenLookingAt) {
			g_ImGuiOverlayShowLookingAt = false;
		}
		ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Fojo Debugger", &g_ImGuiOverlayVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Checkbox("Runtime", &g_ImGuiOverlayShowRuntime);
			ImGui::Checkbox("Stage", &g_ImGuiOverlayShowStage);
			ImGui::Checkbox("Entities", &g_ImGuiOverlayShowEntities);
			ImGui::Checkbox("Assets", &g_ImGuiOverlayShowAssets);
			ImGui::Checkbox("Textures", &g_ImGuiOverlayShowTextures);
			ImGui::Checkbox("Memory", &g_ImGuiOverlayShowMemory);
			ImGui::Checkbox("Profiler", &g_ImGuiOverlayShowProfiler);
			ImGui::BeginDisabled(!canOpenLookingAt);
			ImGui::Checkbox("Looking At", &g_ImGuiOverlayShowLookingAt);
			ImGui::EndDisabled();
			ImGui::Separator();
			ImGui::Text("F12 closes overlay");
		}
		ImGui::End();

		if (g_ImGuiOverlayShowRuntime) {
			ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Fojo Runtime", &g_ImGuiOverlayShowRuntime)) {
				imguiOverlayDrawRuntimePanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowMemory) {
			ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Fojo Memory", &g_ImGuiOverlayShowMemory)) {
				imguiOverlayDrawMemoryPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowProfiler) {
			ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Fojo Profiler", &g_ImGuiOverlayShowProfiler)) {
				imguiOverlayDrawProfilerPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowStage) {
			ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Fojo Stage", &g_ImGuiOverlayShowStage)) {
				imguiOverlayDrawStagePanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayFocusProp) {
			g_ImGuiOverlayShowEntities = true;
		}
		if (g_ImGuiOverlayShowEntities) {
			ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Fojo Entities", &g_ImGuiOverlayShowEntities)) {
				imguiOverlayDrawEntitiesPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowAssets) {
			ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Fojo Assets", &g_ImGuiOverlayShowAssets)) {
				imguiOverlayDrawAssetsPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowLookingAt) {
			ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Fojo Looking At", &g_ImGuiOverlayShowLookingAt)) {
				imguiOverlayDrawLookingAtPanel();
			}
			ImGui::End();
		}

		if (g_ImGuiOverlayShowTextures) {
			ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Fojo Textures", &g_ImGuiOverlayShowTextures)) {
				imguiOverlayDrawTexturesPanel();
			}
			ImGui::End();
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
