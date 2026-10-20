#ifndef _IN_PORT_DET_H
#define _IN_PORT_DET_H

#include <ultra64.h>
#include "types.h"

// Determinism harness (port-only). Ported from Perfect Dark Kai
// (cylonicboom/perfect-dark @ kai be46717, port/src/det.c), trimmed: the
// optional Game.FixedTick render/sim decouple and the netplay step pin are not
// here yet; what is here is the part the headless test harness needs.
//
// Three things:
//   - a decomposable whole-state hash (rng / players / props / chrs) so a
//     divergence report can name the subsystem that forked;
//   - a fixed 1/60 step pin on lvupdate240 while recording or replaying, so
//     the sim advances identically regardless of wall clock;
//   - input record / replay through the joy raw sample ring: record writes
//     {inputs, hash} per tick, replay injects the inputs and compares.
//
// It is a no-op during normal play via a runtime g_DetMode check. There is no
// compile-time guard and nothing changes while DET_OFF.
//
// Boot flags (port/src/main.c): --detrec <file>, --detplay <file>,
// --det-exit (quit when the replay ends; exit code 0 = deterministic, 1 =
// diverged). Kai exposed the same through its `/` console, which fojo does
// not have; the flags are what the harness drives.

#define DET_OFF    0
#define DET_RECORD 2
#define DET_REPLAY 3

struct dethash {
	u64 rng;
	u64 players;
	u64 props;
	u64 chrs;
	u64 all;
};

extern s32 g_DetMode;

// Fold the current live sim state. Walks entities by index / list order, never
// by address, and folds only sim-authoritative value fields.
void detComputeHash(struct dethash *out);

// Called from lvTick after lvupdate240 has been chosen and before the
// lvupdate60 / lvframe* derivation. No-op unless recording or replaying; then
// pins the step to a fixed 1/60 (a paused frame stays 0).
void detPinTimestep(void);

// Per-frame hooks. detFrameBegin() goes after the frame's joy samples are
// consumed (joyDebugJoy) and before lvTick: capture the ring in record mode,
// overwrite it from the recording in replay mode. detEndTick() goes after the
// per-player ticks: write the frame record, or recompute and compare.
void detFrameBegin(void);
void detEndTick(void);

// Start recording to / replaying from a file. Return 1 on success. A replay
// with exitonend set calls _exit when the recording runs out: 0 if every
// tick matched, 1 otherwise.
s32 detStartRecord(const char *path);
s32 detStartReplay(const char *path, s32 exitonend);
void detStop(void);

// Parse the --detrec / --detplay / --det-exit arguments. Called once from
// main() after the system args exist.
void detInitFromArgs(void);

// Wraps the boot-time rngSetSeed argument: recorded into the file while
// recording, substituted from the file while replaying, passed through
// otherwise.
u32 detBootSeed(u32 seed);

// One-line status to the log (mode, frame count, divergence if any).
void detLogInfo(void);

#endif
