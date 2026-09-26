#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include "constants.h"
#include "game/hotjoin.h"
#include "game/playermgr.h"
#include "game/mainmenu.h"
#include "game/pak.h"
#include "game/mplayer/mplayer.h"
#include "bss.h"
#include "data.h"
#include "system.h"

/**
 * fojo hot join. See hotjoin.h.
 *
 * Only the highest live slot can leave: PLAYERCOUNT() counts non-NULL
 * g_Vars.players[] slots and the tree walks 0..n-1 directly, so a hole is not
 * survivable. A relocation routine (swap the leaver with the last slot) is the
 * follow-up that lifts this.
 */

static char g_HotjoinMessage[96];

static void hotjoinSay(const char *fmt, const char *a, s32 b)
{
	snprintf(g_HotjoinMessage, sizeof(g_HotjoinMessage), fmt, a, b);
	sysLogPrintf(LOG_NOTE, "hotjoin: %s", g_HotjoinMessage);
}

const char *hotjoinLastMessage(void)
{
	return g_HotjoinMessage;
}

bool hotjoinAvailable(void)
{
	return g_MissionConfig.isteam
		&& g_Vars.stagenum < STAGE_TITLE
		&& g_Vars.mplayerisrunning
		&& PLAYERCOUNT() >= 1;
}

s32 hotjoinFreeSlot(void)
{
	s32 n = PLAYERCOUNT();
	return (hotjoinAvailable() && n < MAX_PLAYERS) ? n : -1;
}

s32 hotjoinLastSlot(void)
{
	s32 n = PLAYERCOUNT() - 1;
	return (hotjoinAvailable() && n >= 1 && n != g_Vars.bondplayernum) ? n : -1;
}

/**
 * The profile list is a snapshot of every MP player file on every pak,
 * rebuilt only when asked. It deliberately does NOT go through filelist.c:
 * filelistCreate sets var80062944, and menutick.c:586 answers that with
 * menuStop() on the next tick whenever no menu is open - which frees every
 * file list and calls inputAutoLockMouse(true). Built per frame from the
 * Players panel that was a lock/unlock fight with the overlay every frame,
 * which pins the cursor to the centre of the window.
 */
#define HOTJOIN_MAX_PROFILES 64

struct hotjoinprofile {
	s32 fileid;
	u16 deviceserial;
	u8 body[16];
};

static struct hotjoinprofile g_HotjoinProfiles[HOTJOIN_MAX_PROFILES];
static s32 g_HotjoinNumProfiles = 0;
static bool g_HotjoinProfilesValid = false;

void hotjoinInvalidateProfiles(void)
{
	g_HotjoinProfilesValid = false;
}

static void hotjoinRefreshProfiles(void)
{
	const s8 devices[] = {
		SAVEDEVICE_GAMEPAK,
		SAVEDEVICE_CONTROLLERPAK1,
		SAVEDEVICE_CONTROLLERPAK2,
		SAVEDEVICE_CONTROLLERPAK3,
		SAVEDEVICE_CONTROLLERPAK4,
	};
	u32 ids[512];
	s32 d;
	s32 j;

	g_HotjoinNumProfiles = 0;

	for (d = 0; d < (s32)ARRAYCOUNT(devices) && g_HotjoinNumProfiles < HOTJOIN_MAX_PROFILES; d++) {
		if (pakGetFileIdsByType(devices[d], PAKFILETYPE_MPPLAYER, ids) != 0) {
			continue;
		}

		for (j = 0; ids[j] != 0 && g_HotjoinNumProfiles < HOTJOIN_MAX_PROFILES; j++) {
			struct hotjoinprofile *p = &g_HotjoinProfiles[g_HotjoinNumProfiles];

			if (pakReadBodyAtGuid(devices[d], ids[j], p->body, sizeof(p->body)) != 0) {
				continue;
			}

			p->fileid = ids[j];
			p->deviceserial = pakGetSerial(devices[d]);
			g_HotjoinNumProfiles++;
		}
	}

	g_HotjoinProfilesValid = true;
}

s32 hotjoinProfileCount(void)
{
	if (!g_HotjoinProfilesValid) {
		hotjoinRefreshProfiles();
	}

	return g_HotjoinNumProfiles;
}

bool hotjoinProfileInfo(s32 index, char *name, u32 namelen, struct fileguid *guid, s32 *boundslot)
{
	struct hotjoinprofile *p;
	u32 playtime;
	s32 i;

	if (!g_HotjoinProfilesValid) {
		hotjoinRefreshProfiles();
	}

	if (index < 0 || index >= g_HotjoinNumProfiles) {
		return false;
	}

	p = &g_HotjoinProfiles[index];

	name[0] = '\0';
	mpplayerfileGetOverview((char *)p->body, name, &playtime);
	name[namelen - 1] = '\0';

	guid->fileid = p->fileid;
	guid->deviceserial = p->deviceserial;

	*boundslot = -1;

	for (i = 0; i < PLAYERCOUNT(); i++) {
		const struct fileguid *bound = &g_PlayerConfigsArray[g_Vars.playerstats[i].mpindex].fileguid;

		if (bound->fileid == p->fileid && bound->deviceserial == p->deviceserial) {
			*boundslot = i;
			break;
		}
	}

	return true;
}

s32 hotjoinAddPlayer(const struct fileguid *guid)
{
	s32 n = hotjoinFreeSlot();
	s32 device;
	s32 i;

	if (n < 0) {
		hotjoinSay("no free slot%s (%d players)", "", PLAYERCOUNT());
		return -1;
	}

	for (i = 0; i < PLAYERCOUNT(); i++) {
		const struct fileguid *bound = &g_PlayerConfigsArray[g_Vars.playerstats[i].mpindex].fileguid;

		if (bound->fileid == guid->fileid && bound->deviceserial == guid->deviceserial) {
			hotjoinSay("that profile%s is already in slot %d", "", i + 1);
			return -1;
		}
	}

	device = pakFindBySerial(guid->deviceserial);

	if (device < 0) {
		hotjoinSay("profile's pak%s not found (serial %d)", "", guid->deviceserial);
		return -1;
	}

	// the same sequence the DefaultProfile loader runs (filemgr.c), with the
	// TARGET slot on both sides of the ini registration - mpLoadPlayerMenu
	// registers the menu owner's guid against the target, which is the
	// c-slot-profiles defect, so that path is not used here.
	if (mpplayerfileLoad(n, device, guid->fileid, guid->deviceserial) != 0) {
		hotjoinSay("profile%s load failed for slot %d", "", n + 1);
		return -1;
	}

	iniProcessPendingProfiles();
	iniRegisterPlayerSave(&g_PlayerConfigsArray[n].fileguid, 1, n);
	updatePlayerNames();

	if (playermgrAddPlayer() != n) {
		hotjoinSay("seat%s failed for slot %d", "", n + 1);
		return -1;
	}

	hotjoinInvalidateProfiles();
	hotjoinSay("%s seated in slot %d", g_PlayerConfigsArray[n].base.name, n + 1);

	return n;
}

s32 hotjoinDropLastPlayer(void)
{
	s32 n = hotjoinLastSlot();
	s32 prev;
	s32 mpindex;
	struct fileguid *guid;
	char name[16];

	if (n < 0) {
		hotjoinSay("nobody can leave%s (%d players)", "", PLAYERCOUNT());
		return -1;
	}

	mpindex = g_Vars.playerstats[n].mpindex;
	guid = &g_PlayerConfigsArray[mpindex].fileguid;
	snprintf(name, sizeof(name), "%s", g_PlayerConfigsArray[mpindex].base.name);

	// as if they had aborted: mainEndStage runs exactly this per player, and
	// teamCalculateAwards never reads `aborted`, so abort and finish accumulate
	// the same. Without it a drop would launder the mission's deaths away.
	prev = g_Vars.currentplayernum;
	setCurrentPlayerNum(n);
	g_Vars.currentplayer->aborted = true;
	teamCalculateAwards();
	setCurrentPlayerNum(prev);

	// save first, then take the seat away. mpProfileFlushSlotHashes is what
	// the Saves panel runs before a save; see c-saves-panel for the open
	// question over HeadNameHash.
	if (guid->fileid || guid->deviceserial) {
		s32 device = pakFindBySerial(guid->deviceserial);

		mpProfileFlushSlotHashes(mpindex);

		if (device >= 0) {
			if (mpplayerfileSave(mpindex, device, guid->fileid, guid->deviceserial) != 0) {
				sysLogPrintf(LOG_NOTE, "hotjoin: save failed for slot %d, leaving anyway", n + 1);
			}
		}
	}

	if (playermgrRemoveLastPlayer() != n) {
		hotjoinSay("teardown%s failed for slot %d", "", n + 1);
		return -1;
	}

	// release the profile so the slot reads as free and a re-join can take
	// the same file
	mpPlayerSetDefaults(mpindex, true);
	updatePlayerNames();

	hotjoinInvalidateProfiles();
	hotjoinSay("%s left from slot %d", name, n + 1);

	return n;
}
