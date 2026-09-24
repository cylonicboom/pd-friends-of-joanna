#ifndef _IN_GAME_HUDTINT_H
#define _IN_GAME_HUDTINT_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

/**
 * Per-player HUD tint: the colour that stands in for vanilla's hardcoded HUD
 * green. Nothing here decides who gets which colour -- g_HudTintFn does, and
 * with it unset every call hands vanilla's colour straight back.
 *
 * The fn takes an mpindex (a g_PlayerConfigsArray index) and returns a full
 * 0xrrggbbaa, or 0 for "no tint, keep vanilla". The alpha byte is only used
 * for the crosshair; every other site keeps its own alpha.
 */
extern u32 (*g_HudTintFn)(s32 mpindex);

u32 hudTintGet(s32 mpindex);
u32 hudTintColourFor(s32 mpindex, u32 rgba);
u32 hudTint(u32 rgba);
u32 hudTintCrosshair(u32 crosshaircolour);

#endif
