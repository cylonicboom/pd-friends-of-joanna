#include <SDL.h>
#include <PR/os_thread.h>

#include "../fast3d/glad/glad.h"
#include "../fast3d/gfx_pc.h"
#include "fs.h"
#include "imgui_overlay.h"
#include "input.h"
#include "system.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

static bool g_ImGuiOverlayInitialized = false;
static bool g_ImGuiOverlayVisible = false;
static bool g_ImGuiOverlayRestoreMouseLock = false;

extern s32 g_StageNum;
extern s32 g_ModNum;
extern u32 g_OsMemSize;
extern "C" u32 mempGetStageFree(void);

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

			if (ImGui::CollapsingHeader("Mods", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Loaded directories: %u", g_NumModDirs);
				for (u32 modIndex = 0; modIndex < g_NumModDirs; ++modIndex) {
					ImGui::BulletText("%u: %s", modIndex, modDirs[modIndex]);
				}
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
