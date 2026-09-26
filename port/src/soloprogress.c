#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "types.h"
#include "constants.h"
#include "bss.h"
#include "data.h"
#include "config.h"
#include "mod.h"
#include "savequeue.h"
#include "system.h"
#include "soloprogress.h"
#include "game/stagetable.h"

struct soloprogress {
	s16 stagenum;
	s8 modnum;
	s32 t[3];
	s32 c[3];
	char section[CONFIG_MAX_SECNAME + 1];
};

static struct soloprogress g_SoloProgress[NUM_STAGENUMS];
static s32 g_NumSoloProgress = 0;
static struct fileguid g_SoloBoundGuid;

static void soloKey(const struct soloprogress *sp, const char *name, char *out, size_t outlen)
{
	snprintf(out, outlen, "%s.%s", sp->section, name);
}

static void soloEachKey(struct soloprogress *sp, void (*fn)(const char *key, s32 *var, s32 max))
{
	char key[CONFIG_MAX_SECNAME * 2 + 2];
	static const char *tn[3] = { "t0", "t1", "t2" };
	static const char *cn[3] = { "c0", "c1", "c2" };

	for (s32 d = 0; d < 3; ++d) {
		soloKey(sp, tn[d], key, sizeof(key));
		fn(key, &sp->t[d], 0xfff);
		soloKey(sp, cn[d], key, sizeof(key));
		fn(key, &sp->c[d], 1);
	}
}

static void soloRegister(const char *key, s32 *var, s32 max)
{
	*var = 0;
	configRegisterInt(key, var, 0, max);
}

static void soloDetach(const char *key, s32 *var, s32 max)
{
	configUnbindKey(key);
}

static bool soloIsCampaignRow(s32 stagenum)
{
	for (s32 i = 0; i < NUM_SOLOSTAGES; ++i) {
		if (g_SoloStages[i].stagenum == stagenum) {
			return true;
		}
	}

	return false;
}

static struct soloprogress *soloFind(s32 stagenum)
{
	for (s32 i = 0; i < g_NumSoloProgress; ++i) {
		if (g_SoloProgress[i].stagenum == stagenum) {
			return &g_SoloProgress[i];
		}
	}

	return NULL;
}

void soloProgressUnbind(void)
{
	for (s32 i = 0; i < g_NumSoloProgress; ++i) {
		soloEachKey(&g_SoloProgress[i], soloDetach);
	}

	g_NumSoloProgress = 0;
	g_SoloBoundGuid.fileid = 0;
	g_SoloBoundGuid.deviceserial = 0;
}

void soloProgressBind(void)
{
	char modbuf[64];

	if (!g_GameFileGuid.fileid && !g_GameFileGuid.deviceserial) {
		return;
	}

	if (g_GameFileGuid.fileid == g_SoloBoundGuid.fileid
			&& g_GameFileGuid.deviceserial == g_SoloBoundGuid.deviceserial) {
		return;
	}

	soloProgressUnbind();

	for (s32 i = 0; i < g_NumModStageReg && g_NumSoloProgress < NUM_STAGENUMS; ++i) {
		const struct modStageRegEntry *e = &g_ModStageReg[i];

		if (!(e->kind & MODSTAGE_KIND_SOLO)) {
			continue;
		}

		// A block that replaces a campaign row is already in the list and
		// keeps its progress in the save; listing it again here would split
		// its times between the two. Only rows the campaign does not have.
		if (soloIsCampaignRow(e->stagenum)) {
			continue;
		}

		struct soloprogress *sp = &g_SoloProgress[g_NumSoloProgress++];
		sp->stagenum = e->stagenum;
		sp->modnum = e->modnum;
		snprintf(sp->section, sizeof(sp->section), "Solo.%x-%x.%s.%02x",
				g_GameFileGuid.deviceserial, g_GameFileGuid.fileid,
				modDirName(e->modnum, modbuf, sizeof(modbuf)), e->stagenum);
		// configRegisterInt applies whatever the file already held for the key.
		soloEachKey(sp, soloRegister);
	}

	g_SoloBoundGuid = g_GameFileGuid;

	sysLogPrintf(LOG_NOTE, "solo: %d mod mission(s) bound for reality %x-%x",
			g_NumSoloProgress, g_GameFileGuid.deviceserial, g_GameFileGuid.fileid);
}

s32 soloProgressCount(void)
{
	return g_NumSoloProgress;
}

s32 soloProgressStagenumAt(s32 index)
{
	return (index >= 0 && index < g_NumSoloProgress) ? g_SoloProgress[index].stagenum : -1;
}

const char *soloProgressNameAt(s32 index)
{
	if (index < 0 || index >= g_NumSoloProgress) {
		return "?";
	}

	const s32 stagenum = g_SoloProgress[index].stagenum;

	for (s32 i = 0; i < g_NumModStageReg; ++i) {
		if (g_ModStageReg[i].stagenum == stagenum && g_ModStageReg[i].name && g_ModStageReg[i].name[0]) {
			return g_ModStageReg[i].name;
		}
	}

	const char *own = modStageSlotName(stagenum);
	if (own) {
		return own;
	}

	const char *row = stageGetName(stagenum);
	return row ? row : "?";
}

s32 soloProgressBestTime(s32 stagenum, s32 difficulty)
{
	struct soloprogress *sp = soloFind(stagenum);
	return (sp && difficulty >= 0 && difficulty < 3) ? sp->t[difficulty] : -1;
}

bool soloProgressSetBestTime(s32 stagenum, s32 difficulty, s32 secs)
{
	struct soloprogress *sp = soloFind(stagenum);

	if (!sp || difficulty < 0 || difficulty >= 3) {
		return false;
	}

	if (secs > 0xfff) {
		secs = 0xfff;
	}

	if (sp->t[difficulty] == 0 || secs < sp->t[difficulty]) {
		sp->t[difficulty] = secs;
		saveQueueMarkConfig();
	}

	return true;
}

bool soloProgressCoopDone(s32 stagenum, s32 difficulty)
{
	struct soloprogress *sp = soloFind(stagenum);
	return sp && difficulty >= 0 && difficulty < 3 && sp->c[difficulty] != 0;
}

bool soloProgressSetCoopDone(s32 stagenum, s32 difficulty)
{
	struct soloprogress *sp = soloFind(stagenum);

	if (!sp || difficulty < 0 || difficulty >= 3) {
		return false;
	}

	if (!sp->c[difficulty]) {
		sp->c[difficulty] = 1;
		saveQueueMarkConfig();
	}

	return true;
}

void soloProgressProbeFromArgs(void)
{
	const char *arg = sysArgGetString("--solo-probe");
	u32 serial = 0;
	s32 fileid = 0;

	if (!arg || !arg[0]) {
		return;
	}

	if (sscanf(arg, "%x-%x", &serial, &fileid) != 2) {
		sysLogPrintf(LOG_ERROR, "solo-probe: want SERIAL-FILEID in hex, got '%s'", arg);
		_exit(2);
	}

	g_GameFileGuid.deviceserial = serial;
	g_GameFileGuid.fileid = fileid;
	soloProgressBind();

	for (s32 i = 0; i < g_NumSoloProgress; ++i) {
		const struct soloprogress *sp = &g_SoloProgress[i];
		sysLogPrintf(LOG_NOTE, "solo-probe: [%s] stage 0x%02x mod %d t=%d/%d/%d c=%d/%d/%d",
				sp->section, sp->stagenum, sp->modnum,
				sp->t[0], sp->t[1], sp->t[2], sp->c[0], sp->c[1], sp->c[2]);
	}

	// No save and no exit here: the game runs on to the title and the normal
	// shutdown writes pd.ini. A save from this early - before input has
	// filled the bind strings the config table points at - writes those
	// strings half-made (measured: player 1's JOY1 and keyboard halves gone).
	// Probes look; the shutdown path writes.
}
