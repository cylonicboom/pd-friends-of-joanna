/**
 * Read-only chraiLua* bridges behind the core pd.* queries (pd.all_chrs,
 * pd.room_count, pd.chr_slots, pd.buttons, pd.aim_chr, pd.objective_status,
 * pd.lvupdate).
 *
 * From the Perfect Dark Kai fork (be46717), where they sat in chraction.c.
 * The comments are Kai's. Kai's net client tests are gone: this build has no
 * netplay.
 */

#include <ultra64.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "game/chaosstate.h"
#include "game/chr.h"
#include "game/lv.h"
#include "game/objectives.h"
#include "game/options.h"
#include "game/prop.h"
#include "lib/joy.h"
#include "luaai_api_internal.h"

// ---- Toolkit framework bridges (all-actor iteration) ----

s32 chraiLuaGetChrSlotCount(void)
{
	return chrsGetNumSlots();
}

s32 chraiLuaGetChrNumBySlot(s32 slot)
{
	if (slot < 0 || slot >= chrsGetNumSlots()) {
		return -1;
	}
	return (s32)chrslotGet(slot)->chrnum; // < 0 for an empty slot; caller skips it
}

// pd.lvupdate(): game ticks elapsed this frame (g_Vars.lvupdate60) — exactly
// 0 while the game is paused, scaled during slo-mo/boost. Scripts use it to
// advance effect timers on GAME time, so pausing can't run out a bad effect.
s32 chraiLuaLvUpdate(void)
{
	return g_Vars.lvupdate60;
}

// pd.aim_chr(): the chrnum the local player is currently aiming at (the
// autoaim/crosshair target via propFindAimingAt), or -1 if none. Lets the beat
// game reward an on-beat shot by dealing bonus damage to that chr.
s32 chraiLuaAimChr(void)
{
	struct prop *prop;

	if (apLuaPlayerChr() == NULL) {
		return -1;
	}
	prop = propFindAimingAt(HAND_RIGHT, false, FINDPROPCONTEXT_QUERY);
	if (prop && prop->type == PROPTYPE_CHR && prop->chr) {
		return (s32)prop->chr->chrnum;
	}
	return -1;
}

// pd.chr_slots(): free and total chr slots for this stage.
//
// The table is sized ONCE at stage load: chrmgrConfigure allocates
// PLAYERCOUNT() + the setup's own OBJTYPE_CHR count (+ co-op buddies) + MAX_BOTS
// out of MEMPOOL_STAGE, so runtime spawners share the MAX_BOTS slack, and the
// table cannot grow. When it is full chrSpawnAtCoord returns NULL and every
// spawn/clone helper reports failure.
//
// ⚠ A CORPSE still owns its slot until it is reaped, so a killing spree lowers
// the free count even when nothing new is alive. Spawners that want to keep going
// "until the level is full" must poll this rather than count their own spawns.
s32 chraiLuaChrSlotsFree(void)
{
	if (chrsGetNumSlots() <= 0) {
		return 0;
	}
	return chrsGetNumFree();
}

s32 chraiLuaChrSlotsTotal(void)
{
	return chrsGetNumSlots();
}

// pd.buttons() / pd.buttons_pressed(): the local player's RAW pad buttons
// (held / newly-pressed this frame). Reads the joy layer directly, so it sees
// buttons even while pd.button_block hides them from gameplay — the popup
// framework (quiz/EULA) blocks FIRE from shooting but still reads the answer.
u32 chraiLuaButtons(s32 pressed)
{
	s32 contpad;

	if (g_Vars.currentplayer == NULL || g_Vars.currentplayerstats == NULL) {
		return 0;
	}
	contpad = optionsGetContpadNum1(g_Vars.currentplayerstats->mpindex);
	if (pressed) {
		return joyGetButtonsPressedThisFrame(contpad, 0xffffffff);
	}
	return joyGetButtons(contpad, 0xffffffff);
}

// pd.objective_status(index): the objective's REAL status (override bypassed):
// 0 incomplete / 1 complete / 2 failed, or -1 if the index isn't a live
// objective on this stage+difficulty.
s32 chraiLuaObjectiveStatus(s32 index)
{
	u8 saved;
	s32 status;

	if (index < 0 || index >= MAX_OBJECTIVES || index >= objectiveGetCount()) {
		return -1;
	}
	if (!(objectiveGetDifficultyBits(index) & (1 << lvGetDifficulty()))) {
		return -1;
	}
	saved = g_ChaosObjectiveForce[index];
	g_ChaosObjectiveForce[index] = 0;
	status = objectiveCheck(index);
	g_ChaosObjectiveForce[index] = saved;
	return status;
}

// pd.room_count(): how many rooms this stage has (g_Vars.roomcount). Room numbers
// run 1..count-1 — index 0 is not a real room, which is why every room loop in
// the codebase starts at 1. 0 when no stage is loaded.
s32 chraiLuaRoomCount(void)
{
	if (g_Rooms == NULL || g_Vars.roomcount <= 0) {
		return 0;
	}
	return g_Vars.roomcount;
}
