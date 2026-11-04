#ifndef _IN_GAME_CHRSLOTS_H
#define _IN_GAME_CHRSLOTS_H
#include <ultra64.h>
#include "types.h"

/*
 * Chr slots. Reach them ONLY through these three:
 *
 *   chrsGetNumSlots()    how many slot indices exist; 0 before chrmgrConfigure
 *   chrslotGet(i)        slot i, for 0 <= i < chrsGetNumSlots()
 *   chrslotIndexOf(chr)  the slot index of a slot pointer, -1 if it is not one
 *
 * A slot's index and its pointer both stay valid for the whole stage.
 *
 * DO NOT index g_ChrSlotStore, subtract it from a chr pointer, walk a pointer
 * from one slot to the next, or range-check a pointer against it. The slots
 * are about to stop being one contiguous array (they become pages that grow
 * at runtime), and every one of those idioms will then compile and be wrong.
 *
 * The old g_ChrSlots / g_NumChrSlots were renamed away on purpose, so code
 * written against them -- including old code merged back in from a fork --
 * fails to compile instead of silently indexing the wrong memory. Port it to
 * the three calls above. Only chr.c and chrmgr.c own the store.
 */
extern struct chrdata *g_ChrSlotStore;
extern s32 g_ChrSlotCount;

static inline s32 chrsGetNumSlots(void)
{
	return g_ChrSlotCount;
}

static inline struct chrdata *chrslotGet(s32 index)
{
	return &g_ChrSlotStore[index];
}

static inline s32 chrslotIndexOf(const struct chrdata *chr)
{
	if (chr == NULL || g_ChrSlotStore == NULL
			|| chr < g_ChrSlotStore || chr >= g_ChrSlotStore + g_ChrSlotCount) {
		return -1;
	}

	return (s32)(chr - g_ChrSlotStore);
}

#endif
