/**
 * Determinism harness — see port/include/det.h.
 *
 * Ported from Perfect Dark Kai (cylonicboom/perfect-dark @ kai be46717,
 * port/src/det.c). The hash, the pin and the record/replay file format are
 * Kai's; the per-frame hooks were defined there but never called from the loop
 * (measured 2026-10-20: zero call sites outside det.c), so the wiring in
 * pdmain.c and the boot flags are fojo's. Kai's Game.FixedTick decouple, the
 * netplay step pin, the slow-motion halving and the `/` console commands are
 * left out: nothing here needs them yet, and netplay brings its own pin.
 *
 * Field-selection rule for the hash: fold a field iff it is read-modified by
 * the per-tick sim AND is not a pointer, render scratch, audio handle, or
 * wall-clock value. Walk entities in a deterministic order (by index / list
 * order), never by memory address. Start with a minimal high-value field set
 * (positions / angles / health / RNG / counts) that catches gross desync;
 * expand later, re-checking a record->replay self-match after each expansion,
 * and bump DET_FIELDSET_VER when the set changes.
 */
#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include "constants.h"
#include "types.h"
#include "bss.h"   // g_Vars
#include "data.h"  // g_ChrSlots, g_NumChrSlots
#include "system.h"
#include "platform.h"
#include "lib/joy.h"
#include "det.h"
#include "net/net.h"
#include "game/lv.h"

s32 g_DetMode = DET_OFF;

extern u64 g_RngSeed;   // src/lib/rng_c.c
extern u64 g_Rng2Seed;  // src/game/rng2_c.c

/* ---- FNV-1a 64-bit fold helpers ---- */

#define DET_FNV_OFFSET 0xcbf29ce484222325ULL
#define DET_FNV_PRIME  0x00000100000001b3ULL

static void detFoldBytes(u64 *h, const void *p, u32 n)
{
	const u8 *b = (const u8 *)p;
	u64 x = *h;
	u32 i;
	for (i = 0; i < n; i++) {
		x ^= (u64)b[i];
		x *= DET_FNV_PRIME;
	}
	*h = x;
}

static void detFoldU8(u64 *h, u8 v)   { detFoldBytes(h, &v, sizeof(v)); }
static void detFoldU16(u64 *h, u16 v) { detFoldBytes(h, &v, sizeof(v)); }
static void detFoldU32(u64 *h, u32 v) { detFoldBytes(h, &v, sizeof(v)); }
static void detFoldU64(u64 *h, u64 v) { detFoldBytes(h, &v, sizeof(v)); }
static void detFoldS32(u64 *h, s32 v) { detFoldU32(h, (u32)v); }

static void detFoldF32(u64 *h, f32 v)
{
	union { f32 f; u32 i; } u;
	if (v == 0.0f) {
		u.f = 0.0f;          // normalize -0.0 -> +0.0
	} else if (v != v) {
		u.i = 0x7FC00000;    // canonical NaN sentinel
	} else {
		u.f = v;
	}
	detFoldU32(h, u.i);
}

static void detFoldCoord(u64 *h, const struct coord *c)
{
	detFoldF32(h, c->x);
	detFoldF32(h, c->y);
	detFoldF32(h, c->z);
}

/* ---- per-entity hashing (minimal high-value field sets) ---- */

static void detHashPlayer(u64 *h, const struct player *pl)
{
	// View angles + movement speeds + ground/fall + crouch + health. The
	// player's world position is folded via the prop pass (pl->prop).
	detFoldF32(h, pl->vv_theta);
	detFoldF32(h, pl->vv_verta);
	detFoldF32(h, pl->speedsideways);
	detFoldF32(h, pl->speedstrafe);
	detFoldF32(h, pl->speedforwards);
	detFoldF32(h, pl->speedboost);
	detFoldS32(h, pl->speedmaxtime60);
	detFoldF32(h, pl->sumground);
	detFoldF32(h, pl->vv_manground);
	detFoldF32(h, pl->vv_ground);
	detFoldCoord(h, &pl->bdeltapos);
	detFoldS32(h, pl->isfalling);
	detFoldS32(h, pl->fallstart);
	detFoldS32(h, pl->crouchpos);
	detFoldS32(h, pl->autocrouchpos);
	detFoldF32(h, pl->crouchoffset);
	detFoldS32(h, pl->isdead);
	detFoldF32(h, pl->bondhealth);
	detFoldF32(h, pl->oldhealth);
	detFoldF32(h, pl->oldarmour);
	detFoldF32(h, pl->apparenthealth);
	detFoldF32(h, pl->apparentarmour);
}

static void detHashProp(u64 *h, const struct prop *p)
{
	s32 i;
	detFoldU8(h, p->type);
	detFoldU8(h, p->flags);
	detFoldU16(h, (u16)p->timetoregen);
	detFoldCoord(h, &p->pos);
	detFoldF32(h, p->z);
	for (i = 0; i < 8; i++) {
		detFoldS32(h, (s32)p->rooms[i]);
	}
	detFoldU8(h, (u8)p->active);
	// SKIP: union ptr, parent/child/next/prev, wallhits, and the
	// backgrounding/update-slot scratch (render/onscreen-dependent).
}

static void detHashChr(u64 *h, const struct chrdata *chr)
{
	detFoldU16(h, (u16)chr->chrnum);
	detFoldU8(h, (u8)chr->actiontype);
	detFoldU32(h, chr->flags);
	detFoldU32(h, chr->flags2);
	detFoldU32(h, chr->chrflags);
	detFoldU32(h, chr->hidden);
	detFoldF32(h, chr->damage);
	detFoldF32(h, chr->maxdamage);
	detFoldF32(h, chr->cshield);
	detFoldS32(h, chr->timer60);
	detFoldCoord(h, &chr->prevpos);
	detFoldCoord(h, &chr->fallspeed);
	detFoldF32(h, chr->ground);
	detFoldF32(h, chr->manground);
	detFoldF32(h, chr->sumground);
	detFoldS32(h, (s32)chr->target);
	detFoldU8(h, chr->morale);
	detFoldU8(h, chr->alertness);
	detFoldU8(h, chr->random);
	detFoldF32(h, chr->oldframe);
	detFoldF32(h, chr->magicframe);
	detFoldF32(h, chr->magicspeed);
	detFoldS32(h, (s32)chr->magicanim);
	detFoldU16(h, chr->aioffset);
	detFoldS32(h, (s32)chr->aireturnlist);
	detFoldS32(h, (s32)chr->aishotlist);
	// SKIP: the act_* union (the `actiontype` discriminator above catches
	// state-machine divergence; fold the union only after a per-variant
	// pointer audit), all pointers/model/weapons, visual cm*/shadecol fields.
}

void detComputeHash(struct dethash *out)
{
	out->rng = DET_FNV_OFFSET;
	out->players = DET_FNV_OFFSET;
	out->props = DET_FNV_OFFSET;
	out->chrs = DET_FNV_OFFSET;
	out->all = DET_FNV_OFFSET;

	// rng: both gameplay streams + the step-sensitive frame counters (these
	// advance deterministically once the step is pinned, so a mismatch flags
	// a step desync immediately).
	detFoldU64(&out->rng, g_RngSeed);
	detFoldU64(&out->rng, g_Rng2Seed);
	detFoldS32(&out->rng, g_Vars.lvframenum);
	detFoldS32(&out->rng, g_Vars.lvframe60);
	detFoldS32(&out->rng, g_Vars.lvframe240);
	detFoldS32(&out->rng, g_Vars.lvupdate240rem);

	// players: by index, skipping empty slots.
	{
		s32 i;
		for (i = 0; i < MAX_PLAYERS; i++) {
			const struct player *pl = g_Vars.players[i];
			if (pl == NULL) {
				continue;
			}
			detFoldS32(&out->players, i);
			detHashPlayer(&out->players, pl);
		}
	}

	// props: walk the active list head->tail in list order; fold the count
	// last so a count mismatch is caught even when every field matched.
	{
		struct prop *p = g_Vars.activeprops;
		s32 count = 0;
		const s32 maxprops = g_Vars.maxprops;
		while (p != NULL) {
			detHashProp(&out->props, p);
			count++;
			if (p == g_Vars.activepropstail) {
				break;
			}
			p = p->next;
			if (count > maxprops) {
				break; // safety against a corrupt/cyclic list
			}
		}
		detFoldS32(&out->props, count);
	}

	// chrs: by slot index over g_ChrSlots, the real chr array (chrmgr.c). Kai
	// walked g_Vars.chrdata[0..g_NumChrs) — but g_Vars.chrdata is the CURRENT
	// chr pointer, not the array, so that read stale heap past one chr
	// (measured 2026-10-20: a "chr2" of pointer-shaped garbage diverging every
	// run). Free slots (chrnum -1 / no prop) hold whatever the stage pool had
	// and are skipped; the slot index is folded so a chr moving slots shows.
	{
		s32 i;
		s32 live = 0;
		for (i = 0; i < g_NumChrSlots; i++) {
			const struct chrdata *chr = &g_ChrSlots[i];
			if (chr->chrnum == -1 || chr->prop == NULL) {
				continue;
			}
			detFoldS32(&out->chrs, i);
			detHashChr(&out->chrs, chr);
			live++;
		}
		detFoldS32(&out->chrs, live);
	}

	detFoldU64(&out->all, out->rng);
	detFoldU64(&out->all, out->players);
	detFoldU64(&out->all, out->props);
	detFoldU64(&out->all, out->chrs);
}

/* ---- fixed-step pin ---- */

void detPinTimestep(void)
{
	if (!detTickPinActive()) {
		return;
	}
	// Respect pause: when the engine chose a zero step (paused / cutscene
	// gate), leave it zero so a paused frame stays a no-op in both record and
	// replay.
	if (g_Vars.lvupdate240 <= 0) {
		return;
	}
	// Called BEFORE lv.c derives lvupdate60 / lvupdate60f / lvupdate60freal
	// and advances the lvframe* counters, so the derivation produces fully
	// deterministic values from these two assignments and the 200+ downstream
	// `* lvupdate60f` sites inherit the step untouched. (4 -> lvupdate60=1,
	// lvupdate60f=1.0.) This also overrides any slow-motion cap lvTick
	// applied above it: a recording is a fixed 1/60 by definition.
	if (g_DetMode == DET_RECORD || g_DetMode == DET_REPLAY) {
		g_Vars.lvupdate240 = 4;
		g_Vars.lvupdate240rem = 0;
		return;
	}

	// netplay (kai): the sim advances in whole 1/60 steps at real-time rate on
	// both machines, however fast either renders. lvTick runs once per render
	// frame, so a slow frame emits several steps' worth at once (one combined
	// dt - the engine is built for variable dt) and a fast one emits none
	// (lvupdate240 = 0, a no-advance frame like a paused one). Anti-spiral
	// capped at 6 steps: a stage-load hitch on a slow renderer otherwise
	// handed the first frame seconds of sim time, and modelTickAnim walked
	// every anim frame of every chr in that one frame (the client "stall").
	{
		static s32 accum240 = 0;
		s32 step = 4;
		s32 nsteps;

		accum240 += g_Vars.diffframe240;
		if (accum240 < 0) {
			accum240 = 0;
		}
		nsteps = accum240 / step;
		accum240 -= nsteps * step;
		if (nsteps > 6) {
			nsteps = 6;
			accum240 = 0; // drop the backlog rather than chase it
		}
		step *= nsteps;

		// slow motion / combat boost: halve the pinned step, remainder-carried.
		// the host decides (g_LvSlomoEngaged from lvTick); clients mirror it
		// via SVC_TIMESCALE, so both ends advance the same sim time per tick.
		if (g_LvSlomoEngaged) {
			static s32 slomorem = 0;
			const s32 total = step + slomorem;
			slomorem = total & 1;
			step = total >> 1;
		}

		g_Vars.lvupdate240 = step;
		// zero the remainder only for a 4-aligned step (the canonical 1/60:
		// lvupdate240 4 -> lvupdate60 1). a halved step of 2 must accumulate
		// so lvupdate60 alternates 0/1 instead of sitting at 0 forever.
		if (step > 0 && (step & 3) == 0) {
			g_Vars.lvupdate240rem = 0;
		}
	}
}

/* ---- record / replay ---- */

// Bump DET_FIELDSET_VER whenever the set of fields folded into the hash
// changes, so a replay against a recording made with a different field set is
// rejected (its hashes would otherwise spuriously "diverge").
#define DET_MAGIC        0x31544544u // "DET1"
#define DET_VERSION      2u // 2: boot seed in the header (fojo)
#define DET_FIELDSET_VER 1u

#define DET_MAX_RING 20
#define DET_MAX_PADS 4

struct detfileheader {
	u32 magic;
	u32 version;
	u32 ringsize;
	u32 padcount;
	u32 oscontpadsize;
	u32 fieldsetver;
	u32 bootseed; // the rngSetSeed argument at boot (pdmain.c); replay reuses it
};

// One frame's reproducible input slice: the whole ring + its geometry. The
// entire ring is snapshotted (not just the cur window) so replay is exact
// regardless of the intra-frame cur-window mapping — record and replay are
// the same build, so this is bit-for-bit.
struct detframeinputs {
	s32 lvframenum;
	s32 curstart;
	s32 curlast;
	s32 ringsize;
	s32 padcount;
	OSContPad pads[DET_MAX_RING * DET_MAX_PADS];
};

static FILE *g_DetFile = NULL;
static char g_DetPath[1024];
static s32 g_DetExitOnEnd = 0;
static u32 g_DetBootSeed = 0;
static s32 g_DetDumpFrames = 0; // --det-dump <n>: print per-entity state for the first n hashed frames
static s32 g_DetHaveBootSeed = 0;
static struct detframeinputs g_DetIn;
static struct dethash g_DetExpected;
static s32 g_DetReplayDone = 0;
static s32 g_DetFrameCount = 0;
static s32 g_DetMismatchCount = 0;
static s32 g_DetFirstDivergeFrame = -1;
static const char *g_DetFirstDivergeSub = "";

static void detClose(void)
{
	if (g_DetFile) {
		fclose(g_DetFile);
		g_DetFile = NULL;
	}
}

static void detResetState(void)
{
	g_DetReplayDone = 0;
	g_DetFrameCount = 0;
	g_DetMismatchCount = 0;
	g_DetFirstDivergeFrame = -1;
	g_DetFirstDivergeSub = "";
}

static void detCaptureInputs(struct detframeinputs *in)
{
	s32 r, p;
	in->lvframenum = g_Vars.lvframenum;
	in->curstart = joyGetCurStart();
	in->curlast = joyGetCurLast();
	in->ringsize = joyGetRingSize();
	in->padcount = joyGetPadCount();
	if (in->ringsize > DET_MAX_RING) in->ringsize = DET_MAX_RING;
	if (in->padcount > DET_MAX_PADS) in->padcount = DET_MAX_PADS;
	for (r = 0; r < in->ringsize; r++) {
		for (p = 0; p < in->padcount; p++) {
			joyGetRawSample(r, p, &in->pads[r * in->padcount + p]);
		}
	}
}

static void detInjectInputs(const struct detframeinputs *in)
{
	s32 r, p;
	for (r = 0; r < in->ringsize; r++) {
		for (p = 0; p < in->padcount; p++) {
			joySetRawSample(r, p, &in->pads[r * in->padcount + p]);
		}
	}
	joySetCurWindow(in->curstart, in->curlast);
}

static s32 detWriteHeader(void)
{
	struct detfileheader hdr;
	hdr.magic = DET_MAGIC;
	hdr.version = DET_VERSION;
	hdr.ringsize = (u32)joyGetRingSize();
	hdr.padcount = (u32)joyGetPadCount();
	hdr.oscontpadsize = (u32)sizeof(OSContPad);
	hdr.fieldsetver = DET_FIELDSET_VER;
	hdr.bootseed = 0; // patched in place by detBootSeed once the seed exists
	return fwrite(&hdr, sizeof(hdr), 1, g_DetFile) == 1;
}

// pdmain.c seeds the gameplay RNG from osGetCount() at boot, which is the
// first thing that makes two runs differ (measured: frame 0, sub=RNG). While
// recording, remember the seed in the header; while replaying, hand back the
// recorded one instead.
u32 detBootSeed(u32 seed)
{
	if (g_DetMode == DET_RECORD && g_DetFile) {
		const long pos = ftell(g_DetFile);
		g_DetBootSeed = seed;
		g_DetHaveBootSeed = 1;
		fseek(g_DetFile, (long)offsetof(struct detfileheader, bootseed), SEEK_SET);
		fwrite(&g_DetBootSeed, sizeof(u32), 1, g_DetFile);
		fseek(g_DetFile, pos, SEEK_SET);
		fflush(g_DetFile);
		return seed;
	}
	if (g_DetMode == DET_REPLAY && g_DetHaveBootSeed) {
		sysLogPrintf(LOG_NOTE, "DET: boot seed %08x from the recording (live was %08x)", g_DetBootSeed, seed);
		return g_DetBootSeed;
	}
	return seed;
}

static s32 detCheckHeader(void)
{
	struct detfileheader hdr;
	if (fread(&hdr, sizeof(hdr), 1, g_DetFile) != 1) {
		return 0;
	}
	if (hdr.magic != DET_MAGIC || hdr.version != DET_VERSION
			|| hdr.oscontpadsize != (u32)sizeof(OSContPad)
			|| hdr.fieldsetver != DET_FIELDSET_VER) {
		sysLogPrintf(LOG_ERROR, "DET: recording incompatible (magic/version/fieldset mismatch)");
		return 0;
	}
	g_DetBootSeed = hdr.bootseed;
	g_DetHaveBootSeed = 1;
	return 1;
}

static void detWriteFrame(const struct detframeinputs *in, const struct dethash *h)
{
	const s32 n = in->ringsize * in->padcount;
	fwrite(&in->lvframenum, sizeof(s32), 1, g_DetFile);
	fwrite(&in->curstart, sizeof(s32), 1, g_DetFile);
	fwrite(&in->curlast, sizeof(s32), 1, g_DetFile);
	fwrite(&in->ringsize, sizeof(s32), 1, g_DetFile);
	fwrite(&in->padcount, sizeof(s32), 1, g_DetFile);
	fwrite(in->pads, sizeof(OSContPad), n, g_DetFile);
	fwrite(h, sizeof(struct dethash), 1, g_DetFile);
	fflush(g_DetFile);
}

// Returns 1 on a full frame read, 0 on EOF / short read.
static s32 detReadFrame(struct detframeinputs *in, struct dethash *expected)
{
	s32 n;
	if (fread(&in->lvframenum, sizeof(s32), 1, g_DetFile) != 1) return 0;
	if (fread(&in->curstart, sizeof(s32), 1, g_DetFile) != 1) return 0;
	if (fread(&in->curlast, sizeof(s32), 1, g_DetFile) != 1) return 0;
	if (fread(&in->ringsize, sizeof(s32), 1, g_DetFile) != 1) return 0;
	if (fread(&in->padcount, sizeof(s32), 1, g_DetFile) != 1) return 0;
	if (in->ringsize < 0 || in->ringsize > DET_MAX_RING
			|| in->padcount < 0 || in->padcount > DET_MAX_PADS) {
		return 0;
	}
	n = in->ringsize * in->padcount;
	if ((s32)fread(in->pads, sizeof(OSContPad), n, g_DetFile) != n) return 0;
	if (fread(expected, sizeof(struct dethash), 1, g_DetFile) != 1) return 0;
	return 1;
}

static void detCompare(const struct dethash *got, const struct dethash *exp, s32 frame)
{
	const char *sub;
	if (got->all == exp->all) {
		return;
	}
	g_DetMismatchCount++;
	if (g_DetFirstDivergeFrame >= 0) {
		return; // already reported the first divergence; state has forked
	}
	sub = got->rng != exp->rng ? "RNG" :
			got->players != exp->players ? "PLAYERS" :
			got->props != exp->props ? "PROPS" :
			got->chrs != exp->chrs ? "CHRS" : "ALL";
	g_DetFirstDivergeFrame = frame;
	g_DetFirstDivergeSub = sub;
	sysLogPrintf(LOG_WARNING, "DET: DIVERGE frame=%d sub=%s exp=%016llx got=%016llx",
			frame, sub, (unsigned long long)exp->all, (unsigned long long)got->all);
}

static void detReplayEnded(void)
{
	const s32 ok = g_DetMismatchCount == 0;
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING, "DET: replay end, %d frames, %d mismatch%s%s",
			g_DetFrameCount, g_DetMismatchCount,
			g_DetMismatchCount == 1 ? "" : "es",
			ok ? " (DETERMINISTIC)" : "");
	// Machine-readable verdict for the harness, on stdout where a wrapper
	// script can grep it whatever the log destination is.
	printf("DET: %s frames=%d mismatches=%d first=%d sub=%s\n",
			ok ? "PASS" : "FAIL", g_DetFrameCount, g_DetMismatchCount,
			g_DetFirstDivergeFrame, g_DetFirstDivergeSub);
	fflush(stdout);
	detClose();
	g_DetMode = DET_OFF;
	if (g_DetExitOnEnd) {
		// _exit, not exit: the atexit cleanup writes pd.ini, and a harness run
		// must not rewrite the config it was pointed at (same reasoning as the
		// --asset CLI in pdmain.c).
		_exit(ok ? 0 : 1);
	}
}

void detFrameBegin(void)
{
	if (g_DetMode == DET_RECORD) {
		detCaptureInputs(&g_DetIn);
	} else if (g_DetMode == DET_REPLAY && !g_DetReplayDone) {
		if (!detReadFrame(&g_DetIn, &g_DetExpected)) {
			g_DetReplayDone = 1;
			detReplayEnded();
			return;
		}
		detInjectInputs(&g_DetIn);
	}
}

// Per-entity text dump of the folded fields for the first --det-dump frames,
// so two runs can be diffed to find WHICH field forks when the aggregate hash
// says a subsystem diverged.
static void detDumpState(void)
{
	s32 i;
	printf("DETDUMP frame=%d rng=%016llx/%016llx lvf60=%d lvf240=%d rem=%d\n", g_Vars.lvframenum,
			(unsigned long long)g_RngSeed, (unsigned long long)g_Rng2Seed,
			g_Vars.lvframe60, g_Vars.lvframe240, g_Vars.lvupdate240rem);
	for (i = 0; i < MAX_PLAYERS; i++) {
		const struct player *pl = g_Vars.players[i];
		if (pl) {
			printf("DETDUMP  pl%d th=%g va=%g sp=%g/%g/%g/%g smt=%d sg=%g mg=%g g=%g bd=%g,%g,%g fall=%d/%d cr=%d/%d/%g dead=%d hp=%g/%g/%g/%g/%g\n",
					i, pl->vv_theta, pl->vv_verta, pl->speedsideways, pl->speedstrafe, pl->speedforwards, pl->speedboost,
					pl->speedmaxtime60, pl->sumground, pl->vv_manground, pl->vv_ground,
					pl->bdeltapos.x, pl->bdeltapos.y, pl->bdeltapos.z, pl->isfalling, pl->fallstart,
					pl->crouchpos, pl->autocrouchpos, pl->crouchoffset, pl->isdead,
					pl->bondhealth, pl->oldhealth, pl->oldarmour, pl->apparenthealth, pl->apparentarmour);
		}
	}
	for (i = 0; i < g_NumChrSlots; i++) {
		const struct chrdata *chr = &g_ChrSlots[i];
		if (chr->chrnum == -1 || chr->prop == NULL) {
			continue;
		}
		printf("DETDUMP  chr%d n=%d act=%d fl=%08x/%08x/%08x hid=%08x dmg=%g/%g sh=%g t60=%d pp=%g,%g,%g fs=%g,%g,%g g=%g/%g/%g tgt=%d m=%d al=%d r=%d of=%g mf=%g ms=%g ma=%d ai=%d/%d/%d\n",
				i, chr->chrnum, chr->actiontype, chr->flags, chr->flags2, chr->chrflags, chr->hidden,
				chr->damage, chr->maxdamage, chr->cshield, chr->timer60,
				chr->prevpos.x, chr->prevpos.y, chr->prevpos.z, chr->fallspeed.x, chr->fallspeed.y, chr->fallspeed.z,
				chr->ground, chr->manground, chr->sumground, (s32)chr->target, chr->morale, chr->alertness, chr->random,
				chr->oldframe, chr->magicframe, chr->magicspeed, (s32)chr->magicanim,
				chr->aioffset, (s32)chr->aireturnlist, (s32)chr->aishotlist);
	}
	{
		struct prop *p = g_Vars.activeprops;
		s32 count = 0;
		while (p != NULL && count <= g_Vars.maxprops) {
			printf("DETDUMP  prop%d t=%d f=%02x reg=%d pos=%g,%g,%g z=%g r0=%d act=%d\n", count, p->type, p->flags,
					(s32)p->timetoregen, p->pos.x, p->pos.y, p->pos.z, p->z, (s32)p->rooms[0], (s32)p->active);
			count++;
			if (p == g_Vars.activepropstail) break;
			p = p->next;
		}
	}
	fflush(stdout);
}

void detEndTick(void)
{
	if ((g_DetMode == DET_RECORD || g_DetMode == DET_REPLAY) && g_DetFrameCount < g_DetDumpFrames) {
		detDumpState();
	}
	if (g_DetMode == DET_RECORD) {
		struct dethash h;
		detComputeHash(&h);
		detWriteFrame(&g_DetIn, &h);
		g_DetFrameCount++;
	} else if (g_DetMode == DET_REPLAY && !g_DetReplayDone) {
		struct dethash h;
		detComputeHash(&h);
		detCompare(&h, &g_DetExpected, g_DetIn.lvframenum);
		g_DetFrameCount++;
	}
}

s32 detTickPinActive(void)
{
	return g_DetMode == DET_RECORD || g_DetMode == DET_REPLAY || g_NetMode != 0;
}

/* ---- control ---- */

void detStop(void)
{
	if (g_DetMode == DET_RECORD) {
		sysLogPrintf(LOG_NOTE, "DET: recording stopped (%d frames written to '%s')", g_DetFrameCount, g_DetPath);
	} else if (g_DetMode == DET_REPLAY) {
		sysLogPrintf(LOG_NOTE, "DET: replay stopped at frame %d", g_DetFrameCount);
	}
	detClose();
	g_DetMode = DET_OFF;
}

s32 detStartRecord(const char *path)
{
	detStop();
	if (!path || !*path) {
		return 0;
	}
	g_DetFile = fopen(path, "wb");
	if (!g_DetFile || !detWriteHeader()) {
		detClose();
		sysLogPrintf(LOG_ERROR, "DET: could not open '%s' for record", path);
		return 0;
	}
	strncpy(g_DetPath, path, sizeof(g_DetPath) - 1);
	g_DetPath[sizeof(g_DetPath) - 1] = '\0';
	detResetState();
	g_DetMode = DET_RECORD;
	sysLogPrintf(LOG_NOTE, "DET: recording to '%s' (fixed step pinned)", path);
	return 1;
}

s32 detStartReplay(const char *path, s32 exitonend)
{
	detStop();
	if (!path || !*path) {
		return 0;
	}
	g_DetFile = fopen(path, "rb");
	if (!g_DetFile || !detCheckHeader()) {
		detClose();
		sysLogPrintf(LOG_ERROR, "DET: could not open/validate '%s' for replay", path);
		if (exitonend) {
			printf("DET: FAIL could not open '%s'\n", path);
			fflush(stdout);
			_exit(2);
		}
		return 0;
	}
	strncpy(g_DetPath, path, sizeof(g_DetPath) - 1);
	g_DetPath[sizeof(g_DetPath) - 1] = '\0';
	detResetState();
	g_DetExitOnEnd = exitonend;
	g_DetMode = DET_REPLAY;
	sysLogPrintf(LOG_NOTE, "DET: replaying '%s' (fixed step pinned)%s", path,
			exitonend ? ", exiting at the end" : "");
	return 1;
}

void detInitFromArgs(void)
{
	const char *rec = sysArgGetString("--detrec");
	const char *play = sysArgGetString("--detplay");
	const s32 exitonend = sysArgCheck("--det-exit");

	g_DetDumpFrames = sysArgGetInt("--det-dump", 0);

	if (rec && play) {
		sysLogPrintf(LOG_ERROR, "DET: --detrec and --detplay are exclusive; ignoring both");
		return;
	}
	if (rec) {
		detStartRecord(rec);
	} else if (play) {
		detStartReplay(play, exitonend);
	}
}

void detLogInfo(void)
{
	struct dethash h;
	const char *modestr = g_DetMode == DET_OFF ? "OFF" :
			g_DetMode == DET_RECORD ? "RECORD" : "REPLAY";
	detComputeHash(&h);
	sysLogPrintf(LOG_NOTE, "DET: mode=%s frames=%d lvframenum=%d all=%016llx rng=%016llx pl=%016llx prop=%016llx chr=%016llx",
			modestr, g_DetFrameCount, g_Vars.lvframenum,
			(unsigned long long)h.all, (unsigned long long)h.rng,
			(unsigned long long)h.players, (unsigned long long)h.props,
			(unsigned long long)h.chrs);
	if (g_DetMismatchCount > 0) {
		sysLogPrintf(LOG_NOTE, "DET: %d mismatches; first diverge frame=%d sub=%s",
				g_DetMismatchCount, g_DetFirstDivergeFrame, g_DetFirstDivergeSub);
	}
}

/* ---- command surface (chained from netConsoleCommand) ---- */

s32 detConsoleCommand(const char *cmd, const char *arg)
{
	if (strcmp(cmd, "dethash") == 0 || strcmp(cmd, "detinfo") == 0) {
		detLogInfo();
		return 1;
	}
	if (strcmp(cmd, "detrec") == 0) {
		if (*arg) {
			detStartRecord(arg);
		} else {
			detStop();
		}
		return 1;
	}
	if (strcmp(cmd, "detplay") == 0) {
		if (*arg) {
			detStartReplay(arg, 0);
		} else {
			detStop();
		}
		return 1;
	}
	return 0;
}
