#include <ultra64.h>
#include "constants.h"
#include "game/hudtint.h"
#include "bss.h"
#include "data.h"
#include "types.h"

u32 (*g_HudTintFn)(s32 mpindex) = NULL;

// same fallback hudmsg uses to find whose HUD is being drawn
static s32 hudTintCurrentMpIndex(void)
{
	if (g_Vars.currentplayerstats) {
		return g_Vars.currentplayerstats->mpindex;
	}

	return g_Vars.currentplayernum;
}

u32 hudTintGet(s32 mpindex)
{
	if (g_HudTintFn == NULL || mpindex < 0 || mpindex >= MAX_PLAYERS) {
		return 0;
	}

	return g_HudTintFn(mpindex);
}

/**
 * Swap a vanilla HUD green for that player's tint. Anything with red and blue
 * at zero and some green counts, and the tint is scaled by how bright the green
 * was, so the dim greens (the ammo gauge's 0x30, armour's 0xc0) stay dim.
 * Alpha is kept. Anything that isn't green comes back untouched.
 */
u32 hudTintColourFor(s32 mpindex, u32 rgba)
{
	u32 tint;
	u32 g;

	if ((rgba & 0xff00ff00) != 0 || (rgba & 0x00ff0000) == 0) {
		return rgba;
	}

	tint = hudTintGet(mpindex);

	if (tint == 0) {
		return rgba;
	}

	g = (rgba >> 16) & 0xff;

	return (((tint >> 24) & 0xff) * g / 255) << 24
		| (((tint >> 16) & 0xff) * g / 255) << 16
		| (((tint >> 8) & 0xff) * g / 255) << 8
		| (rgba & 0xff);
}

u32 hudTint(u32 rgba)
{
	return hudTintColourFor(hudTintCurrentMpIndex(), rgba);
}

/**
 * The crosshair is the one green the player owns. A tint, when there is one,
 * replaces it whole, alpha included; otherwise the player's colour stands.
 */
u32 hudTintCrosshair(u32 crosshaircolour)
{
	u32 tint = hudTintGet(hudTintCurrentMpIndex());

	return tint ? tint : crosshaircolour;
}
