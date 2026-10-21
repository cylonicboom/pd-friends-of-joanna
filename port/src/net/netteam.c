/**
 * fojo: Team Missions over the wire.
 *
 * Kai's campaign co-op (netCoopEnterStage) seats N players as a single coop
 * buddy slot. fojo's Team Missions have roles per slot (g_Vars.playerroles[]:
 * operative, coop, anti) and their own player model (fojo-coop-players), so
 * a team stage start ships the roles manifest and every machine runs the same
 * accept path the Team Missions menu runs (menuhandlerAcceptMission's isteam
 * branch).
 *
 * Wire playernum is the dense g_Vars.players[] index; roles[playernum] is the
 * seat's role. The host is playernum 0, so the host is always the operative
 * (playermgrResetTeamPlayers puts PLAYERROLE_BOND in slot 0).
 *
 * CLI (host):
 *   --team <solostageindex>     arm a team mission start (0 = Defection)
 *   --team-roles b,c,a,n        role per wire slot: b=operative c=coop a=anti n=none
 *   --team-diff <0..3>          difficulty (DIFF_A..DIFF_PD)
 *   --team-wait <n>             start once n remote clients sit in the lobby
 *                               (default: number of non-none roles minus the host)
 *
 * Also the direct boot for a CLI-latched session (netDedicatedBootTick): no
 * agent file, no menus — gamefile defaults, mpInit, then host or join.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "game/gamefile.h"
#include "game/pak.h"
#include "game/pdmode.h"
#include "game/title.h"
#include "game/lv.h"
#include "game/playermgr.h"
#include "game/mplayer/mplayer.h"
#include "lib/main.h"
#include "system.h"
#include "net/net.h"
#include "net/netmsg.h"

s32 g_NetTeamStageIndex = -1;
s32 g_NetTeamDifficulty = DIFF_A;
s32 g_NetTeamWait = -1;
u8 g_NetTeamRoles[MAX_PLAYERS] = { PLAYERROLE_BOND, PLAYERROLE_COOP, PLAYERROLE_NONE, PLAYERROLE_NONE };

extern s32 g_MainChangeToStageNum; // pdmain.c

static u32 s_teamArmedTick = 0;

static s32 netTeamRoleCount(const u8 *roles)
{
	s32 n = 0;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (roles[i] != PLAYERROLE_NONE) {
			n++;
		}
	}
	return n;
}

static u8 netTeamParseRole(char c)
{
	switch (c) {
	case 'b': case 'B': case 'o': case 'O': return PLAYERROLE_BOND;
	case 'c': case 'C': return PLAYERROLE_COOP;
	case 'a': case 'A': return PLAYERROLE_ANTI;
	default: return PLAYERROLE_NONE;
	}
}

void netTeamInitFromArgs(void)
{
	const s32 idx = sysArgGetInt("--team", -1);
	if (idx < 0) {
		return;
	}
	if (idx >= NUM_SOLOSTAGES) {
		sysLogPrintf(LOG_ERROR, "NET: --team %d out of range (0..%d)", idx, NUM_SOLOSTAGES - 1);
		return;
	}
	g_NetTeamStageIndex = idx;

	const s32 diff = sysArgGetInt("--team-diff", DIFF_A);
	g_NetTeamDifficulty = (diff < DIFF_A) ? DIFF_A : (diff > DIFF_PD) ? DIFF_PD : diff;

	const char *roles = sysArgGetString("--team-roles");
	if (roles && roles[0]) {
		s32 slot = 0;
		for (const char *p = roles; *p && slot < MAX_PLAYERS; p++) {
			if (*p == ',' || *p == ' ') {
				continue;
			}
			g_NetTeamRoles[slot++] = netTeamParseRole(*p);
		}
		while (slot < MAX_PLAYERS) {
			g_NetTeamRoles[slot++] = PLAYERROLE_NONE;
		}
	}
	// slot 0 is the host and the operative, whatever the string said
	g_NetTeamRoles[0] = PLAYERROLE_BOND;

	g_NetTeamWait = sysArgGetInt("--team-wait", netTeamRoleCount(g_NetTeamRoles) - 1);
	if (g_NetTeamWait < 0) {
		g_NetTeamWait = 0;
	}

	sysLogPrintf(LOG_NOTE, "NET: team mission armed: stage index %d diff %d roles {%d,%d,%d,%d} wait %d",
			g_NetTeamStageIndex, g_NetTeamDifficulty,
			g_NetTeamRoles[0], g_NetTeamRoles[1], g_NetTeamRoles[2], g_NetTeamRoles[3], g_NetTeamWait);
}

/**
 * Both ends. The Team Missions accept, minus the menu: roles in, numbers reset
 * and re-derived from the roles, player count from the roles, stage change.
 * stageindex is the solo stage index (g_SoloStages).
 */
void netTeamEnterStage(s32 stageindex, s32 difficulty, const u8 *roles)
{
	if (stageindex < 0 || stageindex >= NUM_SOLOSTAGES) {
		sysLogPrintf(LOG_ERROR, "NET: netTeamEnterStage bad stage index %d", stageindex);
		return;
	}

	netCoopResetStageState();

	g_MissionConfig.isteam = true;
	g_MissionConfig.iscoop = false;
	g_MissionConfig.isanti = false;
	g_MissionConfig.pdmode = true; // players must explicitly join (the hub sets this too)
	g_MissionConfig.difficulty = difficulty;
	g_MissionConfig.stageindex = stageindex;
	g_MissionConfig.stagenum = g_SoloStages[stageindex].stagenum;
	g_Vars.numaibuddies = 0;

	// coop/anti numbers back to -1 so playermgrResetTeamPlayers derives them
	// from the roles (it only assigns a number that is still < 0)
	playermgrDisableTeamPlayers(false);
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		g_Vars.playerroles[i] = roles[i];
	}
	g_Vars.playerroles[0] = PLAYERROLE_BOND;

	titleSetNextStage(g_MissionConfig.stagenum);
	playermgrResetTeamPlayers();
	setNumPlayers(getNumTeamPlayerRoleAssignments());
	lvSetDifficulty(difficulty);
	titleSetNextMode(TITLEMODE_SKIP);

	sysLogPrintf(LOG_NOTE, "NET: team mission enter: stage 0x%02x (index %d) diff %d roles {%d,%d,%d,%d} players %d bond=%d coop=%d anti=%d",
			g_MissionConfig.stagenum, stageindex, difficulty,
			g_Vars.playerroles[0], g_Vars.playerroles[1], g_Vars.playerroles[2], g_Vars.playerroles[3],
			getNumTeamPlayerRoleAssignments(), g_Vars.bondplayernum, g_Vars.coopplayernum, g_Vars.antiplayernum);

	mainChangeToStage(g_MissionConfig.stagenum);
}

/**
 * Host, once per frame from netEndFrame. Waits in the hub for --team-wait
 * remote clients to reach the lobby, then starts the armed team mission.
 */
void netTeamServerTick(void)
{
	if (g_NetMode != NETMODE_SERVER || g_NetTeamStageIndex < 0) {
		return;
	}
	if (g_StageNum != STAGE_CITRAINING || g_MainChangeToStageNum >= 0) {
		return;
	}

	s32 remotes = 0;
	for (s32 i = 0; i < g_NetMaxClients; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl != g_NetLocalClient && cl->state >= CLSTATE_LOBBY) {
			remotes++;
		}
	}

	if (remotes < g_NetTeamWait) {
		if (s_teamArmedTick) {
			sysLogPrintf(LOG_NOTE, "NET: team start cancelled (%d/%d clients)", remotes, g_NetTeamWait);
			s_teamArmedTick = 0;
		}
		return;
	}

	if (s_teamArmedTick == 0) {
		s_teamArmedTick = g_NetTick + 60u;
		sysLogPrintf(LOG_NOTE, "NET: %d/%d clients in lobby, team start in 1s", remotes, g_NetTeamWait);
		return;
	}

	if (g_NetTick < s_teamArmedTick) {
		return;
	}

	// seats beyond the connected players stay in the manifest with their role;
	// playermgrAllocatePlayers(-1) seats them and netPlayersAllocate leaves them
	// unbound (no client), so they idle. One start per arm.
	const s32 idx = g_NetTeamStageIndex;
	g_NetTeamStageIndex = -1;
	s_teamArmedTick = 0;
	netTeamEnterStage(idx, g_NetTeamDifficulty, g_NetTeamRoles);
}

/**
 * Direct boot for a CLI-latched session (--host / --connect). Kai's
 * netDedicatedBootTick, minus the Combat Sim lobby entry: fojo hosts straight
 * into the hub and waits there (netTeamServerTick). Called from
 * playerTickPauseMenu's MENUROOT_FILEMGR case in place of the agent file
 * select. False = pak still preparing, retry next frame.
 */
s32 netDedicatedBootTick(void)
{
	if (!pakIsGamepakReady()) {
		return false;
	}

	gamefileLoadDefaults(&g_GameFile);
	strcpy(g_GameFile.name, "PD");

	mpInit(true);
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_PlayerConfigsArray[i].base.name[0] == '\0') {
			sprintf(g_PlayerConfigsArray[i].base.name, "Player %d\n", i + 1);
		}
	}

	if (g_NetJoinLatch) {
		g_NetJoinLatch = false;
		g_NetHostLatch = false;
		if (netStartClient(g_NetLastJoinAddr) != 0) {
			sysLogPrintf(LOG_ERROR, "NET: join to %s failed at boot", g_NetLastJoinAddr);
		}
	} else if (g_NetHostLatch) {
		g_NetHostLatch = false;
		if (netStartServer(g_NetServerPort, g_NetMaxClients) != 0) {
			sysLogPrintf(LOG_ERROR, "NET: host on port %d failed at boot", g_NetServerPort);
		}
	}

	return true;
}
