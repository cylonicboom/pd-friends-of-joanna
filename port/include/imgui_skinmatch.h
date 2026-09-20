#ifndef FOJO_IMGUI_SKINMATCH_H
#define FOJO_IMGUI_SKINMATCH_H

// Skin match panel (imgui_skinmatch.cpp): edits the companion masks and head
// tag points for the focused chr. Called from imgui_overlay's window loop.
struct chrdata;
struct GfxTextureDebugInfo;
void imguiSkinMatchDrawPanel(struct chrdata *chr);

// Provided by imgui_overlay.cpp: the texture ids a model uses (from its
// modeldef, on screen or not) and a GL texture for one of them, probing it
// through the engine when the renderer did not draw it this frame.
s32 imguiOverlayModelTextureIds(s32 fileid, u16 *out, s32 max);
bool imguiOverlayProbeModelTexture(s32 fileid, u16 texId, struct GfxTextureDebugInfo *out);

#endif
