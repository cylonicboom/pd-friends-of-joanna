#ifndef FOJO_IMGUI_PROFILES_H
#define FOJO_IMGUI_PROFILES_H

// Profile manager (imgui_profiles.cpp): create, rename and delete MP
// profiles on the gamepak and set their head and body, seated or not.
void imguiProfilesDrawPanel(void);

// Seat changes asked for in the panel, run after the overlay has rendered:
// a profile must not change seats half way through a frame.
void imguiProfilesRunRequests(void);

#endif
