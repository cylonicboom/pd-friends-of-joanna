#include <stdio.h>

#include <SDL.h>
#include <PR/os_thread.h>

#include "../fast3d/glad/glad.h"
#include "../fast3d/gfx_pc.h"
#include "bss.h"
#include "data.h"
#undef bool
#undef true
#undef false
#include "fs.h"
#include "imgui_overlay.h"
#include "input.h"
#include "romdata.h"
#include "system.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

static bool g_ImGuiOverlayInitialized = false;
static bool g_ImGuiOverlayVisible = false;
static bool g_ImGuiOverlayRestoreMouseLock = false;
static bool g_ImGuiOverlayShowActivePropsOnly = true;
static bool g_ImGuiOverlayExpandLatch = false;
static bool g_ImGuiOverlayExpandValue = false;
static s32 g_ImGuiOverlaySlotMod = -1;
static s32 g_ImGuiOverlayPropFilter = 0;
static s32 g_ImGuiOverlayObjFilter = 0;
static ImGuiTextFilter g_ImGuiOverlayPropTextFilter;
static ImGuiTextFilter g_ImGuiOverlayChrTextFilter;
static ImGuiTextFilter g_ImGuiOverlaySlotFilter;

extern s32 g_StageNum;
extern s32 g_ModNum;
extern u32 g_OsMemSize;
extern s32 g_StageIndex;
extern struct stagetableentry g_Stages[87];
extern "C" u32 mempGetStageFree(void);

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

static void imguiOverlayDescribeChr(struct chrdata *chr)
{
	ImGui::Text("Number: 0x%04x", (u16)chr->chrnum);
	ImGui::Text("Body: 0x%04x", (u16)chr->bodynum);
	ImGui::Text("Head: 0x%02x", (u8)chr->headnum);
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
		if (chr->weapons_held[handIndex]) {
			const char *label = handIndex == HAND_RIGHT ? "Right hand prop"
				: handIndex == HAND_LEFT ? "Left hand prop" : "Hat prop";
			if (g_ImGuiOverlayExpandLatch) {
				ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
			}
			if (ImGui::TreeNode(chr->weapons_held[handIndex], "%s (%p)", label,
						chr->weapons_held[handIndex])) {
				imguiOverlayDescribeProp(chr->weapons_held[handIndex]);
				ImGui::TreePop();
			}
		}
	}
}

static void imguiOverlayDescribeProp(struct prop *prop)
{
	ImGui::TextUnformatted(prop->active ? "Active" : "Inactive");
	ImGui::Text("Type: %s (0x%02x)", imguiOverlayPropTypeName(prop->type), prop->type);
	ImGui::Text("Flags: 0x%02x", prop->flags);
	ImGui::Text("Position: %s", imguiOverlayCoordString(&prop->pos));
	ImGui::Text("Rooms: %s", imguiOverlayRoomListString(prop->rooms, 8));

	if ((prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR || prop->type == PROPTYPE_WEAPON) && prop->obj) {
		if (g_ImGuiOverlayExpandLatch) {
			ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
		}
		if (ImGui::TreeNode(prop->obj, "%s (%p)", imguiOverlayObjTypeName(prop->obj->type), prop->obj)) {
			imguiOverlayDescribeObj(prop->obj);
			ImGui::TreePop();
		}
	}

	if ((prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER || prop->type == PROPTYPE_EYESPY) && prop->chr) {
		if (g_ImGuiOverlayExpandLatch) {
			ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
		}
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
	return prop
		&& (!g_ImGuiOverlayPropFilter || prop->type == g_ImGuiOverlayPropFilter)
		&& (!g_ImGuiOverlayObjFilter || ((prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR
				|| prop->type == PROPTYPE_WEAPON) && prop->obj && prop->obj->type == g_ImGuiOverlayObjFilter));
}

static bool imguiOverlayPropPassesTextFilter(struct prop *prop, s32 index)
{
	char text[256];
	const char *objTypeName = "";
	s32 objType = 0;
	s32 chrNum = -1;

	if ((prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR || prop->type == PROPTYPE_WEAPON) && prop->obj) {
		objTypeName = imguiOverlayObjTypeName(prop->obj->type);
		objType = prop->obj->type;
	}

	if ((prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER || prop->type == PROPTYPE_EYESPY) && prop->chr) {
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

	snprintf(text, sizeof(text), "slot:%d chr:%04x %p body:%04x head:%02x team:%02x tude:%02x action:%s actionid:%02x damage:%.3f shield:%.3f",
			index, (u16)chr->chrnum, chr, (u16)chr->bodynum, (u8)chr->headnum,
			chr->team, chr->tude, imguiOverlayActionName(chr->actiontype),
			(u8)chr->actiontype, chr->damage, chr->cshield);

	return g_ImGuiOverlayChrTextFilter.PassFilter(text);
}

static void imguiOverlayDrawPropNode(struct prop *prop, s32 index)
{
	if (!imguiOverlayPropPassesFilters(prop) || !imguiOverlayPropPassesTextFilter(prop, index)) {
		return;
	}

	if (g_ImGuiOverlayExpandLatch) {
		ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
	}

	if (ImGui::TreeNode(prop, "%s %d (%p)", imguiOverlayPropTypeName(prop->type), index, prop)) {
		imguiOverlayDescribeProp(prop);
		ImGui::TreePop();
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
		ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Fojo Runtime", &g_ImGuiOverlayVisible)) {
			if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Stage: 0x%02x", (unsigned int)g_StageNum);
				ImGui::Text("Active mod: %d", g_ModNum);
				ImGui::Text("Window: %ux%u", gfx_current_window_dimensions.width,
						gfx_current_window_dimensions.height);
				ImGui::Text("Framebuffers: %s", gfx_framebuffers_enabled ? "enabled" : "disabled");
			}

			if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Emulated heap: %u MiB", g_OsMemSize / (1024 * 1024));
				ImGui::Text("Stage pool free: %u KiB", mempGetStageFree() / 1024);
			}

			if (ImGui::CollapsingHeader("Time")) {
				ImGui::Text("Level frame: %d", g_Vars.lvframenum);
				ImGui::Text("Level tick: %d (60 Hz), %d (240 Hz)",
						g_Vars.lvframe60, g_Vars.lvframe240);
				ImGui::Text("Frame time: %.2f (60 Hz), %.2f (240 Hz)",
						g_Vars.diffframe60f, g_Vars.diffframe240f);
				ImGui::Text("Lost time: %d (60 Hz), %d (240 Hz)",
						g_Vars.lostframetime60t, g_Vars.lostframetime240t);
			}

			if (ImGui::CollapsingHeader("Stage", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Stage: 0x%02x, table index: 0x%02x",
						(unsigned int)g_Vars.stagenum, (unsigned int)g_StageIndex);
				if (g_StageIndex >= 0 && g_StageIndex < 87) {
					const char *setupName = romdataFileGetName(g_Stages[g_StageIndex].setupfileid);
					ImGui::Text("Setup: %s", setupName ? setupName : "unregistered");
				}
				ImGui::Text("Rooms: %d", g_Vars.roomcount);

				if (g_Rooms && ImGui::TreeNode("Rooms")) {
					if (ImGui::BeginChild("Stage rooms", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders)) {
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

			if (ImGui::CollapsingHeader("Props")) {
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

			if (g_ChrSlots && g_NumChrSlots && ImGui::CollapsingHeader("Characters")) {
				ImGui::Text("Count: %d/%d", g_NumChrs, g_NumChrSlots);
				g_ImGuiOverlayChrTextFilter.Draw("Search characters", 180.0f);
				if (ImGui::BeginChild("Character list", ImVec2(0.0f, 280.0f), ImGuiChildFlags_Borders)) {
					for (s32 index = 0; index < g_NumChrSlots; ++index) {
						struct chrdata *chr = &g_ChrSlots[index];
						if (chr->chrnum < 0 || !imguiOverlayChrPassesTextFilter(chr, index)) {
							continue;
						}
						if (g_ImGuiOverlayExpandLatch) {
							ImGui::SetNextItemOpen(g_ImGuiOverlayExpandValue);
						}
						if (ImGui::TreeNode(chr, "Slot %d: 0x%04x (%p)", index, (u16)chr->chrnum, chr)) {
							imguiOverlayDescribeChr(chr);
							if (chr->prop) {
								ImGui::Text("Prop: %p", chr->prop);
							}
							ImGui::TreePop();
						}
					}
				}
				ImGui::EndChild();
			}

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

				if (ImGui::BeginChild("File slots", ImVec2(0.0f, 280.0f), ImGuiChildFlags_Borders)) {
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

			ImGui::Separator();
			ImGui::Text("F12 closes this overlay");
		}
		ImGui::End();
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
