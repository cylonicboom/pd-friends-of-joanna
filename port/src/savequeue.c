#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"

#include "config.h"
#include "input.h"
#include "savequeue.h"
#include "system.h"

// Defined in port/src/libultra.c, which owns the EEPROM shadow and its path.
// Writes the shadow out if it has been marked; a no-op otherwise.
void osEepromFlush(void);

// Defined in src/game/mplayer/mplayer.c. Turns each changed profile into the
// writes that record it - slot name hashes, and a pak save for players that
// have a file. A no-op when nothing changed.
void mpProfileFlushDirty(void);

/*
 * The backstop. An idle frame almost always comes along sooner than this -
 * the flush usually lands on the frame after the write - so this only matters
 * when the machine is pegged and never has spare budget. At 60fps it is two
 * seconds, which is the most play a hard kill can cost.
 */
#define SAVEQUEUE_DEADLINE_FRAMES 120

/*
 * How much of the frame's budget has to be left over before a frame counts as
 * idle. Half is deliberately generous: the write is a couple of hundred
 * microseconds on any sane filesystem, and being wrong costs a dropped frame,
 * not a dropped save.
 */
#define SAVEQUEUE_IDLE_NUMERATOR   1
#define SAVEQUEUE_IDLE_DENOMINATOR 2

static s32 g_SaveQueueEepromDirty = 0;
static s32 g_SaveQueueConfigDirty = 0;
static u32 g_SaveQueueFrame = 0;
static u32 g_SaveQueueDirtySinceFrame = 0;
static u32 g_SaveQueueFlushCount = 0;

void saveQueueMarkEeprom(void)
{
	if (!g_SaveQueueEepromDirty && !g_SaveQueueConfigDirty) {
		g_SaveQueueDirtySinceFrame = g_SaveQueueFrame;
	}

	g_SaveQueueEepromDirty = 1;
}

void saveQueueMarkConfig(void)
{
	if (!g_SaveQueueEepromDirty && !g_SaveQueueConfigDirty) {
		g_SaveQueueDirtySinceFrame = g_SaveQueueFrame;
	}

	g_SaveQueueConfigDirty = 1;
}

s32 saveQueueIsDirty(void)
{
	return g_SaveQueueEepromDirty || g_SaveQueueConfigDirty;
}

u32 saveQueueFramesPending(void)
{
	if (!saveQueueIsDirty()) {
		return 0;
	}

	return g_SaveQueueFrame - g_SaveQueueDirtySinceFrame;
}

u32 saveQueueDeadlineFrames(void)
{
	return SAVEQUEUE_DEADLINE_FRAMES;
}

u32 saveQueueFlushCount(void)
{
	return g_SaveQueueFlushCount;
}

void saveQueueFlush(void)
{
	if (!saveQueueIsDirty()) {
		return;
	}

	// Clear first. A write that fails logs and is not retried, which is what
	// the eager path did too - osEeepromSave only ever logged and
	// osEepromLongWrite returned 0 regardless, so the pak layer has never been
	// able to see a disk error. Leaving the flag set would turn one failed
	// write into a retry on every frame for the rest of the session.
	const s32 eeprom = g_SaveQueueEepromDirty;
	const s32 config = g_SaveQueueConfigDirty;

	g_SaveQueueEepromDirty = 0;
	g_SaveQueueConfigDirty = 0;
	g_SaveQueueFlushCount++;

	// Profiles first: this is what produces the hashes the ini is about to be
	// written with, and the pak writes that mark the EEPROM below.
	mpProfileFlushDirty();

	if (eeprom || g_SaveQueueEepromDirty) {
		g_SaveQueueEepromDirty = 0;
		osEepromFlush();
	}

	if (config) {
		// The bind strings are what the ini entries point at, and they are
		// only rebuilt from the live binds on request: a bind changed since
		// boot is not in them, and inputLoadBinds' strtok has cut every one
		// down to its first key. Shutdown always did this; a flush did not,
		// so a crash after any queued save lost every second bind.
		inputSaveBinds();
		configSave(CONFIG_PATH);
	}
}

/*
 * Did this frame leave enough of its budget unspent to absorb a write?
 *
 * g_Vars.mininc60 is the frame-rate cap in 60ths; when it is zero the loop is
 * not pacing at all and there is no such thing as spare budget, so the
 * deadline is the only thing that can fire.
 */
static s32 saveQueueFrameWasIdle(void)
{
	s32 used;
	s32 budget;

	if (!g_Vars.mininc60) {
		return 0;
	}

	used = osGetCount() - g_Vars.thisframestartt;
	budget = g_Vars.mininc60 * CYCLES_PER_FRAME;

	if (used < 0 || budget <= 0) {
		return 0;
	}

	return used < budget / SAVEQUEUE_IDLE_DENOMINATOR * SAVEQUEUE_IDLE_NUMERATOR;
}

void saveQueueTick(void)
{
	g_SaveQueueFrame++;

	if (!saveQueueIsDirty()) {
		return;
	}

	if (saveQueueFrameWasIdle()
			|| g_SaveQueueFrame - g_SaveQueueDirtySinceFrame >= SAVEQUEUE_DEADLINE_FRAMES) {
		saveQueueFlush();
	}
}
