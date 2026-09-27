#ifndef _IN_MOD_H
#define _IN_MOD_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_CONFIG_FNAME "modconfig.txt"

/* Mod-tagged file ids.
 *
 * A file id is either a plain vanilla id, or a mod-owned id carrying its owning
 * mod in the high bits: MOD_FILEID_TAG | (modNum << 16) | rawId. The convention
 * was open-coded at a dozen sites with three different spellings of the
 * extraction - masked to 0xff, masked to 0xffff, and unmasked - so use these
 * instead of writing the shift by hand.
 *
 * rawId is 16 bits. modNum fits in 6: g_ModelStates_PerMod / g_ExplosionTypes_PerMod
 * are 64 entries and modSwitch bounds g_ModNum to < 64.
 *
 * This header used to say there was no "is this tagged" test, deliberately,
 * because mod 0 is the boot mod and so a zero modNum was a real owner rather
 * than an absence. That is now overturned on purpose. The consequence of
 * merging them was that MOD_FILEID_MOD(a plain vanilla id) returned 0, which
 * modeldef.c hands to g_TexModNum, which texdecompress.c folds together with
 * its own "no context" case - so the shared texture pool's bucket 0 was
 * "vanilla or mod 0 or no-context" and whichever loaded a texture number first
 * won for the others. Vanilla is a distinct owner. An untagged id is vanilla,
 * and MOD_FILEID_MOD returns -1 for it, matching the -1 that g_TexModNum,
 * modNumFromStage and ext_tex's ownerMod already use for "no mod".
 *
 * The tag is bit 24 and specifically not bit 31. A tagged file id is passed
 * verbatim through MENUMODELPARAMS_SET_FILENUM, which is the identity macro
 * (src/include/constants.h), and menu.c then tests that same word with
 * MENUMODELPARAMS_HAS_MASTER_HEADBODY, which is bit 31. A tag there would make
 * every mod-owned head file id read as a master head/body pair. Bits 16-23 of
 * that word are MP_HEADNUM and 24-31 are MP_BODYNUM, but those are only read
 * when the low 16 bits are 0xffff, which a real file id never is.
 *
 * g_ModNum, modDirs[] and every array subscripted by a mod index stay 0-based.
 * The +1-style shift lives nowhere; only the tag bit is new.
 */
#define MOD_FILEID_SHIFT    16
#define MOD_FILEID_RAW_MASK 0xffff
#define MOD_FILEID_MOD_MASK 0xff
#define MOD_FILEID_TAG      0x01000000

#ifdef __cplusplus
static_assert(MOD_FILEID_TAG != 0x80000000,
	"the tag must not be bit 31: MENUMODELPARAMS_HAS_MASTER_HEADBODY tests that "
	"bit on a word that carries a tagged file id verbatim");
#else
_Static_assert(MOD_FILEID_TAG != 0x80000000,
	"the tag must not be bit 31: MENUMODELPARAMS_HAS_MASTER_HEADBODY tests that "
	"bit on a word that carries a tagged file id verbatim");
#endif

#define MOD_FILEID_IS_TAGGED(id) \
	((s32)(id) >= 0 && ((s32)(id) & MOD_FILEID_TAG) != 0)
#define MOD_FILEID_RAW(id)  ((s32)((s32)(id) & MOD_FILEID_RAW_MASK))

/* modNum must be >= 0. A negative one masks to 0xff and makes an id claiming
 * mod 255, which no array is sized for; callers gate on it already. */
#define MOD_FILEID_MAKE(modNum, rawId) \
	((s32)(MOD_FILEID_TAG \
		| (((s32)(modNum) & MOD_FILEID_MOD_MASK) << MOD_FILEID_SHIFT) \
		| ((s32)(rawId) & MOD_FILEID_RAW_MASK)))

/*
 * Owning mod of a file id: -1 for vanilla (untagged), else 0..63.
 *
 * A function rather than a macro because it also reports the one state this
 * encoding can represent but nothing should ever produce - mod bits set with
 * the tag clear, which is what a hand-rolled (mod << 16) leaves behind. That
 * contradiction is undetectable under any encoding that does not spend a bit,
 * and catching it is the reason this one does.
 */
s32 modFileIdMod(s32 id);
#define MOD_FILEID_MOD(id)  modFileIdMod((s32)(id))

extern char g_ModNames[64][64];
extern s32 g_TexModNum;
extern s32 g_TexCurrentModelFileNum;  // low 16 bits: fileSlot of the model currently being loaded/rendered; 0 when none

struct animtableentry;

void modInit(void);
s32 modConfigLoad(const char *path);
s32 modLoadAIO(void);
void modScanAllMods(void);
void modStageDumpOwnership(const char *when);
void modCacheAllConfigs(void);

s32 modTextureLoad(u16 num, void *dst, u32 dstSize);
#define MOD_TEXTURE_RESOLVE_MAX_ATTEMPTS 5
struct modTextureResolveAttempt {
	char name[128];
	s32 fileNum;
};
struct modTextureResolveInfo {
	s32 modNum;
	s32 modelFileNum;
	u16 requestedId;
	u16 reverseLocalId;
	u16 resolvedLocalId;
	s32 fileNum;
	s32 matchedAttempt;
	s32 attemptCount;
	struct modTextureResolveAttempt attempts[MOD_TEXTURE_RESOLVE_MAX_ATTEMPTS];
};
s32 modTextureResolveFileDetailed(s32 modNum, s32 modelFileNum, u16 textureId,
		struct modTextureResolveInfo *info);
s32 modTextureResolveFile(s32 modNum, s32 modelFileNum, u16 textureId,
		u16 *resolvedLocalId, char *resolvedName, u32 resolvedNameSize);

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim);
void *modAnimationLoadData(u16 num);

void *modSequenceLoad(u16 num, u32 *outSize);

/*
 * The stage registry: what a mod says a stage IS, kept apart from what the
 * stage block changes about one.
 *
 * Before this existed a mod that wanted an arena had to declare the stage and
 * then repeat its stagenum in a separate MpArena block, with nothing holding
 * the two in step. One record per stagenum is the whole point.
 *
 * MODSTAGE_KIND_NONE is the default for a stage block that does not spell
 * `kind`, and the choice was measured rather than guessed. The nine
 * modconfig.txt files in the tree spell `kind` nowhere; inferring it from the
 * keys present would have moved stages into menus that never listed them -
 * mod_kakariko_stages names mpsetupfile on stage 0x24 (1 arena injected) and
 * mod_fojo names setupfile on 17 stages (17 missions injected). A missing
 * `kind` therefore means "changes something about a stage that is already
 * wherever it already is", which is exactly what those files are doing.
 *
 * SOLO and MP are bits so BOTH is their union and a caller asks
 * "is this MP-capable" without enumerating four values.
 *
 * MODSTAGE_KIND_SOLO is recorded and reported but nothing lists it yet, and
 * modStageRegReport says so out loud rather than leaving a mod author to
 * wonder. Listing it means growing g_SoloStages, which is NUM_SOLOSTAGES=21
 * rows indexed straight by menuhandlerMissionList's data->list.value, is the
 * index of g_GameFile.besttimes[21][3] and of the coopcompletions bitmask in
 * the save file, carries three lang ids and no customname, and picks its
 * thumbnail as g_TexGeneralConfigs + 13 + stageindex. That is a save-format
 * change, not a menu change, so it is deliberately not bundled with the
 * arena side.
 */
enum modStageKind {
	MODSTAGE_KIND_NONE = 0,
	MODSTAGE_KIND_SOLO = 1,
	MODSTAGE_KIND_MP   = 2,
	MODSTAGE_KIND_BOTH = 3,
};

struct modStageRegEntry {
	s16 stagenum;
	s8 modnum;          /* mod whose block last declared this stage */
	u8 kind;            /* enum modStageKind, unioned across blocks */
	u8 requirefeature;  /* challengeIsFeatureUnlocked arg; 0 = always unlocked */
	u16 langid;         /* legacy MpArena name; 0 when there is none */
	char *name;         /* owned display string, or NULL - see arenaname */
};

extern struct modStageRegEntry *g_ModStageReg;
extern s32 g_NumModStageReg;

void modStageRegReset(void);
// The mod directory's basename ("mod_fojo"), or its index when it has none.
// The mod's identity in pd.ini keys that outlive a session.
const char *modDirName(s32 modnum, char *buf, size_t bufSize);
struct modStageRegEntry *modStageRegFind(s32 stagenum);
struct modStageRegEntry *modStageRegRecord(s32 stagenum, s32 modnum, s32 kind,
		const char *name, s32 langid, s32 requirefeature);
s32 modStageRegCount(s32 kindmask);
void modStageRegReport(void);

/**
 * Per-field stage ownership, as OBSERVED at parse time.
 *
 * Phase 0 of multi-mod-stage-loading-plan.md: this records who claimed what
 * and nothing reads it back, so what the engine loads is unchanged. The point
 * is to be able to diff the record against the game's actual behaviour before
 * any of it becomes load-bearing - a resolver bug found here is free, and one
 * found after the six read sites move is not.
 *
 * `rung` is the candidate-ladder position that answered. There is no ladder
 * yet, so every recorded field carries MODSTAGE_RUNG_DECLARED; the field
 * exists now so the log format does not change when the ladder arrives.
 */
enum modStageField {
	MODSTAGE_BG,
	MODSTAGE_TILES,
	MODSTAGE_PADS,
	MODSTAGE_SETUP,
	MODSTAGE_MPSETUP,
	MODSTAGE_ALLOC,
	MODSTAGE_MUSIC,
	MODSTAGE_WEATHER,
	MODSTAGE_LANGBANK,
	MODSTAGE_ENV,
	MODSTAGE_FIELD_COUNT
};

#define MODSTAGE_RUNG_DECLARED 0

struct modStageFieldBinding {
	s32 fileId;    /* the id as parsed, or -1 for a field with no file */
	s8 owner;      /* mod that named it; -1 = nobody, so vanilla */
	u8 rung;       /* which candidate rung answered */
	u8 declared;   /* 1 = the claimer named this field */
};

struct modStageBinding {
	/* One bit per mod that declared this stage, because a mod's config is
	 * parsed more than once per boot and the count has to mean "how many mods"
	 * rather than "how many parses" - see modStageBindingClaim. modDirs is
	 * [64] and getModDirCount caps --moddir at that, so 64 bits is exact. */
	u64 claimMask;
	s8 claimedBy;    /* the mod whose claim is live in g_Stages; -1 = vanilla */
	s8 priority;     /* declared priority of that claim; unused until phase 1 */
	u8 claimCount;   /* popcount(claimMask) - collision report */
	struct modStageFieldBinding f[MODSTAGE_FIELD_COUNT];
};

/* Sized like g_ModStageNums rather than the plan's [256]: modConfigParseStage
 * refuses a stagenum >= ARRAYCOUNT(g_ModStageNums), so 256 entries would leave
 * 163 of them permanently unreachable. The bound lives in constants.h, which
 * this header does not pull in, so the array is declared incomplete here and
 * a _Static_assert in mod.c holds the two lengths together. */
extern struct modStageBinding g_StageBindings[];

const char *modStageFieldName(enum modStageField field);
void modStageBindingsReset(void);
void modStageBindingClaim(s32 stagenum, s32 modnum);
void modStageBindingRecord(s32 stagenum, s32 modnum, enum modStageField field, s32 fileId);
void modStageBindingReport(void);

void modLoadTextureSurfaceType(void);
void modUnloadTextureSurfaceType(void);
void modSwitch(s32 modnum, s32 stagenum);
s32 modNumFromStage(s32 stagenum);
s32 modLookupHeadByName(const char *name);

// Persisted g_MpHeads / g_MpBodies index allocation, keyed by the head or body
// name a modconfig declares. See the note above the store in mod.c.
void modSlotReservationsInit(void);
// A level name with no stage table row of its own: allocate a free STAGE_EXTRA
// row for it and persist the choice, so the name keeps that row across boots
// and roster changes. Returns -1 when the pool is exhausted.
s32 modStageSlotReserve(const char *name);
void modStageSlotClaim(const char *name, s32 slot);

/*
 * Per-stagenum memory allocation strings, from a modconfig's `allocation`
 * key. Lives in pdmain.c beside g_StageAllocations8Mb, which it is consulted
 * before. STAGE_ALLOCATION_MP_DEFAULT is the string every authored MP row in
 * that table carries, and what a level row with no entry and no key now gets
 * instead of the terminator's.
 */
#define STAGE_ALLOCATION_MP_DEFAULT "-ml0 -me0 -mgfx200 -mvtx200 -ma400"
#define STAGE_ALLOCATION_SOLO_DEFAULT "-ml0 -me0 -mgfx110 -mgfxtra80 -mvtx100 -ma700"
void stageSetModAllocation(s32 stagenum, const char *string);
const char *stageGetModAllocation(s32 stagenum);
// A saved profile records a head/body by the hash of its reservation name, not
// by its index - the index is an allocation detail and the 7-bit save field
// cannot hold it once mod heads push past 127. See mplayer.c.
u32 modSlotNameHash(const char *name);
const char *modHeadSlotName(s32 slot);
const char *modBodySlotName(s32 slot);
s32 modHeadSlotForHash(u32 hash);
s32 modBodySlotForHash(u32 hash);
// A saved MP setup records its level and each bot's head and body by NAME in
// pd.ini ([MpSetup.<guid>]) beside the 7-bit fields; these are the lookups
// both directions. The ForName lookups never allocate. See mpsetups.c.
const char *modStageSlotName(s32 stagenum);
s32 modStageSlotForName(const char *name);
s32 modHeadSlotForName(const char *name);
s32 modBodySlotForName(const char *name);
s32 modHeadSlotReserve(const char *name);
s32 modBodySlotReserve(const char *name);
void modHeadSlotClaim(const char *name, s32 slot);
void modBodySlotClaim(const char *name, s32 slot);
s32 modLookupBodyByName(const char *name);
s32 modLookupHandFileByName(const char *name);
s32 modHandNameCount(void);
const char *modHandName(s32 index, u32 *filenum);
s32 modLookupHeadnumByName(const char *name);
const char *modGetNameForHeadBodyIndex(s32 headBodyIndex);

#ifdef __cplusplus
}
#endif

#endif
