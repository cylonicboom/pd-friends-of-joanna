#ifndef IN_GAME_GAMEFILE_H
#define IN_GAME_GAMEFILE_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

u32 gamefileHasFlag(u32 value);
void gamefileSetFlag(u32 value);
void gamefileUnsetFlag(u32 value);
void gamefilePrintFlags(void);
void gamefileApplyOptions(struct gamefile *file);
// Whether a mission's list position is one the game file can hold. besttimes
// is [NUM_SOLOSTAGES][3] and coopcompletions is a NUM_SOLOSTAGES-bit mask,
// both saved positionally into a body that has three spare bits, so an index
// past the vanilla rows does not overflow into a bigger file - it scribbles
// on firingrangescores and weaponsfound and reads back as garbage. Every
// write keyed by stageindex goes through this; a mod mission's progress
// lives in pd.ini instead (see the sidecar in solo-sidecar-plan.md).
bool gamefileSoloIndexOk(s32 stageindex);
void gamefileLoadDefaults(struct gamefile *file);
s32 gamefileLoad(s32 device);
s32 gamefileSave(s32 device, s32 filenum, u16 deviceserial);
void gamefileGetOverview(char *arg0, char *name, u8 *stage, u8 *difficulty, u32 *time);
void gamefileUnlockEverything(void);

#endif
