#include <ultra64.h>
#include "constants.h"
#include "data.h"
#include "types.h"
#include "bss.h"
#include "mod.h"
#include "system.h"
#include "texscope.h"

#define TEXSCOPE_MAX 8

static struct texscope g_TexScopes[TEXSCOPE_MAX];
static s32 g_TexScopeDepth = 0;

/* Read-only mirrors of the top; see texscope.h. */
s32 g_TexModNum = -1;
s32 g_TexCurrentModelFileNum = 0;

static void texScopeSync(void)
{
	if (g_TexScopeDepth > 0) {
		g_TexModNum = g_TexScopes[g_TexScopeDepth - 1].owner;
		g_TexCurrentModelFileNum = g_TexScopes[g_TexScopeDepth - 1].modelFileNum;
	} else {
		g_TexModNum = -1;
		g_TexCurrentModelFileNum = 0;
	}
}

void texScopePushPair(s32 owner, s32 modelFileNum)
{
	if (g_TexScopeDepth >= TEXSCOPE_MAX) {
		sysLogPrintf(LOG_ERROR, "texscope: push past %d deep (owner %d model %d); keeping the top",
				TEXSCOPE_MAX, owner, modelFileNum);
		return;
	}
	g_TexScopes[g_TexScopeDepth].owner = owner;
	g_TexScopes[g_TexScopeDepth].modelFileNum = modelFileNum;
	g_TexScopeDepth++;
	texScopeSync();
}

void texScopePushOwner(s32 owner)
{
	texScopePushPair(owner, 0);
}

// The owner tag answers for anything a modconfig named - a HeadsAndBodies
// entry, a ModelStates row, a gun file. It cannot answer for a prop: a
// prop's model arrives by model number through the static g_ModelStates
// table, a plain vanilla id unless a modconfig overwrote it, so without the
// fallback a mod's own prop wears a vanilla texture while the room around
// it is right. Her rule: a prop in a stage is the stage's unless tagged
// otherwise. So tag first, then the stage's claim, then nothing.
//
// Not g_ModNum: modSwitch only assigns it when the stage has a claim, so on
// an unclaimed stage it keeps the previous stage's mod and is stale-valid.
// modNumFromStage answers -1 for an unclaimed stage, which is the right
// answer. The stage bounds are the engine's own: STAGE_TITLE is what main.c
// and pdmain.c use to mean "a real level is loaded", and modconfig accepts
// a claim on 0x5c but refuses below 0x02 and at or above 0x5d.
void texScopePush(s32 fileid)
{
	s32 owner = MOD_FILEID_MOD(fileid);

	if (owner < 0) {
		const s32 stagenum = g_Vars.stagenum;

		if (stagenum > 0x01 && stagenum < STAGE_TITLE) {
			owner = modNumFromStage(stagenum);
		}
	}

	texScopePushPair(owner, MOD_FILEID_RAW(fileid));
}

void texScopePop(void)
{
	if (g_TexScopeDepth <= 0) {
		sysLogPrintf(LOG_ERROR, "texscope: pop with nothing pushed");
		return;
	}
	g_TexScopeDepth--;
	texScopeSync();
}

s32 texScopeOwner(void)
{
	return g_TexScopeDepth > 0 ? g_TexScopes[g_TexScopeDepth - 1].owner : -1;
}

s32 texScopeModelFileNum(void)
{
	return g_TexScopeDepth > 0 ? g_TexScopes[g_TexScopeDepth - 1].modelFileNum : 0;
}

s32 texScopeDepth(void)
{
	return g_TexScopeDepth;
}

void texScopeReportMissing(u16 texturenum)
{
	static u8 seen[8192]; // one bit per texture number below 0x10000
	const u32 byte = texturenum >> 3;
	const u8 bit = (u8)(1u << (texturenum & 7));

	if (seen[byte] & bit) {
		return;
	}
	seen[byte] |= bit;
	sysLogPrintf(LOG_WARNING, "texscope: texture 0x%04x past the ROM's table loaded with no scope; "
			"a loader forgot to push one", texturenum);
}
