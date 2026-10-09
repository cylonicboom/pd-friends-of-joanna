#ifndef _IN_SEATPROFILE_H
#define _IN_SEATPROFILE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fileguid;

/*
 * Per-player options that belong to the player's save file, not to the seat.
 *
 * The Game options (g_PlayerExtCfg) and the key bindings live in pd.ini under
 * the MP player file that sits in the seat:
 *
 *   [MpPlayer.<serial>-<fileid>.Game]    FovY=..., CrouchMode=..., ...
 *   [MpPlayer.<serial>-<fileid>.Binds]   ZTRIG=..., ...
 *
 * A seat with no file uses the engine's stand-in for that seat,
 * [MpPlayer.Player<n>.*], the same identity iniBindProfileProperties hands a
 * fileless player (deviceserial 0xFFFF). These replace the old seat-keyed
 * [Game.Player<n>] and [Input.Player<n>.Binds], which migrate into the
 * stand-ins once.
 *
 * The keys are bound straight onto the seat's live storage, so nothing that
 * reads g_PlayerExtCfg or the binds changes. Moving a file out of a seat
 * detaches its keys and keeps their values for the next save; moving one in
 * binds its keys over the defaults.
 *
 * Controller tuning (Input.Player<n>.*: deadzones, stick scale, rumble,
 * ControllerIndex) stays with the seat - it is about the pad, not the person.
 */

// Seat the file's options: defaults first, then whatever the file has saved.
// A NULL or zero guid, or a 0xFFFF one, seats the stand-in. For loading a file
// into a seat.
void seatProfileBind(s32 seat, const struct fileguid *guid);

// Seat the file's options keeping what the seat is playing with now. For a
// seat being saved to a new file: the person did not change, so their
// settings go with them, over anything the file had.
void seatProfileAdopt(s32 seat, const struct fileguid *guid);

// The section a seat's options are under now ("MpPlayer.1aba-10"), or "".
const char *seatProfileSlug(s32 seat);

// Once, after configInit and before inputInit: move [Game.Player<n>] and
// [Input.Player<n>.Binds] into the stand-ins.
void seatProfileMigrateLegacy(void);

#ifdef __cplusplus
}
#endif

#endif
