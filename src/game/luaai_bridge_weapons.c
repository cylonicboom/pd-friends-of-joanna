/**
 * chraiLua* bridges for the weapons group of the pd.* API: inventory and ammo,
 * the weapon behaviour effects, pd.spawn / pd.explosion_at placement, and the
 * text gags.
 *
 * From the Perfect Dark Kai fork (be46717), where the bridges sat in
 * src/game/chraction.c. The comments are Kai's. The g_Chaos* state they set is
 * defined in game/chaosstate.c and read where Kai reads it (bondgun.c,
 * bondmove.c, game_0b0fd0.c, lang.c, ...). Kai's net client tests are gone:
 * this build has no netplay.
 */

#include <ultra64.h>
#include <math.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "game/bg.h"
#include "game/bondgun.h"
#include "game/chaosstate.h"
#include "game/chr.h"
#include "game/explosions.h"
#include "game/game_0b0fd0.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "lib/collision.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "luaai_api_internal.h"

// The Area 51 suitcase (chaos Bag bomb, Kai be46717) has no mapping in
// playermgrGetModelOfWeapon: vanilla leaves it at -1 so nobody gets a hand
// model for it. Kai added the mapping there for every caller; here it is
// applied only to the Lua spawn/drop paths so Lua-off behaviour is unchanged.
static s32 weaponsModelOfWeapon(s32 weaponnum)
{
	if (weaponnum == WEAPON_SUITCASE) {
		return MODEL_SUITCASE;
	}

	return playermgrGetModelOfWeapon(weaponnum);
}

// Magazines the gun-giving effects hand out (pd.dual_wield).
#define CHAOS_GUN_MAGS 2

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
// object may float rather than crash). Server-side only.
s32 chraiLuaSpawnAtPos(s32 refchrnum, s32 weaponnum, f32 x, f32 y, f32 z)
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

	modelnum = weaponsModelOfWeapon(weaponnum);
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

// pd.refill_ammo(): top every ammo type to capacity (covers the current weapon).
s32 chraiLuaRefillAmmo(void)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	bgunGiveMaxAmmo(true);
	return 1;
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
 * pass 2 writes n of those back. Largest rather than smallest deliberately:
 * n mags should be a sensible amount for whatever you are actually holding, and
 * undershooting on a big-magazine gun (Cyclone, Reaper) would leave the effect
 * feeling broken.
 *
 * Everything goes through the public bgun*ForWeapon accessors rather than the
 * ammo table itself, which is file-local to bondgun.c. Ammo types no weapon
 * uses are left alone — zeroing them would strip gadgets and objective items
 * that ride the same array.
 */
s32 chraiLuaGiveMags(s32 mags)
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

// pd.give_ammo(ammotype, qty): grant ammo (auto-gives the matching weapon).
s32 chraiLuaGiveAmmo(s32 ammotype, s32 qty)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	// ammoHandlePickup indexes ammoheldarr/g_AmmoTypes unchecked (same range
	// as pd.set_ammo)
	if (ammotype < 1 || ammotype > AMMOTYPE_ECM_MINE) {
		return 0;
	}
	ammoHandlePickup(ammotype, qty, true, true);
	return 1;
}

// pd.give_weapon(weaponnum): add a weapon to the player's inventory.
s32 chraiLuaGiveWeaponToPlayer(s32 weaponnum)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	// frSetWeaponFound and later g_Weapons reads take the number unchecked
	if (weaponFindById(weaponnum) == NULL) {
		return 0;
	}
	invGiveSingleWeapon(weaponnum);
	return 1;
}

// pd.take_weapon(weaponnum): remove a weapon from the player's inventory and
// cycle off it if held (the aiChrDropWeapon player branch, minus the world
// drop — chaos takes the gun, it doesn't gift it to the floor).
s32 chraiLuaTakeWeapon(s32 weaponnum)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	invRemoveItemByNum(weaponnum);
	if (bgunGetWeaponNum(HAND_RIGHT) == weaponnum) {
		bgunCycleBack();
	}
	return 1;
}

// pd.weapon_held() -> weaponnum of the right hand (-1 with no pawn).
s32 chraiLuaWeaponHeld(void)
{
	if (apLuaPlayerChr() == NULL) {
		return -1;
	}
	return bgunGetWeaponNum(HAND_RIGHT);
}

// pd.switch_weapon(weaponnum): force-equip a weapon the player owns.
s32 chraiLuaSwitchWeapon(s32 weaponnum)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	// bgunEquipWeapon2 only clamps the top end
	if (weaponnum < WEAPON_UNARMED || weaponnum > WEAPON_SUICIDEPILL) {
		return 0;
	}
	bgunEquipWeapon2(HAND_RIGHT, weaponnum);
	return 1;
}

// pd.bag_convert(): Bag bomb's throw conversion. The player throws a DRAGON
// on its proximity-self-destruct secondary (the only real hand-throw
// animation that leaves a placeable object), and this turns the landed ARMED
// dragon into the inert Area 51 suitcase: find it in the proxy registry
// (only thrown-and-armed dragons register there — a floor pickup never
// does, so no collateral), defuse it by unregistering, free the prop, and
// spawn the suitcase pickup at its resting spot. Returns 1 once converted;
// the Lua tick polls this after the throw (registration happens when the
// thrown dragon settles and arms, so 0 just means "still in the air").
s32 chraiLuaBagConvert(void)
{
	s32 i;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	for (i = 0; i < (s32)ARRAYCOUNT(g_Proxies); i++) {
		struct weaponobj *weapon = g_Proxies[i];

		if (weapon != NULL && weapon->weaponnum == WEAPON_DRAGON && weapon->base.prop != NULL) {
			struct coord pos = weapon->base.prop->pos;

			weaponUnregisterProxy(weapon);
			objFreePermanently(&weapon->base, true);
			return chraiLuaSpawnAtPos(-1, WEAPON_SUITCASE, pos.x, pos.y, pos.z);
		}
	}

	return 0;
}

// pd.bag_boom(): Bag bomb's timer-end detonation — find the DROPPED Area 51
// suitcase (the "bag": WEAPON_SUITCASE tossed via pd.drop_weapon, so nothing
// lingers in the inventory — a mine left its weapon entry behind, user
// report), free it, and detonate at its resting spot via the explosion
// helper below (which re-derives rooms from the position). Scan-based — no
// stored prop pointer to dangle; SP chaos only ever has the one dropped
// case (Area 51's own mission suitcase is a held/stage prop, not a dropped
// pickup, and the effect is single-player chaos anyway).
s32 chraiLuaBagBoom(void)
{
	struct prop *prop;
	struct coord pos;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}

	for (prop = g_Vars.activeprops; prop != NULL; prop = prop->next) {
		if (prop->type == PROPTYPE_WEAPON && prop->weapon != NULL
				&& prop->weapon->weaponnum == WEAPON_SUITCASE
				&& prop->parent == NULL) {
			pos = prop->pos;
			objFreePermanently(&prop->weapon->base, true);
			// Not Kai's 0: EXPLOSIONTYPE_NONE makes explosionCreate return
			// without a blast, so the bag just vanished. 9 is pd.explosion_at's
			// default.
			return chraiLuaExplodeAtPos(pos.x, pos.y, pos.z, EXPLOSIONTYPE_9);
		}
	}

	return 0;
}

// pd.explosion_at(x, y, z [, type]): detonate at an arbitrary position,
// attributed to the local player. Rooms are portal-walked from the player's
// (known-valid) rooms to the real floor room at the target — the same
// placement recipe as chraiLuaSpawnAtPos. Backs the Live Grenade / Martyrdom
// delayed-boom pattern: Lua records a position, spawns a grenade pickup
// there (pd.spawn), then calls this when the fuse runs out.
s32 chraiLuaExplodeAtPos(f32 x, f32 y, f32 z, s32 type)
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

// pd.one_bullet(on): "One Bullet Mags" — clip capacity 1. The bake normally
// only runs at equip, so bgunChaosRebakeClipSizes applies it to the gun in
// hand immediately (excess loaded rounds refunded to reserve).
s32 chraiLuaOneBullet(s32 on)
{

	g_ChaosOneBulletMags = on ? 1 : 0;
	if (apLuaPlayerChr() != NULL) {
		bgunChaosRebakeClipSizes();
	}
	return 1;
}

// pd.uwuify(on): every langGet string is uwuified (lang.c g_ChaosUwuMode;
// %-format specs preserved, long strings pass through).
s32 chraiLuaUwuify(s32 on)
{

	g_ChaosUwuMode = on ? 1 : 0;
	return 1;
}

// pd.piglatin(on): igpay atinlay — same text pipeline as uwuify, mode 2.
// Turning either off zeroes the shared mode.
s32 chraiLuaPigLatin(s32 on)
{

	g_ChaosUwuMode = on ? 2 : 0;
	return 1;
}

// pd.text_scramble(on): Text overload — every letter becomes a random other
// letter (lang.c mode 4, seeded per string so menus don't strobe). Same
// shared text-mode global as uwuify/piglatin/buttsbot.
s32 chraiLuaTextScramble(s32 on)
{

	g_ChaosUwuMode = on ? 4 : 0;
	return 1;
}

// pd.gangsta(on): Gangster — force the close-range sideways-pistol pose on
// permanently. Rides bgunUpdateGangsta's own rotate/revert animation (bondgun.c
// g_ChaosGangstaForce is ORed with the autoaim-driven gunctrl.gangsta), so the
// gun still only tilts in states that allow it (not reloads/equips).
s32 chraiLuaGangsta(s32 on)
{

	g_ChaosGangstaForce = on ? 1 : 0;
	return 1;
}

// pd.forced_fire(on): "Itchy Trigger Finger" — the trigger is held down for
// you (bondmove.c movedata choke).
s32 chraiLuaForcedFire(s32 on)
{

	g_ChaosForcedFire = on ? 1 : 0;
	return 1;
}

// pd.rapid_fire(on): "Trigger Happy" — while you hold fire, semi-autos fire as
// fast as automatics (bondmove.c g_ChaosRapidFire).
s32 chraiLuaRapidFire(s32 on)
{

	g_ChaosRapidFire = on ? 1 : 0;
	return 1;
}

// pd.no_reload(on): "Reload Denied" — every reload transition is refused
// (bondgun.c g_ChaosNoReload).
//
// Also arms the all-weapons partial-clip memory that Temu Magazine introduced
// (bgunChaosClipMemoryActive), because refusing the reload ANIMATION means very
// little on its own: vanilla only remembers partial clips for the
// crossbow/shotgun/magnum/LX, so for every other gun a switch away and back
// handed you a fresh mag — a free, animation-less reload. Toggling either way
// clears the table so a past run's holstered clips can't leak into the next.
s32 chraiLuaNoReload(s32 on)
{

	g_ChaosNoReload = on ? 1 : 0;
	bgunChaosTemuSpentClear();
	return 1;
}

// pd.spread(mult): "Chaos Weapon Spread" — scale every weapon's shot spread +
// crosshair bloom (bondgun.c g_ChaosSpreadMult). 1 = normal; clamped sane.
s32 chraiLuaSpread(f32 mult)
{
	if (isnan(mult)) mult = 1.0f;
	if (mult < 0.0f) mult = 0.0f;
	if (mult > 20.0f) mult = 20.0f;
	g_ChaosSpreadMult = mult;
	return 1;
}

// pd.drop_weapon(weaponnum): drop one of the player's weapons as a collectable
// floor pickup (weaponCreateForPlayerDrop = the engine's blessed objDrop +
// netSyncPropSpawn path) AND remove it from inventory so it isn't duplicated.
// Used by "Sonic Mode" to toss the arsenal so it can be re-collected.
s32 chraiLuaDropWeapon(s32 weaponnum)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	// Not in Kai: a weapon with no pickup model (keycards, gadgets, ...)
	// would be created around g_ModelStates[-1] and crash on its first tick.
	if (weaponsModelOfWeapon(weaponnum) < 0) {
		return 0;
	}
	if (weaponnum == WEAPON_SUITCASE) {
		// weaponCreateForPlayerDrop with the model passed in (see above)
		struct prop *prop = weaponCreateForChr(g_Vars.currentplayer->prop->chr, MODEL_SUITCASE,
				weaponnum, OBJFLAG_WEAPON_AICANNOTUSE, NULL, NULL);

		if (prop) {
			objSetDropped(prop, DROPTYPE_DEFAULT);
			objDrop(prop, true);
		}
	} else {
		weaponCreateForPlayerDrop(weaponnum);
	}
	invRemoveItemByNum(weaponnum);
	if (bgunGetWeaponNum(HAND_RIGHT) == weaponnum) {
		bgunCycleBack();
	}
	return 1;
}

// pd.gun_fov(deg): "WAYTOODANK Viewmodel" — override the viewmodel Gun FOV
// (bondgun.c g_ChaosGunFovOverride); 0 restores the player's configured FOV.
s32 chraiLuaGunFov(f32 deg)
{

	if (deg != 0.0f) {
		if (deg < 5.0f) {
			deg = 5.0f;
		} else if (deg > 165.0f) {
			deg = 165.0f;
		}
	}
	g_ChaosGunFovOverride = deg;
	return 1;
}

// pd.buttsbot(on): text mode 3 — random-but-stable words become "butt".
// Shares the text-mode global with uwuify/piglatin (off zeroes it).
s32 chraiLuaButtsbot(s32 on)
{

	g_ChaosUwuMode = on ? 3 : 0;
	return 1;
}

// pd.has_weapon(weaponnum): is the weapon in the player's inventory.
s32 chraiLuaHasWeapon(s32 weaponnum)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	return invHasSingleWeaponIncAllGuns(weaponnum) ? 1 : 0;
}

// pd.strip_ammo(): zero every ammo pool (the inverse of pd.refill_ammo).
// Weapons stay in the inventory — the chaos is the click, not the loss.
s32 chraiLuaStripAmmo(void)
{
	s32 type;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	for (type = 1; type <= AMMOTYPE_ECM_MINE; type++) {
		bgunSetAmmoQuantity(type, 0);
	}
	return 1;
}

// pd.set_ammo(ammotype, qty): set ONE ammo pool to an exact quantity,
// leaving every other pool alone (russian roulette wants exactly one
// Magnum round without confiscating the rest of the arsenal).
s32 chraiLuaSetAmmo(s32 ammotype, s32 qty)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (ammotype < 1 || ammotype > AMMOTYPE_ECM_MINE) {
		return 0;
	}
	if (qty < 0) {
		qty = 0;
	}
	bgunSetAmmoQuantity(ammotype, qty);
	return 1;
}

// pd.dual_wield(weaponnum [, funcnum]): give and equip the weapon in BOTH
// hands (the playerSpawnAnti dual-wield recipe: single + double inventory
// entries, then equip each hand) with CHAOS_GUN_MAGS magazines. funcnum 0/1 forces
// fire function on both hands (1 = secondary, e.g. the Cyclone's Magazine
// Discharge); the player can still cycle functions manually afterwards.
s32 chraiLuaDualWield(s32 weaponnum, s32 funcnum)
{
	struct player *pl = g_Vars.currentplayer;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (weaponFindById(weaponnum) == NULL) {
		return 0;
	}

	invGiveSingleWeapon(weaponnum);
	invGiveDoubleWeapon(weaponnum, weaponnum);
	bgunEquipWeapon2(HAND_RIGHT, weaponnum);
	bgunEquipWeapon2(HAND_LEFT, weaponnum);
	chraiLuaGiveMags(CHAOS_GUN_MAGS);

	if (funcnum == FUNC_PRIMARY || funcnum == FUNC_SECONDARY) {
		pl->hands[HAND_RIGHT].gset.weaponfunc = funcnum;
		pl->hands[HAND_LEFT].gset.weaponfunc = funcnum;
	}
	return 1;
}

// pd.gun_lock(on): "Cyclone Frenzy" lock — while on, bmoveProcessInput forces
// the SECONDARY fire function on both hands, holds the trigger (auto-fire), and
// blocks weapon switching (cycle offsets + the amOpen weapon menu). Local player
// only; cleared in lvInit so a mid-effect stage change can't leave it stuck.
s32 chraiLuaGunLock(s32 on)
{
	g_ChaosGunLock = on ? 1 : 0;
	return 1;
}

// pd.mag_dump(on): "Mag Dump" — a single trigger press empties the whole clip.
// Automatic weapons get the trigger held; semi-autos get it rapidly pulsed. The
// per-tick driving + auto/semi detection lives in bmoveProcessInput. Local player
// only; both g_ChaosMagDump and the g_ChaosMagDumpArmed latch clear in lvInit.
s32 chraiLuaMagDump(s32 on)
{
	g_ChaosMagDump = on ? 1 : 0;
	if (!g_ChaosMagDump) {
		g_ChaosMagDumpArmed = 0;
	}
	return 1;
}

// pd.knife_lock(on): "Knife fight" lock — block weapon switching (cycle offsets
// + the amOpen weapon menu) so only the equipped knife can be used, but leave
// firing/functions normal. Local player only; cleared in lvInit.
s32 chraiLuaKnifeLock(s32 on)
{
	g_ChaosKnifeLock = on ? 1 : 0;
	return 1;
}

// pd.cloak_lock(on): make the player's cloak unbreakable — firing doesn't
// drop it and it needs no cloak ammo (chr.c chrUncloakTemporarily +
// chrUpdateCloak gates). Backs "Now you see me..." paired with
// pd.device_on(WEAPON_CLOAKINGDEVICE).
s32 chraiLuaCloakLock(s32 on)
{
	g_ChaosCloakLock = on ? 1 : 0;
	return 1;
}

// pd.double_shots(on): every fire event takes twice the shots (bondgun.c
// shotstotake doubling — with dual-wield that's the "Quad handed" bit).
s32 chraiLuaDoubleShots(s32 on)
{
	g_ChaosDoubleShots = on ? 1 : 0;
	return 1;
}

// pd.quad_top(on): render a second pair of viewmodel guns hanging upside-down
// from the top of the screen (bgunRender 180-degree-rotated projection pass).
s32 chraiLuaQuadTop(s32 on)
{
	g_ChaosQuadTopGuns = on ? 1 : 0;
	// clip capacity bakes at equip — re-run it now so the 2x applies to the
	// gun already in hand (same fix as one_bullet)
	if (apLuaPlayerChr() != NULL) {
		bgunChaosRebakeClipSizes();
	}
	return 1;
}

// pd.backfire(on): the local player's shots (hitscan traces, fired
// projectiles, tracers) leave 180 degrees behind them; the crosshair and gun
// render stay where they are. Vertical aim is preserved.
s32 chraiLuaBackfire(s32 on)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	g_ChaosBackfire = on ? 1 : 0;
	return 1;
}

// pd.ammo_swap(weaponnum): every gun the player holds fires this weapon's
// primary rounds (rockets, Devastator grenades, DY357 bullets...). The target
// must have a SHOOT-type primary — melee/throw/device functions need hand
// anim states a gun can't provide. pd.ammo_swap() with no arg turns it off.
s32 chraiLuaAmmoSwap(s32 weaponnum)
{
	struct weaponfunc *func;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (weaponnum < 0) {
		g_ChaosAmmoSwapWeapon = -1;
		return 1;
	}
	func = weaponGetFunctionById(weaponnum, FUNC_PRIMARY);
	if (func == NULL || (func->type & 0xff) != INVENTORYFUNCTYPE_SHOOT) {
		return 0;
	}
	g_ChaosAmmoSwapWeapon = weaponnum;
	return 1;
}

// pd.weapon_jam(mode): 1 = trigger pulls dry-fire (bondgun.c reroute);
// 2 = "jam v2": pulls sometimes dry-fire, and a shot that DOES fire jams the
// rest of the magazine (clip drains to 0 — reload to clear). 0 = off.
s32 chraiLuaWeaponJam(s32 mode)
{
	g_ChaosWeaponJam = mode;
	return 1;
}

// pd.force_secondary(on): pin both hands to the secondary weapon function
// (bondmove.c per-tick re-assert, the gun_lock pattern minus auto-fire and
// the switch block).
s32 chraiLuaForceSecondary(s32 on)
{
	g_ChaosForceSecondary = on ? 1 : 0;
	return 1;
}

// pd.ammo_cost(mult): each shot spends mult rounds from the clip
// (bondgun.c post-decrement top-up). 1 = normal. Capped at 1000, more than any
// clip holds, so bondgun.c's shotstotake * (mult - 1) stays in int range.
s32 chraiLuaAmmoCost(s32 mult)
{
	g_ChaosAmmoCost = mult < 1 ? 1 : mult > 1000 ? 1000 : mult;
	return 1;
}

// pd.temu_mag(on): reloads pay full price but only partially refill the clip
// (bondgun.c reload site reads g_ChaosTemuMag). Toggling either way clears
// the all-weapons partial-clip memory (the anti-switch-reload table) so a
// past run's holstered clips can't leak into the next.
s32 chraiLuaTemuMag(s32 on)
{
	g_ChaosTemuMag = on ? 1 : 0;
	bgunChaosTemuSpentClear();
	return 1;
}

// pd.autoaim(on): "XBLA mode" — force aim assist on regardless of the
// player's option (options.c g_ChaosAutoAim).
s32 chraiLuaAutoAim(s32 on)
{
	g_ChaosAutoAim = on ? 1 : 0;
	return 1;
}

// pd.pinball(on): fired physics projectiles become grenade-secondary
// Proximity Pinballs (bondgun.c launch conversion).
s32 chraiLuaPinball(s32 on)
{
	g_ChaosPinball = on ? 1 : 0;
	return 1;
}

// pd.zoom_scale(mult): multiply every weapon's aim-zoom FOV (game_0b0fd0.c);
// > 1 zooms OUT. 1.0 = off.
s32 chraiLuaZoomScale(f32 mult)
{
	if (mult < 0.1f) mult = 0.1f;
	if (mult > 16.0f) mult = 16.0f;
	g_ChaosZoomMult = mult;
	return 1;
}

// pd.gun_sound(weaponnum): every gun's fire sound becomes this weapon's
// primary shoot sound (resolved once here; gsetGetSingleShootSound applies
// it at every consumer). pd.gun_sound() turns it off.
s32 chraiLuaGunSound(s32 weaponnum)
{
	struct gset gset = {0};

	if (weaponnum <= 0) {
		g_ChaosGunSfxOverride = 0;
		return 1;
	}

	gset.weaponnum = weaponnum;
	gset.weaponfunc = FUNC_PRIMARY;
	g_ChaosGunSfxOverride = 0; // resolve against the REAL table, not the override
	g_ChaosGunSfxOverride = (s32)gsetGetSingleShootSound(&gset);
	return g_ChaosGunSfxOverride != 0;
}

// pd.weapon_rename(weaponnum, name): relabel a weapon everywhere its name is
// displayed (HUD, inventory, pickup toast, weapon wheel — full name AND short
// name funnel through langGet). While renamed, the pause-menu inventory also
// hides the weapon's 3D model (func0f105948 checks g_ChaosRenamedWeapon) so a
// spinning Psychosis Gun doesn't undercut the "Nokia 3315" bit. Pass nil/empty
// to restore everything.
s32 chraiLuaWeaponRename(s32 weaponnum, const char *name)
{

	if (name == NULL || name[0] == '\0') {
		g_ChaosLangOverrideId = -1;
		g_ChaosLangOverrideId2 = -1;
		g_ChaosRenamedWeapon = -1;
		return 1;
	}
	// bgunGetNameId/bgunGetShortNameId read g_Weapons[weaponnum] unchecked
	if (weaponFindById(weaponnum) == NULL) {
		return 0;
	}
	g_ChaosLangOverrideId = (s32)bgunGetNameId(weaponnum);
	g_ChaosLangOverrideId2 = (s32)bgunGetShortNameId(weaponnum);
	g_ChaosRenamedWeapon = weaponnum;
	{
		// 31, not sizeof(g_ChaosLangOverrideStr) - 1: langGet hands this
		// string to display code that copies it into fixed label buffers
		// (amGetSlotDetails and its four callers, all char[32] now that the
		// slot-focus block's char[28] has been widened). Those copies take
		// the caller's size, but keeping the rename short means the weapon
		// wheel shows the whole name instead of a cut one.
		s32 i;
		for (i = 0; i < CHAOS_LANG_OVERRIDE_MAX && name[i]; i++) {
			g_ChaosLangOverrideStr[i] = name[i];
		}
		g_ChaosLangOverrideStr[i] = '\0';
		luaApiTextScrub(g_ChaosLangOverrideStr);
	}
	return 1;
}

// pd.weapon_censor(weaponnum, on): render the weapon's MANUFACTURER,
// DESCRIPTION and FIRE-MODE names as "?????" everywhere (inventory menu +
// the HUD function overlay) via the langGet censor list. Pairs with
// pd.weapon_rename for the name itself — together the Blind bag mystery gun
// leaks nothing. on=false (or an invalid weapon) clears the list.
s32 chraiLuaWeaponCensor(s32 weaponnum, s32 on)
{
	struct weapon *wdef;
	struct gset gset = {0};
	s32 n = 0;
	s32 i;

	for (i = 0; i < 8; i++) {
		g_ChaosLangCensorIds[i] = -1;
	}

	if (!on) {
		return 1;
	}

	if (weaponnum < 0 || weaponnum > WEAPON_SUICIDEPILL) {
		return 0;
	}

	wdef = g_Weapons[weaponnum];

	if (wdef == NULL) {
		return 0;
	}

	g_ChaosLangCensorIds[n++] = wdef->manufacturer;
	g_ChaosLangCensorIds[n++] = wdef->description;

	gset.weaponnum = weaponnum;

	for (i = 0; i < 2; i++) {
		struct weaponfunc *func = weaponGetFunction(&gset, i);

		if (func != NULL && n < 8) {
			g_ChaosLangCensorIds[n++] = func->name;
		}
	}

	return 1;
}
