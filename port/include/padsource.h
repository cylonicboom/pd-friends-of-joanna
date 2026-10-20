#ifndef _IN_PORT_PADSOURCE_H
#define _IN_PORT_PADSOURCE_H

#include <ultra64.h>
#include "types.h"

// Pad source: lets code stand in for a controller. The override is applied in
// joyReadData right after the hardware read fills a sample, so everything
// downstream (the sample ring, edge detection, joybutinhibit, the per-player
// pad mapping) sees it exactly as it would see a real pad. On a headless build
// the hardware read is a zeroed stub, so this is the only way a headless pawn
// moves at all.
//
// One-shot by design: an override covers the NEXT sample only and then clears.
// A script that wants a held button sets it every tick. This keeps a script
// that errors out from leaving a stuck pad behind.
//
// Record/replay (det.c) runs later in the frame and rewrites the whole ring,
// so during a replay the replay wins over this.

// Set the next sample for controller `pad` (0..MAXCONTROLLERS-1). Returns 1 if
// the pad index was valid.
s32 padsourceSet(s32 pad, const OSContPad *in);

// Drop a pending override.
void padsourceClear(s32 pad);
void padsourceClearAll(void);

// Called by joyReadData with the freshly read sample for every controller.
// Applies and clears any pending overrides.
void padsourceApply(OSContPad *pads, s32 numpads);

// 1 if any override is pending (diagnostics).
s32 padsourceIsPending(void);

#endif
