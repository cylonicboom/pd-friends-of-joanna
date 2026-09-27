#ifndef _IN_GAME_TILES_H
#define _IN_GAME_TILES_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

void tilesReset(void);

#ifndef PLATFORM_N64
/* editor: tiles drawn as geometry. mode 0 off, 1 shaded per room, 2 solid */
Gfx *tilesRender(Gfx *gdl);
void tilesRenderSetMode(s32 mode);
s32 tilesRenderGetMode(void);
extern s32 g_TilesRenderAllRooms;
#endif

#endif
