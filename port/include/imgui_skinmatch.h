#ifndef FOJO_IMGUI_SKINMATCH_H
#define FOJO_IMGUI_SKINMATCH_H

// Skin match panel (imgui_skinmatch.cpp): edits the companion masks and head
// tag points for the focused chr. Called from imgui_overlay's window loop.
struct chrdata;
void imguiSkinMatchDrawPanel(struct chrdata *chr);

#endif
