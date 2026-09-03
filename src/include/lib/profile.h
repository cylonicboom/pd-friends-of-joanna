#ifndef _IN_LIB_PROFILE_H
#define _IN_LIB_PROFILE_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROFILE_HISTORY_LEN 240
#define PROFILE_MARKER_SLOT_COUNT 8

enum profilemarkerslot {
	PROFILE_SLOT_MAINTICK_START,
	PROFILE_SLOT_MAINTICK_END,
	PROFILE_SLOT_AUDIOFRAME_START,
	PROFILE_SLOT_AUDIOFRAME_END,
	PROFILE_SLOT_RSP_START,
	PROFILE_SLOT_RSP_END,
	PROFILE_SLOT_RDP_START,
	PROFILE_SLOT_RDP_END,
};

struct profileframerecord {
	u32 frame;
	u32 markers[PROFILE_MARKER_SLOT_COUNT];
	u32 markermask;
	f32 diffframe60f;
	f32 diffframe240f;
	s32 stage;
	u32 rdp_counters[4];
};

void profileInit(void);
void profileTick(void);
void profileReset(void);
void profile00009a98(void);
void profileSetMarker(u32 arg0);
Gfx *profileRender(Gfx *gdl);
s32 profileGetFrameHistoryCount(void);
s32 profileGetFrameHistoryRecord(s32 index, struct profileframerecord *record);

#ifdef __cplusplus
}
#endif

#endif
