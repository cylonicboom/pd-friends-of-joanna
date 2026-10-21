#ifndef _IN_SPECTATOR_H
#define _IN_SPECTATOR_H
// Kai's host-spectator panels (port/src/spectator.c) are not ported; the one
// global the net layer writes lives in port/src/net/netshims.c and stays 0.
#include <PR/ultratypes.h>
extern s32 g_SpectatorPanelCount;
#endif
