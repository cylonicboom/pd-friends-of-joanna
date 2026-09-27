#include <ultra64.h>
#include "constants.h"
#include "game/bondmove.h"
#include "game/bondwalk.h"
#include "game/body.h"
#include "game/modspectate.h"
#include "game/prop.h"
#include "bss.h"
#include "lib/vars.h"
#include "data.h"
#include "types.h"

/**
 * Spectator mode: the camera comes off the player and flies.
 *
 * The camera stays on the player prop rather than becoming a thing of its own,
 * because what makes a free camera hard here is not moving it, it is knowing
 * which rooms it is in. Everything the renderer draws is reached from a prop's
 * room list, and building one from nothing means duplicating the portal walk.
 * The player already carries a room list that the rest of the frame trusts, so
 * spectating keeps the prop and takes away the things that make it a
 * participant: it is not collided with, not shot at, and not moved by the walk
 * code.
 *
 * It is still drawn, and on purpose. The prop wears Dr Caroll - the flying
 * laptop from dataDyne Research - instead of the player's own body, so third
 * person shows a camera that looks like one. See modSpectateGetBodyNum().
 *
 * The move itself is deliberately not a collision test. func0f065e74() resolves
 * the destination rooms by walking portals, falls back to bgFindRoomsByPos()
 * when the portal walk comes up empty - which is what going through a wall
 * does - and falls back again to keeping the rooms we already had when the
 * destination is outside the level entirely. That last case is the one that
 * matters: the list is never returned empty, so flying into the void leaves the
 * camera drawing the room it left rather than handing the renderer nothing.
 * That is why this can skip collision at all.
 *
 * g_Vars.bondvisible is what keeps it out of the match, and it is about being
 * noticed rather than being drawn: every check that asks whether something can
 * see or target the player reads it, and nothing in the render path does -
 * bot.c's botIsTargetInvisible, chraction.c's target tests, the autoguns' and
 * choppers' line of sight in propobj.c. Clearing it is why the camera can hang
 * in front of a simulant and be ignored while still being on screen. It is a
 * global rather than per player, so in splitscreen one spectator makes every
 * player unnoticeable - accepted, because splitscreen spectating is not a thing
 * this is for.
 */

// How far the camera moves in one frame at full stick.
f32 g_ModSpectateSpeed = 20.0f;

// --spectate: begin the stage already spectating. There is no way to press a
// button before the first frame, and driving the menus is exactly what the
// headless benchmark runs cannot do.
s32 g_ModSpectateStart = 0;

static bool g_ModSpectating[MAX_PLAYERS] = { false, false, false, false };

// bondvisible and bondcollisions are global and cheats also write them, so what
// goes back on exit is what was there on entry rather than an assumed true.
static bool g_ModSpectateOldVisible = true;
static bool g_ModSpectateOldCollisions = true;
static u8 g_ModSpectateOldInvincible = 0;
static bool g_ModSpectateOldAimStance = false;
static bool g_ModSpectateOldThirdPerson = false;

// The body already built is the other mode's model, and the body is built once.
// playerTickChrBody() is where this is acted on rather than where the mode
// changes: taking a model down from inside the movement tick would leave the
// rest of playerTick() holding a chr whose model had gone.
static bool g_ModSpectateBodyStale[MAX_PLAYERS] = { false, false, false, false };

// Which stage --spectate has already been acted on for.
static s32 g_ModSpectateAppliedStage = -1;

/**
 * Whether the given player is spectating.
 *
 * The per player form exists because playerTickThirdPerson() runs for a player
 * that is not the current one - in splitscreen it is reached once per player
 * per frame, from whichever tick is looking at that body - and asking about the
 * current player there would answer about the wrong one.
 */
bool modSpectateIsOnForPlayer(s32 playernum)
{
	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return false;
	}

	return g_ModSpectating[playernum];
}

bool modSpectateIsOn(void)
{
	return modSpectateIsOnForPlayer(g_Vars.currentplayernum);
}

void modSpectateSetOn(bool on)
{
	if (g_Vars.currentplayernum < 0 || g_Vars.currentplayernum >= MAX_PLAYERS) {
		return;
	}

	if (g_ModSpectating[g_Vars.currentplayernum] == on) {
		return;
	}

	g_ModSpectating[g_Vars.currentplayernum] = on;

	// The spectator wears a different model from the player, so whichever body
	// is standing belongs to the mode being left.
	g_ModSpectateBodyStale[g_Vars.currentplayernum] = true;

	if (on) {
		g_ModSpectateOldVisible = g_Vars.bondvisible;
		g_ModSpectateOldCollisions = g_Vars.bondcollisions;

		g_Vars.bondvisible = false;
		g_Vars.bondcollisions = false;

		if (g_Vars.currentplayer) {
			g_ModSpectateOldInvincible = g_Vars.currentplayer->invincible;
			g_Vars.currentplayer->invincible = true;

			// A camera you cannot see is the one thing this does not want to
			// be, now that it has a model worth looking at. Both things can put
			// the view back on the eye, so both are taken and both are given
			// back: the third person request, which is what the camera is with
			// fojo movement off, and the aim stance, which is the way out of
			// third person with it on. A player who died holding aim would
			// otherwise arrive here looking out of a camera that has no gun.
			g_ModSpectateOldThirdPerson = g_Vars.currentplayer->thirdperson;
			g_ModSpectateOldAimStance = g_Vars.currentplayer->insightaimmode;
			g_Vars.currentplayer->thirdperson = true;
			g_Vars.currentplayer->insightaimmode = false;

			// Nothing should walk into a camera. The perimeter is what other
			// props collide against, and it is restored on the way out.
			if (g_Vars.currentplayer->prop) {
				propSetPerimEnabled(g_Vars.currentplayer->prop, false);
			}
		}
	} else {
		g_Vars.bondvisible = g_ModSpectateOldVisible;
		g_Vars.bondcollisions = g_ModSpectateOldCollisions;

		if (g_Vars.currentplayer) {
			g_Vars.currentplayer->invincible = g_ModSpectateOldInvincible;
			g_Vars.currentplayer->insightaimmode = g_ModSpectateOldAimStance;
			g_Vars.currentplayer->thirdperson = g_ModSpectateOldThirdPerson;

			if (g_Vars.currentplayer->prop) {
				propSetPerimEnabled(g_Vars.currentplayer->prop, true);
			}
		}
	}
}

/**
 * The model the spectator wears, or -1 for the one the player would have had.
 *
 * Dr Caroll is a laptop with wings and a pair of eyes on the screen - the one
 * model in the game that reads as something watching rather than something
 * taking part, and the only one whose shape says the thing wearing it cannot
 * shoot back. BODY_DRCAROLL carries its own head, so playerTickChrBody() forces
 * headnum to -1 and none of the head machinery runs; its height is 159, the
 * same as the body it replaces, so the eye does not move when the model does.
 *
 * The model is loaded from MEMPOOL_STAGE on demand, and a match with the body
 * cap raised can be near the end of that pool. If it will not load, the
 * spectator keeps the body it had rather than handing body0f02ce8c() a NULL
 * modeldef - most callers of modeldefLoadToNew() do not check, and there is a
 * sensible answer here.
 */
s32 modSpectateGetBodyNum(void)
{
	if (!modSpectateIsOn()) {
		return -1;
	}

	bodyLoad(BODY_DRCAROLL);

	if (g_HeadsAndBodies[BODY_DRCAROLL].modeldef == NULL) {
		return -1;
	}

	return BODY_DRCAROLL;
}

/**
 * Whether the standing body was built for the other mode.
 *
 * Two ways to ask, because two places need it for different reasons.
 * playerTickChrBody() takes the mark, being the one place that can act on it.
 * playerTick() only looks, to decide whether to call playerTickChrBody() at
 * all: a multiplayer body is kept rather than rebuilt each tick, and with the
 * camera on the eye that function is never reached.
 */
bool modSpectateBodyIsStale(void)
{
	if (g_Vars.currentplayernum < 0 || g_Vars.currentplayernum >= MAX_PLAYERS) {
		return false;
	}

	return g_ModSpectateBodyStale[g_Vars.currentplayernum];
}

bool modSpectateTakeBodyStale(void)
{
	bool stale = modSpectateBodyIsStale();

	if (stale) {
		g_ModSpectateBodyStale[g_Vars.currentplayernum] = false;
	}

	return stale;
}

void modSpectateToggle(void)
{
	modSpectateSetOn(!modSpectateIsOn());
}

/**
 * Act on --spectate, once per stage.
 *
 * It cannot be done at boot: playermgrAllocatePlayer() ends by putting
 * bondvisible and bondcollisions back to true, so anything set before it runs
 * is undone. It waits for a prop as well, because entering the mode turns the
 * prop's perimeter off. The first movement tick of a stage is the first moment
 * both are true.
 */
void modSpectateApplyStart(void)
{
	if (!g_ModSpectateStart || g_ModSpectateAppliedStage == g_Vars.stagenum) {
		return;
	}

	if (g_Vars.currentplayer == NULL || g_Vars.currentplayer->prop == NULL) {
		return;
	}

	g_ModSpectateAppliedStage = g_Vars.stagenum;
	modSpectateSetOn(true);
}

/**
 * Forget the mode across a stage load. The prop and the room list on the far
 * side belong to a different level, and the saved bondvisible does not.
 */
void modSpectateReset(void)
{
	s32 i;

	for (i = 0; i < MAX_PLAYERS; i++) {
		g_ModSpectating[i] = false;
		g_ModSpectateBodyStale[i] = false;
	}

	g_ModSpectateOldVisible = true;
	g_ModSpectateOldCollisions = true;
	g_ModSpectateOldInvincible = 0;
	g_ModSpectateOldAimStance = false;
	g_ModSpectateOldThirdPerson = false;
	g_ModSpectateAppliedStage = -1;
}

/**
 * Stands in for bwalkTick() while spectating.
 *
 * bmoveProcessInput() has already run in bmoveTick() and left the look angles
 * and the stick in speedforwards/speedsideways, so this only has to turn them
 * into a position. bmoveUpdateVerta() is called the way every other movement
 * mode's tick calls it, because it is what refreshes the look vector this then
 * reads.
 */
void modSpectateTick(void)
{
	struct prop *prop;
	struct coord dstpos;
	RoomNum dstrooms[8];
	f32 speed;
	f32 fwd;
	f32 side;

	if (g_Vars.currentplayer == NULL || g_Vars.currentplayer->prop == NULL) {
		return;
	}

	prop = g_Vars.currentplayer->prop;

	// The head of bwalkTick(), minus the parts that walk. Theta is the stick's
	// half of turning, the mouse having already been taken in
	// bmoveProcessInput(), and bmoveUpdateVerta() is what turns both angles
	// into the look vector read below.
	bwalkUpdatePrevPos();
	bwalkUpdateTheta();
	bmoveUpdateVerta();

	fwd = g_Vars.currentplayer->speedforwards;
	side = g_Vars.currentplayer->speedsideways;
	speed = g_ModSpectateSpeed * g_Vars.lvupdate60freal;

	// Forward follows the look vector including its pitch, so looking up and
	// pushing forward climbs. Strafing stays level, which is what a flying
	// camera is expected to do. The horizontal pair is the same expression
	// bwalkTick() uses, so the sense of the stick does not change when the
	// mode does.
	dstpos.x = prop->pos.x + ((g_Vars.currentplayer->bond2.heading.x * g_Vars.currentplayer->vv_cosverta * fwd)
			- (g_Vars.currentplayer->bond2.heading.z * side)) * speed;
	dstpos.z = prop->pos.z + ((g_Vars.currentplayer->bond2.heading.z * g_Vars.currentplayer->vv_cosverta * fwd)
			+ (g_Vars.currentplayer->bond2.heading.x * side)) * speed;

	// vv_sinverta is taken from vv_verta360, so looking up gives +1 and looking
	// down gives -1 without a sign flip here.
	dstpos.y = prop->pos.y + g_Vars.currentplayer->vv_sinverta * fwd * speed;

	func0f065e74(&prop->pos, prop->rooms, &dstpos, dstrooms);

	prop->pos.x = dstpos.x;
	prop->pos.y = dstpos.y;
	prop->pos.z = dstpos.z;

	propDeregisterRooms(prop);
	roomsCopy(dstrooms, prop->rooms);

	bmoveUpdateRooms(g_Vars.currentplayer);

	// The body's height is not taken from the prop - chr0f01f378() reads
	// player->vv_manground for a player prop and puts the model there, which is
	// how a walking body ends up on the floor it is standing on. bwalkTick()
	// keeps that up to date, and this stands in for bwalkTick(), so without
	// this the model stays on the floor the camera took off from while the
	// camera flies away from it.
	//
	// The prop is the eye, not the feet - playerReset() spawns it at
	// groundy + vv_eyeheight - so the ground under a flying camera is that far
	// below it. bondbike.c does the same thing for the same reason: a movement
	// mode that is not the walk has to say where its own ground is.
	g_Vars.currentplayer->vv_ground = prop->pos.y - g_Vars.currentplayer->vv_height;
	g_Vars.currentplayer->vv_manground = g_Vars.currentplayer->vv_ground;

	// The tail of bwalkTick(), and the part that is easy to leave out: moving
	// the prop does not move the view. bmove0f0cc654() refreshes the look and
	// up vectors, and bmove0f0cc19c() puts the eye on the prop - the camera is
	// built from those three, so without them the prop flies off and the
	// picture stays where the walk last left it.
	//
	// The three arguments are the speeds bwalkTick() passes to lean and sway
	// the view with the stride. A flying camera has no stride, so they are zero
	// and the view stays level.
	bmove0f0cc654(0, 0, 0);
	bmove0f0cc19c(&prop->pos);
}

/**
 * Put the player at a point, editor style. The move half of
 * modSpectateTick() with no input: the destination's rooms come from the
 * same portal walk and the same bgFindRoomsByPos fallback, so a target
 * on the far side of three walls still lands in a room, and the ground
 * and the view are refreshed the way the tick does it.
 *
 * `eye` says whether pos is where the eye goes or where the feet go. A
 * pad is a place to stand, so the bar passes the pad's pos and eye = false
 * and this lifts it by vv_eyeheight; a chr's prop pos is already an eye.
 *
 * Works whether or not spectating: with the walk on, the next bwalkTick
 * takes it from there and gravity does what it does.
 */
void modSpectateTeleport(const struct coord *pos, bool eye)
{
	struct prop *prop;
	struct coord dstpos;
	RoomNum dstrooms[8];

	if (g_Vars.currentplayer == NULL || g_Vars.currentplayer->prop == NULL || pos == NULL) {
		return;
	}

	prop = g_Vars.currentplayer->prop;

	dstpos.x = pos->x;
	dstpos.y = pos->y + (eye ? 0.0f : g_Vars.currentplayer->vv_eyeheight);
	dstpos.z = pos->z;

	func0f065e74(&prop->pos, prop->rooms, &dstpos, dstrooms);

	prop->pos.x = dstpos.x;
	prop->pos.y = dstpos.y;
	prop->pos.z = dstpos.z;

	propDeregisterRooms(prop);
	roomsCopy(dstrooms, prop->rooms);

	bmoveUpdateRooms(g_Vars.currentplayer);

	g_Vars.currentplayer->vv_ground = prop->pos.y - g_Vars.currentplayer->vv_height;
	g_Vars.currentplayer->vv_manground = g_Vars.currentplayer->vv_ground;

	g_Vars.currentplayer->bondprevpos.x = prop->pos.x;
	g_Vars.currentplayer->bondprevpos.y = prop->pos.y;
	g_Vars.currentplayer->bondprevpos.z = prop->pos.z;

	bmove0f0cc654(0, 0, 0);
	bmove0f0cc19c(&prop->pos);
}
