#ifndef _IN_GAME_MODSPECTATE_H
#define _IN_GAME_MODSPECTATE_H

#include <ultra64.h>
#include "types.h"

extern f32 g_ModSpectateSpeed;
extern s32 g_ModSpectateStart;

bool modSpectateIsOnForPlayer(s32 playernum);
bool modSpectateIsOn(void);
void modSpectateSetOn(bool on);
void modSpectateToggle(void);
s32 modSpectateGetBodyNum(void);
bool modSpectateBodyIsStale(void);
bool modSpectateTakeBodyStale(void);
void modSpectateApplyStart(void);
void modSpectateTick(void);
void modSpectateReset(void);
void modSpectateTeleport(const struct coord *pos, bool eye);
bool modSpectateTeleportNear(struct prop *target);

#endif
