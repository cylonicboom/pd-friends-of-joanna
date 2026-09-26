#ifndef _IN_GAME_HOTJOIN_H
#define _IN_GAME_HOTJOIN_H
#include <ultra64.h>
#include "types.h"

/**
 * fojo hot join: players walk into and out of a running team mission.
 * The engine half is playermgrAddPlayer / playermgrRemoveLastPlayer; this
 * module wraps them with the profile load, the stats roll-up and the save.
 */

bool hotjoinAvailable(void);
s32 hotjoinFreeSlot(void);
s32 hotjoinLastSlot(void);

// the profile list, for a picker: refreshed from the pak file list each call
s32 hotjoinProfileCount(void);
bool hotjoinProfileInfo(s32 index, char *name, u32 namelen, struct fileguid *guid, s32 *boundslot);

// load the profile into the next free slot and seat the player. -1 on failure.
s32 hotjoinAddPlayer(const struct fileguid *guid);

// roll the last player's stats up as if they had aborted, save their profile,
// take them out of the stage and release the slot. -1 if nobody can leave.
s32 hotjoinDropLastPlayer(void);

const char *hotjoinLastMessage(void);

#endif
