#include <string.h>
#include <ultra64.h>
#include "game/playermgr.h"
#include "constants.h"
#include "game/game_00b820.h"
#include "game/title.h"
#include "bss.h"
#include "lib/memp.h"
#include "data.h"
#include "types.h"
#ifndef PLATFORM_N64
#include "video.h"
#endif

void chrmgrReset(void)
{
	s32 i;

	var80062968 = 1;
	var8006296c = 0;
	g_SelectedAnimNum = 0;
	var80062974 = 0;
	var80062978 = 0;
	var8006297c = 0;
	g_NextChrnum = 5000;
	g_ChrSlotStore = NULL;
	g_ChrSlotCount = 0;

	g_ShieldHits = mempAlloc(sizeof(struct shieldhit) * 20, MEMPOOL_STAGE);

	for (i = 0; i < 20; i++) {
		g_ShieldHits[i].prop = NULL;
	}

	g_ShieldHitActive = 0;
	g_NumChrs = 0;
	g_Chrnums = NULL;
	g_ChrIndexes = NULL;
	var80062960 = mempAlloc(ALIGN16(15 * sizeof(struct var80062960)), MEMPOOL_STAGE);

	for (i = 0; i < ARRAYCOUNT(var8009ccc0); i++) {
#ifdef PLATFORM_N64
		var8009ccc0[i] = (void *)ALIGN64(mempAlloc(16 * 16 * sizeof(u16) + 0x40, MEMPOOL_STAGE));
#else
		if (!var8009ccc0[i]) {
			var8009ccc0[i] = videoCreateFramebuffer(16, 16, false, false);
		}
#endif
	}

	resetSomeStageThings();
}

// The one place the chr slot store is allocated; see game/chrslots.h.
void chrmgrConfigure(s32 numchrs)
{
	s32 i;

	g_ChrSlotCount = playermgrBudgetCount() + numchrs + 10; // fojo: MAX_PLAYERS in a team mission
	g_ChrSlotStore = mempAlloc(ALIGN16(g_ChrSlotCount * sizeof(struct chrdata)), MEMPOOL_STAGE);

#ifndef PLATFORM_N64
	// Zero the whole slot array. MEMPOOL_STAGE is rewound, never cleared, so
	// slots otherwise start as whatever the PREVIOUS stage left at this pool
	// offset - and chrInit is field-by-field, leaving the port-appended fields
	// (the netplay snapshot ring pointer among them) on those stale bytes.
	memset(g_ChrSlotStore, 0, g_ChrSlotCount * sizeof(struct chrdata));
#endif

	for (i = 0; i < g_ChrSlotCount; i++) {
		g_ChrSlotStore[i].chrnum = -1;
		g_ChrSlotStore[i].model = NULL;
		g_ChrSlotStore[i].prop = NULL;
	}

	g_NumChrs = 0;
	g_Chrnums = mempAlloc(ALIGN16(g_ChrSlotCount * sizeof(g_Chrnums[0])), MEMPOOL_STAGE);
	g_ChrIndexes = mempAlloc(ALIGN16(g_ChrSlotCount * sizeof(g_ChrIndexes[0])), MEMPOOL_STAGE);

	for (i = 0; i < g_ChrSlotCount; i++) {
		g_Chrnums[i] = -1;
		g_ChrIndexes[i] = -1;
	}
}
