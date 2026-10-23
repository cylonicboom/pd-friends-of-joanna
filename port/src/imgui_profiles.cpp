// The profile manager, in fojOS.
//
// One table of every MP profile on the paks plus each seat's engine default,
// and an edit pane for the selected one. All of the file and pd.ini work is
// in mplayer.c (mpprofiles.h); this file is only the window.
//
// Rules it enforces (Catherine, 2026-10-07/08):
//  - seat defaults (deviceserial 0xFFFF, MpPlayer.Player<n>) are shown and
//    never edited
//  - a profile loaded in a seat cannot be deleted
//  - create makes a blank profile; there is no copy
//  - a profile is seated in one seat at a time; seating saves and releases
//    whoever was there first (mpProfileSeat), and is deferred to after the
//    overlay renders, like hot join
//  - and is refused while the save queue holds anything: an edit that has
//    not been flushed yet must not be in flight when a seat changes hands
//  - a profile can be moved to another pak (the game pak, a pool page, a
//    --player pak), move-only: never both copies, never neither
//    (mpProfileMove). Not while seated, and never to or from a read-only pak

#include <stdio.h>
#include <string.h>
// before the project headers: gu.h redeclares cosf/sinf/sqrtf, and clang
// rejects the libc++ declarations if they arrive second
#include <math.h>
#include <vector>

#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"
#include "mod.h"
#include "mpprofiles.h"
#include "mpk.h"
#include "savequeue.h"
// types.h defines bool as s32 for the C side; ImGui wants the real thing
#undef bool
#undef true
#undef false

#include "imgui.h"
#include "imgui_profiles.h"

extern "C" s32 hotjoinProfileCount(void);
extern "C" void hotjoinInvalidateProfiles(void);
extern "C" s32 hotjoinProfileInfo(s32 index, char *name, u32 namelen, struct fileguid *guid, s32 *boundslot);
extern "C" s32 pakGetSerial(s8 device);
extern "C" s32 mpGetNumHeads(void);
extern "C" u32 mpGetNumBodies(void);
extern "C" char *mpGetBodyName(u8 mpbodynum);

// Friends of Joanna: the operative list is the FoJo carousel's own
// (mainmenu.c), options 0..g_FojoHeadCount-1 plus a last one that is the
// profile's own character-select head, which the carousel labels with the
// profile's name
extern "C" s32 g_FojoHeadCount;
extern "C" void fojoInitHeadOptions(void);
extern "C" char *fojoGetHeadName(s32 optionindex);

struct profilerow {
	struct fileguid guid;
	char name[16];
	s32 seat;
	s32 head;
	s32 body;
	s32 operative;
	char pak[40];
	s32 pakreadonly;
};

static std::vector<profilerow> g_ProfRows;
static bool g_ProfStale = true;
static s32 g_ProfDrawnFrame = -10;
static s32 g_ProfSel = -1;          // index into g_ProfRows
static struct fileguid g_ProfSelGuid = { 0, 0 };
static char g_ProfNewName[MPPROFILE_NAME_MAX + 1] = "";
static char g_ProfEditName[MPPROFILE_NAME_MAX + 1] = "";
static bool g_ProfConfirmDelete = false;
static char g_ProfMessage[128] = "";
static s32 g_ProfSeatPick = 0;        // the seat the Seat button targets
static s32 g_ProfPakPick = -1;        // the serial the Move button targets

// one seat change, run by imguiProfilesRunRequests after the frame
enum { PROFREQ_NONE, PROFREQ_SEAT, PROFREQ_UNSEAT, PROFREQ_MOVE };
static s32 g_ProfReq = PROFREQ_NONE;
static struct fileguid g_ProfReqGuid = { 0, 0 };
static s32 g_ProfReqSeat = -1;
static s32 g_ProfReqSerial = -1;

// where a profile lives, for a person: the game pak, a pool page, a --player pak
static void profPakLabel(s32 serial, char *out, size_t outlen, s32 *readonly)
{
	*readonly = 0;

	if (serial == pakGetSerial(SAVEDEVICE_GAMEPAK)) {
		snprintf(out, outlen, "game pak");
	} else if (!mpkPageInfoBySerial(serial, out, (s32)outlen, readonly)) {
		snprintf(out, outlen, "%x?", serial);
	}
}

static void profCopyName(char *out, size_t outlen, const char *in)
{
	size_t o = 0;

	for (const char *p = in ? in : ""; *p && *p != '\n' && o + 1 < outlen; p++) {
		out[o++] = *p;
	}

	out[o] = '\0';
}

static bool profSameGuid(const struct fileguid *a, const struct fileguid *b)
{
	return a->fileid == b->fileid && a->deviceserial == b->deviceserial;
}

static void profRefresh(void)
{
	hotjoinInvalidateProfiles();
	g_ProfRows.clear();

	const s32 count = hotjoinProfileCount();

	for (s32 i = 0; i < count; i++) {
		profilerow row;
		char name[32];
		s32 bound = -1;

		if (!hotjoinProfileInfo(i, name, sizeof(name), &row.guid, &bound)) {
			continue;
		}

		profCopyName(row.name, sizeof(row.name), name);
		row.seat = mpProfileSeatOf(&row.guid);
		row.head = -1;
		row.body = -1;
		mpProfileGetHeadBody(&row.guid, &row.head, &row.body);
		row.operative = mpProfileGetOperative(&row.guid);
		profPakLabel(row.guid.deviceserial, row.pak, sizeof(row.pak), &row.pakreadonly);
		g_ProfRows.push_back(row);
	}

	// keep the selection on the same profile across a rescan
	g_ProfSel = -1;
	for (s32 i = 0; i < (s32)g_ProfRows.size(); i++) {
		if (profSameGuid(&g_ProfRows[i].guid, &g_ProfSelGuid)) {
			g_ProfSel = i;
			break;
		}
	}

	g_ProfStale = false;
}

static void profHeadLabel(s32 head, char *out, size_t outlen)
{
	const char *name = head >= 0 ? modHeadSlotName(head) : NULL;

	if (head < 0) {
		snprintf(out, outlen, "-");
	} else if (name && name[0]) {
		snprintf(out, outlen, "%d %s", head, name);
	} else {
		snprintf(out, outlen, "%d", head);
	}
}

static void profBodyLabel(s32 body, char *out, size_t outlen)
{
	char name[64];

	if (body < 0) {
		snprintf(out, outlen, "-");
		return;
	}

	profCopyName(name, sizeof(name), mpGetBodyName((u8)body));
	snprintf(out, outlen, "%d %s", body, name);
}

static void profOperativeLabel(s32 operative, const char *profilename, char *out, size_t outlen)
{
	if (g_FojoHeadCount == 0) {
		fojoInitHeadOptions();
	}

	if (operative < 0 || operative > g_FojoHeadCount) {
		snprintf(out, outlen, "-");
	} else if (operative == g_FojoHeadCount) {
		snprintf(out, outlen, "%s", profilename);
	} else {
		snprintf(out, outlen, "%s", fojoGetHeadName(operative));
	}
}

static void profSay(const char *fmt, const char *arg)
{
	snprintf(g_ProfMessage, sizeof(g_ProfMessage), fmt, arg);
}

static void profDrawCreate(void)
{
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
	ImGui::InputTextWithHint("##newname", "name", g_ProfNewName, sizeof(g_ProfNewName));
	ImGui::SameLine();
	ImGui::BeginDisabled(g_ProfNewName[0] == '\0');
	if (ImGui::Button("Create blank")) {
		struct fileguid guid;
		const s32 ret = mpProfileCreateBlank(g_ProfNewName, &guid);

		if (ret == 0) {
			profSay("created %s", g_ProfNewName);
			g_ProfSelGuid = guid;
			g_ProfNewName[0] = '\0';
		} else if (ret == -2) {
			profSay("%s", "no free profile slot on the gamepak");
		} else {
			profSay("%s", "create failed (pak error)");
		}

		g_ProfStale = true;
	}
	ImGui::EndDisabled();
}

static void profDrawTable(void)
{
	if (!ImGui::BeginTable("##profiles", 6,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
			ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 9.0f))) {
		return;
	}

	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
	ImGui::TableSetupColumn("seat", ImGuiTableColumnFlags_WidthStretch, 0.5f);
	ImGui::TableSetupColumn("head", ImGuiTableColumnFlags_WidthStretch, 1.0f);
	ImGui::TableSetupColumn("body", ImGuiTableColumnFlags_WidthStretch, 1.0f);
	ImGui::TableSetupColumn("operative", ImGuiTableColumnFlags_WidthStretch, 1.0f);
	ImGui::TableSetupColumn("pak", ImGuiTableColumnFlags_WidthStretch, 0.9f);
	ImGui::TableHeadersRow();

	for (s32 i = 0; i < (s32)g_ProfRows.size(); i++) {
		const profilerow &row = g_ProfRows[i];
		char label[64];

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::PushID(i);
		if (ImGui::Selectable(row.name[0] ? row.name : "(unnamed)", g_ProfSel == i, ImGuiSelectableFlags_SpanAllColumns)) {
			g_ProfSel = i;
			g_ProfSelGuid = row.guid;
			g_ProfPakPick = -1;
			snprintf(g_ProfEditName, sizeof(g_ProfEditName), "%s", row.name);
			g_ProfConfirmDelete = false;
		}
		ImGui::PopID();

		ImGui::TableNextColumn();
		if (row.seat >= 0) {
			ImGui::Text("%d", row.seat + 1);
		} else {
			ImGui::TextDisabled("-");
		}

		ImGui::TableNextColumn();
		profHeadLabel(row.head, label, sizeof(label));
		ImGui::TextUnformatted(label);

		ImGui::TableNextColumn();
		profBodyLabel(row.body, label, sizeof(label));
		ImGui::TextUnformatted(label);

		ImGui::TableNextColumn();
		profOperativeLabel(row.operative, row.name, label, sizeof(label));
		ImGui::TextUnformatted(label);

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(row.pak);
	}

	// each seat's engine default, for reference only
	for (s32 p = 0; p < MAX_PLAYERS; p++) {
		const struct mpplayerconfig *cfg = &g_PlayerConfigsArray[p];
		const bool unbound = !cfg->fileguid.fileid && !cfg->fileguid.deviceserial;

		if (!unbound && cfg->fileguid.deviceserial != 0xFFFF) {
			continue;
		}

		char name[16];
		char label[64];
		profCopyName(name, sizeof(name), cfg->base.name);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s (seat default)", name[0] ? name : "-");
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%d", p + 1);
		ImGui::TableNextColumn();
		profHeadLabel(cfg->base.mpheadnum, label, sizeof(label));
		ImGui::TextDisabled("%s", label);
		ImGui::TableNextColumn();
		profBodyLabel(cfg->base.mpbodynum, label, sizeof(label));
		ImGui::TextDisabled("%s", label);
		ImGui::TableNextColumn();
		profOperativeLabel(cfg->teamagentindex, name, label, sizeof(label));
		ImGui::TextDisabled("%s", label);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("-");
	}

	ImGui::EndTable();
}

static const char *profSeatError(s32 ret)
{
	switch (ret) {
	case MPPROFILE_SEAT_PAKERROR:  return "pak error, nothing moved";
	case MPPROFILE_SEAT_ELSEWHERE: return "already in another seat";
	case MPPROFILE_SEAT_LOCKED:    return "seats change between missions";
	default:                       return "bad seat or profile";
	}
}

// who is in a seat, for the seat picker
static void profSeatLabel(s32 seat, char *out, size_t outlen)
{
	const struct mpplayerconfig *cfg = &g_PlayerConfigsArray[seat];
	const bool isdefault = (!cfg->fileguid.fileid && !cfg->fileguid.deviceserial)
		|| cfg->fileguid.deviceserial == 0xFFFF;
	char name[16];

	profCopyName(name, sizeof(name), cfg->base.name);
	snprintf(out, outlen, "%d: %s%s", seat + 1, name[0] ? name : "-", isdefault ? " (default)" : "");
}

static void profDrawSeat(const profilerow &row)
{
	char label[48];
	const bool locked = mpProfileSeatsLocked() != 0;
	const bool unsaved = saveQueueIsDirty() != 0;
	const bool pending = g_ProfReq != PROFREQ_NONE || unsaved;

	if (g_ProfSeatPick < 0 || g_ProfSeatPick >= MAX_PLAYERS) {
		g_ProfSeatPick = 0;
	}

	ImGui::BeginDisabled(locked || pending);

	profSeatLabel(g_ProfSeatPick, label, sizeof(label));
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
	if (ImGui::BeginCombo("##seatpick", label)) {
		for (s32 p = 0; p < MAX_PLAYERS; p++) {
			char opt[48];
			profSeatLabel(p, opt, sizeof(opt));
			ImGui::PushID(p);
			if (ImGui::Selectable(opt, p == g_ProfSeatPick)) {
				g_ProfSeatPick = p;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(row.seat >= 0);
	if (ImGui::Button("Seat")) {
		g_ProfReq = PROFREQ_SEAT;
		g_ProfReqGuid = row.guid;
		g_ProfReqSeat = g_ProfSeatPick;
	}
	ImGui::EndDisabled();
	if (row.seat >= 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("Already in seat %d. Unseat it there first.", row.seat + 1);
	}

	if (row.seat >= 0) {
		ImGui::SameLine();
		if (ImGui::Button("Unseat")) {
			g_ProfReq = PROFREQ_UNSEAT;
			g_ProfReqGuid = row.guid;
			g_ProfReqSeat = row.seat;
		}
	}

	ImGui::EndDisabled();

	if (locked) {
		ImGui::SameLine();
		ImGui::TextDisabled("seats change between missions");
	} else if (unsaved) {
		ImGui::SameLine();
		ImGui::TextDisabled("saving...");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Edits are waiting to be written. Seats unlock when the save queue\n"
					"flushes (next idle frame, %u frames at most).",
					saveQueueDeadlineFrames() - saveQueueFramesPending());
		}
	}
}

static const char *profMoveError(s32 ret)
{
	switch (ret) {
	case MPPROFILE_MOVE_SEATED:   return "unseat it first";
	case MPPROFILE_MOVE_FULL:     return "no free profile slot on that pak";
	case MPPROFILE_MOVE_READONLY: return "a read-only pak can't take or give up a profile";
	case MPPROFILE_MOVE_UNSAVED:  return "edits still waiting to be written; nothing moved";
	case MPPROFILE_MOVE_PAKERROR: return "pak error, nothing moved";
	default:                      return "bad pak or profile";
	}
}

// the pak picker: the game pak, then every page that has a PD filesystem
// and can be written
static void profDrawPak(const profilerow &row)
{
	char label[48];
	char current[48];
	s32 readonly;
	const s32 gamepak = pakGetSerial(SAVEDEVICE_GAMEPAK);
	const bool unsaved = saveQueueIsDirty() != 0;
	const bool pending = g_ProfReq != PROFREQ_NONE || unsaved;

	if (g_ProfPakPick <= 0) {
		g_ProfPakPick = row.guid.deviceserial;
	}

	profPakLabel(g_ProfPakPick, current, sizeof(current), &readonly);

	ImGui::BeginDisabled(row.seat >= 0 || row.pakreadonly || pending);
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
	if (ImGui::BeginCombo("##pakpick", current)) {
		if (gamepak > 0 && ImGui::Selectable("game pak", g_ProfPakPick == gamepak)) {
			g_ProfPakPick = gamepak;
		}

		for (s32 i = 0; ; i++) {
			s32 serial;

			if (!mpkPageInfo(i, &serial, label, sizeof(label), &readonly)) {
				break;
			}

			if (serial <= 0 || readonly) {
				continue;
			}

			ImGui::PushID(i);
			if (ImGui::Selectable(label, serial == g_ProfPakPick)) {
				g_ProfPakPick = serial;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(g_ProfPakPick == row.guid.deviceserial);
	if (ImGui::Button("Move")) {
		g_ProfReq = PROFREQ_MOVE;
		g_ProfReqGuid = row.guid;
		g_ProfReqSerial = g_ProfPakPick;
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	if (row.seat >= 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("Loaded in seat %d. Unseat it first.", row.seat + 1);
	} else if (row.pakreadonly && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("On a --player-ro pak, which is never written.");
	}
}

void imguiProfilesRunRequests(void)
{
	const s32 req = g_ProfReq;
	s32 ret;

	if (req == PROFREQ_NONE) {
		return;
	}

	g_ProfReq = PROFREQ_NONE;
	g_ProfStale = true;

	// the panel greys the buttons, but the request runs a frame later and
	// something may have been edited in between
	if (saveQueueIsDirty()) {
		profSay("%s", "edits still waiting to be written; nothing done");
		return;
	}

	if (req == PROFREQ_MOVE) {
		struct fileguid moved;
		char where[48];
		char msg[96];
		s32 readonly;

		ret = mpProfileMove(&g_ProfReqGuid, g_ProfReqSerial, &moved);

		if (ret == MPPROFILE_MOVE_OK) {
			profPakLabel(moved.deviceserial, where, sizeof(where), &readonly);
			snprintf(msg, sizeof(msg), "moved to %s", where);
			profSay("%s", msg);
			g_ProfSelGuid = moved;
			g_ProfPakPick = -1;
		} else {
			profSay("%s", profMoveError(ret));
		}

		return;
	}

	if (req == PROFREQ_UNSEAT) {
		// only if the profile asked about is still the one there
		if (mpProfileSeatOf(&g_ProfReqGuid) != g_ProfReqSeat) {
			profSay("%s", "seat changed under the request; nothing done");
			return;
		}

		ret = mpProfileUnseat(g_ProfReqSeat);
	} else {
		ret = mpProfileSeat(&g_ProfReqGuid, g_ProfReqSeat);
	}

	if (ret == MPPROFILE_SEAT_OK) {
		char msg[64];
		snprintf(msg, sizeof(msg), req == PROFREQ_SEAT ? "seated in %d" : "seat %d back to its default",
				g_ProfReqSeat + 1);
		profSay("%s", msg);
	} else {
		profSay("%s", profSeatError(ret));
	}
}

static void profDrawEdit(void)
{
	if (g_ProfSel < 0 || g_ProfSel >= (s32)g_ProfRows.size()) {
		ImGui::TextDisabled("Select a profile to edit it.");
		return;
	}

	profilerow &row = g_ProfRows[g_ProfSel];
	char label[64];

	ImGui::SeparatorText(row.name[0] ? row.name : "(unnamed)");

	if (row.seat >= 0) {
		ImGui::TextDisabled("loaded in seat %d: changes apply live and save through the queue", row.seat + 1);
	}

	profDrawSeat(row);
	profDrawPak(row);

	// name
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
	ImGui::InputText("##editname", g_ProfEditName, sizeof(g_ProfEditName));
	ImGui::SameLine();
	ImGui::BeginDisabled(g_ProfEditName[0] == '\0' || strcmp(g_ProfEditName, row.name) == 0);
	if (ImGui::Button("Rename")) {
		if (mpProfileRename(&row.guid, g_ProfEditName) == 0) {
			profSay("renamed to %s", g_ProfEditName);
		} else {
			profSay("%s", "rename failed");
		}
		g_ProfStale = true;
	}
	ImGui::EndDisabled();

	// head and body
	s32 head = row.head;
	s32 body = row.body;
	bool changed = false;

	profHeadLabel(head, label, sizeof(label));
	if (ImGui::BeginCombo("Head", label)) {
		for (s32 h = 0; h < mpGetNumHeads(); h++) {
			char opt[64];
			profHeadLabel(h, opt, sizeof(opt));
			ImGui::PushID(h);
			if (ImGui::Selectable(opt, h == head)) {
				head = h;
				changed = true;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	profBodyLabel(body, label, sizeof(label));
	if (ImGui::BeginCombo("Body", label)) {
		for (s32 b = 0; b < (s32)mpGetNumBodies(); b++) {
			char opt[64];
			profBodyLabel(b, opt, sizeof(opt));
			ImGui::PushID(b);
			if (ImGui::Selectable(opt, b == body)) {
				body = b;
				changed = true;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	if (changed && head >= 0 && body >= 0) {
		if (mpProfileSetHeadBody(&row.guid, head, body) == 0) {
			row.head = head;
			row.body = body;
		} else {
			profSay("%s", "head/body change failed");
			g_ProfStale = true;
		}
	}

	// operative
	profOperativeLabel(row.operative, row.name, label, sizeof(label));
	if (ImGui::BeginCombo("Operative", label)) {
		for (s32 o = 0; o <= g_FojoHeadCount; o++) {
			char opt[64];
			profOperativeLabel(o, row.name, opt, sizeof(opt));
			ImGui::PushID(o);
			if (ImGui::Selectable(opt, o == row.operative)) {
				if (mpProfileSetOperative(&row.guid, o) == 0) {
					row.operative = o;
				} else {
					profSay("%s", "operative change failed");
				}
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	// delete, two-step
	ImGui::Spacing();
	ImGui::BeginDisabled(row.seat >= 0);
	if (!g_ProfConfirmDelete) {
		if (ImGui::Button("Delete...")) {
			g_ProfConfirmDelete = true;
		}
	} else {
		ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "Delete %s for good?", row.name);
		ImGui::SameLine();
		if (ImGui::Button("Delete")) {
			char gone[16];
			snprintf(gone, sizeof(gone), "%s", row.name);
			if (mpProfileDelete(&row.guid) == 0) {
				profSay("deleted %s", gone);
				g_ProfSelGuid.fileid = 0;
				g_ProfSelGuid.deviceserial = 0;
			} else {
				profSay("%s", "delete failed");
			}
			g_ProfConfirmDelete = false;
			g_ProfStale = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Keep")) {
			g_ProfConfirmDelete = false;
		}
	}
	ImGui::EndDisabled();
	if (row.seat >= 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("Loaded in seat %d. Unseat it first.", row.seat + 1);
	}
}

void imguiProfilesDrawPanel(void)
{
	// the list is a snapshot: rescan when the panel comes up, or on demand
	if (ImGui::GetFrameCount() - g_ProfDrawnFrame > 1) {
		g_ProfStale = true;
	}
	g_ProfDrawnFrame = ImGui::GetFrameCount();

	if (g_ProfStale) {
		profRefresh();
	}

	if (ImGui::SmallButton("Rescan paks")) {
		g_ProfStale = true;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%d profile%s", (s32)g_ProfRows.size(), g_ProfRows.size() == 1 ? "" : "s");

	profDrawCreate();
	profDrawTable();
	profDrawEdit();

	if (g_ProfMessage[0]) {
		ImGui::Separator();
		ImGui::TextDisabled("%s", g_ProfMessage);
	}
}
