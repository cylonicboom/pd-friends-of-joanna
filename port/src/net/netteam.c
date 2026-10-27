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
 * seat's role. The host is playernum 0. Exactly one seat is the operative and
 * it can be any seat (netTeamNormaliseRoles); playermgrResetTeamPlayers points
 * bondplayernum at it. The host's seat always has a role - it is a player
 * until the server seat (PLAYERROLE_SERVER) lands.
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
#include "seatprofile.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "game/gamefile.h"
#include "game/pak.h"
#include "game/pdmode.h"
#include "game/title.h"
#include "game/lv.h"
#include "game/options.h"
#include "lib/joy.h"
#include "lib/anim.h"
#include "game/playermgr.h"
#include "game/mplayer/mplayer.h"
#include "lib/main.h"
#include "system.h"
#include "game/mainmenu.h"
#include "game/menu.h"
#include "game/filemgr.h"
#include "mpk.h"
#include "net/net.h"
#include "net/netmsg.h"
#include "net/netenet.h"

s32 g_NetTeamStageIndex = -1;
s32 g_NetTeamDifficulty = DIFF_A;
s32 g_NetTeamWait = -1;
s32 g_NetTeamArmed = 0; // --team given: netTeamServerTick auto-starts at --team-wait. the window never sets it.
u8 g_NetTeamRoles[MAX_PLAYERS] = { PLAYERROLE_BOND, PLAYERROLE_COOP, PLAYERROLE_NONE, PLAYERROLE_NONE };
char g_NetTeamRolesCfg[16] = "b,c,n,n";

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

static void netTeamParseRoles(const char *roles)
{
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
	netTeamNormaliseRoles(g_NetTeamRoles);
}

/**
 * Exactly one operative, anywhere: the first 'b' wins and any later one
 * becomes co-op; with none, seat 0 is the operative. The host's seat (0)
 * never goes empty - it is a player - so a 'n' there becomes co-op.
 */
void netTeamNormaliseRoles(u8 *roles)
{
	s32 bond = -1;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (roles[i] != PLAYERROLE_BOND) {
			continue;
		}
		if (bond < 0) {
			bond = i;
		} else {
			roles[i] = PLAYERROLE_COOP;
		}
	}
	if (bond < 0) {
		roles[0] = PLAYERROLE_BOND;
	} else if (roles[0] == PLAYERROLE_NONE) {
		roles[0] = PLAYERROLE_COOP;
	}
}


void netTeamRolesFromCfg(void)
{
	if (g_NetTeamRolesCfg[0]) {
		netTeamParseRoles(g_NetTeamRolesCfg);
	}
}

void netTeamRolesToCfg(void)
{
	static const char letters[] = { 'n', 'c', 'a', 'b' }; // PLAYERROLE_NONE..BOND
	char *p = g_NetTeamRolesCfg;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (i) {
			*p++ = ',';
		}
		*p++ = letters[g_NetTeamRoles[i] & 3];
	}
	*p = '\0';
}

void netTeamInitFromArgs(void)
{
	// pd.ini first, so the window opens on what the host last used; the
	// flags below override it for a CLI run
	netTeamRolesFromCfg();

	const s32 idx = sysArgGetInt("--team", -1);
	if (idx < 0) {
		return;
	}
	if (idx >= NUM_SOLOSTAGES) {
		sysLogPrintf(LOG_ERROR, "NET: --team %d out of range (0..%d)", idx, NUM_SOLOSTAGES - 1);
		return;
	}
	g_NetTeamStageIndex = idx;
	g_NetTeamArmed = 1;

	const s32 diff = sysArgGetInt("--team-diff", DIFF_A);
	g_NetTeamDifficulty = (diff < DIFF_A) ? DIFF_A : (diff > DIFF_PD) ? DIFF_PD : diff;

	const char *roles = sysArgGetString("--team-roles");
	if (roles && roles[0]) {
		netTeamParseRoles(roles); // one operative, any seat (netTeamNormaliseRoles)
	}

	g_NetTeamWait = sysArgGetInt("--team-wait", netTeamRoleCount(g_NetTeamRoles) - 1);
	if (g_NetTeamWait < 0) {
		g_NetTeamWait = 0;
	}

	// fojo: only an explicit --team-wait arms the one-shot count start (the
	// harness); otherwise --team just picks the stage and the hub ready-up
	// starts it (netTeamReadyTick)
	if (!sysArgCheck("--team-wait")) {
		g_NetTeamArmed = 0;
	}

	sysLogPrintf(LOG_NOTE, "NET: team mission %s: stage index %d diff %d roles {%d,%d,%d,%d} wait %d",
			g_NetTeamArmed ? "armed" : "picked, starts when everyone is ready",
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

	// still in the hub, slot 0 still ours: the profile restored after the
	// mission (and saved) is the one in it now, not the one we joined with
	netSnapshotLocalProfile();

	// fojo: the ready-up is per lobby visit
	for (s32 i = 0; i < NET_MAX_CLIENTS + 1; i++) {
		g_NetClients[i].ready = 0;
	}

	g_MissionConfig.isteam = true;
	g_MissionConfig.iscoop = false;
	g_MissionConfig.isanti = false;
	g_MissionConfig.pdmode = true; // players must explicitly join (the hub sets this too)
	// the knobs the Team Missions menu resolves on accept
	// (menuhandlerTeamStartMission). without this the f32s were whatever
	// was there - 0 on a CLI boot - and chrDamage divided by it: inf damage,
	// a dead chr the moment anything hit it, and a NaN that poisoned every
	// SVC_PROP_MOVE packet it rode in.
	g_MissionConfig.pdmodehealthf = func0f1036ac(g_MissionConfig.pdmodehealth, PDMODEPROP_HEALTH);
	g_MissionConfig.pdmodedamagef = func0f1036ac(g_MissionConfig.pdmodedamage, PDMODEPROP_DAMAGE);
	g_MissionConfig.pdmodeaccuracyf = func0f1036ac(g_MissionConfig.pdmodeaccuracy, PDMODEPROP_ACCURACY);
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
	// no operative in what arrived: playermgrResetTeamPlayers falls back to seat 0

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
 * Both ends, once a second from netEndFrame: the state a test plan reads.
 * One `team` line per seat: role, pos, dead, lives, and the stage/tickmode
 * header so a reader can tell cutscene from gameplay.
 */
void netTeamDiagTick(void)
{
	// once a second. the net tick can step by more than one per frame (a
	// slow host under xvfb), so key on the second boundary being crossed
	// rather than on an exact remainder, or the host logs only some seconds.
	static u32 lastsec = 0xffffffffu;
	u32 sec = g_NetTick / 60u;
	if (!g_NetMode) {
		lastsec = 0xffffffffu;
		return;
	}
	if (sec == lastsec) {
		return;
	}
	lastsec = sec;
	netDiagLogf("team", "stage=0x%02x tickmode=%d cutscene=%d lvframe60=%d players=%d bond=%d coop=%d anti=%d isteam=%d endscreen=%d csanim=%d csframe=%d ghost=%d",
			g_StageNum, g_Vars.tickmode, g_Vars.in_cutscene, g_Vars.lvframe60, PLAYERCOUNT(),
			g_Vars.bondplayernum, g_Vars.coopplayernum, g_Vars.antiplayernum, g_MissionConfig.isteam, g_MainIsEndscreen,
			g_CutsceneAnimNum, g_CutsceneCurAnimFrame60, g_NetHostGhost);
	for (s32 i = 0; i < g_NetMaxClients; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state) {
			s32 slot = -1;
			for (s32 p = 0; p < MAX_PLAYERS; p++) {
				if (cl->player && g_Vars.players[p] == cl->player) {
					slot = p;
				}
			}
			netDiagLogf("netcl", "id=%d state=%d playernum=%d boundslot=%d local=%d agent=%d", cl->id, cl->state, cl->playernum, slot, cl == g_NetLocalClient, cl->settings.teamagentindex);
		}
	}
	for (s32 i = 0; i < PLAYERCOUNT(); i++) {
		struct player *pl = g_Vars.players[i];
		if (!pl) {
			continue;
		}
		s32 pad = optionsGetContpadNum1(g_Vars.playerstats[i].mpindex);
		netDiagLogf("seat", "i=%d role=%d remote=%d client=%d pos=(%.1f,%.1f,%.1f) dead=%d lives=%d health=%.2f hidden=%d ucmd=0x%08x go=%.2f/%.2f pad=%d stick=%d/%d mode=%d agent=%d",
				i, g_Vars.playerroles[i], (s32)pl->isremote, pl->client ? (s32)pl->client->id : -1,
				pl->prop ? pl->prop->pos.x : 0.0f, pl->prop ? pl->prop->pos.y : 0.0f, pl->prop ? pl->prop->pos.z : 0.0f,
				(s32)pl->isdead, pl->livesremaining, pl->bondhealth,
				(pl->prop && pl->prop->chr && (pl->prop->chr->chrflags & CHRCFLAG_HIDDEN)) ? 1 : 0, pl->ucmd,
				pl->speedforwards, pl->speedsideways, pad,
				pl->isremote ? 0 : joyGetStickX(pad), pl->isremote ? 0 : joyGetStickY(pad),
				optionsGetControlMode(g_Vars.playerstats[i].mpindex),
				g_PlayerConfigsArray[g_Vars.playerstats[i].mpindex].teamagentindex);
	}
	// replicated chr anims (the bug where the guards forget how to walk):
	// what each synced NPC is playing, so the verdict can pair host and
	// client by syncid - looping agreement, and a client cycle parked on
	// its last frame while the host's still loops. Capped per second.
	if (g_Vars.tickmode == TICKMODE_NORMAL && g_ChrSlots) {
		s32 logged = 0;
		for (s32 i = 0; i < g_NumChrSlots && logged < 32; i++) {
			struct chrdata *chr = &g_ChrSlots[i];
			if (chr->chrnum < 0 || !chr->prop || chr->prop->type != PROPTYPE_CHR || !chr->prop->syncid
					|| !chr->model || !chr->model->anim || chr->model->anim->animnum <= 0) {
				continue;
			}
			const struct anim *anim = chr->model->anim;
			netDiagLogf("canim", "sid=%u anim=%d frame=%.1f speed=%.2f loop=%d lf=%.0f end=%.0f n=%d act=%d err=%.1f",
					chr->prop->syncid, anim->animnum, anim->frame, anim->speed, anim->looping ? 1 : 0,
					anim->loopframe, anim->endframe, animGetNumFrames(anim->animnum), chr->actiontype,
					g_NetMode == NETMODE_CLIENT ? netChrAnimLastErr(chr) : 0.0f);
			logged++;
		}
	}
	// the client's soft frame correction over the last second: how many
	// same-anim frames it judged, nudged, re-seeded, and the mean error
	if (g_NetMode == NETMODE_CLIENT) {
		netDiagLogf("canimfix", "on=%d samples=%u nudges=%u snaps=%u restarts=%u meanerr=%.2f",
				g_NetAnimCorrect, g_NetAnimStatSamples, g_NetAnimStatNudges, g_NetAnimStatSnaps, g_NetAnimStatRestarts,
				g_NetAnimStatSamples ? g_NetAnimStatErrSum / (f32)g_NetAnimStatSamples : 0.0f);
		g_NetAnimStatSamples = g_NetAnimStatNudges = g_NetAnimStatSnaps = g_NetAnimStatRestarts = 0;
		g_NetAnimStatErrSum = 0;
	}
	// whose file each config slot carries, and which ini section its Game
	// options and binds go through - what the Fojo Options window shows
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		const struct mpplayerconfig *cfg = &g_PlayerConfigsArray[i];
		// prof0: this slot reads the extended profile slot 0's file has
		// (the local player's, under net); away: the local pawn sits here
		netDiagLogf("file", "i=%d guid=%x-%x cfgidx=%d slug=%s showname=%d prof0=%d away=%d fovy=%.1f",
				i, cfg->fileguid.deviceserial, cfg->fileguid.fileid, cfg->configindex, seatProfileSlug(i),
				cfg->showplayername ? (s32)*cfg->showplayername : -1,
				cfg->showplayername && cfg->showplayername == g_PlayerConfigsArray[0].showplayername,
				netLocalSeatAway() == i, g_PlayerExtCfg[i].fovy);
	}
}

/**
 * Host, once per frame from netEndFrame. Waits in the hub for --team-wait
 * remote clients to reach the lobby, then starts the armed team mission.
 */
static void netTeamPostMissionTick(void);
static void netTeamReadyTick(void);

void netTeamServerTick(void)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}
	netTeamPostMissionTick();
	if (!g_NetTeamArmed) {
		netTeamReadyTick();
		return;
	}
	if (g_NetTeamStageIndex < 0) {
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
	g_NetTeamArmed = 0; // one start per arm; the stage stays selected for the lobby window
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
	static s32 initdone = 0;

	if (!pakIsGamepakReady()) {
		return false;
	}

	if (!initdone) {
		const char *query;
		s8 device;

		gamefileLoadDefaults(&g_GameFile);
		strcpy(g_GameFile.name, "PD");

		mpInit(true);
		initdone = 1;

		// mpInit just put every seat back to its defaults - including a
		// --player the hub's own default-profile try (menutick) may already
		// have loaded into slot 0 a frame earlier. Load it again below.
		filemgrForgetDefaultProfile(0);

		for (s32 i = 1; i < MAX_PLAYERS; i++) {
			if (mpkPlayerArg(i, &device, &query)) {
				sysLogPrintf(LOG_WARNING, "NET: --player for seat %d ignored: under netplay only the local player (the first --player) is seated", i + 1);
			}
		}
	}

	// the local player's own profile (--player), in config slot 0 on host and
	// client alike - netClientReadConfig(g_NetLocalClient, 0) is what goes on
	// the wire. Never the other slots: on the host they hold the remotes'
	// names (netPlayersAllocate, netCoopSeatClient), and a profile there
	// would be saved under them.
	if (filemgrTryLoadPlayerArg(0) == 2) {
		return false; // its pak is still coming up
	}


	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_PlayerConfigsArray[i].base.name[0] == '\0') {
			sprintf(g_PlayerConfigsArray[i].base.name, "Player %d\n", i + 1);
		}
		// fojo: the extended profile pointers (handicap, classic sight, ...)
		// are bound when a seat picks a file in the menus. No menus here, so
		// bind every seat to its stand-in now - chrDamage derefs handicap.
		iniRegisterPlayerSave(&g_PlayerConfigsArray[i].fileguid, 1, i);
	}

	// --team-agent N: the local player's Team Operative carousel pick, for a
	// headless run with no menus (and the harness, which needs picks that
	// differ per machine to prove they cross the wire). After the binding
	// above, which sets an unsaved profile's pick to its slot number.
	{
		const s32 agent = sysArgGetInt("--team-agent", -1);
		if (agent >= 0) {
			g_PlayerConfigsArray[0].teamagentindex = agent;
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

/* ------------------------------------------------------------------------
 * The lobby, for the window (imgui_netplay.cpp). Host builds, broadcasts;
 * client reads. Seats follow netPlayersAllocate's order exactly: host 0,
 * then every non-spectator client in table order, so what the lobby shows
 * is what SVC_STAGE_START will seat.
 * ---------------------------------------------------------------------- */

struct netteamlobby g_NetTeamLobby;

static u32 s_lobbyLastSentTick = 0;
static u8 s_lobbyDirty = 1;

static s32 netTeamInHub(void)
{
	return g_StageNum == STAGE_CITRAINING && g_MainChangeToStageNum < 0;
}

void netTeamLobbyBuild(void)
{
	struct netteamlobby *lb = &g_NetTeamLobby;
	s32 seat = 0;

	if (g_NetMode != NETMODE_SERVER) {
		return;
	}

	memset(lb, 0, sizeof(*lb));
	lb->valid = 1;
	lb->stageindex = (s8)g_NetTeamStageIndex;
	lb->difficulty = (u8)g_NetTeamDifficulty;
	memcpy(lb->roles, g_NetTeamRoles, sizeof(lb->roles));

	for (s32 i = 0; i < g_NetMaxClients && lb->numclients < NET_MAX_CLIENTS; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl->state < CLSTATE_LOBBY) {
			continue;
		}
		struct netteamlobbyclient *out = &lb->clients[lb->numclients++];
		out->id = (u8)cl->id;
		out->ping = (u16)(cl->peer ? cl->peer->roundTripTime : 0u);
		out->ready = cl->ready;
		strncpy(out->name, cl->settings.name, NET_MAX_NAME - 1);
		out->name[NET_MAX_NAME - 1] = '\0';
		if (cl->is_spectator || seat >= MAX_PLAYERS) {
			out->seat = -1;
		} else {
			out->seat = (s8)seat++;
		}
	}
}

static s32 netTeamLobbyChanged(const struct netteamlobby *a, const struct netteamlobby *b)
{
	if (a->stageindex != b->stageindex || a->difficulty != b->difficulty
			|| a->numclients != b->numclients
			|| memcmp(a->roles, b->roles, sizeof(a->roles)) != 0) {
		return 1;
	}
	for (s32 i = 0; i < a->numclients; i++) {
		if (a->clients[i].id != b->clients[i].id || a->clients[i].seat != b->clients[i].seat
				|| strcmp(a->clients[i].name, b->clients[i].name) != 0) {
			return 1;
		}
	}
	return 0;
}

void netTeamLobbyTick(void)
{
	if (g_NetMode != NETMODE_SERVER) {
		return;
	}

	struct netteamlobby prev = g_NetTeamLobby;
	netTeamLobbyBuild();
	if (netTeamLobbyChanged(&prev, &g_NetTeamLobby)) {
		s_lobbyDirty = 1;
	}

	if (!s_lobbyDirty && g_NetTick - s_lobbyLastSentTick < 60u) {
		return;
	}

	s32 anyone = 0;
	for (s32 i = 0; i < g_NetMaxClients; i++) {
		struct netclient *cl = &g_NetClients[i];
		if (cl != g_NetLocalClient && cl->state >= CLSTATE_LOBBY && cl->peer) {
			anyone = 1;
			break;
		}
	}
	s_lobbyDirty = 0;
	s_lobbyLastSentTick = g_NetTick;
	if (!anyone) {
		return;
	}

	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcTeamLobbyWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
}

/**
 * The Team Missions menu's rule (menuhandlerMissionList): a campaign row is
 * reachable while every row before it is complete on some difficulty (solo
 * best time or a team completion); the special assignments after Skedar
 * Ruins open as getNumUnlockedSpecialStages says, in func0f104720's order.
 */
s32 netTeamStageUnlocked(s32 stageindex)
{
	if (stageindex < 0 || stageindex >= NUM_SOLOSTAGES) {
		return 0;
	}

	if (stageindex <= SOLOSTAGEINDEX_SKEDARRUINS) {
		for (s32 i = 0; i < stageindex; i++) {
			s32 complete = 0;
			for (s32 j = 0; j < ARRAYCOUNT(g_GameFile.besttimes[i]); j++) {
				if (g_GameFile.besttimes[i][j] != 0 || (g_GameFile.coopcompletions[j] & (1 << i))) {
					complete = 1;
				}
			}
			if (!complete) {
				return 0;
			}
		}
		return 1;
	}

	const s32 nspecial = getNumUnlockedSpecialStages(true);
	for (s32 k = 0; k < nspecial; k++) {
		if (func0f104720(k) == stageindex) {
			return 1;
		}
	}
	return 0;
}

s32 netTeamSetStage(s32 stageindex)
{
	if (!netTeamStageUnlocked(stageindex)) {
		return 0;
	}
	if (g_NetTeamStageIndex != stageindex) {
		g_NetTeamStageIndex = stageindex;
		s_lobbyDirty = 1;
	}
	return 1;
}

void netTeamSetDifficulty(s32 difficulty)
{
	difficulty = (difficulty < DIFF_A) ? DIFF_A : (difficulty > DIFF_PD) ? DIFF_PD : difficulty;
	if (g_NetTeamDifficulty != difficulty) {
		g_NetTeamDifficulty = difficulty;
		s_lobbyDirty = 1;
	}
}

void netTeamSetRole(s32 seat, u8 role)
{
	if (seat < 0 || seat >= MAX_PLAYERS || role > PLAYERROLE_BOND) {
		return;
	}
	if (g_NetTeamRoles[seat] == role) {
		return;
	}
	// the operative moves, it is never removed: giving a seat BOND makes the
	// old operative's seat co-op; taking BOND off a seat is refused (give it
	// to another seat instead). The host's seat (0) never goes empty.
	if (g_NetTeamRoles[seat] == PLAYERROLE_BOND) {
		return;
	}
	if (seat == 0 && role == PLAYERROLE_NONE) {
		return;
	}
	if (role == PLAYERROLE_BOND) {
		for (s32 i = 0; i < MAX_PLAYERS; i++) {
			if (g_NetTeamRoles[i] == PLAYERROLE_BOND) {
				g_NetTeamRoles[i] = PLAYERROLE_COOP;
			}
		}
	}
	g_NetTeamRoles[seat] = role;
	netTeamRolesToCfg();
	s_lobbyDirty = 1;
}

/**
 * Host: take a whole role list at once (the console's roles, --team-roles'
 * shape). Refuses with why rather than normalising: two operatives, none, or
 * an empty host seat. Returns 1 when taken.
 */
s32 netTeamSetRoles(const u8 *roles, char *why, u32 whylen)
{
	s32 bonds = 0;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (roles[i] > PLAYERROLE_BOND) {
			if (why && whylen) snprintf(why, whylen, "seat %d has no such role", i + 1);
			return 0;
		}
		bonds += roles[i] == PLAYERROLE_BOND;
	}
	if (bonds != 1) {
		if (why && whylen) snprintf(why, whylen, bonds ? "only one operative" : "someone has to be the operative");
		return 0;
	}
	if (roles[0] == PLAYERROLE_NONE) {
		if (why && whylen) snprintf(why, whylen, "the host's seat needs a role");
		return 0;
	}
	if (memcmp(g_NetTeamRoles, roles, sizeof(g_NetTeamRoles)) != 0) {
		memcpy(g_NetTeamRoles, roles, sizeof(g_NetTeamRoles));
		netTeamRolesToCfg();
		s_lobbyDirty = 1;
	}
	return 1;
}

s32 netTeamCanStart(char *why, u32 whylen)
{
	#define WHY(...) do { if (why && whylen) { snprintf(why, whylen, __VA_ARGS__); } } while (0)

	if (g_NetMode != NETMODE_SERVER) {
		WHY("not hosting");
		return 0;
	}
	if (!netTeamInHub()) {
		WHY("not in the hub");
		return 0;
	}
	if (g_NetTeamStageIndex < 0 || g_NetTeamStageIndex >= NUM_SOLOSTAGES) {
		WHY("pick a stage");
		return 0;
	}
	if (!netTeamStageUnlocked(g_NetTeamStageIndex)) {
		WHY("that stage is locked on this save");
		return 0;
	}

	netTeamLobbyBuild();
	s32 seated = 0;
	s32 bondseated = 0;
	for (s32 i = 0; i < g_NetTeamLobby.numclients; i++) {
		const s32 seat = g_NetTeamLobby.clients[i].seat;
		if (seat < 0) {
			continue;
		}
		if (g_NetTeamRoles[seat] == PLAYERROLE_BOND) {
			bondseated = 1;
		}
		if (seat == 0) {
			continue; // the host
		}
		if (g_NetTeamRoles[seat] == PLAYERROLE_NONE) {
			WHY("%s has no role", g_NetTeamLobby.clients[i].name);
			return 0;
		}
		seated++;
	}
	if (seated == 0) {
		WHY("nobody has joined yet");
		return 0;
	}
	if (!bondseated) {
		WHY("nobody sits in the operative's seat");
		return 0;
	}
	WHY("ready");
	return 1;
	#undef WHY
}

s32 netTeamStart(void)
{
	if (!netTeamCanStart(NULL, 0)) {
		return 0;
	}

	// seats nobody sits in carry no role: fojo's players[] is dense and the
	// manifest seats exactly the clients in the lobby
	u8 roles[MAX_PLAYERS];
	memset(roles, PLAYERROLE_NONE, sizeof(roles));
	for (s32 i = 0; i < g_NetTeamLobby.numclients; i++) {
		const s32 seat = g_NetTeamLobby.clients[i].seat;
		if (seat >= 0 && seat < MAX_PLAYERS) {
			roles[seat] = g_NetTeamRoles[seat];
		}
	}

	const s32 idx = g_NetTeamStageIndex;
	s_teamArmedTick = 0;
	netTeamEnterStage(idx, g_NetTeamDifficulty, roles);
	return 1;
}

/* ------------------------------------------------------------------------
 * The hub ready-up and the way back (proto 91). See net.h.
 * ---------------------------------------------------------------------- */

static u32 s_readyStartTick = 0;
static u32 s_hubAtTick = 0;

void netTeamClientReady(struct netclient *cl, s32 ready)
{
	if (!cl || g_NetMode != NETMODE_SERVER || !netTeamInHub()) {
		return;
	}
	ready = ready ? 1 : 0;
	if (cl->ready == ready) {
		return;
	}
	cl->ready = ready;
	s_lobbyDirty = 1;
	netChatPrintf(NULL, "%s is %s", cl->settings.name, ready ? "ready" : "not ready");
}

void netTeamSetReady(s32 ready)
{
	if (!g_NetLocalClient || !netTeamInHub()) {
		return;
	}
	if (g_NetMode == NETMODE_SERVER) {
		netTeamClientReady(g_NetLocalClient, ready);
	} else if (g_NetMode == NETMODE_CLIENT) {
		netbufStartWrite(&g_NetMsgRel);
		netmsgClcTeamReadyWrite(&g_NetMsgRel, ready ? 1 : 0);
		netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
	}
}

s32 netTeamLocalReady(void)
{
	if (!g_NetLocalClient) {
		return 0;
	}
	if (g_NetMode == NETMODE_SERVER) {
		return g_NetLocalClient->ready;
	}
	for (s32 i = 0; i < g_NetTeamLobby.numclients; i++) {
		if (g_NetTeamLobby.clients[i].id == g_NetLocalClient->id) {
			return g_NetTeamLobby.clients[i].ready;
		}
	}
	return 0;
}

/**
 * Host, every frame in the hub when no count start is armed: once every
 * seated player is ready (a --dedicated host is not a player), start after a
 * three second count; anyone unreadying, leaving or joining cancels it.
 */
static void netTeamReadyTick(void)
{
	char why[96];
	s32 waiting = 0;
	s32 readies = 0;

	if (!netTeamInHub() || !netTeamCanStart(why, sizeof(why))) {
		s_readyStartTick = 0;
		return;
	}

	for (s32 i = 0; i < g_NetTeamLobby.numclients; i++) {
		const struct netteamlobbyclient *lc = &g_NetTeamLobby.clients[i];
		if (lc->seat < 0 || (lc->id == 0 && g_NetDedicatedLatch)) {
			continue;
		}
		if (lc->ready) {
			readies++;
		} else {
			waiting++;
		}
	}

	if (waiting || !readies) {
		if (s_readyStartTick) {
			netChatPrintf(NULL, "[server] start cancelled - not everyone is ready");
			s_readyStartTick = 0;
		}
		return;
	}

	if (s_readyStartTick == 0) {
		s_readyStartTick = g_NetTick + 180u;
		netChatPrintf(NULL, "[server] everyone is ready - starting in 3");
		return;
	}
	if (g_NetTick < s_readyStartTick) {
		return;
	}
	s_readyStartTick = 0;
	netTeamStart();
}

/**
 * Host, every frame: a team mission's endscreen came up - ten seconds to
 * read it, then everyone goes back to the hub (SVC_TEAM_HUB) instead of the
 * endscreen waiting on a Continue nobody at a dedicated host can press.
 */
static void netTeamPostMissionTick(void)
{
	if (!g_MissionConfig.isteam || !g_MainIsEndscreen || netTeamInHub() || g_MainChangeToStageNum >= 0) {
		s_hubAtTick = 0;
		return;
	}
	if (s_hubAtTick == 0) {
		s_hubAtTick = g_NetTick + 600u;
		netChatPrintf(NULL, "[server] back to the lobby in 10");
		return;
	}
	if (g_NetTick < s_hubAtTick) {
		return;
	}
	s_hubAtTick = 0;
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcTeamHubWrite(&g_NetMsgRel);
	netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
	netTeamReturnToHub();
}

/**
 * Both ends: leave the stage for the hub the way the endscreen's decline does
 * (endscreenHandleDeclineMission), with every local menu emptied instead of
 * its two pops, and the local profile back in slot 0 - the hub seats one
 * local player there, and during the mission that slot held a remote's
 * settings.
 */
void netTeamReturnToHub(void)
{
	const s32 prevmpplayer = g_MpPlayerNum;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		g_MpPlayerNum = i;
		for (s32 guard = 0; guard < 16 && g_Menus[i].depth > 0; guard++) {
			menuPopDialog();
		}
	}
	g_MpPlayerNum = prevmpplayer;

	mpSetPaused(MPPAUSEMODE_UNPAUSED);
	g_Vars.mplayerisrunning = false;
	g_MissionConfig.iscoop = false;
	g_MissionConfig.isanti = false;
	g_MissionConfig.isteam = false;
	g_MissionConfig.pdmode = false;
	g_Vars.normmplayerisrunning = false;
	g_Vars.lvmpbotlevel = 0;

	for (s32 i = 0; i < NET_MAX_CLIENTS + 1; i++) {
		g_NetClients[i].ready = 0;
		if (g_NetClients[i].state >= CLSTATE_LOBBY) {
			g_NetClients[i].state = CLSTATE_LOBBY;
		}
	}
	s_lobbyDirty = 1;
	netRestoreLocalProfileToSlot0();

	sysLogPrintf(LOG_CHAT, "NET: back to the lobby");
	netChatLogPush("back to the lobby");

	titleSetNextStage(STAGE_CITRAINING);
	setNumPlayers(1);
	titleSetNextMode(TITLEMODE_SKIP);
	mainChangeToStage(STAGE_CITRAINING);
}

/* ------------------------------------------------------------------------
 * The host ghost (--dedicated). See net.h.
 * ---------------------------------------------------------------------- */

u8 g_NetHostGhost = 0;

static struct player *netGhostPlayer(void)
{
	if (!g_NetMode || !g_NetHostGhost || !g_MissionConfig.isteam || netTeamInHub()) {
		return NULL;
	}
	// the host's netclient is wire id 0 on every machine
	for (s32 i = 0; i < g_NetMaxClients; i++) {
		if (g_NetClients[i].state >= CLSTATE_LOBBY && g_NetClients[i].id == 0) {
			return g_NetClients[i].player;
		}
	}
	return NULL;
}

/**
 * The flags an ailist unset_chr_chrflag may clear on chr: all of them, except
 * HIDDEN on the host's ghost. A client re-runs a setup list's unhide of the
 * co-op seat every frame (measured: ~once a frame for chr ids 248 and 245 on
 * a Defection client, once on the host), which would undo the ghost.
 */
u32 netGhostUnsetMask(struct chrdata *chr, u32 flags)
{
	struct player *pl = netGhostPlayer();
	if (pl && pl->prop && pl->prop->chr == chr) {
		return flags & ~CHRCFLAG_HIDDEN;
	}
	return flags;
}

void netGhostTick(void)
{
	if (g_NetMode == NETMODE_SERVER) {
		g_NetHostGhost = g_NetDedicatedLatch ? 1 : 0;
	}
	struct player *pl = netGhostPlayer();
	if (!pl || !pl->prop || !pl->prop->chr) {
		return;
	}

	pl->prop->chr->chrflags |= CHRCFLAG_HIDDEN | CHRCFLAG_INVINCIBLE;
	pl->prop->chr->hidden |= CHRHFLAG_CLOAKED;
	pl->invincible = 1;
}

/* ------------------------------------------------------------------------
 * Chat ring. Everything the console used to print at LOG_CHAT lands here
 * too, so the window can show it.
 * ---------------------------------------------------------------------- */

static char s_chatLog[NET_CHATLOG_LINES][NET_CHATLOG_LEN];
static s32 s_chatHead = 0;   // next write
static s32 s_chatCount = 0;

void netChatLogPush(const char *line)
{
	if (!line) {
		return;
	}
	strncpy(s_chatLog[s_chatHead], line, NET_CHATLOG_LEN - 1);
	s_chatLog[s_chatHead][NET_CHATLOG_LEN - 1] = '\0';
	s_chatHead = (s_chatHead + 1) % NET_CHATLOG_LINES;
	if (s_chatCount < NET_CHATLOG_LINES) {
		s_chatCount++;
	}
}

s32 netChatLogCount(void)
{
	return s_chatCount;
}

const char *netChatLogGet(s32 index)
{
	if (index < 0 || index >= s_chatCount) {
		return "";
	}
	const s32 oldest = (s_chatHead - s_chatCount + NET_CHATLOG_LINES) % NET_CHATLOG_LINES;
	return s_chatLog[(oldest + index) % NET_CHATLOG_LINES];
}

void netChatLogClear(void)
{
	s_chatHead = 0;
	s_chatCount = 0;
}

void netChatSend(const char *text)
{
	if (!text || !text[0]) {
		return;
	}
	if (netConsoleExec(text, 1)) {
		return;
	}
	netChat(NULL, text);
}

/* ------------------------------------------------------------------------
 * Cutscene history (client). A ring of the anims SVC_CUTSCENE started, each
 * with whether it has ended. The intro list on a client waits on these.
 * ---------------------------------------------------------------------- */

#define NET_CUTSCENE_HISTORY 16
static s16 s_csAnim[NET_CUTSCENE_HISTORY];
static u8 s_csEnded[NET_CUTSCENE_HISTORY];
static s32 s_csCount = 0;

void netCutsceneHistoryReset(void)
{
	s_csCount = 0;
}

void netCutsceneHistoryNote(s32 active, s16 animnum)
{
	if (active) {
		// a re-send of the running scene (heartbeat) is not a new start
		if (s_csCount > 0 && s_csAnim[s_csCount - 1] == animnum && !s_csEnded[s_csCount - 1]) {
			return;
		}
		// a new start ends whatever was running
		if (s_csCount > 0) {
			s_csEnded[s_csCount - 1] = 1;
		}
		if (s_csCount == NET_CUTSCENE_HISTORY) {
			memmove(s_csAnim, s_csAnim + 1, sizeof(s_csAnim) - sizeof(s_csAnim[0]));
			memmove(s_csEnded, s_csEnded + 1, sizeof(s_csEnded) - sizeof(s_csEnded[0]));
			s_csCount--;
		}
		s_csAnim[s_csCount] = animnum;
		s_csEnded[s_csCount] = 0;
		s_csCount++;
	} else if (s_csCount > 0) {
		s_csEnded[s_csCount - 1] = 1;
	}
}

s32 netCutsceneStarted(s16 animnum)
{
	for (s32 i = s_csCount - 1; i >= 0; i--) {
		if (s_csAnim[i] == animnum) {
			return 1;
		}
	}
	return 0;
}

s32 netCutsceneEnded(s16 animnum)
{
	for (s32 i = s_csCount - 1; i >= 0; i--) {
		if (s_csAnim[i] == animnum) {
			return s_csEnded[i];
		}
	}
	return 0;
}
