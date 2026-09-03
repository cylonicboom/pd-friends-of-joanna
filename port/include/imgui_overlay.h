#ifndef FOJO_IMGUI_OVERLAY_H
#define FOJO_IMGUI_OVERLAY_H

#include <stdbool.h>

union SDL_Event;

#ifdef __cplusplus
extern "C" {
#endif

void imguiOverlayInit(void *window);
void imguiOverlayShutdown(void);
void imguiOverlayProcessEvent(const union SDL_Event *event);
void imguiOverlayStartFrame(void);
void imguiOverlayRender(void);
bool imguiOverlayCapturesKeyboard(void);
bool imguiOverlayCapturesMouse(void);

#ifdef __cplusplus
}
#endif

#endif