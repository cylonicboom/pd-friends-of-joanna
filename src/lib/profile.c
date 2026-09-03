#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "lib/profile.h"

static struct profileframerecord g_ProfileHistory[PROFILE_HISTORY_LEN];
static struct profileframerecord g_ProfileCurrent;
static s32 g_ProfileHistoryHead = 0;
static s32 g_ProfileHistoryCount = 0;
static bool g_ProfileCurrentActive = false;

static s32 profileMarkerSlot(u32 value)
{
	switch (value) {
	case PROFILE_MAINTICK_START: return PROFILE_SLOT_MAINTICK_START;
	case PROFILE_MAINTICK_END: return PROFILE_SLOT_MAINTICK_END;
	case PROFILE_AUDIOFRAME_START: return PROFILE_SLOT_AUDIOFRAME_START;
	case PROFILE_AUDIOFRAME_END: return PROFILE_SLOT_AUDIOFRAME_END;
	case PROFILE_RSP_START: return PROFILE_SLOT_RSP_START;
	case PROFILE_RSP_END: return PROFILE_SLOT_RSP_END;
	case PROFILE_RDP_START1:
	case PROFILE_RDP_START2: return PROFILE_SLOT_RDP_START;
	case PROFILE_RDP_END: return PROFILE_SLOT_RDP_END;
	default: return -1;
	}
}

static void profileBeginCurrent(void)
{
	g_ProfileCurrent.frame = g_Vars.lvframenum;
	g_ProfileCurrent.markermask = 0;
	g_ProfileCurrent.diffframe60f = g_Vars.diffframe60f;
	g_ProfileCurrent.diffframe240f = g_Vars.diffframe240f;
	g_ProfileCurrent.stage = g_Vars.stagenum;

	for (s32 i = 0; i < PROFILE_MARKER_SLOT_COUNT; ++i) {
		g_ProfileCurrent.markers[i] = 0;
	}

	for (s32 i = 0; i < 4; ++i) {
		g_ProfileCurrent.rdp_counters[i] = 0;
	}

	g_ProfileCurrentActive = true;
}

static void profileCommitCurrent(void)
{
	if (!g_ProfileCurrentActive || g_ProfileCurrent.markermask == 0) {
		return;
	}

	g_ProfileHistory[g_ProfileHistoryHead] = g_ProfileCurrent;
	g_ProfileHistoryHead = (g_ProfileHistoryHead + 1) % PROFILE_HISTORY_LEN;

	if (g_ProfileHistoryCount < PROFILE_HISTORY_LEN) {
		g_ProfileHistoryCount++;
	}

	g_ProfileCurrentActive = false;
}

void profileInit(void)
{
	g_ProfileHistoryHead = 0;
	g_ProfileHistoryCount = 0;
	g_ProfileCurrentActive = false;
}

void profileTick(void)
{
	// empty
}

void profileReset(void)
{
	profileBeginCurrent();
}

void profile00009a98(void)
{
	profileCommitCurrent();
}

void profileSetMarker(u32 value)
{
	s32 slot = profileMarkerSlot(value);

	if (slot < 0) {
		return;
	}

	if (!g_ProfileCurrentActive) {
		profileBeginCurrent();
	}

	g_ProfileCurrent.markers[slot] = osGetCount();
	g_ProfileCurrent.markermask |= 1 << slot;
}

Gfx *profileRender(Gfx *gdl)
{
	return gdl;
}

s32 profileGetFrameHistoryCount(void)
{
	return g_ProfileHistoryCount;
}

s32 profileGetFrameHistoryRecord(s32 index, struct profileframerecord *record)
{
	s32 realindex;

	if (!record || index < 0 || index >= g_ProfileHistoryCount) {
		return false;
	}

	realindex = g_ProfileHistoryHead - g_ProfileHistoryCount + index;
	while (realindex < 0) {
		realindex += PROFILE_HISTORY_LEN;
	}
	realindex %= PROFILE_HISTORY_LEN;

	*record = g_ProfileHistory[realindex];
	return true;
}
