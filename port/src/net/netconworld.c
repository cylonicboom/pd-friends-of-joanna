/**
 * The console's world verbs: what fojo-tui's map reads and does.
 *
 *   state [from N]                 who is where, paged
 *   list rooms|pads|chrs [from N]  catalogs, paged
 *   teleport <anchor>              move the issuer's own player
 *   order <chr> goto <anchor>      queue a step for a chr (chrorder.c)
 *   order <chr> wait               ... and hold when the queue is done
 *   order <chr> release            drop its orders; its ailist resumes
 *   order <chr>                    what it has queued
 *   order release all
 *
 * An anchor is the bar's spelling: `pad 0x..`, `chr 0x..`, `room 0x..`, or
 * `pos <x> <y> <z>`. Numbers take 0x or decimal.
 *
 * Replies are lines, not JSON: the intercom wraps the whole reply in one
 * JSON string and the datagram is 4K, so a line format with no quoting
 * fits two or three times the rows. Anything that can outgrow a datagram
 * is paged: when a reply stops short its last line is `more <N>`, and the
 * same verb with `from <N>` carries on.
 *
 *   state:  `stage <num> me <playernum|-1> chrs <count>`, then one line per
 *           chr: `c <chrnum> <x> <y> <z> <room> <action> <side>[<order>]`
 *           side is `p<N>` for player N, else `f` friend / `h` hostile /
 *           `n` non-combatant, judged against the issuer's own player; order
 *           is `o` running orders, `w` holding after a wait.
 *   rooms:  `r <id> <x> <y> <z>` (the room's centre)
 *   pads:   `p <id> <room> <x> <y> <z>`
 *   chrs:   `c <chrnum> <body> <head> <team>`
 *
 * Who may run what is netConAllowed's (netconsole.c). Which chrs a client
 * may order is decided here, in netConWorldMayOrder: a chr on the same team
 * as that client's own player, never a player. That is a placeholder for
 * the campaign's control model, which is not this file's to decide; root
 * and the intercom on the host may order any chr that is not a player.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <ultra64.h>
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "game/chraction.h"
#include "game/modspectate.h"
#include "game/pad.h"
#include "game/playermgr.h"
#include "system.h"
#include "net/net.h"
#include "chrorder.h"

// what a reply may hold before it pages: the intercom's datagram is 4096
// and every '\n' doubles when the reply is JSON-escaped
#define NETCONWORLD_BUDGET 3300

static s32 s_worldUsed = 0;

// a refusal: the line is answered but did not run (ok=false on the intercom)
static void netConWorldNo(const char *fmt, ...)
{
	char tmp[200];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (netConOrigin()) {
		netConOrigin()->ok = 0;
	}
	netConPrintf("%s", tmp);
}

// one reply line, counted against the page budget; 0 when the page is full
static s32 netConWorldLine(const char *fmt, ...)
{
	char tmp[160];
	va_list ap;

	va_start(ap, fmt);
	const s32 n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);

	if (s_worldUsed + n + 2 > NETCONWORLD_BUDGET) {
		return 0;
	}
	s_worldUsed += n + 2;
	netConPrintf("%s", tmp);
	return 1;
}

// skip spaces; then if the next word is `word`, step over it and return 1
static s32 netConWorldEat(const char **p, const char *word)
{
	const char *s = *p;
	const size_t n = strlen(word);
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	if (strncasecmp(s, word, n) == 0 && (s[n] == '\0' || s[n] == ' ' || s[n] == '\t')) {
		*p = s + n;
		return 1;
	}
	return 0;
}

static s32 netConWorldNum(const char **p, s32 *out)
{
	char *end;
	while (**p == ' ' || **p == '\t') {
		(*p)++;
	}
	const long v = strtol(*p, &end, 0);
	if (end == *p) {
		return 0;
	}
	*out = (s32)v;
	*p = end;
	return 1;
}

static s32 netConWorldFloat(const char **p, f32 *out)
{
	char *end;
	while (**p == ' ' || **p == '\t') {
		(*p)++;
	}
	const f32 v = strtof(*p, &end);
	if (end == *p) {
		return 0;
	}
	*out = v;
	*p = end;
	return 1;
}

// `from N` at the end of a paged verb; 0 when absent
static s32 netConWorldFrom(const char *arg)
{
	s32 from = 0;
	const char *p = arg;
	if (netConWorldEat(&p, "from")) {
		netConWorldNum(&p, &from);
	}
	return from < 0 ? 0 : from;
}

static s32 netConWorldInStage(void)
{
	if (!g_ChrSlots || !g_NumChrSlots || !g_Vars.players[0]) {
		netConWorldNo("not in a stage");
		return 0;
	}
	return 1;
}

static struct chrdata *netConWorldChr(s32 chrnum)
{
	for (s32 i = 0; i < g_NumChrSlots; i++) {
		if (g_ChrSlots[i].chrnum == chrnum && g_ChrSlots[i].prop) {
			return &g_ChrSlots[i];
		}
	}
	return NULL;
}

/**
 * The player a line speaks for: a client's own player for a client's line,
 * else this machine's own player. -1 for a spectator or a dedicated host
 * with no pawn worth moving.
 */
static s32 netConWorldPlayerNum(void)
{
	const struct netconorigin *from = netConOrigin();

	if (from && from->kind == NETCON_CLIENT) {
		if (!from->cl || from->cl->playernum == NET_PLAYERNUM_SPECTATOR || !from->cl->player) {
			return -1;
		}
		return from->cl->playernum;
	}
	if (g_NetMode && g_NetLocalClient) {
		if (g_NetLocalClient->playernum == NET_PLAYERNUM_SPECTATOR || !g_NetLocalClient->player) {
			return -1;
		}
		return g_NetLocalClient->playernum;
	}
	return g_Vars.players[0] ? 0 : -1;
}

static struct chrdata *netConWorldPlayerChr(s32 pn)
{
	if (pn < 0 || pn >= MAX_PLAYERS || !g_Vars.players[pn] || !g_Vars.players[pn]->prop) {
		return NULL;
	}
	return g_Vars.players[pn]->prop->chr;
}

static char netConWorldSide(struct chrdata *me, struct chrdata *chr, s32 *playernum)
{
	*playernum = -1;
	if (chr->prop->type == PROPTYPE_PLAYER) {
		*playernum = playermgrGetPlayerNumByProp(chr->prop);
		return 'p';
	}
	if (chr->team & TEAM_NONCOMBAT) {
		return 'n';
	}
	if (me && chrCompareTeams(me, chr, COMPARE_FRIENDS)) {
		return 'f';
	}
	return 'h';
}

// the console spelling of an anchor, resolved to where it is
struct netconanchor {
	s32 kind; // CHRORDER_GOTO_* (pad, pos, chr)
	s32 id;
	struct coord pos;
	struct prop *prop; // a chr anchor's prop, for landing beside it
};

static s32 netConWorldAnchor(const char *arg, struct netconanchor *a)
{
	const char *p = arg;

	memset(a, 0, sizeof(*a));

	if (netConWorldEat(&p, "pad")) {
		struct pad pad;
		if (!netConWorldNum(&p, &a->id) || !g_PadsFile || a->id < 0 || a->id >= g_PadsFile->numpads) {
			netConWorldNo("no such pad");
			return 0;
		}
		padUnpack(a->id, PADFIELD_POS | PADFIELD_ROOM, &pad);
		a->kind = CHRORDER_GOTO_PAD;
		a->pos = pad.pos;
		return 1;
	}
	if (netConWorldEat(&p, "chr")) {
		struct chrdata *chr;
		if (!netConWorldNum(&p, &a->id) || !(chr = netConWorldChr(a->id))) {
			netConWorldNo("no such chr");
			return 0;
		}
		a->kind = CHRORDER_GOTO_CHR;
		a->pos = chr->prop->pos;
		a->prop = chr->prop;
		return 1;
	}
	if (netConWorldEat(&p, "room")) {
		if (!netConWorldNum(&p, &a->id) || !g_Rooms || a->id <= 0 || a->id >= g_Vars.roomcount) {
			netConWorldNo("no such room");
			return 0;
		}
		a->kind = CHRORDER_GOTO_POS;
		a->pos = g_Rooms[a->id].centre;
		return 1;
	}
	if (netConWorldEat(&p, "pos")) {
		if (!netConWorldFloat(&p, &a->pos.x) || !netConWorldFloat(&p, &a->pos.y) || !netConWorldFloat(&p, &a->pos.z)) {
			netConWorldNo("pos wants three numbers");
			return 0;
		}
		a->kind = CHRORDER_GOTO_POS;
		return 1;
	}
	netConWorldNo("an anchor is pad <n>, chr <n>, room <n> or pos <x> <y> <z>");
	return 0;
}

void netConState(const char *arg)
{
	if (!netConWorldInStage()) {
		return;
	}

	const s32 from = netConWorldFrom(arg);
	const s32 pn = netConWorldPlayerNum();
	struct chrdata *me = netConWorldPlayerChr(pn >= 0 ? pn : 0);
	s32 count = 0;

	for (s32 i = 0; i < g_NumChrSlots; i++) {
		if (g_ChrSlots[i].chrnum >= 0 && g_ChrSlots[i].prop) {
			count++;
		}
	}

	s_worldUsed = 0;
	netConWorldLine("stage %d me %d chrs %d", g_StageNum, pn, count);

	for (s32 i = from; i < g_NumChrSlots; i++) {
		struct chrdata *chr = &g_ChrSlots[i];
		if (chr->chrnum < 0 || !chr->prop) {
			continue;
		}
		s32 cpn;
		const char side = netConWorldSide(me, chr, &cpn);
		const char ord = chrOrderState(chr);
		char sidestr[8];
		if (side == 'p') {
			snprintf(sidestr, sizeof(sidestr), "p%d", cpn);
		} else {
			snprintf(sidestr, sizeof(sidestr), "%c%s", side, ord ? (ord == 'o' ? "o" : "w") : "");
		}
		if (!netConWorldLine("c %d %d %d %d %d %d %s", chr->chrnum,
					(s32)chr->prop->pos.x, (s32)chr->prop->pos.y, (s32)chr->prop->pos.z,
					chr->prop->rooms[0], chr->actiontype, sidestr)) {
			netConPrintf("more %d", i);
			return;
		}
	}
}

void netConList(const char *arg)
{
	const char *p = arg;
	s32 i;

	if (!netConWorldInStage()) {
		return;
	}
	s_worldUsed = 0;

	if (netConWorldEat(&p, "rooms")) {
		const s32 from = netConWorldFrom(p);
		for (i = from > 1 ? from : 1; i < g_Vars.roomcount; i++) {
			const struct coord *c = &g_Rooms[i].centre;
			if (!netConWorldLine("r %d %d %d %d", i, (s32)c->x, (s32)c->y, (s32)c->z)) {
				netConPrintf("more %d", i);
				return;
			}
		}
	} else if (netConWorldEat(&p, "pads")) {
		const s32 from = netConWorldFrom(p);
		const s32 numpads = g_PadsFile ? g_PadsFile->numpads : 0;
		for (i = from; i < numpads; i++) {
			struct pad pad;
			padUnpack(i, PADFIELD_POS | PADFIELD_ROOM, &pad);
			if (!netConWorldLine("p %d %d %d %d %d", i, pad.room, (s32)pad.pos.x, (s32)pad.pos.y, (s32)pad.pos.z)) {
				netConPrintf("more %d", i);
				return;
			}
		}
	} else if (netConWorldEat(&p, "chrs")) {
		const s32 from = netConWorldFrom(p);
		for (i = from; i < g_NumChrSlots; i++) {
			struct chrdata *chr = &g_ChrSlots[i];
			if (chr->chrnum < 0 || !chr->prop) {
				continue;
			}
			if (!netConWorldLine("c %d %d %d %d", chr->chrnum, chr->bodynum, chr->headnum, chr->team)) {
				netConPrintf("more %d", i);
				return;
			}
		}
	} else {
		netConWorldNo("list rooms|pads|chrs [from <n>]");
	}
}

/**
 * Move the issuer's own player: the same primitive the bar's `!` uses
 * (modSpectateTeleport / ...Near). On a host, a remote player is moved on
 * the host and force-snapped on its client the way a respawn is
 * (UCMD_FL_FORCE*, forcetick), so the client takes the host's position
 * instead of overwriting it with its own.
 */
void netConTeleport(const char *arg)
{
	struct netconanchor a;

	if (!netConWorldInStage() || !netConWorldAnchor(arg, &a)) {
		return;
	}
	if (g_NetMode == NETMODE_CLIENT) {
		netConWorldNo("teleport: the host moves players");
		return;
	}

	const s32 pn = netConWorldPlayerNum();
	if (pn < 0 || !netConWorldPlayerChr(pn)) {
		netConWorldNo("teleport: no player of yours to move");
		return;
	}

	const s32 oldpn = g_Vars.currentplayernum;
	setCurrentPlayerNum(pn);

	if (!(a.prop && modSpectateTeleportNear(a.prop))) {
		// a room centre is in the air; a pad and a pos are on the floor
		modSpectateTeleport(&a.pos, false);
	}

#ifndef PLATFORM_N64
	if (g_NetMode == NETMODE_SERVER) {
		struct player *pl = g_Vars.currentplayer;
		pl->ucmd |= UCMD_FL_FORCEPOS | UCMD_FL_FORCEGROUND;
		if (pl->isremote && pl->client) {
			pl->client->forcetick = g_NetTick;
		}
	}
#endif

	const struct coord *at = &g_Vars.currentplayer->prop->pos;
	netConPrintf("teleported player %d to (%d, %d, %d) room %d", pn,
			(s32)at->x, (s32)at->y, (s32)at->z, g_Vars.currentplayer->prop->rooms[0]);
	setCurrentPlayerNum(oldpn);
}

static s32 netConWorldMayOrder(struct chrdata *chr)
{
	const struct netconorigin *from = netConOrigin();

	if (chr->prop->type != PROPTYPE_CHR) {
		netConWorldNo("order: chr %d is a player", chr->chrnum);
		return 0;
	}
	if (from && from->kind == NETCON_CLIENT) {
		struct chrdata *me = netConWorldPlayerChr(netConWorldPlayerNum());
		if (!me || !chrCompareTeams(me, chr, COMPARE_FRIENDS)) {
			netConWorldNo("order: chr %d is not on your side", chr->chrnum);
			return 0;
		}
	}
	return 1;
}

void netConOrder(const char *arg)
{
	const char *p = arg;
	char why[80];
	s32 chrnum;

	if (!netConWorldInStage()) {
		return;
	}
	if (g_NetMode == NETMODE_CLIENT) {
		netConWorldNo("order: the host runs the chrs");
		return;
	}

	if (netConWorldEat(&p, "release")) {
		if (netConWorldEat(&p, "all")) {
			chrOrderReleaseAll();
			netConPrintf("order: every chr is back on its ailist");
			return;
		}
	}

	p = arg;
	netConWorldEat(&p, "chr");
	if (!netConWorldNum(&p, &chrnum)) {
		netConWorldNo("order <chr> goto <anchor> | wait | release   (order release all)");
		return;
	}
	struct chrdata *chr = netConWorldChr(chrnum);
	if (!chr) {
		netConWorldNo("order: no chr %d", chrnum);
		return;
	}
	if (!netConWorldMayOrder(chr)) {
		return;
	}

	if (netConWorldEat(&p, "goto")) {
		struct netconanchor a;
		struct chrorderstep step;
		if (!netConWorldAnchor(p, &a)) {
			return;
		}
		memset(&step, 0, sizeof(step));
		step.kind = (u8)a.kind;
		step.pad = a.id;
		step.target = a.id;
		step.pos = a.pos;
		if (chrOrderPush(chr, &step, why, sizeof(why))) {
			netConPrintf("order: chr %d %s", chrnum, why);
		} else {
			netConWorldNo("order: chr %d %s", chrnum, why);
		}
	} else if (netConWorldEat(&p, "wait")) {
		struct chrorderstep step;
		memset(&step, 0, sizeof(step));
		step.kind = CHRORDER_WAIT;
		if (chrOrderPush(chr, &step, why, sizeof(why))) {
			netConPrintf("order: chr %d %s, holds when done", chrnum, why);
		} else {
			netConWorldNo("order: chr %d %s", chrnum, why);
		}
	} else if (netConWorldEat(&p, "release")) {
		netConPrintf(chrOrderRelease(chr) ? "order: chr %d back on its ailist" : "order: chr %d had no orders", chrnum);
	} else {
		const char st = chrOrderState(chr);
		netConPrintf("order: chr %d %s, %d step%s queued", chrnum,
				st == 'o' ? "under orders" : st == 'w' ? "holding" : "on its ailist",
				chrOrderCount(chr), chrOrderCount(chr) == 1 ? "" : "s");
	}
}
