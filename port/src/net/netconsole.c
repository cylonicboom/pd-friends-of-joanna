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
 * Host only, except ready / unready, which any player may use: a client's
 * other '/' lines are refused locally. The terminal is the server's own;
 * whoever has it already has a shell on the box.
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
#include "game/luaai.h"
#include "lib/main.h"
#include "system.h"
#include "net/net.h"

#define NETCON_LINE 256

extern s32 g_MainChangeToStageNum; // lib/main.c

static s32 s_conInit = 0;
static s32 s_conOn = 0;
static char s_conBuf[NETCON_LINE];
static s32 s_conLen = 0;

static void netConPrintf(const char *fmt, ...)
{
	char tmp[400];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);

	if (g_NetMode == NETMODE_SERVER && g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_LOBBY) {
		netChatPrintf(NULL, "[server] %s", tmp);
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
	netConPrintf("stage [n|list] | roles [b,c,a,n] | diff [0-3] | start | ready | unready");
	netConPrintf("endmission | lua <code> | quit");
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
		netConPrintf("#%u %s - %s, seat %d, %ums%s", cl->id, cl->settings.name,
				netConStateName(cl->state), cl->playernum, ping,
				cl == g_NetLocalClient ? " (host)" : "");
		any = 1;
	}

	if (!any) {
		netConPrintf("nobody");
	}
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
		const s32 idx = atoi(arg);
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
 * Run one console line. fromchat: it came from the chat line, so it needs
 * the leading '/' and runs only on the host. From the terminal the slash
 * is optional. Returns 1 when the line was taken as a command.
 */
s32 netConsoleExec(const char *line, s32 fromchat)
{
	char cmd[32];
	s32 ci = 0;
	const char *p = line;

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

	if (strcmp(cmd, "say") == 0) {
		if (*arg) {
			netConPrintf("%s", arg);
		}
		return 1;
	}

	// echo, so everyone reading the chat sees what was done
	netConPrintf("> %s%s%s", cmd, *arg ? " " : "", arg);

	if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
		netConHelp();
	} else if (strcmp(cmd, "status") == 0) {
		netConStatus();
	} else if (strcmp(cmd, "players") == 0) {
		netConPlayers();
	} else if (strcmp(cmd, "kick") == 0) {
		if (!netConIsHost()) {
			return 1;
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
		netConPrintf("unknown command %s (help lists them)", cmd);
	}

	return 1;
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
