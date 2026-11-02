#ifndef _IN_EDITSPAWN_H
#define _IN_EDITSPAWN_H

#include <PR/ultratypes.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Editor spawns, live only. See editspawn.c.
 */

// spawns one session may hold; the chr, prop and model tables are grown by
// this much at stage load (setup.c), because none of them can grow later
#define EDITSPAWN_RESERVE 256

enum editspawnkind {
	EDITSPAWN_CHR,
	EDITSPAWN_ALLY,
	EDITSPAWN_PROP,
	EDITSPAWN_GLASS,
	EDITSPAWN_WEAPON,
	EDITSPAWN_GADGET,
	EDITSPAWN_AMMO,
	EDITSPAWN_NUMKINDS
};

// where a spawn goes. Filled by editspawnAimCrosshair or editspawnAimAt and
// resolved per spawn, because how far off a wall it must sit depends on
// what is being spawned.
struct editspawnaim {
	bool valid;
	bool crosshair;         // pos is the end of the view ray, not an anchor
	struct coord pos;
	RoomNum rooms[8];
	f32 yaw;                // faces the player
	// crosshair only: the ray, for backing off what it hit
	struct coord eye;
	RoomNum eyerooms[8];
	struct coord dir;
	f32 hitdist;            // eye to the surface the ray stopped on
};

// the "perfect dark source crosshairs" option: a crosshair spawn is pushed
// out of every surface within its own size, not only backed off along the ray
extern s32 g_EditSpawnSourceCrosshair;

// 0 outside gameplay stages; what setup.c adds to the chr, prop and model counts
s32 editspawnReserve(s32 stagenum);
void editspawnStageInit(void);

s32 editspawnCatalogCount(s32 kind);
const char *editspawnCatalogName(s32 kind, s32 index);
s32 editspawnCatalogModel(s32 kind, s32 index); // pickup or prop model, -1 if none

bool editspawnAimCrosshair(struct editspawnaim *aim);
bool editspawnAimAt(struct editspawnaim *aim, const struct coord *pos, struct prop *anchorprop);

struct prop *editspawnSpawn(s32 kind, s32 index, struct editspawnaim *aim);

s32 editspawnCount(void);
bool editspawnUndo(void);
s32 editspawnClear(void);

// one line about what the last call did, for the bar and the log
const char *editspawnLastMessage(void);

#ifdef __cplusplus
}
#endif

#endif
