#include <ultra64.h>
#include "game/playermgr.h"
#include "constants.h"
#include "game/gfxmemory.h"
#include "game/stubs/game_175f50.h"
#include "bss.h"
#include "lib/args.h"
#include "lib/rzip.h"
#include "lib/dma.h"
#include "lib/memp.h"
#include "lib/rng.h"
#include "lib/str.h"
#include "data.h"
#include "types.h"
#include "platform.h"
#ifndef PLATFORM_N64
#include "system.h"
#endif

/**
 * This file handles memory usage for graphics related tasks.
 *
 * There are two pools, "gfx" and "vtx", which are used to store different data.
 *
 * The gfx pool (g_GfxBuffers) is sized based on the stage's -mgfx and -mgfxtra
 * arguments. It contains only the master display list's GBI bytecode.
 * The master gdl is passed through all rendering functions in the game engine,
 * where each appends to the display list.
 *
 * The vtx pool (g_VtxBuffers) is sized based on the stage's -mvtx argument.
 * It is used for auxiliary graphics data such as vertex arrays, matrices and
 * colours.
 *
 * Both the gfx and vtx pools are split into two buffers of equal size.
 * Only one buffer is active at a time - the other is being drawn to the screen
 * while the active one is being built. Each time a frame is finished the active
 * buffer index is swapped to the other one.
 *
 * Both the gfx and vtx pools have a third element in them, but this is just a
 * marker for the end of the second element's allocation.
 */

/**
 * On 64-bit platforms the Gfx struct is twice as large.
*/
#ifdef PLATFORM_64BIT
#define GFX_SIZE_MULTIPLIER 2
#else
#define GFX_SIZE_MULTIPLIER 1
#endif

u8 *g_GfxBuffers[NUM_GFXTASKS + 1];
u32 var800aa58c;
u8 *g_VtxBuffers[NUM_GFXTASKS + 1];
u8 *g_GfxMemPos;
u8 g_GfxActiveBufferIndex;
u32 g_GfxRequestedDisplayList;

u32 g_GfxSizesByPlayerCount[] = {
	0x00010000 * GFX_SIZE_MULTIPLIER,
	0x00018000 * GFX_SIZE_MULTIPLIER,
	0x00020000 * GFX_SIZE_MULTIPLIER,
	0x00028000 * GFX_SIZE_MULTIPLIER,
};

u32 g_VtxSizesByPlayerCount[] = {
	0x00010000,
	0x00018000,
	0x00020000,
	0x00028000,
};

s32 g_GfxNumSwapsPerBuffer[NUM_GFXTASKS] = {0, 1};
u32 g_GfxNumSwaps = 2;

#ifndef PLATFORM_N64
/**
 * fojo (#352): the vtx pool's bump allocators never checked the end of the
 * active buffer. A stage whose -mvtx budget is smaller than what it draws ran
 * g_GfxMemPos straight off g_VtxBuffers[2] into the next stage allocations -
 * on the title stage (-mvtx20) with the profile picker and slow stars up, that
 * was the text banks, then fonts, then hud messages, each found later as the
 * Air Base string, a NULL glyph, a garbage playernum.
 *
 * Now an allocation that does not fit is served from a separate heap scratch
 * block instead. Everything that overflows in a frame shares that block, so
 * those vertices can draw wrong for the frame; nothing outside the pool is
 * written. The first overflow per stage is logged with the numbers needed to
 * set the stage's -mvtx, and gfxReset logs the previous stage's high-water
 * mark so budgets can be set from measurements.
 */
static u8 *g_VtxOverflow = NULL;
static u32 g_VtxOverflowSize = 0;
static s32 g_VtxOverflowLogged = false;
static u32 g_VtxOverflowCount = 0;
static u32 g_VtxHighWater = 0;
static u32 g_VtxWorstRequest = 0;

static void gfxNoteHighWater(void)
{
	const u32 used = g_GfxMemPos - g_VtxBuffers[g_GfxActiveBufferIndex];

	if (used > g_VtxHighWater) {
		g_VtxHighWater = used;
	}
}

static void *gfxVtxReserve(u32 size)
{
	u8 *ptr = g_GfxMemPos;
	u8 *end = g_VtxBuffers[g_GfxActiveBufferIndex + 1];

	if (ptr + size <= end) {
		g_GfxMemPos += size;
		return ptr;
	}

	// past the end of this frame's buffer: never write there
	{
		const u32 used = ptr - g_VtxBuffers[g_GfxActiveBufferIndex];
		const u32 wanted = used + size;

		g_VtxOverflowCount++;

		if (wanted > g_VtxWorstRequest) {
			g_VtxWorstRequest = wanted;
		}

		if (!g_VtxOverflowLogged) {
			g_VtxOverflowLogged = true;
			sysLogPrintf(LOG_WARNING, "gfx: vtx pool overflow on stage 0x%02x: frame needs >= 0x%x bytes, buffer is 0x%x (-mvtx%u); excess drawn from scratch",
					g_Vars.stagenum, wanted, (u32)(end - g_VtxBuffers[g_GfxActiveBufferIndex]),
					(u32)(end - g_VtxBuffers[g_GfxActiveBufferIndex]) / 1024);
		}
	}

	if (size > g_VtxOverflowSize) {
		u8 *grown = sysMemAlloc(size);

		if (grown == NULL) {
			// nothing safe to hand out; the old behaviour, but loud
			sysLogPrintf(LOG_ERROR, "gfx: vtx overflow scratch of 0x%x bytes could not be allocated", size);
			g_GfxMemPos += size;
			return ptr;
		}

		// the previous scratch may still be referenced by this frame's
		// display list; leave it allocated rather than free it under the GPU
		g_VtxOverflow = grown;
		g_VtxOverflowSize = size;
	}

	return g_VtxOverflow;
}
#endif

/**
 * Allocate graphics memory from the heap. Presumably called on stage load.
 *
 * Comments in this function are strings that appear in an XBLA debug build.
 * They were likely in the N64 version but ifdeffed out.
 */
void gfxReset(void)
{
	s32 stack;

	if (argFindByPrefix(1, "-mgfx")) {
		// Argument specified master_dl_size\n
		s32 gfx;
		s32 gfxtra = 0;

		gfx = strtol(argFindByPrefix(1, "-mgfx"), NULL, 0) * 1024;

		if (argFindByPrefix(1, "-mgfxtra")) {
			// ******** Extra specified but are we in the correct game mode I wonder???\n
			if ((g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) && PLAYERCOUNT() == 2) {
				// ******** Extra Display List Memeory Required\n
				// ******** Shall steal from video buffer\n
				// ******** If you try and run hi-res then\n
				// ******** you're gonna shafted up the arse\n
				// ******** so don't blame me\n
				gfxtra = strtol(argFindByPrefix(1, "-mgfxtra"), NULL, 0) * 1024;
			} else {
				// ******** No we're not so there\n
			}
		}

		// ******** Original Amount required = %dK ber buffer\n
		// ******** Extra Amount required = %dK ber buffer\n
		// ******** Total of %dK (Double Buffered)\n
		g_GfxSizesByPlayerCount[playermgrBudgetCount() - 1] = (gfx + gfxtra) * GFX_SIZE_MULTIPLIER;
	}

	if (argFindByPrefix(1, "-mvtx")) {
		// Argument specified mtxvtx_size\n
		g_VtxSizesByPlayerCount[playermgrBudgetCount() - 1] = strtol(argFindByPrefix(1, "-mvtx"), NULL, 0) * 1024;
	}

	// %d Players : Allocating %d bytes for master dl's\n
	g_GfxBuffers[0] = mempAlloc(g_GfxSizesByPlayerCount[playermgrBudgetCount() - 1] * NUM_GFXTASKS, MEMPOOL_STAGE);
	g_GfxBuffers[1] = g_GfxBuffers[0] + g_GfxSizesByPlayerCount[playermgrBudgetCount() - 1];
	g_GfxBuffers[2] = g_GfxBuffers[1] + g_GfxSizesByPlayerCount[playermgrBudgetCount() - 1];

	// Allocating %d bytes for mtxvtx space\n
	g_VtxBuffers[0] = mempAlloc(g_VtxSizesByPlayerCount[playermgrBudgetCount() - 1] * NUM_GFXTASKS, MEMPOOL_STAGE);
	g_VtxBuffers[1] = g_VtxBuffers[0] + g_VtxSizesByPlayerCount[playermgrBudgetCount() - 1];
	g_VtxBuffers[2] = g_VtxBuffers[1] + g_VtxSizesByPlayerCount[playermgrBudgetCount() - 1];

#ifndef PLATFORM_N64
	if (g_VtxHighWater || g_VtxOverflowCount) {
		sysLogPrintf(g_VtxOverflowCount ? LOG_WARNING : LOG_NOTE,
				"gfx: previous stage vtx high-water 0x%x, %u overflowed allocations, worst frame wanted 0x%x",
				g_VtxHighWater, g_VtxOverflowCount, g_VtxWorstRequest);
	}

	g_VtxHighWater = 0;
	g_VtxOverflowCount = 0;
	g_VtxWorstRequest = 0;
	g_VtxOverflowLogged = false;
#endif

	g_GfxActiveBufferIndex = 0;
	g_GfxRequestedDisplayList = false;
	g_GfxMemPos = g_VtxBuffers[0];
}

Gfx *gfxGetMasterDisplayList(void)
{
	g_GfxRequestedDisplayList = true;

	return (Gfx *)g_GfxBuffers[g_GfxActiveBufferIndex];
}

Vtx *gfxAllocateVertices(u32 count)
{
#ifndef PLATFORM_N64
	// g_GfxMemPos is always 16-aligned here, so this is the same advance as
	// "add, then align the position"
	return gfxVtxReserve(ALIGN16(count * sizeof(Vtx)));
#else
	void *ptr = g_GfxMemPos;
	g_GfxMemPos += count * sizeof(Vtx);
	g_GfxMemPos = (u8 *)ALIGN16((uintptr_t)g_GfxMemPos);

	return ptr;
#endif
}

void *gfxAllocateMatrix(void)
{
#ifndef PLATFORM_N64
	return gfxVtxReserve(sizeof(Mtx));
#else
	void *ptr = g_GfxMemPos;
	g_GfxMemPos += sizeof(Mtx);

	return ptr;
#endif
}

/**
 * sizeof(LookAt) is 0x10 and it consists of two Light structs of 0x8 each.
 * The function allocates 0x8 for every count, so it could be allocating lights
 * instead, however it's only used for LookAts so it's named as LookAt.
 */
LookAt *gfxAllocateLookAt(s32 count)
{
#ifndef PLATFORM_N64
#ifdef PLATFORM_64BIT
	return gfxVtxReserve(count * (sizeof(LookAt) * 2));
#else
	return gfxVtxReserve(count * (sizeof(LookAt) / 2));
#endif
#else
	void *ptr = g_GfxMemPos;
	g_GfxMemPos += count * (sizeof(LookAt) / 2);

	return ptr;
#endif
}

Col *gfxAllocateColours(s32 count)
{
#ifndef PLATFORM_N64
	return gfxVtxReserve(ALIGN16(count * sizeof(Col)));
#else
	void *ptr = g_GfxMemPos;
	count = ALIGN16(count * sizeof(Col));
	g_GfxMemPos += count;

	return ptr;
#endif
}

void *gfxAllocate(u32 size)
{
#ifndef PLATFORM_N64
	return gfxVtxReserve(ALIGN16(size));
#else
	void *ptr = g_GfxMemPos;
	size = ALIGN16(size);
	g_GfxMemPos += size;

	return ptr;
#endif
}

void gfxSwapBuffers(void)
{
#ifndef PLATFORM_N64
	gfxNoteHighWater();
#endif
	g_GfxActiveBufferIndex ^= 1;
	g_GfxRequestedDisplayList = false;
	g_GfxMemPos = g_VtxBuffers[g_GfxActiveBufferIndex];
	g_GfxNumSwapsPerBuffer[g_GfxActiveBufferIndex] = g_GfxNumSwaps;
	g_GfxNumSwaps++;

	if (g_GfxNumSwaps == -1) {
		g_GfxNumSwaps = 2;
	}
}

s32 gfxGetFreeGfx(Gfx *gdl)
{
	return (Gfx *)g_GfxBuffers[g_GfxActiveBufferIndex + 1] - gdl;
}

u32 gfxGetFreeVtx(void)
{
	return g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_GfxMemPos;
}

// Total size (bytes) of one vtx-pool buffer. used = this - gfxGetFreeVtx().
// From Kai (be46717), for pd.perf.
u32 gfxGetVtxPoolSize(void)
{
	return g_VtxBuffers[g_GfxActiveBufferIndex + 1] - g_VtxBuffers[g_GfxActiveBufferIndex];
}
