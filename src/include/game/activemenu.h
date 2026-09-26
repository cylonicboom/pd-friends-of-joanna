#ifndef IN_GAME_ACTIVEMENU_H
#define IN_GAME_ACTIVEMENU_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

void amTick(void);

void amOpenPickTarget(void);
MenuDialogHandlerResult menudialog000fcd48(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data);
MenuDialogHandlerResult amPickTargetMenuDialog(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data);
void amSetAiBuddyTemperament(bool aggressive);
void amSetAiBuddyStealth(void);
s32 amGetFirstBuddyIndex(void);
void amApply(s32 slot);
// Longest label amGetSlotDetails will write, and the size every caller
// except the slot-focus block in activemenutick.c already used.
#define AM_SLOT_LABEL_LEN 32

void amGetSlotDetails(s32 slot, u32 *flags, char *label, s32 len);
void amReset(void);
void amResetSlot(s32 i);
void amUpdateFontForPlayerCount(void);
s16 amCalculateSlotWidth(void);
void amChangeScreen(s32 step);
void amAssignWeaponSlots(void);
void amOpen(void);
void amClose(void);
bool amIsCramped(void);
void amCalculateSlotPosition(s16 column, s16 row, s16 *x, s16 *y);
Gfx *amRenderText(Gfx *gdl, char *text, u32 colour, s16 left, s16 top);
Gfx *amRenderAibotInfo(Gfx *gdl, s32 buddynum);
Gfx *amRenderSlot(Gfx *gdl, char *text, s16 x, s16 y, s32 mode, s32 flags);
Gfx *amRender(Gfx *gdl);

#endif
