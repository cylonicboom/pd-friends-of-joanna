/**
 * chraiLua* bridges for the chrs group of the pd.* API: chr mutators, spawns,
 * explosions, body swaps and possession.
 *
 * From the Perfect Dark Kai fork (be46717), where the bridges sat in
 * src/game/chraction.c (the Lua helper block, 8617-13927). The comments are
 * Kai's. Kai's net client / net mode tests are gone: this build has no
 * netplay, so every call behaves as it did for Kai's solo player.
 *
 * Also here, because only these bridges use them:
 *  - the evil-twin registry test chaosIsTwin (read by chrDamage) and the
 *    chaos chopper identity tests chaosChopperIsChaos / chaosChopperKind
 *    (read by propobj.c's chopper tick);
 *  - chraiLuaSetChrPos, which port/src/possess.c calls every frame;
 *  - chraiLuaResetSentries, which lvReset calls.
 *
 * pd.spawn / pd.spawn_at_chr, pd.explosion_at and pd.body_snatch need three
 * bridges the weapons group owns (chraiLuaSpawnAtPos, chraiLuaExplodeAtPos,
 * chraiLuaGiveMags). This file carries private copies of them
 * (chrsSpawnAtPos, chrsExplodeAtPos, chrsGiveMags) so the group stands on
 * its own.
 */

#include <ultra64.h>
#include <math.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "game/atan2f.h"
#include "game/bg.h"
#include "game/body.h"
#include "game/bondgun.h"
#include "game/botact.h"
#include "game/botinv.h"
#include "game/chaosstate.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrai.h"
#include "game/explosions.h"
#include "game/game_0b0fd0.h"
#include "game/inv.h"
#include "game/modelmgr.h"
#include "game/mplayer/mplayer.h"
#include "game/nbomb.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/propsnd.h"
#include "game/setup.h"
#include "game/setuputils.h"
#include "game/vtxstore.h"
#include "lib/ailist.h"
#include "lib/anim.h"
#include "lib/collision.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "lib/rng.h"
#include "lib/snd.h"
#include "luaai_api_internal.h"

// pd.body_snatch hands over this many magazines of the guard's gun (Kai's
// CHAOS_GUN_MAGS: a free gun should be a moment of power, not a licence to
// stop caring about ammo).
#define CHAOS_GUN_MAGS 2

/* ------------------------------------------------------------------------- *
 * Private copies of weapons-group bridges
 * ------------------------------------------------------------------------- */

// Lua bridge: spawn a weapon/item world object at an arbitrary position, with
// rooms seeded from a reference chr (refchrnum, default-resolved by the caller).
// Backs pd.spawn(). The object is created anchored to the reference chr (so the
// engine's MP-index / creation paths stay valid), then repositioned to the
// target and floor-snapped using the same primitives the engine uses for normal
// object placement: cdFindFloorRoomYColourFlagsAtPos walks the portal graph from
// the reference chr's (known-valid) rooms to find the real floor room + height
// at the target, then func0f06a580 sets pos + re-registers rooms. Placement is
// reliable when the target is reachable through portals from the reference chr;
// if no floor is found we fall back to the raw pos with the seed rooms (the
// object may float rather than crash).
// (Kai's chraiLuaSpawnAtPos.)
static s32 chrsSpawnAtPos(s32 refchrnum, s32 weaponnum, f32 x, f32 y, f32 z)
{
	struct chrdata *refchr;
	struct weaponobj *weapon;
	s32 modelnum;
	struct coord pos;
	Mtxf mtx;
	RoomNum seedrooms[8];
	RoomNum floorroom;
	f32 floory;
	struct modelrodata_bbox *bbox;

	// refchrnum < 0 means "use the local player's chr" -- the common case for
	// spawning near the player, and a guaranteed-valid seed-rooms source.
	if (refchrnum < 0) {
		refchr = (g_Vars.currentplayer && g_Vars.currentplayer->prop)
				? g_Vars.currentplayer->prop->chr : NULL;
	} else {
		refchr = chrFindByLiteralId(refchrnum);
	}
	if (refchr == NULL || refchr->prop == NULL) {
		return 0; // need a valid reference chr for creation + seed rooms
	}

	// the suitcase has no model in playermgrGetModelOfWeapon (Lua-only mapping,
	// as in the weapons bridge)
	modelnum = weaponnum == WEAPON_SUITCASE ? MODEL_SUITCASE : playermgrGetModelOfWeapon(weaponnum);
	if (modelnum < 0) {
		return 0;
	}

	weapon = weaponCreateProjectileFromWeaponNum(modelnum, (u8)weaponnum, refchr);
	if (weapon == NULL || weapon->base.prop == NULL || weapon->base.model == NULL) {
		return 0;
	}

	modelSetScale(weapon->base.model, weapon->base.model->scale);
	weapon->timer240 = TICKS(720);

	pos.x = x;
	pos.y = y;
	pos.z = z;
	mtx4LoadIdentity(&mtx);
	roomsCopy(refchr->prop->rooms, seedrooms);

	// Floor-snap: find the real floor room + Y at the target (portal-walk from the
	// reference rooms), then place resting on the floor. Mirrors func0f06a650.
	bbox = modelFindBboxRodata(weapon->base.model);
#if VERSION >= VERSION_NTSC_1_0
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, seedrooms, &floory, &weapon->base.floorcol, NULL);
#else
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, seedrooms, &floory, &weapon->base.floorcol);
#endif

	if (floorroom > 0) {
		RoomNum placerooms[2];
		struct coord placepos;
		placepos.x = pos.x;
		placepos.y = floory - objGetRotatedLocalYMinByMtx4(bbox, &mtx);
		placepos.z = pos.z;
		placerooms[0] = floorroom;
		placerooms[1] = -1;
		func0f06a580(&weapon->base, &placepos, &mtx, placerooms);
	} else {
		func0f06a580(&weapon->base, &pos, &mtx, seedrooms);
	}

	objSetDropped(weapon->base.prop, DROPTYPE_DEFAULT);
	return 1;
}

// pd.explosion_at(x, y, z [, type]): detonate at an arbitrary position,
// attributed to the local player. Rooms are portal-walked from the player's
// (known-valid) rooms to the real floor room at the target — the same
// placement recipe as chraiLuaSpawnAtPos. Backs the Live Grenade / Martyrdom
// delayed-boom pattern: Lua records a position, spawns a grenade pickup
// there (pd.spawn), then calls this when the fuse runs out.
// (Kai's chraiLuaExplodeAtPos.)
static s32 chrsExplodeAtPos(f32 x, f32 y, f32 z, s32 type)
{
	struct coord pos;
	RoomNum rooms[8];
	// bgFindRoomsByPos writes up to `max` entries PLUS a -1 terminator, and
	// roomsCopy below copies to the terminator with no bound of its own — so the
	// cap must be 7, not the 20 most callers pass, or a position spanning many
	// rooms would overflow rooms[8]. A point is realistically in one or two.
	RoomNum inrooms[8];
	RoomNum aboverooms[8];
	f32 floory;
	u16 floorcol;
	s32 floorroom;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	pos.x = x;
	pos.y = y;
	pos.z = z;

	// Derive the room set from the POSITION, not from the player.
	//
	// This used to seed `rooms` with the player's own rooms and then refine it
	// via cdFindFloorRoomYColourFlagsAtPos — but that search only resolves a
	// floor for positions in (or portal-adjacent to) the seed rooms. Detonate
	// somewhere the player's rooms don't reach and it returned <= 0, leaving
	// `rooms` as the PLAYER's room set: the explosion then went off in the
	// player's room instead of at the target, which reads in-game as "the
	// explosion spawned on me" (user report, 2026-07-30, Chain Reaction blowing
	// up at the player rather than the corpse). Every distant caller was
	// affected — Chain Reaction, Martyrdom's fuse blast, the SPEED payoff.
	//
	// bgFindRoomsByPos is the seed-free primitive for this (the Slayer rocket's
	// out-of-bounds test in player.c uses it the same way); `aboverooms` covers a
	// point floating just above a floor, which a corpse position can be. The
	// player's rooms remain the last resort so an out-of-bounds position still
	// produces an explosion rather than nothing at all.
	bgFindRoomsByPos(&pos, inrooms, aboverooms, 7, NULL);

	if (inrooms[0] != -1) {
		roomsCopy(inrooms, rooms);
	} else if (aboverooms[0] != -1) {
		roomsCopy(aboverooms, rooms);
	} else {
		roomsCopy(g_Vars.currentplayer->prop->rooms, rooms);
	}

#if VERSION >= VERSION_NTSC_1_0
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, rooms, &floory, &floorcol, NULL);
#else
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, rooms, &floory, &floorcol);
#endif
	if (floorroom > 0) {
		rooms[0] = floorroom;
		rooms[1] = -1;
	}

	// g_ExplosionTypes has no bound of its own; explosionCreate only rejects NONE
	if (type <= EXPLOSIONTYPE_NONE || type > EXPLOSIONTYPE_HUGE25) {
		return 0;
	}

	return explosionCreateSimple(NULL, &pos, rooms, (s16)type, g_Vars.bondplayernum) ? 1 : 0;
}

/**
 * pd.give_mags(n): stock every ammo type with n MAGAZINES instead of filling it
 * to capacity, so the gun-giving chaos effects hand you a usable weapon rather
 * than an inexhaustible one.
 *
 * A magazine is a property of the WEAPON FUNCTION, not the ammo type — several
 * weapons share an ammo type with different clip sizes — so there is no single
 * "one magazine" figure to read off the ammo table. Pass 1 walks every weapon
 * and both its functions and records the LARGEST clip size seen per ammo type;
 * pass 2 writes n of those back.
 * (Kai's chraiLuaGiveMags.)
 */
static s32 chrsGiveMags(s32 mags)
{
	s32 clip[AMMOTYPE_ECM_MINE + 1];
	s32 i;
	s32 f;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	if (mags < 1) {
		mags = 1;
	} else if (mags > 99) {
		mags = 99;
	}

	for (i = 0; i < ARRAYCOUNT(clip); i++) {
		clip[i] = 0;
	}

	for (i = WEAPON_UNARMED; i <= WEAPON_SUICIDEPILL; i++) {
		for (f = 0; f < 2; f++) {
			struct inventory_ammo *ammo = weaponGetAmmoByFunction(i, f);

			if (ammo && ammo->type < ARRAYCOUNT(clip) && ammo->clipsize > clip[ammo->type]) {
				clip[ammo->type] = ammo->clipsize;
			}
		}
	}

	for (i = WEAPON_UNARMED; i <= WEAPON_SUICIDEPILL; i++) {
		for (f = 0; f < 2; f++) {
			struct inventory_ammo *ammo = weaponGetAmmoByFunction(i, f);

			if (ammo && ammo->type < ARRAYCOUNT(clip) && clip[ammo->type] > 0) {
				s32 cap = bgunGetAmmoCapacityForWeapon(i, f);
				s32 qty = clip[ammo->type] * mags;

				if (cap > 0 && qty > cap) {
					qty = cap;
				}

				bgunSetAmmoQtyForWeapon(i, f, qty);
			}
		}
	}

	return 1;
}

/* ------------------------------------------------------------------------- *
 * Shared chaos state and helpers
 * ------------------------------------------------------------------------- */

// Chaos Evil-twin/clone registry (g_ChaosTwinChrnums, chaosstate.c): chrnums of
// live twins, used to make them psychosis-immune (a twin damaged by the player
// must stay hostile, not flip to the psychosised "friendly" AI list — see
// chaosIsTwin's use in chrDamage).

// True if chr is a chaos evil-twin/clone (used to make them psychosis-immune).
bool chaosIsTwin(struct chrdata *chr)
{
	s32 i;

	if (chr == NULL) {
		return false;
	}
	for (i = 0; i < (s32)ARRAYCOUNT(g_ChaosTwinChrnums); i++) {
		if (g_ChaosTwinChrnums[i] == chr->chrnum) {
			return true;
		}
	}
	return false;
}

// Is there actually room for a body here? (user report 2026-07-30: Hydra spawning
// guards inside walls.) A floor snap only answers "how high is the ground" — it
// says nothing about whether a body FITS, so any caller picking a blind offset
// could drop a chr straight into geometry.
//
// chrAdjustPosForSpawn is the engine's own answer, and the same call the
// quantum-teleport safety fix uses: it volume-tests CDTYPE_ALL — world geometry
// AND physics objects like tables and crates — at the requested point and, if
// that collides, nudges through a ring of 8 directions looking for a clear one.
// `angle` only biases which direction it tries first.
//
// The nudge moves x/z, so the floor is re-derived afterwards, and that doubles as
// the out-of-bounds guard: a nudge can clear the point test on the far side of a
// thin wall or out over void, where there is no floor room at all. Probes on
// COPIES and fails rather than committing garbage. On success pos.y and rooms
// are updated to the final standing spot; on failure both are left alone and the
// caller should retry elsewhere.
static s32 chaosSpawnFindClearPos(struct coord *pos, RoomNum *rooms, f32 angle)
{
	struct chrdata *plchr = g_Vars.currentplayer ? g_Vars.currentplayer->prop->chr : NULL;
	f32 radius = (plchr && plchr->radius > 0.0f) ? plchr->radius : 30.0f;
	struct coord probe;
	RoomNum proberooms[8];
	f32 floory;
	u16 floorcol;
	s32 floorroom;

#if VERSION >= VERSION_NTSC_1_0
	if (!chrAdjustPosForSpawn(radius, pos, rooms, angle, true, false, false)) {
#else
	if (!chrAdjustPosForSpawn(radius, pos, rooms, angle, true, false)) {
#endif
		return 0;
	}

	probe = *pos;
	roomsCopy(rooms, proberooms);
#if VERSION >= VERSION_NTSC_1_0
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&probe, proberooms, &floory, &floorcol, NULL);
#else
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&probe, proberooms, &floory, &floorcol);
#endif
	if (floorroom <= 0) {
		return 0;
	}

	pos->y = floory;
	rooms[0] = floorroom;
	rooms[1] = -1;
	return 1;
}

// Place a chr (or a body-model player) at pos/rooms, re-grounding it. The
// net-snap entry point of the upstream port (chrSetPos in Kai's chraction.c);
// this build has no caller for it outside pd.body_snatch's warp, so it lives
// here, private.
static bool chrsChrSetPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms, f32 theta, bool findground)
{
	// Net-snap entry point (CSP teleport / force-move). Unlike its sibling
	// chrMoveToPos this is reached directly from wire-driven paths, so guard the
	// pointers it unconditionally dereferences below (chr->prop->rooms,
	// chr->model->...) — a spectator / mid-spawn remote slot can momentarily have
	// a NULL model or prop. See docs/netplay-code-review-2026.md (M-1).
	if (chr == NULL || chr->prop == NULL || chr->model == NULL || pos == NULL || rooms == NULL) {
		return false;
	}

	const f32 angle = BADDEG2RAD(360.f - theta);

	if (findground) {
		const u32 oldhidden = chr->hidden;
		chr->hidden |= CHRHFLAG_WARPONSCREEN;
		const bool ret = chrMoveToPos(chr, pos, rooms, angle, true);
		if ((oldhidden & CHRHFLAG_WARPONSCREEN) == 0) {
			chr->hidden &= ~CHRHFLAG_WARPONSCREEN;
		}
	}

	bool newrooms = false;
	for (s32 i = 0; i < ARRAYCOUNT(chr->prop->rooms) && rooms[i] >= 0; ++i) {
		if (chr->prop->rooms[i] != rooms[i]) {
			newrooms = true;
			break;
		}
	}

	propSetPerimEnabled(chr->prop, false);

	chr->prop->pos = *pos;

	const f32 ground = cdFindGroundInfoAtCyl(pos, chr->radius, rooms, &chr->floorcol,
			&chr->floortype, NULL, &chr->floorroom, NULL, NULL);

	chr->ground = ground;
	chr->manground = ground;
	chr->sumground = ground * (PAL ? 8.4175090789795f : 9.999998f);

	if (newrooms) {
		propDeregisterRooms(chr->prop);
		roomsCopy(rooms, chr->prop->rooms);
		chr0f0220ac(chr);
	}

	modelSetRootPosition(chr->model, pos);

	const u16 nodetype = chr->model->definition->rootnode->type;

	if ((nodetype & 0xff) == MODELNODETYPE_CHRINFO) {
		union modelrwdata *rwdata = modelGetNodeRwData(chr->model, chr->model->definition->rootnode);
		rwdata->chrinfo.ground = ground;
	}

	chrSetLookAngle(chr, angle);

	if (chr->prop->type == PROPTYPE_PLAYER) {
		struct player *player = g_Vars.players[playermgrGetPlayerNumByProp(chr->prop)];
		player->vv_manground = ground;
		player->vv_ground = ground;
		player->vv_theta = theta;
		player->unk1c64 = 1;
	}

	propSetPerimEnabled(chr->prop, true);

	return true;
}


// Body-snatch "home" — the player's position before the snatch, so the timed
// effect can teleport them back when it ends.
static struct coord g_ChaosSnatchHome;
static RoomNum g_ChaosSnatchHomeRooms[8];
// g_ChaosSnatchActive (chaosstate.c) is cleared on stage load — a mid-snatch
// stage change tears down the Lua state without calling stop(), and a stale
// "active" flag would keep guards from ever firing (and break real disguise
// missions).


// Warp the local player to pos/rooms. The solo first-person player has no chr
// model, so move the prop by hand (the chraiLuaTeleportToChr model-less path);
// a body-model player (Combat Sim) uses chrSetPos.
static void chaosPlayerWarp(struct coord *pos, RoomNum *rooms)
{
	struct chrdata *pl = g_Vars.currentplayer->prop->chr;
	struct prop *plprop = g_Vars.currentplayer->prop;
	RoomNum tmp[8];
	f32 ground;

	if (pl == NULL) {
		return;
	}

	roomsCopy(rooms, tmp);

	if (pl->model != NULL) {
		chrsChrSetPos(pl, pos, tmp, chrGetRotY(pl), true);
		return;
	}

	propSetPerimEnabled(plprop, false);
	plprop->pos = *pos;
	ground = cdFindGroundInfoAtCyl(&plprop->pos, pl->radius, tmp,
			&pl->floorcol, &pl->floortype, NULL, &pl->floorroom, NULL, NULL);
	pl->ground = ground;
	pl->manground = ground;
	pl->sumground = ground * (PAL ? 8.4175090789795f : 9.999998f);
	propDeregisterRooms(plprop);
	roomsCopy(tmp, plprop->rooms);
	chr0f0220ac(pl);
	if (plprop->type == PROPTYPE_PLAYER) {
		struct player *player = g_Vars.players[playermgrGetPlayerNumByProp(plprop)];
		player->vv_manground = ground;
		player->vv_ground = ground;
		player->vv_verta = 0;
		player->unk1c64 = 1;
	}
	propSetPerimEnabled(plprop, true);
}

// pd.civil_war(on): "Civil war" — turn the NPCs on each other for the effect's
// duration. Just forcing a target isn't enough: guards are the same team, so the
// AI drops a same-team target and re-locks onto the player. So we split them into
// mutually-hostile teams (distinct single team bits, cycled through the 8) — the
// AI's own nearest-enemy search then fires on other guards — and additionally
// point each one at its nearest living neighbour with full alertness + the
// trigger-shot flag. The Lua effect re-asserts this every ~0.5s so they keep
// hunting as they move and die. on=false restores the original teams and calms
// everyone. Solo/server only. First activation snapshots teams; re-activations
// (on=true while already active) just re-target.
#define CHAOS_CIVILWAR_MAX 256
static s16 g_ChaosCivilWarChr[CHAOS_CIVILWAR_MAX];
static u8  g_ChaosCivilWarTeam[CHAOS_CIVILWAR_MAX];

// pd.spawn_bike(): spawn a personal HALF-SIZE hoverbike at the player's feet
// (extrascale 128 — the collision cylinder radius scales with it via the
// propobj.c geo fix, so it genuinely fits where a full bike wouldn't).
// One static instance: retriggering repositions the existing bike back to
// the player instead of allocating another. Solo/offline only — runtime
// objects have no syncid, so netplay clients would never see it.
static struct hoverbikeobj g_ChaosBike;
static s32 g_ChaosBikeSpawned = 0;

// propobj.c scales the geo cylinder by extrascale for this object only.
bool chaosObjIsLuaBike(struct defaultobj *obj)
{
	return obj == &g_ChaosBike.base;
}


// Chaos "Sentries Out": free-standing hostile laptop sentry guns. Storage is our
// own pool (the engine's g_ThrownLaptops is per-player and slot-limited), so we
// can drop up to CHAOS_MAX_SENTRIES of them. Reset per stage in chraiLuaResetSentries.
#define CHAOS_MAX_SENTRIES 8
static struct autogunobj g_ChaosSentries[CHAOS_MAX_SENTRIES];
static s32 g_ChaosSentryCount = 0;


// pd.spawn_chopper(): a hostile dD hovercopter appears near the player and
// opens fire (the "helicopter helicopter" chaos effect). The spawn_bike
// recipe adapted to OBJTYPE_CHOPPER: runtime template + objInitWithModelDef +
// manual placement, then setup.c's chopper field block with an IDLE ailist
// (choppers run chraiExecute every tick, and a NULL list is not survivable),
// the player as target, and CHOPPERMODE_COMBAT. No patrol path: a path-less
// chopper holds position and engages when its target is visible
// (chopperTickMove's "stay put" branch). EXPLORATORY — the mission choppers
// are ailist-driven, so combat behaviour without a script is best-effort.
static struct chopperobj g_ChaosChoppers[2]; // 0 = dD hovercopter, 1 = A51 interceptor
static s32 g_ChaosChopperSpawned[2] = {0, 0};


// propobj.c's chopper tick asks whether a chopper is one of ours (the chaos
// spawns run GAILIST_IDLE, so the tick drives their see-target/attack loop).
s32 chaosChopperIsChaos(struct chopperobj *chopper)
{
	return chopper == &g_ChaosChoppers[0] || chopper == &g_ChaosChoppers[1];
}

// Which chaos chopper is this? 0 = dD hovercopter, 1 = A51 interceptor, -1 =
// not one of ours. Slot 1 stalks the player (chopperTickCombat); slot 0 keeps
// the original hold-position behaviour.
s32 chaosChopperKind(struct chopperobj *chopper)
{
	if (chopper == &g_ChaosChoppers[0]) {
		return 0;
	}

	if (chopper == &g_ChaosChoppers[1]) {
		return 1;
	}

	return -1;
}

/* ------------------------------------------------------------------------- *
 * Bridges
 * ------------------------------------------------------------------------- */

// Lua bridge: spawn a weapon/item world object at the given chr's location, by
// chrnum. Reuses chrDropItem -- the same engine-blessed path the drop_item AI
// command and chrDie's own loot-drop use -- so object init, model load, and
// floor placement are handled correctly (no manual prop-pos / room math). This
// is the mutating counterpart to the read-only pd.* queries; it is server-side
// only (AI/world mutation must not run on a net client) and a no-op if the
// chrnum is unknown or the weapon has no world model. Returns 1 on success.
//
// Intended for the kill event (spawn a marker where an enemy died); chrDie
// itself drops the chr's weapons at this same point, so spawning here is safe.
s32 chraiLuaSpawnAtChr(s32 chrnum, s32 weaponnum)
{
	struct chrdata *chr;

	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL || chr->prop == NULL) {
		return 0;
	}

	// Drop a weapon as an INDEPENDENT floor pickup at the chr's feet. Do NOT
	// use chrDropItem here: it reparents the item to the chr and defers the
	// actual drop to the chr's death/drop tick, so on a LIVING NPC the weapon
	// stays attached and never lands on the ground (Fire Sale spawned nothing at
	// their feet). chraiLuaSpawnAtPos creates a free-standing, floor-snapped
	// pickup at the given position — exactly what Fire Sale wants.
	return chrsSpawnAtPos(chrnum, weaponnum,
			chr->prop->pos.x, chr->prop->pos.y, chr->prop->pos.z);
}

// pd.chr_yeet(chrnum, force): fling a chr away from the local player with the
// explosion-knockback machinery (chrYeetFromPos). Purely kinetic — no damage.
s32 chraiLuaYeetChr(s32 chrnum, f32 force)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);

	// a NaN or inf would stay in the chr's fallspeed and position
	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL || chr->model == NULL
			|| !isfinite(force)) {
		return 0;
	}
	if (force > 1000.0f) {
		force = 1000.0f;
	} else if (force < -1000.0f) {
		force = -1000.0f;
	}
	chrYeetFromPos(chr, &g_Vars.currentplayer->prop->pos, force);
	return 1;
}

// pd.explosion(chrnum, type): detonate an explosion of the given type at a
// chr's feet, attributed to the local player. Position + rooms come from the
// live prop so the visual/damage register in the right room.
s32 chraiLuaExplodeAtChr(s32 chrnum, s32 type)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);

	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL) {
		return 0;
	}
	if (type <= EXPLOSIONTYPE_NONE || type > EXPLOSIONTYPE_HUGE25) {
		return 0;
	}
	return explosionCreateSimple(NULL, &chr->prop->pos, chr->prop->rooms,
			(s16)type, g_Vars.bondplayernum) ? 1 : 0;
}

// pd.grenade(x, y, z [, chrnum]): drop a LIVE, armed grenade at a position —
// the real engine thrown-grenade path (bgunCreateThrownProjectile2): it lands,
// arms, and the engine detonates it on the grenade's own fuse. Plays the
// pin-pull ping (SFX_05C1 — the sound the throw animation would have played)
// plus the engine's own SFX_THROW. Attributed to the local player. Backs Live
// Grenade / Martyrdom (no Lua-side explosion timing needed — it's real).
//
// chrnum >= 0 seeds the floor-room search from that chr's rooms: martyrdom
// drops at corpses far from the player, where the player's rooms resolve no
// floor and the grenade lands in the wrong room set (never detonating where
// it should). Defaults to the player's rooms for at-your-feet drops.
s32 chraiLuaSpawnGrenade(f32 x, f32 y, f32 z, s32 chrnum)
{
	struct coord pos;
	struct coord vel;
	RoomNum rooms[8];
	f32 floory;
	u16 floorcol;
	s32 floorroom;
	Mtxf mtx;
	struct gset gset;
	struct defaultobj *obj;
	struct chrdata *seedchr = (chrnum >= 0) ? chrFindByLiteralId(chrnum) : NULL;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	pos.x = x;
	pos.y = y;
	pos.z = z;

	if (seedchr && seedchr->prop) {
		roomsCopy(seedchr->prop->rooms, rooms);
	} else {
		roomsCopy(g_Vars.currentplayer->prop->rooms, rooms);
	}
#if VERSION >= VERSION_NTSC_1_0
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, rooms, &floory, &floorcol, NULL);
#else
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, rooms, &floory, &floorcol);
#endif
	if (floorroom > 0) {
		pos.y = floory + 10.0f; // just above the floor so it settles, not clips
		rooms[0] = floorroom;
		rooms[1] = -1;
	}

	gset.weaponnum = WEAPON_GRENADE;
	gset.weaponfunc = FUNC_PRIMARY; // invfunc_grenade_throw
	gset.unk0639 = 0;
	gset.unk063a = 0;

	vel.x = 0.0f;
	vel.y = -1.0f; // drop straight down at the target
	vel.z = 0.0f;
	mtx4LoadIdentity(&mtx);

	obj = bgunCreateThrownProjectile2(g_Vars.currentplayer->prop->chr, &gset,
			&pos, rooms, &mtx, &vel);

	if (obj != NULL) {
		// the pin-pull ping the throw animation would have played
		// (gunscript_playsound(6, SFX_05C1) in invanim_grenade_throw). Played
		// flat from the player's perspective via sndStart — the gunscripts
		// route their sounds the same way, and the psCreate-on-the-projectile
		// attempt was inaudible in testing (fresh projectile props don't
		// register with propsnd this early).
		sndStart(var80095200, SFX_05C1, NULL, -1, -1, -1, -1, -1);
	}

	return obj ? 1 : 0;
}

// pd.headshot_boost(on): Birthday party — headshots land at x10 and every
// head hit emits a "headshot" (chrnum, attackerplayernum) Lua event.
s32 chraiLuaHeadshotBoost(s32 on)
{
	g_ChaosHeadshotBoost = on ? 1 : 0;
	return 1;
}

// pd.chr_armor(chrnum, amount): "Armor Guard" — add body armor to an NPC by
// driving chr->damage negative via chrAddHealth (health beyond maxdamage is
// armor; a negative damage value is the engine's no-flinch body-armor state,
// see add_health_or_armor in commands.h). NPCs only; server-authoritative.
s32 chraiLuaChrArmor(s32 chrnum, f32 amount)
{
	struct chrdata *chr;

	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL) {
		return 0;
	}
	if (chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		return 0; // never armor the player through this path
	}
	chrAddHealth(chr, amount);
	return 1;
}

// pd.chr_armor_clear(chrnum): strip chaos body armor again — used when a timed
// armor effect ends (Armoured Guards).
//
// This ZEROES the negative overflow instead of subtracting the granted amount
// back, and that difference matters: chrAddHealth is a bare
// `chr->damage -= health` with NO clamp, so handing back 30 to a guard who had
// already chewed through part of the armor can land chr->damage >= maxdamage
// without ever routing through chrBeginDeath — a chr that is "dead" but never
// died. Clearing the overflow instead leaves the guard at full health and no
// armor, which is safe from any starting state. Armor already spent
// (damage >= 0) is left exactly as it is: reverting must never retroactively
// injure anyone.
s32 chraiLuaChrArmorClear(s32 chrnum)
{
	struct chrdata *chr;

	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL) {
		return 0;
	}
	if (chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		return 0;
	}
	if (chr->damage < 0.0f) {
		chr->damage = 0.0f;
	}
	return 1;
}

// pd.chr_freeze_one(chrnum | -1): "Weeping Skedar" — statue exactly one chr
// (chr.c anim gate + chrTickShoot fire gate); -1 unfreezes.
s32 chraiLuaChrFreezeOne(s32 chrnum)
{
	g_ChaosFreezeChrnum = chrnum;
	return 1;
}

// pd.chr_cloak(chrnum, on): toggle a chr's cloak (CHRHFLAG_CLOAKED — the same
// bit the cloaking device sets; render + AI treat the chr as cloaked, IR
// scanner still reveals them).
s32 chraiLuaChrCloak(s32 chrnum, s32 on)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);

	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL) {
		return 0;
	}
	if (on) {
		chr->hidden |= CHRHFLAG_CLOAKED;
	} else {
		chr->hidden &= ~CHRHFLAG_CLOAKED;
	}
	return 1;
}

// pd.explosions_around(on): the Air Force One crash sequence — surround the
// local player with staggered random explosions (playerSurroundWithExplosions
// starts the bondexploding loop that playerTickExplode drives; off just
// clears the flag). Damage respects pd.invincible — the chr damage handler
// early-outs on player->invincible, but the explosions still spawn, so the
// self-destruct effect looks lethal without being lethal.
s32 chraiLuaPlayerExplosions(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (on) {
		playerSurroundWithExplosions(0);
	} else {
		g_Vars.currentplayer->bondexploding = false;
	}
	return 1;
}

// pd.nbomb(): detonate an N-Bomb storm on the local player (nbombCreateStorm,
// the same call the thrown N-Bomb's impact makes). Owner is the player, so
// kills it causes are credited to them; the player is inside the storm and
// takes the full disorientation ride.
s32 chraiLuaNbomb(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	nbombCreateStorm(&g_Vars.currentplayer->prop->pos, g_Vars.currentplayer->prop);
	return 1;
}

s32 chraiLuaYassify(s32 on)
{
	g_ChaosYassify = on ? 1 : 0;
	return 1;
}

// pd.spawn_body(bodynum, weaponnum, dx, dz): spawn a HOSTILE chr of the given
// body at the player's position plus a horizontal offset, facing the player,
// already alerted. The chraiLuaSpawnAlly recipe with the allegiance inverted;
// weaponnum -1 spawns unarmed (melee bodies like the mini Skedar claw).
s32 chraiLuaSpawnBody(s32 bodynum, s32 weaponnum, f32 dx, f32 dz, s32 sunglasses, f32 mindist)
{
	struct prop *prop;
	struct chrdata *chr;
	struct coord pos;
	RoomNum spawnrooms[8];
	f32 floory;
	u16 floorcol;
	s32 floorroom;
	u32 spawnflags = SPAWNFLAG_ALLOWONSCREEN;
	s32 headnum = -1; // >= 0 overrides bodyChooseHead (a twin uses Jo's own head)

	// pd.spawn_body(..., sunglasses=true): the head model gets its shades
	// variant (SPAWNFLAG_FORCESUNGLASSES — the setup-file guard mechanism).
	if (sunglasses) {
		spawnflags |= SPAWNFLAG_FORCESUNGLASSES;
	}

	if (apLuaPlayerChr() == NULL) {
		return -1;
	}

	// bodynum -1 = "a copy of the player" (the evil-twin effect): use the
	// player's own body AND head so she actually looks like Jo. (The head was a
	// red herring earlier — the real "goes friendly" bug was the psychosis flag.)
	if (bodynum < 0) {
		bodynum = g_Vars.currentplayer->prop->chr->bodynum;
		headnum = g_Vars.currentplayer->prop->chr->headnum;
	}

	// g_HeadsAndBodies is sized at runtime and nothing downstream bounds it
	if (bodynum < 0 || bodynum >= g_NumHeadsAndBodies) {
		return -1;
	}

	// Force-load the body's model file if it isn't resident on this stage.
	// The spawn path only loads on demand and bails silently if the file
	// isn't in memory — which is why the full-size Skedar (BODY_SKEDAR /
	// FILE_CSKEDAR) never appeared on non-Skedar stages. bodyLoad is a no-op
	// if already loaded; a genuinely absent file still bails downstream.
	if (bodynum >= 0 && bodynum < 152) {
		bodyLoad(bodynum);
	}

	pos.x = g_Vars.currentplayer->prop->pos.x + dx;
	pos.y = g_Vars.currentplayer->prop->pos.y;
	pos.z = g_Vars.currentplayer->prop->pos.z + dz;

	// Floor-snap the spawn: portal-walk from the player's rooms to the real
	// floor room + height at the (possibly far, e.g. 1000-unit) target, so she
	// lands on the ground in the correct room instead of floating or clipping.
	roomsCopy(g_Vars.currentplayer->prop->rooms, spawnrooms);
#if VERSION >= VERSION_NTSC_1_0
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, spawnrooms, &floory, &floorcol, NULL);
#else
	floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, spawnrooms, &floory, &floorcol);
#endif
	if (floorroom > 0) {
		pos.y = floory;
		spawnrooms[0] = floorroom;
		spawnrooms[1] = -1;
	}

#ifndef PLATFORM_N64
	if (!chaosSpawnFindClearPos(&pos, spawnrooms, atan2f(dx, dz))) {
		return -1;
	}

	// Minimum spawn distance: chrAdjustPosForSpawn (inside FindClearPos)
	// SLIDES an invalid far target back toward valid space, which can land it
	// right on top of the player ("Alert! Alert! spawns them on me", user
	// 2026-07-31). Rather than accepting a slid-onto-you spot, FAIL the
	// attempt so the Lua retry loop rolls a fresh angle/distance.
	if (mindist > 0.0f) {
		f32 mdx = pos.x - g_Vars.currentplayer->prop->pos.x;
		f32 mdz = pos.z - g_Vars.currentplayer->prop->pos.z;

		if (mdx * mdx + mdz * mdz < mindist * mindist) {
			return -1;
		}
	}
#endif

	// GAILIST_ALERTED is a real combat list (chase + shoot). The twin is kept
	// locked onto the PLAYER — and hostile — every frame by chaosTwinsHuntPlayer
	// (propsTick), which overrides the AI/team drift that otherwise let her
	// re-pick the nearest guard or turn into a passive blue-reticle buddy.
	// Pass headnum through raw: chrSpawnAtCoord picks a head for headed
	// bodies and (port fix) leaves built-in-head bodies (skedar etc.)
	// headless instead of wedging a human head on — that made
	// pd.spawn_body(SKEDARKING/MINISKEDAR) fail outright.
	prop = chrSpawnAtCoord(bodynum, headnum, &pos,
			spawnrooms,
			atan2f(-dx, -dz), // face inward toward the player
			ailistFindById(GAILIST_ALERTED),
			spawnflags);

	if (prop == NULL || prop->chr == NULL) {
		return -1;
	}

	chr = prop->chr;
	chr->flags |= CHRFLAG0_SKIPSAFETYCHECKS;
	// Put her on the GUARDS' side so the PLAYER is her only enemy. Then her own
	// AI acquires the player through normal sight/detection and fights properly
	// (forcing chr->target instead just made her replay the "spotted you"
	// reaction forever without firing — the combat AI rejects a target it did
	// not acquire itself). Copy the team of an existing chr that is hostile to
	// the player: that team makes the player an enemy AND the guards allies.
	// Skip TEAM_ALLY (0x10) — that team is the follow-Bond buddy. If nothing
	// suitable is found, fall back to a hostile team the player doesn't share.
	{
		struct chrdata *plchr = g_Vars.currentplayer->prop->chr;
		u8 twinteam = 0;
		s32 numslots = chrsGetNumSlots();
		s32 si;

		for (si = 0; si < numslots; si++) {
			s32 cn = (s32)chrslotGet(si)->chrnum;
			struct chrdata *other = (cn < 0) ? NULL : chrFindByLiteralId(cn);

			if (other != NULL && other != plchr && other != chr && other->prop != NULL
					&& other->prop->type == PROPTYPE_CHR && other->team != 0
					&& other->team != TEAM_ALLY && !chrIsDead(other)
					&& chrCompareTeams(other, plchr, COMPARE_ENEMIES)) {
				twinteam = other->team;
				break;
			}
		}

		if (twinteam == 0) {
			u8 plteam = plchr->team;
			twinteam = TEAM_ENEMY;
			if (twinteam & plteam) twinteam = TEAM_01;
			if (twinteam & plteam) twinteam = TEAM_04;
			if (twinteam & plteam) twinteam = TEAM_20;
		}
		chr->team = twinteam;
	}
	chr->voicebox = VOICEBOX_FEMALE; // Jo twin
	chr->squadron = SQUADRON_01;
	// NB: do NOT set CHRHFLAG_DETECTED here — that bit (0x80000000) means
	// "detected" only on a PLAYER; on an AI chr the very same bit is
	// CHRHFLAG_PSYCHOSISED, so setting it made the twin psychotic at spawn
	// (GAILIST_ALERTED -> GAILIST_INIT_PSYCHOSIS, which rewrites her team to
	// TEAM_NONCOMBAT then TEAM_ALLY = friendly). This was THE "goes friendly" bug.
	chr->teamscandist = 50;
	chr->accuracyrating = 100;
	chr->speedrating = 100;
	chrAddHealth(chr, 20);
	chrSetMaxDamage(chr, 4);
	chr->chrflags |= CHRCFLAG_NEVERSLEEP;

	if (weaponnum >= 0) {
		s32 modelnum = playermgrGetModelOfWeapon(weaponnum);
		if (modelnum >= 0) {
			chrGiveWeapon(chr, modelnum, weaponnum, 0);
		}
	}

	// Point her at the player and alert her so she engages immediately; with the
	// player as her only enemy (guard team above) she stays on him and fires.
	chr->target = propGetIndexByChrId(chr, CHR_BOND);
	chr->alertness = 100;
	chr->chrflags |= CHRCFLAG_TRIGGERSHOTLIST;

	// Register the twin so chaosIsTwin() keeps her psychosis-immune when the
	// player damages her (otherwise she flips to the friendly psychosised AI).
	{
		s32 ti;
		for (ti = 0; ti < (s32)ARRAYCOUNT(g_ChaosTwinChrnums); ti++) {
			if (g_ChaosTwinChrnums[ti] < 0) {
				g_ChaosTwinChrnums[ti] = chr->chrnum;
				break;
			}
		}
	}

	return chr->chrnum;
}

// pd.clone_chr(chrnum, x, y, z): spawn a COPY of a chr at a position — Hydra's
// "every guard you kill splits into two more, right where it fell".
//
// Cloned: body, head, and the ACTION BLOCK (chr->ailist, re-resolved by id so the
// copy runs the dead guard's own AI script rather than a generic combat list),
// plus team, squadron, voicebox and the weapon it was carrying. That is what
// makes the offspring a real guard of that type instead of a reskin — a Skedar
// clone behaves like a Skedar, a lab tech like a lab tech.
//
// ⚠ Call this from a TICK, never from the "kill" event: it allocates a chrslot
// and inserts a prop, and the death callback runs from inside chrDamage, itself
// inside the prop tick — growing the prop list mid-iteration is the documented
// corruption family (docs/PORT_NET_CRASH_LEDGER.md). The caller must therefore
// snapshot the position at kill time, since a corpse is yeeted away from where it
// died and may be reaped before the tick runs; the TEMPLATE has to be read here
// though, so pass a chrnum that is still alive-or-corpse, not a reaped slot.
//
// Returns the new chrnum, or -1 if the template is gone or nowhere near the
// position has room for a body (see chaosSpawnFindClearPos).
s32 chraiLuaCloneChr(s32 chrnum, f32 x, f32 y, f32 z)
{
	struct chrdata *src;
	struct chrdata *chr;
	struct prop *prop;
	struct coord pos;
	RoomNum spawnrooms[8];
	s32 ailistid;
	s32 srcweapon = -1;
	s32 bodynum;
	s32 headnum;
	f32 angle;

	if (apLuaPlayerChr() == NULL) {
		return -1;
	}

	src = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);

	if (src == NULL || src->prop == NULL) {
		return -1;
	}

	bodynum = src->bodynum;
	headnum = src->headnum;
	ailistid = chraiLuaGetListId(src->ailist);

	// ⚠ The template is normally a chr that just DIED, and death can already have
	// replaced its script: chrDie swaps a sim's list to GAILIST_AIBOT_DEAD, and a
	// reaped chr has ailist == NULL (which getListId reports as -1). Cloning
	// either would hand the offspring a corpse script — it would spawn and
	// immediately lie down. Fall back to a real combat list in both cases.
	// A campaign guard's own list is untouched by death, which is the case that
	// matters here and the whole point of cloning it.
	if (src->aibot || ailistid == GAILIST_AIBOT_DEAD) {
		ailistid = -1;
	}

	// Remember what it was holding so the copy is armed the same way. The corpse
	// has usually dropped the object by now, so read the weapon NUMBER off the
	// held slots while they are still populated and re-give it by model below.
	{
		struct prop *wp = chrGetHeldProp(src, HAND_RIGHT);

		if (wp == NULL || wp->weapon == NULL) {
			wp = chrGetHeldProp(src, HAND_LEFT);
		}
		if (wp && wp->weapon) {
			srcweapon = wp->weapon->weaponnum;
		}
	}

	// The model has to be resident before the spawn (chrSpawnAtCoord loads on
	// demand and bails silently otherwise) — the chraiLuaSpawnBody precedent.
	if (bodynum >= 0 && bodynum < 152) {
		bodyLoad(bodynum);
	}

	pos.x = x;
	pos.y = y;
	pos.z = z;

	// Seed the room search from the SOURCE chr's rooms, not the player's: the
	// corpse is what is near this position, and the player may be rooms away.
	roomsCopy(src->prop->rooms, spawnrooms);

	// Face the clone at the player, and bias the clearance nudge away from them.
	angle = atan2f(g_Vars.currentplayer->prop->pos.x - x,
			g_Vars.currentplayer->prop->pos.z - z);

	if (!chaosSpawnFindClearPos(&pos, spawnrooms, angle + M_PI)) {
		return -1;
	}

	prop = chrSpawnAtCoord(bodynum, headnum, &pos, spawnrooms, angle,
			(ailistid >= 0) ? ailistFindById(ailistid) : ailistFindById(GAILIST_ALERTED),
			SPAWNFLAG_ALLOWONSCREEN);

	if (prop == NULL || prop->chr == NULL) {
		return -1;
	}

	chr = prop->chr;
	chr->flags |= CHRFLAG0_SKIPSAFETYCHECKS;
	chr->team = src->team;
	chr->squadron = src->squadron;
	chr->voicebox = src->voicebox;
	chr->chrflags |= CHRCFLAG_NEVERSLEEP;

	if (srcweapon >= 0) {
		s32 modelnum = playermgrGetModelOfWeapon(srcweapon);

		if (modelnum >= 0) {
			chrGiveWeapon(chr, modelnum, srcweapon, 0);
		}
	}

	// Awake and already looking for you: a head that spawns idle just stands in
	// the corpse pile, which reads as broken rather than as a hydra.
	chr->alertness = 100;
	chr->chrflags |= CHRCFLAG_TRIGGERSHOTLIST;
	return chr->chrnum;
}

// pd.body_snatch(chrnum): "lite" Counter-Op takeover for the solo campaign.
// The full playerSpawnAnti takeover needs the player to have a third-person chr
// body model, which the first-person solo player lacks and can't build cleanly
// mid-mission (the gunmem body-swap is cutscene-only machinery and stalls in
// active gameplay). So instead of becoming the guard's model we take its PLACE:
// remember where the player was (for pd.body_unsnatch), give the player the
// guard's weapon, warp onto it, mark the player disguised, and remove the guard.
// The chaos.lua effect also calms every chr so nobody aggros, and pd.body_unsnatch
// (called when the timer ends) un-disguises and teleports the player home. Solo
// only. Returns 1 on success, 0 if the target can't be snatched.
s32 chraiLuaBodySnatch(s32 chrnum)
{
	struct chrdata *chr;
	struct chrdata *pl;
	struct prop *gunprop;
	s32 weaponnum;

	if (apLuaPlayerChr() == NULL || g_Vars.normmplayerisrunning) {
		return 0;
	}
	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL || chr->prop == NULL || chr->prop->type != PROPTYPE_CHR
			|| chr->model == NULL || chrIsDead(chr)
			|| chr->prop == g_Vars.currentplayer->prop) {
		return 0;
	}

	pl = g_Vars.currentplayer->prop->chr;

	// Remember the pre-snatch position so the timed effect can return here.
	g_ChaosSnatchHome = g_Vars.currentplayer->prop->pos;
	roomsCopy(g_Vars.currentplayer->prop->rooms, g_ChaosSnatchHomeRooms);
	g_ChaosSnatchActive = 1;

	// Take the guard's weapon.
	gunprop = chrGetHeldProp(chr, HAND_RIGHT);
	weaponnum = (gunprop && gunprop->weapon) ? gunprop->weapon->weaponnum : WEAPON_UNARMED;
	if (weaponnum > WEAPON_UNARMED) {
		invGiveSingleWeapon(weaponnum);
		bgunEquipWeapon2(HAND_RIGHT, weaponnum);
		chrsGiveMags(CHAOS_GUN_MAGS);
	}

	// Warp onto the guard.
	chaosPlayerWarp(&chr->prop->pos, chr->prop->rooms);

	// Disguise: the target-selection search (chraicommands.c) skips chrs with
	// CHRHFLAG_DISGUISED, so guards won't acquire the player. Also set the
	// struct-player flag the outfit code reads. (chaos.lua calms everyone too.)
	if (pl != NULL) {
		pl->hidden |= CHRHFLAG_DISGUISED;
	}
	g_Vars.currentplayer->disguised = true;

	// Remove the snatched guard — the playerSpawnAnti host-teardown sequence.
	chrRemove(chr->prop, true);
	propDeregisterRooms(chr->prop);
	propDelist(chr->prop);
	propDisable(chr->prop);
	propFree(chr->prop);
	return 1;
}

// pd.body_unsnatch(): end the snatch — drop the disguise so guards behave
// normally again, and teleport the player back to where they were. Called from
// the body_snatch effect's stop() when the timer ends.
s32 chraiLuaBodyUnsnatch(void)
{
	struct chrdata *pl;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	pl = g_Vars.currentplayer->prop->chr;
	if (pl != NULL) {
		pl->hidden &= ~CHRHFLAG_DISGUISED;
	}
	g_Vars.currentplayer->disguised = false;

	if (g_ChaosSnatchActive) {
		chaosPlayerWarp(&g_ChaosSnatchHome, g_ChaosSnatchHomeRooms);
		g_ChaosSnatchActive = 0;
	}
	return 1;
}

s32 chraiLuaBloodColour(s32 r, s32 g, s32 b, s32 on)
{
	if (!on) {
		g_ChaosBloodColour = 0;
	} else {
		// low bit doubles as the "set" flag so pure black still registers
		g_ChaosBloodColour = ((u32)(r & 0xff) << 24) | ((u32)(g & 0xff) << 16)
				| ((u32)(b & 0xff) << 8) | 1;
	}
	return 1;
}

s32 chraiLuaMaxBlood(s32 on)
{
	g_ChaosMaxBlood = on ? 1 : 0;
	return 1;
}

// pd.chr_target(chrnum, victimchrnum): point a chr's combat AI at another chr
// (the aiSetTargetChr recipe: target index + the trigger-shot flag + full
// alertness). Backs the "Civil war" infighting effect.
s32 chraiLuaChrTarget(s32 chrnum, s32 victimchrnum)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);
	struct chrdata *victim = chrFindByLiteralId(victimchrnum);

	if (chr == NULL || victim == NULL
			|| victim->prop == NULL || chr == victim || chrIsDead(chr) || chrIsDead(victim)) {
		return 0;
	}
	chr->target = propGetIndexByChrId(chr, victim->chrnum);
	chr->alertness = 100;
	chr->chrflags |= CHRCFLAG_TRIGGERSHOTLIST;
	return 1;
}

// pd.chr_calm(chrnum): the neuralyzer — drop a chr's alertness to zero, clear
// its target and the trigger-shot flag. The chr doesn't rewind to its patrol
// script, but it stops hunting until re-provoked.
s32 chraiLuaChrCalm(s32 chrnum)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);

	if (chr == NULL || chrIsDead(chr)) {
		return 0;
	}
	chr->alertness = 0;
	chr->target = -1;
	chr->chrflags &= ~CHRCFLAG_TRIGGERSHOTLIST;
	return 1;
}

s32 chraiLuaCivilWar(s32 on)
{
	struct chrdata *pl = apLuaPlayerChr();
	s32 n, i;

	if (pl == NULL) {
		return 0;
	}

	if (!on) {
		for (i = 0; i < g_ChaosCivilWarCount; i++) {
			struct chrdata *chr = chrFindByLiteralId(g_ChaosCivilWarChr[i]);
			if (chr != NULL) {
				chr->team = g_ChaosCivilWarTeam[i];
				chr->chrflags &= ~CHRCFLAG_TRIGGERSHOTLIST;
			}
		}
		g_ChaosCivilWarCount = 0;
		return 1;
	}

	// Turn ONLY hostile combat guards on each other. Two hard rules learned the
	// hard way: (1) never touch non-combatants / mission NPCs — forcing team +
	// alert onto them breaks their scripted action block irreversibly and
	// softlocks the mission; so skip TEAM_NONCOMBAT and anything that isn't an
	// enemy of the player. (2) Split the guards into distinct team bits so each
	// registers the others as enemies, but ONLY bits 0-6 — bit 7 (0x80) IS
	// TEAM_NONCOMBAT and would turn a guard into a passive non-combatant. Then
	// kick CHRCFLAG_TRIGGERSHOTLIST (guards have no mission script to break) so
	// idle ones scan; their own AI acquires + engages the nearest enemy guard.
	n = chraiLuaGetChrSlotCount();

	for (i = 0; i < n && g_ChaosCivilWarCount < CHAOS_CIVILWAR_MAX; i++) {
		s32 cn = chraiLuaGetChrNumBySlot(i);
		struct chrdata *chr = (cn < 0) ? NULL : chrFindByLiteralId(cn);
		s32 k;
		bool known = false;

		if (chr == NULL || chr->prop == NULL || chr->prop->type != PROPTYPE_CHR
				|| chr->model == NULL || chrIsDead(chr) || chr == pl) {
			continue;
		}
		// Combat guards only — never non-combatants / allies / mission NPCs.
		if ((chr->team & TEAM_NONCOMBAT) || !chrCompareTeams(pl, chr, COMPARE_ENEMIES)) {
			continue;
		}
		for (k = 0; k < g_ChaosCivilWarCount; k++) {
			if (g_ChaosCivilWarChr[k] == chr->chrnum) {
				known = true;
				break;
			}
		}
		if (!known) {
			g_ChaosCivilWarChr[g_ChaosCivilWarCount] = chr->chrnum;
			g_ChaosCivilWarTeam[g_ChaosCivilWarCount] = chr->team;
			chr->team = (u8)(1 << (g_ChaosCivilWarCount % 7)); // bits 0-6 only
			g_ChaosCivilWarCount++;
		}

		chr->chrflags |= CHRCFLAG_TRIGGERSHOTLIST;
	}

	return g_ChaosCivilWarCount > 0;
}

// pd.chr_summon(chrnum, dx, dz): teleport a chr to the player's position plus
// a horizontal offset — chrMoveToPos, the ground-validated primitive the
// Counter-Op spawn uses (rooms come from the player, so cross-map summons
// register correctly). Fails cleanly if the spot doesn't validate.
s32 chraiLuaChrSummon(s32 chrnum, f32 dx, f32 dz)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);
	struct coord pos;

	if (apLuaPlayerChr() == NULL
			|| chr == NULL || chr->prop == NULL || chrIsDead(chr)
			|| chr->prop == g_Vars.currentplayer->prop) {
		return 0;
	}
	pos.x = g_Vars.currentplayer->prop->pos.x + dx;
	pos.y = g_Vars.currentplayer->prop->pos.y;
	pos.z = g_Vars.currentplayer->prop->pos.z + dz;
	return chrMoveToPos(chr, &pos, g_Vars.currentplayer->prop->rooms,
			atan2f(-dx, -dz), false) ? 1 : 0;
}

// pd.one_punch(on): a player's unarmed strikes become lethal-through-armour
// and launch the victim flying (the chrDamage boost near the top of this
// file). Pair with CHEAT_FISTS + forced-unarmed for the full Saitama.
s32 chraiLuaOnePunch(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	g_ChaosOnePunch = on ? 1 : 0;
	return 1;
}

// pd.space_program(on): every player bullet becomes a one-hit-kill launcher
// (chrDamage reads g_ChaosSpaceProgram). Like one_punch, but for guns.
s32 chraiLuaSpaceProgram(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	g_ChaosSpaceProgram = on ? 1 : 0;
	return 1;
}

// pd.frag_out(on): human enemies throw grenades whenever they would fire
// (chrConsiderGrenadeThrow reads g_ChaosFragOut). No player pawn needed to clear
// it, so an /chaos off from the hub still turns it back off.
s32 chraiLuaFragOut(s32 on)
{
	g_ChaosFragOut = on ? 1 : 0;
	return 1;
}

s32 chraiLuaSpawnBike(void)
{
	static const struct hoverbikeobj zerobike; // BSS zero template for reinit
	struct hoverbikeobj *bike = &g_ChaosBike;
	struct defaultobj *obj = &bike->base;
	struct coord pos;
	Mtxf mtx;
	RoomNum seedrooms[8];
	f32 floory;
	struct modelrodata_bbox *bbox;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	pos.x = g_Vars.currentplayer->prop->pos.x + g_Vars.currentplayer->bond2.heading.x * 80.0f;
	pos.y = g_Vars.currentplayer->prop->pos.y;
	pos.z = g_Vars.currentplayer->prop->pos.z + g_Vars.currentplayer->bond2.heading.z * 80.0f;
	// Materialise above the player's feet (user-tuned 2026-07-28: 85 waist →
	// 160 head → settled at 120): both placement paths below rest the bike's
	// bbox min at pos.y, so lifting here spawns it mid-air and OBJFLAG_FALL
	// settles it onto the floor instead of it popping up embedded at ground
	// level.
	pos.y += 120.0f;
	mtx4LoadIdentity(&mtx);
	roomsCopy(g_Vars.currentplayer->prop->rooms, seedrooms);

	// Already spawned this stage (prop still points back at us — a stage
	// unload recycles the prop pool, which breaks this backlink): just
	// summon the existing bike back to the player.
	if (g_ChaosBikeSpawned && obj->prop && obj->prop->obj == obj
			&& obj->prop->type == PROPTYPE_OBJ && obj->model) {
		bbox = modelFindBboxRodata(obj->model);
		// Sets obj->floorcol (floor shading) via the out-param.
		cdFindFloorRoomYColourFlagsAtPos(&pos, seedrooms, &floory, &obj->floorcol, NULL);
		// Rest on the player's own floor (pos.y) in the player's rooms — see the
		// fresh-spawn path for why the snapped floor is unreliable.
		{
			struct coord placepos;
			placepos.x = pos.x;
			placepos.y = pos.y - objGetRotatedLocalYMinByMtx4(bbox, &mtx) * obj->model->scale;
			placepos.z = pos.z;
			func0f06a580(obj, &placepos, &mtx, seedrooms);
		}
		return 1;
	}

	// Fresh spawn: build the template (the setup.c OBJTYPE_HOVERBIKE recipe,
	// minus the pad — we place manually like chraiLuaSpawnAtPos).
	*bike = zerobike;
	obj->extrascale = 128; // HALF SIZE
	obj->type = OBJTYPE_HOVERBIKE;
	obj->modelnum = MODEL_HOVBIKE;
	obj->pad = -1;
	obj->flags = OBJFLAG_FALL;
	obj->flags3 = OBJFLAG3_GEOCYL; // bikes use the cylinder geo
	obj->realrot[0][0] = 1;
	obj->realrot[1][1] = 1;
	obj->realrot[2][2] = 1;
	obj->maxdamage = 1000;
	obj->shadecol[0] = obj->shadecol[1] = obj->shadecol[2] = 0xff;
	obj->nextcol[0] = obj->nextcol[1] = obj->nextcol[2] = 0xff;
	obj->floorcol = 0x0fff;

	if (!setupLoadModeldef(MODEL_HOVBIKE)) {
		return 0;
	}
	if (objInitWithModelDef(obj, g_ModelStates[MODEL_HOVBIKE].modeldef) == NULL || obj->model == NULL) {
		return 0;
	}
	modelSetScale(obj->model, obj->model->scale * (obj->extrascale * (1.0f / 256.0f)));
	setupCreateHov(obj, &bike->hov);

	bbox = modelFindBboxRodata(obj->model);
	// Sets obj->floorcol (floor shading) via the out-param.
	cdFindFloorRoomYColourFlagsAtPos(&pos, seedrooms, &floory, &obj->floorcol, NULL);
	// Rest the bike on the PLAYER's own floor level (pos.y) in the player's rooms
	// — NOT the snapped floor. A floor-snap at the offset can portal-walk to a
	// much lower floor over a ledge and drop the bike out of sight (WAR put it
	// ~130u below the player, on an elevated deck). Lift by the scaled bbox min so
	// it rests on the surface; OBJFLAG_FALL settles any small mismatch.
	{
		struct coord placepos;
		placepos.x = pos.x;
		placepos.y = pos.y - objGetRotatedLocalYMinByMtx4(bbox, &mtx) * obj->model->scale;
		placepos.z = pos.z;
		func0f06a580(obj, &placepos, &mtx, seedrooms);
	}

	// Register the bike's prop into the active/rendered prop list. objInit only
	// allocates the prop; the setup.c object recipe calls propActivate+propEnable
	// after placement (setup.c:1199) — without them the bike exists but never
	// ticks or renders (spawns "nothing"). This was the missing step.
	propActivate(obj->prop);
	propEnable(obj->prop);

	g_ChaosBikeSpawned = 1;
	return 1;
}

void chraiLuaResetSentries(void)
{
	// A stage unload recycles the prop/model pools, so the objects are already
	// gone — just drop our count so the next stage can spawn a fresh batch.
	g_ChaosSentryCount = 0;

	// Same for the bike and choppers: their obj->prop points into the old
	// stage's prop pool, so don't let the next spawn dereference it.
	g_ChaosBikeSpawned = 0;
	g_ChaosChopperSpawned[0] = 0;
	g_ChaosChopperSpawned[1] = 0;
}

// pd.spawn_sentry(dx, dz) -> bool. Deploy a laptop sentry gun (MODEL_CHRAUTOGUN,
// OBJTYPE_AUTOGUN) at the player's position plus a horizontal offset, floor-
// snapped, hostile to the PLAYER (targetteam = the player's team, which is how
// the autogun's target scan selects who to shoot). The laptop-sentry field
// values (aim range/speed, full rotation) mirror laptopDeploy; storage is our
// own pool so several can coexist. Solo only; returns 1 on success.
s32 chraiLuaSpawnSentry(f32 dx, f32 dz)
{
	struct chrdata *plchr = apLuaPlayerChr();
	struct autogunobj *gun;
	struct defaultobj *obj;
	struct coord pos;
	Mtxf mtx;
	RoomNum seedrooms[8];
	f32 floory;
	u16 floorcol;
	s32 floorroom;
	struct modelrodata_bbox *bbox;
	struct coord mountnormal;

	if (plchr == NULL) {
		return 0;
	}
	if (g_ChaosSentryCount >= CHAOS_MAX_SENTRIES) {
		return 0;
	}

	gun = &g_ChaosSentries[g_ChaosSentryCount];
	obj = &gun->base;

	roomsCopy(g_Vars.currentplayer->prop->rooms, seedrooms);
	mtx4LoadIdentity(&mtx);
	mountnormal.x = 0.0f;
	mountnormal.y = 1.0f;
	mountnormal.z = 0.0f;

	// Cast a ray from the player toward this ring direction (with a random
	// vertical tilt, so some rays find the FLOOR/CEILING and others WALLS) and
	// mount the sentry flush on the first surface hit, oriented to its normal —
	// like a deployed laptop sentry. If the ray hits nothing (open direction),
	// fall back to floor-snapping at the ring offset.
	{
		struct coord raystart, rayend, dir, n;
		f32 m, vy;

		m = sqrtf(dx * dx + dz * dz);
		if (m < 1.0f) {
			m = 1.0f;
		}
		dir.x = dx / m;
		dir.z = dz / m;
		vy = ((s32)(rngRandom() % 2001) - 1000) * 0.0012f; // ~[-1.2, 1.2] vertical tilt
		dir.y = vy;
		m = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		dir.x /= m;
		dir.y /= m;
		dir.z /= m;

		raystart.x = g_Vars.currentplayer->prop->pos.x;
		raystart.y = g_Vars.currentplayer->prop->pos.y + 100.0f;
		raystart.z = g_Vars.currentplayer->prop->pos.z;
		rayend.x = raystart.x + dir.x * 2500.0f;
		rayend.y = raystart.y + dir.y * 2500.0f;
		rayend.z = raystart.z + dir.z * 2500.0f;

		if (cdExamLos08(&raystart, seedrooms, &rayend, CDTYPE_BG, GEOFLAG_BLOCK_SHOOT) == CDRESULT_COLLISION) {
			struct coord right, fwd, ref;

			cdGetPos(&pos, __LINE__, "chraction.c"); // surface hit point
			cdGetObstacleNormal(&n);

			m = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
			if (m < 0.0001f) {
				n.x = 0.0f; n.y = 1.0f; n.z = 0.0f;
			} else {
				n.x /= m; n.y /= m; n.z /= m;
			}
			// Point the normal back toward the room (opposite the ray) so the
			// sentry mounts ON the surface, not buried in it.
			if (n.x * dir.x + n.y * dir.y + n.z * dir.z > 0.0f) {
				n.x = -n.x; n.y = -n.y; n.z = -n.z;
			}

			// Orthonormal basis with local Y (the autogun's up/mount axis) = normal,
			// so its base sits against the surface and it points into the room.
			if (fabsf(n.y) < 0.99f) {
				ref.x = 0.0f; ref.y = 1.0f; ref.z = 0.0f;
			} else {
				ref.x = 1.0f; ref.y = 0.0f; ref.z = 0.0f;
			}
			right.x = ref.y * n.z - ref.z * n.y;
			right.y = ref.z * n.x - ref.x * n.z;
			right.z = ref.x * n.y - ref.y * n.x;
			m = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
			right.x /= m; right.y /= m; right.z /= m;
			fwd.x = right.y * n.z - right.z * n.y;
			fwd.y = right.z * n.x - right.x * n.z;
			fwd.z = right.x * n.y - right.y * n.x;

			mtx.m[0][0] = right.x; mtx.m[0][1] = right.y; mtx.m[0][2] = right.z;
			mtx.m[1][0] = n.x;     mtx.m[1][1] = n.y;     mtx.m[1][2] = n.z;
			mtx.m[2][0] = fwd.x;   mtx.m[2][1] = fwd.y;   mtx.m[2][2] = fwd.z;
			mountnormal = n;
		} else {
			// No surface hit: floor-snap at the ring offset (original behaviour).
			pos.x = g_Vars.currentplayer->prop->pos.x + dx;
			pos.y = g_Vars.currentplayer->prop->pos.y;
			pos.z = g_Vars.currentplayer->prop->pos.z + dz;
#if VERSION >= VERSION_NTSC_1_0
			floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, seedrooms, &floory, &floorcol, NULL);
#else
			floorroom = cdFindFloorRoomYColourFlagsAtPos(&pos, seedrooms, &floory, &floorcol);
#endif
			if (floorroom > 0) {
				pos.y = floory;
				seedrooms[0] = floorroom;
				seedrooms[1] = -1;
			}
		}
	}

	// Build the autogun object template (the laptopDeploy defaultobj recipe).
	{
		static const struct autogunobj zerogun; // BSS zero template
		*gun = zerogun;
	}
	obj->extrascale = 256;
	obj->type = OBJTYPE_AUTOGUN;
	obj->modelnum = MODEL_CHRAUTOGUN;
	obj->pad = -1;
	obj->flags = 0;
	// Orient the object to the mount surface (identity = upright on a floor).
	obj->realrot[0][0] = mtx.m[0][0]; obj->realrot[0][1] = mtx.m[0][1]; obj->realrot[0][2] = mtx.m[0][2];
	obj->realrot[1][0] = mtx.m[1][0]; obj->realrot[1][1] = mtx.m[1][1]; obj->realrot[1][2] = mtx.m[1][2];
	obj->realrot[2][0] = mtx.m[2][0]; obj->realrot[2][1] = mtx.m[2][1]; obj->realrot[2][2] = mtx.m[2][2];
	obj->maxdamage = 1000;
	obj->shadecol[0] = obj->shadecol[1] = obj->shadecol[2] = 0xff;
	obj->nextcol[0] = obj->nextcol[1] = obj->nextcol[2] = 0xff;
	obj->floorcol = 0x0fff;

	// setupLoadModeldef returns true only when it actually LOADS the modeldef and
	// false when it's already resident — NOT a success/failure flag (all canonical
	// callers ignore it). Treating false as failure meant only the FIRST sentry
	// spawned; the rest found the model already loaded and bailed. Load (if needed)
	// then check the modeldef is actually available.
	setupLoadModeldef(MODEL_CHRAUTOGUN);
	if (g_ModelStates[MODEL_CHRAUTOGUN].modeldef == NULL) {
		return 0;
	}
	if (objInitWithModelDef(obj, g_ModelStates[MODEL_CHRAUTOGUN].modeldef) == NULL || obj->model == NULL) {
		return 0;
	}

	// Autogun runtime state (setupCreateAutogun + laptopDeploy values). The
	// beam is the aiming laser; MEMPOOL_STAGE is freed with the stage.
	gun->targetpad = -1;
	gun->aimdist = 5000.0f;
	gun->maxspeed = PALUPF(0.0697f);
	gun->ymaxleft = 12.56f;
	gun->ymaxright = -12.56f;
	gun->firecount = 0;
	gun->lastseebond60 = -1;
	gun->lastaimbond60 = -1;
	gun->allowsoundframe = -1;
	gun->yrot = gun->yspeed = gun->yzero = 0;
	gun->xrot = gun->xspeed = gun->xzero = 0;
	gun->barrelspeed = gun->barrelrot = 0;
	gun->firing = false;
	gun->shotbondsum = 0;
	gun->target = NULL;
	gun->nextchrtest = 0;
	gun->ammoquantity = 255;
	// Hostile to the player. The autogun target scan (autogunTick) only runs when
	// targetteam != 0 (else the gun sits idle), and matches victims by team bits
	// when MP teams are on — so prefer the player's own team, but fall back to
	// "all teams" (0xffff) when the player's team is 0 (offline Combat Sim with
	// teams off), which keeps the scan alive and, with teams off, targets everyone.
	gun->targetteam = plchr->team ? plchr->team : 0xffff;
	gun->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
	if (gun->beam) {
		gun->beam->age = -1;
	}

	// Seat the base flush on the mount surface: push the origin out along the
	// surface normal by the model's base offset (bbox->ymin, the local mount axis).
	// For a floor (normal = +Y) this is exactly the old floor lift; for a wall or
	// ceiling it lifts along that surface's normal instead of world-down.
	bbox = modelFindBboxRodata(obj->model);
	{
		struct coord placepos;
		f32 lift = bbox->ymin * obj->model->scale;
		placepos.x = pos.x - lift * mountnormal.x;
		placepos.y = pos.y - lift * mountnormal.y;
		placepos.z = pos.z - lift * mountnormal.z;
		// Bake model->scale into the rotation before func0f06a580 copies it into
		// realrot — the autogun BASE renders straight from realrot with NO
		// model->scale (objInitMatrices), while the turret DOES get it
		// (autogunInitMatrices), so without this the base is huge next to a
		// normal turret. This mirrors the real laptop throw (bgun0f09ebcc).
		mtx00015f04(obj->model->scale, &mtx);
		func0f06a580(obj, &placepos, &mtx, seedrooms);
	}
	if (obj->prop) {
		obj->prop->forcetick = true; // tick + fire even while off-screen
		// No owning player. In MP the autogun scan SKIPS its owner player
		// (the owner nibble in obj->hidden), which defaults to 0 = the human
		// player (slot 0), so without this the sentry never targets you. Kai
		// set a port-side prop owner of -2 (owned by nobody); this build has
		// only the nibble, so 15 — no combatant index — does the same.
		obj->hidden |= 0xf0000000;
		propActivate(obj->prop);
		propEnable(obj->prop);
	}

	g_ChaosSentryCount++;
	return 1;
}

s32 chraiLuaSpawnChopper(s32 kind, s32 extrascale)
{
	static const struct chopperobj zerochopper;
	struct chopperobj *chopper;
	struct defaultobj *obj;
	struct coord pos;
	Mtxf mtx;
	RoomNum seedrooms[8];
	f32 floory;
	u16 floorcol;
	s32 modelnum;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	kind = (kind == 1) ? 1 : 0;
	modelnum = kind ? MODEL_A51INTERCEPTOR : MODEL_DD_HOVERCOPTER;
	// The interceptor's MODELDEF is natively ~0.1 scale (the chopper gunfire
	// code corrects its gun position by 0.1/model->scale), so extrascale 256
	// = authored size — scaling it further down made it near-invisible. The
	// dD copter is 1:1 at 256 (chaos runs it quarter size). Clamp so a bad
	// script value can't produce a zero-scale model.
	if (extrascale < 8) {
		extrascale = kind ? 1024 : 64;
	} else if (extrascale > 2048) {
		extrascale = 2048;
	}
	chopper = &g_ChaosChoppers[kind];
	obj = &chopper->base;

	// ~350 units ahead of the player, hovering 200 above their floor
	pos.x = g_Vars.currentplayer->prop->pos.x + g_Vars.currentplayer->bond2.heading.x * 350.0f;
	pos.y = g_Vars.currentplayer->prop->pos.y + 200.0f;
	pos.z = g_Vars.currentplayer->prop->pos.z + g_Vars.currentplayer->bond2.heading.z * 350.0f;
	mtx4LoadIdentity(&mtx);
	roomsCopy(g_Vars.currentplayer->prop->rooms, seedrooms);

	// Already spawned this stage (prop backlink still valid): summon it back
	// to the player and re-aggro.
	if (g_ChaosChopperSpawned[kind] && obj->prop && obj->prop->obj == obj
			&& obj->prop->type == PROPTYPE_OBJ && obj->model
			&& chopper->attackmode != CHOPPERMODE_FALL
			&& chopper->attackmode != CHOPPERMODE_DEAD) {
		func0f06a580(obj, &pos, &mtx, seedrooms);
		chopper->target = g_Vars.currentplayer->prop - g_Vars.props;
		chopper->attackmode = CHOPPERMODE_COMBAT;
		chopper->patroltimer60 = TICKS(240);
		return 1;
	}

	*chopper = zerochopper;
	obj->extrascale = (u16)extrascale;
	obj->type = OBJTYPE_CHOPPER;
	obj->modelnum = (s16)modelnum;
	obj->pad = -1;
	obj->flags = OBJFLAG_CHOPPER_INIT;
	obj->realrot[0][0] = 1;
	obj->realrot[1][1] = 1;
	obj->realrot[2][2] = 1;
	obj->maxdamage = 1000;
	obj->shadecol[0] = obj->shadecol[1] = obj->shadecol[2] = 0xff;
	obj->nextcol[0] = obj->nextcol[1] = obj->nextcol[2] = 0xff;
	obj->floorcol = 0x0fff;

	if (!setupLoadModeldef(modelnum)) {
		return 0;
	}
	if (objInitWithModelDef(obj, g_ModelStates[modelnum].modeldef) == NULL
			|| obj->model == NULL || obj->prop == NULL) {
		return 0;
	}
	modelSetScale(obj->model, obj->model->scale * (obj->extrascale * (1.0f / 256.0f)));

	// keep obj->floorcol fresh for the shade path (out-param)
	cdFindFloorRoomYColourFlagsAtPos(&pos, seedrooms, &floory, &floorcol, NULL);
	obj->floorcol = floorcol;

	func0f06a580(obj, &pos, &mtx, seedrooms);

	// setup.c's OBJTYPE_CHOPPER init block, minus the setup-file ailist
	chopper->turnrot60 = 0;
	chopper->roty = 0;
	chopper->rotx = 0;
	chopper->gunroty = 0;
	chopper->gunrotx = 0;
	chopper->barrelrot = 0;
	chopper->barrelrotspeed = 0;
	chopper->ailist = ailistFindById(GAILIST_IDLE);
	chopper->aioffset = 0;
	chopper->aireturnlist = -1;
	chopper->path = NULL;
	chopper->nextstep = 0;
	chopper->targetvisible = false;
	chopper->vz = 0;
	chopper->vy = 0;
	chopper->vx = 0;
	chopper->otz = 0;
	chopper->oty = 0;
	chopper->otx = 0;
	chopper->power = 0;
	chopper->bob = 0;
	chopper->bobstrength = 0.05f;
	chopper->timer60 = 0;
	chopper->cw = 0;
	chopper->weaponsarmed = true;
	chopper->fireslotthing = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
	chopper->fireslotthing->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
	chopper->fireslotthing->beam->age = -1;
	chopper->fireslotthing->unk08 = -1;
	chopper->fireslotthing->unk00 = 0;
	chopper->fireslotthing->unk01 = 0;
	chopper->fireslotthing->unk0c = 0.85f;
	chopper->fireslotthing->unk10 = 0.2f;
	chopper->fireslotthing->unk14 = 0;
	chopper->dead = false;

	// hunt the player
	chopper->target = g_Vars.currentplayer->prop - g_Vars.props;
	chopper->attackmode = CHOPPERMODE_COMBAT;
	chopper->patroltimer60 = TICKS(240);

	obj->prop->forcetick = true;
	propActivate(obj->prop);
	propEnable(obj->prop);

	g_ChaosChopperSpawned[kind] = 1;
	return 1;
}

// pd.chr_give_weapon(chrnum, weaponnum): make a chr wield the given weapon.
// NPCs only — a player's inventory is managed through give_weapon/take_weapon.
//
// Two different actor kinds need two different mechanisms:
//
//  - Simulants (chr->aibot): a bot re-picks its weapon from its own inventory
//    every tick (bot.c changeguntimer60 path), so overwriting weapons_held is
//    reverted almost immediately. Instead add the weapon to the bot inventory
//    with a full ammo reserve and drive the engine's own switch routine, which
//    sets aibot->weaponnum and schedules the hand-model equip. The bot keeps it
//    until its AI later decides to change guns.
//
//  - Campaign guards (non-bot): chrTickShoot fires straight from the prop in
//    weapons_held[hand], so replacing the held weapon models is enough. The old
//    hand props are marked DELETING so they vanish rather than drop.
s32 chraiLuaChrGiveWeapon(s32 chrnum, s32 weaponnum, s32 dual)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);
	s32 h;

	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL || chr->model == NULL) {
		return 0;
	}
	if (chr->prop->type != PROPTYPE_CHR) {
		return 0;
	}

	// The bot inventory and bot.c's preference tables index by weaponnum
	// unchecked
	if (weaponnum != WEAPON_NONE
			&& (weaponnum < WEAPON_UNARMED || weaponnum > WEAPON_SUICIDEPILL
				|| weaponFindById(weaponnum) == NULL)) {
		return 0;
	}

	if (chr->aibot) {
		if (weaponnum != WEAPON_UNARMED && weaponnum != WEAPON_NONE) {
			botinvGiveSingleWeapon(chr, weaponnum);
			botactGiveAmmoByWeapon(chr->aibot, weaponnum, FUNC_PRIMARY, 0x7fff);
			botactGiveAmmoByWeapon(chr->aibot, weaponnum, FUNC_SECONDARY, 0x7fff);
		}
		return botinvSwitchToWeapon(chr, weaponnum, FUNC_PRIMARY) ? 1 : 0;
	}

	for (h = 0; h < 2; h++) {
		struct prop *wp = chr->weapons_held[h];

		if (wp && wp->obj) {
			wp->obj->hidden |= OBJHFLAG_DELETING;
			chr->weapons_held[h] = NULL;
		}
	}

	// Unarmed / no weapon has no hand model (model -1) — the hands are already
	// cleared above, so leave the guard empty-handed. This is the restore path
	// for a chr that was originally unarmed.
	{
		s32 model = playermgrGetModelOfWeapon(weaponnum);
		struct prop *wp;

		if (model < 0) {
			return 1;
		}

		wp = chrGiveWeapon(chr, model, weaponnum, 0);

		// dual: a second copy in the left hand (the netplay client's
		// weapons-held pattern) — "Enemy Dual RCP45s" actually means duals.
		if (wp != NULL && dual) {
			chrGiveWeapon(chr, model, weaponnum, OBJFLAG_WEAPON_LEFTHANDED);
		}

		return wp != NULL;
	}
}

// pd.chr_weapon(chrnum) -> weaponnum. The chr's currently-wielded weapon:
// aibot->weaponnum for simulants, else the right/left held weapon (WEAPON_UNARMED
// if empty-handed). Returns -1 for an invalid chr. Lets Lua snapshot an NPC's
// gun before pd.chr_give_weapon and restore it when a timed effect ends.
s32 chraiLuaChrWeapon(s32 chrnum)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);
	struct prop *wp;

	if (chr == NULL || chr->prop == NULL) {
		return -1;
	}
	if (chr->aibot) {
		return chr->aibot->weaponnum;
	}

	wp = chrGetHeldProp(chr, HAND_RIGHT);
	if (wp && wp->weapon) {
		return wp->weapon->weaponnum;
	}

	wp = chrGetHeldProp(chr, HAND_LEFT);
	if (wp && wp->weapon) {
		return wp->weapon->weaponnum;
	}

	return WEAPON_UNARMED;
}

// pd.chr_freeze(on): statue every non-player chr (chr.c anim gate +
// chrTickShoot fire gate).
s32 chraiLuaChrFreeze(s32 on)
{
	g_ChaosChrFreeze = on ? 1 : 0;
	return 1;
}

// pd.no_drops(on): dead chrs keep their weapons (chrBeginDeath gate).
s32 chraiLuaNoDrops(s32 on)
{
	g_ChaosNoDrops = on ? 1 : 0;
	return 1;
}

// pd.damage_scale(frac): scale all chr/player damage (1.0 = off).
s32 chraiLuaDamageScale(f32 frac)
{
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 10.0f) frac = 10.0f;
	g_ChaosDamageScale = frac;
	return 1;
}

// pd.chr_speed(mult): scale every non-player chr's anim playback (movement +
// attack cadence ride along). 1 = off. chr.c consumes it in chr0f0220ec.
s32 chraiLuaChrSpeed(f32 mult)
{
	if (mult < 0.1f) mult = 0.1f;
	if (mult > 8.0f) mult = 8.0f;
	g_ChaosChrSpeedMult = mult;
	return 1;
}

// pd.chr_damage(chrnum, amount): hurt any chr (or player pawn) through the
// real damage path — shield, flinch, death, kill credit as environment.
s32 chraiLuaChrDamage(s32 chrnum, f32 amount)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);
	struct coord vec = {0, 0, 1};

	// Skip chrs that are already dead or mid-death: re-running the damage/death
	// path on a corpse re-processes the kill and can corrupt the prop list
	// (the ->next-cycle hang family). Reproduced by Airstrike (kills chrs) then
	// The Snap (damages random chrs) hard-locking the game. Covers all
	// pd.chr_damage callers (snap, plague, ...).
	//
	// NEVER damage a PLAYER pawn: pd.all_chrs() includes the local player, so
	// The Snap/Plague could roll the player and kill Jo. When these effects are
	// fired FROM the Lua Director menu, the player-death path re-enters
	// menuPopDialog/menuClose mid-menu-tick and hangs the game (WAR crash). The
	// player is damaged only through the proper player paths, never here.
	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL
			|| chr->prop->type == PROPTYPE_PLAYER
			|| amount <= 0.0f || chrIsDead(chr)) {
		return 0;
	}
	chrDamageByMisc(chr, amount, &vec, NULL, NULL);
	return 1;
}

// pd.chr_scale(chrnum, mult): multiply a chr's model scale (visual size).
// Cosmetic — collision/eye height keep the original values, which is fine
// for a chaos gag. Callers undo with the inverse multiplier.
s32 chraiLuaChrScale(s32 chrnum, f32 mult)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);

	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL || chr->model == NULL) {
		return 0;
	}
	if (mult < 0.05f) mult = 0.05f;
	if (mult > 8.0f) mult = 8.0f;
	// Cosmetic uniform resize done ENTIRELY in the render matrix
	// (modelUpdateChrNodeMtx scales the composed root->world matrix by groundmult,
	// pivoting X/Z on the chr's vertical axis and Y on the GROUND so the feet stay
	// planted). This replaces the old model->scale + animscale approach, whose
	// pelvis-pivot skeleton spread floated/sank the feet. Collision/eye height keep
	// their original values. Accumulate so a caller's inverse-mult undo divides it
	// back toward 1.0; clamp the running value. chrInit resets it on a recycled slot.
	chr->groundmult *= mult;
	if (chr->groundmult < 0.05f) chr->groundmult = 0.05f;
	if (chr->groundmult > 8.0f) chr->groundmult = 8.0f;
	return 1;
}

// pd.chr_hum(chrnum [, on]): give a chr the two engine loops the Chicago
// interceptor flies on — SFX_810F (the constant hover hum) plus SFX_8110 (the
// thrust layer the real chopper only adds while under power). This is
// chopperTickMove's recipe verbatim: psCreateIfNotDupe issues them as
// POSITIONAL, PSFLAG_REPEATING sounds bound to the chr's prop, so they track
// it through the level and fall off with distance on their own.
//
// The chopper's PSTYPE dedupe keys are reused deliberately — they are only
// ever dedupe/stop selectors (nothing else in the game reads them), and a chr
// is never simultaneously a chopper, so there is nothing to collide with.
//
// Two consequences the caller has to live with, both inherited from the
// chopper: the create is idempotent (calling it repeatedly is free), and
// psCreateIfNotDupe REFUSES to start anything whose distance-attenuated
// volume computes to zero (past ~3000u). So this must be re-issued every tick
// for the loops to resume once the player closes back in — exactly how
// chopperTickMove drives it. on=0 stops both layers.
s32 chraiLuaChrHum(s32 chrnum, s32 on)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);

	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL) {
		return 0;
	}
	if (!on) {
		psStopSound(chr->prop, PSTYPE_CHOPPERHUM1, 0xffff);
		psStopSound(chr->prop, PSTYPE_CHOPPERHUM2, 0xffff);
		return 1;
	}
	psCreateIfNotDupe(chr->prop, SFX_810F, PSTYPE_CHOPPERHUM1);
	psCreateIfNotDupe(chr->prop, SFX_8110, PSTYPE_CHOPPERHUM2);
	return 1;
}

// pd.beyblade(on): "Bayblade!" — every non-player chr's model yaw spins at
// ~2 rev/s (absolute frame-derived stomp in chr0f0220ec, chr.c, so AI facing
// writes can't unwind it). Purely visual: AI, movement and aim keep running.
s32 chraiLuaBeyblade(s32 on)
{
	g_ChaosBeyblade = on ? 1 : 0;
	return 1;
}

// pd.chr_ko(chrnum): knock a chr out via the tranquiliser's sanctioned
// knockout path (chrBeginDeath with knockout=true -> ACT_DRUGGEDDROP).
// The chr collapses and drops its weapon. Engine knockouts are PERMANENT —
// chrTickDruggedKo only fades/reaps the body, and a reaped chr reads as
// eliminated to mission scripts (aiIfChrDead passes on !chr), failing
// protect objectives. So CHRCFLAG_KEEPCORPSEKO is set to park the body
// un-reaped until chraiLuaChrWake recovers it. The KO counter is NOT
// incremented (nap KOs must not trip aiIfNumKnockedOutChrs branches or
// exhaust the first-two-KOs KEEPCORPSEKO budget in chrKnockOut); the
// matching decrement-below-zero hazard is clamped in mpstats.c.
// NPCs only; dead/already-KO'd chrs skip.
s32 chraiLuaChrKo(s32 chrnum)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);
	struct coord dir = {0, 0, 1};
	struct gset gset = {0};

	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL || chr->model == NULL) {
		return 0;
	}
	if (chr->prop->type != PROPTYPE_CHR || chr->aibot) {
		return 0;
	}
	if (chrIsDead(chr) || chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD
			|| chr->actiontype == ACT_DRUGGEDDROP || chr->actiontype == ACT_DRUGGEDKO
			|| chr->actiontype == ACT_DRUGGEDCOMINGUP) {
		return 0;
	}

	gset.weaponnum = WEAPON_TRANQUILIZER;
	gset.weaponfunc = FUNC_SECONDARY;
	// HITPART_TORSO, not HITPART_GENERAL: GENERAL (200) isn't in
	// g_AnimTablesByRace, so chrBeginDeath's Skedar branch fell through to
	// table entry 0 whose deathanims is NULL -> NULL row deref (crash at
	// chraction.c row->thudframe1). TORSO exists in every race's table.
	chrBeginDeath(chr, &dir, 0.0f, HITPART_TORSO, &gset, true, -1);
	chr->chrflags |= CHRCFLAG_KEEPCORPSEKO;
	return 1;
}

// pd.chr_wake(chrnum): recover a chr from the tranquiliser knockout chain.
// Uses the engine's own knockdown recovery (the ANIM_DEATH_STOMACH_LONG
// path in chrTickArgh): a 26-tick blend back to standing via
// func0f02ed28, after which normal AI resumes. Any drugged stage
// (coming-up / falling / on the floor) can be woken. The chr comes up
// unarmed — the drop scattered their weapons.
s32 chraiLuaChrWake(s32 chrnum)
{
	struct chrdata *chr = chrFindByLiteralId(chrnum);

	if (apLuaPlayerChr() == NULL || chr == NULL || chr->prop == NULL || chr->model == NULL) {
		return 0;
	}
	if (chr->prop->type != PROPTYPE_CHR || chr->aibot) {
		return 0;
	}
	if (chr->actiontype != ACT_DRUGGEDCOMINGUP && chr->actiontype != ACT_DRUGGEDDROP
			&& chr->actiontype != ACT_DRUGGEDKO) {
		return 0;
	}
	if (chr->hidden & CHRHFLAG_DELETING) {
		return 0;
	}

	chr->chrflags &= ~CHRCFLAG_KEEPCORPSEKO;
	chr->fadealpha = 255;
	chrRecordLastSeeTargetTime(chr);
	func0f02ed28(chr, 26);
	return 1;
}

// pd.spawn_ally([weaponnum]): spawn a friendly "Perfect Buddy" that fights
// alongside the player. Mirrors the campaign buddy spawn (player.c) -- TEAM_ALLY
// + a buddy ailist; solo allegiance is the bitwise chrCompareTeams test, so it
// targets enemies and won't shoot the player. Returns the new chrnum, or -1.
//
// The buddy wears the player's **Combat Sim profile** body and head when one is
// set up (player 1's MP player config here; Kai read MP.Profile.Body / Head
// from pd.ini), mapped to real model ids by mpGetBodyId / mpGetHeadId, so
// "your squad looks like you" comes free. Falls back to Dark Combat / VD when no profile is loaded
// (user call 2026-07-30).
//
// weaponnum < 0 keeps the historical Falcon 2; the effects pass a random gun.
// A weapon with no chr-held model (playermgrGetModelOfWeapon returns -1) falls
// back to the Falcon rather than spawning the buddy empty-handed.
s32 chraiLuaSpawnAlly(s32 weaponnum)
{
	struct prop *prop;
	struct chrdata *chr;
	s32 bodynum = BODY_DARK_COMBAT;
	s32 headnum = HEAD_VD;
	s32 weaponmodel = -1;

	if (apLuaPlayerChr() == NULL) {
		return -1;
	}

	// Kai read a pd.ini profile pair (MP.Profile.Body/Head, -1 when unset).
	// This build keeps player 1's Combat Sim character in its MP player
	// config; a config with a name is one the player has set up.
	if (g_PlayerConfigsArray[0].base.name[0] != '\0') {
		bodynum = mpGetBodyId(g_PlayerConfigsArray[0].base.mpbodynum);
		headnum = mpGetHeadId(g_PlayerConfigsArray[0].base.mpheadnum);

		// Force the model resident: the spawn path loads on demand and bails
		// SILENTLY otherwise, which would turn a profile body that isn't already
		// on this stage into "the cavalry never arrives" (the chraiLuaSpawnBody
		// lesson). A genuinely absent file still bails downstream.
		if (bodynum >= 0 && bodynum < 152) {
			bodyLoad(bodynum);
		} else {
			bodynum = BODY_DARK_COMBAT;
			headnum = HEAD_VD;
		}
	}

	prop = chrSpawnAtCoord(bodynum, headnum,
			&g_Vars.currentplayer->prop->pos,
			g_Vars.currentplayer->prop->rooms,
			BADDEG2RAD(g_Vars.currentplayer->vv_theta / 2),
			ailistFindById(GAILIST_INIT_DEFAULT_BUDDY),
			SPAWNFLAG_ALLOWONSCREEN);

	if (prop == NULL || prop->chr == NULL) {
		return -1;
	}

	chr = prop->chr;
	chr->flags |= CHRFLAG0_SKIPSAFETYCHECKS;
	chr->team = TEAM_ALLY;
	chr->squadron = SQUADRON_01;
	chr->hidden |= CHRHFLAG_DETECTED;
	chr->voicebox = VOICEBOX_FEMALE;
	chr->teamscandist = 50;
	chr->accuracyrating = 100;
	chr->speedrating = 100;
	chrAddHealth(chr, 20);
	chrSetMaxDamage(chr, 4);
	chr->chrflags |= CHRCFLAG_NEVERSLEEP;

	if (weaponnum > 0) {
		weaponmodel = playermgrGetModelOfWeapon(weaponnum);
	}

	if (weaponmodel >= 0) {
		chrGiveWeapon(chr, weaponmodel, weaponnum, 0);
	} else {
		chrGiveWeapon(chr, MODEL_CHRFALCON2, WEAPON_FALCON2, 0);
	}

	return chr->chrnum;
}

// pd.spawn_ally_clone([healthfrac]) -> chrnum or -1. The spawn_ally recipe, but
// the buddy wears the PLAYER's own body AND head — a friendly Jo clone that
// fights on TEAM_ALLY beside you — and her health pool is scaled by healthfrac
// (default 0.5 = a fragile half-HP clone). Backs the "Me and my son" chaos
// effect. Server/solo-side (a net client never spawns AI).
s32 chraiLuaSpawnAllyClone(f32 healthfrac, f32 yscale)
{
	struct prop *prop;
	struct chrdata *chr;
	struct chrdata *plchr = apLuaPlayerChr();
	s32 bodynum;
	s32 headnum;
	f32 maxdmg;

	if (plchr == NULL) {
		return -1;
	}

	if (healthfrac <= 0.0f) {
		healthfrac = 0.5f;
	}
	if (healthfrac > 1.0f) {
		healthfrac = 1.0f;
	}

	// Use the player's own body/head so the clone actually looks like Jo. Force
	// the body's model file resident first (the spawn path only loads on demand
	// and bails silently otherwise) — mirrors chraiLuaSpawnBody.
	bodynum = plchr->bodynum;
	headnum = plchr->headnum;
	if (bodynum >= 0 && bodynum < 152) {
		bodyLoad(bodynum);
	}

	prop = chrSpawnAtCoord(bodynum, headnum,
			&g_Vars.currentplayer->prop->pos,
			g_Vars.currentplayer->prop->rooms,
			BADDEG2RAD(g_Vars.currentplayer->vv_theta / 2),
			ailistFindById(GAILIST_INIT_DEFAULT_BUDDY),
			SPAWNFLAG_ALLOWONSCREEN);

	if (prop == NULL || prop->chr == NULL) {
		return -1;
	}

	chr = prop->chr;
	chr->flags |= CHRFLAG0_SKIPSAFETYCHECKS;
	chr->team = TEAM_ALLY;
	chr->squadron = SQUADRON_01;
	chr->hidden |= CHRHFLAG_DETECTED;
	chr->voicebox = VOICEBOX_FEMALE;
	chr->teamscandist = 50;
	chr->accuracyrating = 100;
	chr->speedrating = 100;
	// A standard buddy is maxdamage 4 + 20 armour (~24 effective). Scale the
	// whole pool by healthfrac so the clone dies that much sooner. chrAddHealth
	// subtracts from chr->damage (negative damage = armour buffer).
	maxdmg = 4.0f * healthfrac;
	if (maxdmg < 1.0f) {
		maxdmg = 1.0f;
	}
	chrSetMaxDamage(chr, maxdmg);
	chrAddHealth(chr, 20.0f * healthfrac);
	chr->chrflags |= CHRCFLAG_NEVERSLEEP;
	chrGiveWeapon(chr, MODEL_CHRFALCON2, WEAPON_FALCON2, 0);

	// Apply the vertical squash HERE, directly on the chr we just spawned, rather
	// than leaving it to a follow-up pd.chr_yscale(chrnum, ...) — that has to
	// re-find the chr by the returned chrnum, and the squat "Me and my son" clone
	// was rendering full height because that separate set wasn't landing. Same
	// clamp/semantics as chraiLuaChrYscale; <= 0 or 1.0 leaves the default.
	if (yscale > 0.0f && yscale != 1.0f) {
		if (yscale > 4.0f) {
			yscale = 4.0f;
		}
		chr->yscale = yscale;
	}

	return chr->chrnum;
}

// pd.chr_yscale(chrnum, mult): non-uniform VERTICAL squash/stretch. Unlike
// chr_scale (uniform), this scales only the model's local up axis, so mult 0.4
// makes a chr 40% as tall while keeping full width/depth (a squat, wide look).
// Stored on chr->yscale and applied every frame in modelUpdateChrNodeMtx.
// mult <= 0 resets to 1.0 (no squash); the render side additionally caps it at
// 4.0. Purely visual (hitbox/AI unchanged). Returns 1 on success, else 0.
s32 chraiLuaChrYscale(s32 chrnum, f32 mult)
{
	struct chrdata *chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);

	if (apLuaPlayerChr() == NULL || chr == NULL) {
		return 0;
	}
	if (mult <= 0.0f) {
		mult = 1.0f;
	}
	if (mult > 4.0f) {
		mult = 4.0f;
	}
	chr->yscale = mult;
	return 1;
}

s32 chraiLuaChrAnim(s32 chrnum, s32 animnum, f32 speed)
{
	struct chrdata *chr;

	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL || chr->model == NULL) {
		return 0;
	}
	// modelSetAnimation reads g_Anims[animnum] unchecked
	if (animnum < 0 || animnum >= g_NumAnimations || !animHasFrames((s16)animnum)) {
		return 0;
	}
	// flip 0, start frame 0, given speed, short merge for a smooth cut-in.
	modelSetAnimation(chr->model, (s16)animnum, 0, 0, speed, 16);
	return 1;
}

s32 chraiLuaChrSetShield(s32 chrnum, f32 value)
{
	struct chrdata *chr;

	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL) {
		return 0;
	}
	// NPCs only — the player's shield is a separate system (pd.player_set_shield).
	// Without this, "Shielded enemies" (all_chrs loop) also shielded Jo.
	if (chr->prop && chr->prop->type == PROPTYPE_PLAYER) {
		return 0;
	}
	chrSetShield(chr, value);
	return 1;
}

s32 chraiLuaChrAlert(s32 chrnum)
{
	struct chrdata *chr;
	struct chrdata *pl;

	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL || chrIsDead(chr)) {
		return 0;
	}
	// Same flag the damage path sets to make a chr switch to its shot/alert
	// list — but the flag alone reproduces only PART of being shot: chrDamage
	// also raises alertness and identifies the attacker. Without those the
	// shot list ran with no target and settled straight back to idle (the
	// PANIC! visible-no-op bug, fixed 2026-07-29).
	chr->chrflags |= CHRCFLAG_TRIGGERSHOTLIST;
	chr->alertness = 100;

	pl = apLuaPlayerChr();
	if (pl != NULL && chr != pl
			&& (chr->team & TEAM_NONCOMBAT) == 0
			&& chrCompareTeams(pl, chr, COMPARE_ENEMIES)) {
		// Hostile combatants learn WHO to hunt; civilians/allies just panic
		// (never force targets onto non-combatants — the civil-war lesson).
		chr->target = propGetIndexByChrId(chr, pl->chrnum);
	}
	return 1;
}

// Swap a live actor's body model at runtime ("turn everyone into X") -- the most
// invasive toolkit primitive. Rebuilds the chr's model from a new bodynum and
// re-establishes the chr<->model links by reusing the engine's own spawn wiring
// (chr0f020b14) + teardown (free vertices/model, detach held weapons). Captures
// and re-gives held weapons to the new skeleton. Solo/missions only (refused in
// Combat Sim: bodynum is only synced at stage start, so a runtime swap wouldn't
// replicate); player props refused; server-side. Returns 1 on success, else 0.
s32 chraiLuaChrSetBody(s32 chrnum, s32 bodynum, s32 headnum)
{
	struct chrdata *chr;
	struct model *oldmodel;
	struct model *newmodel;
	f32 faceangle;
	s32 h;
	s32 heldweapon[2];

	if (g_Vars.normmplayerisrunning) {
		return 0; // Combat Sim: not supported (no runtime bodynum sync)
	}
	// g_HeadsAndBodies is sized at runtime and nothing downstream bounds it
	if (bodynum < 0 || bodynum >= g_NumHeadsAndBodies || headnum >= g_NumHeadsAndBodies) {
		return 0;
	}
	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL || chr->prop == NULL || chr->model == NULL) {
		return 0;
	}
	if (chr->prop->type == PROPTYPE_PLAYER) {
		return 0; // never swap the player's own body model
	}
	if (chrIsDead(chr) || chr->actiontype == ACT_DIE) {
		return 0; // mid-death model state is fragile
	}

	// ⚠ Refuse the swap unless there is real ground under the chr RIGHT NOW.
	//
	// chr0f020b14 (below) re-grounds whatever it re-links: it probes from
	// pos.y + 100 with cdFindGroundInfoAtCyl and then hard-assigns
	// `prop->pos.y = ground + 100`. When that probe finds no floor it does not
	// fail — it returns cdFindGroundFromList's "no ground" sentinel,
	// **-4294967296** — so the chr is placed four billion units down and is gone.
	// That is the "guards sometimes fall through the floor" report (2026-07-30):
	// Identity Crisis reskins EVERY guard on a timer, so it only takes one who
	// happens to be airborne (yeeted by an explosion), on a lift, or standing
	// somewhere their collision cylinder can't find a floor.
	//
	// Probed with the same call, from the same offset, with the same radius, so
	// it predicts chr0f020b14's result exactly. `> -100000` is the codebase's
	// established "did we find ground" idiom (cf. chrAdjustPosForSpawn) — well
	// above the sentinel, far below any real geometry. Skipping one reskin cycle
	// for that guard is invisible; losing them under the map is not.
	{
		struct coord testpos;
		f32 ground;

		testpos.x = chr->prop->pos.x;
		testpos.y = chr->prop->pos.y + 100.0f;
		testpos.z = chr->prop->pos.z;

		ground = cdFindGroundInfoAtCyl(&testpos, chr->radius, chr->prop->rooms,
				NULL, NULL, NULL, NULL, NULL, NULL);

		if (ground <= -100000.0f) {
			return 0;
		}
	}

	for (h = 0; h < 2; h++) {
		heldweapon[h] = (chr->weapons_held[h] && chr->weapons_held[h]->obj
				&& chr->weapons_held[h]->obj->type == OBJTYPE_WEAPON
				&& chr->weapons_held[h]->weapon)
			? (s32)chr->weapons_held[h]->weapon->weaponnum : -1;
	}

	if (headnum < 0) {
		headnum = bodyChooseHead(bodynum);
	}

	newmodel = bodyAllocateModel(bodynum, headnum, 0);
	if (newmodel == NULL) {
		return 0;
	}

	oldmodel = chr->model;
	faceangle = chrGetRotY(chr);

	for (h = 0; h < 3; h++) {
		if (chr->weapons_held[h]) {
			if (chr->weapons_held[h]->obj) {
				chr->weapons_held[h]->obj->hidden |= OBJHFLAG_DELETING;
			}
			chr->weapons_held[h] = NULL;
		}
	}

	chr0f020b14(chr->prop, newmodel, &chr->prop->pos, chr->prop->rooms, faceangle, chr->ailist);

	// ⚠ HAND THE NEW MODEL VALID ANIM STATE BEFORE ANYTHING TICKS IT.
	//
	// chr0f020b14 is the SPAWN-time linker: it leaves the fresh model's anim
	// struct and its root CHRINFO rwdata (root position / facing / root-motion
	// accumulator) un-initialised. A spawning chr doesn't care — its ailist
	// issues an animation on the first tick — but a LIVE chr is mid-animation, so
	// the next chrTick reads whatever was in that allocation. That is a hard lock
	// caught in gdb (2026-07-30): modelTickAnimQuarterSpeed pulled
	// curframe = -2.4e21 out of the fresh anim and dove into animLoadFrame with
	// it. It is also the more likely cause of the "guards fall through the floor"
	// reports than the ground sentinel guarded above — a garbage root-motion
	// accumulator moves the chr on its own.
	//
	// body.c's modelSwapRebuildLiveChrs already documents and solves this by
	// copying both across from the old model. It can do that unconditionally
	// because it re-allocates the SAME bodynum, so the layouts are identical.
	//
	// ⚠ Here the whole point is a DIFFERENT body, so the copy is only valid when
	// the two share a skeleton. Identity Crisis's pool mixes human bodies with
	// skedar-skeleton ones (BODY_SKEDAR / BODY_MINISKEDAR), and an animnum+frame
	// that means "walking" on g_SkelChr indexes nothing sane on g_SkelSkedar.
	// Cross-skeleton swaps are therefore REFUSED rather than fudged: better a
	// guard who declines to become a Skedar than one reading garbage.
	if (oldmodel->definition == NULL || newmodel->definition == NULL
			|| oldmodel->definition->skel != newmodel->definition->skel) {
		// Put it back exactly as it was; nothing has been freed yet.
		chr0f020b14(chr->prop, oldmodel, &chr->prop->pos, chr->prop->rooms, faceangle, chr->ailist);
		modelFreeVertices(VTXSTORETYPE_CHRVTX, newmodel);
		modelmgrFreeModel(newmodel);

		for (h = 0; h < 2; h++) {
			if (heldweapon[h] >= 0) {
				s32 modelnum = playermgrGetModelOfWeapon(heldweapon[h]);

				if (modelnum >= 0) {
					u32 flags = (h == 1) ? OBJFLAG_WEAPON_LEFTHANDED : 0;
					chrGiveWeapon(chr, modelnum, heldweapon[h], flags);
				}
			}
		}
		return 0;
	}

	modelCopyAnimData(oldmodel, newmodel);

	if ((oldmodel->definition->rootnode->type & 0xff) == MODELNODETYPE_CHRINFO
			&& (newmodel->definition->rootnode->type & 0xff) == MODELNODETYPE_CHRINFO) {
		union modelrwdata *oldrw =
				modelGetNodeRwData(oldmodel, oldmodel->definition->rootnode);
		union modelrwdata *newrw =
				modelGetNodeRwData(newmodel, newmodel->definition->rootnode);

		if (oldrw && newrw) {
			newrw->chrinfo = oldrw->chrinfo;
		}
	}

	modelFreeVertices(VTXSTORETYPE_CHRVTX, oldmodel);
	modelmgrFreeModel(oldmodel);

	chr->bodynum = bodynum;
	chr->headnum = headnum;

	for (h = 0; h < 2; h++) {
		if (heldweapon[h] >= 0) {
			s32 modelnum = playermgrGetModelOfWeapon(heldweapon[h]);
			if (modelnum >= 0) {
				u32 flags = (h == 1) ? OBJFLAG_WEAPON_LEFTHANDED : 0;
				chrGiveWeapon(chr, modelnum, heldweapon[h], flags);
			}
		}
	}

	return 1;
}

// Move a chr prop to a position without physics (used by the possession freecam
// to keep the visible cube under the fly pose). Mirrors how eyespy moves its
// prop: chr0f021fa8 + modelSetRootPosition. Server-side. Returns 1 on success.
s32 chraiLuaSetChrPos(s32 chrnum, f32 x, f32 y, f32 z)
{
	struct chrdata *chr;
	struct coord pos;

	chr = (chrnum < 0) ? NULL : chrFindByLiteralId(chrnum);
	if (chr == NULL || chr->prop == NULL) {
		return 0;
	}
	pos.x = x;
	pos.y = y;
	pos.z = z;
	chr0f021fa8(chr, &pos, chr->prop->rooms);
	if (chr->model) {
		modelSetRootPosition(chr->model, &pos);
	}
	return 1;
}

// Spawn a "cube" entity (BODY_EYESPY model -- a small controllable device) at
// the player's location and possess it (free-fly). Solo/missions only. Returns
// the spawned chrnum, or -1 on failure. Backs pd.possess_spawn().
s32 chraiLuaPossessSpawn(s32 bodynum)
{
	struct player *pl;
	struct model *model;
	struct prop *prop;
	RoomNum rooms[8];
	struct coord pos;

	if (g_Vars.normmplayerisrunning) {
		return -1;
	}
	pl = g_Vars.players ? g_Vars.players[0] : NULL;
	if (pl == NULL || pl->prop == NULL) {
		return -1;
	}
	// one cube at a time: pd.unpossess first
	if (luaPossessIsActive()) {
		return -1;
	}

	if (bodynum < 0) {
		bodynum = BODY_EYESPY;
	}
	if (bodynum >= g_NumHeadsAndBodies) {
		return -1;
	}

	model = bodyAllocateModel(bodynum, bodyChooseHead(bodynum), 0);
	if (model == NULL) {
		return -1;
	}

	pos = pl->prop->pos;
	pos.y += 60.f; // float a touch above the player
	roomsCopy(pl->prop->rooms, rooms);

	prop = chrAllocate(model, &pos, rooms, 0.0f, NULL);
	if (prop == NULL || prop->chr == NULL) {
		return -1;
	}
	propActivate(prop);
	propEnable(prop);
	prop->chr->chrflags |= CHRCFLAG_INVINCIBLE;

	if (!luaPossessBegin((s32)prop->chr->chrnum)) {
		prop->chr->hidden |= CHRHFLAG_DELETING; // don't leave the cube behind
		return -1;
	}
	return (s32)prop->chr->chrnum;
}

// Stop possessing and return control to the player body. Frees the cube prop.
void chraiLuaUnpossess(void)
{
	s32 chrnum = luaPossessGetChrNum();
	luaPossessEnd();
	if (chrnum >= 0) {
		struct chrdata *chr = chrFindByLiteralId(chrnum);
		if (chr && chr->prop) {
			chr->hidden |= CHRHFLAG_DELETING; // remove the cube
		}
	}
}

// pd.spawn and pd.explosion_at call the weapons group's chraiLuaSpawnAtPos /
// chraiLuaExplodeAtPos; the chrs group reaches them through its private copies.
s32 chrsApiSpawnAtPos(s32 refchrnum, s32 weaponnum, f32 x, f32 y, f32 z)
{
	return chrsSpawnAtPos(refchrnum, weaponnum, x, y, z);
}

s32 chrsApiExplodeAtPos(f32 x, f32 y, f32 z, s32 type)
{
	return chrsExplodeAtPos(x, y, z, type);
}
