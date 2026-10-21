#ifndef _IN_CONSOLE_H
#define _IN_CONSOLE_H
// Kai's `~` console is not ported (fojo's command surface is fojOS / the
// awesome bar). This header keeps the net code's few console calls
// compiling; the bodies in port/src/net/netshims.c are no-ops.
#include <PR/ultratypes.h>
#include "types.h"
void conSetFilter(const char *f);
const char *conGetFilter(void);
void conSetOpaqueBg(s32 on);
s32 conGetOpaqueBg(void);
#endif
