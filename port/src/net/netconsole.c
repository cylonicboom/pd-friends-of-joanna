/**
 * The server console: a Quake-style command line on the server's terminal
 * (`--console`, read from stdin once a frame) and in the host's fojOS chat
 * line (a line starting with '/'). Both feed one small dispatcher.
 *
 * Replies go out as chat: on a host they are logged, pushed into the chat
 * ring and broadcast to every client (netChatPrintf), so the terminal, the
 * host's chat window and every client's chat window show the same lines.
 * Offline they are logged and pushed into the local ring only.
 *
 * Every line arrives with an ORIGIN (struct netconorigin): the terminal and
 * the host's own chat line are root and may run anything; a remote player's
 * line (CLC_CONSOLE, not yet ported) may run only the player verbs; the
 * intercom (c-net-api-port) may run only what `Api.Allow` lists, and a few
 * verbs are a hard floor no allow-list opens (lua, quit). netConAllowed()
 * is the one place that rule lives. Replies follow the origin: broadcast as
 * chat for root, to that one client for a client, to the origin's reply
 * callback for the intercom. The terminal is the server's own; whoever has
 * it already has a shell on the box.
 *
 * This is fojo's set, not kai's netConsoleCommand (net.c), which has no
 * callers here and assumes features that were not ported.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <poll.h>
#include <strings.h>
#include <unistd.h>
#endif
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "game/lang.h"
#include "game/stagetable.h"
#include "game/luaai.h"
#include "lib/main.h"
#include "system.h"
#include "config.h"
#include "net/net.h"

#define NETCON_LINE 256

extern s32 g_MainChangeToStageNum; // lib/main.c

static s32 s_conInit = 0;
static s32 s_conOn = 0;
static char s_conBuf[NETCON_LINE];
static s32 s_conLen = 0;

// the origin of the line being run right now; NULL outside netConsoleExecFrom
static struct netconorigin *s_conFrom = NULL;

// Api.Allow: the verbs the intercom origin may run, comma separated. The
// default names verbs that do not exist yet (the verdict and standing-list
// slices); until they land the intercom can run nothing but what is listed.
char g_NetConApiAllow[128] = "verdict,standing";

// the verb table: name, who may run it, help text. CONV_PLAYER verbs are any
// origin's; CONV_FLOOR verbs are root's whatever an allow-list says; the
// rest are root's, or an origin's when its allow-list names them.
#define CONV_PLAYER 0x1
#define CONV_FLOOR  0x2
struct netconverb {
	const char *name;
	u32 flags;
};
static const struct netconverb s_conVerbs[] = {
	{ "help",       CONV_PLAYER },
	{ "?",          CONV_PLAYER },
	{ "status",     0 },
	{ "players",    0 },
	{ "kick",       0 },
	{ "say",        0 },
	{ "stage",      0 },
	{ "roles",      0 },
	{ "diff",       0 },
	{ "ready",      CONV_PLAYER },
	{ "unready",    CONV_PLAYER },
	{ "captain",    CONV_PLAYER }, // `captain key [new]` is the player's own; `captain standing ...` re-checks as `standing`
	{ "standing",   0 },           // list | reload | add <who> | rm <who>; Api.Allow's default opens it for the sidecar
	{ "start",      0 },
	{ "verdict",    0 },
	{ "endmission", CONV_FLOOR },
	{ "lua",        CONV_FLOOR },
	{ "quit",       CONV_FLOOR },
};

static const struct netconverb *netConFindVerb(const char *cmd)
{
	for (u32 i = 0; i < ARRAYCOUNT(s_conVerbs); i++) {
		if (strcmp(s_conVerbs[i].name, cmd) == 0) {
			return &s_conVerbs[i];
		}
	}
	return NULL;
}

// is `verb` in the comma-separated allow-list `list`? exact, case-insensitive
static s32 netConListHas(const char *list, const char *verb)
{
	const size_t n = strlen(verb);
	const char *p = list;

	while (*p) {
		while (*p == ',' || *p == ' ') {
			p++;
		}
		const char *e = p;
		while (*e && *e != ',' && *e != ' ') {
			e++;
		}
		if ((size_t)(e - p) == n && strncasecmp(p, verb, n) == 0) {
			return 1;
		}
		p = e;
	}
	return 0;
}

/**
 * May this origin run this verb? Root may run anything. The floor verbs are
 * root's only. Player verbs are anyone's. Beyond that, a client may run
 * nothing (Captain.Allow is the captain slice's to add) and the intercom
 * may run what Api.Allow lists. An unknown verb is nobody's but root's, so
 * the unknown-command reply still comes from the root path.
 */
s32 netConAllowed(const struct netconorigin *from, const char *verb)
{
	const struct netconverb *v = netConFindVerb(verb);

	if (!from || from->kind == NETCON_ROOT) {
		return 1;
	}
	if (!v || (v->flags & CONV_FLOOR)) {
		return 0;
	}
	if (v->flags & CONV_PLAYER) {
		return 1;
	}
	switch (from->kind) {
	case NETCON_API:
		return netConListHas(g_NetConApiAllow, verb);
	case NETCON_CLIENT:
	default:
		return 0;
	}
}

static void netConPrintf(const char *fmt, ...)
{
	char tmp[400];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);

	// the intercom gets its replies back on its own channel, never as chat
	if (s_conFrom && s_conFrom->kind == NETCON_API) {
		if (s_conFrom->reply) {
			s_conFrom->reply(s_conFrom->ctx, tmp);
		}
		return;
	}

	if (g_NetMode == NETMODE_SERVER && g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_LOBBY) {
		// a remote player's line is answered to that player alone
		struct netclient *dst = (s_conFrom && s_conFrom->kind == NETCON_CLIENT) ? s_conFrom->cl : NULL;
		netChatPrintf(dst, "[server] %s", tmp);
	} else {
		char line[420];
		snprintf(line, sizeof(line), "[server] %s", tmp);
		sysLogPrintf(LOG_CHAT, "%s", line);
		netChatLogPush(line);
	}
}

static s32 netConIsHost(void)
{
	if (g_NetMode != NETMODE_SERVER) {
		netConPrintf("not hosting");
		return 0;
	}
	return 1;
}

static s32 netConInHub(void)
{
	return g_StageNum == STAGE_CITRAINING && g_MainChangeToStageNum < 0;
}

// langGet's stage names carry '\n' width markers; one line here
static void netConStageName(s32 idx, char *out, s32 outlen)
{
	char a[64], b[64];

	snprintf(a, sizeof(a), "%s", langGet(g_SoloStages[idx].name1));
	snprintf(b, sizeof(b), "%s", langGet(g_SoloStages[idx].name2));
	for (char *p = a; *p; p++) if (*p == '\n') *p = ' ';
	for (char *p = b; *p; p++) if (*p == '\n') *p = ' ';
	snprintf(out, outlen, "%s %s", a, b);
	// trim trailing spaces the markers left
	for (s32 n = strlen(out); n > 0 && out[n - 1] == ' '; n--) {
		out[n - 1] = '\0';
	}
}

static const char *netConDiffName(s32 diff)
{
	switch (diff) {
	case DIFF_A:  return "agent";
	case DIFF_SA: return "special agent";
	case DIFF_PA: return "perfect agent";
	case DIFF_PD: return "perfect dark";
	}
	return "?";
}

static char netConRoleLetter(u8 role)
{
	switch (role) {
	case PLAYERROLE_BOND: return 'b';
	case PLAYERROLE_COOP: return 'c';
	case PLAYERROLE_ANTI: return 'a';
	}
	return 'n';
}

static void netConRolesString(char *out, s32 outlen)
{
	s32 n = 0;
	for (s32 i = 0; i < MAX_PLAYERS && n + 2 < outlen; i++) {
		if (i) {
			out[n++] = ',';
		}
		out[n++] = netConRoleLetter(g_NetTeamRoles[i]);
	}
	out[n] = '\0';
}

static struct netclient *netConFindClient(const char *arg)
{
	char *end;
	const long id = strtol(arg, &end, 10);
	const s32 byid = (*arg && *end == '\0');

	for (s32 i = 1; i < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state < CLSTATE_CONNECTING || cl == g_NetLocalClient) {
			continue;
		}
		if (byid ? (cl->id == (u32)id) : (strcasecmp(cl->settings.name, arg) == 0)) {
			return cl;
		}
	}
	return NULL;
}

static const char *netConStateName(u32 state)
{
	switch (state) {
	case CLSTATE_CONNECTING: return "connecting";
	case CLSTATE_AUTH:       return "auth";
	case CLSTATE_LOBBY:      return "hub";
	case CLSTATE_GAME:       return "in mission";
	}
	return "?";
}

static void netConHelp(void)
{
	netConPrintf("status | players | kick <id|name> | say <text>");
	netConPrintf("stage [n|name|list] | roles [b,c,a,n] | diff [0-3] | start | ready | unready");
	netConPrintf("verdict <joinid> accept|deny [account:<x>] [owns-uuid] [reason] | endmission | lua <code> | quit");
	netConPrintf("captain key [new] | standing [list|reload|add <id|name|principal>|rm <principal>]");
}

// `standing list|reload|add|rm` - the two-source standing list (netcaptain.c).
// `captain standing ...` is the same verb; netConCaptain re-checks the rights
// as `standing` before handing over, so the player-level `captain` does not
// smuggle it in.
static void netConStanding(const char *arg)
{
	char sub[16], rest[NET_ACCOUNT_LEN + 16];
	s32 n = 0;

	while (*arg && *arg != ' ' && n < (s32)sizeof(sub) - 1) {
		sub[n++] = (char)tolower((unsigned char)*arg++);
	}
	sub[n] = '\0';
	while (*arg == ' ') {
		arg++;
	}
	snprintf(rest, sizeof(rest), "%s", arg);

	if (!*sub || strcmp(sub, "list") == 0) {
		char e[NET_ACCOUNT_LEN + 16];
		s32 fromfile, i = 0;
		while (netCaptainStandingEntry(i, e, sizeof(e), &fromfile)) {
			netConPrintf("%s%s", e, fromfile ? "  (file)" : "");
			i++;
		}
		netConPrintf("%d standing captain%s", i, i == 1 ? "" : "s");
		return;
	}
	if (strcmp(sub, "reload") == 0) {
		const s32 c = netCaptainStandingReload();
		netConPrintf("reloaded %d", c);
		return;
	}
	if (strcmp(sub, "add") == 0 || strcmp(sub, "rm") == 0) {
		if (!netConIsHost()) {
			return;
		}
		if (!*rest) {
			netConPrintf("standing %s: who? a player (id or name) or a principal (pk:... / account:...)", sub);
			return;
		}
		char principal[NET_ACCOUNT_LEN + 16];
		if (strncmp(rest, "pk:", 3) == 0 || strncmp(rest, "account:", 8) == 0 || strncmp(rest, "uuid:", 5) == 0) {
			snprintf(principal, sizeof(principal), "%s", rest);
		} else {
			struct netclient *cl = netConFindClient(rest);
			if (!cl) {
				netConPrintf("standing %s: no player %s (players lists them)", sub, rest);
				return;
			}
			if (!netCaptainPrincipalFor(cl, principal, sizeof(principal))) {
				netConPrintf("standing %s: %s has no key and no account - they run `captain key new` and rejoin", sub, cl->settings.name);
				return;
			}
		}
		const s32 r = strcmp(sub, "add") == 0 ? netCaptainStandingAdd(principal) : netCaptainStandingRm(principal);
		if (r < 0 && s_conFrom) {
			s_conFrom->ok = 0; // the intercom's reply says refused
		}
		if (strcmp(sub, "add") == 0) {
			netConPrintf(r > 0 ? "standing: added %s" : r == 0 ? "standing: %s is already listed" : "standing: refused %s (the log says why)", principal);
		} else {
			netConPrintf(r > 0 ? "standing: removed %s" : r == 0 ? "standing: %s is not in the ini list (a file entry goes by the file)" : "standing: refused %s (the log says why)", principal);
		}
		return;
	}
	netConPrintf("standing: list | reload | add <id|name|principal> | rm <principal>");
}

// `captain key [new]`: show or rotate this machine's captain key. The rest of
// the captain verbs (appoint, veto, standing ...) land with c-net-captain.
static void netConCaptain(const char *arg)
{
	char sub[16], rest[32];
	s32 n = 0;

	while (*arg && *arg != ' ' && n < (s32)sizeof(sub) - 1) {
		sub[n++] = (char)tolower((unsigned char)*arg++);
	}
	sub[n] = '\0';
	while (*arg == ' ') {
		arg++;
	}
	snprintf(rest, sizeof(rest), "%s", arg);

	if (strcmp(sub, "standing") == 0) {
		// the sidecar's nudge is spelled `captain standing reload`; same rights as `standing`
		if (!netConAllowed(s_conFrom, "standing")) {
			if (s_conFrom) {
				s_conFrom->ok = 0;
			}
			netConPrintf("standing: not yours to run");
			return;
		}
		netConStanding(arg);
		return;
	}
	if (strcmp(sub, "key") != 0) {
		netConPrintf("captain: `captain key [new]` or `captain standing ...`");
		return;
	}
	if (strcmp(rest, "new") == 0) {
		if (!netCaptainKeyNew()) {
			netConPrintf("captain key: could not mint one (no random source)");
			return;
		}
		netConPrintf("captain key rotated - rejoin for it to count");
	} else if (*rest) {
		netConPrintf("captain key: `captain key` shows it, `captain key new` rotates it");
		return;
	}
	netCaptainKeyLoad();
	{
		u8 pk[32], sig[64];
		u8 zero[32] = { 0 };
		char b64[45];
		if (netCaptainKeySign(zero, zero, pk, sig)) {
			netPkBase64(pk, b64);
			netConPrintf("captain key pk:%s", b64);
		} else {
			netConPrintf("captain key: none");
		}
	}
}

static void netConStatus(void)
{
	char name[96], roles[16];
	const s32 idx = g_NetTeamStageIndex;

	netConRolesString(roles, sizeof(roles));
	if (idx >= 0 && idx < NUM_SOLOSTAGES) {
		netConStageName(idx, name, sizeof(name));
	} else {
		snprintf(name, sizeof(name), "none");
	}

	netConPrintf("%s, port %u, %d client(s)",
			g_NetMode == NETMODE_SERVER ? "hosting" : g_NetMode == NETMODE_CLIENT ? "client" : "offline",
			g_NetServerPort, g_NetNumClients);
	netConPrintf("stage %d (%s), %s, roles %s", idx, name, netConDiffName(g_NetTeamDifficulty), roles);
	netConPrintf("%s", netConInHub() ? "in the hub" : "in a mission");
}

static void netConPlayers(void)
{
	s32 any = 0;

	netTeamLobbyBuild();

	for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		u16 ping = 0;

		if (cl->state < CLSTATE_CONNECTING) {
			continue;
		}
		for (s32 j = 0; j < g_NetTeamLobby.numclients; j++) {
			if (g_NetTeamLobby.clients[j].id == cl->id) {
				ping = g_NetTeamLobby.clients[j].ping;
			}
		}
		netConPrintf("#%u %s - %s, seat %d, %ums%s%s", cl->id, cl->settings.name,
				netConStateName(cl->state), cl->playernum, ping,
				cl == g_NetLocalClient ? " (host)" : "",
				g_NetMode == NETMODE_SERVER && cl != g_NetLocalClient && netCaptainIsStanding(cl) ? " [standing captain]" : "");
		any = 1;
	}

	if (!any) {
		netConPrintf("nobody");
	}
}

// case-insensitive "needle anywhere in hay"
static s32 netConStrCaseHas(const char *hay, const char *needle)
{
	const size_t n = strlen(needle);
	for (; *hay; hay++) {
		if (strncasecmp(hay, needle, n) == 0) {
			return 1;
		}
	}
	return 0;
}

/**
 * A stage argument is a solo stage index ("0"), a STAGE_* row name
 * ("STAGE_EXTRACTION", case-insensitive), or any part of the display name
 * ("defection", "dataDyne"). Names, not numbers, are the identity of anything
 * mods fill in (her 2026-10-29 requirement); the solo list is vanilla-fixed
 * today, so this is the console learning the habit before it matters.
 * Returns the solo stage index or -1.
 */
static s32 netConStageLookup(const char *arg)
{
	char name[96];
	s32 alldigits = *arg != '\0';

	for (const char *p = arg; *p; p++) {
		if (!isdigit((unsigned char)*p)) {
			alldigits = 0;
		}
	}
	if (alldigits) {
		return atoi(arg);
	}

	const s32 row = stageGetIndexByName(arg);
	if (row >= 0) {
		for (s32 i = 0; i < NUM_SOLOSTAGES; i++) {
			if (g_SoloStages[i].stagenum == g_Stages[row].id) {
				return i;
			}
		}
	}

	for (s32 i = 0; i < NUM_SOLOSTAGES; i++) {
		netConStageName(i, name, sizeof(name));
		if (netConStrCaseHas(name, arg)) {
			return i;
		}
	}
	return -1;
}

static void netConStage(const char *arg)
{
	char name[96];

	if (strcmp(arg, "list") == 0) {
		for (s32 i = 0; i < NUM_SOLOSTAGES; i++) {
			if (netTeamStageUnlocked(i)) {
				netConStageName(i, name, sizeof(name));
				netConPrintf("%d %s", i, name);
			}
		}
		return;
	}

	if (*arg) {
		if (!netConIsHost()) {
			return;
		}
		const s32 idx = netConStageLookup(arg);
		if (idx < 0) {
			netConPrintf("stage: no stage called '%s' (stage list)", arg);
			return;
		}
		if (!netTeamSetStage(idx)) {
			netConPrintf("stage %d is locked or out of range (stage list)", idx);
			return;
		}
	}

	const s32 cur = g_NetTeamStageIndex;
	if (cur >= 0 && cur < NUM_SOLOSTAGES) {
		netConStageName(cur, name, sizeof(name));
		netConPrintf("stage %d %s", cur, name);
	} else {
		netConPrintf("no stage picked");
	}
}

static void netConRoles(const char *arg)
{
	char roles[16];

	if (*arg) {
		if (!netConIsHost()) {
			return;
		}
		// one letter per seat, as --team-roles: exactly one b (the operative,
		// any seat), and the host's seat (the first) needs a role
		u8 want[MAX_PLAYERS];
		char why[64];
		s32 seat = 0;
		memset(want, PLAYERROLE_NONE, sizeof(want));
		for (const char *p = arg; *p; p++) {
			u8 role;
			switch (tolower((unsigned char)*p)) {
			case ',': case ' ': continue;
			case 'b': case 'o': role = PLAYERROLE_BOND; break;
			case 'c': role = PLAYERROLE_COOP; break;
			case 'a': role = PLAYERROLE_ANTI; break;
			case 'n': role = PLAYERROLE_NONE; break;
			default:
				netConPrintf("roles: '%c' is not b, c, a or n", *p);
				return;
			}
			if (seat >= MAX_PLAYERS) {
				netConPrintf("roles: %d seats at most", MAX_PLAYERS);
				return;
			}
			want[seat++] = role;
		}
		if (!netTeamSetRoles(want, why, sizeof(why))) {
			netConPrintf("roles: %s", why);
			return;
		}
	}

	netConRolesString(roles, sizeof(roles));
	netConPrintf("roles %s", roles);
}

static void netConDiff(const char *arg)
{
	if (*arg) {
		if (!netConIsHost()) {
			return;
		}
		const s32 d = atoi(arg);
		if (d < DIFF_A || d > DIFF_PD || !isdigit((unsigned char)arg[0])) {
			netConPrintf("diff: 0 agent, 1 special agent, 2 perfect agent, 3 perfect dark");
			return;
		}
		netTeamSetDifficulty(d);
	}
	netConPrintf("difficulty %d (%s)", g_NetTeamDifficulty, netConDiffName(g_NetTeamDifficulty));
}

static void netConStart(void)
{
	char why[96];

	if (!netConIsHost()) {
		return;
	}
	if (!netTeamCanStart(why, sizeof(why))) {
		netConPrintf("can't start: %s", why);
		return;
	}
	netConPrintf("starting");
	netTeamStart();
}

/**
 * Run one console line as root. fromchat: it came from the local chat line,
 * so it needs the leading '/' and runs only on the host. From the terminal
 * the slash is optional. Returns 1 when the line was taken as a command.
 */
s32 netConsoleExec(const char *line, s32 fromchat)
{
	struct netconorigin from = { NETCON_ROOT, fromchat, NULL, NULL, NULL, 1 };
	return netConsoleExecFrom(line, &from);
}

/**
 * Run one console line from an origin. The slash is required when the line
 * came from a chat box (from->fromchat), optional otherwise. Rights are
 * netConAllowed()'s; replies follow the origin (netConPrintf). Returns 1 when
 * the line was taken as a command, whether or not it was allowed; from->ok
 * says whether it ran (0: refused or unknown).
 */
s32 netConsoleExecFrom(const char *line, struct netconorigin *from)
{
	char cmd[32];
	s32 ci = 0;
	const char *p = line;
	const s32 fromchat = from && from->fromchat;
	struct netconorigin *prevfrom = s_conFrom;
	const s32 r = 1;

	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p == '/') {
		p++;
	} else if (fromchat) {
		return 0;
	}
	if (*p == '\0') {
		return 1;
	}

	while (*p && *p != ' ' && *p != '\t' && ci < (s32)sizeof(cmd) - 1) {
		cmd[ci++] = (char)tolower((unsigned char)*p++);
	}
	cmd[ci] = '\0';
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	const char *arg = p;

	// ready / unready are the player's own; everything else is the host's
	const s32 playercmd = strcmp(cmd, "ready") == 0 || strcmp(cmd, "unready") == 0;
	if (fromchat && g_NetMode == NETMODE_CLIENT && !playercmd) {
		netChatLogPush("commands are host only - /ready and /unready are yours");
		return 1;
	}

	s_conFrom = from;
	from->ok = 1;

	if (!netConAllowed(from, cmd)) {
		from->ok = 0;
		if (netConFindVerb(cmd)) {
			netConPrintf("%s: not yours to run", cmd);
		} else {
			netConPrintf("unknown command %s (help lists them)", cmd);
		}
		s_conFrom = prevfrom;
		return r;
	}

	if (strcmp(cmd, "say") == 0) {
		if (*arg) {
			netConPrintf("%s", arg);
		}
		s_conFrom = prevfrom;
		return r;
	}

	// echo, so everyone reading the chat sees what was done; the intercom
	// knows what it sent
	if (!from || from->kind != NETCON_API) {
		netConPrintf("> %s%s%s", cmd, *arg ? " " : "", arg);
	}

	if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
		netConHelp();
	} else if (strcmp(cmd, "status") == 0) {
		netConStatus();
	} else if (strcmp(cmd, "players") == 0) {
		netConPlayers();
	} else if (strcmp(cmd, "kick") == 0) {
		if (!netConIsHost()) {
			s_conFrom = prevfrom;
			return r;
		}
		struct netclient *cl = *arg ? netConFindClient(arg) : NULL;
		if (!cl) {
			netConPrintf("kick: no client %s (players lists them)", *arg ? arg : "named");
		} else {
			netConPrintf("kicking #%u %s", cl->id, cl->settings.name);
			netServerKick(cl, DISCONNECT_KICKED);
		}
	} else if (strcmp(cmd, "stage") == 0) {
		netConStage(arg);
	} else if (strcmp(cmd, "roles") == 0) {
		netConRoles(arg);
	} else if (strcmp(cmd, "diff") == 0) {
		netConDiff(arg);
	} else if (strcmp(cmd, "captain") == 0) {
		netConCaptain(arg);
	} else if (strcmp(cmd, "standing") == 0) {
		netConStanding(arg);
	} else if (playercmd) {
		if (!g_NetMode) {
			netConPrintf("not in a network game");
		} else if (!netConInHub()) {
			netConPrintf("ready is for the lobby");
		} else {
			netTeamSetReady(strcmp(cmd, "ready") == 0);
		}
	} else if (strcmp(cmd, "start") == 0) {
		netConStart();
	} else if (strcmp(cmd, "verdict") == 0) {
		char why[160];
		if (!netConIsHost()) {
			s_conFrom = prevfrom;
			return r;
		}
		if (!netVerifyVerdict(arg, why, sizeof(why))) {
			from->ok = 0;
		}
		netConPrintf("%s", why);
	} else if (strcmp(cmd, "endmission") == 0) {
		if (netConInHub()) {
			netConPrintf("not in a mission");
		} else if (g_NetMode == NETMODE_CLIENT) {
			netConPrintf("the host ends the mission");
		} else {
			mainEndStage();
		}
	} else if (strcmp(cmd, "lua") == 0) {
		luaaiConsoleCommand(*arg ? arg : NULL);
	} else if (strcmp(cmd, "quit") == 0) {
		netConPrintf("shutting down");
		exit(0); // what SIGTERM does (main.c handleTermSignal)
	} else {
		from->ok = 0;
		netConPrintf("unknown command %s (help lists them)", cmd);
	}

	s_conFrom = prevfrom;
	return r;
}

PD_CONSTRUCTOR static void netConsoleConfigInit(void)
{
	configRegisterString("Api.Allow", g_NetConApiAllow, sizeof(g_NetConApiAllow) - 1);
}

/**
 * Once a frame: take whatever whole lines the terminal has typed. Off unless
 * --console; it turns itself off when stdin closes (a server started with
 * </dev/null, or the terminal went away).
 */
void netConsoleTick(void)
{
#ifndef _WIN32
	if (!s_conInit) {
		s_conInit = 1;
		s_conOn = sysArgCheck("--console");
		if (s_conOn) {
			sysLogPrintf(LOG_NOTE, "console: reading commands from the terminal - help lists them");
		}
	}

	while (s_conOn) {
		struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
		char chunk[128];

		if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & (POLLIN | POLLHUP))) {
			return;
		}

		const ssize_t n = read(STDIN_FILENO, chunk, sizeof(chunk));
		if (n <= 0) {
			s_conOn = 0;
			sysLogPrintf(LOG_NOTE, "console: stdin closed, console off");
			return;
		}

		for (ssize_t i = 0; i < n; i++) {
			const char c = chunk[i];
			if (c == '\n' || c == '\r') {
				s_conBuf[s_conLen] = '\0';
				if (s_conLen) {
					netConsoleExec(s_conBuf, 0);
				}
				s_conLen = 0;
			} else if (s_conLen < NETCON_LINE - 1) {
				s_conBuf[s_conLen++] = c;
			}
		}
	}
#endif
}
