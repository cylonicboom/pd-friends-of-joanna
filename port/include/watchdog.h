#ifndef _IN_WATCHDOG_H
#define _IN_WATCHDOG_H
#include <PR/ultratypes.h>

// --watchdog <secs>: bumped once per mainTick; a side thread trips a SIGSEGV
// backtrace if it stops moving. Off unless the flag is given.
extern volatile u32 g_WatchdogFrame;
void watchdogInit(void);

#endif
