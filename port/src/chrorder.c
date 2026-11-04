/**
 * Orders: a short queue of steps given to one chr from outside its ailist.
 * See port/include/chrorder.h for the shape; this is the bookkeeping.
 *
 * A chr is keyed by chrnum, not by pointer: chr slots are reused across a
 * stage restart, and a chrnum survives a reallocation of the chr slots. The
 * whole table is dropped when the stage changes or restarts (lvframenum
 * going backwards).
 *
 * Off its ailist means the chr tick (chraction.c) calls chrOrderAiTick in
 * place of chraiExecute, so orders advance at exactly the moments the
 * ailist would have run and never while the game is paused. The chr
 * still runs its action tick, so a goto it was given still walks, doors
 * still open for it and it still dies when shot. It does not react to
 * anything: that is the ailist's job, and the ailist is what was taken away.
 */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "game/chraction.h"
#include "game/pad.h"
#include "system.h"
#include "chrorder.h"

// a goto that cannot start (chr busy, in a fight the game won't interrupt)
// is retried on each of the chr's ai ticks this many times, then dropped
// with a log line
#define CHRORDER_START_TICKS 180

// the two early ends of a walk, in game time (lvframe60); see chrOrderAiTick
#define CHRORDER_MOVED        4.0f      // units that count as having moved
#define CHRORDER_STUCK_60     (60 * 30) // not moved at all: stuck
#define CHRORDER_NEAR         150.0f    // within this of the goal...
#define CHRORDER_NEAR_GAIN    8.0f      // ...and not this much closer...
#define CHRORDER_NEAR_60      (60 * 5)  // ...for this long: arrived

struct chrorders {
	s32 chrnum;       // -1 = free slot
	u8 n;             // steps queued
	u8 started;       // q[0] is a goto that has begun
	u8 holding;       // a wait was reached: stay off the ailist when drained
	u16 tries;        // ai ticks q[0] has failed to start
	s32 moved60;      // lvframe60 when the chr last moved
	s32 gain60;       // lvframe60 when it last got closer to its goal
	f32 best;         // closest it has got to its goal, xz, this step
	struct coord lastpos; // where it was when it last moved
	struct chrorderstep q[CHRORDER_QUEUE];
};

static struct chrorders s_Orders[CHRORDER_MAX_CHRS];
static s32 s_OrdersInit = 0;
static s32 s_OrdersStage = -1;
static s32 s_OrdersFrame = 0;
static s32 s_OrdersLive = 0; // slots in use, so the guard is free when nobody has orders

static void chrOrderClearAll(void)
{
	for (s32 i = 0; i < CHRORDER_MAX_CHRS; i++) {
		s_Orders[i].chrnum = -1;
		s_Orders[i].n = 0;
		s_Orders[i].started = 0;
		s_Orders[i].holding = 0;
		s_Orders[i].tries = 0;
	}
	s_OrdersLive = 0;
	s_OrdersInit = 1;
}

// a stage change or restart drops everything: chrnums mean something else now
static void chrOrderCheckStage(void)
{
	if (!s_OrdersInit || s_OrdersStage != g_StageNum || g_Vars.lvframenum < s_OrdersFrame) {
		chrOrderClearAll();
		s_OrdersStage = g_StageNum;
	}
	s_OrdersFrame = g_Vars.lvframenum;
}

static struct chrorders *chrOrderFind(s32 chrnum)
{
	if (!s_OrdersLive) {
		return NULL;
	}
	for (s32 i = 0; i < CHRORDER_MAX_CHRS; i++) {
		if (s_Orders[i].chrnum == chrnum) {
			return &s_Orders[i];
		}
	}
	return NULL;
}

static void chrOrderFree(struct chrorders *o)
{
	o->chrnum = -1;
	o->n = 0;
	o->started = 0;
	o->holding = 0;
	o->tries = 0;
	s_OrdersLive--;
}

static void chrOrderPop(struct chrorders *o)
{
	if (o->n) {
		memmove(&o->q[0], &o->q[1], sizeof(o->q[0]) * (o->n - 1));
		o->n--;
	}
	o->started = 0;
	o->tries = 0;
	o->moved60 = g_Vars.lvframe60;
	o->best = 1e30f;
	o->gain60 = g_Vars.lvframe60;
}

static s32 chrOrderIsDead(struct chrdata *chr)
{
	return !chr->prop || chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD;
}

s32 chrOrderPush(struct chrdata *chr, const struct chrorderstep *step, char *why, s32 whylen)
{
	chrOrderCheckStage();

	if (!chr || chrOrderIsDead(chr)) {
		snprintf(why, whylen, "chr is dead or gone");
		return 0;
	}

	struct chrorders *o = chrOrderFind(chr->chrnum);
	if (!o) {
		for (s32 i = 0; i < CHRORDER_MAX_CHRS; i++) {
			if (s_Orders[i].chrnum < 0) {
				o = &s_Orders[i];
				o->chrnum = chr->chrnum;
				o->n = 0;
				o->started = 0;
				o->holding = 0;
				o->tries = 0;
				o->moved60 = g_Vars.lvframe60;
				o->best = 1e30f;
				o->gain60 = g_Vars.lvframe60;
				o->lastpos = chr->prop->pos;
				s_OrdersLive++;
				break;
			}
		}
	}
	if (!o) {
		snprintf(why, whylen, "%d chrs already have orders", CHRORDER_MAX_CHRS);
		return 0;
	}
	if (o->n >= CHRORDER_QUEUE) {
		snprintf(why, whylen, "queue full (%d steps)", CHRORDER_QUEUE);
		return 0;
	}

	o->q[o->n++] = *step;
	snprintf(why, whylen, "%d step%s queued", o->n, o->n == 1 ? "" : "s");
	return 1;
}

s32 chrOrderRelease(struct chrdata *chr)
{
	chrOrderCheckStage();
	struct chrorders *o = chr ? chrOrderFind(chr->chrnum) : NULL;
	if (!o) {
		return 0;
	}
	chrOrderFree(o);
	return 1;
}

void chrOrderReleaseAll(void)
{
	chrOrderClearAll();
}

char chrOrderState(struct chrdata *chr)
{
	struct chrorders *o = chr ? chrOrderFind(chr->chrnum) : NULL;
	if (!o) {
		return 0;
	}
	return o->n ? 'o' : (o->holding ? 'w' : 0);
}

s32 chrOrderCount(struct chrdata *chr)
{
	struct chrorders *o = chr ? chrOrderFind(chr->chrnum) : NULL;
	return o ? o->n : 0;
}

static struct chrdata *chrOrderChr(s32 chrnum)
{
	if (chrsGetNumSlots() <= 0) {
		return NULL;
	}
	for (s32 i = 0; i < chrsGetNumSlots(); i++) {
		if (chrslotGet(i)->chrnum == chrnum) {
			return chrslotGet(i);
		}
	}
	return NULL;
}

// where q[0] is headed, for the stall check; 0 when it has no place
static s32 chrOrderGoal(struct chrorders *o, struct coord *out)
{
	const struct chrorderstep *s = &o->q[0];
	struct chrdata *target;
	struct pad pad;

	switch (s->kind) {
	case CHRORDER_GOTO_PAD:
		padUnpack(s->pad, PADFIELD_POS, &pad);
		*out = pad.pos;
		return 1;
	case CHRORDER_GOTO_POS:
		*out = s->pos;
		return 1;
	case CHRORDER_GOTO_CHR:
		target = chrOrderChr(s->target);
		if (target && target->prop) {
			*out = target->prop->pos;
			return 1;
		}
		return 0;
	}
	return 0;
}

// begin q[0]; 1 when it is under way (or done at once), 0 to try again next frame
static s32 chrOrderStart(struct chrdata *chr, struct chrorders *o)
{
	struct chrorderstep *s = &o->q[0];

	switch (s->kind) {
	case CHRORDER_GOTO_PAD:
		return chrGoToPad(chr, s->pad, GOPOSFLAG_RUN);
	case CHRORDER_GOTO_POS:
		return chrGoToPos(chr, &s->pos, GOPOSFLAG_RUN);
	case CHRORDER_GOTO_CHR: {
		// resolved now, not when queued: the target may have moved or died
		struct chrdata *target = chrOrderChr(s->target);
		if (!target || !target->prop || target == chr) {
			return 1; // nothing to walk to; the step is done
		}
		return chrGoToProp(chr, target->prop, GOPOSFLAG_RUN);
	}
	default:
		return 1;
	}
}

s32 chrOrderAiTick(struct chrdata *chr)
{
	if (!s_OrdersLive || !chr) {
		return 0;
	}
	chrOrderCheckStage();

	struct chrorders *o = chrOrderFind(chr->chrnum);
	if (!o) {
		return 0;
	}
	if (chrOrderIsDead(chr)) {
		chrOrderFree(o);
		return 0;
	}

	// a wait takes no time: it marks the end of the queue as a hold
	while (o->n && o->q[0].kind == CHRORDER_WAIT) {
		o->holding = 1;
		chrOrderPop(o);
	}

	if (o->n == 0) {
		if (o->holding) {
			return 1;
		}
		// drained with no wait: back to the ailist, this very tick
		chrOrderFree(o);
		return 0;
	}

	if (!o->started) {
		if (chrOrderStart(chr, o)) {
			o->started = 1;
			o->tries = 0;
			o->moved60 = g_Vars.lvframe60;
			o->best = 1e30f;
			o->gain60 = g_Vars.lvframe60;
			o->lastpos = chr->prop->pos;
		} else if (++o->tries > CHRORDER_START_TICKS) {
			sysLogPrintf(LOG_NOTE, "ORDER: chr %d could not start a step (kind %d); dropped",
					o->chrnum, o->q[0].kind);
			chrOrderPop(o);
		}
		return 1;
	}

	// under way until the chr stops walking: arrived, or stopped by
	// something it walked into (a locked door). Either way, next step.
	if (chr->actiontype != ACT_GOPOS) {
		chrOrderPop(o);
		return 1;
	}

	// Two ways a walk ends early. Near the goal and no longer getting closer:
	// vanilla GOPOS only ends within 30 units of its end point, and a pad on
	// a desk or behind a rail never gets there, so the chr circles it - call
	// that arrived. Anywhere, not moving at all for a long while: stuck. The
	// long while is long because a chr off every screen has its action
	// ticked by the host in batches seconds apart (prop.c background
	// propstates) while this runs every frame; waiting at a door or a lift
	// does not count as not moving.
	struct coord goal;
	const s32 hasgoal = chrOrderGoal(o, &goal);
	const f32 mx = chr->prop->pos.x - o->lastpos.x;
	const f32 mz = chr->prop->pos.z - o->lastpos.z;

	if (mx * mx + mz * mz > CHRORDER_MOVED * CHRORDER_MOVED || (chr->act_gopos.flags & GOPOSFLAG_WAITING)) {
		o->lastpos = chr->prop->pos;
		o->moved60 = g_Vars.lvframe60;
	} else if (g_Vars.lvframe60 - o->moved60 > CHRORDER_STUCK_60) {
		sysLogPrintf(LOG_NOTE, "ORDER: chr %d has not moved in %ds (kind %d); dropping the step",
				o->chrnum, CHRORDER_STUCK_60 / 60, o->q[0].kind);
		chrStop(chr);
		chrOrderPop(o);
		return 1;
	}

	if (hasgoal) {
		const f32 dx = goal.x - chr->prop->pos.x;
		const f32 dz = goal.z - chr->prop->pos.z;
		const f32 d = sqrtf(dx * dx + dz * dz);
		if (d < o->best - CHRORDER_NEAR_GAIN) {
			o->best = d;
			o->gain60 = g_Vars.lvframe60;
		} else if (d < CHRORDER_NEAR && g_Vars.lvframe60 - o->gain60 > CHRORDER_NEAR_60) {
			sysLogPrintf(LOG_NOTE, "ORDER: chr %d is %.0f from its goal and getting no closer; arrived", o->chrnum, d);
			chrStop(chr);
			chrOrderPop(o);
		}
	}
	return 1;
}
