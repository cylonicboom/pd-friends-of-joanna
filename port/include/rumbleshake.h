#ifndef _IN_RUMBLESHAKE_H
#define _IN_RUMBLESHAKE_H

#include <PR/ultratypes.h>

/*
 * Rumble shown as a shake of one player's viewport, for players with no motor
 * in their pad or who would rather see it than feel it.
 *
 * pakRumble is the one place the game asks for rumble (gunfire in bondgun.c,
 * taking a hit in chraction.c). It hands the request here as well as, or
 * instead of, to the pak's motor, per Input.PlayerN.RumbleShake. The shake is
 * applied by offsetting that player's viewport transform inside its unchanged
 * scissor (vi.c), so the world and the gun move, the HUD does not, and in
 * splitscreen nobody else's view moves. viShake, by contrast, moves the whole
 * window.
 */

void rumbleShakeStart(s32 playernum, f32 numsecs, s32 onduration, s32 offduration);

// Signed vertical offset in viewport pixels for this frame; 0 when idle.
s32 rumbleShakeGetOffset(s32 playernum);

#endif
