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
 * The friends of Jo are the Team Missions roster (modconfig Operative blocks,
 * mod.c): a friend number is a roster index. Her colour, name and wardrobe
 * are her block's. A colour is 0xrrggbbaa, alpha 0xb0 against the crosshair's
 * vanilla 0x28 -- a pastel at 0x28 washes out to nothing; 0x90-0xd0 is where
 * it should land. An operative with no colour (vanilla, as port4p's Joanna and
 * Velvet are) tints nothing and takes no glow.
 */

extern s32 g_FojoHeadCount;

/**
 * Which friend a player is, from the operative they picked. The carousel's
 * options are the roster in order, so a slot below g_FojoHeadCount is the
 * friend number. The last slot is the player's own combat sim head. That's a
 * guest, not a friend, and gets -1 like anything else unrecognised.
 */
s32 fojoFriendForMpIndex(s32 mpindex)
{
	s32 slot;

	if (mpindex < 0 || mpindex >= MAX_PLAYERS) {
		return -1;
	}

	if (g_FojoHeadCount == 0) {
		fojoInitHeadOptions();
	}

	slot = g_PlayerConfigsArray[mpindex].teamagentindex;

	if (slot < 0 || slot >= g_FojoHeadCount || !modOperativeAt(slot)) {
		return -1;
	}

	return slot;
}

/**
 * A friend's wardrobe: the bodies she wears in place of Joanna's, registered
 * by a mod under "<wardrobe>_<outfit>" - body_mikado_combat, body_mikado_snow.
 * No wardrobe means she wears the vanilla body. The outfit words are the
 * vanilla Joanna rows the stage asks for, so a friend with a wardrobe needs
 * one body per word she covers and the rest fall through to Joanna's.
 */
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

	const struct modoperative *op = modOperativeAt(friendnum);

	if (!op || !op->wardrobe[0]) {
		return bodynum;
	}

	for (i = 0; i < ARRAYCOUNT(g_FojoOutfitWords); i++) {
		if (g_FojoOutfitWords[i].bodynum == bodynum) {
			snprintf(name, sizeof(name), "%s_%s", op->wardrobe, g_FojoOutfitWords[i].outfit);
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

	const struct modoperative *op = modOperativeAt(friendnum);

	return op ? op->colour : 0;
}

/**
 * The slow stars' glows split between the friends by share of deaths:
 * every loaded profile's lifetime deaths, credited to the friend it plays.
 * A profile playing a guest counts for nobody.
 */
static void fojoFriendsGlowShare(struct menubgglowshare *share)
{
	static u32 colours[MENUBG_MAX_GLOWS];
	s32 glowfor[NUM_FOJO_HEADS];
	const s32 count = modOperativeCount();
	s32 i;

	share->count = 0;

	// only the coloured get a glow; the cap is the menu's, not the roster's
	for (i = 0; i < count && i < NUM_FOJO_HEADS; i++) {
		const struct modoperative *op = modOperativeAt(i);

		glowfor[i] = -1;

		if (op->colour == 0 || share->count >= MENUBG_MAX_GLOWS) {
			continue;
		}

		glowfor[i] = share->count;
		colours[share->count] = op->colour;
		share->colour[share->count] = &colours[share->count];
		share->weight[share->count] = 0;
		share->label[share->count] = op->shortname;
		share->count++;
	}

	for (i = 0; i < MAX_PLAYERS; i++) {
		s32 friendnum;

		if (g_PlayerConfigsArray[i].fileguid.fileid == 0) {
			continue;
		}

		friendnum = fojoFriendForMpIndex(i);

		if (friendnum >= 0 && friendnum < count && glowfor[friendnum] >= 0) {
			share->weight[glowfor[friendnum]] += g_PlayerConfigsArray[i].deaths;
		}
	}
}

void fojoFriendsInit(void)
{
	g_HudTintFn = fojoFriendsHudTint;
	g_MenuBgGlowShareFn = fojoFriendsGlowShare;
}
