#ifndef _IN_MPSETUPS_H
#define _IN_MPSETUPS_H

#include "types.h"

s32 mpsetupLoadCurrentFile(void);
s32 mpsetupSaveCurrentFile(void);
void mpsetupLoadSetup(s32 slotindex);
s32 mpsetupSaveSetup(s32 slotindex, u8 savefile);
void mpsetupCopyAllFromPak(void);
// --mpsetup-probe: load the setup file after the mods are mounted, apply every
// setup so its names resolve (or refuse) in the log, and exit. The headless
// way to see what a saved setup would load on this install.
void mpsetupProbeFromArgs(void);

// netplay (Kai be46717): Kai's saved Combat Sim weapon presets and the per-slot
// function-mode flags. NOT ported — fojo has no preset store or FNFLAG gates.
// The declarations stay so the net layer (SVC_STAGE_START ships slotfnflags;
// the playlist can name a preset) compiles; port/src/net/netshims.c provides
// an empty preset table, zero fn flags and lookups that find nothing.
#define MPWEAPONPRESET_MAXNAME    17
#define MPWEAPONPRESET_MAXENTRIES 32
#define FNFLAG_PRIMARY_DISABLED   0x01
#define FNFLAG_SECONDARY_DISABLED 0x02
struct mpweaponpreset {
	char name[MPWEAPONPRESET_MAXNAME + 1];
	u8 weapons[NUM_MPWEAPONSLOTS];
	u8 slotfnflags[NUM_MPWEAPONSLOTS];
};
extern u8 g_MpWeaponPresetCount;
extern struct mpweaponpreset g_MpWeaponPresets[MPWEAPONPRESET_MAXENTRIES];
extern u8 g_MpSlotFnFlags[NUM_MPWEAPONSLOTS];
s32 mpWeaponPresetFind(const char *name);
s32 mpWeaponPresetAdd(const char *name, const u8 *weapons, const u8 *slotfnflags);
void mpWeaponPresetReplace(s32 idx, const u8 *weapons, const u8 *slotfnflags);

#endif
