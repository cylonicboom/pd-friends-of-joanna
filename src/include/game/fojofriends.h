#ifndef _IN_GAME_FOJOFRIENDS_H
#define _IN_GAME_FOJOFRIENDS_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

// A friend number is a Team Missions roster index (mod.h modOperativeAt).
s32 fojoFriendForMpIndex(s32 mpindex);
s32 fojoFriendWardrobeBody(s32 friendnum, s32 bodynum);
void fojoFriendsInit(void);
s32 fojoBuddyRollOperative(void);
s32 fojoBuddySeatOperative(s32 seat);
void fojoBuddySettleSeats(void);
bool fojoBuddyIdentity(s32 seat, s32 *bodynum, s32 *headnum);

#endif
