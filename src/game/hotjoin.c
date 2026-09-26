#include <ultra64.h>
#include <stdio.h>
#include <string.h>
#include "constants.h"
#include "game/hotjoin.h"
#include "game/playermgr.h"
#include "game/filelist.h"
#include "game/filemgr.h"
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

static struct filelist *hotjoinProfileList(void)
{
	s32 listnum = filelistFindOrCreate(FILETYPE_MPPLAYER);

	if (listnum < 0 || g_FileLists[listnum] == NULL) {
		return NULL;
	}

	// the menus tick this from menutick; the picker is open when no menu is
	filelistUpdate(g_FileLists[listnum]);
	g_FileLists[listnum]->updatedthisframe = true;

	return g_FileLists[listnum];
}

s32 hotjoinProfileCount(void)
{
	struct filelist *list = hotjoinProfileList();
	return list ? list->numfiles : 0;
}

bool hotjoinProfileInfo(s32 index, char *name, u32 namelen, struct fileguid *guid, s32 *boundslot)
{
	struct filelist *list = hotjoinProfileList();
	struct filelistfile *file;
	u32 playtime;
	s32 i;

	if (list == NULL || index < 0 || index >= list->numfiles) {
		return false;
	}

	file = &list->files[index];

	name[0] = '\0';
	mpplayerfileGetOverview(file->name, name, &playtime);
	name[namelen - 1] = '\0';

	guid->fileid = file->fileid;
	guid->deviceserial = file->deviceserial;

	*boundslot = -1;

	for (i = 0; i < PLAYERCOUNT(); i++) {
		const struct fileguid *bound = &g_PlayerConfigsArray[g_Vars.playerstats[i].mpindex].fileguid;

		if (bound->fileid == file->fileid && bound->deviceserial == file->deviceserial) {
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

	hotjoinSay("%s left from slot %d", name, n + 1);

	return n;
}
