#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"
#include "rumbleshake.h"

/*
 * Peak offset in viewport pixels at Game.ScreenShakeIntensity 1.0. Small on
 * purpose: gunfire rumbles on every shot, so automatic fire holds this on for
 * as long as the trigger is down.
 */
#define RUMBLESHAKE_AMPLITUDE 2.f

/*
 * Mirrors the pak's rumble fields (struct pak rumblettl / rumblepulsestopat /
 * rumblepulselen), but evaluated from the frame clock on demand instead of
 * ticked: vi.c asks several times per player per frame (world, gun, menus),
 * and every ask in one frame has to agree, which a pure function of
 * lvframe60 gives for free.
 */
struct rumbleshake {
	s32 start;    // lvframe60 when it started
	s32 ttl;      // length in 60ths
	s32 on;       // pulse on-length in 60ths, -1 for continuous
	s32 len;      // pulse period in 60ths
};

static struct rumbleshake g_RumbleShakes[MAX_PLAYERS];

static s32 rumbleShakeRemaining(const struct rumbleshake *rs)
{
	const s32 elapsed = g_Vars.lvframe60 - rs->start;

	// lvframe60 restarts at every stage load, so a start in the future is a
	// shake from the previous stage
	if (rs->ttl <= 0 || elapsed < 0 || elapsed >= rs->ttl) {
		return 0;
	}

	return rs->ttl - elapsed;
}

void rumbleShakeStart(s32 playernum, f32 numsecs, s32 onduration, s32 offduration)
{
	struct rumbleshake *rs;
	const s32 ttl = (s32)(60.f * numsecs);

	if (playernum < 0 || playernum >= MAX_PLAYERS || ttl <= 0) {
		return;
	}

	rs = &g_RumbleShakes[playernum];

	// same rule as pakRumble: a request only replaces one with less time left
	if (rumbleShakeRemaining(rs) >= ttl) {
		return;
	}

	rs->start = g_Vars.lvframe60;
	rs->ttl = ttl;
	rs->on = onduration;
	rs->len = onduration + offduration;
}

s32 rumbleShakeGetOffset(s32 playernum)
{
	const struct rumbleshake *rs;
	s32 amplitude;

	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return 0;
	}

	// paused, or a frame where no time passed: hold still rather than freeze
	// at one edge of the shake
	if (g_Vars.lvupdate240 <= 0) {
		return 0;
	}

	rs = &g_RumbleShakes[playernum];

	if (rumbleShakeRemaining(rs) <= 0) {
		return 0;
	}

	// the off half of a pulse, as joysTickRumble stops the motor there
	if (rs->on >= 0 && rs->len > 0) {
		const s32 phase = (g_Vars.lvframe60 - rs->start) % rs->len;

		if (phase >= rs->on) {
			return 0;
		}
	}

	amplitude = (s32)(RUMBLESHAKE_AMPLITUDE * g_ViShakeIntensityMult + 0.5f);

	if (amplitude <= 0) {
		return 0;
	}

	// flip every frame, like viHandleRetrace does for the whole window
	return (g_Vars.lvframenum & 1) ? amplitude : -amplitude;
}
