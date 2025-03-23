#ifndef _IN_MPSETUPS_H
#define _IN_MPSETUPS_H

#include "types.h"

s32 mpsetupLoadFile();
void mpsetupLoadSetup(s32 index);
s32 mpsetupSaveFile(const char *name, u8 overwrite);

#endif
