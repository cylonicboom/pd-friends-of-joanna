#include <ultra64.h>
#include <stdio.h>
#include "constants.h"
#include "game/fojofriends.h"
#include "game/hudtint.h"
#include "game/mainmenu.h"
#include "game/menugfx.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "mod.h"

/**
 * The friends of Jo: whose colour a player's HUD and the slow stars' glows
 * take. The colours are their favourite colours, alpha 0xb0 against the
 * crosshair's vanilla 0x28 -- a pastel at 0x28 washes out to nothing. Treat
 * 0xb0 as the middle of a range to tune; 0x90-0xd0 is where it should land.
 */
u32 g_FojoFriendColours[NUM_FOJO_FRIENDS] = {
	/* FOJO_INDEX_JOANNA */ 0x7ee0e6b0,
	/* FOJO_INDEX_VELVET */ 0xe0a6c4b0,
	/* FOJO_INDEX_MIKADO */ 0xffd0a6b0,
	/* FOJO_INDEX_POPLIN */ 0xfb9c8db0,
	/* FOJO_INDEX_CALICO */ 0xa6e6bcb0,
	/* FOJO_INDEX_WILLOW */ 0x8e96c8b0,
};

static const char *g_FojoFriendNames[NUM_FOJO_FRIENDS] = {
	"Joanna", "Velvet", "Mikado", "Poplin", "Calico", "Willow",
};

// modconfig names, as fojoHeadConfigName has them; joanna and velvet are built in
static const char *g_FojoFriendHeadNames[NUM_FOJO_FRIENDS] = {
	NULL, NULL, "head_mikado", "head_foslerfer", "head_catherine", "head_willow",
};

extern s32 g_FojoHeadCount;

static s32 g_FojoFriendHeads[NUM_FOJO_FRIENDS];
static bool g_FojoFriendHeadsResolved = false;

// the hud asks per green, several times a frame, so resolve the names once
static void fojoFriendsResolveHeads(void)
{
	s32 i;

	g_FojoFriendHeads[FOJO_INDEX_JOANNA] = FOJO_HEAD_JOANNA;
	g_FojoFriendHeads[FOJO_INDEX_VELVET] = FOJO_HEAD_VELVET;

	for (i = FOJO_INDEX_MIKADO; i < NUM_FOJO_FRIENDS; i++) {
		g_FojoFriendHeads[i] = modLookupHeadByName(g_FojoFriendHeadNames[i]);
	}

	g_FojoFriendHeadsResolved = true;
}

/**
 * Which friend a player is, from the operative they picked: carousel slot ->
 * mpheadnum -> friend. The slot number alone isn't enough, because the
 * carousel skips heads that aren't loaded and mikado without a jpn rom.
 *
 * The last slot is the player's own combat sim head. That's a guest, not a
 * friend, and gets -1 like anything else unrecognised.
 */
s32 fojoFriendForMpIndex(s32 mpindex)
{
	s32 slot;
	s32 head;
	s32 i;

	if (mpindex < 0 || mpindex >= MAX_PLAYERS) {
		return -1;
	}

	if (g_FojoHeadCount == 0) {
		fojoInitHeadOptions();
	}

	slot = g_PlayerConfigsArray[mpindex].teamagentindex;

	if (slot < 0 || slot >= g_FojoHeadCount) {
		return -1;
	}

	if (!g_FojoFriendHeadsResolved) {
		fojoFriendsResolveHeads();
	}

	head = g_FojoHeadOptions[slot];

	for (i = 0; i < NUM_FOJO_FRIENDS; i++) {
		if (g_FojoFriendHeads[i] >= 0 && g_FojoFriendHeads[i] == head) {
			return i;
		}
	}

	return -1;
}

/**
 * A friend's wardrobe: the bodies she wears in place of Joanna's, registered
 * by a mod under "<prefix>_<outfit>" - body_mikado_combat, body_mikado_snow.
 * NULL means she wears the vanilla body. The outfit words are the vanilla
 * Joanna rows the stage asks for, so a friend with a wardrobe needs one body
 * per word she covers and the rest fall through to Joanna's.
 */
static const char *g_FojoFriendWardrobe[NUM_FOJO_FRIENDS] = {
	NULL, NULL, "body_mikado", NULL, NULL, NULL,
};

static const struct {
	u16 bodynum;
	const char *outfit;
} g_FojoOutfitWords[] = {
	{ BODY_DARK_COMBAT,     "combat"     },
	{ BODY_DARK_FROCK,      "frock"      },
	{ BODY_DARK_LEATHER,    "leather"    },
	{ BODY_DARK_NEGOTIATOR, "negotiator" },
	{ BODY_DARK_RIPPED,     "ripped"     },
	{ BODY_DARKSNOW,        "snow"       },
	{ BODY_DARK_AF1,        "af1"        },
};

/**
 * The body a friend spawns in for the outfit the stage chose. Answers the
 * vanilla bodynum unchanged when the friend has no wardrobe, the outfit has no
 * word, or the mod set did not register that body (no jpn rom, say) - so a
 * missing body degrades to Joanna's and never to nothing.
 */
s32 fojoFriendWardrobeBody(s32 friendnum, s32 bodynum)
{
	char name[64];
	s32 i;
	s32 found;

	if (friendnum < 0 || friendnum >= NUM_FOJO_FRIENDS || !g_FojoFriendWardrobe[friendnum]) {
		return bodynum;
	}

	for (i = 0; i < ARRAYCOUNT(g_FojoOutfitWords); i++) {
		if (g_FojoOutfitWords[i].bodynum == bodynum) {
			snprintf(name, sizeof(name), "%s_%s", g_FojoFriendWardrobe[friendnum], g_FojoOutfitWords[i].outfit);
			found = modLookupBodynumByName(name);
			return found >= 0 ? found : bodynum;
		}
	}

	return bodynum;
}

/**
 * A friend's colour is forced on their player's hud, crosshair included.
 * Guests, and everyone in the combat simulator, keep the crosshair they chose.
 */
static u32 fojoFriendsHudTint(s32 mpindex)
{
	s32 friendnum;

	if (g_Vars.normmplayerisrunning) {
		return 0;
	}

	friendnum = fojoFriendForMpIndex(mpindex);

	return friendnum >= 0 ? g_FojoFriendColours[friendnum] : 0;
}

/**
 * The slow stars' glows split between the friends by share of deaths:
 * every loaded profile's lifetime deaths, credited to the friend it plays.
 * A profile playing a guest counts for nobody.
 */
static void fojoFriendsGlowShare(struct menubgglowshare *share)
{
	s32 i;

	share->count = NUM_FOJO_FRIENDS;

	for (i = 0; i < NUM_FOJO_FRIENDS; i++) {
		share->colour[i] = &g_FojoFriendColours[i];
		share->weight[i] = 0;
		share->label[i] = g_FojoFriendNames[i];
	}

	for (i = 0; i < MAX_PLAYERS; i++) {
		s32 friendnum;

		if (g_PlayerConfigsArray[i].fileguid.fileid == 0) {
			continue;
		}

		friendnum = fojoFriendForMpIndex(i);

		if (friendnum >= 0) {
			share->weight[friendnum] += g_PlayerConfigsArray[i].deaths;
		}
	}
}

void fojoFriendsInit(void)
{
	g_HudTintFn = fojoFriendsHudTint;
	g_MenuBgGlowShareFn = fojoFriendsGlowShare;
}
