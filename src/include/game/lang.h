#ifndef IN_GAME_LANG_H
#define IN_GAME_LANG_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

extern u8 *g_LangBuffer;

void langInit(void);
void langReset(s32 stagenum);
void langTick(void);

u32 langGetLangBankIndexFromStagenum(s32 stagenum);
struct jpncharpixels *langGetJpnCharPixels(s32 codepoint);
s32 langGetFileNumOffset(void);
s32 langGetFileId(s32 bank);
void langLoad(s32 bank);
void langLoadToAddr(s32 bank, u8 *dst, s32 size);
void langClearBank(s32 bank);
char *langGet(s32 textid);
void langReload(void);
void langSetEuropean(u32 arg0);
void langSetJpnEnabled(bool enable);

#ifndef PLATFORM_N64
/* Kai (be46717): the chaos text gags (g_ChaosUwuMode) for text that does not
 * flow through langGet at render time (hudmsgs, Lua overlays). Returns src
 * unchanged when the mode is off. */
char *langChaosTransform(char *src);

/*
 * A modconfig's `langbank <NAME>` for a stage: the bank that stage loads
 * instead of whatever langGetLangBankIndexFromStagenum's switch says for its
 * row. NAME is the LANGBANK_ suffix (AREC, MP13, REF ...). Only existing banks:
 * g_LangBanks has 69 slots and text ids bake the bank index in, so a new bank
 * is a table change, not a config one.
 */
s32 langGetBankByName(const char *name);
void langSetStageBank(s32 stagenum, s32 bank);
void langClearStageBank(s32 stagenum);
#endif

#endif
