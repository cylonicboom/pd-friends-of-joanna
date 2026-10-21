/**
 * Shims for the Kai subsystems fojo did not take with the netplay intake:
 * the `~` console, host-spectator panels and demo recording. Each is the
 * smallest thing that lets port/src/net compile; none does anything. If one
 * of these is ever ported for real, delete its stub here.
 */
#include <PR/ultratypes.h>
#include <string.h>
#include "types.h"
#include "console.h"
#include "spectator.h"
#include "net/demo.h"
#include "system.h"
#include "net/net.h"
#include "constants.h"
#include "mpsetups.h"

static char g_ConFilter[1] = "";
void conSetFilter(const char *f) { (void)f; }
const char *conGetFilter(void) { return g_ConFilter; }
void conSetOpaqueBg(s32 on) { (void)on; }
s32 conGetOpaqueBg(void) { return 0; }

s32 g_SpectatorPanelCount = 0;

void netDemoRecordTick(void) {}
void netDemoStop(void) {}
s32 netDemoIsRecording(void) { return 0; }
void netDemoPlayTick(void) {}
s32 netDemoRenderBegin(void) { return 0; }
void netDemoRenderEnd(void) {}
s32 netDemoIsPlaying(void) { return 0; }
struct chrdata *netDemoFollowedChr(void) { return NULL; }
s32 netDemoConsoleCommand(const char *cmd, const char *arg) { (void)cmd; (void)arg; return 0; }

// Weapon presets / per-slot fn flags (Kai's PORT_WEAPON_PRESETS): no store here.
u8 g_MpWeaponPresetCount = 0;
struct mpweaponpreset g_MpWeaponPresets[MPWEAPONPRESET_MAXENTRIES];
u8 g_MpSlotFnFlags[NUM_MPWEAPONSLOTS];
s32 mpWeaponPresetFind(const char *name) { (void)name; return -1; }
s32 mpWeaponPresetAdd(const char *name, const u8 *weapons, const u8 *slotfnflags) { (void)name; (void)weapons; (void)slotfnflags; return -1; }
void mpWeaponPresetReplace(s32 idx, const u8 *weapons, const u8 *slotfnflags) { (void)idx; (void)weapons; (void)slotfnflags; }

// Killcam (netkillcam.c.kai): not ported. The state struct is read by the
// interpolation path, so it exists and stays inactive.
struct netkillcamstate g_NetKillcam;
void netKillcamReset(void) {}
void netKillcamRecordTick(void) {}
void netKillcamNoteKill(struct chrdata *killer, struct chrdata *victim) { (void)killer; (void)victim; }
s32 netKillcamGetCamera(struct coord *eye, struct coord *look, struct coord *up, s32 *room) { (void)eye; (void)look; (void)up; (void)room; return 0; }
struct chrdata *netKillcamFindChrByName(const char *name) { (void)name; return NULL; }

// Kai's std-menu lobby (netmenu.c.kai): the two entry points net.c reaches
// for. fojo's lobby will be a fojOS window; until then these log and return.
void netHostOnlineEnterSetup(void) { sysLogPrintf(LOG_WARNING, "NET: host-online setup menu not ported"); }
void netAdminConfigure(void) { sysLogPrintf(LOG_WARNING, "NET: admin configure menu not ported"); }

// Chaos external events (Kai PORT_CHAOS): the UDP/console ingress has nowhere
// to go here.
void luaExtEventPush(const char *source, const char *text) { (void)source; (void)text; }

// Combat Sim scenario wire state (carry/HTM/PAC/CTC dirty flags): the
// SVC_*_STATE writers are stubbed in netmsg.c; the flags exist so net.c's
// heartbeat block compiles. Porting the scenario sync is its own chunk.
u8 g_MpCarryDirty = 0;
u8 g_MpHtmDirty = 0;
u8 g_MpPacDirty = 0;
u8 g_MpPaintDirty = 0;
u8 g_MpZonesDirty = 0;
u8 g_MpElimDirty = 0;
u8 g_MpRaceDirty = 0;

// lv.c split-pass skip mask (Kai's per-pass render skip for discarded remote
// viewports): the /lvsplit knob is cut, the variable stays so net.c links.
s32 g_LvSplitMask = 0;
