#include <SDL.h>
#include <PR/os_thread.h>

#include "../fast3d/glad/glad.h"
#include "../fast3d/gfx_pc.h"
#include "bss.h"
#undef bool
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
static s32 g_ImGuiOverlaySlotMod = -1;
static ImGuiTextFilter g_ImGuiOverlaySlotFilter;

extern s32 g_StageNum;
extern s32 g_ModNum;
extern u32 g_OsMemSize;
extern s32 g_StageIndex;
extern struct stagetableentry g_Stages[87];
extern "C" u32 mempGetStageFree(void);

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
						if (ImGui::BeginTable("Stage room table", 4,
								ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY)) {
							ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthFixed, 58.0f);
							ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed, 64.0f);
							ImGui::TableSetupColumn("Portals", ImGuiTableColumnFlags_WidthFixed, 64.0f);
							ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 72.0f);
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
							}
							ImGui::EndTable();
						}
					}
					ImGui::EndChild();
					ImGui::TreePop();
				}
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
