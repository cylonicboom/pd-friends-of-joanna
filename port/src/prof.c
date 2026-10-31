/*
 * Zone profiler: per-zone, per-seat ticks and counts inside the frame, with
 * an optional Fuchsia Trace Format (.fxt) event stream on disk. See prof.h
 * for the model. Format reference:
 * https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format
 *
 * Why FXT: a duration begin/end is two 64-bit words (header + timestamp)
 * when the thread and the name are table references, counters are three
 * words plus one argument, and every record is self-delimiting from its
 * header word. That is the same byte count as a home-grown format with no
 * encoding step, and ui.perfetto.dev opens the file as it is. Seats are
 * modelled as threads so each seat gets its own track.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L // clock_gettime under -std=c11
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ultra64.h>
#include "platform.h"
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "system.h"
#include "config.h"
#include "prof.h"

#ifdef PLATFORM_WIN32
#include <windows.h>
#endif

s32 g_ProfOn = 0;

// ---- zones --------------------------------------------------------------

struct profzonedef {
	const char *name;
	s32 perseat;
};

static const struct profzonedef g_ProfZones[PROF_ZONE_COUNT] = {
	[PROF_Z_FRAME_TICK]    = { "tick",            0 },
	[PROF_Z_NET_START]     = { "net.startframe",  0 },
	[PROF_Z_NET_END]       = { "net.endframe",    0 },
	[PROF_Z_LVRENDER]      = { "lvrender",        0 },
	[PROF_Z_GFXSWAP]       = { "gfxswap",         0 },
	[PROF_Z_GFX_RUN]       = { "gfx.run",         0 },
	[PROF_Z_GFX_ENDFRAME]  = { "gfx.endframe",    0 },
	[PROF_Z_PROF_DUMP]     = { "prof.dump",       0 },
	[PROF_Z_TICKPLAYER]    = { "tickplayer",      1 },
	[PROF_Z_BGTICK]        = { "bgtick",          1 },
	[PROF_Z_LIGHTSTICK]    = { "lightstick",      1 },
	[PROF_Z_PROPSTICK]     = { "propstick",       1 },
	[PROF_Z_SCENARIOTICK]  = { "scenariotick",    1 },
	[PROF_Z_PROPSSORT]     = { "propssort",       1 },
	[PROF_Z_AUTOAIM]       = { "autoaim",         1 },
	[PROF_Z_HANDSATTACK]   = { "handsattack",     1 },
	[PROF_Z_LOOKINGAT]     = { "lookingat",       1 },
	[PROF_Z_INTERACT]      = { "interact",        1 },
	[PROF_Z_BGRENDER]      = { "bgrender",        1 },
	[PROF_Z_BEAMS]         = { "beams",           1 },
	[PROF_Z_HUD]           = { "hud",             1 },
	[PROF_Z_GDL_EMITTED]   = { "gdl.emitted",     1 },
	[PROF_Z_GDL_DISCARDED] = { "gdl.discarded",   1 },
	[PROF_Z_AI_EXEC]       = { "ai.exec",         1 },
	[PROF_Z_AI_INTERP]     = { "ai.interp",       1 },
	[PROF_Z_NET_OUT]       = { "net.out",         0 },
	[PROF_Z_NET_IN]        = { "net.in",          0 },
};

const char *profZoneName(s32 zone)
{
	return (zone >= 0 && zone < PROF_ZONE_COUNT) ? g_ProfZones[zone].name : "?";
}

s32 profZoneIsPerSeat(s32 zone)
{
	return (zone >= 0 && zone < PROF_ZONE_COUNT) ? g_ProfZones[zone].perseat : 0;
}

// ---- clock --------------------------------------------------------------

static u64 g_ProfTicksPerSec = 1000000000ull;

static inline u64 profNow(void)
{
#ifdef PLATFORM_WIN32
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (u64)c.QuadPart;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
#endif
}

u64 profTicksPerSecond(void)
{
	return g_ProfTicksPerSec;
}

// ---- live table ---------------------------------------------------------

static struct profcell g_ProfCur[PROF_ZONE_COUNT][PROF_SEATS];
static struct profcell g_ProfLast[PROF_ZONE_COUNT][PROF_SEATS];

#define PROF_STACK_MAX 32

struct profframe {
	s32 zone;
	s32 seat;
	u64 t0;
	u64 child;   // ticks spent in nested zones, for self time
};

static struct profframe g_ProfStack[PROF_STACK_MAX];
static s32 g_ProfDepth = 0;
static s32 g_ProfOverflow = 0;

static inline s32 profSeatFor(s32 zone)
{
	if (!g_ProfZones[zone].perseat) {
		return PROF_SEAT_NONE;
	}
	s32 seat = g_Vars.currentplayernum;
	if (seat < 0 || seat >= PROF_SEAT_NONE) {
		seat = PROF_SEAT_NONE;
	}
	return seat;
}

const struct profcell *profLastFrame(void)
{
	return &g_ProfLast[0][0];
}

// ---- fxt writer ---------------------------------------------------------

#define FXT_BUF_WORDS (131072)                 // 1 MiB
#define FXT_FLUSH_AT  (FXT_BUF_WORDS - 512)   // leave room for one more record of any size we write

static FILE *g_FxtFile = NULL;
static char g_FxtPath[1024] = "";
static u64 *g_FxtBuf = NULL;
static u32 g_FxtWp = 0;
static u64 g_FxtLastFlush = 0;
static u64 g_FxtBytesWritten = 0;

// record types
#define FXT_REC_METADATA 0
#define FXT_REC_INIT     1
#define FXT_REC_STRING   2
#define FXT_REC_THREAD   3
#define FXT_REC_EVENT    4
#define FXT_REC_KOBJ     7

// event types
#define FXT_EV_INSTANT   0
#define FXT_EV_COUNTER   1
#define FXT_EV_BEGIN     2
#define FXT_EV_END       3

// argument types
#define FXT_ARG_UINT32   2
#define FXT_ARG_UINT64   4
#define FXT_ARG_STRING   6
#define FXT_ARG_KOID     8

// zircon object types (zircon/syscalls/types.h)
#define ZX_OBJ_TYPE_PROCESS 1
#define ZX_OBJ_TYPE_THREAD  2

// string table layout: 1 = category, 2.. = zone names, then the extras
#define FXT_STR_CAT      1
#define FXT_STR_ZONE(z)  (2 + (z))
#define FXT_STR_FRAME    (2 + PROF_ZONE_COUNT + 0)
#define FXT_STR_VALUE    (2 + PROF_ZONE_COUNT + 1)
#define FXT_STR_NETIN    (2 + PROF_ZONE_COUNT + 2)
#define FXT_STR_NETOUT   (2 + PROF_ZONE_COUNT + 3)
#define FXT_STR_PROCESS  (2 + PROF_ZONE_COUNT + 4)
#define FXT_STR_MARK     (2 + PROF_ZONE_COUNT + 5)
#define FXT_STR_SEAT(s)  (2 + PROF_ZONE_COUNT + 6 + (s))
#define FXT_STR_COUNT    (2 + PROF_ZONE_COUNT + 6 + PROF_SEATS)

// thread table: seat s -> ref s + 1 (0 would mean inline)
#define FXT_PROCESS_KOID 1ull
#define FXT_THREAD_KOID(s) (0x100ull + (u64)(s))
#define FXT_THREAD_REF(s)  ((u64)(s) + 1)

static void fxtFlush(void)
{
	if (!g_FxtFile || g_FxtWp == 0) {
		return;
	}
	fwrite(g_FxtBuf, sizeof(u64), g_FxtWp, g_FxtFile);
	g_FxtBytesWritten += (u64)g_FxtWp * sizeof(u64);
	g_FxtWp = 0;
}

static inline void fxtWord(u64 w)
{
	g_FxtBuf[g_FxtWp++] = w;
}

static inline void fxtReserve(void)
{
	if (g_FxtWp >= FXT_FLUSH_AT) {
		fxtFlush();
	}
}

static inline u64 fxtHeader(u64 type, u64 words, u64 typebits)
{
	return type | (words << 4) | typebits;
}

// Event header: [16..19] event type, [20..23] nargs, [24..31] thread ref,
// [32..47] category ref, [48..63] name ref.
static inline u64 fxtEventHeader(u64 words, u64 evtype, u64 nargs, u64 threadref, u64 nameref)
{
	return fxtHeader(FXT_REC_EVENT, words,
			(evtype << 16) | (nargs << 20) | (threadref << 24) | ((u64)FXT_STR_CAT << 32) | (nameref << 48));
}

static void fxtString(u32 index, const char *s)
{
	size_t len = strlen(s);
	if (len > 0x7fff) {
		len = 0x7fff;
	}
	u32 words = 1 + (u32)((len + 7) / 8);
	fxtReserve();
	fxtWord(fxtHeader(FXT_REC_STRING, words, ((u64)index << 16) | ((u64)len << 32)));
	u64 *dst = &g_FxtBuf[g_FxtWp];
	memset(dst, 0, (words - 1) * sizeof(u64));
	memcpy(dst, s, len);
	g_FxtWp += words - 1;
}

static void fxtThread(u32 index, u64 pkoid, u64 tkoid)
{
	fxtReserve();
	fxtWord(fxtHeader(FXT_REC_THREAD, 3, (u64)index << 16));
	fxtWord(pkoid);
	fxtWord(tkoid);
}

// Kernel object record: [16..23] object type, [24..39] name ref, [40..43] nargs.
static void fxtKobj(u64 objtype, u64 koid, u32 nameref, u64 processkoid)
{
	fxtReserve();
	if (objtype == ZX_OBJ_TYPE_THREAD) {
		fxtWord(fxtHeader(FXT_REC_KOBJ, 4, (objtype << 16) | ((u64)nameref << 24) | (1ull << 40)));
		fxtWord(koid);
		// "process" koid argument: header {type koid, 2 words, name ref} + koid word
		fxtWord((u64)FXT_ARG_KOID | (2ull << 4) | ((u64)FXT_STR_PROCESS << 16));
		fxtWord(processkoid);
	} else {
		fxtWord(fxtHeader(FXT_REC_KOBJ, 2, (objtype << 16) | ((u64)nameref << 24)));
		fxtWord(koid);
	}
}

static void fxtWriteHeader(void)
{
	// magic + initialization (ticks per second)
	fxtWord(0x0016547846040010ull);
	fxtWord(fxtHeader(FXT_REC_INIT, 2, 0));
	fxtWord(g_ProfTicksPerSec);

	fxtString(FXT_STR_CAT, "pd");
	for (s32 z = 0; z < PROF_ZONE_COUNT; z++) {
		fxtString(FXT_STR_ZONE(z), g_ProfZones[z].name);
	}
	fxtString(FXT_STR_FRAME, "frame");
	fxtString(FXT_STR_VALUE, "value");
	fxtString(FXT_STR_NETIN, "net.in.msg");
	fxtString(FXT_STR_NETOUT, "net.out.pkt");
	fxtString(FXT_STR_PROCESS, "process");
	fxtString(FXT_STR_MARK, "mark");

	char pname[128];
#ifdef VERSION_HASH
	snprintf(pname, sizeof(pname), "pd " VERSION_BRANCH " " VERSION_HASH);
#else
	snprintf(pname, sizeof(pname), "pd");
#endif
	fxtString(FXT_STR_COUNT, pname);
	fxtKobj(ZX_OBJ_TYPE_PROCESS, FXT_PROCESS_KOID, FXT_STR_COUNT, 0);

	for (s32 s = 0; s < PROF_SEATS; s++) {
		char tname[32];
		if (s == PROF_SEAT_NONE) {
			snprintf(tname, sizeof(tname), "frame");
		} else {
			snprintf(tname, sizeof(tname), "seat %d", s);
		}
		fxtString(FXT_STR_SEAT(s), tname);
		fxtThread((u32)FXT_THREAD_REF(s), FXT_PROCESS_KOID, FXT_THREAD_KOID(s));
		fxtKobj(ZX_OBJ_TYPE_THREAD, FXT_THREAD_KOID(s), FXT_STR_SEAT(s), FXT_PROCESS_KOID);
	}
}

static inline void fxtDuration(u64 evtype, s32 zone, s32 seat, u64 ts)
{
	fxtReserve();
	fxtWord(fxtEventHeader(2, evtype, 0, FXT_THREAD_REF(seat), FXT_STR_ZONE(zone)));
	fxtWord(ts);
}

// counter: header, ts, one uint32/uint64 arg named "value", counter id word
static inline void fxtCounter(u32 nameref, s32 seat, u64 ts, u64 value, u64 counterid)
{
	fxtReserve();
	if (value <= 0xffffffffull) {
		fxtWord(fxtEventHeader(4, FXT_EV_COUNTER, 1, FXT_THREAD_REF(seat), nameref));
		fxtWord(ts);
		fxtWord((u64)FXT_ARG_UINT32 | (1ull << 4) | ((u64)FXT_STR_VALUE << 16) | (value << 32));
	} else {
		fxtWord(fxtEventHeader(5, FXT_EV_COUNTER, 1, FXT_THREAD_REF(seat), nameref));
		fxtWord(ts);
		fxtWord((u64)FXT_ARG_UINT64 | (2ull << 4) | ((u64)FXT_STR_VALUE << 16));
		fxtWord(value);
	}
	fxtWord(counterid);
}

// instant "frame" event on the frame track with the frame number as its arg:
// the resync point for a reader walking a truncated file
static inline void fxtFrameMark(u64 ts, u32 frame)
{
	fxtReserve();
	fxtWord(fxtEventHeader(3, FXT_EV_INSTANT, 1, FXT_THREAD_REF(PROF_SEAT_NONE), FXT_STR_FRAME));
	fxtWord(ts);
	fxtWord((u64)FXT_ARG_UINT32 | (1ull << 4) | ((u64)FXT_STR_FRAME << 16) | ((u64)frame << 32));
}

static void fxtOpen(const char *path)
{
	g_FxtBuf = (u64 *)malloc(FXT_BUF_WORDS * sizeof(u64));
	if (!g_FxtBuf) {
		sysLogPrintf(LOG_ERROR, "prof: no memory for the dump buffer");
		return;
	}
	g_FxtFile = fopen(path, "wb");
	if (!g_FxtFile) {
		sysLogPrintf(LOG_ERROR, "prof: could not open %s", path);
		free(g_FxtBuf);
		g_FxtBuf = NULL;
		return;
	}
	strncpy(g_FxtPath, path, sizeof(g_FxtPath) - 1);
	g_FxtWp = 0;
	fxtWriteHeader();
	g_FxtLastFlush = profNow();
	sysLogPrintf(LOG_NOTE, "prof: dump -> %s (%u zones, %llu ticks/s)", path, PROF_ZONE_COUNT,
			(unsigned long long)g_ProfTicksPerSec);
}

static void fxtClose(void)
{
	if (!g_FxtFile) {
		return;
	}
	fxtFlush();
	fclose(g_FxtFile);
	g_FxtFile = NULL;
	sysLogPrintf(LOG_NOTE, "prof: dump closed, %llu bytes", (unsigned long long)g_FxtBytesWritten);
	free(g_FxtBuf);
	g_FxtBuf = NULL;
}

const char *profDumpPath(void)
{
	return g_FxtFile ? g_FxtPath : NULL;
}

// ---- the edges ----------------------------------------------------------

void profZoneBegin(s32 zone)
{
	if (zone < 0 || zone >= PROF_ZONE_COUNT) {
		return;
	}
	if (g_ProfDepth >= PROF_STACK_MAX) {
		g_ProfOverflow++;
		return;
	}
	const u64 now = profNow();
	const s32 seat = profSeatFor(zone);
	struct profframe *f = &g_ProfStack[g_ProfDepth++];
	f->zone = zone;
	f->seat = seat;
	f->t0 = now;
	f->child = 0;
	if (g_FxtFile) {
		fxtDuration(FXT_EV_BEGIN, zone, seat, now);
	}
}

void profZoneEnd(void)
{
	if (g_ProfDepth <= 0) {
		return;
	}
	const u64 now = profNow();
	struct profframe *f = &g_ProfStack[--g_ProfDepth];
	const u64 incl = now - f->t0;
	struct profcell *c = &g_ProfCur[f->zone][f->seat];
	c->incl += incl;
	c->self += incl - f->child;
	c->calls++;
	if (g_ProfDepth > 0) {
		g_ProfStack[g_ProfDepth - 1].child += incl;
	}
	if (g_FxtFile) {
		fxtDuration(FXT_EV_END, f->zone, f->seat, now);
	}
}

// counts only accumulate here; the dump gets one counter event per cell per
// frame from profFrameEnd, not one per call (propstick counts every prop)
void profZoneCount(s32 zone, u32 n)
{
	if (zone < 0 || zone >= PROF_ZONE_COUNT) {
		return;
	}
	g_ProfCur[zone][profSeatFor(zone)].count += n;
}

static u32 g_ProfNetIn[256];
static u32 g_ProfNetOut;

void profNetMsg(s32 in, u32 msgid, u32 bytes)
{
	if (in) {
		g_ProfNetIn[msgid & 0xff] += bytes;
		g_ProfCur[PROF_Z_NET_IN][PROF_SEAT_NONE].count += bytes;
		g_ProfCur[PROF_Z_NET_IN][PROF_SEAT_NONE].calls++;
	} else {
		g_ProfNetOut += bytes;
		g_ProfCur[PROF_Z_NET_OUT][PROF_SEAT_NONE].count += bytes;
		g_ProfCur[PROF_Z_NET_OUT][PROF_SEAT_NONE].calls++;
	}
}

// a marker: instant event on the frame track, name "mark", the text as an
// inline string argument - what the person saw, on the frame they saw it
void profMark(const char *text)
{
	if (!g_ProfOn) {
		return;
	}
	const u64 now = profNow();
	sysLogPrintf(LOG_NOTE, "prof: mark %s", text ? text : "");
	if (!g_FxtFile) {
		return;
	}
	size_t len = text ? strlen(text) : 0;
	if (len > 200) {
		len = 200;
	}
	const u32 strwords = (u32)((len + 7) / 8);
	// header, ts, arg header (string, inline value ref = 0x8000 | len), value words
	fxtReserve();
	fxtWord(fxtEventHeader(3 + strwords, FXT_EV_INSTANT, 1, FXT_THREAD_REF(PROF_SEAT_NONE), FXT_STR_MARK));
	fxtWord(now);
	fxtWord((u64)FXT_ARG_STRING | ((u64)(1 + strwords) << 4) | ((u64)FXT_STR_MARK << 16) | ((u64)(0x8000 | len) << 32));
	if (strwords) {
		u64 *dst = &g_FxtBuf[g_FxtWp];
		memset(dst, 0, strwords * sizeof(u64));
		memcpy(dst, text, len);
		g_FxtWp += strwords;
	}
}

// ---- frame --------------------------------------------------------------

void profFrameEnd(u32 frame)
{
	if (!g_ProfOn) {
		return;
	}
	// a zone left open at frame end is a hook bug; close it so the stack
	// cannot drift, and say so once
	if (g_ProfDepth > 0) {
		static s32 warned = 0;
		if (!warned) {
			warned = 1;
			sysLogPrintf(LOG_WARNING, "prof: %d zone(s) still open at frame end (top: %s)",
					g_ProfDepth, profZoneName(g_ProfStack[g_ProfDepth - 1].zone));
		}
		while (g_ProfDepth > 0) {
			profZoneEnd();
		}
	}

	PROF_BEGIN(PROF_Z_PROF_DUMP);
	if (g_FxtFile) {
		const u64 now = profNow();
		// this frame's counts: one counter event per cell that has any, the
		// zone as the counter id; and the per-message-class net bytes, the
		// message id as the counter id so the viewer draws a series per class
		for (s32 z = 0; z < PROF_ZONE_COUNT; z++) {
			if (z == PROF_Z_NET_IN || z == PROF_Z_NET_OUT) {
				continue; // ride as the per-message-id series below
			}
			for (s32 st = 0; st < PROF_SEATS; st++) {
				if (g_ProfCur[z][st].count) {
					fxtCounter(FXT_STR_ZONE(z), st, now, g_ProfCur[z][st].count, (u64)z);
				}
			}
		}
		for (s32 id = 0; id < 256; id++) {
			if (g_ProfNetIn[id]) {
				fxtCounter(FXT_STR_NETIN, PROF_SEAT_NONE, now, g_ProfNetIn[id], (u64)id);
			}
		}
		if (g_ProfNetOut) {
			fxtCounter(FXT_STR_NETOUT, PROF_SEAT_NONE, now, g_ProfNetOut, 0);
		}
		fxtFrameMark(now, frame);
		if (now - g_FxtLastFlush >= g_ProfTicksPerSec) {
			fxtFlush();
			fflush(g_FxtFile);
			g_FxtLastFlush = now;
		}
	}
	PROF_END();

	memcpy(g_ProfLast, g_ProfCur, sizeof(g_ProfLast));
	memset(g_ProfCur, 0, sizeof(g_ProfCur));
	memset(g_ProfNetIn, 0, sizeof(g_ProfNetIn));
	g_ProfNetOut = 0;
}

void profCrashFlush(void)
{
	if (g_FxtFile) {
		fxtFlush();
		fflush(g_FxtFile);
	}
}

// ---- init ---------------------------------------------------------------

static char g_ProfCfgDumpPath[1024] = "";

PD_CONSTRUCTOR static void profConfigInit(void)
{
	configRegisterInt("Prof.Enabled", &g_ProfOn, 0, 1);
	configRegisterString("Prof.DumpPath", g_ProfCfgDumpPath, sizeof(g_ProfCfgDumpPath) - 1);
}

void profInit(void)
{
#ifdef PLATFORM_WIN32
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_ProfTicksPerSec = (u64)f.QuadPart;
#endif

	// --prof turns the live table on; --profdump <path> turns it on and
	// streams it. Prof.Enabled / Prof.DumpPath in pd.ini do the same.
	const char *dump = sysArgGetString("--profdump");
	if (!dump && g_ProfCfgDumpPath[0]) {
		dump = g_ProfCfgDumpPath;
	}
	if (sysArgCheck("--prof") || dump) {
		g_ProfOn = 1;
	}
	if (g_ProfOn && dump) {
		fxtOpen(dump);
	}
	if (g_ProfOn) {
		sysLogPrintf(LOG_NOTE, "prof: on%s", g_FxtFile ? ", dumping" : "");
	}
}

void profShutdown(void)
{
	fxtClose();
	if (g_ProfOverflow) {
		sysLogPrintf(LOG_WARNING, "prof: zone stack overflowed %d times", g_ProfOverflow);
	}
}
