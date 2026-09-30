#ifndef _IN_GAME_FOJOFRIENDS_H
#define _IN_GAME_FOJOFRIENDS_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

#define NUM_FOJO_FRIENDS (FOJO_INDEX_WILLOW + 1)

// indexed by FOJO_INDEX_*, 0xrrggbbaa
extern u32 g_FojoFriendColours[NUM_FOJO_FRIENDS];

s32 fojoFriendForMpIndex(s32 mpindex);
s32 fojoFriendWardrobeBody(s32 friendnum, s32 bodynum);
void fojoFriendsInit(void);

#endif
