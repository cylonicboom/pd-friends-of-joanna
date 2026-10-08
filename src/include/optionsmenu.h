#ifndef _OPTIONSMENU_H
#define _OPTIONSMENU_H
MenuItemHandlerResult optionsmenuhandlerController(s32 operation, struct menuitem *item, union handlerdata *data);
MenuItemHandlerResult optionsmenuhandlerControllerMpPlayer(s32 operation, struct menuitem *item, union handlerdata *data);

#ifdef __cplusplus
extern "C" {
#endif

void optionsmenuSetExtPlayer(s32 player);
s32 optionsmenuGetExtPlayer(void);
s32 optionsmenuGetNumBinds(void);
u32 optionsmenuGetBindCk(s32 idx);
const char *optionsmenuGetBindName(s32 idx);

extern struct menudialogdef g_ExtendedVideoMenuDialog;
extern struct menudialogdef g_ExtendedAudioMenuDialog;
extern struct menudialogdef g_ExtendedMouseMenuDialog;
extern struct menudialogdef g_ExtendedControllerMenuDialog;
extern struct menudialogdef g_ExtendedGameMenuDialog;
extern struct menudialogdef g_ExtendedBindsMenuDialog;

#ifdef __cplusplus
}
#endif

#endif
