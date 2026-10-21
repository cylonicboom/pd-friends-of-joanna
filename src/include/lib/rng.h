#ifndef _IN_LIB_RNG_H
#define _IN_LIB_RNG_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

u32 rngRandom(void);
u32 rngRotateSeed(u64 *value);

#ifndef PLATFORM_N64
// Cosmetic stream (src/game/rngcosmetic_c.c, from Kai): purely visual effects
// draw from this so they never advance the gameplay seed netplay syncs.
u32 rngCosmeticRandom(void);
void rngCosmeticSetSeed(u64 seed);
#endif

#endif
