#ifndef _IN_SOLOPROGRESS_H
#define _IN_SOLOPROGRESS_H

#include "types.h"

/*
 * A mod mission's campaign progress - best time per difficulty and the coop
 * completion pip - kept in pd.ini beside the game file, because the game file
 * has three spare bits. See solo-sidecar-plan.md.
 *
 *   [Solo.<reality serial>-<fileid>.<mod dir>.<stagenum hex>]
 *   t0= t1= t2=    seconds, 0..0xfff, 0 = not done
 *   c0= c1= c2=    coop completed, 0/1
 *
 * Keyed per REALITY: a fresh game file starts clean, as the vanilla rows do.
 * Bound when a reality is selected (soloProgressBind), for every stage the
 * registry lists as solo or both; detached but kept when the reality changes.
 * The config store applies a section's parsed values at bind and writes
 * unbound sections back verbatim, so a reality or a mod absent this session
 * loses nothing.
 */
void soloProgressBind(void);
void soloProgressUnbind(void);

// -1 when the stage is not a mod mission bound this session.
s32 soloProgressBestTime(s32 stagenum, s32 difficulty);
bool soloProgressSetBestTime(s32 stagenum, s32 difficulty, s32 secs);
bool soloProgressCoopDone(s32 stagenum, s32 difficulty);
bool soloProgressSetCoopDone(s32 stagenum, s32 difficulty);

// --solo-probe SERIAL-FILEID: bind as if that reality were selected and log
// every section, then carry on booting so the normal shutdown saves pd.ini.
void soloProgressProbeFromArgs(void);

#endif
