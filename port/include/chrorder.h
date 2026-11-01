#ifndef _IN_CHRORDER_H
#define _IN_CHRORDER_H

#include <ultra64.h>
#include "types.h"

/**
 * Orders (port only): a short queue of steps given to one chr from outside
 * its ailist - the map in fojo-tui, the console's `order` verb.
 *
 * A chr with orders is off its ailist: the chr tick runs chrOrderAiTick
 * in place of chraiExecute while it has a queue or is holding. Steps run in order. When the queue runs dry the
 * chr goes back to its ailist, unless a `wait` was queued, in which case it
 * holds where it ended up until `release`.
 */

#define CHRORDER_MAX_CHRS 32
#define CHRORDER_QUEUE    8

#define CHRORDER_GOTO_PAD  1
#define CHRORDER_GOTO_POS  2
#define CHRORDER_GOTO_CHR  3
#define CHRORDER_WAIT      4

struct chrorderstep {
	u8 kind;
	s32 pad;            // CHRORDER_GOTO_PAD
	struct coord pos;   // CHRORDER_GOTO_POS
	s32 target;         // CHRORDER_GOTO_CHR: a chrnum, resolved when the step starts
};

// 1 queued, 0 refused (queue full, no free slot, dead chr); why says which
s32 chrOrderPush(struct chrdata *chr, const struct chrorderstep *step, char *why, s32 whylen);
// drop the queue and any hold; the ailist resumes next frame. 1 if it had orders
s32 chrOrderRelease(struct chrdata *chr);
void chrOrderReleaseAll(void);
// called by the chr tick where it would run the ailist: advances the
// chr's orders and returns 1 when the ailist must not run this tick
s32 chrOrderAiTick(struct chrdata *chr);
// a short state for `state`: 0 none, 'o' running orders, 'w' holding
char chrOrderState(struct chrdata *chr);
// queue length, for `order <chr>` with no step
s32 chrOrderCount(struct chrdata *chr);

#endif
