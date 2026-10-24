// Netplay, in fojOS.
//
// One window, two faces. In the hub it is the lobby: host or join, then the
// seat table (seat, name, role, ping), the host's stage and difficulty, and
// Start. In a mission it is the roster: the same seats with dead/alive and
// health, and Leave. Chat sits under both.
//
// All of the wire work is in port/src/net (netteam.c for the Team Missions
// side); this file is only the window. It drives the same netTeam* entry
// points the CLI flags do, so tools/nettest keeps proving the same path.
//
// Rules (Catherine, 2026-10-21):
//  - the host is seat 0 and the operative; clients fill seats in join order
//  - the host assigns a role per seat; clients see theirs read-only
//  - the stage list is the Team Missions menu's, gated by the host's save
//  - Start only when every seated client has a role
//  - the mission's end returns everyone to this lobby, still connected
//  - direct address joins; no browser, no master
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"
#include "config.h"
// the net headers have no C++ guards of their own
extern "C" {
#include "game/lang.h"
#include "net/net.h"
#include "net/netmsg.h"
}
// types.h defines bool as s32 for the C side; ImGui wants the real thing
#undef bool
#undef true
#undef false
#include "imgui.h"
#include "imgui_netplay.h"

extern "C" s32 g_MainChangeToStageNum;

static char s_joinAddr[NET_MAX_ADDR + 1];
static char s_joinPassword[NET_MAX_PASSWORD];
static char s_hostPassword[NET_MAX_PASSWORD];
static int s_hostPort = 0;
static char s_chatInput[NET_CHATLOG_LEN];
static int s_chatSeen = 0;
static bool s_chatScrollToEnd = false;
static bool s_inited = false;

static const char *const kRoleNames[] = { "none", "co-op", "counter-op", "operative" };
static const char *const kDiffNames[] = { "Agent", "Special Agent", "Perfect Agent", "Perfect Dark" };

// langGet strings carry '\n' width markers; the window wants one line.
static void imguiNetplayCleanName(char *dst, u32 len, const char *src)
{
	u32 n = 0;
	for (; src && *src && n + 1 < len; src++) {
		if (*src == '\n') {
			break;
		}
		dst[n++] = *src;
	}
	dst[n] = '\0';
}

static const char *imguiNetplayStageName(s32 stageindex, char *buf, u32 len)
{
	if (stageindex < 0 || stageindex >= NUM_SOLOSTAGES) {
		snprintf(buf, len, "(none)");
		return buf;
	}
	char a[48], b[48];
	imguiNetplayCleanName(a, sizeof(a), langGet(g_SoloStages[stageindex].name1));
	imguiNetplayCleanName(b, sizeof(b), langGet(g_SoloStages[stageindex].name2));
	if (b[0]) {
		snprintf(buf, len, "%s: %s", a, b);
	} else {
		snprintf(buf, len, "%s", a);
	}
	return buf;
}

static void imguiNetplayInit(void)
{
	if (s_inited) {
		return;
	}
	s_inited = true;
	strncpy(s_joinAddr, g_NetLastJoinAddr, NET_MAX_ADDR);
	s_joinAddr[NET_MAX_ADDR] = '\0';
	if (!s_joinAddr[0]) {
		snprintf(s_joinAddr, sizeof(s_joinAddr), "127.0.0.1:%u", g_NetServerPort);
	}
	s_hostPort = (int)g_NetServerPort;
	strncpy(s_hostPassword, g_NetServerPassword, NET_MAX_PASSWORD - 1);
}

static bool imguiNetplayInHub(void)
{
	return g_StageNum == STAGE_CITRAINING && g_MainChangeToStageNum < 0;
}

/* ---------------------------------------------------------------- chat --- */

static void imguiNetplayDrawChat(void)
{
	const s32 count = netChatLogCount();
	ImGui::SeparatorText("Chat");

	const float footer = ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild("##netchat", ImVec2(0.0f, -footer), ImGuiChildFlags_FrameStyle)) {
		for (s32 i = 0; i < count; i++) {
			ImGui::TextWrapped("%s", netChatLogGet(i));
		}
		if (count != s_chatSeen || s_chatScrollToEnd) {
			ImGui::SetScrollHereY(1.0f);
			s_chatSeen = count;
			s_chatScrollToEnd = false;
		}
	}
	ImGui::EndChild();

	ImGui::SetNextItemWidth(-60.0f);
	bool send = ImGui::InputText("##chatline", s_chatInput, sizeof(s_chatInput), ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	send = ImGui::Button("Send") || send;
	if (send && s_chatInput[0]) {
		netChatSend(s_chatInput);
		s_chatInput[0] = '\0';
		s_chatScrollToEnd = true;
		ImGui::SetKeyboardFocusHere(-1);
	}
}

/* ------------------------------------------------------------ not joined --- */

static void imguiNetplayDrawOffline(void)
{
	if (!imguiNetplayInHub()) {
		ImGui::TextDisabled("Host or join from the hub (Carrington Institute).");
		return;
	}

	if (ImGui::BeginTabBar("##netmode")) {
		if (ImGui::BeginTabItem("Host")) {
			ImGui::TextDisabled("You are seat 1, the operative. Friends join by address.");
			ImGui::SetNextItemWidth(120.0f);
			if (ImGui::InputInt("Port", &s_hostPort, 0, 0)) {
				if (s_hostPort < 1024) s_hostPort = 1024;
				if (s_hostPort > 65535) s_hostPort = 65535;
			}
			ImGui::SetNextItemWidth(200.0f);
			ImGui::InputText("Password", s_hostPassword, sizeof(s_hostPassword), ImGuiInputTextFlags_Password);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Empty = anyone with the address can join.");
			}
			if (ImGui::Button("Host a Team Mission")) {
				g_NetServerPort = (u32)s_hostPort;
				strncpy(g_NetServerPassword, s_hostPassword, NET_MAX_PASSWORD - 1);
				g_NetServerPassword[NET_MAX_PASSWORD - 1] = '\0';
				netChatLogClear();
				if (netStartServer((u16)s_hostPort, g_NetMaxClients) != 0) {
					netChatLogPush("could not host - is the port in use?");
				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Join")) {
			ImGui::TextDisabled("host:port, as the host sees it from your side of the network.");
			ImGui::SetNextItemWidth(260.0f);
			bool go = ImGui::InputText("Address", s_joinAddr, sizeof(s_joinAddr), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SetNextItemWidth(200.0f);
			ImGui::InputText("Password", s_joinPassword, sizeof(s_joinPassword), ImGuiInputTextFlags_Password);
			go = ImGui::Button("Join") || go;
			if (go && s_joinAddr[0]) {
				strncpy(g_NetJoinPassword, s_joinPassword, NET_MAX_PASSWORD - 1);
				g_NetJoinPassword[NET_MAX_PASSWORD - 1] = '\0';
				netChatLogClear();
				if (netStartClient(s_joinAddr) != 0) {
					netChatLogPush("could not start the join - bad address?");
				}
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

/* ----------------------------------------------------------------- lobby --- */

static void imguiNetplayDrawSeatTable(bool inmission)
{
	const bool host = g_NetMode == NETMODE_SERVER;
	const struct netteamlobby *lb = &g_NetTeamLobby;
	const s32 localid = g_NetLocalClient ? (s32)g_NetLocalClient->id : -1;

	const int cols = 5; // seat, name, role, then ready (lobby) or state (mission), ping
	if (!ImGui::BeginTable("##seats", cols, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
		return;
	}
	ImGui::TableSetupColumn("seat");
	ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("role", ImGuiTableColumnFlags_WidthFixed, 120.0f);
	ImGui::TableSetupColumn("ping");
	ImGui::TableSetupColumn(inmission ? "state" : "ready");
	ImGui::TableHeadersRow();

	for (s32 i = 0; i < lb->numclients; i++) {
		const struct netteamlobbyclient *c = &lb->clients[i];
		const bool me = (s32)c->id == localid;
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		if (c->seat >= 0) {
			ImGui::Text("%d", c->seat + 1);
		} else {
			ImGui::TextDisabled("-");
		}
		ImGui::TableNextColumn();
		if (me) {
			ImGui::Text("%s (you)", c->name);
		} else {
			ImGui::TextUnformatted(c->name);
		}
		ImGui::TableNextColumn();
		if (c->seat < 0) {
			ImGui::TextDisabled("no seat");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Every seat is taken. This player watches, or waits for one.");
			}
		} else if (host && !inmission) {
			// any seat can be the operative; picking it moves it (the old
			// operative's seat turns co-op). The operative's own seat and the
			// host's seat offer no "none": the operative is moved, not removed,
			// and the host is a player until the server seat lands.
			const int role = lb->roles[c->seat] & 3;
			const bool isbond = role == PLAYERROLE_BOND;
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("##role", kRoleNames[role])) {
				const int options[] = { PLAYERROLE_BOND, PLAYERROLE_COOP, PLAYERROLE_ANTI, PLAYERROLE_NONE };
				for (int k = 0; k < 4; k++) {
					const bool fixed = isbond && options[k] != PLAYERROLE_BOND;
					const bool hostnone = c->seat == 0 && options[k] == PLAYERROLE_NONE;
					if (hostnone) {
						continue;
					}
					if (fixed) {
						ImGui::BeginDisabled();
					}
					if (ImGui::Selectable(kRoleNames[options[k]], role == options[k])) {
						netTeamSetRole(c->seat, (u8)options[k]);
					}
					if (fixed) {
						ImGui::EndDisabled();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
							ImGui::SetTooltip("Make another seat the operative to move it off this one.");
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
		} else {
			const u8 role = lb->roles[c->seat] & 3;
			if (role == PLAYERROLE_NONE) {
				ImGui::TextDisabled("waiting for a role");
			} else {
				ImGui::TextUnformatted(kRoleNames[role]);
			}
		}
		ImGui::TableNextColumn();
		if (c->seat == 0) {
			ImGui::TextDisabled("host");
		} else {
			ImGui::Text("%u ms", c->ping);
		}
		if (!inmission) {
			ImGui::TableNextColumn();
			if (c->seat < 0) {
				ImGui::TextDisabled("-");
			} else if (c->id == 0 && g_NetHostGhost) {
				ImGui::TextDisabled("server");
			} else if (c->ready) {
				ImGui::TextColored(ImVec4(0.5f, 0.86f, 0.6f, 1.0f), "ready");
			} else {
				ImGui::TextDisabled("not ready");
			}
		}
		if (inmission) {
			ImGui::TableNextColumn();
			const s32 seat = c->seat;
			if (seat >= 0 && seat < PLAYERCOUNT() && g_Vars.players[seat]) {
				const struct player *pl = g_Vars.players[seat];
				if (pl->isdead) {
					ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "dead");
				} else {
					ImGui::TextColored(ImVec4(0.5f, 0.86f, 0.6f, 1.0f), "%d%%", (int)(pl->bondhealth * 100.0f + 0.5f));
				}
			} else {
				ImGui::TextDisabled("-");
			}
		}
	}
	ImGui::EndTable();
}

static void imguiNetplayDrawLobby(void)
{
	const bool host = g_NetMode == NETMODE_SERVER;
	const struct netteamlobby *lb = &g_NetTeamLobby;
	char stagename[96];

	if (host) {
		ImGui::Text("Hosting on port %u", g_NetServerPort);
	} else {
		ImGui::Text("Joined %s", g_NetLastJoinAddr);
		if (g_NetLocalClient && g_NetLocalClient->state < CLSTATE_LOBBY) {
			ImGui::SameLine();
			ImGui::TextDisabled("- connecting...");
		}
	}
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f + ImGui::GetCursorPosX());
	if (ImGui::Button("Leave")) {
		netDisconnect();
		return;
	}

	ImGui::SeparatorText("Mission");

	if (host) {
		// the host's list, gated by the host's save, like the Team Missions menu
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##stage", imguiNetplayStageName(g_NetTeamStageIndex, stagename, sizeof(stagename)))) {
			for (s32 i = 0; i < NUM_SOLOSTAGES; i++) {
				const bool unlocked = netTeamStageUnlocked(i) != 0;
				char name[96];
				imguiNetplayStageName(i, name, sizeof(name));
				ImGui::BeginDisabled(!unlocked);
				if (ImGui::Selectable(name, i == g_NetTeamStageIndex)) {
					netTeamSetStage(i);
				}
				ImGui::EndDisabled();
				if (!unlocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("Locked on this save.");
				}
			}
			ImGui::EndCombo();
		}
		int diff = g_NetTeamDifficulty;
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::Combo("Difficulty", &diff, kDiffNames, 4)) {
			netTeamSetDifficulty(diff);
		}
	} else if (lb->valid) {
		ImGui::TextUnformatted(imguiNetplayStageName(lb->stageindex, stagename, sizeof(stagename)));
		ImGui::TextDisabled("%s", kDiffNames[lb->difficulty & 3]);
	} else {
		ImGui::TextDisabled("Waiting for the host...");
	}

	ImGui::SeparatorText("Seats");
	if (lb->valid) {
		imguiNetplayDrawSeatTable(false);
	}

	// the ready-up: every seated player readies and the mission starts; a
	// --dedicated host is not a player and has no button
	if (!(host && g_NetHostGhost)) {
		const bool ready = netTeamLocalReady() != 0;
		if (ImGui::Button(ready ? "Not ready" : "Ready", ImVec2(120.0f, 0.0f))) {
			netTeamSetReady(!ready);
		}
		ImGui::SameLine();
		ImGui::TextDisabled(ready ? "Waiting for the others." : "The mission starts when everyone is ready.");
	}

	if (host) {
		char why[96];
		const bool can = netTeamCanStart(why, sizeof(why)) != 0;
		ImGui::BeginDisabled(!can);
		if (ImGui::Button("Start now", ImVec2(120.0f, 0.0f))) {
			netTeamStart();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (can) {
			ImGui::TextDisabled("Everyone seated has a role.");
		} else {
			ImGui::TextDisabled("%s", why);
		}
	} else {
		ImGui::TextDisabled("Or the host starts it.");
	}
}

/* ---------------------------------------------------------------- roster --- */

/**
 * What the engine actually has, seat by seat - not the lobby's picture. The
 * role column is g_Vars.playerroles[]; "is" is what the bond / coopplayers /
 * antiplayers pointers say, so a disagreement between the two shows. Idle
 * seats (a role, no client) and the host's ghost show here too.
 */
static void imguiNetplayDrawEngineSeats(void)
{
	ImGui::SeparatorText("Engine seats");
	ImGui::TextDisabled("operative %d, co-op %d, counter-op %d, %d player(s)%s",
			g_Vars.bondplayernum, g_Vars.coopplayernum, g_Vars.antiplayernum, PLAYERCOUNT(),
			g_NetHostGhost ? ", host is a ghost" : "");

	if (!ImGui::BeginTable("##engineseats", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
		return;
	}
	ImGui::TableSetupColumn("seat");
	ImGui::TableSetupColumn("role");
	ImGui::TableSetupColumn("is");
	ImGui::TableSetupColumn("who", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("pick");
	ImGui::TableSetupColumn("state");
	ImGui::TableHeadersRow();

	for (s32 i = 0; i < PLAYERCOUNT() && i < MAX_PLAYERS; i++) {
		struct player *pl = g_Vars.players[i];
		if (!pl) {
			continue;
		}

		const char *is = "-";
		if (pl == g_Vars.bond) {
			is = "operative";
		} else if (g_Vars.coopplayers[i] == pl) {
			is = "co-op";
		} else if (g_Vars.antiplayers[i] == pl) {
			is = "counter-op";
		}

		const struct netclient *who = NULL;
		for (s32 c = 0; c < g_NetMaxClients; c++) {
			if (g_NetClients[c].state >= CLSTATE_LOBBY && g_NetClients[c].player == pl) {
				who = &g_NetClients[c];
				break;
			}
		}

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Text("%d", i + 1);
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(kRoleNames[g_Vars.playerroles[i] & 3]);
		ImGui::TableNextColumn();
		if (pl == g_Vars.bond) {
			ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1.0f), "%s", is);
		} else {
			ImGui::TextUnformatted(is);
		}
		ImGui::TableNextColumn();
		if (who) {
			ImGui::Text("%s%s%s", who->settings.name, who == g_NetLocalClient ? " (you)" : "", who->id == 0 ? " (host)" : "");
		} else {
			ImGui::TextDisabled("idle - no player");
		}
		ImGui::TableNextColumn();
		ImGui::Text("%d", g_PlayerConfigsArray[g_Vars.playerstats[i].mpindex].teamagentindex);
		ImGui::TableNextColumn();
		const struct chrdata *chr = pl->prop ? pl->prop->chr : NULL;
		const bool hidden = chr && (chr->chrflags & CHRCFLAG_HIDDEN);
		const bool cloaked = chr && (chr->hidden & CHRHFLAG_CLOAKED);
		if (pl->isdead) {
			ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "dead");
		} else {
			ImGui::Text("%d%%", (int)(pl->bondhealth * 100.0f + 0.5f));
		}
		if (hidden || cloaked || pl->invincible) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s%s%s", hidden ? " hidden" : "", cloaked ? " cloaked" : "", pl->invincible ? " invincible" : "");
		}
	}
	ImGui::EndTable();
}

static void imguiNetplayDrawRoster(void)
{
	const bool host = g_NetMode == NETMODE_SERVER;
	char stagename[96];

	if (g_MissionConfig.isteam) {
		ImGui::TextUnformatted(imguiNetplayStageName(g_MissionConfig.stageindex, stagename, sizeof(stagename)));
		ImGui::SameLine();
		ImGui::TextDisabled("%s", kDiffNames[g_MissionConfig.difficulty & 3]);
	} else {
		ImGui::TextDisabled("Not a team mission.");
	}
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f + ImGui::GetCursorPosX());
	if (ImGui::Button("Leave")) {
		netDisconnect();
		return;
	}
	ImGui::TextDisabled(host ? "Hosting." : "Joined. The mission ends on the host's word.");

	ImGui::SeparatorText("Seats");
	if (g_NetTeamLobby.valid) {
		imguiNetplayDrawSeatTable(true);
	}

	imguiNetplayDrawEngineSeats();
}

/* --------------------------------------------------------------- the window --- */

void imguiNetplayDrawPanel(void)
{
	imguiNetplayInit();

	if (g_NetMode == NETMODE_NONE) {
		imguiNetplayDrawOffline();
		return;
	}

	if (imguiNetplayInHub()) {
		imguiNetplayDrawLobby();
	} else {
		imguiNetplayDrawRoster();
	}

	if (g_NetMode != NETMODE_NONE) {
		imguiNetplayDrawChat();
	}
}
