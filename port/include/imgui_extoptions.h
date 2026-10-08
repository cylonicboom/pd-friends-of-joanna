#ifndef FOJO_IMGUI_EXTOPTIONS_H
#define FOJO_IMGUI_EXTOPTIONS_H

// Extended Options in fojOS (imgui_extoptions.cpp): the port's Extended menu -
// Video, Audio, Mouse, Controller, Game, Key Bindings - drawn from the same
// menuitem tables the std menu uses, for any of the four players.
void imguiExtOptionsDrawPanel(void);

// true while a bind is waiting for its key; the overlay stops feeding input
// to ImGui so the press is not also a click or a keystroke there
bool imguiExtOptionsIsCapturing(void);

#endif
