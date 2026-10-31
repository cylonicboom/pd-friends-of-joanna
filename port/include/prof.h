#ifndef _IN_PROF_H
#define _IN_PROF_H

#include <ultra64.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Zone profiler (port only). Per-zone, per-seat time and counts inside the
 * frame, so "what does the server do N times for N seats" is answered by a
 * table and not by reading lvRender.
 *
 * - PROF_BEGIN(z) / PROF_END() bracket a zone; nest freely (depth 32). END
 *   needs no name: a stack remembers it, and subtracts the child time so a
 *   cell carries both inclusive and self ticks.
 * - PROF_COUNT(z, n) adds n to a zone's count without timing anything
 *   (props ticked, opcodes run, display-list words discarded, ...).
 * - A zone is PERSEAT or not. A per-seat zone files under
 *   g_Vars.currentplayernum; the rest under the "none" column.
 *
 * Off (g_ProfOn == 0) every macro is one load and one branch. On, an edge is
 * one monotonic clock read and a few stores. The live [zone][seat] table of
 * the last complete frame feeds the fojOS Profiler panel and the netdiag
 * `prof` line. --profdump <path> additionally streams every edge as a
 * Fuchsia Trace Format (.fxt) event (ui.perfetto.dev opens it): fixed-size
 * records, 1 MiB buffer, fflush once a second and from the crash handler.
 *
 * src/lib/profile.c (the 8-slot N64 frame profiler) stays as it is; this is
 * the inside of its MAINTICK span.
 */

#define PROF_SEATS     8                 // MAX_PLAYERS may grow to 5; keep headroom
#define PROF_SEAT_NONE (PROF_SEATS - 1)  // zones outside any player loop

enum profzone {
	// frame level
	PROF_Z_FRAME_TICK,      // lvTick + playermgrShuffle (pdmain)
	PROF_Z_NET_START,       // netStartFrame
	PROF_Z_NET_END,         // netEndFrame
	PROF_Z_LVRENDER,        // lvRender, whole
	PROF_Z_GFXSWAP,         // gfxSwapBuffers (pool swap, not the drawing)
	PROF_Z_GFX_STARTFRAME,  // videoStartFrame: gfx_start_frame (where a vsync wait may land)
	PROF_Z_GFX_RUN,         // videoSubmitCommands: fast3d walks the display list into GL
	PROF_Z_GFX_ENDFRAME,    // videoEndFrame: present / vsync wait
	PROF_Z_PROF_DUMP,       // the dump's own cost (write + flush)
	// per seat: the tick side
	PROF_Z_TICKPLAYER,      // lvTickPlayer
	// per seat: the gameplay-tier calls that live inside lvRender's loop
	PROF_Z_BGTICK,          // bgTick (portal flood -> ROOMFLAG_ONSCREEN)
	PROF_Z_LIGHTSTICK,      // lightsTick
	PROF_Z_PROPSTICK,       // propsTickPlayer; count = props ticked
	PROF_Z_SCENARIOTICK,    // scenarioTickChr
	PROF_Z_PROPSSORT,       // propsSort; count = onscreen props
	PROF_Z_AUTOAIM,         // autoaimTick
	PROF_Z_HANDSATTACK,     // handsTickAttack
	PROF_Z_LOOKINGAT,       // lookingatprop + lvFindThreats + tracked props
	PROF_Z_INTERACT,        // currentPlayerInteract + propsTestForPickup
	// per seat: drawing
	PROF_Z_BGRENDER,        // bgRender (rooms + props)
	PROF_Z_BEAMS,           // propsRenderBeams
	PROF_Z_HUD,             // playerRenderHud (carries the death state machine too)
	PROF_Z_GDL_EMITTED,     // count: display-list words this seat built
	PROF_Z_GDL_DISCARDED,   // count: words rolled back (viewdiscarded)
	// AI
	PROF_Z_AI_EXEC,         // chraiExecute; count = ailist opcodes run
	PROF_Z_AI_INTERP,       // netChrInterpolate (client side)
	// net, counts only
	PROF_Z_NET_OUT,         // count: packet bytes sent
	PROF_Z_NET_IN,          // count: packet bytes received (sum of messages)

	PROF_ZONE_COUNT
};

struct profcell {
	u64 incl;   // ticks, inclusive of children
	u64 self;   // ticks, children subtracted
	u32 calls;
	u32 count;
};

extern s32 g_ProfOn;

void profInit(void);              // CLI + config; opens --profdump
void profShutdown(void);          // closes the dump
void profFrameEnd(u32 frame);     // commits the live table, writes the FRAME record, flushes once a second
void profCrashFlush(void);        // from the crash handler: get what we have to disk

void profZoneBegin(s32 zone);
void profZoneEnd(void);
void profZoneCount(s32 zone, u32 n);
void profNetMsg(s32 in, u32 msgid, u32 bytes);  // per-message-class byte counters (in: 1 = received)
void profMark(const char *text);                // an instant event on the frame track: /mark from the console
void profNetSessionBegin(const char *role);     // Prof.OnNet: open a dated dump when a net session starts
void profNetSessionEnd(void);                   // ... and close it at disconnect

const char *profZoneName(s32 zone);
s32 profZoneIsPerSeat(s32 zone);
const struct profcell *profLastFrame(void);   // [PROF_ZONE_COUNT][PROF_SEATS], last complete frame
u64 profTicksPerSecond(void);
const char *profDumpPath(void);               // NULL when not dumping

#define PROF_BEGIN(z)    do { if (g_ProfOn) profZoneBegin(z); } while (0)
#define PROF_END()       do { if (g_ProfOn) profZoneEnd(); } while (0)
#define PROF_COUNT(z, n) do { if (g_ProfOn) profZoneCount((z), (u32)(n)); } while (0)
#define PROF_NETMSG(in, id, bytes) do { if (g_ProfOn) profNetMsg((in), (id), (bytes)); } while (0)

#ifdef __cplusplus
}
#endif

#endif
