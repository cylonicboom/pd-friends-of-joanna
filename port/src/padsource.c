/**
 * Pad source — see port/include/padsource.h.
 */
#include <ultra64.h>
#include <string.h>
#include "constants.h"
#include "types.h"
#include "platform.h"
#include "padsource.h"

#ifndef PLATFORM_N64

static OSContPad g_PadSourceNext[MAXCONTROLLERS];
static u8 g_PadSourcePending[MAXCONTROLLERS];

s32 padsourceSet(s32 pad, const OSContPad *in)
{
	if (pad < 0 || pad >= MAXCONTROLLERS || in == NULL) {
		return 0;
	}
	g_PadSourceNext[pad] = *in;
	g_PadSourceNext[pad].errnum = 0; // a present, healthy controller
	g_PadSourcePending[pad] = 1;
	return 1;
}

void padsourceClear(s32 pad)
{
	if (pad >= 0 && pad < MAXCONTROLLERS) {
		g_PadSourcePending[pad] = 0;
	}
}

void padsourceClearAll(void)
{
	memset(g_PadSourcePending, 0, sizeof(g_PadSourcePending));
}

void padsourceApply(OSContPad *pads, s32 numpads)
{
	s32 i;
	if (numpads > MAXCONTROLLERS) {
		numpads = MAXCONTROLLERS;
	}
	for (i = 0; i < numpads; i++) {
		if (g_PadSourcePending[i]) {
			pads[i] = g_PadSourceNext[i];
			g_PadSourcePending[i] = 0;
		}
	}
}

s32 padsourceIsPending(void)
{
	s32 i;
	for (i = 0; i < MAXCONTROLLERS; i++) {
		if (g_PadSourcePending[i]) {
			return 1;
		}
	}
	return 0;
}

#endif
