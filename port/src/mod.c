#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "utils.h"
#include "romdata.h"
#include "mod.h"
#include "config.h"
#include "data.h"
#include "bss.h"
#include "game/body.h"
#include "game/training.h"
#include "game/stagetable.h"

#define DEBUG_MODELS(fmt, ...) \
	do { if (g_DebugModels) sysLogPrintf(LOG_NOTE, fmt, ##__VA_ARGS__); } while (0)
#include "game/mplayer/mplayer.h"
#include "game/mplayer/setup.h"

#define MOD_TEXTURES_DIR "textures"
#define MOD_ANIMATIONS_DIR "animations"
#define MOD_SEQUENCES_DIR "sequences"

s32 g_TexModNum = -1;
s32 g_TexCurrentModelFileNum = 0;

/*
 * Owning mod of a file id. See the encoding note in port/include/mod.h.
 *
 * The middle branch is the whole reason the encoding spends a bit. An id with
 * mod bits but no tag cannot be produced by MOD_FILEID_MAKE; it can only come
 * from somewhere that built the id by hand as (mod << 16), which is how this
 * convention was written at a dozen sites before the macros existed. Such an
 * id is answered as vanilla - the safe direction, a missing texture rather
 * than another mod's - and reported, because nothing else can see it: it
 * compiles, it warns about nothing, and the wrong texture it produces looks
 * like a mod authoring mistake.
 *
 * Capped rather than rate-limited by time: the interesting thing is which
 * call sites exist, and they are all hit within the first few loads.
 */
s32 modFileIdMod(s32 id)
{
	if (id < 0) {
		// -1 is "no file" (modeldefEditorWorkspaceUnload sets it). All-ones
		// has the tag bit and 0xff of mod bits, so it would otherwise decode
		// as a real id owned by a mod 255 that no array is sized for.
		return -1;
	}

	if ((id & MOD_FILEID_TAG) == 0) {
		if (id & (MOD_FILEID_MOD_MASK << MOD_FILEID_SHIFT)) {
			static u32 reported = 0;
			if (reported < 32) {
				reported++;
				sysLogPrintf(LOG_WARNING,
						"modFileIdMod: file id 0x%08x carries mod bits (%d) but no owner tag - "
						"built by hand instead of MOD_FILEID_MAKE; treating as vanilla",
						(u32)id, (id >> MOD_FILEID_SHIFT) & MOD_FILEID_MOD_MASK);
			}
		}
		return -1;
	}

	return (id >> MOD_FILEID_SHIFT) & MOD_FILEID_MOD_MASK;
}

extern struct stagemusic g_StageTracks[];
extern struct stageallocation g_StageAllocations8Mb[];
extern s32 g_MainIsBooting;
extern s32 g_MainChangeToStageNum;

extern struct headorbody *g_HeadsAndBodies;
extern struct headorbody g_HeadsAndBodiesOriginal[];
extern s32 g_NumHeadsAndBodies;
extern const u32 g_NumHeadsAndBodies_Original;

extern struct mphead *g_MpHeads;
extern struct mphead g_MpHeadsOriginal[];
extern s32 g_NumMpHeads;
extern const u32 g_NumMpHeads_Original;

extern struct mpbody *g_MpBodies;
extern struct mpbody g_MpBodiesOriginal[];
extern s32 g_NumMpBodies;
extern const u32 g_NumMpBodies_Original;

extern u32 g_NumModDirs;
extern char modDirs[64][FS_MAXPATH + 1];

extern struct modelstate g_ModelStates[NUM_MODELS];
extern s8 g_PropExplosionTypes[];

struct texturesurfaceconfig g_VanillaTextures[NUM_TEXTURES];
struct modelstate g_ModelStatesOriginal[NUM_MODELS];
s8 g_PropExplosionTypesOriginal[NUM_MODELS];

static struct { char *name; s32 id; } *g_ModHeadNames = NULL;
static s32 g_NumModHeadNames = 0;

static struct { char *name; u32 filenum; } *g_ModHandFileNames = NULL;
static s32 g_NumModHandFileNames = 0;

/*
 * Persisted mp-slot reservations.
 *
 * A mod head that declares no slotnum is appended to g_MpHeads, and the index
 * it lands on is the index a saved profile stores: mpheadnum indexes
 * g_MpHeads (mpGetHeadId, mplayer.c:3161), not g_HeadsAndBodies. Appending
 * meant that index was decided by --moddir order, so adding a mod ahead of
 * another one in the roster moved every head behind it and a saved profile
 * came back wearing a different face. Nothing keyed the index to the head, and
 * g_ModHeadNames - the only name -> id map there was - is rebuilt from the
 * modconfigs every boot and never written anywhere.
 *
 * So the allocation is what gets persisted, in pd.ini, one key per head name:
 *
 *   [MpHeadSlots]
 *   head_mikado=75
 *
 * A name that has been allocated a slot keeps it. A name that has not takes
 * the lowest slot no reservation holds. A name whose mod is not loaded this
 * session keeps its reservation, because the entry is registered with a live
 * pointer at scan time and configSave writes back what it finds registered -
 * the same reason iniBindProfileProperties binds [MpPlayer.*] sections for
 * identities that are not present (mplayer.c:394).
 *
 * The entries array is fixed and never realloc'd: config holds a pointer into
 * it for the lifetime of the process.
 *
 * Head names are unique across the shipped roster - 153 declarations in the
 * three character mods, 153 distinct names, no collision - so the head name
 * alone is the key and the mod name is not part of it. Two mods that do pick
 * the same name already replace each other's row; this does not change that.
 */
#define MOD_SLOT_MAX_NAME 64
#define MOD_MAX_SLOT_RESERVATIONS 512

// Highest index a save file can hold: mpheadnum and mpbodynum are written in
// 7 bits (mplayer.c:4171-4172 for the profile, :4701/:4716 for the bots).
#define MOD_MAX_PERSISTABLE_SLOT 127

// A reservation past this is a damaged ini, not an allocation.
#define MOD_MAX_SLOT_INDEX 1023

#define MOD_HEADSLOT_SECTION "MpHeadSlots"
#define MOD_BODYSLOT_SECTION "MpBodySlots"
#define MOD_STAGESLOT_SECTION "MpStageSlots"

struct modslotreservation {
	char name[MOD_SLOT_MAX_NAME];
	s32 slot;
};

struct modslottable {
	const char *section;
	s32 first;      // lowest index this table may hand out
	// Optional gate on which indices this table may hand out. NULL means every
	// index from `first` up is fair game, which is true of heads and bodies:
	// they append rows the mod itself supplies, so any index past the vanilla
	// count is a legal home. Stage rows are not like that - a stagenum selects
	// a row that already exists and carries data - so that table sets this.
	bool (*usable)(s32 slot);
	s32 count;
	u8 fullWarned;
	u8 ceilingWarned;
	struct modslotreservation entries[MOD_MAX_SLOT_RESERVATIONS];
};

static struct modslottable g_ModHeadSlots = { MOD_HEADSLOT_SECTION };
static struct modslottable g_ModBodySlots = { MOD_BODYSLOT_SECTION };
static bool modStageSlotUsable(s32 stagenum);
static struct modslottable g_ModStageSlots = { MOD_STAGESLOT_SECTION, 0, modStageSlotUsable };

/*
 * Which stagenums the allocator may hand out.
 *
 * The STAGE_EXTRA rows are the extension space and nothing else is: every
 * other row is a level the base game ships, and handing one out would silently
 * replace it. The names carry that intent already - upstream labels
 * STAGE_EXTRA20..23 Junkyard, Steel Mill, Mall and Tunnels, which are the four
 * Goldfinger 64 levels by name.
 *
 * The digit test is not decoration. STAGE_EXTRACTION shares the first eleven
 * characters and is a real solo mission; a prefix-only test hands it out.
 */
static bool modStageSlotUsable(s32 stagenum)
{
	const char *name = stageGetName(stagenum);

	if (!name || strncmp(name, "STAGE_EXTRA", 11) != 0) {
		return false;
	}

	if (name[11] < '0' || name[11] > '9') {
		return false;
	}

	// Claimed earlier this boot by an explicit block. Allocating over it would
	// hand two mods the same row and let roster order decide, which is the
	// thing this table exists to stop.
	if (stagenum >= 0 && stagenum < (s32)ARRAYCOUNT(g_ModStageNums)
			&& g_ModStageNums[stagenum] >= 0) {
		return false;
	}

	return true;
}
static bool g_ModSlotsLoaded = false;

static struct modslotreservation *modSlotFind(struct modslottable *tbl, const char *name)
{
	for (s32 i = 0; i < tbl->count; ++i) {
		if (!strcmp(tbl->entries[i].name, name)) {
			return &tbl->entries[i];
		}
	}
	return NULL;
}

static bool modSlotTaken(struct modslottable *tbl, s32 slot)
{
	for (s32 i = 0; i < tbl->count; ++i) {
		if (tbl->entries[i].slot == slot) {
			return true;
		}
	}
	return false;
}

static struct modslotreservation *modSlotAdd(struct modslottable *tbl, const char *name, s32 slot)
{
	char key[CONFIG_MAX_KEYNAME + 1];
	struct modslotreservation *r;

	if (tbl->count >= MOD_MAX_SLOT_RESERVATIONS) {
		if (!tbl->fullWarned) {
			tbl->fullWarned = 1;
			sysLogPrintf(LOG_WARNING,
					"modconfig: more than %d [%s] reservations; '%s' and later names fall back to roster order",
					MOD_MAX_SLOT_RESERVATIONS, tbl->section, name);
		}
		return NULL;
	}

	r = &tbl->entries[tbl->count++];
	snprintf(r->name, sizeof(r->name), "%s", name);
	r->slot = slot;

	// min == max, so configSet does not clamp it.
	snprintf(key, sizeof(key), "%s.%s", tbl->section, r->name);
	configRegisterInt(key, &r->slot, 0, 0);

	if (slot > MOD_MAX_PERSISTABLE_SLOT && !tbl->ceilingWarned) {
		tbl->ceilingWarned = 1;
		sysLogPrintf(LOG_WARNING,
				"modconfig: [%s] '%s' got index %d; a save file holds 7 bits, so anything above %d cannot be stored",
				tbl->section, name, slot, MOD_MAX_PERSISTABLE_SLOT);
	}

	return r;
}

static void modSlotScanned(const char *name, const char *value, void *ctx)
{
	struct modslottable *tbl = ctx;
	s32 slot;

	if (!name || !name[0] || !value) {
		return;
	}

	slot = (s32)strtol(value, NULL, 0);

	if (slot < tbl->first || slot > MOD_MAX_SLOT_INDEX) {
		sysLogPrintf(LOG_WARNING, "modconfig: [%s] '%s' = %d is out of range; dropping the reservation",
				tbl->section, name, slot);
		return;
	}

	if (modSlotFind(tbl, name)) {
		return;
	}

	if (modSlotTaken(tbl, slot)) {
		sysLogPrintf(LOG_WARNING, "modconfig: [%s] '%s' wants index %d, already reserved; dropping the reservation",
				tbl->section, name, slot);
		return;
	}

	modSlotAdd(tbl, name, slot);
}

/*
 * Read the reservations back before any modconfig is parsed. Dropped keys are
 * simply not registered, so the next configSave writes the file without them
 * and the name is reallocated on the run after that.
 */
void modSlotReservationsInit(void)
{
	if (g_ModSlotsLoaded) {
		return;
	}
	g_ModSlotsLoaded = true;

	g_ModHeadSlots.first = (s32)g_NumMpHeads_Original;
	g_ModBodySlots.first = (s32)g_NumMpBodies_Original;

	// A stagenum is not an append index: the floor is the bottom of the range
	// modConfigParseStage will accept, and modStageSlotUsable does the rest.
	g_ModStageSlots.first = 0x02;

	configScanSection(CONFIG_PATH, MOD_HEADSLOT_SECTION, modSlotScanned, &g_ModHeadSlots);
	configScanSection(CONFIG_PATH, MOD_BODYSLOT_SECTION, modSlotScanned, &g_ModBodySlots);
	configScanSection(CONFIG_PATH, MOD_STAGESLOT_SECTION, modSlotScanned, &g_ModStageSlots);

	sysLogPrintf(LOG_NOTE,
			"modconfig: restored %d head, %d body and %d stage slot reservations from " CONFIG_FNAME,
			g_ModHeadSlots.count, g_ModBodySlots.count, g_ModStageSlots.count);
}

static s32 modSlotReserve(struct modslottable *tbl, const char *name)
{
	struct modslotreservation *r;
	s32 slot;

	if (!name || !name[0]) {
		return -1;
	}

	modSlotReservationsInit();

	r = modSlotFind(tbl, name);
	if (r) {
		return r->slot;
	}

	slot = tbl->first;
	while (slot <= MOD_MAX_SLOT_INDEX
			&& (modSlotTaken(tbl, slot) || (tbl->usable && !tbl->usable(slot)))) {
		slot++;
	}

	// A gated table can genuinely run out, unlike heads and bodies which just
	// count upward. Say so instead of handing back MOD_MAX_SLOT_INDEX + 1.
	if (slot > MOD_MAX_SLOT_INDEX) {
		sysLogPrintf(LOG_ERROR, "modconfig: [%s] no free index left for '%s'", tbl->section, name);
		return -1;
	}

	r = modSlotAdd(tbl, name, slot);

	return r ? r->slot : -1;
}

/*
 * An explicit slotnum is the modconfig's own choice, so it is recorded rather
 * than allocated - otherwise a later auto-allocated name could be handed the
 * same index.
 */
static void modSlotClaim(struct modslottable *tbl, const char *name, s32 slot)
{
	struct modslotreservation *r;

	if (!name || !name[0]) {
		return;
	}

	modSlotReservationsInit();

	if (slot < tbl->first || slot > MOD_MAX_SLOT_INDEX) {
		return;
	}

	r = modSlotFind(tbl, name);
	if (r) {
		if (r->slot != slot) {
			sysLogPrintf(LOG_WARNING, "modconfig: [%s] '%s' is reserved at %d but the modconfig asks for %d; using %d",
					tbl->section, name, r->slot, slot, slot);
		}
		return;
	}

	if (modSlotTaken(tbl, slot)) {
		sysLogPrintf(LOG_WARNING, "modconfig: [%s] '%s' asks for index %d, already reserved by another name",
				tbl->section, name, slot);
		return;
	}

	modSlotAdd(tbl, name, slot);
}

s32 modHeadSlotReserve(const char *name) { return modSlotReserve(&g_ModHeadSlots, name); }
s32 modBodySlotReserve(const char *name) { return modSlotReserve(&g_ModBodySlots, name); }
void modHeadSlotClaim(const char *name, s32 slot) { modSlotClaim(&g_ModHeadSlots, name, slot); }
void modBodySlotClaim(const char *name, s32 slot) { modSlotClaim(&g_ModBodySlots, name, slot); }
s32 modStageSlotReserve(const char *name) { return modSlotReserve(&g_ModStageSlots, name); }
void modStageSlotClaim(const char *name, s32 slot) { modSlotClaim(&g_ModStageSlots, name, slot); }

// Per-mod cached config data (parsed once at boot, then just copied on modSwitch)
struct modelstate g_ModelStates_PerMod[64][NUM_MODELS];
s8 g_ExplosionTypes_PerMod[64][NUM_MODELS];
static bool g_ModConfigsCached = false;

s32 g_ModStageNums[NUM_STAGENUMS];

static bool g_DebugModStage = false;
#define MODSTAGE(...) if (g_DebugModStage) { sysLogPrintf(LOG_NOTE, "MODSTAGE " __VA_ARGS__); }

char g_ModNames[64][64];
char g_ModVersions[64][32];

struct mparenagroup *g_MpArenaGroups = NULL;
s32 g_NumMpArenaGroups = 0;

#define MAX_IMPORTED_ASSETS 256
static char *g_ImportedAssets[MAX_IMPORTED_ASSETS];
static s32 g_NumImportedAssets = 0;

#define PARSE_STAGE_FLOAT(sec, name, v, min, max) \
	p = modConfigParseFloatValue(p, token, &v); \
	if (!p || v < (min) || v > (max)) { \
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: " sec " invalid " name " value: %s", stagenum, token); \
		return NULL; \
	}

#define PARSE_STAGE_INT(sec, name, v, min, max) \
	p = modConfigParseIntValue(p, token, &v); \
	if (!p || v < (min) || v > (max)) { \
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: " sec " invalid " name " value: %s", stagenum, token); \
		return NULL; \
	}

#define PARSE_STAGE_FILENAME(sec, name, v) \
	p = modConfigParseFileValue(p, token, &v, modnum); \
	if (!p) { \
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: " sec " invalid " name " value: %s", stagenum, token); \
		return NULL; \
	}

/*
 * Store a resolved file id into one of stagetableentry's five file fields.
 *
 * The fields are u32, so the store does not truncate and an owner tag survives
 * it; `modConfigParseFileValue` now stamps one on. Both halves are in place, so
 * the report below fires for every field a mod declares - one LOG_NOTE per
 * declared stage file per parse, which is the evidence that a stage's files
 * carry their declaring mod rather than resolving against g_ModNum.
 *
 * Per-field ownership needs no resolver: each of the five fields is stamped
 * with the modNum that parsed THAT line, so a stage whose bgfile comes from one
 * mod and whose setupfile comes from another resolves each against its own
 * owner at `romdataFileLoad`. g_Stages still holds one row per stage and the
 * last mod to parse a given field still wins that field - what changed is that
 * the winner's identity now travels with the value instead of being inferred
 * from whoever is active at load time.
 *
 * The test is >= 0, not != 0. It used to be != 0 because mod 0 read the same
 * as an untagged id and so could not be reported; now an untagged id reads as
 * -1 and every owner including mod 0 can be. LOG_NOTE rather than LOG_ERROR,
 * because an owner arriving here is what this is FOR, not a loss to shout
 * about. A vanilla field nobody declared keeps its raw ROM id, reads as -1, and
 * is not reported.
 */
#define SET_STAGE_FILEID(field, name, id) \
	do { \
		const s32 stageFileId = (s32)(id); \
		if (MOD_FILEID_MOD(stageFileId) >= 0) { \
			sysLogPrintf(LOG_NOTE, \
					"modconfig: stage 0x%02x: " name " id 0x%08x is owned by mod %d", \
					stagenum, stageFileId, MOD_FILEID_MOD(stageFileId)); \
		} \
		(field) = stageFileId; \
	} while (0)

/* The point of the widening: a stage field has to be able to hold a tagged id,
 * or romdataFileLoad falls back to g_ModNum and the stage loads whichever mod
 * is active. This cannot be observed from a headless run - videoInit()
 * (main.c:133) comes before modCacheAllConfigs() (pdmain.c:345) - so assert
 * the storage instead. */
_Static_assert(sizeof(((struct stagetableentry *)0)->setupfileid) >= sizeof(s32),
		"a stage file id field must be wide enough for MOD_FILEID_MAKE's owner bits");

#define PARSE_STAGE_STRING(sec, name, v) \
	p = strParseToken(p, token, NULL); \
	if (!p) { \
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: " sec " invalid " name " value: %s", stagenum, token); \
		return NULL; \
	} \
	v = strUnquote(token);

#define PARSE_INT(sec, name, v, min, max, ret) \
	p = modConfigParseIntValue(p, token, &v); \
	if (!p || v < (min) || v > (max)) { \
		sysLogPrintf(LOG_ERROR, "mod: %s: invalid " name " value: %s", sec, token); \
		return ret; \
	}

/*
 * Unknown keys, and why they are survivable.
 *
 * A modconfig is read by whatever build the player happens to be running, and
 * a key can be newer than the loader reading it. Every unknown key used to
 * return NULL all the way up to modConfigLoad, which broke out of its loop -
 * so one key nobody recognised threw away the entire file: heads, bodies,
 * stage claims, model states, the lot, and the mod loaded as though it had no
 * modconfig at all. Nothing reads modConfigLoad's return value, so the harm
 * was never the `success = false`; it was the `break` that came with it.
 *
 * That is the wrong trade. A config written for a newer loader should lose
 * the feature it asked for, not the mod. So an unknown key is now stepped
 * over and counted.
 *
 * Everything else stays fatal. A bad value for a key we DO know means the
 * file is not what it claims, and an unbalanced brace means every token after
 * it is misattributed; neither can be guessed past without corrupting what
 * has already been read.
 */

/*
 * True when nothing but whitespace, or a comment that owns the rest of its
 * line, stands between p and the next line.
 *
 * strParseToken treats '\n' as ordinary whitespace, so the token stream has
 * no line structure at all and the shape of an unknown key's value - a bare
 * word, a quoted string (one token, quotes included), three floats like
 * weather's constant_wind, a bracket list, a brace block - cannot be known
 * from the grammar. The files are line-oriented even though the grammar is
 * not: every key in every modconfig on disk and in PD_AIO_March_2026 is
 * written one per line. That convention is the only honest boundary there
 * is, so it is the one the skip uses.
 */
static bool modConfigAtLineEnd(const char *p)
{
	if (!p) {
		return true;
	}

	while (*p) {
		if (*p == '\n') {
			return true;
		}
		if ((u8)*p <= ' ') {
			++p;
			continue;
		}
		if (*p == ';' || *p == '#' || (p[0] == '/' && p[1] == '/')) {
			return true;
		}
		return false;
	}

	return true;
}

/*
 * Unknown keys are reported once per distinct name per config file, not once
 * per occurrence. A roster of seventeen stage blocks that all carry the same
 * key written for a newer loader is one fact about this build, not seventeen
 * about the config, and seventeen identical lines bury the warnings that are
 * about one place each. The tally printed at the end of modConfigLoad keeps
 * the counts the repeats would have carried.
 */
#define MODCONFIG_MAX_UNKNOWN_NAMES 8

static struct { char name[32]; s32 count; } g_UnknownKeys[MODCONFIG_MAX_UNKNOWN_NAMES];
static s32 g_NumUnknownKeyNames = 0;
static s32 g_NumUnknownKeyNamesDropped = 0;
static s32 g_NumUnknownKeysSkipped = 0;

static void modConfigUnknownKeysReset(void)
{
	memset(g_UnknownKeys, 0, sizeof(g_UnknownKeys));
	g_NumUnknownKeyNames = 0;
	g_NumUnknownKeyNamesDropped = 0;
	g_NumUnknownKeysSkipped = 0;
}

// Counts the sighting and answers whether this name has been seen before in
// this file. Past the table a new name is counted but not named; a config
// with more than MODCONFIG_MAX_UNKNOWN_NAMES distinct unknown keys is telling
// us one thing, not eight more.
static bool modConfigUnknownKeyFirstSighting(const char *key)
{
	++g_NumUnknownKeysSkipped;

	for (s32 i = 0; i < g_NumUnknownKeyNames; ++i) {
		if (!strcmp(g_UnknownKeys[i].name, key)) {
			++g_UnknownKeys[i].count;
			return false;
		}
	}

	if (g_NumUnknownKeyNames >= MODCONFIG_MAX_UNKNOWN_NAMES) {
		++g_NumUnknownKeyNamesDropped;
		return false;
	}

	strncpy(g_UnknownKeys[g_NumUnknownKeyNames].name, key, sizeof(g_UnknownKeys[0].name) - 1);
	g_UnknownKeys[g_NumUnknownKeyNames].name[sizeof(g_UnknownKeys[0].name) - 1] = '\0';
	g_UnknownKeys[g_NumUnknownKeyNames].count = 1;
	++g_NumUnknownKeyNames;

	return true;
}

static void modConfigUnknownKeysReport(const char *fname, const char *modname)
{
	if (!g_NumUnknownKeysSkipped) {
		return;
	}

	char list[256];
	s32 len = 0;

	list[0] = '\0';

	for (s32 i = 0; i < g_NumUnknownKeyNames; ++i) {
		const s32 n = snprintf(list + len, sizeof(list) - len, "%s%s x%d",
				len ? ", " : "", g_UnknownKeys[i].name, g_UnknownKeys[i].count);
		if (n < 0 || n >= (s32)(sizeof(list) - len)) {
			break;
		}
		len += n;
	}

	if (g_NumUnknownKeyNamesDropped) {
		snprintf(list + len, sizeof(list) - len, ", and %d more name(s)", g_NumUnknownKeyNamesDropped);
	}

	sysLogPrintf(LOG_WARNING,
			"modconfig: '%s' (mod '%s'): skipped %d key(s) this build does not know: %s. "
			"The rest of the config loaded; whatever those keys asked for did not happen.",
			fname, modname, g_NumUnknownKeysSkipped, list);
}

/*
 * Step over an unknown key and whatever was written as its value.
 *
 * Peek one token without consuming it. A brace opens a block, so step over
 * the whole balanced block - that is how a newer loader's new section
 * survives. Otherwise take every token still on the key's own line, which
 * covers a word, a quoted string, a multi-value key and a one-line bracket
 * list, and stop short of any brace, so a block written on one line keeps the
 * closing brace the loop above is waiting for. A key with no value at all,
 * like weather's cutscene_only, consumes nothing, because the next token is
 * already on the next line.
 *
 * What this cannot do is follow a braceless value that wraps across lines:
 * its tail lands in the next iteration, is reported as another unknown key,
 * and is skipped in turn. No config in the tree writes one. The line is the
 * honest boundary and that is what it costs.
 */
static char *modConfigSkipUnknownKey(char *p, const char *where, const char *key)
{
	if (modConfigUnknownKeyFirstSighting(key)) {
		sysLogPrintf(LOG_WARNING,
				"modconfig: %s: unknown key '%s' - skipped; the rest of the config still loads",
				where, key);
	}

	if (!p || modConfigAtLineEnd(p)) {
		return p;
	}

	char peek[UTIL_MAX_TOKEN + 1];
	peek[0] = '\0';

	char *after = strParseToken(p, peek, NULL);
	if (!after || !peek[0]) {
		return p;
	}

	if (peek[0] == '{' && peek[1] == '\0') {
		s32 depth = 1;
		p = after;
		while (p && depth > 0) {
			p = strParseToken(p, peek, NULL);
			if (!p || !peek[0]) {
				break;
			}
			if (peek[0] == '{' && peek[1] == '\0') {
				++depth;
			} else if (peek[0] == '}' && peek[1] == '\0') {
				--depth;
			}
		}
		return p;
	}

	// a closer belongs to whoever opened it
	if ((peek[0] == '}' || peek[0] == ']' || peek[0] == ')') && peek[1] == '\0') {
		return p;
	}

	p = after;
	while (!modConfigAtLineEnd(p)) {
		after = strParseToken(p, peek, NULL);
		if (!after || !peek[0]) {
			break;
		}
		if ((peek[0] == '{' || peek[0] == '}') && peek[1] == '\0') {
			break;
		}
		p = after;
	}

	return p;
}

static inline char *modConfigParseStringValue(char *p, char *token, char *value){
	p = strParseToken(p, token, NULL);
	if (!p) {
		return NULL;
	}
	char *t = strUnquote(token);
	strcpy(value, t);
	sysLogPrintf(LOG_NOTE, "modconfigParseStringValue %s", value);
	return p;
}
/*
 * Resolve a modconfig file value to a file id TAGGED with its declaring mod.
 *
 * The name lookup was already scoped to modNum and the owner was then thrown
 * away. Three of the four callers put it back by hand - heads (mod.c filenum),
 * hands (handfilenum) and ModelStates (File) each wrap the result in
 * MOD_FILEID_MAKE(modNum, ...) - and the five stage keys did not. So a stage
 * file id arrived at romdataFileLoad untagged, took its `modNum = g_ModNum`
 * fallback, and resolved against whichever mod happened to be ACTIVE rather
 * than the one that declared it.
 *
 * Stamping here makes the stage keys agree with the three that were already
 * right, and is a no-op for those three: MOD_FILEID_MAKE masks rawId to
 * 0xffff, so re-stamping an already-stamped id with the same modNum reproduces
 * it bit for bit. Both of those callers pass the same modNum they passed here,
 * so there is no mis-tag either.
 *
 * modNum is >= 0 at every live call site: modConfigLoad takes it from g_ModNum,
 * and all four callers of modConfigLoad set g_ModNum to a loop index over
 * [0, g_NumModDirs) or to 0 first (modCacheAllConfigs, pdmain.c:310/313) or
 * reach it only after modSwitch has repaired a negative g_ModNum. So the
 * negative-modNum hazard MOD_FILEID_MAKE warns about cannot arise here and is
 * not gated for, which keeps both branches below unconditionally tagged.
 *
 * The numeric branch used romdataFileGetName, which reads fileSlots[g_ModNum]
 * and ignores the modNum argument entirely - it validated a literal against the
 * ACTIVE mod's table. Harmless only because g_ModNum equals modNum at every
 * live site today; modCacheAllConfigs already parses one mod's config with
 * g_ModNum pointed at it, so the two would diverge the moment anything parsed
 * out of band. Fixed here rather than deferred, because tagging one branch and
 * not the other would leave the two halves of one function disagreeing about
 * whether their output carries an owner. romdataFileGetSlotName is the
 * mod-scoped spelling and bounds-checks modNum as well as fileNum. No config in
 * PD_AIO_March_2026 or in the working roster uses a numeric file value, so
 * nothing in either changes today.
 */
static inline char *modConfigParseFileValue(char *p, char *token, s32 *filenum, s32 modNum)
{
	p = strParseToken(p, token, NULL);
	if (!token[0]) {
		return NULL; // empty
	}
	// check if it is a number already
	s32 num = strtol(token, NULL, 0);
	if (num > 0 && romdataFileGetSlotName(modNum, num)) {
		*filenum = MOD_FILEID_MAKE(modNum, num);
		return p;
	}
	// it's a filename
	char *unquoted = strUnquote(token);
	num = romdataFileGetNumForNameInMod(unquoted, modNum);
	if (num >= 0) {
		*filenum = MOD_FILEID_MAKE(modNum, num);
		return p;
	}
	sysLogPrintf(LOG_ERROR, "modConfigParseFileValue: failed to find '%s' in mod %d", unquoted, modNum);
	// the filename was invalid
	return NULL;
}

static inline char *modConfigParseIntValue(char *p, char *token, s32 *out)
{
	p = strParseToken(p, token, NULL);
	if (!token[0]) {
		return NULL; // empty
	}
	char *endp = token;
	const s32 num = strtol(token, &endp, 0);
	if (num == 0 && (endp == token || *endp != '\0')) {
		return NULL;
	}
	*out = num;
	return p;
}

static inline char *modConfigParseFloatValue(char *p, char *token, f32 *out)
{
	p = strParseToken(p, token, NULL);
	if (!token[0]) {
		return NULL; // empty
	}
	char *endp = token;
	const f32 num = strtof(token, &endp);
	if (num == 0.f && (endp == token || *endp != '\0')) {
		return NULL;
	}
	*out = num;
	return p;
}

static char *modConfigParseStageMusic(char *p, char *token, s32 stagenum)
{
	struct stagemusic *smus = NULL;
	for (struct stagemusic *p = g_StageTracks; p->stagenum; ++p) {
		if (p->stagenum == stagenum) {
			smus = p;
			break;
		}
	}

	if (!smus) {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: music can't be changed for this stage", stagenum);
		return NULL;
	}

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	// parse keyvalues until } is reached
	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "primarytrack")) {
			PARSE_STAGE_INT("music:", "primarytrack", tmp, 0, 128);
			smus->primarytrack = tmp;
		} else if (!strcmp(token, "ambienttrack")) {
			PARSE_STAGE_INT("music:", "ambienttrack", tmp, 0, 128);
			smus->ambienttrack = tmp;
		} else if (!strcmp(token, "xtrack")) {
			PARSE_STAGE_INT("music:", "xtrack", tmp, 0, 128);
			smus->xtrack = tmp;
		} else {
			char where[40];
			snprintf(where, sizeof(where), "stage 0x%02x: music", stagenum);
			p = modConfigSkipUnknownKey(p, where, token);
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: unterminated music block", stagenum);
		return NULL;
	}

	return p;
}

static char *modConfigParseStageWeatherRooms(char *p, char *token, s32 stagenum, struct weathercfg *wcfg)
{
	// determine where we can start adding rooms
	s32 idx;
	for (idx = 0; idx < WEATHERCFG_MAX_SKIPROOMS && wcfg->skiprooms[idx]; ++idx);

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	// check if user wants to clear the whole list
	p = strParseToken(p, token, NULL);
	if (!strcmp(token, "clear")) {
		memset(wcfg->skiprooms, 0, sizeof(wcfg->skiprooms));
		idx = 0;
		p = strParseToken(p, token, NULL);
	}

	s32 tmp = 0;
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (token[0] == ',' && !token[1]) {
			p = strParseToken(p, token, NULL);
			continue;
		}

		tmp = strtol(token, NULL, 0);
		if (tmp <= 0 || tmp > 32767) {
			sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: weather: rooms: invalid room %s", stagenum, token);
			return NULL;
		}

		if (idx < WEATHERCFG_MAX_SKIPROOMS) {
			wcfg->skiprooms[idx++] = tmp;
		}

		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: weather: unterminated rooms block", stagenum);
		return NULL;
	}

	return p;
}

static char *modConfigParseStageWeather(char *p, char *token, s32 stagenum)
{
	s32 wi;
	struct weathercfg *wcfg = NULL;
	for (wi = 0; wi < ARRAYCOUNT(g_WeatherConfig) && g_WeatherConfig[wi].stagenum; ++wi) {
		if (g_WeatherConfig[wi].stagenum == stagenum) {
			break;
		}
	}

	if (wi >= WEATHERCFG_MAX_STAGES) {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: no more space for weather config", stagenum);
		return NULL;
	}

	wcfg = &g_WeatherConfig[wi];

	if (!wcfg->stagenum) {
		// new weather config; initialize with defaults
		*wcfg = g_DefaultWeatherConfig;
		wcfg->stagenum = stagenum;
	} else {
		// flags have to be re-specified
		wcfg->flags = 0;
	}

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	// parse keyvalues until } is reached
	s32 tmpi = 0;
	f32 tmpf = 0.f;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "include_rooms") || !strcmp(token, "exclude_rooms")) {
			// include_rooms | exclude_rooms { ROOM_NUMBERS... }
			const s32 include = (token[0] == 'i');
			p = modConfigParseStageWeatherRooms(p, token, stagenum, wcfg);
			if (!p) {
				return NULL;
			}
			if (wcfg->skiprooms[0] && include) {
				wcfg->flags |= WEATHERFLAG_INCLUDE;
			}
		} else if (!strcmp(token, "cutscene_only")) {
			wcfg->flags |= WEATHERFLAG_CUTSCENE_ONLY;
		} else if (!strcmp(token, "constant_wind")) {
			PARSE_STAGE_FLOAT("weather:", "constant_wind (0)", wcfg->windanglerad, -M_TAU, M_TAU);
			PARSE_STAGE_FLOAT("weather:", "constant_wind (1)", wcfg->windspeedx, -1024.f, 1024.f);
			PARSE_STAGE_FLOAT("weather:", "constant_wind (2)", wcfg->windspeedz, -1024.f, 1024.f);
			wcfg->flags |= WEATHERFLAG_FORCE_WINDDIR;
		} else if (!strcmp(token, "windspeed")) {
			PARSE_STAGE_FLOAT("weather:", "windspeed", tmpf, -1024.f, 1024.f);
			wcfg->windspeed = tmpf;
		} else if (!strcmp(token, "ymin")) {
			PARSE_STAGE_FLOAT("weather:", "ymin", tmpf, -65536.f, 65536.f);
			wcfg->ymin = tmpf;
		} else if (!strcmp(token, "ymax")) {
			PARSE_STAGE_FLOAT("weather:", "ymax", tmpf, -65536.f, 65536.f);
			wcfg->ymax = tmpf;
		} else if (!strcmp(token, "zmax")) {
			PARSE_STAGE_FLOAT("weather:", "zmax", tmpf, -65536.f, 65536.f);
			wcfg->zmax = tmpf;
		} else {
			char where[40];
			snprintf(where, sizeof(where), "stage 0x%02x: weather", stagenum);
			p = modConfigSkipUnknownKey(p, where, token);
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: unterminated weather block", stagenum);
		return NULL;
	}

	return p;
}

static char *modConfigParseTexture(char *p, char *token, s32 modnum) {
	s32 texid = 0;
	sysLogPrintf(LOG_ERROR, "modconfigParseTexture: parsing texture block: %s, p: %s", token, p);
	if (sscanf(p, "%x", &texid) == 1) {
		sysLogPrintf(LOG_ERROR, "modconfigParseTexture: found texture id: %d", texid);

		// texid is whatever hex the mod's modconfig.txt asked for, and mod
		// texture slots are now based at 4096, past the end of g_Textures[],
		// which is sized for the NUM_TEXTURES vanilla entries and nothing
		// else. Writing through an unchecked id is a heap write off the end
		// of that table. A non-vanilla texture has no vanilla surface config
		// to set, so the block is parsed to its closing brace as usual and
		// the assignments are simply skipped.
		bool isvanillatex = texid >= 0 && texid < NUM_TEXTURES;

		if (!isvanillatex) {
			sysLogPrintf(LOG_ERROR, "modconfig: texture 0x%x is outside the vanilla texture table; surface types ignored", texid);
		}

		while (1) {

			p = strParseToken(p, token, NULL);
			// p = modConfigNextToken(p, token);
			if (!p || strcmp(token, "}") == 0)
				break;

			if (strncmp(token, "surfacetype", 11) == 0) {
				s32 val = 0;
				sscanf(token + 11, "%d", &val);

				// Both fields are 4 bits wide, so any value fits, but
				// g_SurfaceTypes[] has only 15 entries and several of its
				// readers index it without a range test. 15 is storable and
				// out of range, and only a mod can put it there.
				if (isvanillatex && val >= 0 && val < ARRAYCOUNT(g_SurfaceTypes)) {
					g_Textures[texid].surfacetype = val;
				} else if (isvanillatex) {
					sysLogPrintf(LOG_ERROR, "modconfig: texture 0x%x: surfacetype %d out of range", texid, val);
				}
			} else if (strncmp(token, "soundsurfacetype", 16) == 0) {
				s32 val = 0;
				sscanf(token + 16, "%d", &val);

				if (isvanillatex && val >= 0 && val < ARRAYCOUNT(g_SurfaceTypes)) {
					g_Textures[texid].soundsurfacetype = val;
				} else if (isvanillatex) {
					sysLogPrintf(LOG_ERROR, "modconfig: texture 0x%x: soundsurfacetype %d out of range", texid, val);
				}
			}
		}
	}
	return p;
}

static char *modConfigSkipBlock(char *p, char *token)
{
	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{') return NULL;

	// skip until }
	int depth = 1;
	while (p && token[0] && depth > 0) {
		p = strParseToken(p, token, NULL);
		if (!strcmp(token, "{")) depth++;
		else if (!strcmp(token, "}")) depth--;
	}
	return p;
}

void modResetMplayerArrays(void)
{
	if (g_HeadsAndBodies != g_HeadsAndBodiesOriginal) {
		free(g_HeadsAndBodies);
		g_HeadsAndBodies = g_HeadsAndBodiesOriginal;
		g_NumHeadsAndBodies = g_NumHeadsAndBodies_Original;
	}

	if (g_MpHeads != g_MpHeadsOriginal) {
		free(g_MpHeads);
		g_MpHeads = g_MpHeadsOriginal;
		g_NumMpHeads = g_NumMpHeads_Original;
	}

	if (g_MpBodies != g_MpBodiesOriginal) {
		free(g_MpBodies);
		g_MpBodies = g_MpBodiesOriginal;
		g_NumMpBodies = g_NumMpBodies_Original;
	}

	// g_MpArenas points into g_MpArenas_AIO whenever a mod declared an arena,
	// so the pointer goes home before the array it names is released.
	// The rows no longer own their customname strings - mpArenasRebuild lends
	// them from the registry, which frees them in modStageRegReset below - so
	// freeing them here would be a double free.
	mpSetArenaMode(false);

	if (g_MpArenas_AIO) {
		free(g_MpArenas_AIO);
		g_MpArenas_AIO = NULL;
	}
	g_NumMpArenas_AIO = 0;

	if (g_MpArenaGroups) {
		free(g_MpArenaGroups);
		g_MpArenaGroups = NULL;
	}
	g_NumMpArenaGroups = 0;

	modStageRegReset();

	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		free(g_ModHeadNames[i].name);
	}
	free(g_ModHeadNames);
	g_ModHeadNames = NULL;
	g_NumModHeadNames = 0;

	for (s32 i = 0; i < g_NumModHandFileNames; ++i) {
		free(g_ModHandFileNames[i].name);
	}
	free(g_ModHandFileNames);
	g_ModHandFileNames = NULL;
	g_NumModHandFileNames = 0;

	for (s32 i = 0; i < g_NumImportedAssets; ++i) {
		if (g_ImportedAssets[i]) {
			free(g_ImportedAssets[i]);
		}
	}
	g_NumImportedAssets = 0;
}

struct modconfigslotinfo {
	s32 slotNum;
	s32 bodySlotNum;
	s32 bodyName;
	s32 bodyHeadNum;
	u8 requireFeature;
	char handName[64];
};

// Returns updated p. Sets *skipEntry=1 if the entry should be discarded
// (e.g. file not found). On hard parse failure (a malformed value for a key
// we know), returns NULL with *skipEntry left as 0. An unknown key is not a
// hard failure; it is skipped and the rest of the entry is kept.
static char *modConfigParseHeadOrBodyEntry(char *p, char *token, struct headorbody *item, s32 modNum, char *nameOut, struct modconfigslotinfo *slotInfo, s32 *skipEntry)
{
	s32 tmp = 0;
	f32 tmpf = 0.0f;
	char tmps[64] = "";
	if (skipEntry) *skipEntry = 0;

	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "ismale")) {
			PARSE_INT("HeadsAndBodies", "ismale", tmp, 0, 1, NULL);
			item->ismale = tmp;
		}	else if (!strcmp(token, "requiresrom")) {
			p = modConfigParseStringValue(p, token, tmps);
			if (p) {
				sysLogPrintf(LOG_NOTE, "requiresrom %s", tmps);
				if (!romsourceIsMounted(tmps)){
					if (skipEntry) *skipEntry = 1;
					return NULL;
				}
			}
		} else if (!strcmp(token, "slotnum")) {
			PARSE_INT("HeadsAndBodies", "slotnum", tmp, 0, 255, NULL);
			if (slotInfo) slotInfo->slotNum = tmp;
		} else if (!strcmp(token, "requirefeature")) {
			PARSE_INT("HeadsAndBodies", "requirefeature", tmp, 0, 255, NULL);
			if (slotInfo) slotInfo->requireFeature = tmp;
		} else if (!strcmp(token, "bodyslotnum")) {
			PARSE_INT("HeadsAndBodies", "bodyslotnum", tmp, 0, 255, NULL);
			if (slotInfo) slotInfo->bodySlotNum = tmp;
		} else if (!strcmp(token, "bodyname")) {
			PARSE_INT("HeadsAndBodies", "bodyname", tmp, 0, 0xFFFF, NULL);
			if (slotInfo) slotInfo->bodyName = tmp;
		} else if (!strcmp(token, "bodyheadnum")) {
			PARSE_INT("HeadsAndBodies", "bodyheadnum", tmp, 0, 0xFFFF, NULL);
			if (slotInfo) slotInfo->bodyHeadNum = tmp;
		} else if (!strcmp(token, "hasownhead") || !strcmp(token, "unk00_01")) {
			// `unk00_01` is the legacy field name kept for back-compat.
			PARSE_INT("HeadsAndBodies", "hasownhead", tmp, 0, 1, NULL);
			item->unk00_01 = tmp;
		} else if (!strcmp(token, "canvaryheight")) {
			PARSE_INT("HeadsAndBodies", "canvaryheight", tmp, 0, 1, NULL);
			item->canvaryheight = tmp;
		} else if (!strcmp(token, "type")) {
			PARSE_INT("HeadsAndBodies", "type", tmp, 0, 7, NULL);
			item->type = tmp;
		} else if (!strcmp(token, "height")) {
			PARSE_INT("HeadsAndBodies", "height", tmp, 0, 255, NULL);
			item->height = tmp;
		} else if (!strcmp(token, "filenum")) {
			p = modConfigParseFileValue(p, token, &tmp, modNum);
			if (!p) {
				sysLogPrintf(LOG_WARNING, "modconfig: HeadsAndBodies '%s': filenum unresolved, skipping entry",
				             nameOut && nameOut[0] ? nameOut : "?");
				if (skipEntry) *skipEntry = 1;
				return NULL;
			}
			item->filenum = MOD_FILEID_MAKE(modNum, tmp);
		} else if (!strcmp(token, "scale")) {
			p = modConfigParseFloatValue(p, token, &tmpf);
			if (!p) return NULL;
			item->scale = tmpf;
		} else if (!strcmp(token, "animscale")) {
			p = modConfigParseFloatValue(p, token, &tmpf);
			if (!p) return NULL;
			item->animscale = tmpf;
		} else if (!strcmp(token, "handfilenum")) {
			p = modConfigParseFileValue(p, token, &tmp, modNum);
			if (!p) {
				sysLogPrintf(LOG_WARNING, "modconfig: HeadsAndBodies '%s': handfilenum unresolved, skipping entry",
				             nameOut && nameOut[0] ? nameOut : "?");
				if (skipEntry) *skipEntry = 1;
				return NULL;
			}
			item->handfilenum = MOD_FILEID_MAKE(modNum, tmp);
			if (slotInfo) {
				strncpy(slotInfo->handName, strUnquote(token), 63);
				slotInfo->handName[63] = '\0';
			}
		} else if (!strcmp(token, "yoffset")) {
			PARSE_INT("HeadsAndBodies", "yoffset", tmp, -1000, 1000, NULL);
			item->yoffset = tmp;
		} else if (!strcmp(token, "name")) {
			p = strParseToken(p, token, NULL);
			if (nameOut) {
				strncpy(nameOut, strUnquote(token), 63);
				nameOut[63] = '\0';
			}
		} else {
			p = modConfigSkipUnknownKey(p, "HeadsAndBodies", token);
		}
		p = strParseToken(p, token, NULL);
	}
	return p;
}

static char *modConfigParseModelStates(char *p, char *token, s32 modNum)
{
	sysLogPrintf(LOG_NOTE, "modconfig: Parsing ModelStates block for mod %d", modNum);
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: expected '{', got '%s'", token);
		return NULL;
	}

	s32 numParsed = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		// Parse model ID (hex number)
		s32 modelId = strtol(token, NULL, 0);
		sysLogPrintf(LOG_NOTE, "  Parsing model ID: %s -> 0x%04x", token, modelId);
		if (modelId < 0 || modelId >= NUM_MODELS) {
			sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: invalid model ID: %s", token);
			return NULL;
		}

		// Expect '{'
		p = strParseToken(p, token, NULL);
		if (token[0] != '{' || token[1] != '\0') {
			sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: expected '{' after model ID, got '%s'", token);
			return NULL;
		}

		// Parse key-value pairs
		p = strParseToken(p, token, NULL);
		while (p && token[0] && strcmp(token, "}") != 0) {
			// Strip trailing colon from token if present
			s32 len = strlen(token);
			if (len > 0 && token[len - 1] == ':') {
				token[len - 1] = '\0';
			}

			if (!strcasecmp(token, "File")) {
				s32 fileNum = 0;
				p = modConfigParseFileValue(p, token, &fileNum, modNum);
				if (!p) return NULL;
				g_ModelStates[modelId].fileid = MOD_FILEID_MAKE(modNum, fileNum);
			} else if (!strcasecmp(token, "Scale")) {
				f32 scale = 0.0f;
				p = modConfigParseFloatValue(p, token, &scale);
				if (!p || scale <= 0.0f) {
					sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: invalid scale value: %s", token);
					return NULL;
				}
				// Convert float scale to fixed-point (scale * 4096)
				u16 fixedScale = (u16)(scale * 4096.0f);
				g_ModelStates[modelId].scale = fixedScale;
				DEBUG_MODELS("  Model 0x%04x: scale %.4f -> 0x%04x (was 0x%04x)",
					modelId, scale, fixedScale, g_ModelStatesOriginal[modelId].scale);
				numParsed++;
			} else {
				char where[48];
				snprintf(where, sizeof(where), "ModelStates: model 0x%04x", modelId);
				p = modConfigSkipUnknownKey(p, where, token);
			}
			p = strParseToken(p, token, NULL);
		}

		if (token[0] != '}') {
			sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: expected '}' after model properties");
			return NULL;
		}

		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: ModelStates: expected '}' at end of block");
		return NULL;
	}

	sysLogPrintf(LOG_NOTE, "modconfig: ModelStates: parsed %d model overrides", numParsed);
	return p;
}

static char *modConfigParseExplosionTypes(char *p, char *token, s32 modNum)
{
	sysLogPrintf(LOG_NOTE, "modconfig: Parsing ExplosionTypes block for mod %d", modNum);
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected '{', got '%s'", token);
		return NULL;
	}

	s32 numParsed = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		// Parse model ID (hex number)
		s32 modelId = strtol(token, NULL, 0);
		if (modelId < 0 || modelId >= NUM_MODELS) {
			sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: invalid model ID: %s", token);
			return NULL;
		}

		// Expect '{'
		p = strParseToken(p, token, NULL);
		if (token[0] != '{' || token[1] != '\0') {
			sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected '{' after model ID, got '%s'", token);
			return NULL;
		}

		// Parse key-value pairs
		p = strParseToken(p, token, NULL);
		while (p && token[0] && strcmp(token, "}") != 0) {
			if (!strcasecmp(token, "Type")) {
				p = strParseToken(p, token, NULL);
				if (!p || token[0] != ':'  || token[1] != '\0') {
					sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected ':' after 'Type'");
					return NULL;
				}
				p = strParseToken(p, token, NULL);
				if (!p) return NULL;

				// Map explosion type name to value
				s8 expType = -1;
				if (!strcasecmp(token, "NONE")) expType = 0;
				else if (!strcasecmp(token, "BULLETHOLE")) expType = 1;
				else if (!strcasecmp(token, "EYESPY")) expType = 2;
				else if (!strcasecmp(token, "LAPTOP")) expType = 3;
				else if (!strcasecmp(token, "A51TABLE")) expType = 4;
				else if (!strcasecmp(token, "FRTARGET")) expType = 5;
				else if (!strcasecmp(token, "6")) expType = 6;
				else if (!strcasecmp(token, "7")) expType = 7;
				else if (!strcasecmp(token, "8")) expType = 8;
				else if (!strcasecmp(token, "9")) expType = 9;
				else if (!strcasecmp(token, "11")) expType = 11;
				else if (!strcasecmp(token, "12")) expType = 12;
				else if (!strcasecmp(token, "ROCKET")) expType = 13;
				else if (!strcasecmp(token, "GASBARREL")) expType = 14;
				else if (!strcasecmp(token, "16")) expType = 16;
				else if (!strcasecmp(token, "HUGE17")) expType = 17;
				else if (!strcasecmp(token, "BONDEXPLODE")) expType = 18;
				else if (!strcasecmp(token, "SDGRENADE")) expType = 21;
				else if (!strcasecmp(token, "PHOENIX")) expType = 22;
				else if (!strcasecmp(token, "DRAGONBOMBSPY")) expType = 23;
				else if (!strcasecmp(token, "24")) expType = 24;
				else if (!strcasecmp(token, "HUGE25")) expType = 25;
				else {
					sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: unknown explosion type: %s", token);
					return NULL;
				}

				g_PropExplosionTypes[modelId] = expType;
			} else {
				char where[48];
				snprintf(where, sizeof(where), "ExplosionTypes: model 0x%04x", modelId);
				p = modConfigSkipUnknownKey(p, where, token);
			}
			p = strParseToken(p, token, NULL);
		}

		if (token[0] != '}') {
			sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected '}' after model properties");
			return NULL;
		}

		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: ExplosionTypes: expected '}' at end of block");
		return NULL;
	}

	sysLogPrintf(LOG_NOTE, "modconfig: ExplosionTypes: parsed %d explosion type overrides", numParsed);
	return p;
}

static struct { char *name; s32 id; } g_VanillaHeadNames[] = {
	{ "head_dark_combat", HEAD_DARK_COMBAT },
	{ "head_elvis", HEAD_ELVIS },
	{ "head_ross", HEAD_ROSS },
	{ "head_carrington", HEAD_CARRINGTON },
	{ "head_mrblonde", HEAD_MRBLONDE },
	{ "head_trent", HEAD_TRENT },
	{ "head_ddshock", HEAD_DDSHOCK },
	{ "head_graham", HEAD_GRAHAM },
	{ "head_dark_frock", HEAD_DARK_FROCK },
	{ "head_secretary", HEAD_SECRETARY },
	{ "head_cassandra", HEAD_CASSANDRA },
	{ "head_theking", HEAD_THEKING },
	{ "head_fem_guard", HEAD_FEM_GUARD },
	{ "head_jon", HEAD_JON },
	{ "head_mark2", HEAD_MARK2 },
	{ "head_christ", HEAD_CHRIST },
	{ "head_russ", HEAD_RUSS },
	{ "head_grey", HEAD_GREY },
	{ "head_darling", HEAD_DARLING },
	{ "head_robert", HEAD_ROBERT },
	{ "head_beau1", HEAD_BEAU1 },
	{ "head_fem_guard2", HEAD_FEM_GUARD2 },
	{ "head_brian", HEAD_BRIAN },
	{ "head_jamie", HEAD_JAMIE },
	{ "head_duncan2", HEAD_DUNCAN2 },
	{ "head_biotech", HEAD_BIOTECH },
	{ "head_neil2", HEAD_NEIL2 },
	{ "head_edmcg", HEAD_EDMCG },
	{ "head_anka", HEAD_ANKA },
	{ "head_leslie_s", HEAD_LESLIE_S },
	{ "head_matt_c", HEAD_MATT_C },
	{ "head_peer_s", HEAD_PEER_S },
	{ "head_eileen_t", HEAD_EILEEN_T },
	{ "head_andy_r", HEAD_ANDY_R },
	{ "head_ben_r", HEAD_BEN_R },
	{ "head_steve_k", HEAD_STEVE_K },
	{ "head_jonathan", HEAD_JONATHAN },
	{ "head_maian_s", HEAD_MAIAN_S },
	{ "head_shaun", HEAD_SHAUN },
	{ "head_beau2", HEAD_BEAU2 },
	{ "head_eileen_h", HEAD_EILEEN_H },
	{ "head_scott_h", HEAD_SCOTT_H },
	{ "head_sanchez", HEAD_SANCHEZ },
	{ "head_darkaqua", HEAD_DARKAQUA },
	{ "head_ddsniper", HEAD_DDSNIPER },
	{ "head_beau3", HEAD_BEAU3 },
	{ "head_beau4", HEAD_BEAU4 },
	{ "head_beau5", HEAD_BEAU5 },
	{ "head_beau6", HEAD_BEAU6 },
	{ "head_griffey", HEAD_GRIFFEY },
	{ "head_moto", HEAD_MOTO },
	{ "head_keith", HEAD_KEITH },
	{ "head_winner", HEAD_WINNER },
	{ "head_a51faceplate", HEAD_A51FACEPLATE },
	{ "head_elvis_gogs", HEAD_ELVIS_GOGS },
	{ "head_stevem", HEAD_STEVEM },
	{ "head_dark_snow", HEAD_DARK_SNOW },
	{ "head_president", HEAD_PRESIDENT },
	{ "head_vd", HEAD_VD },
	{ "head_ken", HEAD_KEN },
	{ "head_joel", HEAD_JOEL },
	{ "head_tim", HEAD_TIM },
	{ "head_grant", HEAD_GRANT },
	{ "head_penny", HEAD_PENNY },
	{ "head_robin", HEAD_ROBIN },
	{ "head_alex", HEAD_ALEX },
	{ "head_julianne", HEAD_JULIANNE },
	{ "head_laura", HEAD_LAURA },
	{ "head_davec", HEAD_DAVEC },
	{ "head_cook", HEAD_COOK },
	{ "head_pryce", HEAD_PRYCE },
	{ "head_silke", HEAD_SILKE },
	{ "head_smith", HEAD_SMITH },
	{ "head_gareth", HEAD_GARETH },
	{ "head_murchie", HEAD_MURCHIE },
	{ "head_wong", HEAD_WONG },
	{ "head_carter", HEAD_CARTER },
	{ "head_tintin", HEAD_TINTIN },
	{ "head_munton", HEAD_MUNTON },
	{ "head_stamper", HEAD_STAMPER },
	{ "head_jones", HEAD_JONES },
	{ "head_phelps", HEAD_PHELPS },
	{ NULL, -1 }
};

static struct { char *name; s32 id; } g_VanillaBodyNames[] = {
	{ "body_dark_combat", BODY_DARK_COMBAT },
	{ "body_elvis1", BODY_ELVIS1 },
	{ "body_area51guard", BODY_AREA51GUARD },
	{ "body_overall", BODY_OVERALL },
	{ "body_carrington", BODY_CARRINGTON },
	{ "body_mrblonde", BODY_MRBLONDE },
	{ "body_skedar", BODY_SKEDAR },
	{ "body_trent", BODY_TRENT },
	{ "body_ddshock", BODY_DDSHOCK },
	{ "body_labtech", BODY_LABTECH },
	{ "body_stripes", BODY_STRIPES },
	{ "body_dark_frock", BODY_DARK_FROCK },
	{ "body_dark_trench", BODY_DARK_TRENCH },
	{ "body_officeworker", BODY_OFFICEWORKER },
	{ "body_officeworker2", BODY_OFFICEWORKER2 },
	{ "body_secretary", BODY_SECRETARY },
	{ "body_cassandra", BODY_CASSANDRA },
	{ "body_theking", BODY_THEKING },
	{ "body_fem_guard", BODY_FEM_GUARD },
	{ "body_dd_labtech", BODY_DD_LABTECH },
	{ "body_dd_secguard", BODY_DD_SECGUARD },
	{ "body_drcaroll", BODY_DRCAROLL },
	{ "body_eyespy", BODY_EYESPY },
	{ "body_dark_ripped", BODY_DARK_RIPPED },
	{ "body_dd_guard", BODY_DD_GUARD },
	{ "body_dd_shock_inf", BODY_DD_SHOCK_INF },
	{ "body_testchr", BODY_TESTCHR },
	{ "body_biotech", BODY_BIOTECH },
	{ "body_fbiguy", BODY_FBIGUY },
	{ "body_ciaguy", BODY_CIAGUY },
	{ "body_a51trooper", BODY_A51TROOPER },
	{ "body_a51airman", BODY_A51AIRMAN },
	{ "body_chicrob", BODY_CHICROB },
	{ "body_steward", BODY_STEWARD },
	{ "body_stewardess", BODY_STEWARDESS },
	{ "body_president", BODY_PRESIDENT },
	{ "body_stewardess_coat", BODY_STEWARDESS_COAT },
	{ "body_miniskedar", BODY_MINISKEDAR },
	{ "body_nsa_lackey", BODY_NSA_LACKEY },
	{ "body_pres_security", BODY_PRES_SECURITY },
	{ "body_negotiator", BODY_NEGOTIATOR },
	{ "body_g5_guard", BODY_G5_GUARD },
	{ "body_pelagic_guard", BODY_PELAGIC_GUARD },
	{ "body_g5_swat_guard", BODY_G5_SWAT_GUARD },
	{ "body_alaskan_guard", BODY_ALASKAN_GUARD },
	{ "body_maian_soldier", BODY_MAIAN_SOLDIER },
	{ "body_president_clone", BODY_PRESIDENT_CLONE },
	{ "body_president_clone2", BODY_PRESIDENT_CLONE2 },
	{ "body_dark_af1", BODY_DARK_AF1 },
	{ "body_darkwet", BODY_DARKWET },
	{ "body_darkaqualung", BODY_DARKAQUALUNG },
	{ "body_darksnow", BODY_DARKSNOW },
	{ "body_darklab", BODY_DARKLAB },
	{ "body_femlabtech", BODY_FEMLABTECH },
	{ "body_ddsniper", BODY_DDSNIPER },
	{ "body_pilotaf1", BODY_PILOTAF1 },
	{ "body_cilabtech", BODY_CILABTECH },
	{ "body_cifemtech", BODY_CIFEMTECH },
	{ "body_carreveningsuit", BODY_CARREVENINGSUIT },
	{ "body_jonathan", BODY_JONATHAN },
	{ "body_cisoldier", BODY_CISOLDIER },
	{ "body_skedarking", BODY_SKEDARKING },
	{ "body_elviswaistcoat", BODY_ELVISWAISTCOAT },
	{ "body_dark_leather", BODY_DARK_LEATHER },
	{ "body_dark_negotiator", BODY_DARK_NEGOTIATOR },
	{ NULL, -1 }
};

s32 modLookupHeadByName(const char *name)
{
	if (!name || !name[0]) return -1;

	// Check vanilla names
	for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
		if (!strcmp(name, g_VanillaHeadNames[i].name)) {
			// Find which MpHead slot points to this HeadsAndBodies index
			for (s32 j = 0; j < g_NumMpHeads; ++j) {
				if (g_MpHeads[j].headnum == g_VanillaHeadNames[i].id) {
					return j;
				}
			}
			return -1;
		}
	}

	// Check dynamic mod names
	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		if (!strcmp(name, g_ModHeadNames[i].name)) {
			for (s32 j = 0; j < g_NumMpHeads; ++j) {
				if (g_MpHeads[j].headnum == g_ModHeadNames[i].id) {
					return j;
				}
			}
			return -1;
		}
	}

	return -1;
}

s32 modLookupHandFileByName(const char *name)
{
	if (!name || !name[0]) return -1;

	for (s32 i = 0; i < g_NumModHandFileNames; ++i) {
		if (!strcmp(name, g_ModHandFileNames[i].name)) {
			return (s32)g_ModHandFileNames[i].filenum;
		}
	}

	return -1;
}

s32 modLookupHeadnumByName(const char *name)
{
	if (!name || !name[0]) return -1;

	for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
		if (!strcmp(name, g_VanillaHeadNames[i].name)) {
			return g_VanillaHeadNames[i].id;
		}
	}

	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		if (!strcmp(name, g_ModHeadNames[i].name)) {
			return g_ModHeadNames[i].id;
		}
	}

	return -1;
}

s32 modLookupBodyByName(const char *name)
{
	if (!name || !name[0]) return -1;

	// Check vanilla names
	for (s32 i = 0; g_VanillaBodyNames[i].name; ++i) {
		if (!strcmp(name, g_VanillaBodyNames[i].name)) {
			for (s32 j = 0; j < g_NumMpBodies; ++j) {
				if (g_MpBodies[j].bodynum == g_VanillaBodyNames[i].id) {
					return j;
				}
			}
			return -1;
		}
	}

	// Check dynamic mod names
	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		if (!strcmp(name, g_ModHeadNames[i].name)) {
			for (s32 j = 0; j < g_NumMpBodies; ++j) {
				if (g_MpBodies[j].bodynum == g_ModHeadNames[i].id) {
					return j;
				}
			}
			return -1;
		}
	}

	return -1;
}

// Reverse lookup: given a HeadsAndBodies array index (the value stored in
// g_MpHeads[i].headnum or g_MpBodies[i].bodynum), return the matching
// name string from the vanilla or mod-registered name tables. Returns NULL
// if no name is registered for that slot.
const char *modGetNameForHeadBodyIndex(s32 headBodyIndex)
{
	if (headBodyIndex < 0) {
		return NULL;
	}
	for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
		if (g_VanillaHeadNames[i].id == headBodyIndex) {
			return g_VanillaHeadNames[i].name;
		}
	}
	for (s32 i = 0; g_VanillaBodyNames[i].name; ++i) {
		if (g_VanillaBodyNames[i].id == headBodyIndex) {
			return g_VanillaBodyNames[i].name;
		}
	}
	for (s32 i = 0; i < g_NumModHeadNames; ++i) {
		if (g_ModHeadNames[i].id == headBodyIndex) {
			return g_ModHeadNames[i].name;
		}
	}
	return NULL;
}

static char *modConfigParseHeadsAndBodies(char *p, char *token, s32 modNum)
{
	struct headorbody tempItem;
	memset(&tempItem, 0, sizeof(tempItem));
	char name[64] = "";

	struct modconfigslotinfo slotInfo = { -1, -1, -1, -1, 0, "" };

	// Save the position right before the opening '{' so we can recover by
	// skipping the entire block if a non-fatal parse error occurs.
	char *blockStart = p;

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	p = strParseToken(p, token, NULL);
	s32 skipEntry = 0;
	p = modConfigParseHeadOrBodyEntry(p, token, &tempItem, modNum, name, &slotInfo, &skipEntry);

	if (skipEntry) {
		// Soft failure: skip to end of block and continue parsing the modconfig.
		sysLogPrintf(LOG_WARNING, "modconfig: skipping HeadsAndBodies '%s' (mod %d) due to unresolved file reference",
		             name[0] ? name : "?", modNum);
		return modConfigSkipBlock(blockStart, token);
	}

	if (!p || token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: unterminated HeadsAndBodies block");
		return NULL;
	}

	// Ensure array is writable
	if (g_HeadsAndBodies == g_HeadsAndBodiesOriginal) {
		struct headorbody *new_array = malloc((g_NumHeadsAndBodies + 1) * sizeof(struct headorbody));
		if (!new_array) return NULL;
		memcpy(new_array, g_HeadsAndBodiesOriginal, g_NumHeadsAndBodies * sizeof(struct headorbody));
		g_HeadsAndBodies = new_array;
	}

	s32 replaceIndex = -1;
	if (name[0]) {
		// Check vanilla names first
		for (s32 i = 0; g_VanillaHeadNames[i].name; ++i) {
			if (!strcmp(name, g_VanillaHeadNames[i].name)) {
				replaceIndex = g_VanillaHeadNames[i].id;
				break;
			}
		}
		// Then check dynamic mod names
		if (replaceIndex < 0) {
			for (s32 i = 0; i < g_NumModHeadNames; ++i) {
				if (!strcmp(name, g_ModHeadNames[i].name)) {
					replaceIndex = g_ModHeadNames[i].id;
					break;
				}
			}
		}
	}

	if (replaceIndex >= 0) {
		// Overwrite existing head/body entry
		if (g_NumHeadsAndBodies > replaceIndex) {
			g_HeadsAndBodies[replaceIndex] = tempItem;
			sysLogPrintf(LOG_NOTE, "modconfig: replaced %s (index %d) with imported asset", name, replaceIndex);
		} else {
			sysLogPrintf(LOG_ERROR, "modconfig: cannot replace %s, array too small", name);
		}
	} else {
		// Append
		struct headorbody *new_array = realloc(g_HeadsAndBodies, (g_NumHeadsAndBodies + 1) * sizeof(struct headorbody));
		if (!new_array) {
			sysLogPrintf(LOG_ERROR, "modconfig: failed to allocate memory for HeadsAndBodies");
			return NULL;
		}
		g_HeadsAndBodies = new_array;
		g_HeadsAndBodies[g_NumHeadsAndBodies] = tempItem;
		g_NumHeadsAndBodies++;

		// Register name for future lookups (modLookupHeadByName etc.)
		if (name[0]) {
			void *tmp = realloc(g_ModHeadNames, (g_NumModHeadNames + 1) * sizeof(*g_ModHeadNames));
			if (tmp) {
				g_ModHeadNames = tmp;
				g_ModHeadNames[g_NumModHeadNames].name = strDuplicate(name);
				g_ModHeadNames[g_NumModHeadNames].id = g_NumHeadsAndBodies - 1;
				g_NumModHeadNames++;
				sysLogPrintf(LOG_NOTE, "modconfig: registered head name '%s' -> HeadsAndBodies[%d]",
				             name, g_NumHeadsAndBodies - 1);
			}
		}
	}

	// Register hand file name for future lookups (modLookupHandFileByName)
	if (slotInfo.handName[0]) {
		s32 replaced = 0;
		for (s32 i = 0; i < g_NumModHandFileNames; ++i) {
			if (!strcmp(slotInfo.handName, g_ModHandFileNames[i].name)) {
				g_ModHandFileNames[i].filenum = tempItem.handfilenum;
				replaced = 1;
				break;
			}
		}
		if (!replaced) {
			void *tmp = realloc(g_ModHandFileNames, (g_NumModHandFileNames + 1) * sizeof(*g_ModHandFileNames));
			if (tmp) {
				g_ModHandFileNames = tmp;
				g_ModHandFileNames[g_NumModHandFileNames].name = strDuplicate(slotInfo.handName);
				g_ModHandFileNames[g_NumModHandFileNames].filenum = tempItem.handfilenum;
				g_NumModHandFileNames++;
			}
		}
	}

	s32 headBodyIndex = (replaceIndex >= 0) ? replaceIndex : (g_NumHeadsAndBodies - 1);

	// Head-slot linking
	if (slotInfo.slotNum >= 0) {
		modHeadSlotClaim(name, slotInfo.slotNum);

		if (g_MpHeads == g_MpHeadsOriginal) {
			struct mphead *new_array = malloc(g_NumMpHeads * sizeof(struct mphead));
			if (new_array) {
				memcpy(new_array, g_MpHeadsOriginal, g_NumMpHeads * sizeof(struct mphead));
				g_MpHeads = new_array;
			}
		}

		if (g_MpHeads != g_MpHeadsOriginal) {
			if (slotInfo.slotNum >= g_NumMpHeads) {
				s32 oldNum = g_NumMpHeads;
				struct mphead *new_array = realloc(g_MpHeads, (slotInfo.slotNum + 1) * sizeof(struct mphead));
				if (new_array) {
					g_MpHeads = new_array;
					g_NumMpHeads = slotInfo.slotNum + 1;
					memset(&g_MpHeads[oldNum], 0, (g_NumMpHeads - oldNum) * sizeof(struct mphead));
				}
			}

			if (slotInfo.slotNum < g_NumMpHeads) {
				g_MpHeads[slotInfo.slotNum].headnum = headBodyIndex;
				g_MpHeads[slotInfo.slotNum].requirefeature = slotInfo.requireFeature;
			}
		}
	} else if (replaceIndex < 0 && slotInfo.bodySlotNum < 0) {
		// New entry with no explicit slot and no body slot: it goes to the
		// index this head name has been allocated, which does not move when
		// the roster does. A head with no name, or one past the end of the
		// reservation table, falls back to appending.
		s32 headSlot = modHeadSlotReserve(name);

		if (g_MpHeads == g_MpHeadsOriginal) {
			struct mphead *new_array = malloc(g_NumMpHeads * sizeof(struct mphead));
			if (new_array) {
				memcpy(new_array, g_MpHeadsOriginal, g_NumMpHeads * sizeof(struct mphead));
				g_MpHeads = new_array;
			}
		}

		if (headSlot < 0) {
			headSlot = g_NumMpHeads;
		}

		if (g_MpHeads != g_MpHeadsOriginal) {
			if (headSlot >= g_NumMpHeads) {
				s32 oldNum = g_NumMpHeads;
				struct mphead *new_array = realloc(g_MpHeads, (headSlot + 1) * sizeof(struct mphead));
				if (new_array) {
					g_MpHeads = new_array;
					g_NumMpHeads = headSlot + 1;
					memset(&g_MpHeads[oldNum], 0, (g_NumMpHeads - oldNum) * sizeof(struct mphead));
				}
			}

			if (headSlot < g_NumMpHeads) {
				g_MpHeads[headSlot].headnum = headBodyIndex;
				g_MpHeads[headSlot].requirefeature = slotInfo.requireFeature;
				sysLogPrintf(LOG_NOTE, "modconfig: head '%s' -> MpHeads[%d]", name, headSlot);
			}
		}
	}

	// Body-slot linking
	if (slotInfo.bodySlotNum >= 0) {
		// bodyslotnum 1 is a flag meaning "auto-assign next available slot"
		s32 bodySlot = slotInfo.bodySlotNum;
		if (bodySlot == 1) {
			bodySlot = modBodySlotReserve(name);

			if (bodySlot < 0) {
				// No name, or the reservation table is full. If a body slot
				// already references this headBodyIndex (e.g. the modconfig has
				// been re-parsed), update that slot in place instead of
				// allocating a duplicate.
				s32 existingSlot = -1;
				for (s32 i = 0; i < g_NumMpBodies; ++i) {
					if (g_MpBodies[i].bodynum == headBodyIndex) {
						existingSlot = i;
						break;
					}
				}
				bodySlot = (existingSlot >= 0) ? existingSlot : g_NumMpBodies;
			}
		} else {
			modBodySlotClaim(name, bodySlot);
		}

		if (g_MpBodies == g_MpBodiesOriginal) {
			struct mpbody *new_array = malloc(g_NumMpBodies * sizeof(struct mpbody));
			if (new_array) {
				memcpy(new_array, g_MpBodiesOriginal, g_NumMpBodies * sizeof(struct mpbody));
				g_MpBodies = new_array;
			}
		}

		if (g_MpBodies != g_MpBodiesOriginal) {
			if (bodySlot >= g_NumMpBodies) {
				s32 oldNum = g_NumMpBodies;
				struct mpbody *new_array = realloc(g_MpBodies, (bodySlot + 1) * sizeof(struct mpbody));
				if (new_array) {
					g_MpBodies = new_array;
					g_NumMpBodies = bodySlot + 1;
					memset(&g_MpBodies[oldNum], 0, (g_NumMpBodies - oldNum) * sizeof(struct mpbody));
				}
			}

			if (bodySlot < g_NumMpBodies) {
				g_MpBodies[bodySlot].bodynum = headBodyIndex;
				g_MpBodies[bodySlot].requirefeature = slotInfo.requireFeature;
				if (slotInfo.bodyName >= 0) {
					g_MpBodies[bodySlot].name = slotInfo.bodyName;
				}
				if (slotInfo.bodyHeadNum >= 0) {
					g_MpBodies[bodySlot].headnum = slotInfo.bodyHeadNum;
				}
			}
		}
	}

	return p;
}

static char *modConfigParseMpHeads(char *p, char *token)
{
	struct mphead *new_array;

	if (g_MpHeads == g_MpHeadsOriginal) {
		new_array = malloc((g_NumMpHeads + 1) * sizeof(struct mphead));
		if (!new_array) return NULL;
		memcpy(new_array, g_MpHeadsOriginal, g_NumMpHeads * sizeof(struct mphead));
	} else {
		new_array = realloc(g_MpHeads, (g_NumMpHeads + 1) * sizeof(struct mphead));
		if (!new_array) return NULL;
	}

	g_MpHeads = new_array;
	struct mphead *item = &g_MpHeads[g_NumMpHeads];
	memset(item, 0, sizeof(struct mphead));

	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') return NULL;

	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "headnum")) {
			PARSE_INT("MpHeads", "headnum", tmp, 0, 0xFFFF, NULL);
			item->headnum = tmp;
		} else if (!strcmp(token, "requirefeature")) {
			PARSE_INT("MpHeads", "requirefeature", tmp, 0, 255, NULL);
			item->requirefeature = tmp;
		} else {
			p = modConfigSkipUnknownKey(p, "MpHeads", token);
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') return NULL;

	g_NumMpHeads++;
	return p;
}

static char *modConfigParseMpBodies(char *p, char *token)
{
	struct mpbody *new_array;

	if (g_MpBodies == g_MpBodiesOriginal) {
		new_array = malloc((g_NumMpBodies + 1) * sizeof(struct mpbody));
		if (!new_array) return NULL;
		memcpy(new_array, g_MpBodiesOriginal, g_NumMpBodies * sizeof(struct mpbody));
	} else {
		new_array = realloc(g_MpBodies, (g_NumMpBodies + 1) * sizeof(struct mpbody));
		if (!new_array) return NULL;
	}

	g_MpBodies = new_array;
	struct mpbody *item = &g_MpBodies[g_NumMpBodies];
	memset(item, 0, sizeof(struct mpbody));

	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') return NULL;

	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "bodynum")) {
			PARSE_INT("MpBodies", "bodynum", tmp, 0, 0xFFFF, NULL);
			item->bodynum = tmp;
		} else if (!strcmp(token, "name")) {
			PARSE_INT("MpBodies", "name", tmp, 0, 0xFFFF, NULL);
			item->name = tmp;
		} else if (!strcmp(token, "headnum")) {
			PARSE_INT("MpBodies", "headnum", tmp, 0, 0xFFFF, NULL);
			item->headnum = tmp;
		} else if (!strcmp(token, "requirefeature")) {
			PARSE_INT("MpBodies", "requirefeature", tmp, 0, 255, NULL);
			item->requirefeature = tmp;
		} else {
			p = modConfigSkipUnknownKey(p, "MpBodies", token);
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') return NULL;

	g_NumMpBodies++;
	return p;
}

struct modStageRegEntry *g_ModStageReg = NULL;
s32 g_NumModStageReg = 0;

void modStageRegReset(void)
{
	for (s32 i = 0; i < g_NumModStageReg; i++) {
		free(g_ModStageReg[i].name);
	}
	free(g_ModStageReg);
	g_ModStageReg = NULL;
	g_NumModStageReg = 0;
}

struct modStageRegEntry *modStageRegFind(s32 stagenum)
{
	for (s32 i = 0; i < g_NumModStageReg; i++) {
		if (g_ModStageReg[i].stagenum == stagenum) {
			return &g_ModStageReg[i];
		}
	}

	return NULL;
}

s32 modStageRegCount(s32 kindmask)
{
	s32 count = 0;

	for (s32 i = 0; i < g_NumModStageReg; i++) {
		if (g_ModStageReg[i].kind & kindmask) {
			count++;
		}
	}

	return count;
}

/*
 * Record what a mod said about a stage.
 *
 * Keyed by stagenum and updated in place, because one modconfig is parsed
 * several times per boot: pdmain runs modConfigLoad once per mod dir and again
 * for mod 0, modCacheAllConfigs runs it once more per mod, and modSwitch falls
 * back to it when the cache is cold. Appending would put one arena row in the
 * Combat Simulator list per parse rather than per stage.
 *
 * A stagenum with no g_Stages row cannot be loaded at all - stageGetIndex is
 * what romdata and bg key off - so it is refused here rather than offered in a
 * menu that would fail on selection. modConfigParseStage already bails on that
 * case before reaching this, so the check bites only for a legacy MpArena
 * block, which names a stagenum with no stage block behind it.
 */
struct modStageRegEntry *modStageRegRecord(s32 stagenum, s32 modnum, s32 kind,
		const char *name, s32 langid, s32 requirefeature)
{
	struct modStageRegEntry *e;

	if (stageGetIndex(stagenum) < 0) {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: no stage table row; not registered", stagenum);
		return NULL;
	}

	e = modStageRegFind(stagenum);

	if (!e) {
		struct modStageRegEntry *grown = realloc(g_ModStageReg,
				(g_NumModStageReg + 1) * sizeof(struct modStageRegEntry));

		if (!grown) {
			sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: out of memory registering stage", stagenum);
			return NULL;
		}

		g_ModStageReg = grown;
		e = &g_ModStageReg[g_NumModStageReg];
		memset(e, 0, sizeof(*e));
		e->stagenum = stagenum;
		g_NumModStageReg++;
	}

	// Last writer wins on the owning mod, matching g_ModStageNums above, which
	// the same block has already overwritten the same way.
	e->modnum = (s8)modnum;
	e->kind |= (u8)(kind & MODSTAGE_KIND_BOTH);

	if (requirefeature > 0) {
		e->requirefeature = (u8)requirefeature;
	}

	if (langid > 0) {
		e->langid = (u16)langid;
	}

	if (name && name[0]) {
		free(e->name);
		e->name = strDuplicate(name);
	}

	return e;
}

/*
 * Gated on PD_DEBUG_MODSTAGE=1 like modStageDumpOwnership, and filtered the
 * same way: grep '^MODSTAGE ' pd.log.
 */
void modStageRegReport(void)
{
	static const char *const kindnames[] = { "none", "solo", "mp", "both" };

	if (!g_DebugModStage) {
		return;
	}

	for (s32 i = 0; i < g_NumModStageReg; i++) {
		const struct modStageRegEntry *e = &g_ModStageReg[i];

		const char *stagename = stageGetName(e->stagenum);

		/* stage= is the identity a modconfig now declares; arena= is the
		 * display string an MP block set, which is a different thing. */
		MODSTAGE("reg stage=0x%02x/%s kind=%s mod=%d feature=%d arena='%s'",
				e->stagenum, stagename ? stagename : "?",
				kindnames[e->kind & MODSTAGE_KIND_BOTH], e->modnum,
				e->requirefeature, e->name ? e->name : "");
	}

	MODSTAGE("reg summary entries=%d solo=%d mp=%d", g_NumModStageReg,
			modStageRegCount(MODSTAGE_KIND_SOLO), modStageRegCount(MODSTAGE_KIND_MP));
}

/*
 * Ungated, unlike the report above, because a mod author who wrote `kind solo`
 * would otherwise get silence and assume it worked. See enum modStageKind for
 * what listing a solo stage would actually cost.
 */
void modStageRegWarnUnlisted(void)
{
	const s32 solo = modStageRegCount(MODSTAGE_KIND_SOLO);

	if (solo > 0) {
		sysLogPrintf(LOG_WARNING,
				"modconfig: %d stage(s) declared 'kind solo'; recorded in the stage registry "
				"but not yet listed - the mission list is indexed by save-file stage index",
				solo);
	}
}

/*
 * The legacy standalone arena declaration.
 *
 * It now feeds the registry instead of appending straight to g_MpArenas_AIO,
 * so there is one collector rather than two. Nothing in the tree or in
 * PD_AIO_March_2026 spells MpArena in a modconfig.txt - the string survives
 * only inside the prebuilt pd.exe - so keeping every key it ever accepted
 * costs nothing and stops an unseen config hard-failing on an unknown key.
 * Prefer `kind mp` inside the stage block: it cannot drift from the stage
 * declaration the way a repeated stagenum can.
 */
static char *modConfigParseMpArena(char *p, char *token)
{
	s32 stagenum = -1;
	s32 requirefeature = 0;
	s32 langid = 0;
	char name[64];

	name[0] = '\0';

	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') return NULL;

	s32 tmp = 0;
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "stagenum")) {
			PARSE_INT("MpArena", "stagenum", tmp, 0, 0xFFFF, NULL);
			stagenum = tmp;
		} else if (!strcmp(token, "requirefeature")) {
			PARSE_INT("MpArena", "requirefeature", tmp, 0, 255, NULL);
			requirefeature = tmp;
		} else if (!strcmp(token, "name")) {
			PARSE_INT("MpArena", "name", tmp, 0, 0xFFFF, NULL);
			langid = tmp;
		} else if (!strcmp(token, "label") || !strcmp(token, "literalname")) {
			p = strParseToken(p, token, NULL);
			if (!p) return NULL;
			strncpy(name, strUnquote(token), sizeof(name) - 1);
			name[sizeof(name) - 1] = '\0';
		} else if (!strcmp(token, "group")) {
			// Retired with MpArenaGroup; see modConfigLoad. Still parsed so an
			// older config does not hard-fail on an unknown key.
			p = strParseToken(p, token, NULL);
			if (!p) return NULL;
		} else {
			p = modConfigSkipUnknownKey(p, "MpArena", token);
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') return NULL;

	if (stagenum < 0) {
		sysLogPrintf(LOG_ERROR, "modconfig: MpArena: block names no stagenum");
		return NULL;
	}

	modStageRegRecord(stagenum, g_ModNum, MODSTAGE_KIND_MP,
			name[0] ? name : NULL, langid, requirefeature);

	return p;
}

struct modStageBinding g_StageBindings[NUM_STAGENUMS];

/*
 * The stage number space is the multiplayer save's 7-bit field and nothing
 * else. Held here rather than in a comment, because the failure mode is
 * silent: savebufferOr has no bounds check and the layout is positional, so a
 * stagenum past the field truncates into whatever is written next.
 */
_Static_assert(NUM_STAGENUMS == (1 << 7),
		"g_MpSetup.stagenum is saved in 7 bits (mplayer.c:4692); the space is 0..127");
_Static_assert(STAGE_CREDITS < NUM_STAGENUMS && STAGE_4MBMENU < NUM_STAGENUMS
		&& STAGE_TITLE < NUM_STAGENUMS,
		"the menu pseudo-stages must stay inside the saved field");

_Static_assert(ARRAYCOUNT(g_StageBindings) == ARRAYCOUNT(g_ModStageNums),
		"the binding record and the stage->mod map must cover the same stage numbers");

static const char *const g_ModStageFieldNames[MODSTAGE_FIELD_COUNT] = {
	"bgfile", "tilesfile", "padsfile", "setupfile", "mpsetupfile",
	"allocation", "music", "weather"
};

const char *modStageFieldName(enum modStageField field)
{
	if (field < 0 || field >= MODSTAGE_FIELD_COUNT) {
		return "?";
	}

	return g_ModStageFieldNames[field];
}

void modStageBindingsReset(void)
{
	for (s32 i = 0; i < (s32)ARRAYCOUNT(g_StageBindings); ++i) {
		g_StageBindings[i].claimMask = 0;
		g_StageBindings[i].claimedBy = -1;
		g_StageBindings[i].priority = 0;
		g_StageBindings[i].claimCount = 0;

		for (s32 j = 0; j < MODSTAGE_FIELD_COUNT; ++j) {
			g_StageBindings[i].f[j].fileId = -1;
			g_StageBindings[i].f[j].owner = -1;
			g_StageBindings[i].f[j].rung = MODSTAGE_RUNG_DECLARED;
			g_StageBindings[i].f[j].declared = 0;
		}
	}
}

/**
 * A mod declared this stage.
 *
 * The count is the whole point: two mods declaring one stage both write
 * g_Stages today, in mod-dir order, with nothing said about it. Recording the
 * count makes that visible without changing who wins.
 *
 * Counted per MOD, not per call, because one mod's config is parsed three
 * times before the report runs: mainInit walks every mod dir (pdmain.c:308)
 * and then re-parses mod 0 (pdmain.c:314), and modCacheAllConfigs walks them
 * all again (mod.c:1807). A per-call counter would report all 17 of mod_fojo's
 * stages as contested by mod 0 with itself, which is the opposite of what this
 * record exists to say.
 */
/*
 * The mod directory's basename, for a log line a person has to act on.
 * "$B/mods/mod_fojo" reads as "mod_fojo"; an index with no directory reads as
 * its number, because that is still better than nothing.
 */
static const char *modDirName(s32 modnum, char *buf, size_t bufSize)
{
	if (modnum >= 0 && (u32)modnum < g_NumModDirs && modDirs[modnum][0]) {
		const char *slash = strrchr(modDirs[modnum], '/');

		return slash ? slash + 1 : modDirs[modnum];
	}

	snprintf(buf, bufSize, "mod %d", modnum);

	return buf;
}

void modStageBindingClaim(s32 stagenum, s32 modnum)
{
	struct modStageBinding *b;
	u64 bit;

	if (stagenum < 0 || stagenum >= (s32)ARRAYCOUNT(g_StageBindings)) {
		return;
	}

	b = &g_StageBindings[stagenum];
	bit = (modnum >= 0 && modnum < 64) ? ((u64)1 << modnum) : 0;

	if (bit && !(b->claimMask & bit)) {
		b->claimMask |= bit;

		if (b->claimCount < 0xff) {
			++b->claimCount;
		}

		if (b->claimCount > 1) {
			/* Now that a declaration names the stage, this is the only way two
			 * mods can land on one row, so it says which stage by name and which
			 * mods by directory rather than leaving a reader to look up two
			 * integers. It is LOG_WARNING and ungated on purpose: with names,
			 * reaching here means two mods asked for the same thing. */
			char mine[16];
			char theirs[16];
			const char *stagename = stageGetName(stagenum);

			sysLogPrintf(LOG_WARNING,
					"modstage: %s and %s both declare stage %s (0x%02x); %u claims, "
					"and the later one's fields win by parse order",
					modDirName(b->claimedBy, theirs, sizeof(theirs)),
					modDirName(modnum, mine, sizeof(mine)),
					stagename ? stagename : "with no stage table row",
					stagenum, b->claimCount);
		}
	}

	/* Matches what the engine does today: the last mod to parse wins, because
	 * every claim writes straight into the one global g_Stages row. */
	b->claimedBy = (s8)modnum;
}

void modStageBindingRecord(s32 stagenum, s32 modnum, enum modStageField field, s32 fileId)
{
	struct modStageFieldBinding *fb;

	if (stagenum < 0 || stagenum >= (s32)ARRAYCOUNT(g_StageBindings)) {
		return;
	}

	if (field < 0 || field >= MODSTAGE_FIELD_COUNT) {
		return;
	}

	fb = &g_StageBindings[stagenum].f[field];

	if (fb->declared && fb->owner != (s8)modnum) {
		sysLogPrintf(LOG_WARNING,
				"modstage: stage 0x%02x %s: mod %d overwrites mod %d's binding",
				stagenum, modStageFieldName(field), modnum, fb->owner);
	}

	fb->fileId = fileId;
	fb->owner = (s8)modnum;
	fb->rung = MODSTAGE_RUNG_DECLARED;
	fb->declared = 1;
}

/**
 * The record, once every modconfig has been parsed.
 *
 * This is the artefact Phase 0 exists to produce: one line per claimed stage
 * naming the owner of every field, so it can be read against what the game
 * actually loads. A field nobody named prints as `vanilla`, which is what the
 * engine does with it too - g_Stages keeps its ROM value.
 */
void modStageBindingReport(void)
{
	s32 stagenum;
	s32 claimed = 0;
	s32 contested = 0;

	for (stagenum = 0; stagenum < (s32)ARRAYCOUNT(g_StageBindings); ++stagenum) {
		const struct modStageBinding *b = &g_StageBindings[stagenum];
		char line[256];
		s32 len = 0;
		s32 field;

		if (!b->claimCount) {
			continue;
		}

		++claimed;

		if (b->claimCount > 1) {
			++contested;
		}

		for (field = 0; field < MODSTAGE_FIELD_COUNT; ++field) {
			const struct modStageFieldBinding *fb = &b->f[field];
			s32 n;

			if (!fb->declared) {
				continue;
			}

			n = snprintf(line + len, sizeof(line) - (size_t)len, "%s%s=mod%d",
					len ? " " : "", modStageFieldName((enum modStageField)field), fb->owner);

			if (n < 0 || (size_t)n >= sizeof(line) - (size_t)len) {
				break;
			}

			len += n;
		}

		sysLogPrintf(LOG_NOTE, "modstage: 0x%02x claimed by mod %d (%u claim%s): %s",
				stagenum, b->claimedBy, b->claimCount, b->claimCount == 1 ? "" : "s",
				len ? line : "no fields declared");
	}

	sysLogPrintf(LOG_NOTE, "modstage: %d stage%s claimed, %d contested by more than one mod",
			claimed, claimed == 1 ? "" : "s", contested);
}

/*
 * stage STAGE_NAME { ... }, or the older stage NUMBER { ... }.
 *
 * Identity is the name, the way it is for a head or a body. A declaration that
 * names an existing stage REPLACES that row - which is all any shipped stage
 * mod is doing - and two mods then collide only when they name the same stage,
 * which is a real conflict and is reported, instead of colliding on an integer
 * they both happened to pick out of a comment table.
 *
 * A name also settles the ambiguity that made inferring the claim from a mod's
 * files unusable: one bg seg is several vanilla rows (bg_arec.seg is 0x03,
 * 0x3d, 0x4f and 0x53), so inference claims STAGE_EXTRA* duplicates, while a
 * declaration naming STAGE_TEST_MP8 touches exactly the row it names.
 *
 * A name this build has no row for is NOT appended yet, and the block is
 * skipped with that said out loud. That used to be forced: the menu ids sat at
 * 0x5c..0x5e directly above the last level and g_ModStageNums was sized off
 * one of them, so there was nowhere to put a new row. The menus have since
 * moved to the top of the 7-bit save field and the levels have 0x02..0x7b, so
 * appending is now a matter of adding rows.
 *
 * The number keeps working, unchanged and unwarned. Nine modconfigs in the tree
 * declare stages by number, and it is the same resolution with the answer
 * written out by hand - not a legacy path with different semantics.
 */
static char *modConfigParseStage(char *p, char *token, s32 modnum)
{
	// stage NUMBER, or stage NAME / stage "NAME"
	p = strParseToken(p, token, NULL);

	char *spec = token;

	if (spec[0] == '"') {
		spec = strUnquote(spec);
	}

	// A STAGE_* name never starts with a digit and a number never starts with
	// a letter, so the two spellings cannot be confused for each other.
	s32 stagenum = -1;

	if (spec[0] >= '0' && spec[0] <= '9') {
		stagenum = strtol(spec, NULL, 0);
		// Bounded by STAGE_TITLE, not by ARRAYCOUNT(g_ModStageNums). The array
		// is the whole 7-bit space now, and the menu ids live at the top of it,
		// so an ARRAYCOUNT bound would let a modconfig claim the title screen
		// and own every menu model - which the old 0x5d bound also allowed,
		// since it admitted 0x5c exactly.
		if (stagenum <= 0x01 || stagenum >= STAGE_TITLE) {
			sysLogPrintf(LOG_ERROR, "modconfig: invalid stage number: %x", stagenum);
			return NULL;
		}

		// A literal is the modconfig's own choice; record it so the allocator
		// cannot later hand the same row to a name.
		modStageSlotClaim(spec, stagenum);
	} else {
		const s32 named = stageGetIndexByName(spec);

		if (named < 0) {
			// Not a STAGE_* row name, so treat it as the mod's own name for a
			// level and hand it a free STAGE_EXTRA row. The allocation is
			// recorded under that name in [MpStageSlots] in pd.ini, so the same
			// name gets the same row on every later boot even if the roster
			// changes - which is the whole point, and is how head and body
			// slots already behave.
			stagenum = modStageSlotReserve(spec);

			if (stagenum < 0) {
				sysLogPrintf(LOG_ERROR,
						"modconfig: '%s' is not a stage table row name and no free "
						"STAGE_EXTRA row is left to give it; skipping the block",
						spec);
			} else {
				sysLogPrintf(LOG_NOTE, "modconfig: '%s' allocated stage 0x%02x (%s)",
						spec, stagenum, stageGetName(stagenum));
			}
		} else if (g_Stages[named].id <= 0x01
				|| g_Stages[named].id >= STAGE_TITLE) {
			// No row is outside that range today - all 87 ids sit in 0x01..0x5b
			// and g_ModStageNums covers 0x00..0x5c - but g_ModStageNums is the
			// smaller table, and growing g_Stages past it must fail here rather
			// than become a write past the end of it.
			sysLogPrintf(LOG_ERROR,
					"modconfig: stage '%s' is 0x%02x, past the end of g_ModStageNums; skipping the block",
					spec, g_Stages[named].id);
		} else {
			stagenum = g_Stages[named].id;
			// Recorded, not allocated - same reason modSlotClaim exists for
			// heads: a name allocated later must not be handed this row.
			modStageSlotClaim(spec, stagenum);
		}
	}

	if (stagenum >= 0) {
		const char *stagename = stageGetName(stagenum);

		g_ModStageNums[stagenum] = modnum;
		sysLogPrintf(LOG_NOTE, "modconfig: mapped stage 0x%02x (%s) to mod %d",
				stagenum, stagename ? stagename : "no stage table row", modnum);
		modStageBindingClaim(stagenum, modnum);
	}

	// modConfigSkipBlock eats the opening bracket itself, so the skip arms below
	// have to be handed the file position from before it, the way the
	// HeadsAndBodies skip is. Handing it the position after meant it took the
	// block's first key for the bracket and returned NULL, which is not a skip -
	// it aborts the whole modconfig.
	char *blockStart = p;

	// eat opening bracket
	p = strParseToken(p, token, NULL);
	if (token[0] != '{' || token[1] != '\0') {
		return NULL;
	}

	if (stagenum < 0) {
		// An unresolved name. Said so above; skip the block and keep reading
		// the rest of the modconfig.
		return modConfigSkipBlock(blockStart, token);
	}

	// find the stage table pointers this corresponds to
	struct stagetableentry *stab = NULL;
	struct stageallocation *salloc = NULL;
	const s32 sidx = stageGetIndex(stagenum);
	if (sidx >= 0) {
		stab = &g_Stages[sidx];
	} else {
		sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: unknown stage number", stagenum);
		// Skip this block instead of failing completely
		return modConfigSkipBlock(blockStart, token);
	}
	for (struct stageallocation *p = g_StageAllocations8Mb; p->stagenum; ++p) {
		if (p->stagenum == stagenum) {
			salloc = p;
			break;
		}
	}

	// parse keyvalues until } is reached
	s32 tmp = 0;
	f32 tmpf = 0;
	char *tmps = NULL;
	// Held in a fixed buffer rather than a strDuplicate because every failure
	// arm below returns straight out of the function; strUnquote points into
	// `token`, which the next strParseToken overwrites.
	s32 kind = MODSTAGE_KIND_NONE;
	char arenaname[64];
	arenaname[0] = '\0';
	p = strParseToken(p, token, NULL);
	while (p && token[0] && strcmp(token, "}") != 0) {
		if (!strcmp(token, "bgfile")) {
			// bg FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "bgfile", tmp);
			SET_STAGE_FILEID(stab->bgfileid, "bgfile", tmp);
			modStageBindingRecord(stagenum, modnum, MODSTAGE_BG, tmp);
		} else if (!strcmp(token, "tilesfile")) {
			// tilesfile FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "tilesfile", tmp);
			SET_STAGE_FILEID(stab->tilefileid, "tilesfile", tmp);
			modStageBindingRecord(stagenum, modnum, MODSTAGE_TILES, tmp);
		} else if (!strcmp(token, "padsfile")) {
			// padsfile FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "padsfile", tmp);
			SET_STAGE_FILEID(stab->padsfileid, "padsfile", tmp);
			modStageBindingRecord(stagenum, modnum, MODSTAGE_PADS, tmp);
		} else if (!strcmp(token, "setupfile") || !strcmp(token, "setupFile")) {
			// setupfile FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "setupfile", tmp);
			SET_STAGE_FILEID(stab->setupfileid, "setupfile", tmp);
			modStageBindingRecord(stagenum, modnum, MODSTAGE_SETUP, tmp);
		} else if (!strcmp(token, "mpsetupfile")) {
			// mpsetupfile FILE_NAME_OR_NUM
			PARSE_STAGE_FILENAME("", "mpsetupfile", tmp);
			SET_STAGE_FILEID(stab->mpsetupfileid, "mpsetupfile", tmp);
			modStageBindingRecord(stagenum, modnum, MODSTAGE_MPSETUP, tmp);
		} else if (!strcmp(token, "alarm")) {
			PARSE_STAGE_INT("", "alarm", tmp, 1, 0xFFFF);
			stab->alarm = tmp;
		} else if (!strcmp(token, "extragunmem")) {
			PARSE_STAGE_INT("", "extragunmem", tmp, 0, 0xFFFF);
			stab->extragunmem = tmp;
		} else if (!strcmp(token, "gfxscale")) {
			// The row's world-to-graphics scale: g_Stages[].unk18. bg.c:1771
			// copies it into every player's scale_bg2gfx at stage load and
			// bgSetScaleBg2Gfx (bg.c:2150) re-derives from it on every zoom
			// step. mtx00016748 turns it into var8005ef10[0] = 65536 * scale,
			// the s15.16 factor mtxF2L applies to the whole world pass - rooms,
			// props and chrs - and playerAllocateMatrices (player.c:6008)
			// scales the camera by the same number, so the level's own
			// proportions do not change. What changes is the world's size
			// against everything drawn at a fixed scale, and how far the fixed
			// graphics-space frustum reaches in world units.
			//
			// Named gfxscale, not scale, because it is a rendering unit and
			// nothing in the simulation moves with it: collision, pads, AI and
			// weapon ranges are all world units. `scale` would read as "make
			// this level bigger", which this does not do - and the struct
			// region already holds two other per-stage floats that could be
			// called a scale (unk14, unk1c).
			//
			// A level is authored for one of these. Four vanilla rows carry
			// 0.5 - CRASHSITE, AIRBASE, VILLA, TEST_MP20 - and the other 83
			// carry 1, so a mod that moves one of those levels onto a free
			// STAGE_EXTRA* row silently inherits 1 and the world draws at
			// twice the graphics extent it was built for. The first-person
			// weapon, which bondgun.c:11710-11846 brackets with
			// mtx00016760/mtx00016784 to force scale 1, then reads half-size
			// against it, and the far plane and fog band reach half as far in
			// world terms because bg.c:6059 and env.c:231-232 divide by this
			// value to convert back. This key lets such a block state the
			// scale its level was authored for instead of taking the row's.
			//
			// Absolute, never accumulated: a modconfig is parsed several times
			// per boot and g_Stages is mutated in place with no vanilla
			// snapshot and no restore, so the write has to be idempotent. Same
			// shape as `alarm` above.
			//
			// Bounds. Those same three divisions are why zero is refused: it
			// is a float divide, so it yields inf/NaN for the far plane and
			// the fog band rather than trapping, and 0 in var8005ef10[0] also
			// collapses every converted matrix to zeros. A negative value
			// mirrors the world and inverts the far-plane test. The ceiling is
			// the fixed point itself - mtxF2L computes
			// (s32)(coord * 65536 * scale), so a coordinate is representable
			// only up to +-32768/scale; 4 leaves +-8192, and higher trades
			// away the headroom the 0.5 rows exist to buy. The floor is 50x
			// below the smallest value any vanilla row carries.
			//
			// A value outside the bounds is refused with a LOG_ERROR and
			// aborts this modconfig, exactly as `alarm` and `kind` do. It
			// never reaches the field.
			PARSE_STAGE_FLOAT("", "gfxscale", tmpf, 0.01f, 4.0f);
			stab->unk18 = tmpf;
		}  else if (!strcmp(token, "allocation")) {
			// allocation "ALLOCSTRING"
			PARSE_STAGE_STRING("", "allocation", tmps);

			// g_StageAllocations8Mb has no row for every stage, and the search
			// above leaves salloc NULL when there is none - so this wrote
			// through a null pointer for any such stage. The table is a
			// {stagenum, string} list terminated by a zero stagenum; a stage
			// missing from it falls off the end and takes the terminator's
			// string, which is why an added stage runs with the wrong pool.
			// That is its own thread; here, say so and do not crash.
			if (!salloc) {
				sysLogPrintf(LOG_ERROR,
						"modconfig: stage 0x%02x: allocation given, but that stage has no row "
						"in g_StageAllocations8Mb - ignored",
						stagenum);
			} else {
				// FIXME: this leaks
				tmps = strDuplicate(tmps);
				if (tmps) {
					salloc->string = tmps;
					modStageBindingRecord(stagenum, modnum, MODSTAGE_ALLOC, -1);
				}
			}
		}	else if (!strcmp(token, "music")) {
			// music { KEYVALUES... }
			p = modConfigParseStageMusic(p, token, stagenum);
			modStageBindingRecord(stagenum, modnum, MODSTAGE_MUSIC, -1);
			if (!p) {
				sysLogPrintf(LOG_NOTE, "modConfigParseStage: returning NULL (music parse failed for stage 0x%02x)", stagenum);
				return NULL;
			}
		} else if (!strcmp(token, "weather")) {
			// weather { KEYVALUES... }
			p = modConfigParseStageWeather(p, token, stagenum);
			modStageBindingRecord(stagenum, modnum, MODSTAGE_WEATHER, -1);
			if (!p) {
				sysLogPrintf(LOG_NOTE, "modConfigParseStage: returning NULL (weather parse failed for stage 0x%02x)", stagenum);
				return NULL;
			}
		} else if (!strcmp(token, "kind")) {
			// kind solo|mp|both|none - what the stage IS, not what this block
			// changes about it. Absent means none; see enum modStageKind.
			PARSE_STAGE_STRING("", "kind", tmps);
			if (!strcmp(tmps, "solo")) {
				kind = MODSTAGE_KIND_SOLO;
			} else if (!strcmp(tmps, "mp")) {
				kind = MODSTAGE_KIND_MP;
			} else if (!strcmp(tmps, "both")) {
				kind = MODSTAGE_KIND_BOTH;
			} else if (!strcmp(tmps, "none")) {
				kind = MODSTAGE_KIND_NONE;
			} else {
				sysLogPrintf(LOG_ERROR, "modconfig: stage 0x%02x: invalid kind: %s", stagenum, tmps);
				return NULL;
			}
		} else if (!strcmp(token, "arenaname")) {
			// Named for the field it reaches: struct mparena's customname,
			// which setup.c prefers over the langid at :367, :2831 and :2846,
			// so a mod names an arena with a plain string and ships no lang
			// files. It is not called stagename because nothing on the solo
			// side reads a display string - struct solostage has three langids
			// and no customname at all.
			PARSE_STAGE_STRING("", "arenaname", tmps);
			strncpy(arenaname, tmps, sizeof(arenaname) - 1);
			arenaname[sizeof(arenaname) - 1] = '\0';
		} else if (!strcmp(token, "use_mod_files")) {
			// Retired. Both this and force_vanilla were per-stage overrides of
			// g_NotLoadMod, the All-Solos-in-Multi switch fojo never sets. The
			// value is still parsed and validated, then discarded, so an older
			// modconfig does not hard-fail on an unknown key. Delete this arm
			// next release.
			PARSE_STAGE_INT("", "use_mod_files", tmp, 0, 1);
			sysLogPrintf(LOG_WARNING, "modconfig: stage 0x%02x: 'use_mod_files' is retired and ignored", stagenum);
		} else if (!strcmp(token, "force_vanilla")) {
			// Retired; see use_mod_files above.
			PARSE_STAGE_INT("", "force_vanilla", tmp, 0, 1);
			sysLogPrintf(LOG_WARNING, "modconfig: stage 0x%02x: 'force_vanilla' is retired and ignored", stagenum);
		} else {
			char where[32];
			snprintf(where, sizeof(where), "stage 0x%02x", stagenum);
			p = modConfigSkipUnknownKey(p, where, token);
		}
		p = strParseToken(p, token, NULL);
	}

	if (token[0] != '}') {
		sysLogPrintf(LOG_ERROR, "modconfig: unterminated stage 0x%02x block", stagenum);
		return NULL;
	}

	// Recorded only once the block has parsed cleanly, so a malformed block
	// cannot put a half-read stage in a menu. sidx >= 0 was checked above, so
	// everything reaching here has a g_Stages row and can actually load.
	modStageRegRecord(stagenum, modnum, kind, arenaname[0] ? arenaname : NULL, 0, 0);

	return p;
}

static s32 g_ModsScanned = 0;

/**
 * Report stage ownership as the engine currently sees it.
 *
 * Gated on PD_DEBUG_MODSTAGE=1; every record begins with MODSTAGE so it can be
 * filtered with grep '^MODSTAGE ' pd.log.
 *
 * This is the parity baseline for replacing the single-active-mod switch with
 * per-stage resolution: dump before the change, dump after, diff. It reads
 * state and emits records; it must not alter anything.
 */
void modStageDumpOwnership(const char *when)
{
	s32 stagenum;
	s32 stageindex;
	s32 claimed = 0;

	if (!g_DebugModStage) {
		return;
	}

	for (stagenum = 0; stagenum < (s32)ARRAYCOUNT(g_ModStageNums); stagenum++) {
		const s32 modnum = g_ModStageNums[stagenum];
		struct stagetableentry *entry;

		if (modnum < 0) {
			continue;
		}

		claimed++;
		stageindex = stageGetIndex(stagenum);
		entry = (stageindex >= 0) ? &g_Stages[stageindex] : NULL;

		MODSTAGE("claim when=%s stage=0x%02x mod=%d name='%s'",
				when, stagenum, modnum,
				(modnum >= 0 && modnum < 64) ? g_ModNames[modnum] : "?");

		if (entry) {
			// The five overloadable fields, as they stand in the stage table.
			// u32 since the widening, so print them wide enough to show an
			// owner if one ever gets applied - %04x hid exactly those bits.
			MODSTAGE("fields stage=0x%02x setup=0x%08x mpsetup=0x%08x bg=0x%08x tiles=0x%08x pads=0x%08x",
					stagenum, entry->setupfileid, entry->mpsetupfileid,
					entry->bgfileid, entry->tilefileid, entry->padsfileid);
		}
	}

	MODSTAGE("summary when=%s claimed=%d activemod=%d", when, claimed, g_ModNum);
}

void modScanAllMods(void)
{
	if (g_ModsScanned) return;

	for (s32 i = 0; i < g_NumModDirs; ++i) {
		char path[FS_MAXPATH];
		snprintf(path, sizeof(path), "%s/modconfig.txt", modDirs[i]);

		u32 len = 0;
		char *data = fsFileLoad(path, &len);
		if (!data) continue;

		char token[UTIL_MAX_TOKEN + 1];
		char *p = strParseToken(data, token, NULL);
		while (p && token[0]) {
			if (!strcmp(token, "modname")) {
				p = strParseToken(p, token, NULL);
				if (token[0]) {
					strncpy(g_ModNames[i], strUnquote(token), sizeof(g_ModNames[i]) - 1);
				}
			} else if (!strcmp(token, "modversion")) {
				p = strParseToken(p, token, NULL);
				if (token[0]) {
					strncpy(g_ModVersions[i], strUnquote(token), sizeof(g_ModVersions[i]) - 1);
				}
			}
			p = strParseToken(p, token, NULL);
		}
		sysMemFree(data);
	}
	g_ModsScanned = 1;
}


s32 modLoadAIO(void)
{
	s32 loaded = 0;
	modScanAllMods();

	for (s32 i = 0; i < g_NumModDirs; ++i) {
		char path[FS_MAXPATH];
		snprintf(path, sizeof(path), "%s/modconfig.txt", modDirs[i]);

		u32 len = 0;
		char *data = fsFileLoad(path, &len);
		if (!data) continue;

		char token[UTIL_MAX_TOKEN + 1];
		char *p = strParseToken(data, token, NULL);

		while (p && token[0]) {
			if (!strcmp(token, "MpArena")) {
				// No longer skipped: it feeds the registry, which is keyed by
				// stagenum, so this second pass over the same file re-records
				// rather than duplicating.
				p = modConfigParseMpArena(p, token);
				if (!p) {
					sysLogPrintf(LOG_ERROR, "modLoadAIO: malformed MpArena block");
					break;
				}
				continue;
			} else if (!strcmp(token, "MpArenaGroup")) {
				// Retired; see modConfigLoad.
				p = modConfigSkipBlock(p, token);
				continue;
			} else if (!strcmp(token, "MpHeads")) {
				p = modConfigParseMpHeads(p, token);
				continue;
			} else if (!strcmp(token, "MpBodies")) {
				p = modConfigParseMpBodies(p, token);
				continue;
			} else if (!strcmp(token, "HeadsAndBodies")) {
				p = modConfigParseHeadsAndBodies(p, token, i);
				continue;
			} else if (!strcmp(token, "stage")) {
				// Skip stage number, then skip the block
				p = strParseToken(p, token, NULL); // skip stage number
				p = modConfigSkipBlock(p, token);
				continue;
			}
			else if (!strcmp(token, "texture")) {
				p = modConfigSkipBlock(p, token);
				continue;
			}

			p = strParseToken(p, token, NULL);
		}


		sysMemFree(data);
	}

	if (!loaded) {
		sysLogPrintf(LOG_ERROR, "modLoadAIO: failed to find AIO mod in any of %d dirs", g_NumModDirs);
	}

	return loaded;
}

void modInit(void)
{
	if (getenv("PD_DEBUG_MODSTAGE")) {
		g_DebugModStage = true;
	}

	// Before any modconfig is read, so the first HeadsAndBodies block already
	// sees what this pd.ini has allocated.
	modSlotReservationsInit();

	// Reset mod stage mapping
	for (s32 i = 0; i < STAGE_4MBMENU; i++) {
		g_ModStageNums[i] = -1;
	}

	modStageBindingsReset();
}

// Cache all mod configs at boot (parse once, then just copy on modSwitch)
void modCacheAllConfigs(void)
{
	if (g_ModConfigsCached) {
		return;
	}

	sysLogPrintf(LOG_NOTE, "modCacheAllConfigs: Caching configs for %d mods", g_NumModDirs);

	// First, backup the original vanilla states
	memcpy(g_ModelStatesOriginal, g_ModelStates, sizeof(g_ModelStates));
	memcpy(g_PropExplosionTypesOriginal, g_PropExplosionTypes, NUM_MODELS);
	sysLogPrintf(LOG_NOTE, "modCacheAllConfigs: Backed up vanilla states");

	// For each mod, load its config and cache the results
	for (u32 i = 0; i < g_NumModDirs; i++) {
		// Start with vanilla defaults for this mod
		memcpy(g_ModelStates_PerMod[i], g_ModelStatesOriginal, sizeof(g_ModelStates));
		memcpy(g_ExplosionTypes_PerMod[i], g_PropExplosionTypesOriginal, NUM_MODELS);

		// Temporarily set g_ModNum to this mod for config parsing
		s32 oldModNum = g_ModNum;
		g_ModNum = i;

		// Load the modconfig (which will modify g_ModelStates and g_PropExplosionTypes)
		modConfigLoad(MOD_CONFIG_FNAME);

		// Cache the modified states for this mod
		memcpy(g_ModelStates_PerMod[i], g_ModelStates, sizeof(g_ModelStates));
		memcpy(g_ExplosionTypes_PerMod[i], g_PropExplosionTypes, NUM_MODELS);

		sysLogPrintf(LOG_NOTE, "modCacheAllConfigs: Cached config for mod %d (%s)", i, g_ModNames[i][0] ? g_ModNames[i] : "unnamed");

		// Restore g_ModNum
		g_ModNum = oldModNum;
	}

	g_ModConfigsCached = true;
	sysLogPrintf(LOG_NOTE, "modCacheAllConfigs: All configs cached");

	// Every claim has been seen by now, so the record is complete. Reporting
	// rather than acting on it is the whole of phase 0.
	modStageBindingReport();
}

s32 modConfigLoad(const char *fname)
{
	u32 dataLen = 0;

	const char *activeModDir = (g_ModNum >= 0 && g_ModNum < (s32)g_NumModDirs) ? modDirs[g_ModNum] : "(none)";
	const char *activeModName = (g_ModNum >= 0 && g_ModNum < 64 && g_ModNames[g_ModNum][0]) ? g_ModNames[g_ModNum] : "(unnamed)";

	/*
	 * Build the path from the active mod's directory rather than handing the
	 * bare filename to fsFullPath, which is what modLoadAIO has always done
	 * and is the only route that works.
	 *
	 * fsFullPath's mod-dir probe cannot resolve this - or anything else. It
	 * calls fsModFullPath(pathBuf, relPath) with its OWN static pathBuf, the
	 * probe writes "<modDir>/<relPath>" into that buffer, and then asks
	 * fsFileSize whether it exists. fsFileSize calls fsFullPath again, on the
	 * very buffer fsFullPath is in the middle of filling. Every modDirs[]
	 * entry is stored in "$B/mods/<name>" placeholder form, so that re-entry
	 * takes the '$' branch and does
	 *
	 *     memcpy(pathBuf, baseDir, strlen(baseDir));
	 *     strncpy(pathBuf + len, relPath + 2, ...);   // relPath IS pathBuf
	 *
	 * - the memcpy overwrites the candidate before the strncpy reads the tail
	 * it was supposed to append, so the stat runs against baseDir with a slice
	 * of baseDir glued onto it. It never matches, so the probe reports "not in
	 * any mod dir" for every file, and fsFullPath falls back to
	 * "<baseDir>/modconfig.txt", which does not exist. That is the 13 fsFileLoad
	 * errors a boot log shows, one per modConfigLoad call.
	 *
	 * So this function has never parsed anything. modConfigLoad is the only
	 * caller of modConfigParseStage, and modLoadAIO - the route that does open
	 * these files - deliberately skips stage blocks, which is why heads and
	 * bodies work and no stage has ever been claimed.
	 *
	 * Fixed here rather than in fsModFullPathCheck. The aliasing is real and
	 * the helper is wrong, but it sits on every asset lookup in the game and
	 * has resolved nothing since it was written ("found in modDir" appears
	 * zero times in every log on disk), so repairing it turns on mod-dir
	 * resolution for textures, files/ and sequences/ all at once. That is a
	 * separate change with its own blast radius; it should not ride along with
	 * a stage-parsing fix.
	 *
	 * The filename stays a parameter because every caller passes
	 * MOD_CONFIG_FNAME and the log lines quote it, but the directory is not
	 * the caller's to choose: g_ModNum is what the rest of this function
	 * parses as (`modnum` below), so resolving the path against anything else
	 * would let the file and the mod it is attributed to disagree. All four
	 * callers set g_ModNum immediately before calling for exactly that reason.
	 */
	char path[FS_MAXPATH];
	if (g_ModNum >= 0 && g_ModNum < (s32)g_NumModDirs && modDirs[g_ModNum][0]) {
		snprintf(path, sizeof(path), "%s/%s", modDirs[g_ModNum], fname);
	} else {
		snprintf(path, sizeof(path), "%s", fname);
	}

	char *data = fsFileLoad(path, &dataLen);
	if (!data) {
		sysLogPrintf(LOG_NOTE,
				"modconfig: no config at '%s' (g_ModNum=%d '%s' modDir='%s')",
				path, g_ModNum, activeModName, activeModDir);
		return false;
	}

	sysLogPrintf(LOG_NOTE, "modconfig: loaded '%s' (%u bytes) for mod %d '%s' from '%s'",
			fname, dataLen, g_ModNum, activeModName, path);

	s32 modnum = g_ModNum;

	// Restore original model states before applying mod overrides
	if (!g_MainIsBooting) {
		sysLogPrintf(LOG_NOTE, "modconfig: Restoring original ModelStates and ExplosionTypes before loading mod %d", modnum);
		memcpy(g_ModelStates, g_ModelStatesOriginal, sizeof(g_ModelStates));
		memcpy(g_PropExplosionTypes, g_PropExplosionTypesOriginal, sizeof(g_PropExplosionTypesOriginal));
	}

	s32 success = true;
	modConfigUnknownKeysReset();
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char *end = data + dataLen;
	char *p = strParseToken(data, token, NULL);
	while (p && token[0]) {
		if (!strcmp(token, "modname")) {
			p = strParseToken(p, token, NULL);
			if (token[0]) {
				char *name = strUnquote(token);
				if (modnum >= 0 && modnum < 64) {
					strncpy(g_ModNames[modnum], name, sizeof(g_ModNames[modnum]) - 1);
				}
			}
		} else if (!strcmp(token, "modversion")) {
			p = strParseToken(p, token, NULL);
			if (token[0]) {
				char *ver = strUnquote(token);
				if (modnum >= 0 && modnum < 64) {
					strncpy(g_ModVersions[modnum], ver, sizeof(g_ModVersions[modnum]) - 1);
				}
			}
		} else if (!strcmp(token, "stage")) {
			// stage NUMBER { KEYVALUES... }
			char *prev = p;
			p = modConfigParseStage(p, token, modnum);
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed stage block at offset %d", prev - data);
				sysLogPrintf(LOG_ERROR, "modconfig: stage block skipped: %s", token);
				success = false;
				break;
			}

		} else if (!strcmp(token, "texture")) {
			// process texture surcfacetype and soundsurfacetype

				sysLogPrintf(LOG_ERROR, "modconfig: processing texture block at offset %d", p - data);
				sysLogPrintf(LOG_ERROR, "modconfig: texture block: %s", token);
				char *prev = p;

				p = modConfigParseTexture(p, token, modnum);
				if (!p) {
					sysLogPrintf(LOG_ERROR, "modconfig: malformed texture block at offset %d", prev - data);
					sysLogPrintf(LOG_ERROR, "modconfig: texture block skipped: %s", token);
					success = false;
					break;
				}

		} else if (!strcmp(token, "HeadsAndBodies")) {
			if (!g_MainIsBooting) {
				p = modConfigParseHeadsAndBodies(p, token, modnum);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed HeadsAndBodies block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "MpHeads")) {
			if (!g_MainIsBooting) {
				p = modConfigParseMpHeads(p, token);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed MpHeads block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "MpBodies")) {
			if (!g_MainIsBooting) {
				p = modConfigParseMpBodies(p, token);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed MpBodies block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "MpArena")) {
			// Parsed at boot too, unlike the blocks above it. Those edit the
			// mplayer arrays, which are not up yet while g_MainIsBooting;
			// this one only writes the registry, which is plain malloc, and
			// the arena list is not built until pdmain calls mpArenasRebuild
			// after every modconfig has been read.
			p = modConfigParseMpArena(p, token);
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed MpArena block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "MpArenaGroup")) {
			// Retired. Its parser was defined and called from nowhere, so no
			// config could ever have used it, and the arena groups are now
			// derived from the merged list in mpArenasRebuild. Skipped rather
			// than rejected so a config carrying one does not hard-fail.
			sysLogPrintf(LOG_WARNING, "modconfig: 'MpArenaGroup' is retired and ignored");
			p = modConfigSkipBlock(p, token);
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed MpArenaGroup block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "ModelStates")) {
			if (!g_MainIsBooting) {
				p = modConfigParseModelStates(p, token, modnum);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed ModelStates block");
				success = false;
				break;
			}
		} else if (!strcmp(token, "ExplosionTypes")) {
			if (!g_MainIsBooting) {
				p = modConfigParseExplosionTypes(p, token, modnum);
			} else {
				p = modConfigSkipBlock(p, token);
			}
			if (!p) {
				sysLogPrintf(LOG_ERROR, "modconfig: malformed ExplosionTypes block");
				success = false;
				break;
			}
		} else {
			// An unknown key at the top level, which is the same version-skew
			// story as one inside a block and can be a whole section a newer
			// loader writes - so the skip peeks for a brace before deciding.
			// It also catches actual garbage, which the grammar cannot tell
			// apart from a key it has not heard of; the loop advances a token
			// either way, so junk costs a warning rather than the file.
			char where[80];
			snprintf(where, sizeof(where), "%s: offset %d", fname, (s32)(p - data));
			p = modConfigSkipUnknownKey(p, where, token[0] ? token : "end of file");
		}
		p = strParseToken(p, token, NULL);
	}

	// Named again rather than reusing activeModName from the top: a mod's
	// own `modname` key is read by the loop above, so on the first pass the
	// name only exists once the file has been parsed.
	modConfigUnknownKeysReport(fname,
			(modnum >= 0 && modnum < 64 && g_ModNames[modnum][0]) ? g_ModNames[modnum] : activeModName);

	sysMemFree(data);
	return success;
}

// Derive a short texture-name prefix from the mod directory name.
// E.g. "$H/mods/mod_gex_characters" -> "gex". Returns NULL if not derivable.
static const char *modGetTexPrefix(s32 modNum, char *buf, size_t bufSize)
{
	if (modNum < 0 || (u32)modNum >= g_NumModDirs || !modDirs[modNum][0]) {
		return NULL;
	}
	const char *slash = strrchr(modDirs[modNum], '/');
	const char *base = slash ? slash + 1 : modDirs[modNum];
	if (strncmp(base, "mod_", 4) != 0) {
		return NULL;
	}
	base += 4;
	size_t i = 0;
	while (base[i] && base[i] != '_' && i + 1 < bufSize) {
		buf[i] = base[i];
		++i;
	}
	if (i == 0) {
		return NULL;
	}
	buf[i] = '\0';
	return buf;
}

static s32 modTextureResolveTry(s32 modNum, const char *candidate,
		struct modTextureResolveInfo *info)
{
	const s32 fileNum = romdataFileGetNumForNameInMod(candidate, modNum);
	if (info && info->attemptCount < MOD_TEXTURE_RESOLVE_MAX_ATTEMPTS) {
		struct modTextureResolveAttempt *attempt = &info->attempts[info->attemptCount++];
		snprintf(attempt->name, sizeof(attempt->name), "%s", candidate);
		attempt->fileNum = fileNum;
	}
	return fileNum;
}

s32 modTextureResolveFileDetailed(s32 modNum, s32 modelFileNum, u16 textureId,
		struct modTextureResolveInfo *info)
{
	char candidate[128];
	char prefixBuf[32];
	const char *prefix;
	u16 localId = textureId;
	s32 fileNum = 0;

	if (info) {
		memset(info, 0, sizeof(*info));
		info->modNum = modNum;
		info->modelFileNum = modelFileNum;
		info->requestedId = textureId;
		info->reverseLocalId = 0xffff;
		info->resolvedLocalId = textureId;
		info->matchedAttempt = -1;
	}
	if (modNum < 0 || (u32)modNum >= g_NumModDirs) return 0;

	if (textureId >= NUM_TEXTURES) {
		u16 reverseId = modTexMapReverseLookup(modNum, textureId);
		if (info) info->reverseLocalId = reverseId;
		if (reverseId != 0xffff) localId = reverseId;
	}

	if (modelFileNum > 0) {
		const char *modelName = romdataFileGetSlotName(modNum, modelFileNum);
		if (modelName) {
			const char *nameStart = strstr(modelName, "::");
			nameStart = nameStart ? nameStart + 2 : modelName;
			if (strncmp(nameStart, "files/", 6) == 0) nameStart += 6;
			snprintf(candidate, sizeof(candidate), "%s/%04x.bin", nameStart, localId);
			fileNum = modTextureResolveTry(modNum, candidate, info);
		}
	}

	prefix = modGetTexPrefix(modNum, prefixBuf, sizeof(prefixBuf));
	if (fileNum <= 0) {
		localId = textureId;
		snprintf(candidate, sizeof(candidate), "%04x.bin", textureId);
		fileNum = modTextureResolveTry(modNum, candidate, info);
	}
	if (fileNum <= 0 && prefix) {
		snprintf(candidate, sizeof(candidate), "%s_%04x.bin", prefix, textureId);
		fileNum = modTextureResolveTry(modNum, candidate, info);
	}
	if (fileNum <= 0 && textureId >= NUM_TEXTURES) {
		u16 reverseId = modTexMapReverseLookup(modNum, textureId);
		if (reverseId != 0xffff && reverseId != textureId) {
			localId = reverseId;
			snprintf(candidate, sizeof(candidate), "%04x.bin", reverseId);
			fileNum = modTextureResolveTry(modNum, candidate, info);
			if (fileNum <= 0 && prefix) {
				snprintf(candidate, sizeof(candidate), "%s_%04x.bin", prefix, reverseId);
				fileNum = modTextureResolveTry(modNum, candidate, info);
			}
		}
	}

	if (fileNum > 0) {
		if (info) {
			info->resolvedLocalId = localId;
			info->fileNum = fileNum;
			info->matchedAttempt = info->attemptCount - 1;
		}
	}
	return fileNum;
}

s32 modTextureResolveFile(s32 modNum, s32 modelFileNum, u16 textureId,
		u16 *resolvedLocalId, char *resolvedName, u32 resolvedNameSize)
{
	struct modTextureResolveInfo info;
	const s32 fileNum = modTextureResolveFileDetailed(modNum, modelFileNum, textureId, &info);
	if (resolvedLocalId) *resolvedLocalId = info.resolvedLocalId;
	if (resolvedName && resolvedNameSize > 0) {
		resolvedName[0] = '\0';
		if (info.matchedAttempt >= 0) {
			snprintf(resolvedName, resolvedNameSize, "%s", info.attempts[info.matchedAttempt].name);
		}
	}
	return fileNum;
}

s32 modTextureLoad(u16 num, void *dst, u32 dstSize)
{
	// Only attempt mod texture loading when we have an explicit model-level mod context
	// (g_TexModNum is set by modeldef during a mod-owned model's load/process). Without
	// this gate, vanilla model loads would probe mod filetables and accidentally pick up
	// mod overrides for unrelated texture IDs.
	if (g_TexModNum < 0) {
		// Before the owner tag this was unreachable for a vanilla model load:
		// MOD_FILEID_MOD of a plain id was 0, so vanilla arrived here claiming
		// to be mod 0 and pulled mod 0's overrides. Anything that was relying
		// on that now stops getting them, with nothing to see. Count the skips
		// and name the first few distinct texture numbers they happen to, so a
		// diagnostic boot can say how much content was leaning on the merge.
		{
			static u32 skips = 0;
			static u32 named = 0;
			static u8 seen[512]; // one bit per texture number below 4096
			skips++;
			if (num < 4096) {
				const u32 byte = num >> 3;
				const u8 bit = (u8)(1u << (num & 7));
				if ((seen[byte] & bit) == 0) {
					seen[byte] |= bit;
					if (named < 32) {
						named++;
						sysLogPrintf(LOG_NOTE,
								"modTextureLoad: no mod context, not probing mod filetables for "
								"tex 0x%04x (skip %u, distinct %u)", num, skips, named);
					}
				}
			}
		}
		return 0;
	}

	s32 modNum = g_TexModNum;
	u16 lookup = num;
	const char *modelNameForLog = NULL;

	// If we know which model is loading (set by modeldef), try a per-model
	// dir first: `<ModelName>/<local-texid>.bin`. This mirrors how PNG
	// overrides live under `ext_tex/<ModelName>/<texid>.png` and prevents
	// two models in the same mod that reference the same source texid from
	// pulling each other's bytes.
	char name[128] = { 0 };
	if (g_TexCurrentModelFileNum > 0) {
		const char *modelName = romdataFileGetSlotName(modNum, g_TexCurrentModelFileNum);
		if (modelName) {
			// Strip any leading "mod:...::" prefix produced by merge-filetables.
			const char *nameStart = strstr(modelName, "::");
			nameStart = nameStart ? nameStart + 2 : modelName;
			// Also strip a leading files/ directory if present.
			if (strncmp(nameStart, "files/", 6) == 0) nameStart += 6;
			modelNameForLog = nameStart;
		}
	}
	s32 fileNum = modTextureResolveFile(modNum, g_TexCurrentModelFileNum, num,
		&lookup, name, sizeof(name));

	if (modNum == 2 && (g_TexCurrentModelFileNum == 2025 || g_TexCurrentModelFileNum == 2028)) {
		static u8 s_seen[2][512];
		u32 slot = (g_TexCurrentModelFileNum == 2025) ? 0 : 1;
		if (num < 4096) {
			u32 byte = num >> 3;
			u32 bit = 1u << (num & 7);
			if ((s_seen[slot][byte] & bit) == 0) {
				s_seen[slot][byte] |= bit;
				sysLogPrintf(LOG_NOTE,
					"AIO texture trace: modelFile=%d model='%s' port=0x%04x local=0x%04x fileNum=%d query='%s'",
					g_TexCurrentModelFileNum,
					modelNameForLog ? modelNameForLog : "(none)",
					num,
					lookup,
					fileNum,
					name[0] ? name : "(none)");
			}
		}
	}

	// DIAG: probe trace (disabled — re-enable to see which tex IDs are missed)
	{
		static u8 s_modTexSeen[64][512]; // 64 mods * 4096 tex / 8
		if ((u32)modNum < 64 && num < 4096) {
			u32 byte = num >> 3;
			u32 bit = 1u << (num & 7);
			if ((s_modTexSeen[modNum][byte] & bit) == 0) {
				s_modTexSeen[modNum][byte] |= bit;
				/* sysLogPrintf(LOG_NOTE, "modTextureLoad PROBE: tex=0x%04x modNum=%d fileNum=0x%x", num, modNum, fileNum); */
			}
		}
	}

	if (fileNum > 0) {
		DEBUG_MODELS("modTextureLoad: checking texture %04x (file %d) in mod %d", num, fileNum, modNum);
		u32 size = 0;
		s32 encodedFileNum = MOD_FILEID_MAKE(modNum, fileNum);
		u8 *data = romdataFileLoad(encodedFileNum, &size);


		if (data) {
			// If the data is pointing to the ROM, we can let the game's default DMA handler
			// take care of it (return 0). This avoids unnecessary memcpy and keeps vanilla behavior.
			if (data >= g_RomFile && data < g_RomFile + g_RomFileSize) {
				if (num >= 0x1010 && num <= 0x1023) {
					/* sysLogPrintf(LOG_NOTE, "modTextureLoad PORTRANGE: tex=0x%04x ROM-pointer, returning 0 (DMA fallback)", num); */
				}
				return 0;
			}

			if (num >= 0x1010 && num <= 0x1023) {
				/* sysLogPrintf(LOG_NOTE, "modTextureLoad PORTRANGE: tex=0x%04x size=%u dstSize=%u data=%p", num, size, dstSize, data); */
			}

			// It's external (or alt-rom) data
			if (size <= dstSize) {
				memcpy(dst, data, size);
				romdataFileFree(encodedFileNum);
				return size;
			} else {
				sysLogPrintf(LOG_ERROR, "mod: texture %04x (file %d) too large for buffer (%d > %d)", num, fileNum, size, dstSize);
				romdataFileFree(encodedFileNum);
				return 0;
			}
		} else {
			if (num >= 0x1010 && num <= 0x1023) {
				/* sysLogPrintf(LOG_NOTE, "modTextureLoad PORTRANGE: tex=0x%04x fileNum=0x%x romdataFileLoad returned NULL", num, fileNum); */
			}
			return 0;
		}
	}

	if (num >= 0x1010 && num <= 0x1023) {
		/* sysLogPrintf(LOG_NOTE, "modTextureLoad PORTRANGE MISS: tex=0x%04x no fileNum found", num); */
	}

	// Fallback to a loose file on disk under the active mod's textures dir.
	// Scoped to g_TexModNum so we don't pull from unrelated mods.
	if ((u32)modNum < g_NumModDirs && modDirs[modNum][0]) {
		char path[FS_MAXPATH + 1];
		snprintf(path, sizeof(path), "%s/" MOD_TEXTURES_DIR "/%04x.bin", modDirs[modNum], num);
		const s32 ret = fsFileLoadTo(path, dst, dstSize);
		if (ret > 0) {
			sysLogPrintf(LOG_NOTE, "mod: loaded external texture %04x from mod %d", num, modNum);
			return ret;
		}
	}

	return 0;
}void *modSequenceLoad(u16 num, u32 *outSize)
{
	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_SEQUENCES_DIR "/") >= 0);
	}

	if (!dirExists) {
		return NULL;
	}

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_SEQUENCES_DIR "/%04x.bin", num);
	if (fsFileSize(path) > 0) {
		void *ret = fsFileLoad(path, outSize);
		if (ret) {
			sysLogPrintf(LOG_NOTE, "mod: loaded external sequence %04x", num);
			return ret;
		}
	}

	return NULL;
}

void *modAnimationLoadData(u16 num)
{
	char path[FS_MAXPATH + 1];
	// load the animation data
	snprintf(path, sizeof(path), MOD_ANIMATIONS_DIR "/%04x.bin", num);
	void *data = fsFileLoad(path, NULL);
	if (!data) {
		sysFatalError("External animation %04x has no data file.\nEnsure that it is placed at %s or delete the descriptor.", num, path);
	}
	return data;
}

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim)
{
	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_ANIMATIONS_DIR "/") >= 0);
	}

	if (!dirExists) {
		return false;
	}

	char path[FS_MAXPATH + 1];

	// load the descriptor, if any
	snprintf(path, sizeof(path), MOD_ANIMATIONS_DIR "/%04x.txt", num);
	if (fsFileSize(path) <= 0) {
		return false;
	}

	char *desc = fsFileLoad(path, NULL);
	if (!desc) {
		return false;
	}

	// parse the descriptor
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char *p = strParseToken(desc, token, NULL);
	s32 tmp = 0;
	while (p && token[0]) {
		if (!strcmp(token, "numframes")) {
			PARSE_INT(path, "numframes", tmp, 0, 0xFFFF, false);
			anim->numframes = tmp;
		} else if (!strcmp(token, "bytesperframe")) {
			PARSE_INT(path, "bytesperframe", tmp, 0, 0xFFFF, false);
			anim->bytesperframe = tmp;
		} else if (!strcmp(token, "headerlen")) {
			PARSE_INT(path, "headerlen", tmp, 0, 0xFFFF, false);
			anim->headerlen = tmp;
		} else if (!strcmp(token, "framelen")) {
			PARSE_INT(path, "framelen", tmp, 0, 0xFF, false);
			anim->framelen = tmp;
		} else if (!strcmp(token, "flags")) {
			PARSE_INT(path, "flags", tmp, 0, 0xFF, false);
			anim->flags = tmp;
		} else {
			sysLogPrintf(LOG_ERROR, "mod: %s: invalid key: %s", path, token);
			return false;
		}
		p = strParseToken(p, token, NULL);
	}

	sysMemFree(desc);

	sysLogPrintf(LOG_NOTE, "mod: loaded external animation %04x", num);

	return true;
}

// mplayer
void modUnloadTextureSurfaceType(void) {
	for (s32 i = 0; i < NUM_TEXTURES; i++) {
		g_Textures[i].surfacetype = g_VanillaTextures[i].surfacetype;
		g_Textures[i].soundsurfacetype = g_VanillaTextures[i].soundsurfacetype;
	}
}


s32 modNumFromStage(s32 stagenum) {
	s32 modnum = -1;
	// this needs to be initialized at boot
	// by attempting to load all mods
	// and discovering which stages are used
	if (stagenum >= 0 && stagenum < ARRAYCOUNT(g_ModStageNums)) {
		if (g_ModStageNums[stagenum] > -1) {
			modnum = g_ModStageNums[stagenum];
		}
	}

	sysLogPrintf(LOG_NOTE, "modNumFromStage: stage 0x%02x -> mod %d", stagenum, modnum);
	return modnum;
}


void modSwitch(s32 modnum, s32 stagenum) {
	sysLogPrintf(LOG_NOTE, "modSwitch(mod=%d, stage=0x%02x) called. Current g_ModNum=%d", modnum, stagenum, g_ModNum);

	// Fix for race condition where menu logic resets mod during stage transition
	if (stagenum == -1 && g_MainChangeToStageNum >= 0) {
		s32 pendingMod = modNumFromStage(g_MainChangeToStageNum);
		if (pendingMod > -1) {
			modnum = pendingMod;
			stagenum = g_MainChangeToStageNum;
			sysLogPrintf(LOG_NOTE, "modSwitch: overriding mod switch to %d for pending stage 0x%02x", modnum, stagenum);
		}
	}

	// this essentially reloads you back to the boot mod
	modUnloadTextureSurfaceType();
	// NOTE: Do NOT reset multiplayer arrays (heads/bodies) on modSwitch!
	// These are now global/exported across all mods and should only be reset at game startup.

	// Initialize backup of original model states on first run
	static bool modelStatesBackedUp = false;
	if (!modelStatesBackedUp) {
		DEBUG_MODELS("modSwitch: Creating backup of original ModelStates and ExplosionTypes");
		memcpy(g_ModelStatesOriginal, g_ModelStates, sizeof(g_ModelStates));
		memcpy(g_PropExplosionTypesOriginal, g_PropExplosionTypes, NUM_MODELS);
		modelStatesBackedUp = true;
		DEBUG_MODELS("modSwitch: Backup complete - sample model 0x0001 scale: 0x%04x, explosion type: %d",
			g_ModelStatesOriginal[0x0001].scale, g_PropExplosionTypesOriginal[0x0001]);
	}

	if (modNumFromStage(stagenum) > -1 && modnum < 0) {
		g_ModNum = modNumFromStage(stagenum);
		sysLogPrintf(LOG_NOTE, "modSwitch: switching to mod %d for stage 0x%02x", g_ModNum, stagenum);
	} else {
		if (modnum >= 0) {
			sysLogPrintf(LOG_NOTE, "modSwitch: manual switch to mod %d (stage 0x%02x)", modnum, stagenum);
		}
		g_ModNum = modnum;
	}

	// Safety check: if g_ModNum is invalid (e.g. -1), keep current mod or default to 0
	if (g_ModNum < 0) {
		// If we already have a valid mod loaded, keep it (don't switch away when loading menu stages)
		static s32 lastValidMod = 0;
		if (lastValidMod > 0) {
			sysLogPrintf(LOG_NOTE, "modSwitch: keeping current mod %d for stage 0x%02x (no mod assignment)", lastValidMod, stagenum);
			g_ModNum = lastValidMod;
		} else {
			sysLogPrintf(LOG_NOTE, "modSwitch: defaulting to mod 0 (boot) for stage 0x%02x", stagenum);
			g_ModNum = 0;
		}
	}

	// Track the last valid mod we switched to (for persistence)
	static s32 lastValidMod = 0;
	if (g_ModNum >= 0) {
		lastValidMod = g_ModNum;
	}

	romdataResetMod(g_ModNum);

	sysLogPrintf(LOG_NOTE, "g_ModNum: %d", g_ModNum);
	modStageDumpOwnership("switch");

	// Use cached config data instead of re-parsing (fast, atomic, no overwrites)
	if (g_ModConfigsCached && g_ModNum >= 0 && g_ModNum < 64) {
		sysLogPrintf(LOG_NOTE, "modSwitch: Loading cached config for mod %d", g_ModNum);
		memcpy(g_ModelStates, g_ModelStates_PerMod[g_ModNum], sizeof(g_ModelStates));
		memcpy(g_PropExplosionTypes, g_ExplosionTypes_PerMod[g_ModNum], NUM_MODELS);
		DEBUG_MODELS("modSwitch: Applied cached config (model 0x0020 scale: 0x%04x)", g_ModelStates[0x0020].scale);
	} else {
		// Fallback: parse config on-the-fly (only during boot before cache is ready)
		modConfigLoad(MOD_CONFIG_FNAME);
	}

	// Only enable AIO arena mode if AIO is present AND we are in the boot mod (menus)
	// or if the current mod IS the AIO mod.

  bodiesInit();          // recount guard-head arrays now mods are loaded
  fojoPatchGuardHeads(); // splice FoJo Calico/Poplin into female guard pool
  fojoInitChrBioCharacters(); // resolve FoJo bio chr ids to runtime mpheadnums
	// Load AIO assets (heads, bodies, character models)
	modLoadAIO();

	// The fallback modConfigLoad above can register stages that were not in
	// the registry at boot, so the list is rebuilt rather than assumed. With
	// no mod-declared arena this repoints nothing - see mpArenasRebuild.
	mpArenasRebuild();
	modStageRegReport();
}
