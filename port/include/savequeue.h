#ifndef _IN_SAVEQUEUE_H
#define _IN_SAVEQUEUE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Deferred save flushing.
 *
 * The port inherited the N64 block-write model on top of a file-backed
 * EEPROM: pakGetBlockSize is 16 bytes and every block write used to call
 * osEeepromSave, which is a whole fopen/fwrite(2048)/fclose of eeprom.bin.
 * One MP profile save is about seven of those back to back, a game file
 * twelve to fourteen, all synchronous on the only thread there is - the same
 * thread that pushes audio with SDL_QueueAudio, so a save could underrun the
 * audio queue rather than merely drop a frame.
 *
 * Writes are now marked instead of performed, and flushed once, later, at a
 * point where the frame had budget to spare. A whole multi-block save
 * collapses into a single write.
 *
 * This is safe because the port's EEPROM is already a RAM shadow that
 * osEepromLongWrite fills before saving, and every pak read comes back out of
 * that shadow - see port/src/libultra.c. Nothing in pak.c can tell that the
 * file lagged behind. It is also strictly SAFER than writing eagerly: the
 * two-pass header protocol in pakWriteFileAtOffset deliberately puts a
 * writecompleted=0 header on disk and then corrects it, so eager writing
 * exposes that inconsistent state to a crash. One flush at a quiescent point
 * never does.
 *
 * What it costs is durability on a hard kill. Eager writing meant eeprom.bin
 * was always current; now it is current as of the last flush.
 */

// A write happened. Cheap, call freely - it only stamps a flag.
void saveQueueMarkEeprom(void);
void saveQueueMarkConfig(void);

// Once per frame, after the frame has been presented. Decides whether to
// flush now.
void saveQueueTick(void);

// Flush anything pending right now, wherever we are. For shutdown, stage
// teardown, and the overlay's flush button.
void saveQueueFlush(void);

// Readouts for the Saves panel.
// Nonzero when something is waiting to be written. s32 rather than bool:
// this header is included before the tree gets round to defining one.
s32 saveQueueIsDirty(void);
u32 saveQueueFramesPending(void);
u32 saveQueueDeadlineFrames(void);
u32 saveQueueFlushCount(void);

#ifdef __cplusplus
}
#endif

#endif
