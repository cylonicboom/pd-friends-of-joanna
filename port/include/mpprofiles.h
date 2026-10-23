#ifndef _IN_MPPROFILES_H
#define _IN_MPPROFILES_H

#include <PR/ultratypes.h>

/*
 * Profile operations for the fojOS profile manager (imgui_profiles.cpp),
 * implemented in src/game/mplayer/mplayer.c beside the wad code they depend
 * on. Every call takes a profile's fileguid and works whether or not the
 * profile is seated: a seated profile is edited live and saved through the
 * save queue (mpProfileMarkDirty), an unseated one is edited in its pak file
 * and its pd.ini section directly.
 */

struct fileguid;

#ifdef __cplusplus
extern "C" {
#endif

#define MPPROFILE_NAME_MAX 10 // the wad's name field is always 10 bytes

// the seat a profile is loaded in, or -1
s32 mpProfileSeatOf(const struct fileguid *guid);

// blank profile on a controller pak page with room (the pool grows a page if
// none has): the given name, default head and body, no stats. 0 on success,
// -1 pak error, -2 no page could take it
s32 mpProfileCreateBlank(const char *name, struct fileguid *out);

s32 mpProfileRename(const struct fileguid *guid, const char *name);

// refused (-1) while the profile is seated
s32 mpProfileDelete(const struct fileguid *guid);

s32 mpProfileGetHeadBody(const struct fileguid *guid, s32 *head, s32 *body);
s32 mpProfileSetHeadBody(const struct fileguid *guid, s32 head, s32 body);

// Friends of Joanna: the operative (TeamAgentIndex), an index into the FoJo
// carousel's options (mainmenu.c g_FojoHeadOptions); -1 when never chosen
s32 mpProfileGetOperative(const struct fileguid *guid);
s32 mpProfileSetOperative(const struct fileguid *guid, s32 index);

// Move an unseated profile to the pak whose PD serial is `dstserial`
// (pakGetSerial(SAVEDEVICE_GAMEPAK) for the game pak): copy, verify, then
// remove the original, journalled so a crash is finished at the next boot.
// Its pd.ini sections go with it. `out` gets its new guid.
#define MPPROFILE_MOVE_OK        0
#define MPPROFILE_MOVE_BADARGS  -1
#define MPPROFILE_MOVE_SEATED   -2 // unseat it first
#define MPPROFILE_MOVE_PAKERROR -3 // nothing moved
#define MPPROFILE_MOVE_FULL     -4 // no room on the destination
#define MPPROFILE_MOVE_READONLY -5 // a --player-ro pak on either side
#define MPPROFILE_MOVE_UNSAVED  -6 // edits waiting in the save queue
s32 mpProfileMove(const struct fileguid *guid, s32 dstserial, struct fileguid *out);

// finishes a move a crash interrupted; every frame, from the save queue
void mpProfileMoveTick(void);

// Seats. A seat holds one profile or its engine default (deviceserial
// 0xFFFF). Seats only change between missions - in the institute, the title
// or the menus stage - because the end of a match or mission saves whoever is
// seated; mid-mission joins are hot join's.
#define MPPROFILE_SEAT_OK         0
#define MPPROFILE_SEAT_BADARGS   -1
#define MPPROFILE_SEAT_PAKERROR  -2 // nothing moved
#define MPPROFILE_SEAT_ELSEWHERE -3 // the profile is in another seat
#define MPPROFILE_SEAT_LOCKED    -4 // a mission or match is running

// nonzero while seats cannot change
s32 mpProfileSeatsLocked(void);

// Put the profile in the seat. Whoever was there is saved and released
// first; if the profile then fails to load they are put back.
s32 mpProfileSeat(const struct fileguid *guid, s32 seat);

// Save the seat's profile and give the seat back its default. Refused, with
// the profile still seated, if the save fails.
s32 mpProfileUnseat(s32 seat);

#ifdef __cplusplus
}
#endif

#endif
