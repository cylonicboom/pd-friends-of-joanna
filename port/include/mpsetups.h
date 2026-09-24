#ifndef _IN_MPSETUPS_H
#define _IN_MPSETUPS_H

#include "types.h"

s32 mpsetupLoadCurrentFile(void);
s32 mpsetupSaveCurrentFile(void);
void mpsetupLoadSetup(s32 slotindex);
s32 mpsetupSaveSetup(s32 slotindex, u8 savefile);
void mpsetupCopyAllFromPak(void);
// --mpsetup-probe: load the setup file after the mods are mounted, apply every
// setup so its names resolve (or refuse) in the log, and exit. The headless
// way to see what a saved setup would load on this install.
void mpsetupProbeFromArgs(void);

#endif
