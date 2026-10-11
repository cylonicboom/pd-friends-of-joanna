// Options, in fojOS: the std Options pages (Audio, Video, Control, Display)
// and the port's Extended ones, in one window.
//
// Both are sets of menuitem tables - mainmenu.c and mplayer/setup.c for the
// std pages, optionsmenu.c for Extended - and every row does its work in its
// own handler (MENUOP_GET / SET / GETSLIDER / ...). This panel walks those
// same tables and calls those same handlers, so there is one implementation
// of every setting and the front ends cannot drift: a row added upstream
// turns up here without anyone touching this file. The std menus are left
// as they are.
//
// Two things the tables cannot do for ImGui, and so are done here:
//  - the player. Handlers find their player three ways: Extended ones read
//    optionsmenu.c's g_ExtMenuPlayer (set by a Select Player dialog that
//    misbehaves in 4P), the std Control page reads g_MpPlayerNum, and the
//    Display rows go through menuResolveOptionMpChrNum, which otherwise
//    prefers currentplayerstats in coop/anti. Here it is one selector at the
//    top, applied to all three around every handler call and put back after.
//  - key bindings. A std bind row's SET pushes a "press a key" dialog. Here
//    the capture is inline, through inputArmBindCapture.

#include <stdio.h>
#include <string.h>
// before the project headers: gu.h redeclares cosf/sinf/sqrtf, and clang
// rejects the libc++ declarations if they arrive second
#include <math.h>

#include "bss.h"
#include "constants.h"
#include "data.h"
#include "types.h"
#include "input.h"
#include "savequeue.h"
#include "optionsmenu.h"
#include "seatprofile.h"
// types.h defines bool as s32 for the C side; ImGui wants the real thing
#undef bool
#undef true
#undef false

#include "imgui.h"
#include "imgui_extoptions.h"

extern "C" char *menuResolveParam2Text(struct menuitem *item);
extern "C" char *langGet(s32 textid);
extern "C" s32 g_MenuOptionPlayerOverride;
extern "C" s32 menuCommitOptionChanges(void);
extern "C" void mpProfileMarkDirty(s32 playernum);

// the std Options pages (mainmenu.c; Control is mplayer/setup.c's, via data.h)
extern "C" struct menudialogdef g_AudioOptionsMenuDialog;
extern "C" struct menudialogdef g_VideoOptionsMenuDialog;
extern "C" struct menudialogdef g_CiDisplayMenuDialog;

enum {
	EXTTAB_VIDEO,
	EXTTAB_AUDIO,
	EXTTAB_CONTROL,
	EXTTAB_DISPLAY,
	EXTTAB_MOUSE,
	EXTTAB_CONTROLLER,
	EXTTAB_GAME,
	EXTTAB_BINDS,
};

static s32 g_ExtOptPlayer = 0;

// a std row changed something that lives in the game file or a profile, and
// it has not been written yet (see extCommit)
static bool g_ExtOptCommitPending = false;

// the tab being drawn is a std per-player page, whose rows live in the
// seated profile's wad
static bool g_ExtOptTabInProfile = false;

// a bind waiting for its key: which player, which control, which of its slots
static bool g_ExtOptCapturing = false;
static s32 g_ExtOptDrawnFrame = -10;
static s32 g_ExtOptCapPlayer = 0;
static s32 g_ExtOptCapBind = 0;
static s32 g_ExtOptCapSlot = 0;

// Only while the panel is actually on screen: a window closed or an overlay
// hidden mid-capture must not leave the overlay ignoring input.
bool imguiExtOptionsIsCapturing(void)
{
	return g_ExtOptCapturing && ImGui::GetFrameCount() - g_ExtOptDrawnFrame <= 1;
}

// Point every way a handler can find its player at the picked one, for the
// length of one call. g_MpPlayerNum is the std menu's own and is put back
// straight away; the override is -1 whenever this panel is not mid-call.
struct ExtPlayerScope {
	s32 prevmpplayernum;

	ExtPlayerScope()
	{
		prevmpplayernum = g_MpPlayerNum;
		optionsmenuSetExtPlayer(g_ExtOptPlayer);
		g_MpPlayerNum = g_ExtOptPlayer;
		g_MenuOptionPlayerOverride = g_ExtOptPlayer;
	}

	~ExtPlayerScope()
	{
		g_MenuOptionPlayerOverride = -1;
		g_MpPlayerNum = prevmpplayernum;
	}
};

static uintptr_t extCall(struct menuitem *item, s32 op, union handlerdata *data)
{
	ExtPlayerScope scope;
	return item->handler ? item->handler(op, item, data) : 0;
}

// A menu text is a language id below 0x5a00, else a string (menuResolveText).
static const char *extTextOf(uintptr_t thing)
{
	if (!thing) {
		return "";
	}

	if (thing < 0x5a00) {
		const char *s = langGet((s32)thing);
		return s ? s : "";
	}

	return (const char *)thing;
}

// Copy a menu label, dropping the trailing newline the std menu uses as a
// line break, and doubling '%' so it is safe as an ImGui format string.
static void extCopyLabel(char *out, size_t outlen, const char *in, bool forformat)
{
	size_t o = 0;

	for (const char *p = in ? in : ""; *p && o + 2 < outlen; p++) {
		if (*p == '\n') {
			continue;
		}
		if (forformat && *p == '%') {
			out[o++] = '%';
		}
		out[o++] = *p;
	}

	out[o] = '\0';
}

static void extItemLabel(struct menuitem *item, char *out, size_t outlen)
{
	ExtPlayerScope scope;
	extCopyLabel(out, outlen, item->param2 ? menuResolveParam2Text(item) : "", false);
}

static void extChanged(void)
{
	// the std menu leaves these for the exit save; fojOS commits them through
	// the queue so a crash or kill after a change does not lose it
	saveQueueMarkConfig();

	// std rows also mark g_Vars.modifiedfiles (the game file) and change the
	// seated profile (control style, options, displayoptions are in its wad)
	g_ExtOptCommitPending = true;

	if (g_ExtOptTabInProfile) {
		mpProfileMarkDirty(g_ExtOptPlayer);
	}
}

// What closing the std Options dialog does - menuCommitOptionChanges - once
// nothing is being dragged, so a slider is one game-file save, not one a
// frame. Runs with the std menu's own g_MpPlayerNum: a failed save pushes its
// error dialog where the std one would.
static void extCommit(void)
{
	if (!g_ExtOptCommitPending || ImGui::IsAnyItemActive()) {
		return;
	}

	g_ExtOptCommitPending = false;
	menuCommitOptionChanges();
}

static void extDrawItems(struct menuitem *items, s32 depth);

static void extDrawItem(struct menuitem *item, s32 depth)
{
	const bool opensdialog = item->type == MENUITEMTYPE_SELECTABLE
		&& (item->flags & MENUITEMFLAG_SELECTABLE_OPENSDIALOG);
	union handlerdata data;
	char label[128];
	bool disabled = false;

	// "Back" rows close a std dialog; tabs and tree nodes replace them
	if (item->type == MENUITEMTYPE_SELECTABLE && (item->flags & MENUITEMFLAG_SELECTABLE_CLOSESDIALOG)) {
		return;
	}

	// an opens-dialog row keeps a menudialogdef in the handler slot, not a
	// function, so it must never be called
	if (!opensdialog && item->handler) {
		memset(&data, 0, sizeof(data));
		if (extCall(item, MENUOP_CHECKHIDDEN, &data)) {
			return;
		}
		memset(&data, 0, sizeof(data));
		disabled = extCall(item, MENUOP_CHECKDISABLED, &data) != 0;
	}

	extItemLabel(item, label, sizeof(label));

	ImGui::PushID(item);
	ImGui::BeginDisabled(disabled);

	switch (item->type) {
	case MENUITEMTYPE_SEPARATOR:
		ImGui::Separator();
		break;
	case MENUITEMTYPE_LABEL:
		if (label[0]) {
			ImGui::TextUnformatted(label);
		}
		break;
	case MENUITEMTYPE_CHECKBOX: {
		memset(&data, 0, sizeof(data));
		bool v = extCall(item, MENUOP_GET, &data) != 0;
		if (ImGui::Checkbox(label, &v)) {
			memset(&data, 0, sizeof(data));
			data.checkbox.value = v;
			extCall(item, MENUOP_SET, &data);
			extChanged();
		}
		break;
	}
	case MENUITEMTYPE_SLIDER: {
		char text[64];
		char fmt[140];
		memset(&data, 0, sizeof(data));
		extCall(item, MENUOP_GETSLIDER, &data);
		s32 v = (s32)data.slider.value;
		const s32 max = (s32)item->param3;

		// rows that format their own value (e.g. "60 FPS", "0.50") say so
		// through GETSLIDERLABEL; the rest show the raw step
		text[0] = '\0';
		memset(&data, 0, sizeof(data));
		data.slider.value = v;
		data.slider.label = text;
		extCall(item, MENUOP_GETSLIDERLABEL, &data);
		if (text[0]) {
			extCopyLabel(fmt, sizeof(fmt), text, true);
		} else {
			snprintf(fmt, sizeof(fmt), "%%d");
		}

		if (ImGui::SliderInt(label, &v, 0, max > 0 ? max : 1, fmt, ImGuiSliderFlags_AlwaysClamp)) {
			memset(&data, 0, sizeof(data));
			data.slider.value = v;
			extCall(item, MENUOP_SET, &data);
			extChanged();
		}
		break;
	}
	case MENUITEMTYPE_DROPDOWN: {
		char preview[128];
		memset(&data, 0, sizeof(data));
		extCall(item, MENUOP_GETOPTIONCOUNT, &data);
		const s32 count = (s32)data.dropdown.value;
		memset(&data, 0, sizeof(data));
		extCall(item, MENUOP_GETSELECTEDINDEX, &data);
		const s32 selected = (s32)data.dropdown.value;

		// option text often comes back in one static buffer per handler, so
		// copy each one before asking for the next
		preview[0] = '\0';
		if (selected >= 0 && selected < count) {
			memset(&data, 0, sizeof(data));
			data.dropdown.value = selected;
			extCopyLabel(preview, sizeof(preview), extTextOf(extCall(item, MENUOP_GETOPTIONTEXT, &data)), false);
		}

		if (ImGui::BeginCombo(label, preview)) {
			for (s32 i = 0; i < count; i++) {
				char opt[128];
				memset(&data, 0, sizeof(data));
				data.dropdown.value = i;
				extCopyLabel(opt, sizeof(opt), extTextOf(extCall(item, MENUOP_GETOPTIONTEXT, &data)), false);
				ImGui::PushID(i);
				if (ImGui::Selectable(opt, i == selected)) {
					memset(&data, 0, sizeof(data));
					data.dropdown.value = i;
					extCall(item, MENUOP_SET, &data);
					extChanged();
				}
				if (i == selected) {
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		break;
	}
	case MENUITEMTYPE_SELECTABLE:
		if (opensdialog) {
			struct menudialogdef *dialog = (struct menudialogdef *)(void *)item->handler;
			// sub-dialogs (Stick Settings, Crosshair Colour) open in place
			if (dialog && depth < 3 && ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth)) {
				extDrawItems(dialog->items, depth + 1);
				ImGui::TreePop();
			}
		} else if (item->handler && ImGui::Button(label)) {
			memset(&data, 0, sizeof(data));
			extCall(item, MENUOP_SET, &data);
			extChanged();
		}
		break;
	case MENUITEMTYPE_COLORBOX: {
		memset(&data, 0, sizeof(data));
		extCall(item, MENUOP_GETCOLOUR, &data);
		const u32 c = data.label.colour1;
		const ImVec4 col((c >> 24 & 0xff) / 255.0f, (c >> 16 & 0xff) / 255.0f, (c >> 8 & 0xff) / 255.0f, (c & 0xff) / 255.0f);
		ImGui::ColorButton("##preview", col, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(ImGui::GetFrameHeight() * 3.0f, ImGui::GetFrameHeight()));
		break;
	}
	default:
		break;
	}

	ImGui::EndDisabled();
	ImGui::PopID();
}

static void extDrawItems(struct menuitem *items, s32 depth)
{
	if (!items) {
		return;
	}

	for (struct menuitem *item = items; item->type != MENUITEMTYPE_END; item++) {
		extDrawItem(item, depth);
	}
}

static void extKeyName(s32 vk, char *out, size_t outlen)
{
	const char *name = vk ? inputGetKeyName(vk) : NULL;

	if (!name || !name[0]) {
		snprintf(out, outlen, "-");
		return;
	}

	snprintf(out, outlen, "%s", name);

	for (char *p = out; *p; p++) {
		if (*p == '_') {
			*p = ' ';
		}
	}
}

// Poll for the captured key. Escape cancels and Delete clears the slot, the
// same rules as the std Bind dialog (menuhandlerDoBind).
static void extTickCapture(void)
{
	if (!g_ExtOptCapturing) {
		return;
	}

	inputArmBindCapture();

	const s32 key = inputGetLastKey();

	if (!key) {
		return;
	}

	inputClearLastKey();
	inputDisarmBindCapture();
	g_ExtOptCapturing = false;

	if (key == VK_ESCAPE) {
		return;
	}

	inputKeyBind(g_ExtOptCapPlayer, optionsmenuGetBindCk(g_ExtOptCapBind), g_ExtOptCapSlot, key == VK_DELETE ? 0 : key);
	extChanged();
}

static void extDrawBinds(void)
{
	const s32 numbinds = optionsmenuGetNumBinds();

	extTickCapture();

	if (g_ExtOptCapturing) {
		optionsmenuSetExtPlayer(g_ExtOptCapPlayer);
		char name[64];
		extCopyLabel(name, sizeof(name), optionsmenuGetBindName(g_ExtOptCapBind), false);
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s, %s, slot %d: press a key or button",
				optionsmenuPlayerName(g_ExtOptCapPlayer), name, g_ExtOptCapSlot + 1);
		ImGui::TextDisabled("Esc cancels, Delete clears the slot");
	} else {
		ImGui::TextDisabled("Click a slot, then press the key or button for it.");
	}

	if (ImGui::BeginTable("##extbinds", 1 + INPUT_MAX_BINDS,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
			ImVec2(0.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.5f))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch, 1.4f);
		for (s32 s = 0; s < INPUT_MAX_BINDS; s++) {
			char col[16];
			snprintf(col, sizeof(col), "%d", s + 1);
			ImGui::TableSetupColumn(col, ImGuiTableColumnFlags_WidthStretch, 1.0f);
		}
		ImGui::TableHeadersRow();

		for (s32 b = 0; b < numbinds; b++) {
			char name[64];
			optionsmenuSetExtPlayer(g_ExtOptPlayer);
			extCopyLabel(name, sizeof(name), optionsmenuGetBindName(b), false);
			const u32 *binds = inputKeyGetBinds(g_ExtOptPlayer, optionsmenuGetBindCk(b));

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(name);

			for (s32 s = 0; s < INPUT_MAX_BINDS; s++) {
				char key[64];
				const bool waiting = g_ExtOptCapturing && g_ExtOptCapPlayer == g_ExtOptPlayer
					&& g_ExtOptCapBind == b && g_ExtOptCapSlot == s;

				ImGui::TableNextColumn();
				ImGui::PushID(b * INPUT_MAX_BINDS + s);

				if (waiting) {
					snprintf(key, sizeof(key), "...");
				} else {
					extKeyName(binds ? (s32)binds[s] : 0, key, sizeof(key));
				}

				ImGui::BeginDisabled(g_ExtOptCapturing && !waiting);
				if (ImGui::Button(key, ImVec2(-FLT_MIN, 0.0f)) && !g_ExtOptCapturing) {
					g_ExtOptCapturing = true;
					g_ExtOptCapPlayer = g_ExtOptPlayer;
					g_ExtOptCapBind = b;
					g_ExtOptCapSlot = s;
					inputClearLastKey();
					inputArmBindCapture();
				}
				ImGui::EndDisabled();

				ImGui::PopID();
			}
		}

		ImGui::EndTable();
	}

	// the table's own rows past the binds: the two reset buttons
	ImGui::BeginDisabled(g_ExtOptCapturing);
	struct menuitem *items = g_ExtendedBindsMenuDialog.items;
	for (struct menuitem *item = items; item->type != MENUITEMTYPE_END; item++) {
		if (item->type == MENUITEMTYPE_SELECTABLE && !(item->flags & (MENUITEMFLAG_SELECTABLE_OPENSDIALOG | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG))) {
			extDrawItem(item, 0);
			ImGui::SameLine();
		}
	}
	ImGui::NewLine();
	ImGui::EndDisabled();
}

static void extDrawPlayerPicker(void)
{
	ImGui::TextUnformatted("Player");

	// named for the profile in the seat, as the Team Missions menus do;
	// "Player <n>" only when the seat has no file
	for (s32 p = 0; p < MAX_PLAYERS; p++) {
		char name[48];
		snprintf(name, sizeof(name), "%s##seat%d", optionsmenuPlayerName(p), p);
		ImGui::SameLine();
		ImGui::BeginDisabled(g_ExtOptCapturing);
		if (ImGui::RadioButton(name, g_ExtOptPlayer == p)) {
			g_ExtOptPlayer = p;
		}
		ImGui::EndDisabled();
	}

	// which device that player is on, so picking the right number is easy
	const s32 ctrl = inputGetAssignedControllerId(g_ExtOptPlayer);
	ImGui::SameLine();
	if (inputKbmPlayer() == g_ExtOptPlayer) {
		ImGui::TextDisabled("  keyboard + mouse%s", ctrl >= 0 ? ", plus a pad" : "");
	} else if (ctrl >= 0) {
		ImGui::TextDisabled("  %s", inputGetConnectedControllerName(ctrl));
	} else {
		ImGui::TextDisabled("  no device");
	}
	// whose save file the Game options and binds are being read from and
	// written to (Mouse and Controller stay with the seat)
	const char *slug = seatProfileSlug(g_ExtOptPlayer);
	ImGui::TextDisabled("options saved under [%s]", slug[0] ? slug : "-");
}

void imguiExtOptionsDrawPanel(void)
{
	// std is the vanilla page drawn first, ext the Extended one under it;
	// either may be NULL. Display is the Combat Simulator's page (subtitles,
	// mission time, player name): the solo one repeats Control's HUD rows.
	static const struct {
		const char *name;
		struct menudialogdef *std;
		struct menudialogdef *ext;
		bool perplayer;
	} tabs[] = {
		{ "Video",        &g_VideoOptionsMenuDialog, &g_ExtendedVideoMenuDialog,      false },
		{ "Audio",        &g_AudioOptionsMenuDialog, &g_ExtendedAudioMenuDialog,      false },
		{ "Control",      &g_MpControlMenuDialog,    NULL,                            true  },
		{ "Display",      &g_CiDisplayMenuDialog,    NULL,                            true  },
		{ "Mouse",        NULL,                      &g_ExtendedMouseMenuDialog,      true  },
		{ "Controller",   NULL,                      &g_ExtendedControllerMenuDialog, true  },
		{ "Game",         NULL,                      &g_ExtendedGameMenuDialog,       true  },
		{ "Key Bindings", NULL,                      &g_ExtendedBindsMenuDialog,      true  },
	};

	// keep the std menu's own idea of the player where it was, whatever this
	// panel does to it while drawing
	const s32 stdplayer = optionsmenuGetExtPlayer();

	// a capture left behind by a frame where the panel was not drawn is dead
	if (g_ExtOptCapturing && ImGui::GetFrameCount() - g_ExtOptDrawnFrame > 1) {
		g_ExtOptCapturing = false;
		inputDisarmBindCapture();
	}

	g_ExtOptDrawnFrame = ImGui::GetFrameCount();

	if (ImGui::BeginTabBar("##extoptions")) {
		for (s32 t = 0; t < (s32)(sizeof(tabs) / sizeof(tabs[0])); t++) {
			// a pending capture keeps its tab, so it cannot be left armed
			// on a tab nobody is looking at
			const ImGuiTabItemFlags flags = (g_ExtOptCapturing && t == EXTTAB_BINDS) ? ImGuiTabItemFlags_SetSelected : 0;

			if (!ImGui::BeginTabItem(tabs[t].name, NULL, flags)) {
				continue;
			}

			g_ExtOptTabInProfile = tabs[t].perplayer && tabs[t].std;

			if (tabs[t].perplayer) {
				extDrawPlayerPicker();
				ImGui::Separator();
			}

			if (t == EXTTAB_BINDS) {
				extDrawBinds();
			} else if (ImGui::BeginChild("##extitems")) {
				if (tabs[t].std) {
					extDrawItems(tabs[t].std->items, 0);
				}
				if (tabs[t].std && tabs[t].ext) {
					ImGui::Spacing();
					ImGui::SeparatorText("Extended");
				}
				if (tabs[t].ext) {
					extDrawItems(tabs[t].ext->items, 0);
				}
				ImGui::EndChild();
			} else {
				ImGui::EndChild();
			}

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	optionsmenuSetExtPlayer(stdplayer);
	extCommit();
}
