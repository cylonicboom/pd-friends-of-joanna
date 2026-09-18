/**
 * Lua scripting API + dev overlay for the action-block runtime (port-only in
 * practice; the whole Lua layer is compiled into the port build only).
 *
 * This sits on top of luaai.c (which owns the lua_State and the ailist
 * transpile/execute loop) and adds the developer-facing surface:
 *
 *   pd.on(event, fn)            -- "tick" | "draw" | "weaponfire" | "chrfire" | "punch"
 *                                  | "alert" | "kill" | "damage" | "headshot" | "spawn"
 *                                  | "roomenter" | "missioncomplete" | "firingrange"
 *                                  | "weaponfound" | "weaponpickup" | "objective"
 *                                  | "cheatunlock" | "challengecomplete"
 *   pd.draw_box(x,y,w,h,color[,secs])
 *   pd.draw_text(x,y,text,color[,secs])
 *   pd.each_chr(fn)             -- fn(chrnum, ailistid, aioffset, alertness, islua)
 *
 * Plus the C-side glue: a timed 2D overlay list rendered each frame, an event
 * registry + emitters called from game code (weapon fire / chr alert / kill),
 * and the per-frame X-ray sampling that proves every ailist is running through
 * the Lua exec loop.
 *
 * Coordinates are the lo-res virtual screen space (same as the console); colours
 * are 0xRRGGBBAA. All Lua calls go through lua_pcall so a broken script logs and
 * is skipped, never crashing the game.
 *
 * Taken from the Perfect Dark Kai fork (be46717). This file is Kai's core:
 * logging, overlays, events, persistence, the Archipelago gate state, the
 * read-only queries, CI bios, the per-frame tick and HUD pass. The rest of
 * Kai's pd.* functions live in luaai_api_<group>.c, registered from
 * luaApiRegister. Differences from Kai:
 *  - messages go through sysLogPrintf (this tree has no in-game console);
 *  - no netplay, so the net client tests are gone and pd.ext_poll is a stub;
 *  - the Archipelago transport (luaai_ap.c) is not here: pd.ap_connect,
 *    ap_status, ap_send, ap_poll and ap_disconnect are stubs, and so are
 *    pd.octree_stats and pd.dlcache_stats (no octree, no dlcache);
 *  - event handlers run protected and under the AI instruction budget;
 *  - luaTick and luaHudRender do nothing unless the Lua AI layer is on.
 */

#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "constants.h"
#include "types.h"
#include "game/luaai.h"
#include "game/chaosstate.h"  /* g_ChaosLoadSerial / g_ChaosMissionComplete / g_LuaShow* */
#include "game/game_1531a0.h" /* text0f153628 / text0f153780 / textRenderProjected */
#include "game/hudmsg.h"      /* hudmsgRenderBox */
#include "game/training.h"    /* ciGet*Bio* (pd.bio_count / pd.bio_text — CI lore) */
#include "game/lang.h"        /* langGet (bio text ids -> strings) */
#include "game/tex.h"         /* texSelect (OVL_SPRITE blood-splat textures) */
#include "game/camera.h"      /* camGetScreen* (pd.aim_bounds reticle box) */
#include "game/gfxmemory.h"   /* gfxGetFreeVtx / gfxGetVtxPoolSize (pd.perf) */
#include "data.h"             /* g_FontHandelGothicXs / g_CharsHandelGothicXs; g_ScaleX */
#include "bss.h"              /* g_Vars / g_GameFile / g_TexWallhitConfigs */
#include "lib/vi.h"           /* viGetWidth / viGetHeight */
#include "luaai_api_internal.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifndef PLATFORM_N64
#include "system.h"           /* sysLogPrintf */
#include "fs.h"               /* fsFileOpenRead/Write (pd.persist_* disk backing) */
#include "input.h"            /* inputLastSourceWasPad (pd.input_source) */
#include "video.h"            /* videoGetAverageFPS (pd.perf) */
#endif

/* ------------------------------------------------------------------------- *
 * State
 * ------------------------------------------------------------------------- */

#define LUA_MAX_OVERLAYS 96
#define LUA_MAX_XRAY     48
#define LUA_TEXT_MAX     56

/* g_TcWallhitConfigs has this many entries (textureconfig.h). */
#define LUA_WALLHIT_TEXCOUNT 18

struct luaoverlay {
	s32 kind;
	s32 x, y, w, h;
	u32 color;
	s32 texnum;     /* OVL_SPRITE: g_TexWallhitConfigs index. OVL_IMAGE: image handle. */
	f32 angle;      /* OVL_IMAGE: rotation in radians (x,y = CENTRE when set). */
	char text[LUA_TEXT_MAX];
	s32 framesleft; /* >0 timed; one-frame entries use 1 + oneframe flag */
	s32 oneframe;
};

static struct luaoverlay g_LuaOverlays[LUA_MAX_OVERLAYS];
static s32 g_LuaOverlayCount = 0;

struct luaxray {
	s32 chrnum, ailistid, aioffset, alertness, islua;
};

static struct luaxray g_LuaXray[LUA_MAX_XRAY];
static s32 g_LuaXrayCount = 0;

/* Last-seen room of player 0, for synthesising the "roomenter" event in luaTick
 * (there is no single engine call site for it). -0x7fffffff = "unknown yet". */
static s32 g_LuaLastPlayerRoom = -0x7fffffff;

/* registry table: event name -> array of handler functions */
static const char *const KEY_EVENTS = "luaai.events";

/* ------------------------------------------------------------------------- *
 * Logging (sysLogPrintf: pd.log and the terminal)
 * ------------------------------------------------------------------------- */

void luaApiLog(const char *s)
{
	sysLogPrintf(LOG_NOTE, "luaai: %s", s ? s : "");
}

void luaApiLog2(const char *prefix, const char *s)
{
	sysLogPrintf(LOG_NOTE, "luaai: %s%s", prefix ? prefix : "", s ? s : "");
}

void luaApiTextScrub(char *s)
{
	if (s == NULL) {
		return;
	}
	for (; *s != '\0'; s++) {
		u8 c = (u8)*s;
		if (c >= 0x7f || (c < 0x20 && c != '\n')) {
			*s = '?';
		}
	}
}

/* ------------------------------------------------------------------------- *
 * Script floats -- see luaai_api_internal.h for why.
 * ------------------------------------------------------------------------- */

static f32 luaApiFinite(lua_State *L, s32 idx, double d, f32 lo, f32 hi)
{
	f32 v;

	if (!isfinite(d)) {
		luaL_argerror(L, idx, "not a finite number");
	}

	/* Narrow AFTER the test: a double past FLT_MAX becomes an f32 inf. */
	v = (f32)(d < (double)lo ? (double)lo : d > (double)hi ? (double)hi : d);

	return v;
}

f32 luaApiNumR(lua_State *L, s32 idx, f32 lo, f32 hi)
{
	return luaApiFinite(L, idx, luaL_checknumber(L, idx), lo, hi);
}

f32 luaApiNum(lua_State *L, s32 idx)
{
	return luaApiNumR(L, idx, -LUAAPI_F_LIMIT, LUAAPI_F_LIMIT);
}

f32 luaApiOptNumR(lua_State *L, s32 idx, f32 def, f32 lo, f32 hi)
{
	if (lua_isnoneornil(L, idx)) {
		return def; /* unclamped: a default may be an out-of-range sentinel */
	}

	return luaApiFinite(L, idx, luaL_checknumber(L, idx), lo, hi);
}

f32 luaApiOptNum(lua_State *L, s32 idx, f32 def)
{
	return luaApiOptNumR(L, idx, def, -LUAAPI_F_LIMIT, LUAAPI_F_LIMIT);
}

/* Few enough names that a linear list is fine. */
#define LUA_UNAVAILABLE_MAX 16
static const char *g_LuaUnavailableLogged[LUA_UNAVAILABLE_MAX];
static s32 g_LuaUnavailableCount = 0;

void luaApiLogUnavailable(const char *name)
{
	s32 i;

	for (i = 0; i < g_LuaUnavailableCount; i++) {
		if (strcmp(g_LuaUnavailableLogged[i], name) == 0) {
			return;
		}
	}

	if (g_LuaUnavailableCount < LUA_UNAVAILABLE_MAX) {
		g_LuaUnavailableLogged[g_LuaUnavailableCount++] = name;
	}

	sysLogPrintf(LOG_WARNING, "luaai: pd.%s is unavailable in this build", name);
}

/* ------------------------------------------------------------------------- *
 * Overlay list
 * ------------------------------------------------------------------------- */

void luaOverlayAdd(s32 kind, s32 x, s32 y, s32 w, s32 h, u32 color,
		const char *text, s32 texnum, f32 angle, f32 secs)
{
	struct luaoverlay *o;

	if (g_LuaOverlayCount >= LUA_MAX_OVERLAYS) {
		return; /* full: drop silently */
	}

	o = &g_LuaOverlays[g_LuaOverlayCount++];
	o->kind = kind;
	o->x = x;
	o->y = y;
	o->w = w;
	o->h = h;
	o->color = color;
	o->texnum = texnum;
	o->angle = angle;

	if (text) {
		strncpy(o->text, text, LUA_TEXT_MAX - 1);
		o->text[LUA_TEXT_MAX - 1] = '\0';
		luaApiTextScrub(o->text);
	} else {
		o->text[0] = '\0';
	}

	/* The upper bound keeps the frame count in range: a huge secs would
	 * overflow the conversion. */
	if (secs > 0.f) {
		if (secs > 86400.f) {
			secs = 86400.f;
		}
		o->oneframe = 0;
		o->framesleft = (s32)(secs * 60.f) + 1;
	} else {
		o->oneframe = 1;
		o->framesleft = 1;
	}
}

/* ------------------------------------------------------------------------- *
 * Event registry + dispatch
 * ------------------------------------------------------------------------- */

#define LUA_EVENT_MAXARGS 4

/* Protected: [name (light userdata), argc ints...]. Looking the list up
 * allocates (the key strings), so it runs inside lua_pcall like everything
 * else that can raise. Each handler runs in its own luaaiPcall, so a broken
 * handler is logged and the rest still run, and outside an entity call each
 * handler gets its own instruction budget: one that loops is stopped without
 * taking the handlers after it down too. */
static int luaEventDispatchP(lua_State *L)
{
	const char *name = (const char *)lua_touserdata(L, 1);
	int argc = lua_gettop(L) - 1;
	int i, a, n;

	lua_getfield(L, LUA_REGISTRYINDEX, KEY_EVENTS); /* events */
	if (!lua_istable(L, -1)) {
		return 0;
	}
	lua_getfield(L, -1, name);                      /* events[name] */
	if (!lua_istable(L, -1)) {
		return 0;
	}

	n = (int)lua_rawlen(L, -1);
	for (i = 1; i <= n; i++) {
		lua_rawgeti(L, -1, i); /* fn */
		if (lua_isfunction(L, -1)) {
			for (a = 0; a < argc; a++) {
				lua_pushvalue(L, 2 + a);
			}
			if (luaaiPcall(L, argc, 0) != LUA_OK) {
				luaApiLog2("event error: ", luaaiErrStr(L, -1));
				lua_pop(L, 1); /* error msg */
			}
		} else {
			lua_pop(L, 1); /* non-function entry */
		}
	}

	return 0;
}

/* Never creates the Lua state: with no state (Lua off, or not loaded yet)
 * this is a pointer test and a return. */
static void luaEventDispatchInts(const char *name, int argc, const lua_Integer *argv)
{
	lua_State *L = luaaiGetState();
	int a;

	if (!L || luaaiIsSuspended()) {
		return;
	}

	if (argc > LUA_EVENT_MAXARGS) {
		argc = LUA_EVENT_MAXARGS;
	}

	if (!lua_checkstack(L, argc + 2)) {
		return;
	}

	/* None of these pushes allocates. */
	lua_pushcfunction(L, luaEventDispatchP);
	lua_pushlightuserdata(L, (void *)name);
	for (a = 0; a < argc; a++) {
		lua_pushinteger(L, argv[a]);
	}

	/* Not luaaiPcall: the lookup runs no Lua code, and arming here would
	 * make every handler share one budget. */
	if (lua_pcall(L, argc + 1, 0, 0) != LUA_OK) {
		luaApiLog2("event error: ", luaaiErrStr(L, -1));
		lua_pop(L, 1);
	}
}

/* ------------------------------------------------------------------------- *
 * Lua-callable functions (registered onto the pd table)
 * ------------------------------------------------------------------------- */

/* pd.on(event, fn) */
static int l_pd_on(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	lua_getfield(L, LUA_REGISTRYINDEX, KEY_EVENTS); /* events */
	lua_getfield(L, -1, name);                      /* events[name] */

	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);             /* nil */
		lua_newtable(L);           /* new list */
		lua_pushvalue(L, -1);      /* dup list */
		lua_setfield(L, -3, name); /* events[name] = list */
	}

	/* stack: events, list */
	lua_pushvalue(L, 2);                                  /* fn */
	lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1); /* list[#+1] = fn */
	lua_pop(L, 2);                                        /* list + events */
	return 0;
}

/* pd.draw_box(x, y, w, h, color, [secs]) */
static int l_pd_draw_box(lua_State *L)
{
	s32 x = (s32)luaL_checkinteger(L, 1);
	s32 y = (s32)luaL_checkinteger(L, 2);
	s32 w = (s32)luaL_checkinteger(L, 3);
	s32 h = (s32)luaL_checkinteger(L, 4);
	u32 color = (u32)luaL_optinteger(L, 5, 0xffffffffu);
	f32 secs = luaApiOptNum(L, 6, 0.0f);

	luaOverlayAdd(OVL_BOX, x, y, w, h, color, NULL, 0, 0.f, secs);
	return 0;
}

/* pd.draw_text(x, y, text, color, [secs]) */
static int l_pd_draw_text(lua_State *L)
{
	s32 x = (s32)luaL_checkinteger(L, 1);
	s32 y = (s32)luaL_checkinteger(L, 2);
	const char *text = luaL_checkstring(L, 3);
	u32 color = (u32)luaL_optinteger(L, 4, 0xffffffffu);
	f32 secs = luaApiOptNum(L, 5, 0.0f);

	luaOverlayAdd(OVL_TEXT, x, y, 0, 0, color, text, 0, 0.f, secs);
	return 0;
}

/* ------------------------------------------------------------------------- *
 * Session-persistent key->string store (pd.persist_get / pd.persist_set).
 *
 * The whole lua_State is destroyed (lua_close in luaaiReset) on every stage
 * change -- mission load, return to the main menu -- so script globals do NOT
 * survive a reload. This tiny C-owned table lives outside the lua_State, so a
 * script can persist state across that teardown.
 *
 * Backed by a plain "key=value" text file in the save dir (next to pd.ini), so
 * settings also survive QUITTING THE GAME.
 *
 * SESSION-ONLY KEYS: a key beginning with '~' is never written to (or read
 * from) the file -- it lives only for this process. Use it for state that must
 * outlive the per-stage lua_State teardown but MUST NOT outlive the game, e.g.
 * a mid-mission effect carry-over: restoring a half-finished effect after a
 * mission restart is right, resurrecting one days later after relaunching the
 * game is not.
 * ------------------------------------------------------------------------- */
#define LUA_PERSIST_MAX 32
#define LUA_PERSIST_FILE "$S/lua_persist.txt"
#define LUA_PERSIST_MAXLINE 2048
/* The file has one "key=value\n" line per entry and the reader (fgets into
 * char[LUA_PERSIST_MAXLINE]) SPLITS anything longer, so a longer value would
 * come back as a truncated value plus a junk second entry. Refuse it at
 * pd.persist_set instead, where the script can still see it fail. */
#define LUA_PERSIST_MAXENTRY (LUA_PERSIST_MAXLINE - 2)
static struct luapersist { char *key; char *val; } g_LuaPersist[LUA_PERSIST_MAX];
static s32 g_LuaPersistLoaded = 0;

static char *luaApiStrDup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = (char *)malloc(n);
	if (p) {
		memcpy(p, s, n);
	}
	return p;
}

/*
 * Write the whole table out. Called after any change, so a crash can never lose
 * more than the entry being written. The file is tiny (a few hundred bytes) and
 * changes at most a few times a minute, so a full rewrite is cheaper than
 * tracking dirty entries.
 */
static void luaApiPersistSave(void)
{
#ifndef PLATFORM_N64
	FILE *f;
	s32 i;

	/* don't write a file until we've read the existing one -- that would
	 * truncate the user's saved settings with a half-populated table */
	if (!g_LuaPersistLoaded) {
		return;
	}

	f = fsFileOpenWrite(LUA_PERSIST_FILE);
	if (!f) {
		return;
	}

	fprintf(f, "# Perfect Dark - persistent script settings (pd.persist_set).\n");
	fprintf(f, "# Rewritten by the game whenever a setting changes.\n");

	for (i = 0; i < LUA_PERSIST_MAX; i++) {
		const char *key = g_LuaPersist[i].key;
		const char *val = g_LuaPersist[i].val;

		if (!key || !val) {
			continue;
		}

		/* '~' prefix = session-only: keep it out of the file entirely (see the
		 * header comment). It stays live in the table for this process. */
		if (key[0] == '~') {
			continue;
		}

		/* The format has no escaping: a newline anywhere, or an '=' in the
		 * key, would produce a file we'd read back as something else. Values
		 * may contain '=' -- the reader splits on the FIRST one. */
		if (strchr(key, '\n') || strchr(key, '\r') || strchr(key, '=')
				|| strchr(val, '\n') || strchr(val, '\r')) {
			continue;
		}

		fprintf(f, "%s=%s\n", key, val);
	}

	fsFileFree(f);
#endif
}

/* Store a value without touching the file. Returns 1 if anything changed. */
static s32 luaApiPersistStore(const char *key, const char *val)
{
	s32 i;
	s32 slot = -1;

	for (i = 0; i < LUA_PERSIST_MAX; i++) {
		if (g_LuaPersist[i].key && strcmp(g_LuaPersist[i].key, key) == 0) {
			if (val && g_LuaPersist[i].val && strcmp(g_LuaPersist[i].val, val) == 0) {
				return 0; /* unchanged -- skip the rewrite */
			}
			free(g_LuaPersist[i].val);
			g_LuaPersist[i].val = NULL;
			if (val) {
				g_LuaPersist[i].val = luaApiStrDup(val);
			} else {
				free(g_LuaPersist[i].key);
				g_LuaPersist[i].key = NULL;
			}
			return 1;
		}
		if (slot < 0 && !g_LuaPersist[i].key) {
			slot = i;
		}
	}

	if (val && slot >= 0) {
		g_LuaPersist[slot].key = luaApiStrDup(key);
		g_LuaPersist[slot].val = luaApiStrDup(val);
		if (!g_LuaPersist[slot].key || !g_LuaPersist[slot].val) {
			/* out of memory: leave the slot empty rather than half-set */
			free(g_LuaPersist[slot].key);
			free(g_LuaPersist[slot].val);
			g_LuaPersist[slot].key = NULL;
			g_LuaPersist[slot].val = NULL;
			return 0;
		}
		return 1;
	}

	return 0;
}

/*
 * Read the file once, on first use. Scripts call pd.persist_get while building
 * their state, so this runs before any script can observe the store.
 */
static void luaApiPersistEnsureLoaded(void)
{
#ifndef PLATFORM_N64
	char line[LUA_PERSIST_MAXLINE];
	FILE *f;
#endif

	if (g_LuaPersistLoaded) {
		return;
	}

	/* set BEFORE parsing: luaApiPersistStore must not recurse back in here,
	 * and a missing file is a successful "loaded nothing" */
	g_LuaPersistLoaded = 1;

#ifndef PLATFORM_N64
	f = fsFileOpenRead(LUA_PERSIST_FILE);
	if (!f) {
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		char *eq;
		size_t len = strlen(line);

		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}

		/* '~' = session-only: we never write those, so one here means a
		 * hand-edited file. Ignore it rather than honour it. */
		if (line[0] == '\0' || line[0] == '#' || line[0] == '~') {
			continue;
		}

		eq = strchr(line, '=');
		if (!eq) {
			continue;
		}

		*eq = '\0';
		luaApiPersistStore(line, eq + 1);
	}

	fsFileFree(f);
#endif
}

/* The reader splits the file on '\n'/'\r' and splits each line on its first
 * '=', and none of that is recoverable once it is written. A value carrying a
 * line break comes back as a truncated value plus a forged second entry under
 * a key the script was never given; an '=' inside a key moves the split, so
 * the key reads back short and the value reads back long; and an embedded NUL
 * counts toward the length checked below but stops what fputs actually writes,
 * so the accounting and the file disagree. Refuse all three at write time. An
 * '=' in a VALUE is fine -- the reader only splits on the first one. */
static s32 luaApiPersistBadChars(const char *s, size_t len, s32 iskey)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (s[i] == '\n' || s[i] == '\r' || s[i] == '\0') {
			return 1;
		}

		if (iskey && s[i] == '=') {
			return 1;
		}
	}

	return 0;
}

/* pd.persist_set(key, value): a nil/absent value clears the key. A key starting
 * with '~' is session-only -- kept in memory, never written to the file. */
static int l_pd_persist_set(lua_State *L)
{
	size_t keylen = 0;
	size_t vallen = 0;
	const char *key = luaL_checklstring(L, 1, &keylen);
	const char *val = lua_isnoneornil(L, 2) ? NULL : luaL_checklstring(L, 2, &vallen);

	/* A session-only key is never written, so only the file-backed ones have
	 * to fit a line -- but hold both to the same limit so a script cannot
	 * find out a value was too long only when it drops the '~'. */
	if (keylen + vallen + 1 > LUA_PERSIST_MAXENTRY) {
		luaApiLog2("pd.persist_set: entry too long for the settings file, refused: ", key);
		lua_pushboolean(L, 0);
		return 1;
	}

	if (luaApiPersistBadChars(key, keylen, 1) || (val && luaApiPersistBadChars(val, vallen, 0))) {
		luaApiLog2("pd.persist_set: a line break or NUL, or an '=' in the key, refused: ", key);
		lua_pushboolean(L, 0);
		return 1;
	}

	luaApiPersistEnsureLoaded();

	if (luaApiPersistStore(key, val) && key[0] != '~') {
		luaApiPersistSave();
	}

	lua_pushboolean(L, 1);
	return 1;
}

/* pd.persist_get(key) -> string | nil */
static int l_pd_persist_get(lua_State *L)
{
	const char *key = luaL_checkstring(L, 1);
	s32 i;

	luaApiPersistEnsureLoaded();

	for (i = 0; i < LUA_PERSIST_MAX; i++) {
		if (g_LuaPersist[i].key && strcmp(g_LuaPersist[i].key, key) == 0) {
			if (g_LuaPersist[i].val) {
				lua_pushstring(L, g_LuaPersist[i].val);
			} else {
				lua_pushnil(L);
			}
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

/* ------------------------------------------------------------------------- *
 * Archipelago gating (pd.ap_mode / pd.unlock / pd.lock / pd.is_unlocked).
 *
 * One unlock set per category, 256 ids each (covers stages 0..20, weapons,
 * devices, and MP features 0..79). Lives outside the lua_State (C statics) so
 * the AP run's locks survive the per-stage lua_State teardown, exactly like the
 * persist KV. Engine gate points would read apGateActive() + apGateIsUnlocked();
 * none are wired in this build, so this is state only. Everything is INERT
 * unless a script has called pd.ap_mode(true).
 * ------------------------------------------------------------------------- */
#define AP_GATE_IDS 256
static bool g_ApGateMode;
static u8 g_ApUnlocks[AP_NUM_CATEGORIES][AP_GATE_IDS / 8];

/* Custom label the AP solo mission list shows in place of the "Mission 1"
 * group header — the script sets it (e.g. "Archipelago  3/20") via
 * pd.ap_list_header(). Lives outside the lua_State like the unlock set. Empty
 * string = fall back to the default header. */
#define AP_LIST_HEADER_MAX 48
static char g_ApListHeader[AP_LIST_HEADER_MAX];

/* Returns the script-set mission-list header, or NULL if unset/empty (caller
 * then uses the default). */
const char *apGetListHeader(void)
{
	return g_ApListHeader[0] ? g_ApListHeader : NULL;
}

/* Kai also returns false on a net client; this build has no netplay. */
bool apGateActive(void)
{
	return g_ApGateMode;
}

bool apGateIsUnlocked(s32 cat, s32 id)
{
	if (cat < 0 || cat >= AP_NUM_CATEGORIES || id < 0 || id >= AP_GATE_IDS) {
		return false;
	}
	return (g_ApUnlocks[cat][id >> 3] & (1 << (id & 7))) != 0;
}

/* Map a Lua category (string name or raw int) to an AP_CAT_* index, or -1. */
static s32 apGateCatArg(lua_State *L, s32 argn)
{
	const char *s;

	if (lua_type(L, argn) == LUA_TNUMBER) {
		lua_Integer c = luaL_checkinteger(L, argn);
		return (c >= 0 && c < AP_NUM_CATEGORIES) ? (s32)c : -1;
	}
	s = luaL_optstring(L, argn, "");
	if (strcmp(s, "stage") == 0)      return AP_CAT_STAGE;
	if (strcmp(s, "difficulty") == 0) return AP_CAT_DIFFICULTY;
	if (strcmp(s, "weapon_pri") == 0) return AP_CAT_WEAPON_PRI;
	if (strcmp(s, "weapon_sec") == 0) return AP_CAT_WEAPON_SEC;
	if (strcmp(s, "device") == 0)     return AP_CAT_DEVICE;
	if (strcmp(s, "feature") == 0)    return AP_CAT_FEATURE;
	return -1;
}

static void apGateSet(s32 cat, lua_Integer id, s32 on)
{
	if (cat < 0 || cat >= AP_NUM_CATEGORIES || id < 0 || id >= AP_GATE_IDS) {
		return;
	}
	if (on) {
		g_ApUnlocks[cat][id >> 3] |= (1 << (id & 7));
	} else {
		g_ApUnlocks[cat][id >> 3] &= ~(1 << (id & 7));
	}
}

/* pd.ap_mode([on]) -> bool : enable/disable AP gating (no arg = query). */
static int l_pd_ap_mode(lua_State *L)
{
	if (!lua_isnoneornil(L, 1)) {
		g_ApGateMode = lua_toboolean(L, 1) ? true : false;
	}
	lua_pushboolean(L, g_ApGateMode);
	return 1;
}

/* pd.unlock(category, id) : add (category,id) to the unlock set. */
static int l_pd_unlock(lua_State *L)
{
	apGateSet(apGateCatArg(L, 1), luaL_checkinteger(L, 2), 1);
	return 0;
}

/* pd.lock(category, id) : remove (category,id). */
static int l_pd_lock(lua_State *L)
{
	apGateSet(apGateCatArg(L, 1), luaL_checkinteger(L, 2), 0);
	return 0;
}

/* pd.is_unlocked(category, id) -> bool */
static int l_pd_is_unlocked(lua_State *L)
{
	s32 cat = apGateCatArg(L, 1);
	lua_Integer id = luaL_checkinteger(L, 2);

	lua_pushboolean(L, id >= 0 && id < AP_GATE_IDS && apGateIsUnlocked(cat, (s32)id));
	return 1;
}

/* pd.ap_reset() : clear all unlocks (does not change ap_mode). */
static int l_pd_ap_reset(lua_State *L)
{
	memset(g_ApUnlocks, 0, sizeof(g_ApUnlocks));
	(void)L;
	return 0;
}

/* pd.ap_list_header([str]) -> str : set/clear the AP mission-list group header
 * (nil or "" restores the default), returns the current value. The script
 * typically sets this to a progress string like "Archipelago  3/20". */
static int l_pd_ap_list_header(lua_State *L)
{
	if (!lua_isnoneornil(L, 1)) {
		const char *s = luaL_checkstring(L, 1);
		snprintf(g_ApListHeader, sizeof(g_ApListHeader), "%s", s);
		luaApiTextScrub(g_ApListHeader);
	} else if (lua_type(L, 1) == LUA_TNIL) {
		g_ApListHeader[0] = '\0';
	}
	lua_pushstring(L, g_ApListHeader);
	return 1;
}

/* pd.each_chr(fn) -> fn(chrnum, ailistid, aioffset, alertness, islua) */
static int l_pd_each_chr(lua_State *L)
{
	s32 i;

	luaL_checktype(L, 1, LUA_TFUNCTION);

	for (i = 0; i < g_LuaXrayCount; i++) {
		struct luaxray *r = &g_LuaXray[i];
		lua_pushvalue(L, 1); /* fn */
		lua_pushinteger(L, r->chrnum);
		lua_pushinteger(L, r->ailistid);
		lua_pushinteger(L, r->aioffset);
		lua_pushinteger(L, r->alertness);
		lua_pushinteger(L, r->islua);
		if (lua_pcall(L, 5, 0, 0) != LUA_OK) {
			luaApiLog2("each_chr error: ", luaaiErrStr(L, -1));
			lua_pop(L, 1);
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------- *
 * World / entity query API (read-only). Backed by bridge accessors in chrai.c
 * and luaai_bridge_core.c so this file stays free of engine structs.
 * ------------------------------------------------------------------------- */

/* Push a Lua table describing a chr snapshot. Shared by pd.chr_info and
 * ctx:self() so both have the same shape. Leaves the table on the stack. */
void luaApiPushChrInfo(lua_State *L, const struct luaaiselfinfo *info)
{
	lua_newtable(L);
	lua_pushinteger(L, info->chrnum);    lua_setfield(L, -2, "chrnum");
	lua_pushnumber(L, info->x);          lua_setfield(L, -2, "x");
	lua_pushnumber(L, info->y);          lua_setfield(L, -2, "y");
	lua_pushnumber(L, info->z);          lua_setfield(L, -2, "z");
	lua_pushinteger(L, info->room);      lua_setfield(L, -2, "room");
	lua_pushnumber(L, info->health);     lua_setfield(L, -2, "health");
	lua_pushnumber(L, info->maxhealth);  lua_setfield(L, -2, "maxhealth");
	lua_pushnumber(L, info->shield);     lua_setfield(L, -2, "shield");
	lua_pushinteger(L, info->alertness); lua_setfield(L, -2, "alertness");
	if (info->targetchrnum >= 0) {
		lua_pushinteger(L, info->targetchrnum);
		lua_setfield(L, -2, "target_chrnum");
	}
	if (info->targetplayernum >= 0) {
		lua_pushinteger(L, info->targetplayernum);
		lua_setfield(L, -2, "target_playernum");
	}
}

/* chrnum argument: out-of-range integers become -1 (no such chr) instead of
 * wrapping into a real one. */
static s32 luaApiCheckChrnum(lua_State *L, int arg)
{
	lua_Integer v = luaL_checkinteger(L, arg);
	return (v >= 0 && v <= 0x7fffffff) ? (s32)v : -1;
}

/* pd.chr_info(chrnum) -> table | nil */
static int l_pd_chr_info(lua_State *L)
{
	s32 chrnum = luaApiCheckChrnum(L, 1);
	struct luaaiselfinfo info;
	if (!chraiLuaGetChrInfo(chrnum, &info)) {
		lua_pushnil(L);
		return 1;
	}
	luaApiPushChrInfo(L, &info);
	return 1;
}

/* pd.chr_pos(chrnum) -> x, y, z | nil */
static int l_pd_chr_pos(lua_State *L)
{
	s32 chrnum = luaApiCheckChrnum(L, 1);
	struct luaaiselfinfo info;
	if (!chraiLuaGetChrInfo(chrnum, &info)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushnumber(L, info.x);
	lua_pushnumber(L, info.y);
	lua_pushnumber(L, info.z);
	return 3;
}

/* pd.chr_health(chrnum) -> health, maxhealth | nil */
static int l_pd_chr_health(lua_State *L)
{
	s32 chrnum = luaApiCheckChrnum(L, 1);
	struct luaaiselfinfo info;
	if (!chraiLuaGetChrInfo(chrnum, &info)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushnumber(L, info.health);
	lua_pushnumber(L, info.maxhealth);
	return 2;
}

/* pd.player_pos([n]) -> x, y, z | nil  (n defaults to 0) */
static int l_pd_player_pos(lua_State *L)
{
	lua_Integer n = luaL_optinteger(L, 1, 0);
	struct luaaiplayerinfo info;
	if (n < 0 || n >= MAX_PLAYERS || !chraiLuaGetPlayerInfo((s32)n, &info)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushnumber(L, info.x);
	lua_pushnumber(L, info.y);
	lua_pushnumber(L, info.z);
	return 3;
}

/* pd.player_count() -> n */
static int l_pd_player_count(lua_State *L)
{
	lua_pushinteger(L, chraiLuaGetPlayerCount());
	return 1;
}

/* pd.stage() -> current stage number (g_Vars.stagenum, e.g. STAGE_CITRAINING
 * for the Carrington Institute main-menu hub). Lets scripts tell the menu/hub
 * apart from real gameplay. */
static int l_pd_stage(lua_State *L)
{
	lua_pushinteger(L, chraiLuaGetStageNum());
	return 1;
}

/* pd.text_size(str) -> width, height in the same font pd.draw_text renders with
 * (g_FontHandelGothicXs). Lets a script size a background box to hug the text. */
static int l_pd_text_size(lua_State *L)
{
	const char *text = luaL_checkstring(L, 1);
	char buf[256];
	s32 h = 0, w = 0;

	/* textMeasure has the renderer's high-byte and control-byte hazards */
	strncpy(buf, text, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	luaApiTextScrub(buf);

	if (g_CharsHandelGothicXs && g_FontHandelGothicXs) {
		textMeasure(&h, &w, buf, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0);
	}
	lua_pushinteger(L, w);
	lua_pushinteger(L, h);
	return 2;
}

/* pd.distance(x1,y1,z1, x2,y2,z2) -> number. Pure helper; convenient for
 * deciding on ranges from chr_pos/player_pos results. */
static int l_pd_distance(lua_State *L)
{
	double dx = luaL_checknumber(L, 1) - luaL_checknumber(L, 4);
	double dy = luaL_checknumber(L, 2) - luaL_checknumber(L, 5);
	double dz = luaL_checknumber(L, 3) - luaL_checknumber(L, 6);
	lua_pushnumber(L, (lua_Number)sqrt(dx * dx + dy * dy + dz * dz));
	return 1;
}

/* pd.all_chrs(fn): call fn(chrnum) for EVERY live actor (not just those whose AI
 * ran this frame, which is pd.each_chr).
 * pd.all_chrs()   : with no function arg, return an array table of the live
 * chrnums instead. */
static int l_pd_all_chrs(lua_State *L)
{
	s32 i, n;
	const int hasfn = (lua_type(L, 1) == LUA_TFUNCTION);
	s32 outidx = 0;

	if (!hasfn) {
		lua_newtable(L);
	}

	n = chraiLuaGetChrSlotCount();
	for (i = 0; i < n; i++) {
		s32 chrnum = chraiLuaGetChrNumBySlot(i);
		if (chrnum < 0) {
			continue; /* empty slot */
		}
		if (hasfn) {
			lua_pushvalue(L, 1); /* fn */
			lua_pushinteger(L, chrnum);
			if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
				luaApiLog2("all_chrs error: ", luaaiErrStr(L, -1));
				lua_pop(L, 1);
			}
			/* the callback may have spawned or reaped chrs */
			n = chraiLuaGetChrSlotCount();
		} else {
			lua_pushinteger(L, chrnum);
			lua_rawseti(L, -2, ++outidx); /* result[outidx] = chrnum */
		}
	}
	return hasfn ? 0 : 1;
}

/* pd.lvupdate() -> int. Game ticks elapsed this frame (0 while paused). */
static int l_pd_lvupdate(lua_State *L)
{
	lua_pushinteger(L, chraiLuaLvUpdate());
	return 1;
}

/* pd.mission_complete() -> bool. True once the game has pushed a COMPLETED solo/
 * co-op mission endscreen (mission won, not failed/aborted); cleared on the next
 * stage load. Lets a script tear down effects on success, before the hub. */
static int l_pd_mission_complete(lua_State *L)
{
	lua_pushboolean(L, g_ChaosMissionComplete != 0);
	return 1;
}

/* pd.load_serial() -> int. Counter bumped once per level load (lvReset). A
 * CHANGE means a reload happened — the reliable form of "did we leave
 * gameplay". Compare, don't try to catch the window. */
static int l_pd_load_serial(lua_State *L)
{
	lua_pushinteger(L, g_ChaosLoadSerial);
	return 1;
}

/* pd.input_source() -> "pad" | "kbm". The device the player most recently
 * used. */
static int l_pd_input_source(lua_State *L)
{
	lua_pushstring(L, inputLastSourceWasPad() ? "pad" : "kbm");
	return 1;
}

/* pd.bio_count() -> nchr, nmisc. How many character / misc bios the save has
 * unlocked (Carrington Institute information terminal). */
static int l_pd_bio_count(lua_State *L)
{
	lua_pushinteger(L, ciGetNumUnlockedChrBios());
	lua_pushinteger(L, ciGetNumUnlockedMiscBios());
	return 2;
}

/* pd.bio_text(kind, slot) -> name, body | nil. kind 0 = character bios
 * (body = description text), kind 1 = misc bios. slot is 0-based within the
 * unlocked set (pd.bio_count). The game's own bios, straight from the CI
 * information terminal. */
static int l_pd_bio_text(lua_State *L)
{
	lua_Integer kind = luaL_checkinteger(L, 1);
	lua_Integer slot = luaL_checkinteger(L, 2);

	if (kind == 0) {
		struct chrbio *bio;
		if (slot < 0 || slot >= ciGetNumUnlockedChrBios()) {
			lua_pushnil(L);
			return 1;
		}
		/* This tree's bio table is keyed by character (body and head), and
		 * its entries can be literal strings rather than text ids. */
		bio = ciGetChrBio(ciGetChrBioBySlot((s32)slot));
		if (bio == NULL) {
			lua_pushnil(L);
			return 1;
		}
		lua_pushstring(L, chrBioText(bio->name, bio->flags));
		lua_pushstring(L, chrBioText(bio->description, bio->flags));
		return 2;
	} else {
		struct miscbio *bio;
		if (slot < 0 || slot >= ciGetNumUnlockedMiscBios()) {
			lua_pushnil(L);
			return 1;
		}
		bio = ciGetMiscBio(ciGetMiscBioIndexBySlot((s32)slot));
		if (bio == NULL) {
			lua_pushnil(L);
			return 1;
		}
		lua_pushstring(L, langGet(bio->name));
		lua_pushstring(L, langGet(bio->description));
		return 2;
	}
}

/* pd.player_name() -> string. The solo save file's agent name — whatever the
 * player typed when creating their file. */
static int l_pd_player_name(lua_State *L)
{
	char name[sizeof(g_GameFile.name) + 1];

	/* not NUL-terminated when the name fills the field */
	memcpy(name, g_GameFile.name, sizeof(g_GameFile.name));
	name[sizeof(g_GameFile.name)] = '\0';
	lua_pushstring(L, name);
	return 1;
}

/* pd.room_count() -> n. Rooms on this stage; real room numbers are 1..n-1
 * (index 0 is not a room). 0 when no stage is loaded. */
static int l_pd_room_count(lua_State *L)
{
	lua_pushinteger(L, chraiLuaRoomCount());
	return 1;
}

/* pd.chr_slots() -> free, total. Chr slots for this stage. The table is fixed at
 * stage load (players + the setup's own chrs + MAX_BOTS spare), so runtime
 * spawners share the spare slots. NB a corpse still holds its slot until reaped. */
static int l_pd_chr_slots(lua_State *L)
{
	lua_pushinteger(L, chraiLuaChrSlotsFree());
	lua_pushinteger(L, chraiLuaChrSlotsTotal());
	return 2;
}

/* pd.buttons() -> int. The local player's RAW held pad buttons (N64 mask:
 * A 0x8000, B 0x4000, Z 0x2000, R 0x10, C-up 8, C-down 4, C-left 2,
 * C-right 1). Sees buttons even while pd.button_block hides them from
 * gameplay. */
static int l_pd_buttons(lua_State *L)
{
	lua_pushinteger(L, (lua_Integer)chraiLuaButtons(0));
	return 1;
}

/* pd.buttons_pressed() -> int. Buttons newly pressed this frame (same mask). */
static int l_pd_buttons_pressed(lua_State *L)
{
	lua_pushinteger(L, (lua_Integer)chraiLuaButtons(1));
	return 1;
}

/* pd.aim_chr() -> chrnum | nil. The chr the local player is aiming at. */
static int l_pd_aim_chr(lua_State *L)
{
	s32 chrnum = chraiLuaAimChr();
	if (chrnum < 0) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, chrnum);
	}
	return 1;
}

/* pd.aim_screen() -> x, y | nil. The local player's aim reticle position in the
 * lo-res HUD/overlay space (the same ~320x220 coords pd.draw_* / pd.draw_box
 * use). nil when there is no live pawn (menus / cutscene). crosspos[0] is stored
 * in g_ScaleX-scaled units, so it is divided back to virtual space to match
 * sightDrawDefault; crosspos[1] is already virtual. */
static int l_pd_aim_screen(lua_State *L)
{
	if (g_Vars.currentplayer == NULL || g_Vars.currentplayer->prop == NULL) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushnumber(L, g_Vars.currentplayer->crosspos[0] / (f32)(g_ScaleX ? g_ScaleX : 1));
	lua_pushnumber(L, g_Vars.currentplayer->crosspos[1]);
	return 2;
}

/* pd.aim_bounds() -> x0, y0, x1, y1 | nil. The rectangle the aim reticle can
 * actually reach, in the same HUD space as pd.aim_screen. It is the crosspos
 * clamp box from bondgun.c — [3, screenwidth-4] x [3, screenheight-4] plus the
 * camera screen offset — with X divided by g_ScaleX to match aim_screen. Place
 * on-screen aim targets as fractions of this box so they stay reachable at any
 * resolution / viewport. nil when there is no live pawn. */
static int l_pd_aim_bounds(lua_State *L)
{
	f32 sx, sy, sw, sh, div;
	if (g_Vars.currentplayer == NULL || g_Vars.currentplayer->prop == NULL) {
		lua_pushnil(L);
		return 1;
	}
	sx = camGetScreenLeft();
	sy = camGetScreenTop();
	sw = camGetScreenWidth();
	sh = camGetScreenHeight();
	div = (f32)(g_ScaleX ? g_ScaleX : 1);
	lua_pushnumber(L, (sx + 3.0f) / div);
	lua_pushnumber(L, sy + 3.0f);
	lua_pushnumber(L, (sx + sw - 4.0f) / div);
	lua_pushnumber(L, sy + sh - 4.0f);
	return 4;
}

/* pd.objective_status(index) -> int. The objective's REAL status (any force
 * bypassed): 0 incomplete / 1 complete / 2 failed, or -1 if the index isn't a
 * live objective on this stage + difficulty. */
static int l_pd_objective_status(lua_State *L)
{
	lua_Integer index = luaL_checkinteger(L, 1);
	lua_pushinteger(L, (index >= 0 && index < MAX_OBJECTIVES)
			? chraiLuaObjectiveStatus((s32)index) : -1);
	return 1;
}

/* ------------------------------------------------------------------------- *
 * Diagnostics
 * ------------------------------------------------------------------------- */

/* pd.perf() -> table { fps, frame_ms, vtx_used, vtx_total, show_fps, show_mem }.
 * Render rate (video.c's 1s-averaged FPS) + the per-frame vtx scratch pool
 * (process RSS isn't useful here: the pools are pre-allocated, so RSS doesn't
 * move with load). show_fps / show_mem are g_LuaShowFps / g_LuaShowMem, which
 * Kai toggles from its console; nothing toggles them in this build. Kai's perf
 * table has no octree or dlcache fields, and neither does this one. */
static int l_pd_perf(lua_State *L)
{
	f32 fps = videoGetAverageFPS();
	u32 total = gfxGetVtxPoolSize();
	u32 freev = gfxGetFreeVtx();
	u32 used = (freev <= total) ? (total - freev) : total;

	lua_createtable(L, 0, 6);
	lua_pushnumber(L, (lua_Number)fps);                              lua_setfield(L, -2, "fps");
	lua_pushnumber(L, fps > 0.0f ? 1000.0 / (lua_Number)fps : 0.0);  lua_setfield(L, -2, "frame_ms");
	lua_pushinteger(L, (lua_Integer)used);                          lua_setfield(L, -2, "vtx_used");
	lua_pushinteger(L, (lua_Integer)total);                         lua_setfield(L, -2, "vtx_total");
	lua_pushboolean(L, g_LuaShowFps);                               lua_setfield(L, -2, "show_fps");
	lua_pushboolean(L, g_LuaShowMem);                               lua_setfield(L, -2, "show_mem");
	return 1;
}

/* ------------------------------------------------------------------------- *
 * Registered but unavailable: Kai's octree and display-list cache, its
 * netplay event ingress and its Archipelago transport are not in this build.
 * Each returns nil and logs once, so a script written for Kai keeps loading
 * and can test the result.
 * ------------------------------------------------------------------------- */

#define LUA_API_UNAVAILABLE(fn, name)             \
	static int fn(lua_State *L)                   \
	{                                             \
		luaApiLogUnavailable(name);               \
		lua_pushnil(L);                           \
		return 1;                                 \
	}

LUA_API_UNAVAILABLE(l_pd_octree_stats, "octree_stats")
LUA_API_UNAVAILABLE(l_pd_dlcache_stats, "dlcache_stats")
LUA_API_UNAVAILABLE(l_pd_ext_poll, "ext_poll")
LUA_API_UNAVAILABLE(l_pd_ap_connect, "ap_connect")
LUA_API_UNAVAILABLE(l_pd_ap_status, "ap_status")
LUA_API_UNAVAILABLE(l_pd_ap_send, "ap_send")
LUA_API_UNAVAILABLE(l_pd_ap_poll, "ap_poll")
LUA_API_UNAVAILABLE(l_pd_ap_disconnect, "ap_disconnect")

/* ------------------------------------------------------------------------- *
 * Registration
 * ------------------------------------------------------------------------- */

static const luaL_Reg g_LuaApiCoreFuncs[] = {
	{ "on",               l_pd_on },
	{ "draw_box",         l_pd_draw_box },
	{ "draw_text",        l_pd_draw_text },
	{ "text_size",        l_pd_text_size },
	{ "each_chr",         l_pd_each_chr },
	{ "octree_stats",     l_pd_octree_stats },
	{ "dlcache_stats",    l_pd_dlcache_stats },
	{ "perf",             l_pd_perf },
	{ "chr_info",         l_pd_chr_info },
	{ "chr_pos",          l_pd_chr_pos },
	{ "chr_health",       l_pd_chr_health },
	{ "player_pos",       l_pd_player_pos },
	{ "player_count",     l_pd_player_count },
	{ "stage",            l_pd_stage },
	{ "distance",         l_pd_distance },
	{ "all_chrs",         l_pd_all_chrs },
	{ "lvupdate",         l_pd_lvupdate },
	{ "mission_complete", l_pd_mission_complete },
	{ "load_serial",      l_pd_load_serial },
	{ "persist_get",      l_pd_persist_get },
	{ "persist_set",      l_pd_persist_set },
	{ "ap_mode",          l_pd_ap_mode },
	{ "unlock",           l_pd_unlock },
	{ "lock",             l_pd_lock },
	{ "is_unlocked",      l_pd_is_unlocked },
	{ "ap_reset",         l_pd_ap_reset },
	{ "ap_list_header",   l_pd_ap_list_header },
	{ "input_source",     l_pd_input_source },
	{ "bio_count",        l_pd_bio_count },
	{ "bio_text",         l_pd_bio_text },
	{ "ext_poll",         l_pd_ext_poll },
	{ "objective_status", l_pd_objective_status },
	{ "buttons",          l_pd_buttons },
	{ "buttons_pressed",  l_pd_buttons_pressed },
	{ "chr_slots",        l_pd_chr_slots },
	{ "player_name",      l_pd_player_name },
	{ "room_count",       l_pd_room_count },
	{ "aim_chr",          l_pd_aim_chr },
	{ "aim_screen",       l_pd_aim_screen },
	{ "aim_bounds",       l_pd_aim_bounds },
	/* Kai's luaApiRegisterAp (luaai_ap.c) */
	{ "ap_connect",       l_pd_ap_connect },
	{ "ap_status",        l_pd_ap_status },
	{ "ap_send",          l_pd_ap_send },
	{ "ap_poll",          l_pd_ap_poll },
	{ "ap_disconnect",    l_pd_ap_disconnect },
	{ NULL, NULL },
};

/* Called by luaai.c's luaai_build_pd with the pd table on top of the stack. */
void luaApiRegister(lua_State *L)
{
	/* create the events registry table (replaces any previous one) */
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, KEY_EVENTS);

	/* pd.* functions (pd table is at -1) */
	luaL_setfuncs(L, g_LuaApiCoreFuncs, 0);

	luaApiRegisterPlayer(L);
	luaApiRegisterWeapons(L);
	luaApiRegisterChrs(L);
	luaApiRegisterWorld(L);
	luaApiRegisterFx(L);
	luaApiRegisterMenus(L);
}

/* Clear C-side per-state data. Called from luaaiReset (the Lua registry events
 * table is dropped automatically when the state is closed). */
void luaApiResetFrame(void)
{
	g_LuaOverlayCount = 0;
	g_LuaXrayCount = 0;
	g_LuaLastPlayerRoom = -0x7fffffff; /* re-baseline room tracking on reset */
	luaApiResetFx();
	luaApiResetMenus();
	luaDirectorRebuild(); /* drop stale entries from the menu items array */
}

/* ------------------------------------------------------------------------- *
 * X-ray sampling (called from luaaiExecute once per chr per frame)
 * ------------------------------------------------------------------------- */

void luaApiRecordChr(s32 chrnum, s32 ailistid, s32 aioffset, s32 alertness, s32 islua)
{
	struct luaxray *r;

	if (chrnum < 0 || g_LuaXrayCount >= LUA_MAX_XRAY) {
		return;
	}

	r = &g_LuaXray[g_LuaXrayCount++];
	r->chrnum = chrnum;
	r->ailistid = ailistid;
	r->aioffset = aioffset;
	r->alertness = alertness;
	r->islua = islua;
}

/* ------------------------------------------------------------------------- *
 * Event emitters (called from game code). Each is a no-op while no Lua state
 * exists (see luaEventDispatchInts).
 * ------------------------------------------------------------------------- */

void luaEmitWeaponFire(s32 weaponnum, s32 playernum)
{
	lua_Integer a[2];
	a[0] = weaponnum;
	a[1] = playernum;
	luaEventDispatchInts("weaponfire", 2, a);
}

/* NPC/simulant gun discharge (chraction.c chrTickShoot) or punch/kick
 * (chrTryPunch, weaponnum UNARMED; players report via luaEmitWeaponFire /
 * luaEmitPunch instead). */
void luaEmitChrFire(s32 chrnum, s32 weaponnum)
{
	lua_Integer a[2];
	a[0] = chrnum;
	a[1] = weaponnum;
	luaEventDispatchInts("chrfire", 2, a);
}

/* Player melee swing (bondgun.c bgunTickIncAttackingMelee; fists and knife
 * alike — listeners filter by weaponnum). Distinct from "weaponfire" so
 * gun-only hooks stay unaffected. */
void luaEmitPunch(s32 weaponnum, s32 playernum)
{
	lua_Integer a[2];
	a[0] = weaponnum;
	a[1] = playernum;
	luaEventDispatchInts("punch", 2, a);
}

void luaEmitAlert(s32 chrnum, s32 playernum)
{
	lua_Integer a[2];
	a[0] = chrnum;
	a[1] = playernum;
	luaEventDispatchInts("alert", 2, a);
}

void luaEmitKill(s32 chrnum, s32 killerplayernum)
{
	lua_Integer a[2];
	a[0] = chrnum;
	a[1] = killerplayernum;
	luaEventDispatchInts("kill", 2, a);
}

void luaEmitDamage(s32 chrnum, s32 attackerplayernum, s32 amount)
{
	lua_Integer a[3];
	a[0] = chrnum;
	a[1] = attackerplayernum;
	a[2] = amount;
	luaEventDispatchInts("damage", 3, a);
}

void luaEmitHeadshot(s32 chrnum, s32 attackerplayernum)
{
	lua_Integer a[2];
	a[0] = chrnum;
	a[1] = attackerplayernum;
	luaEventDispatchInts("headshot", 2, a);
}

void luaEmitSpawn(s32 chrnum)
{
	lua_Integer a[1];
	a[0] = chrnum;
	luaEventDispatchInts("spawn", 1, a);
}

void luaEmitRoomEnter(s32 room, s32 fromroom)
{
	lua_Integer a[2];
	a[0] = room;
	a[1] = fromroom;
	luaEventDispatchInts("roomenter", 2, a);
}

void luaEmitMissionComplete(s32 stageindex, s32 difficulty, s32 secs, s32 cheated)
{
	lua_Integer a[4];
	a[0] = stageindex;
	a[1] = difficulty;
	a[2] = secs;
	a[3] = cheated;
	luaEventDispatchInts("missioncomplete", 4, a);
}

void luaEmitFiringRange(s32 weaponindex, s32 medal)
{
	lua_Integer a[2];
	a[0] = weaponindex;
	a[1] = medal;
	luaEventDispatchInts("firingrange", 2, a);
}

/* First discovery only: fires when a g_GameFile.weaponsfound bit is newly
 * set, so it is rare on a developed save. "weaponpickup" fires on every
 * pickup. */
void luaEmitWeaponFound(s32 weaponnum)
{
	lua_Integer a[1];
	a[0] = weaponnum;
	luaEventDispatchInts("weaponfound", 1, a);
}

/* EVERY local weapon-class pickup (weaponPlayPickupSound, propobj.c). */
void luaEmitWeaponPickup(s32 weaponnum)
{
	lua_Integer a[1];
	a[0] = weaponnum;
	luaEventDispatchInts("weaponpickup", 1, a);
}

void luaEmitObjective(s32 stageindex, s32 difficulty, s32 objindex, s32 status)
{
	lua_Integer a[4];
	a[0] = stageindex;
	a[1] = difficulty;
	a[2] = objindex;
	a[3] = status;
	luaEventDispatchInts("objective", 4, a);
}

void luaEmitCheatUnlock(s32 cheatid)
{
	lua_Integer a[1];
	a[0] = cheatid;
	luaEventDispatchInts("cheatunlock", 1, a);
}

void luaEmitChallengeComplete(s32 challengeindex, s32 numplayers)
{
	lua_Integer a[2];
	a[0] = challengeindex;
	a[1] = numplayers;
	luaEventDispatchInts("challengecomplete", 2, a);
}

/* ------------------------------------------------------------------------- *
 * Per-frame tick + render (called from the port frame loop)
 * ------------------------------------------------------------------------- */

void luaTick(void)
{
	s32 i, w;

	/* Lua off, or suspended for this stage: nothing to load, nothing to age. */
	if (!g_LuaAiEnabled || luaaiIsSuspended()) {
		return;
	}

	/* Make sure scripts are loaded even when no AI is running (title/CI), so
	 * the event handlers work everywhere. */
	luaaiEnsureState();

	/* Per-frame "tick" event -- fires everywhere (menus/loading too), unlike
	 * "draw" which only fires while the HUD renders. */
	luaEventDispatchInts("tick", 0, NULL);

	/* Synthesise the "roomenter" event by watching player 0's room each frame
	 * (there is no single engine call site that means "player changed room").
	 * Only emits on an actual change; the first observed room is recorded
	 * silently so we don't fire a spurious enter at stage start. */
	{
		struct luaaiplayerinfo pi;
		if (chraiLuaGetPlayerInfo(0, &pi) && pi.valid) {
			if (g_LuaLastPlayerRoom == -0x7fffffff) {
				g_LuaLastPlayerRoom = pi.room;
			} else if (pi.room != g_LuaLastPlayerRoom) {
				s32 from = g_LuaLastPlayerRoom;
				g_LuaLastPlayerRoom = pi.room;
				luaEmitRoomEnter(pi.room, from);
			}
		}
	}

	/* Age timed overlays. One-frame overlays are removed by luaHudRender after
	 * they're drawn, so they shouldn't normally be present here. */
	w = 0;
	for (i = 0; i < g_LuaOverlayCount; i++) {
		struct luaoverlay *o = &g_LuaOverlays[i];
		if (o->oneframe) {
			continue; /* drop stragglers */
		}
		if (--o->framesleft > 0) {
			if (w != i) {
				g_LuaOverlays[w] = *o;
			}
			w++;
		}
	}
	g_LuaOverlayCount = w;
}

#ifndef PLATFORM_N64
// Blit a textureconfig as a screen-space texrect (no projection matrix needed
// — same path the font glyphs use inside the text0f153628 bracket). Combine:
// colour = PRIMITIVE (the requested tint), alpha = TEXEL0 * PRIMITIVE.alpha, so
// the shape comes from the texture and the RGB from `color`. Pass a full-white
// opaque colour to draw an image untinted.
static Gfx *luaDrawTexConfig(Gfx *gdl, struct textureconfig *tc, s32 texw, s32 texh,
		s32 x, s32 y, s32 w, s32 h, u32 color)
{
	if (texw < 1) texw = 1;
	if (texh < 1) texh = 1;
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	texSelect(&gdl, tc, 2, 0, 2, 1, NULL);

	gDPSetCombineLERP(gdl++,
			0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0,
			0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
	gDPSetPrimColorViaWord(gdl++, 0, 0, color);

	gSPTextureRectangle(gdl++,
			x * 4, y * 4, (x + w) * 4, (y + h) * 4,
			G_TX_RENDERTILE,
			0, 0,
			(texw << 10) / w, (texh << 10) / h);

	return gdl;
}

// Draw a wall-hit texture (blood splat) as a tinted HUD quad (OVL_SPRITE,
// pd.draw_sprite). The splat shape comes from the texture and the RGB from
// `color`.
static Gfx *luaDrawSprite(Gfx *gdl, s32 texnum, s32 x, s32 y, s32 w, s32 h, u32 color)
{
	if (!g_TexWallhitConfigs || texnum < 0 || texnum >= LUA_WALLHIT_TEXCOUNT) {
		return gdl;
	}
	return luaDrawTexConfig(gdl, &g_TexWallhitConfigs[texnum],
			g_TexWallhitConfigs[texnum].width, g_TexWallhitConfigs[texnum].height,
			x, y, w, h, color);
}
#endif

Gfx *luaHudRender(Gfx *gdl)
{
#ifndef PLATFORM_N64
	s32 i, w;

	/* Lua off, suspended, or no state yet: nothing was queued and nobody
	 * listens. */
	if (!g_LuaAiEnabled || luaaiIsSuspended() || !luaaiGetState()) {
		g_LuaXrayCount = 0;
		return gdl;
	}

	if (!g_FontHandelGothicXs || !g_CharsHandelGothicXs) {
		g_LuaXrayCount = 0;
		return gdl;
	}

	/* Fire the per-frame draw event so scripts enqueue this frame's overlays
	 * (e.g. the X-ray, which reads the freshly-sampled g_LuaXray rows). */
	luaEventDispatchInts("draw", 0, NULL);

	if (g_LuaOverlayCount > 0) {
		gdl = text0f153628(gdl);

		for (i = 0; i < g_LuaOverlayCount; i++) {
			struct luaoverlay *o = &g_LuaOverlays[i];
			if (o->kind == OVL_BOX) {
				gdl = hudmsgRenderBox(gdl, o->x, o->y, o->x + o->w, o->y + o->h,
						1.f, o->color, 0.85f);
			} else if (o->kind == OVL_SPRITE) {
				gdl = luaDrawSprite(gdl, o->texnum, o->x, o->y, o->w, o->h, o->color);
			} else if (o->kind == OVL_IMAGE) {
				gdl = luaApiDrawImage(gdl, o->texnum, o->x, o->y, o->w, o->h, o->angle, o->color);
			} else {
				s32 tx = o->x, ty = o->y;
				// luaApiOverlayText: the text gags (Kai's langChaosTransform)
				// cover the Lua overlay text too (no-op when the mode is off)
				gdl = textRenderProjected(gdl, &tx, &ty, (char *)luaApiOverlayText(o->text),
						g_CharsHandelGothicXs, g_FontHandelGothicXs, (s32)o->color,
						viGetWidth(), viGetHeight(), 0, 0);
			}
		}

		gdl = text0f153780(gdl);

		/* Remove one-frame overlays now that they've been drawn; timed ones
		 * persist and are aged in luaTick. */
		w = 0;
		for (i = 0; i < g_LuaOverlayCount; i++) {
			if (!g_LuaOverlays[i].oneframe) {
				if (w != i) {
					g_LuaOverlays[w] = g_LuaOverlays[i];
				}
				w++;
			}
		}
		g_LuaOverlayCount = w;
	}

	/* The overlay's frame-rate and vertex-pool readouts. */
	gdl = luaMenusHudRender(gdl);

	/* X-ray rows are consumed each frame; AI re-fills them next tick. */
	g_LuaXrayCount = 0;
#endif
	return gdl;
}
