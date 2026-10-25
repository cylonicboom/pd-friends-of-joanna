#ifndef _IN_NAMETRACE_H
#define _IN_NAMETRACE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NAMETRACE - temporary. The bug where a hard close eats a profile's name.
 *
 * Every line starts "NAMETRACE" so `grep NAMETRACE pd.log` is the whole story.
 * Names print as 'text' with \n made visible, then the raw bytes.
 */

// set by main.c's cleanup / the signal handler before the exit flush
extern s32 g_NameTraceExiting;

// one line about a seat: what it is called right now and which file it points at
void nameTraceSeat(const char *what, s32 seat);

// one line about raw wad bytes (the name field is the first 10)
void nameTraceBytes(const char *what, s32 seat, const u8 *bytes, s32 fileid, u16 serial, s32 extra);

// a free-form line, prefixed
void nameTracef(const char *fmt, ...);

// every frame: log any seat whose name or file guid changed since last frame,
// with the address of nobody in particular - the frame number is the clue
void nameTraceWatch(void);

// every seat at once
void nameTraceDumpSeats(const char *why);

#ifdef __cplusplus
}
#endif

#endif
