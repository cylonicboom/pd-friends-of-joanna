#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <ultra64.h>
#include "platform.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "types.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/propobj.h"
#include "game/prop.h"
#include "game/setuputils.h"
#include "game/bondgun.h"
#include "game/playerreset.h"
#include "lib/ailist.h"
#include "lib/collision.h"
#include "lib/lib_17ce0.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "net/net.h"
#include "romdata.h"
#include "system.h"
#include "editspawn.h"

/**
 * Editor spawns: chrs, props, glass, weapons, gadgets and ammo placed live at
 * an anchor from the fojOS bar - the crosshair, jo, a register, a pad, a chr,
 * a prop or a room.
 *
 * Live only. Nothing here mints a pad or writes the edits journal: a spawn has
 * pad -1 and is gone after a reload. Minting is its own verb (#307), because
 * a pad is content-pipeline data and spawning must never touch that by accident.
 *
 * The spawn recipes, the catalogs, the P-file prop list and the aim ray come
 * from fireflamesniper's sandbox mode (port/src/sandbox.c in
 * perfect_dark_sandbox_mode, 5527eec6). Their menu, level rules and input
 * hooks were left behind; the bar is the menu here.
 *
 * Budget: EDITSPAWN_RESERVE per stage load. setup.c grows the chr, prop and
 * model tables by that much because none of them can grow mid-stage; the
 * tracker below refuses past it. A corpse keeps its chr slot until reaped, so
 * a spawn can also fail on a full chr table before the budget says so.
 *
 * Off in netplay: no reserve at stage load and every spawn refused. A spawn
 * is local only and never goes over the wire, and without the reserve it
 * would eat the tables' own spare. Collaborative editing is a later maybe.
 */

#define EDITSPAWN_AIM_DIST 4000.f
#define EDITSPAWN_PULLBACK 30.f
#define EDITSPAWN_MAX_PROPS 768

// off = sandbox mode's fixed 30 back along the ray, which is the default
s32 g_EditSpawnSourceCrosshair = 0;

// Source crosshairs, vertically: the low probe ring sits this far off the
// floor at most, so a step or a ramp is not read as a wall; a spawn wants
// this much air between its top and whatever is overhead; and a spawn that
// does not fit under an overhang steps back along the view ray this many
// times looking for room.
#define EDITSPAWN_KNEE 20.f
#define EDITSPAWN_HEADROOM 2.f
#define EDITSPAWN_FIT_STEPS 4

// set by editspawnResolve when no step found headroom, read by editspawnSpawn
static bool s_NoHeadroom = false;

struct editspawnentry {
	const char *name;
	s16 id;    // body, weapon, ammo type or model number, by kind
	s16 model; // pickup model, -1 if none
};

struct editspawntracked {
	struct prop *prop;
	void *entity; // chrdata or defaultobj
	s16 chrnum;
};

static struct editspawntracked s_Tracked[EDITSPAWN_RESERVE];
static s32 s_NumTracked = 0;
static char s_Message[128];

static const struct editspawnentry s_ChrEntries[] = {
	{ "Joanna (Combat)",       BODY_DARK_COMBAT,       -1 },
	{ "Joanna (Trenchcoat)",   BODY_DARK_TRENCH,       -1 },
	{ "Joanna (Frock)",        BODY_DARK_FROCK,        -1 },
	{ "Joanna (Ripped)",       BODY_DARK_RIPPED,       -1 },
	{ "Joanna (Leather)",      BODY_DARK_LEATHER,      -1 },
	{ "Joanna (Negotiator)",   BODY_DARK_NEGOTIATOR,   -1 },
	{ "Joanna (Air Force One)",BODY_DARK_AF1,          -1 },
	{ "Joanna (Wetsuit)",      BODY_DARKWET,           -1 },
	{ "Joanna (Aqualung)",     BODY_DARKAQUALUNG,      -1 },
	{ "Joanna (Snow)",         BODY_DARKSNOW,          -1 },
	{ "Joanna (Lab)",          BODY_DARKLAB,           -1 },
	{ "Daniel Carrington",     BODY_CARRINGTON,        -1 },
	{ "Carrington (Evening)",  BODY_CARREVENINGSUIT,   -1 },
	{ "Cassandra De Vries",    BODY_CASSANDRA,         -1 },
	{ "Trent Easton",          BODY_TRENT,             -1 },
	{ "Jonathan",              BODY_JONATHAN,          -1 },
	{ "Elvis",                 BODY_ELVIS1,            -1 },
	{ "Elvis (Waistcoat)",     BODY_ELVISWAISTCOAT,    -1 },
	{ "Mr. Blonde",            BODY_MRBLONDE,          -1 },
	{ "The President",         BODY_PRESIDENT,         -1 },
	{ "President Clone",       BODY_PRESIDENT_CLONE2,  -1 },
	{ "Dr. Caroll",            BODY_DRCAROLL,          -1 },
	{ "dataDyne Guard",        BODY_DD_GUARD,          -1 },
	{ "dataDyne Shock Trooper",BODY_DDSHOCK,           -1 },
	{ "dataDyne Infantry",     BODY_DD_SHOCK_INF,      -1 },
	{ "dataDyne Sniper",       BODY_DDSNIPER,          -1 },
	{ "dataDyne Security",     BODY_DD_SECGUARD,       -1 },
	{ "dataDyne Lab Tech",     BODY_DD_LABTECH,        -1 },
	{ "dataDyne Female Guard", BODY_FEM_GUARD,         -1 },
	{ "Area 51 Guard",         BODY_AREA51GUARD,       -1 },
	{ "Area 51 Trooper",       BODY_A51TROOPER,        -1 },
	{ "Area 51 Airman",        BODY_A51AIRMAN,         -1 },
	{ "Overalls Worker",       BODY_OVERALL,           -1 },
	{ "Stripes",               BODY_STRIPES,           -1 },
	{ "Lab Technician",        BODY_LABTECH,           -1 },
	{ "Female Lab Technician", BODY_FEMLABTECH,        -1 },
	{ "Biotech",               BODY_BIOTECH,           -1 },
	{ "Office Worker",         BODY_OFFICEWORKER,      -1 },
	{ "Office Worker 2",       BODY_OFFICEWORKER2,     -1 },
	{ "Secretary",             BODY_SECRETARY,         -1 },
	{ "FBI Agent",             BODY_FBIGUY,            -1 },
	{ "CIA Agent",             BODY_CIAGUY,            -1 },
	{ "NSA Lackey",            BODY_NSA_LACKEY,        -1 },
	{ "Presidential Security", BODY_PRES_SECURITY,     -1 },
	{ "Negotiator",            BODY_NEGOTIATOR,        -1 },
	{ "Steward",               BODY_STEWARD,           -1 },
	{ "Stewardess",            BODY_STEWARDESS,        -1 },
	{ "Stewardess (Coat)",     BODY_STEWARDESS_COAT,   -1 },
	{ "Air Force One Pilot",   BODY_PILOTAF1,          -1 },
	{ "G5 Guard",              BODY_G5_GUARD,          -1 },
	{ "G5 SWAT Guard",         BODY_G5_SWAT_GUARD,     -1 },
	{ "Pelagic II Guard",      BODY_PELAGIC_GUARD,     -1 },
	{ "Alaskan Guard",         BODY_ALASKAN_GUARD,     -1 },
	{ "Carrington Inst. Soldier", BODY_CISOLDIER,      -1 },
	{ "Carrington Inst. Tech", BODY_CILABTECH,         -1 },
	{ "Carrington Inst. Tech (F)", BODY_CIFEMTECH,     -1 },
	{ "Maian Soldier",         BODY_MAIAN_SOLDIER,     -1 },
	{ "Skedar Warrior",        BODY_SKEDAR,            -1 },
	{ "Mini Skedar",           BODY_MINISKEDAR,        -1 },
	{ "Skedar King",           BODY_SKEDARKING,        -1 },
	{ "The King",              BODY_THEKING,           -1 },
	{ "Chicago Robot",         BODY_CHICROB,           -1 },
};

static const struct editspawnentry s_WeaponEntries[] = {
	{ NULL, WEAPON_FALCON2,           MODEL_CHRFALCON2 },
	{ NULL, WEAPON_FALCON2_SILENCER,  MODEL_CHRFALCON2SIL },
	{ NULL, WEAPON_FALCON2_SCOPE,     MODEL_CHRFALCON2SCOPE },
	{ NULL, WEAPON_MAGSEC4,           MODEL_CHRLEEGUN1 },
	{ NULL, WEAPON_MAULER,            MODEL_CHRMAULER },
	{ NULL, WEAPON_PHOENIX,           MODEL_CHRMAIANPISTOL },
	{ NULL, WEAPON_DY357MAGNUM,       MODEL_CHRDY357 },
	{ NULL, WEAPON_DY357LX,           MODEL_CHRDY357TRENT },
	{ NULL, WEAPON_CMP150,            MODEL_CHRCMP150 },
	{ NULL, WEAPON_CYCLONE,           MODEL_CHRCYCLONE },
	{ NULL, WEAPON_CALLISTO,          MODEL_CHRMAIANSMG },
	{ NULL, WEAPON_RCP120,            MODEL_CHRRCP120 },
	{ NULL, WEAPON_LAPTOPGUN,         MODEL_CHRPCGUN },
	{ NULL, WEAPON_DRAGON,            MODEL_CHRDRAGON },
	{ NULL, WEAPON_K7AVENGER,         MODEL_CHRAVENGER },
	{ NULL, WEAPON_AR34,              MODEL_CHRAR34 },
	{ NULL, WEAPON_SUPERDRAGON,       MODEL_CHRSUPERDRAGON },
	{ NULL, WEAPON_SHOTGUN,           MODEL_CHRSHOTGUN },
	{ NULL, WEAPON_REAPER,            MODEL_CHRSKMINIGUN },
	{ NULL, WEAPON_SNIPERRIFLE,       MODEL_CHRSNIPERRIFLE },
	{ NULL, WEAPON_FARSIGHT,          MODEL_CHRZ2020 },
	{ NULL, WEAPON_DEVASTATOR,        MODEL_CHRDEVASTATOR },
	{ NULL, WEAPON_ROCKETLAUNCHER,    MODEL_CHRDYROCKET },
	{ NULL, WEAPON_SLAYER,            MODEL_CHRSKROCKET },
	{ NULL, WEAPON_COMBATKNIFE,       MODEL_CHRKNIFE },
	{ NULL, WEAPON_CROSSBOW,          MODEL_CHRCROSSBOW },
	{ NULL, WEAPON_TRANQUILIZER,      MODEL_CHRDRUGGUN },
	{ NULL, WEAPON_LASER,             MODEL_CHRLASER },
	{ NULL, WEAPON_GRENADE,           MODEL_CHRGRENADE },
	{ NULL, WEAPON_NBOMB,             MODEL_CHRNBOMB },
	{ NULL, WEAPON_TIMEDMINE,         MODEL_CHRTIMEDMINE },
	{ NULL, WEAPON_PROXIMITYMINE,     MODEL_CHRPROXIMITYMINE },
	{ NULL, WEAPON_REMOTEMINE,        MODEL_CHRREMOTEMINE },
	{ NULL, WEAPON_PP9I,              MODEL_CHRWPPK },
	{ NULL, WEAPON_CC13,              MODEL_CHRTT33 },
	{ NULL, WEAPON_KL01313,           MODEL_CHRSKORPION },
	{ NULL, WEAPON_KF7SPECIAL,        MODEL_CHRKALASH },
	{ NULL, WEAPON_ZZT,               MODEL_CHRUZI },
	{ NULL, WEAPON_DMC,               MODEL_CHRMP5K },
	{ NULL, WEAPON_AR53,              MODEL_CHRM16 },
	{ NULL, WEAPON_RCP45,             MODEL_CHRFNP90 },
	{ NULL, WEAPON_PSYCHOSISGUN,      MODEL_CHRDRUGGUN },
	{ NULL, WEAPON_HAMMER,            MODEL_CHRLUMPHAMMER },
	{ NULL, WEAPON_SCREWDRIVER,       MODEL_CHRSONICSCREWER },
};

// id -1 is the shield, which is its own object type rather than a weapon
static const struct editspawnentry s_GadgetEntries[] = {
	{ NULL, WEAPON_NIGHTVISION,       MODEL_CHRNIGHTSIGHT },
	{ NULL, WEAPON_XRAYSCANNER,       MODEL_XRAYSPECS },
	{ NULL, WEAPON_IRSCANNER,         MODEL_CHRNIGHTSIGHT },
	{ NULL, WEAPON_EYESPY,            MODEL_CHREYESPY },
	{ NULL, WEAPON_CLOAKINGDEVICE,    MODEL_CHRCLOAKER },
	{ NULL, WEAPON_COMBATBOOST,       MODEL_CHRSPEEDPILL },
	{ NULL, WEAPON_ECMMINE,           MODEL_CHRECMMINE },
	{ NULL, WEAPON_DATAUPLINK,        MODEL_CHRDATATHIEF },
	{ NULL, WEAPON_DOORDECODER,       MODEL_CHRDOORDECODER },
	{ NULL, WEAPON_TRACERBUG,         MODEL_CHRBUG },
	{ NULL, WEAPON_AUTOSURGEON,       MODEL_AUTOSURGEON },
	{ NULL, WEAPON_EXPLOSIVES,        MODEL_EXPLOSIVEBRICK },
	{ NULL, WEAPON_SKEDARBOMB,        MODEL_SKEDARBOMB },
	{ NULL, WEAPON_KEYCARD45,         MODEL_KEYCARD },
	{ NULL, WEAPON_SUITCASE,          MODEL_SUITCASE },
	{ NULL, WEAPON_BRIEFCASE,         MODEL_CHRBRIEFCASE },
	{ "Shield",          -1,          MODEL_CHRSHIELD },
};

// id -1 fills every slot
static const struct editspawnentry s_AmmoEntries[] = {
	{ "Pistol Ammo",         AMMOTYPE_PISTOL,      -1 },
	{ "SMG Ammo",            AMMOTYPE_SMG,         -1 },
	{ "Rifle Ammo",          AMMOTYPE_RIFLE,       -1 },
	{ "Shotgun Shells",      AMMOTYPE_SHOTGUN,     -1 },
	{ "Magnum Rounds",       AMMOTYPE_MAGNUM,      -1 },
	{ "Reaper Ammo",         AMMOTYPE_REAPER,      -1 },
	{ "Farsight Rounds",     AMMOTYPE_FARSIGHT,    -1 },
	{ "Crossbow Bolts",      AMMOTYPE_CROSSBOW,    -1 },
	{ "Tranquilizer Darts",  AMMOTYPE_SEDATIVE,    -1 },
	{ "Devastator Grenades", AMMOTYPE_DEVASTATOR,  -1 },
	{ "Rockets",             AMMOTYPE_ROCKET,      -1 },
	{ "Homing Rockets",      AMMOTYPE_HOMINGROCKET, -1 },
	{ "Grenades",            AMMOTYPE_GRENADE,     -1 },
	{ "N-Bombs",             AMMOTYPE_NBOMB,       -1 },
	{ "Combat Knives",       AMMOTYPE_KNIFE,       -1 },
	{ "Timed Mines",         AMMOTYPE_TIMED_MINE,  -1 },
	{ "Proximity Mines",     AMMOTYPE_PROXY_MINE,  -1 },
	{ "Remote Mines",        AMMOTYPE_REMOTE_MINE, -1 },
	{ "Psychosis Darts",     AMMOTYPE_DART,        -1 },
	{ "All Ammo Types",      -1,                   -1 },
};

// props and glass come from the model table, so every P file in the ROM shows up
static struct editspawnentry s_PropEntries[EDITSPAWN_MAX_PROPS];
static s32 s_NumPropEntries = 0;
static struct editspawnentry s_GlassEntries[64];
static s32 s_NumGlassEntries = 0;
static char s_PropNames[EDITSPAWN_MAX_PROPS][40];
static s32 s_CatalogBuilt = false;

static void editspawnSay(const char *fmt, const char *name)
{
	snprintf(s_Message, sizeof(s_Message), fmt, name ? name : "");
	sysLogPrintf(LOG_NOTE, "editspawn: %s", s_Message);
}

const char *editspawnLastMessage(void)
{
	return s_Message;
}

s32 editspawnReserve(s32 stagenum)
{
	if (g_NetMode != NETMODE_NONE) {
		return 0;
	}

	return stagenum < STAGE_TITLE ? EDITSPAWN_RESERVE : 0;
}

void editspawnStageInit(void)
{
	// every prop and chr pointer died with the old stage pool
	s_NumTracked = 0;
}

/* catalog */

static s32 editspawnIsExcludedProp(const char *name)
{
	static const char *excluded[] = {
		"Pchr",          // weapons and gadgets have their own lists
		"Pnintendologo", "Pnlogo", "Pjpnlogo", "Pjpnpd", "Pperfectdark",
		"Ppdone", "Ppdtwo", "Ppdthree", "Ppdfour", "Pmarker",
	};

	for (s32 i = 0; i < ARRAYCOUNT(excluded); i++) {
		if (strncmp(name, excluded[i], strlen(excluded[i])) == 0) {
			return true;
		}
	}

	return false;
}

static s32 editspawnIsGlassName(const char *name)
{
	char lower[64];
	s32 i;

	for (i = 0; name[i] && i < ARRAYCOUNT(lower) - 1; i++) {
		lower[i] = tolower((unsigned char)name[i]);
	}

	lower[i] = '\0';

	return strstr(lower, "window") || strstr(lower, "glass") || strstr(lower, "wind");
}

// "Pa51_crate1" -> "a51 crate1": the P goes, underscores become spaces. The
// case stays as the file has it, so the name still greps against the ROM.
static void editspawnPropName(const char *src, char *dst, s32 dstlen)
{
	s32 i = 0;

	if (*src == 'P') {
		src++;
	}

	for (; *src && i < dstlen - 1; src++) {
		dst[i++] = (*src == '_') ? ' ' : *src;
	}

	dst[i] = '\0';
}

static void editspawnBuildCatalog(void)
{
	static u16 seenfiles[NUM_MODELS];
	s32 numseen = 0;

	if (s_CatalogBuilt) {
		return;
	}

	s_NumPropEntries = 0;
	s_NumGlassEntries = 0;

	for (s32 modelnum = 0; modelnum < NUM_MODELS && s_NumPropEntries < EDITSPAWN_MAX_PROPS; modelnum++) {
		const u16 fileid = g_ModelStates[modelnum].fileid;
		const char *name = romdataFileGetName(fileid);
		s32 seen = false;

		if (!name || name[0] != 'P' || editspawnIsExcludedProp(name)) {
			continue;
		}

		// several model numbers can share a file
		for (s32 i = 0; i < numseen; i++) {
			if (seenfiles[i] == fileid) {
				seen = true;
				break;
			}
		}

		if (seen) {
			continue;
		}

		seenfiles[numseen++] = fileid;

		editspawnPropName(name, s_PropNames[s_NumPropEntries], sizeof(s_PropNames[0]));
		s_PropEntries[s_NumPropEntries].name = s_PropNames[s_NumPropEntries];
		s_PropEntries[s_NumPropEntries].id = modelnum;
		s_PropEntries[s_NumPropEntries].model = modelnum;

		if (editspawnIsGlassName(name) && s_NumGlassEntries < ARRAYCOUNT(s_GlassEntries)) {
			s_GlassEntries[s_NumGlassEntries++] = s_PropEntries[s_NumPropEntries];
		}

		s_NumPropEntries++;
	}

	sysLogPrintf(LOG_NOTE, "editspawn: catalog has %d props, %d glass", s_NumPropEntries, s_NumGlassEntries);

	s_CatalogBuilt = true;
}

s32 editspawnCatalogCount(s32 kind)
{
	switch (kind) {
	case EDITSPAWN_CHR:
	case EDITSPAWN_ALLY:   return ARRAYCOUNT(s_ChrEntries);
	case EDITSPAWN_WEAPON: return ARRAYCOUNT(s_WeaponEntries);
	case EDITSPAWN_GADGET: return ARRAYCOUNT(s_GadgetEntries);
	case EDITSPAWN_AMMO:   return ARRAYCOUNT(s_AmmoEntries);
	case EDITSPAWN_PROP:   editspawnBuildCatalog(); return s_NumPropEntries;
	case EDITSPAWN_GLASS:  editspawnBuildCatalog(); return s_NumGlassEntries;
	}

	return 0;
}

static const struct editspawnentry *editspawnGetEntry(s32 kind, s32 index)
{
	if (index < 0 || index >= editspawnCatalogCount(kind)) {
		return NULL;
	}

	switch (kind) {
	case EDITSPAWN_CHR:
	case EDITSPAWN_ALLY:   return &s_ChrEntries[index];
	case EDITSPAWN_WEAPON: return &s_WeaponEntries[index];
	case EDITSPAWN_GADGET: return &s_GadgetEntries[index];
	case EDITSPAWN_AMMO:   return &s_AmmoEntries[index];
	case EDITSPAWN_PROP:   return &s_PropEntries[index];
	case EDITSPAWN_GLASS:  return &s_GlassEntries[index];
	}

	return NULL;
}

s32 editspawnCatalogModel(s32 kind, s32 index)
{
	const struct editspawnentry *entry = editspawnGetEntry(kind, index);
	return entry ? entry->model : -1;
}

// a weapon's name comes from the lang bank and ends in a newline
const char *editspawnCatalogName(s32 kind, s32 index)
{
	static char buf[64];
	const struct editspawnentry *entry = editspawnGetEntry(kind, index);
	char *name;
	s32 len;

	if (!entry) {
		return "";
	}

	if (entry->name) {
		return entry->name;
	}

	name = bgunGetName(entry->id);
	strncpy(buf, name ? name : "???", sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	len = strlen(buf);

	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == ' ')) {
		buf[--len] = '\0';
	}

	return buf;
}

/* aim */

static f32 editspawnYawToPlayer(const struct coord *pos)
{
	struct prop *pprop = g_Vars.currentplayer->prop;
	return atan2f(pprop->pos.x - pos->x, pprop->pos.z - pos->z);
}

// The view ray, stopped by bg, doors and objects. What comes back is the ray
// and where it ended; how far back off the surface a spawn sits is decided
// per spawn in editspawnResolve, since that depends on the spawn's size.
bool editspawnAimCrosshair(struct editspawnaim *aim)
{
	struct player *player = g_Vars.currentplayer;
	struct coord to;
	f32 len;

	memset(aim, 0, sizeof(*aim));

	if (!player || !player->prop) {
		editspawnSay("no player to aim from", NULL);
		return false;
	}

	aim->eye = player->cam_pos;
	aim->dir = player->cam_look;
	len = sqrtf(aim->dir.x * aim->dir.x + aim->dir.y * aim->dir.y + aim->dir.z * aim->dir.z);

	if (len < 0.0001f) {
		editspawnSay("the camera has no look direction", NULL);
		return false;
	}

	aim->dir.x /= len;
	aim->dir.y /= len;
	aim->dir.z /= len;

	portal00018148(&player->prop->pos, &aim->eye, player->prop->rooms, aim->eyerooms, NULL, 0);

	to.x = aim->eye.x + aim->dir.x * EDITSPAWN_AIM_DIST;
	to.y = aim->eye.y + aim->dir.y * EDITSPAWN_AIM_DIST;
	to.z = aim->eye.z + aim->dir.z * EDITSPAWN_AIM_DIST;

	if (cdExamLos08(&aim->eye, aim->eyerooms, &to, CDTYPE_BG | CDTYPE_DOORS | CDTYPE_OBJS,
				GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2 | GEOFLAG_WALL | GEOFLAG_BLOCK_SHOOT) == CDRESULT_COLLISION) {
		cdGetPos(&to, __LINE__, "editspawn.c");
	}

	aim->hitdist = sqrtf((to.x - aim->eye.x) * (to.x - aim->eye.x)
			+ (to.y - aim->eye.y) * (to.y - aim->eye.y)
			+ (to.z - aim->eye.z) * (to.z - aim->eye.z));
	aim->pos = to;
	aim->crosshair = true;
	aim->valid = true;

	return true;
}

// An anchor from the bar: a register, jo, a pad, a room, or a chr/prop. A
// chr or prop anchor is something to spawn beside, not inside, so the point
// steps from it towards the player by its radius plus a margin. Rooms come
// from the same portal walk noclip and teleport use, from the player.
bool editspawnAimAt(struct editspawnaim *aim, const struct coord *pos, struct prop *anchorprop)
{
	struct player *player = g_Vars.currentplayer;
	struct coord target = *pos;

	memset(aim, 0, sizeof(*aim));

	if (!player || !player->prop) {
		editspawnSay("no player to anchor from", NULL);
		return false;
	}

	if (anchorprop) {
		f32 radius = 50.f;
		f32 dx = player->prop->pos.x - anchorprop->pos.x;
		f32 dz = player->prop->pos.z - anchorprop->pos.z;
		f32 d = sqrtf(dx * dx + dz * dz);

		if ((anchorprop->type == PROPTYPE_CHR || anchorprop->type == PROPTYPE_PLAYER) && anchorprop->chr) {
			radius = anchorprop->chr->radius;
		}

		if (d > 0.001f) {
			target.x = anchorprop->pos.x + dx / d * (radius + 40.f);
			target.z = anchorprop->pos.z + dz / d * (radius + 40.f);
		}
	}

	func0f065e74(&player->prop->pos, player->prop->rooms, &target, aim->rooms);

	if (aim->rooms[0] <= 0 && anchorprop) {
		roomsCopy(anchorprop->rooms, aim->rooms);
	}

	if (aim->rooms[0] <= 0) {
		editspawnSay("that anchor is in no room", NULL);
		return false;
	}

	aim->pos = target;
	aim->yaw = editspawnYawToPlayer(&target);
	aim->valid = true;

	return true;
}

// Perfect dark source crosshairs: probe eight ways around the point and push
// it out of anything closer than the spawn's clearance. Each way is probed at
// every height in ys and the deepest hit counts, so a wall that only reaches
// the spawn's knees or its head still pushes. Opposite walls closer together
// than twice the clearance cancel, which centres the spawn between them. Each
// step is taken only if the portal walk still finds a room.
//
// The probes start from rooms[] even when ys[] is above or below pos->y.
// Rooms in PD run floor to ceiling, so this holds everywhere except a probe
// that crosses into a storey above; there the probe is simply blind.
static void editspawnPushOut(struct coord *pos, RoomNum *rooms, f32 clearance, const f32 *ys, s32 numys)
{
	for (s32 iter = 0; iter < 2; iter++) {
		struct coord push = { 0, 0, 0 };
		struct coord next;
		RoomNum nextrooms[8];
		s32 hits = 0;

		for (s32 i = 0; i < 8; i++) {
			const f32 a = i * (M_PI / 4.f);
			const f32 dx = sinf(a);
			const f32 dz = cosf(a);
			f32 deepest = 0.f;

			for (s32 h = 0; h < numys; h++) {
				struct coord from;
				struct coord to;

				from.x = pos->x;
				from.y = ys[h];
				from.z = pos->z;

				to.x = pos->x + dx * clearance;
				to.y = ys[h];
				to.z = pos->z + dz * clearance;

				if (cdExamLos08(&from, rooms, &to, CDTYPE_BG | CDTYPE_DOORS | CDTYPE_OBJS,
							GEOFLAG_WALL | GEOFLAG_BLOCK_SHOOT) == CDRESULT_COLLISION) {
					struct coord hit;
					f32 d;

					cdGetPos(&hit, __LINE__, "editspawn.c");
					d = sqrtf((hit.x - pos->x) * (hit.x - pos->x) + (hit.z - pos->z) * (hit.z - pos->z));

					if (clearance - d > deepest) {
						deepest = clearance - d;
					}
				}
			}

			if (deepest > 0.f) {
				push.x -= dx * deepest;
				push.z -= dz * deepest;
				hits++;
			}
		}

		if (hits == 0) {
			return;
		}

		next.x = pos->x + push.x;
		next.y = pos->y;
		next.z = pos->z + push.z;

		portal00018148(pos, &next, rooms, nextrooms, NULL, 0);

		if (nextrooms[0] <= 0) {
			return;
		}

		*pos = next;
		roomsCopy(nextrooms, rooms);
	}
}

// The floor a spawn at pos will be dropped onto, and the ceiling over that
// floor. A ceiling here is the underside of the nearest floor tile overhead -
// a ledge, a walkway, the next storey. A room's drawn ceiling is not
// collision, so nothing can see it, and a spawn can still poke through one.
// No ceiling found reads as unlimited headroom.
static bool editspawnSpan(struct coord *pos, RoomNum *rooms, f32 *floory, f32 *ceily)
{
	struct coord probe;
	f32 y = 0.f;
	f32 c = 0.f;

	if (cdFindFloorRoomYColourFlagsAtPos(pos, rooms, &y, NULL, NULL) <= 0) {
		return false;
	}

	*floory = y;
	*ceily = y + 1000000.f;

	probe = *pos;
	probe.y = y + 1.f;
	c = probe.y;

	if (cdFindCeilingRoomYColourFlagsAtPos(&probe, rooms, &c, NULL, NULL) > 0 && c > probe.y) {
		*ceily = c;
	}

	return true;
}

// Source crosshairs with height. Both callers drop the spawn onto the floor
// below the point, so the walls that matter are the ones beside where it
// will stand, not beside where the ray hit: the push-out probes at knee
// height and near the top of the spawn, measured up from that floor. Then,
// if what is overhead leaves less than the spawn's height, it steps back
// along the view ray - which is open all the way to the eye, it is how the
// point was found - and tries again. Past EDITSPAWN_FIT_STEPS, or halfway
// back to the eye, it keeps the last try and s_NoHeadroom says so.
static void editspawnFit(struct editspawnaim *aim, struct coord *pos, RoomNum *rooms, f32 clearance, f32 height, f32 pulled)
{
	const f32 step = clearance > 20.f ? clearance : 20.f;

	for (s32 i = 0; ; i++) {
		struct coord next;
		RoomNum nextrooms[8];
		f32 floory;
		f32 ceily;
		f32 ys[2];
		f32 knee = height * 0.25f;

		if (knee > EDITSPAWN_KNEE) {
			knee = EDITSPAWN_KNEE;
		}

		if (!editspawnSpan(pos, rooms, &floory, &ceily)) {
			// nothing below to drop onto: probe where the point is
			ys[0] = pos->y;
			editspawnPushOut(pos, rooms, clearance, ys, 1);
			return;
		}

		ys[0] = floory + knee;
		ys[1] = floory + height * 0.9f;
		editspawnPushOut(pos, rooms, clearance, ys, height * 0.9f > knee ? 2 : 1);

		// the push can move it over a different floor
		if (!editspawnSpan(pos, rooms, &floory, &ceily)
				|| ceily - floory >= height + EDITSPAWN_HEADROOM) {
			return;
		}

		if (i >= EDITSPAWN_FIT_STEPS || pulled + step > aim->hitdist * 0.5f) {
			s_NoHeadroom = true;
			sysLogPrintf(LOG_NOTE, "editspawn: %.0f under the overhang, needs %.0f",
					ceily - floory, height + EDITSPAWN_HEADROOM);
			return;
		}

		pulled += step;
		next.x = aim->pos.x - aim->dir.x * pulled;
		next.y = aim->pos.y - aim->dir.y * pulled;
		next.z = aim->pos.z - aim->dir.z * pulled;

		portal00018148(&aim->eye, &next, aim->eyerooms, nextrooms, NULL, 0);

		if (nextrooms[0] <= 0) {
			s_NoHeadroom = true;
			return;
		}

		*pos = next;
		roomsCopy(nextrooms, rooms);
	}
}

// Where this spawn actually goes. An anchor is used as given. A crosshair
// point is backed off the surface it hit: by the fixed 30 sandbox mode uses,
// or, with source crosshairs on, by the spawn's own clearance - never more
// than half the distance to the eye, so it cannot end up behind the camera -
// and then fitted: pushed out of walls at the height it will stand, and
// stepped back from an overhang too low for it (editspawnFit).
static bool editspawnResolve(struct editspawnaim *aim, f32 clearance, f32 height, struct coord *pos, RoomNum *rooms)
{
	f32 pull = EDITSPAWN_PULLBACK;

	if (!aim->valid) {
		return false;
	}

	if (!aim->crosshair) {
		*pos = aim->pos;
		roomsCopy(aim->rooms, rooms);
		return true;
	}

	if (g_EditSpawnSourceCrosshair) {
		if (clearance > pull) {
			pull = clearance;
		}
		if (pull > aim->hitdist * 0.5f) {
			pull = aim->hitdist * 0.5f;
		}
	}

	pos->x = aim->pos.x - aim->dir.x * pull;
	pos->y = aim->pos.y - aim->dir.y * pull;
	pos->z = aim->pos.z - aim->dir.z * pull;

	portal00018148(&aim->eye, pos, aim->eyerooms, rooms, NULL, 0);

	if (rooms[0] <= 0) {
		// the ray left the map: the player's own spot
		struct prop *pprop = g_Vars.currentplayer->prop;
		*pos = pprop->pos;
		roomsCopy(pprop->rooms, rooms);
	} else if (g_EditSpawnSourceCrosshair) {
		editspawnFit(aim, pos, rooms, clearance, height, pull);
	}

	aim->yaw = editspawnYawToPlayer(pos);

	return true;
}

/* tracking */

static void editspawnTrack(struct prop *prop)
{
	struct editspawntracked *t;

	if (!prop || s_NumTracked >= EDITSPAWN_RESERVE) {
		return;
	}

	t = &s_Tracked[s_NumTracked++];
	t->prop = prop;

	if (prop->type == PROPTYPE_CHR) {
		t->entity = prop->chr;
		t->chrnum = prop->chr->chrnum;
	} else {
		t->entity = prop->obj;
		t->chrnum = -1;
	}
}

static bool editspawnRemove(struct editspawntracked *t)
{
	if (t->chrnum >= 0) {
		struct chrdata *chr = t->entity;

		if (chr->chrnum == t->chrnum && chr->prop == t->prop) {
			chr->hidden |= CHRHFLAG_DELETING;
			return true;
		}
	} else {
		struct defaultobj *obj = t->entity;

		// slots get recycled (a weapon picked up is a different weapon), so
		// make sure this is still the object we made
		if (obj->prop == t->prop && t->prop->obj == obj && obj->model) {
			obj->hidden |= OBJHFLAG_DELETING;
			return true;
		}
	}

	return false;
}

s32 editspawnCount(void)
{
	return s_NumTracked;
}

bool editspawnUndo(void)
{
	while (s_NumTracked > 0) {
		if (editspawnRemove(&s_Tracked[--s_NumTracked])) {
			editspawnSay("undid the last spawn", NULL);
			return true;
		}
	}

	editspawnSay("nothing to undo", NULL);
	return false;
}

s32 editspawnClear(void)
{
	s32 n = 0;
	char buf[16];

	while (s_NumTracked > 0) {
		n += editspawnRemove(&s_Tracked[--s_NumTracked]) ? 1 : 0;
	}

	snprintf(buf, sizeof(buf), "%d", n);
	editspawnSay("cleared %s spawns", buf);
	return n;
}

/* spawning */

static void editspawnInitObj(struct defaultobj *obj, s32 type, s32 modelnum)
{
	obj->extrascale = 256;
	obj->hidden2 = 0;
	obj->type = type;
	obj->modelnum = modelnum;
	obj->pad = -1;
	obj->flags = 0;
	obj->flags2 = 0;
	obj->flags3 = 0;
	obj->prop = NULL;
	obj->model = NULL;
	obj->realrot[0][0] = 1; obj->realrot[0][1] = 0; obj->realrot[0][2] = 0;
	obj->realrot[1][0] = 0; obj->realrot[1][1] = 1; obj->realrot[1][2] = 0;
	obj->realrot[2][0] = 0; obj->realrot[2][1] = 0; obj->realrot[2][2] = 1;
	obj->hidden = 0;
	obj->unkgeo = NULL;
	obj->projectile = NULL;
	obj->damage = 0;
	obj->maxdamage = 1000;
	obj->shadecol[0] = obj->shadecol[1] = obj->shadecol[2] = 0xff;
	obj->shadecol[3] = 0;
	obj->nextcol[0] = obj->nextcol[1] = obj->nextcol[2] = 0xff;
	obj->nextcol[3] = 0;
	obj->floorcol = 0xfff;
	obj->geocount = 0;
}

// the model's horizontal extent, scaled: what a wall must stay clear of
static f32 editspawnObjClearance(struct defaultobj *obj)
{
	struct modelrodata_bbox *bbox = modelFindBboxRodata(obj->model);
	f32 r = 20.f;

	if (bbox) {
		f32 m = fabsf(bbox->xmin);
		if (fabsf(bbox->xmax) > m) m = fabsf(bbox->xmax);
		if (fabsf(bbox->zmin) > m) m = fabsf(bbox->zmin);
		if (fabsf(bbox->zmax) > m) m = fabsf(bbox->zmax);
		r = m * obj->model->scale;
	}

	return r;
}

// the model's height, scaled: what has to fit under an overhang
static f32 editspawnObjHeight(struct defaultobj *obj)
{
	struct modelrodata_bbox *bbox = modelFindBboxRodata(obj->model);

	if (bbox && bbox->ymax > bbox->ymin) {
		return (bbox->ymax - bbox->ymin) * obj->model->scale;
	}

	return 20.f;
}

static struct prop *editspawnPlaceObj(struct defaultobj *obj, struct editspawnaim *aim)
{
	struct modeldef *modeldef;
	struct prop *prop;
	struct coord pos;
	RoomNum rooms[8];
	Mtxf mtx;

	if (!setupLoadModeldef(obj->modelnum)) {
		return NULL;
	}

	modeldef = g_ModelStates[obj->modelnum].modeldef;

	if (!modeldef) {
		return NULL;
	}

	if (obj->type == OBJTYPE_WEAPON) {
		prop = func0f08ae0c((struct weaponobj *)obj, modeldef);
	} else {
		prop = objInitWithModelDef(obj, modeldef);
	}

	if (!prop || !obj->model) {
		return NULL;
	}

	modelSetScale(obj->model, obj->model->scale * (obj->extrascale * (1.0f / 256.0f)));

	// the model exists now, so its size is known before it is placed
	if (!editspawnResolve(aim, editspawnObjClearance(obj), editspawnObjHeight(obj), &pos, rooms)) {
		return NULL;
	}

	mtx4LoadYRotation(aim->yaw, &mtx);
	mtx00015f04(obj->model->scale, &mtx);

	// drops the object onto whatever floor is below the point
	func0f06a730(obj, &pos, &mtx, rooms, &pos);

	propActivate(prop);
	propEnable(prop);

	return prop;
}

static struct prop *editspawnWeapon(s32 weaponnum, s32 modelnum, struct editspawnaim *aim)
{
	struct weaponobj *weapon;
	struct modeldef *modeldef;

	if (modelnum < 0 || !setupLoadModeldef(modelnum)) {
		return NULL;
	}

	modeldef = g_ModelStates[modelnum].modeldef;
	weapon = weaponCreate(false, false, modeldef);

	if (!weapon) {
		return NULL;
	}

	memset(weapon, 0, sizeof(*weapon));
	editspawnInitObj(&weapon->base, OBJTYPE_WEAPON, modelnum);
	weapon->weaponnum = weaponnum;
	weapon->gunfunc = FUNC_PRIMARY;
	weapon->dualweaponnum = -1;
	weapon->timer240 = -1;
	weapon->dualweapon = NULL;

	modelmgrLoadProjectileModeldefs(weaponnum);

	return editspawnPlaceObj(&weapon->base, aim);
}

static struct prop *editspawnShield(struct editspawnaim *aim)
{
	struct shieldobj *shield = mempAlloc(ALIGN16(sizeof(struct shieldobj)), MEMPOOL_STAGE);

	if (!shield) {
		return NULL;
	}

	memset(shield, 0, sizeof(*shield));
	editspawnInitObj(&shield->base, OBJTYPE_SHIELD, MODEL_CHRSHIELD);
	shield->initialamount = 1.0f;
	shield->amount = 1.0f;

	return editspawnPlaceObj(&shield->base, aim);
}

static struct prop *editspawnAmmo(s32 ammotype, struct editspawnaim *aim)
{
	struct multiammocrateobj *crate = mempAlloc(ALIGN16(sizeof(struct multiammocrateobj)), MEMPOOL_STAGE);

	if (!crate) {
		return NULL;
	}

	memset(crate, 0, sizeof(*crate));
	editspawnInitObj(&crate->base, OBJTYPE_MULTIAMMOCRATE, MODEL_MULTI_AMMO_CRATE);

	for (s32 i = 0; i < ARRAYCOUNT(crate->slots); i++) {
		const s32 type = i + 1;

		crate->slots[i].modelnum = 0xffff;
		crate->slots[i].quantity = 0;

		if (type == ammotype || (ammotype < 0 && type <= AMMOTYPE_NBOMB)) {
			s32 qty = bgunGetCapacityByAmmotype(type);
			crate->slots[i].quantity = qty > 0 ? qty : 1;
		}
	}

	return editspawnPlaceObj(&crate->base, aim);
}

static struct prop *editspawnProp(s32 modelnum, bool asglass, struct editspawnaim *aim)
{
	struct defaultobj *obj;

	if (asglass) {
		struct glassobj *glass = mempAlloc(ALIGN16(sizeof(struct glassobj)), MEMPOOL_STAGE);

		if (!glass) {
			return NULL;
		}

		memset(glass, 0, sizeof(*glass));
		editspawnInitObj(&glass->base, OBJTYPE_GLASS, modelnum);
		glass->portalnum = -1;
		glass->base.flags = OBJFLAG_AISEETHROUGH;
		glass->base.maxdamage = 100;
		obj = &glass->base;
	} else {
		obj = mempAlloc(ALIGN16(sizeof(struct defaultobj)), MEMPOOL_STAGE);

		if (!obj) {
			return NULL;
		}

		memset(obj, 0, sizeof(*obj));
		editspawnInitObj(obj, OBJTYPE_BASIC, modelnum);
	}

	return editspawnPlaceObj(obj, aim);
}

// Unarmed: what a spawned chr carries is the inspector's business, not a
// third stage in the bar.
static struct prop *editspawnChr(s32 bodynum, bool ally, struct editspawnaim *aim)
{
	struct coord pos;
	RoomNum rooms[8];
	struct prop *prop;
	struct chrdata *chr;
	f32 floory;
	u8 *ailist;

	// the chr is not made yet, so its size is the stock one: radius 30, and
	// the height of a standing guard (setup.c's DrCaroll fixup uses 185 too)
	if (!editspawnResolve(aim, 30.f, 185.f, &pos, rooms)) {
		return NULL;
	}

	// stand on the floor below the point
	if (cdFindFloorRoomYColourFlagsAtPos(&pos, rooms, &floory, NULL, NULL) > 0) {
		pos.y = floory + 10.f;
	}

	ailist = ailistFindById(ally ? GAILIST_INIT_DEFAULT_BUDDY : GAILIST_UNALERTED);
	prop = chrSpawnAtCoord(bodynum, -1, &pos, rooms, aim->yaw, ailist, SPAWNFLAG_ALLOWONSCREEN);

	if (!prop) {
		return NULL;
	}

	chr = prop->chr;

	// setup.c gives these two bodies their extras; a runtime spawn has to as well
	if (bodynum == BODY_DRCAROLL) {
		chr->drcarollimage_left = 0;
		chr->drcarollimage_right = 0;
		chr->height = 185;
		chr->radius = 30;
	} else if (bodynum == BODY_CHICROB) {
		chr->unk348[0] = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
		chr->unk348[1] = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
		chr->unk348[0]->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
		chr->unk348[1]->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
		chr->unk348[0]->beam->age = -1;
		chr->unk348[1]->beam->age = -1;
		chr->height = 200;
		chr->radius = 42;
	}

	chr->chrflags |= CHRCFLAG_NEVERSLEEP;
	chr->accuracyrating = 50;
	chr->speedrating = 50;

	if (ally) {
		chr->flags |= CHRFLAG0_SKIPSAFETYCHECKS;
		chr->flags2 |= CHRFLAG1_IGNORECOVER | CHRFLAG1_AIVSAI_ADVANTAGED;
		chr->team = TEAM_ALLY;
		chr->squadron = SQUADRON_01;
		chr->hidden |= CHRHFLAG_DETECTED;
		chr->teamscandist = 50;
		chr->accuracyrating = 100;
		chr->speedrating = 100;
		chrAddHealth(chr, 20);
	} else {
		chr->team = TEAM_ENEMY;
		chr->squadron = SQUADRON_0F;
		chr->visionrange = 200;
		chr->hearingscale = 1.0f;
	}

	return prop;
}

struct prop *editspawnSpawn(s32 kind, s32 index, struct editspawnaim *aim)
{
	const struct editspawnentry *entry = editspawnGetEntry(kind, index);
	char name[64];
	struct prop *prop = NULL;

	if (!entry) {
		editspawnSay("nothing to spawn", NULL);
		return NULL;
	}

	snprintf(name, sizeof(name), "%s", editspawnCatalogName(kind, index));

	if (g_NetMode != NETMODE_NONE) {
		editspawnSay("spawning is off in netplay (%s)", name);
		return NULL;
	}

	if (s_NumTracked >= EDITSPAWN_RESERVE) {
		snprintf(s_Message, sizeof(s_Message),
				"spawn budget full (%d): undo, clear or reload", EDITSPAWN_RESERVE);
		sysLogPrintf(LOG_NOTE, "editspawn: %s", s_Message);
		return NULL;
	}

	if (!aim || !aim->valid) {
		editspawnSay("no anchor for %s", name);
		return NULL;
	}

	s_NoHeadroom = false;

	switch (kind) {
	case EDITSPAWN_CHR:
	case EDITSPAWN_ALLY:
		prop = editspawnChr(entry->id, kind == EDITSPAWN_ALLY, aim);
		break;
	case EDITSPAWN_WEAPON:
		prop = editspawnWeapon(entry->id, entry->model, aim);
		break;
	case EDITSPAWN_GADGET:
		prop = entry->id < 0 ? editspawnShield(aim) : editspawnWeapon(entry->id, entry->model, aim);
		break;
	case EDITSPAWN_AMMO:
		prop = editspawnAmmo(entry->id, aim);
		break;
	case EDITSPAWN_PROP:
		prop = editspawnProp(entry->model, false, aim);
		break;
	case EDITSPAWN_GLASS:
		prop = editspawnProp(entry->model, true, aim);
		break;
	}

	if (prop) {
		editspawnTrack(prop);
		editspawnSay(s_NoHeadroom ? "spawned %s (live only) - no headroom, it may clip"
				: "spawned %s (live only)", name);
	} else {
		editspawnSay("could not spawn %s there", name);
	}

	return prop;
}
