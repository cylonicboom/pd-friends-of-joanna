#include "types.h"
#include "constants.h"
#include "bss.h"
#include "game/pak.h"
#include "game/filelist.h"
#include "game/menu.h"
#include "game/savebuffer.h"
#include "game/mplayer/mplayer.h"
#include <string.h>
#include "fs.h"
#include "system.h"
#include "mpsetups.h"
#include "savequeue.h"
#include "mod.h"
#include "config.h"
#include "lib/rng.h"
#include "game/stagetable.h"
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

/*
MP Setup File Format
	# header
	[version{1}]
	[defaultsetup{1}]
	[numsetups{1}]
	# setups
	[setup_1{80}]
	...
	[setup_n{80}]
 */

// MPSETUP_VERSION and MPSETUP_BLOCKSIZE live in constants.h beside the wad
// reader that has to agree with them.

#define MPSETUP_EXPORTDIR "$S/exported/"
#define MPSETUP_FILENAME "mpsetups"
#define MPSETUP_FILENAME_EXP "mpsetups-ext"

#define MPSETUP_OP_DEFAULT 0
#define MPSETUP_OP_IMPORT 1
#define MPSETUP_OP_EXPORT 2

#define MPSETUP_IMPORT_OVERWRITE 0
#define MPSETUP_IMPORT_ADD 1

#define MPSETUP_IMPORT_CONFLICT 0x1313

extern struct menudialogdef g_MpSaveSetupNameMenuDialog;

s16 g_MpCurrentSetup = -1;
struct mpsetupfile g_MpSetupFile;

static struct mpsetupfile g_ImportMpSetupFile;
static u64 g_MpImportExportFilter[2];

static char g_StatusText[128];
static char g_TitleImportExportDialog[20];
static char g_LabelSetDefault[20] = "Set Default\n";

/* extended mpsetup menu definitions */

static MenuItemHandlerResult menuhandlerRenameSetup(s32 operation, struct menuitem *item, union handlerdata *data);
static MenuItemHandlerResult menuhandlerDeleteSetup(s32 operation, struct menuitem *item, union handlerdata *data);
static MenuItemHandlerResult menuhandlerImportOrExportSettings(s32 operation, struct menuitem *item, union handlerdata *data);
static MenuItemHandlerResult menuhandlerImportAction(s32 operation, struct menuitem *item, union handlerdata *data);
static MenuItemHandlerResult menuhandlerOpenImportExportDialog(s32 operation, struct menuitem *item, union handlerdata *data);
static MenuItemHandlerResult menuhandlerSelectSetupHandler(s32 operation, struct menuitem *item, union handlerdata *data);
static MenuItemHandlerResult menuhandlerSetupRename(s32 operation, struct menuitem *item, union handlerdata *data);
static MenuItemHandlerResult menuhandlerSetupDelete(s32 operation, struct menuitem *item, union handlerdata *data);
static MenuItemHandlerResult menuhandlerSetupSetDefault(s32 operation, struct menuitem *item, union handlerdata *data);

static struct menuitem g_StatusOkMenuItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_LESSLEFTPADDING,
		(uintptr_t)g_StatusText,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CLOSESDIALOG | MENUITEMFLAG_SELECTABLE_CENTRE,
		L_OPTIONS_347, // "OK"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

static struct menudialogdef g_StatusOkDialog = {
	MENUDIALOGTYPE_SUCCESS,
	L_OPTIONS_345, // "Cool!"
	g_StatusOkMenuItems,
	NULL,
	MENUDIALOGFLAG_DISABLEBANNER,
	NULL,
};

static struct menuitem g_StatusErrorMenuItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_LESSLEFTPADDING,
		(uintptr_t)g_StatusText,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CLOSESDIALOG | MENUITEMFLAG_SELECTABLE_CENTRE,
		L_OPTIONS_347, // "OK"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

/* public */
struct menudialogdef g_StatusErrorDialog = {
		MENUDIALOGTYPE_DANGER,
		L_OPTIONS_277, // "Failed"
		g_StatusErrorMenuItems,
		NULL,
		MENUDIALOGFLAG_DISABLEBANNER,
		NULL,
};

static struct menuitem g_RenameSetupItems[] = {
#if VERSION != VERSION_JPN_FINAL
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_LESSLEFTPADDING,
		(uintptr_t)"Enter the setup name:\n",
		0,
		NULL,
	},
#endif
	{
		MENUITEMTYPE_KEYBOARD,
		18,
		0,
		0,
		1,
		menuhandlerRenameSetup,
	},
	{ MENUITEMTYPE_END },
};

static struct menudialogdef g_RenameSetupDialog = {
		MENUDIALOGTYPE_DEFAULT,
		(uintptr_t)"Setup Name:\n",
		g_RenameSetupItems,
		NULL,
		MENUDIALOGFLAG_LITERAL_TEXT,
		NULL,
};

static struct menuitem g_DeleteSetupItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_LESSLEFTPADDING,
		(uintptr_t)"Delete Setup?\n",
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CLOSESDIALOG | MENUITEMFLAG_SELECTABLE_CENTRE,
		L_OPTIONS_385, // "No"
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CENTRE,
		L_OPTIONS_386, // "Yes"
		0,
		menuhandlerDeleteSetup,
	},
	{ MENUITEMTYPE_END },
};

static struct menudialogdef g_DeleteSetupDialog = {
		MENUDIALOGTYPE_DANGER,
		(uintptr_t)"Delete Setup\n",
		g_DeleteSetupItems,
		NULL,
		MENUDIALOGFLAG_LITERAL_TEXT,
		NULL,
};

static struct menuitem g_ImportExportItems[] = {
	{
		MENUITEMTYPE_LIST,
		0,
		MENUITEMFLAG_LOCKABLEMINOR | MENUITEMFLAG_LABEL_CUSTOMCOLOUR,
		140,
		0x0000004d,
		menuhandlerImportOrExportSettings,
	},
	{ MENUITEMTYPE_END },
};

static struct menudialogdef g_ImportExportDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t) g_TitleImportExportDialog,
	g_ImportExportItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
};

static struct menuitem g_ManageImportExportItems[] = {
	{
		MENUITEMTYPE_SELECTABLE,
		MPSETUP_OP_IMPORT,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Import Settings\n",
		0,
		menuhandlerOpenImportExportDialog,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		MPSETUP_OP_EXPORT,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Export Settings\n",
		0,
		menuhandlerOpenImportExportDialog,
	},
	{ MENUITEMTYPE_END },
};

static struct menudialogdef g_ManageImportExportDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t) "Import/Export\n",
	g_ManageImportExportItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
};

static struct menuitem g_MpManageSettingsListItems[] = {
	{
		MENUITEMTYPE_LIST,
		0,
		MENUITEMFLAG_LABEL_CUSTOMCOLOUR,
		160,
		0x00000042,
		menuhandlerSelectSetupHandler,
	},
	{ MENUITEMTYPE_END },

};

/* public */
struct menudialogdef g_ManageSettingsDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t) "Manage Settings\n",
	g_MpManageSettingsListItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	&g_ManageImportExportDialog,
};

static struct menuitem g_ManageSetupItems[] = {
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Rename\n",
		0,
		menuhandlerSetupRename,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)"Delete\n",
		0,
		menuhandlerSetupDelete,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_LITERAL_TEXT,
		(uintptr_t)g_LabelSetDefault,
		0,
		menuhandlerSetupSetDefault,
	},
	{
		MENUITEMTYPE_SEPARATOR,
		0,
		0,
		0,
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		0,
		MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		L_OPTIONS_213, // "Back"
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

static struct menudialogdef g_ManageSetupDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Manage Setup",
	g_ManageSetupItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
};

static struct menuitem g_ImportOverrideItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_LESSLEFTPADDING,
		(uintptr_t) "How to resolve setups\nwith the same name?\n",
		0,
		NULL,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		MPSETUP_IMPORT_ADD,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t) "Add\n",
		0,
		menuhandlerImportAction,
	},
	{
		MENUITEMTYPE_SELECTABLE,
		MPSETUP_IMPORT_OVERWRITE,
		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_SELECTABLE_CLOSESDIALOG,
		(uintptr_t) "Overwrite\n",
		0,
		menuhandlerImportAction,
	},
	{ MENUITEMTYPE_END },
};

static struct menudialogdef g_ImportOverrideDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Name Conflicts\n",
	g_ImportOverrideItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
};

/*
 * What a saved setup remembers by NAME.
 *
 * The block holds the level as a 7-bit stagenum and each bot's head and body
 * as 7-bit slot indices (mpsetupfileSaveWad). Those numbers are allocation
 * details: a mod level or head gets whatever index the reservation tables
 * handed its name on this install, the tables can hand out more than 127 of
 * them, and the number a different install hands the same name is different.
 * A player's own head already solves this by writing the reservation NAME's
 * hash into the extended profile (mpProfileStoreSlotHashes). A setup does the
 * same in text: every setup carries a guid (MPSETUP_GUID_BITPOS, the one
 * addition to the block), and pd.ini carries a section for it -
 *
 *   [MpSetup.1d6a9f03]
 *   stage=gex_arec
 *   bot0head=head_mikado
 *   bot0body=
 *   ...
 *
 * - bound with live pointers into g_MpSetupExt so configSave writes it back,
 * exactly as [MpPlayer.*] is bound. A vanilla level or head has no reservation
 * and so no name, and the number is trusted: vanilla ids never move.
 *
 * On load, a name outranks the number whenever a mounted mod reserved that
 * name this session (modStageSlotForName and friends never allocate). A name
 * NO mounted mod reserved means the mod is not here, and the number would load
 * some other mod's level or wear some other mod's face without a word - so it
 * is refused: the level falls back to the arena mpInit picks and the bot to
 * the defaults func0f1881d4 gives a fresh one, and the log names the setup.
 *
 * Every setup in the file is bound when the file loads, so no section is
 * dropped for being untouched this session. A deleted setup's section is
 * unbound and goes with it. An exported setup carries its guid but not its
 * section; on an install whose pd.ini lacks it the names are empty and the
 * numbers stand, which is what v1 always did.
 */
#define MPSETUP_EXT_NAME 64

struct mpsetupext {
	u32 guid;
	char stage[MPSETUP_EXT_NAME];
	char bothead[MAX_BOTS][MPSETUP_EXT_NAME];
	char botbody[MAX_BOTS][MPSETUP_EXT_NAME];
};

static struct mpsetupext g_MpSetupExt[MPSETUP_MAXSETUPS];

static u32 mpsetupNewGuid(void)
{
	u32 guid;

	do {
		guid = rngRandom() ^ (rngRandom() << 16) ^ (u32)time(NULL);
	} while (!guid);

	return guid;
}

static void mpsetupExtSection(u32 guid, char *out, size_t outlen)
{
	snprintf(out, outlen, "MpSetup.%08x", guid);
}

static void mpsetupExtKeys(s32 index, void (*fn)(const char *key, char *field))
{
	struct mpsetupext *ext = &g_MpSetupExt[index];
	char sec[32];
	char key[CONFIG_MAX_SECNAME * 2 + 2];

	mpsetupExtSection(ext->guid, sec, sizeof(sec));

	snprintf(key, sizeof(key), "%s.stage", sec);
	fn(key, ext->stage);

	for (s32 i = 0; i < MAX_BOTS; ++i) {
		snprintf(key, sizeof(key), "%s.bot%dhead", sec, i);
		fn(key, ext->bothead[i]);
		snprintf(key, sizeof(key), "%s.bot%dbody", sec, i);
		fn(key, ext->botbody[i]);
	}
}

static void mpsetupExtRegister(const char *key, char *field)
{
	configRegisterString(key, field, MPSETUP_EXT_NAME);
}

static void mpsetupExtUnregister(const char *key, char *field)
{
	struct configentry *cfg = configFindEntryByPtr(field);

	if (cfg) {
		// No ptr means configSave skips it, which is how a section goes away.
		cfg->ptr = NULL;
	}
}

// Register this index's keys with live pointers; with `load`, also read the
// section off disk into them.
static void mpsetupBindExt(s32 index, bool load)
{
	if (!g_MpSetupExt[index].guid) {
		return;
	}

	mpsetupExtKeys(index, mpsetupExtRegister);

	if (load) {
		char sec[32];
		mpsetupExtSection(g_MpSetupExt[index].guid, sec, sizeof(sec));
		configLoadSection(CONFIG_PATH, sec);
	}
}

static void mpsetupUnbindExt(s32 index)
{
	if (g_MpSetupExt[index].guid) {
		mpsetupExtKeys(index, mpsetupExtUnregister);
	}

	memset(&g_MpSetupExt[index], 0, sizeof(g_MpSetupExt[index]));
}

// A block just read from a file, or just imported: take its guid, or give it
// one if it predates guids, and bind its section. Returns whether a guid was
// minted - the caller must then write the file, or the section configSave
// writes at exit names a guid no block carries and the next boot mints
// another, leaving an orphan section per boot.
static bool mpsetupAdoptBlock(s32 index)
{
	struct savebuffer buffer;
	u32 guid;
	bool minted = false;

	savebufferClear(&buffer);
	memcpy(buffer.bytes, g_MpSetupFile.setups[index].bytes, MPSETUP_BLOCKSIZE);
	guid = mpsetupfilePeekGuid(&buffer);

	if (!guid) {
		guid = mpsetupNewGuid();
		mpsetupfilePokeGuid(&buffer, guid);
		memcpy(g_MpSetupFile.setups[index].bytes, buffer.bytes, MPSETUP_BLOCKSIZE);
		minted = true;
	}

	mpsetupUnbindExt(index);
	g_MpSetupExt[index].guid = guid;
	mpsetupBindExt(index, true);

	return minted;
}

static void mpsetupCopyName(char *dst, const char *src)
{
	if (src) {
		strncpy(dst, src, MPSETUP_EXT_NAME - 1);
		dst[MPSETUP_EXT_NAME - 1] = '\0';
	} else {
		dst[0] = '\0';
	}
}

// Before the live setup is written into slot `index`: settle its guid and
// record the names its numbers currently stand for.
static void mpsetupStoreExt(s32 index)
{
	struct mpsetupext *ext = &g_MpSetupExt[index];

	if (!g_MpSetupGuid || ext->guid != g_MpSetupGuid) {
		// A new setup, or the live one being saved into a slot other than the
		// one it was loaded from: it needs an identity of its own rather than
		// sharing one. Overwriting the slot it came from keeps its guid.
		g_MpSetupGuid = mpsetupNewGuid();
		mpsetupUnbindExt(index);
		ext->guid = g_MpSetupGuid;
	}

	mpsetupCopyName(ext->stage, modStageSlotName(g_MpSetup.stagenum));

	for (s32 i = 0; i < MAX_BOTS; ++i) {
		mpsetupCopyName(ext->bothead[i], modHeadSlotName(g_BotConfigsArray[i].base.mpheadnum));
		mpsetupCopyName(ext->botbody[i], modBodySlotName(g_BotConfigsArray[i].base.mpbodynum));
	}

	mpsetupBindExt(index, false);
}

// After a setup is loaded into g_MpSetup: let its names outrank its numbers.
static void mpsetupApplyExt(s32 index)
{
	struct mpsetupext *ext = &g_MpSetupExt[index];
	s32 slot;

	if (ext->guid != g_MpSetupGuid) {
		return;
	}

	// A setup saved before names were recorded (or by a v1 build) has empty
	// names. Its numbers are trusted this once, and any that stand for a
	// reservation this session are written back as names, so the upgrade
	// completes the first time the setup is loaded rather than the next
	// time it is saved.
	if (!ext->stage[0]) {
		mpsetupCopyName(ext->stage, modStageSlotName(g_MpSetup.stagenum));
	}

	for (s32 i = 0; i < MAX_BOTS; ++i) {
		if (!ext->bothead[i][0]) {
			mpsetupCopyName(ext->bothead[i], modHeadSlotName(g_BotConfigsArray[i].base.mpheadnum));
		}
		if (!ext->botbody[i][0]) {
			mpsetupCopyName(ext->botbody[i], modBodySlotName(g_BotConfigsArray[i].base.mpbodynum));
		}
	}

	if (ext->stage[0]) {
		slot = modStageSlotForName(ext->stage);

		if (slot >= 0) {
			sysLogPrintf(LOG_NOTE, "mpsetup '%s': level '%s' is stage 0x%02x this session (saved as 0x%02x)",
					g_MpSetup.name, ext->stage, slot, g_MpSetup.stagenum);
			g_MpSetup.stagenum = slot;
		} else {
			sysLogPrintf(LOG_WARNING, "mpsetup '%s': its level '%s' is not mounted; using the default arena "
					"instead of whatever holds stage 0x%02x now",
					g_MpSetup.name, ext->stage, g_MpSetup.stagenum);
			g_MpSetup.stagenum = STAGE_MP_SKEDAR;
		}
	}

	for (s32 i = 0; i < MAX_BOTS; ++i) {
		if (ext->bothead[i][0]) {
			slot = modHeadSlotForName(ext->bothead[i]);

			if (slot >= 0) {
				g_BotConfigsArray[i].base.mpheadnum = slot;
			} else {
				sysLogPrintf(LOG_WARNING, "mpsetup '%s': bot %d's head '%s' is not mounted; using the default head",
						g_MpSetup.name, i, ext->bothead[i]);
				g_BotConfigsArray[i].base.mpheadnum = MPHEAD_DARK_COMBAT;
			}
		}

		if (ext->botbody[i][0]) {
			slot = modBodySlotForName(ext->botbody[i]);

			if (slot >= 0) {
				g_BotConfigsArray[i].base.mpbodynum = slot;
			} else {
				sysLogPrintf(LOG_WARNING, "mpsetup '%s': bot %d's body '%s' is not mounted; using the default body",
						g_MpSetup.name, i, ext->botbody[i]);
				g_BotConfigsArray[i].base.mpbodynum = MPBODY_DARK_COMBAT;
			}
		}
	}
}


/* common utils */

static s32 mpsetupDeserialize(FILE *f, struct mpsetupfile *setupfile)
{
	s32 rx = 0;

	rx += fread(&setupfile->version, sizeof(setupfile->version), 1, f);
	rx += fread(&setupfile->defaultsetup, sizeof(setupfile->defaultsetup), 1, f);
	rx += fread(&setupfile->numsetups, sizeof(setupfile->numsetups), 1, f);

	if (setupfile->version > MPSETUP_VERSION) {
		// Written by a newer build. The block layout past what this build
		// knows is unreadable, and guessing at it would load the wrong level
		// or the wrong face without a word. Refuse it whole.
		sysLogPrintf(LOG_ERROR, "mpsetup: file is version %u, this build reads up to %u; ignoring it",
				setupfile->version, MPSETUP_VERSION);
		setupfile->numsetups = 0;
		setupfile->defaultsetup = 0;
		return rx;
	}

	for (int i = 0; i < setupfile->numsetups; ++i) {
		// A v1 block is the same 80 bytes with a zero tail, so its guid reads
		// as 0 and mpsetupLoadFile assigns it one.
		memset(setupfile->setups[i].bytes, 0, sizeof(setupfile->setups[i].bytes));
		rx += fread(setupfile->setups[i].bytes, sizeof(setupfile->setups[i].bytes), 1, f);
	}

	return rx;
}

static s32 mpsetupSerialize(FILE *f, struct mpsetupfile *setupfile)
{
	s32 wx = 0;

	wx += fwrite(&setupfile->version, sizeof(setupfile->version), 1, f);
	wx += fwrite(&setupfile->defaultsetup, sizeof(setupfile->defaultsetup), 1, f);
	wx += fwrite(&setupfile->numsetups, sizeof(setupfile->numsetups), 1, f);

	for (int i = 0; i < setupfile->numsetups; ++i) {
		wx += fwrite(setupfile->setups[i].bytes, sizeof(setupfile->setups[i].bytes), 1, f);
	}

	return wx;
}

static FILE *mpsetupOpenFile(bool write, u8 op) {
	const char *filename = fsFullPath("$S/" MPSETUP_FILENAME ".bin");

	if (op == MPSETUP_OP_EXPORT) {
		// create export directory if it doesn't exist
		if (fsFileSize(MPSETUP_EXPORTDIR) < 0) {
			if (fsCreateDir(MPSETUP_EXPORTDIR) != 0) {
				return NULL;
			}
		}
		filename = fsFullPath(MPSETUP_EXPORTDIR MPSETUP_FILENAME_EXP ".bin");
	} else if (op == MPSETUP_OP_IMPORT) {
		// same name as export but different folder
		filename = fsFullPath("$S/" MPSETUP_FILENAME_EXP ".bin");
	}

	FILE *f;

	if (fsFileSize(filename) < 0) {
		// setup file doesn't exist: create one
		f = fsFileOpenWrite(filename);
		fsFileFree(f);
	}

	if (write) {
		f = fsFileOpenWrite(filename);
	} else {
		f = fsFileOpenRead(filename);
	}

	if (f == NULL) {
		sysLogPrintf(LOG_ERROR, "Unable to open mp setup file");
		return NULL;
	}

	return f;
}

static s32 mpsetupSaveFile(u8 op, struct mpsetupfile *setupfile)
{
	FILE *f = mpsetupOpenFile(true, op);
	if (f == NULL) {
		return -1;
	}

	s32 nwritten = mpsetupSerialize(f, setupfile);
	if (nwritten < 1) {
		fsFileFree(f);
		sysLogPrintf(LOG_ERROR, "Unable to write the MP setup file");
		snprintf(g_StatusText, sizeof(g_StatusText), "Unable to write the setup file\n");
		return -1;
	}

	fsFileFree(f);
	return 0;
}

static s32 mpsetupLoadFile(struct mpsetupfile *setupfile, u8 op)
{
	FILE *f = mpsetupOpenFile(false, op);
	if (f == NULL) {
		return -1;
	}

	mpsetupDeserialize(f, setupfile);

	if (setupfile == &g_MpSetupFile) {
		s32 minted = 0;

		for (s32 i = 0; i < g_MpSetupFile.numsetups; ++i) {
			minted += mpsetupAdoptBlock(i);
		}

		if (minted > 0) {
			// Upgrade on read: a v1 file becomes v2 here, with its guids on
			// disk before anything else can mention them.
			sysLogPrintf(LOG_NOTE, "mpsetup: %d setup(s) had no guid; assigned and saved (file version %u -> %u)",
					minted, setupfile->version, MPSETUP_VERSION);
			fsFileFree(f);
			f = NULL;
			mpsetupSaveCurrentFile();
		}
	}

	if (op == MPSETUP_OP_DEFAULT && setupfile->defaultsetup > 0) {
		mpsetupLoadSetup(setupfile->defaultsetup - 1);
	}

	if (f) {
		fsFileFree(f);
	}

	return 0;
}

static s32 mpsetupImportFile(u8 op, u8 skipOverlap)
{
	if (!skipOverlap) {
		// check for names overlap
		u8 overlap = false;
		for (int i = 0; i < g_ImportMpSetupFile.numsetups; ++i) {
			for (int j = 0; j < g_MpSetupFile.numsetups; ++j) {
				if (strcmp(g_ImportMpSetupFile.setups[i].bytes, g_MpSetupFile.setups[j].bytes) == 0) {
					overlap = true;
					return MPSETUP_IMPORT_CONFLICT;
				}
			}
		}
	}

	for (int i = 0; i < g_ImportMpSetupFile.numsetups; ++i) {
		s16 overlapIdx = -1;
		for (int j = g_MpSetupFile.numsetups-1; j >= 0; --j) {
			if (strcmp(g_ImportMpSetupFile.setups[i].bytes, g_MpSetupFile.setups[j].bytes) == 0) {
				overlapIdx = j;
				break;
			}
		}

		s16 importIdx = overlapIdx;
		if (overlapIdx < 0 || op == MPSETUP_IMPORT_ADD) {
			if (g_MpSetupFile.numsetups == MPSETUP_MAXSETUPS) {
				snprintf(g_StatusText, sizeof(g_StatusText), "Number of setups exceeds %d\n", MPSETUP_MAXSETUPS);
				return -1;
			}
			importIdx = g_MpSetupFile.numsetups++;
		}

		memcpy(g_MpSetupFile.setups[importIdx].bytes, g_ImportMpSetupFile.setups[i].bytes, MPSETUP_BLOCKSIZE);
		mpsetupAdoptBlock(importIdx);
	}

	return mpsetupSaveCurrentFile();
}

static s32 mpsetupExportFile(void)
{
	struct mpsetupfile expMpSetupFile;
	expMpSetupFile.numsetups = 0;
	expMpSetupFile.defaultsetup = 0;
	expMpSetupFile.version = MPSETUP_VERSION;

	u8 maxsetups = MPSETUP_MAXSETUPS;
	maxsetups = MIN(maxsetups, g_MpSetupFile.numsetups);
	for (int i = 0; i < maxsetups; ++i) {
		u8 bank = i < 64 ? 0 : 1;
		u8 bit = i - bank * 64;
		if (g_MpImportExportFilter[bank] & (1 << bit)) {
			u8 n = expMpSetupFile.numsetups;
			expMpSetupFile.numsetups++;
			char *name = g_MpSetupFile.setups[i].bytes;
			memcpy(expMpSetupFile.setups[n].bytes, g_MpSetupFile.setups[i].bytes, MPSETUP_BLOCKSIZE);
			expMpSetupFile.numsetups = n + 1;
		}
	}

	s32 err = mpsetupSaveFile(MPSETUP_OP_EXPORT, &expMpSetupFile);
	if (err) {
		snprintf(g_StatusText, sizeof(g_StatusText), "Unable to write\nsetup file\n");
	}

	return err;
}

static s32 mpsetupDelete(void)
{
	s32 slotindex = g_Menus[g_MpPlayerNum].mpsetup.slotindex;
	mpsetupUnbindExt(slotindex);
	for (int i = slotindex; i < g_MpSetupFile.numsetups - 1; ++i) {
		u8* dst = g_MpSetupFile.setups[i].bytes;
		u8* src = g_MpSetupFile.setups[i+1].bytes;
		memcpy(dst, src, MPSETUP_BLOCKSIZE);
		// The section's pointers followed the setup down a slot.
		g_MpSetupExt[i] = g_MpSetupExt[i + 1];
		mpsetupBindExt(i, false);
	}
	if (g_MpSetupFile.numsetups > 0) {
		memset(&g_MpSetupExt[g_MpSetupFile.numsetups - 1], 0, sizeof(g_MpSetupExt[0]));
	}

	if (g_MpCurrentSetup > slotindex) {
		g_MpCurrentSetup--;
	}
	else if (g_MpCurrentSetup == slotindex) {
		g_MpCurrentSetup = -1;
	}

	if (g_MpSetupFile.defaultsetup > slotindex + 1) {
		 g_MpSetupFile.defaultsetup--;
	}
	else if (g_MpSetupFile.defaultsetup == slotindex + 1) {
		g_MpSetupFile.defaultsetup = 0;
	}

	g_MpSetupFile.numsetups--;
	return mpsetupSaveCurrentFile();
}

/* menu handlers */

// Rename setup dialog
static MenuItemHandlerResult menuhandlerRenameSetup(s32 operation, struct menuitem *item, union handlerdata *data)
{
	char *name = data->keyboard.string;
	s32 slotindex = g_Menus[g_MpPlayerNum].mpsetup.slotindex;
	struct setupblock *setup = &g_MpSetupFile.setups[slotindex];
	s32 err;

	switch (operation) {
	case MENUOP_GETTEXT:
		strcpy(name, setup->bytes);
		break;
	case MENUOP_SETTEXT:
		strcpy(g_MpSetup.name, name);
		break;
	case MENUOP_SET:
		strcpy(setup->bytes, name);
		err = mpsetupSaveSetup(slotindex, true);
		if (!err) {
			menuPopDialog();
		} else {
			// TODO
			// menuPushDialog(&g_FilemgrFileSavedMenuDialog);
		}
		break;
	}

	return 0;
}

// Delete setup dialog
static MenuItemHandlerResult menuhandlerDeleteSetup(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		s32 err = mpsetupDelete();
		if (!err) {
			menuPopDialog();
			menuPopDialog();
		} else {
			// TODO
		}
	}

	return 0;
}

// Import (or Export) settings dialog
static MenuItemHandlerResult menuhandlerImportOrExportSettings(s32 operation, struct menuitem *item, union handlerdata *data)
{
	static const char *labels[] = {
		"Select All",
		"Select None",
		"Import",
		"Export",
	};

	s32 op = g_Menus[g_MpPlayerNum].mpsetup.unke24;

	struct mpsetupfile *setupfile = op == MPSETUP_OP_IMPORT ? &g_ImportMpSetupFile : &g_MpSetupFile;

	u8 numsetups = setupfile->numsetups;
	u8 numitems = numsetups + 3;
	u8 bank = data->list.value < 64 ? 0 : 1;
	u8 bit = data->list.value - bank * 64;
	u8 hasselection = (g_MpImportExportFilter[0] | g_MpImportExportFilter[1]) != 0;
	u8 ofs;
	s32 err;
	u32 colour;
	s32 index;

	switch (operation) {
	case MENUOP_GETCOLOUR:
		colour = data->list.unk04;
		if (data->list.value == numitems - 1) {
			data->list.unk04 = hasselection ? colour : (colour & 0xffffff00) | 0x66;
		}
		break;

	case MENUOP_GETOPTIONCOUNT:
		data->list.value = numitems;
		break;

	case MENUOP_GETOPTIONTEXT:
		if (data->list.value < numsetups) {
			return (uintptr_t) setupfile->setups[data->list.value].bytes;
		} else {
			// creates an offset to select the "Export" or "Import" label for the last item
			ofs = (data->list.value == numitems - 1) ? op - 1 : 0;
			return (intptr_t)labels[data->list.value - numsetups + ofs];
		}

	case MENUOP_SET:
		if (data->list.value < setupfile->numsetups) {
			g_MpImportExportFilter[bank] ^= (1 << bit);
		} else {
			index = data->list.value - setupfile->numsetups;

			switch (index) {
			// Select All
			case 0:
				g_MpImportExportFilter[0] = g_MpImportExportFilter[1] = -1;
				break;
			// Select None
			case 1:
				g_MpImportExportFilter[0] = g_MpImportExportFilter[1] = 0;
				break;
			// Export (or Import)
			case 2:
				if (hasselection) {
					err = op == MPSETUP_OP_IMPORT ? mpsetupImportFile(0, false) : mpsetupExportFile();
					if (!err) {
						menuPopDialog();
						if (op == MPSETUP_OP_IMPORT) {
							// back to the 'Manage Settings' screen
							menuPopDialog();
							menuPushDialog(&g_ManageSettingsDialog);
						} else {
							snprintf(g_StatusText, sizeof(g_StatusText), "File %s.bin\nwritten to the folder 'exported'\n", MPSETUP_FILENAME_EXP);
							menuPushDialog(&g_StatusOkDialog);
						}
					} else {
						if (err == MPSETUP_IMPORT_CONFLICT) {
							menuPopDialog();
							menuPushDialog(&g_ImportOverrideDialog);
						} else {
							menuPushDialog(&g_StatusErrorDialog);
						}
					}
				}
				break;
			}
		}
		break;

	case MENUOP_GETSELECTEDINDEX:
		data->list.value = 0x000fffff;
		break;

	case MENUOP_GETLISTITEMCHECKBOX:
		if (data->list.value < numsetups) {
			data->list.unk04 = (g_MpImportExportFilter[bank] & (1 << bit));
		}
		break;
	}

	return 0;
}

// Import: overwrite dialog
static MenuItemHandlerResult menuhandlerImportAction(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		s32 err = mpsetupImportFile(item->param, true);
		if (!err) {
			menuPopDialog();
			menuPushDialog(&g_ManageSettingsDialog);
		} else {
			menuPushDialog(&g_StatusErrorDialog);
		}
	}

	return 0;
}

// Manage Import/Export settings dialog
static MenuItemHandlerResult menuhandlerOpenImportExportDialog(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_SET:
		if (item->param == MPSETUP_OP_IMPORT) {
			strcpy(g_TitleImportExportDialog, "Import Settings\n");
			if (fsFileSize("$S/" MPSETUP_FILENAME_EXP ".bin") < 0) {
				snprintf(
					g_StatusText, sizeof(g_StatusText),
					"No import file found.\n"
					"Place the file %s.bin\n"
					"Next to your %s.bin file\n",
					MPSETUP_FILENAME_EXP, MPSETUP_FILENAME
				);
				menuPushDialog(&g_StatusErrorDialog);
				return 0;
			}

			mpsetupLoadFile(&g_ImportMpSetupFile, MPSETUP_OP_IMPORT);
		} else {
			strcpy(g_TitleImportExportDialog, "Export Settings\n");
		}
		g_Menus[g_MpPlayerNum].mpsetup.unke24 = item->param;
		g_MpImportExportFilter[0] = g_MpImportExportFilter[1] = -1;
		menuPushDialog(&g_ImportExportDialog);
		break;
	}

	return 0;
}

// Manage settings dialog
static MenuItemHandlerResult menuhandlerSelectSetupHandler(s32 operation, struct menuitem *item, union handlerdata *data)
{
	u32 colour;
	switch (operation) {
	case MENUOP_GETCOLOUR:
		colour = data->list.unk04;
		if (data->list.value + 1 == g_MpSetupFile.defaultsetup) {
			data->list.unk04 = (colour & 0xffff0000) | 0xff;
		}
		break;
	case MENUOP_GETOPTIONCOUNT:
		data->list.value = g_MpSetupFile.numsetups;
		break;
	case MENUOP_GETOPTIONTEXT:
		return (uintptr_t) g_MpSetupFile.setups[data->list.value].bytes;
	case MENUOP_SET:
		g_Menus[g_MpPlayerNum].mpsetup.slotindex = data->list.value;
		if (data->list.value == g_MpSetupFile.defaultsetup - 1) {
			strcpy(g_LabelSetDefault, "Clear Default\n");
		} else {
			strcpy(g_LabelSetDefault, "Set Default\n");
		}
		menuPushDialog(&g_ManageSetupDialog);
		break;
	case MENUOP_GETSELECTEDINDEX:
		data->list.value = 0xfffff;
		break;
	}

	return 0;
}

static MenuItemHandlerResult menuhandlerSetupRename(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		menuPushDialog(&g_RenameSetupDialog);
	}

	return 0;
}

static MenuItemHandlerResult menuhandlerSetupDelete(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		menuPushDialog(&g_DeleteSetupDialog);
	}

	return 0;
}

static MenuItemHandlerResult menuhandlerSetupSetDefault(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		s32 selected = g_Menus[g_MpPlayerNum].mpsetup.slotindex;
		// clicked on "clear default"
		if (selected == g_MpSetupFile.defaultsetup - 1) {
			g_MpSetupFile.defaultsetup = 0;
			strcpy(g_LabelSetDefault, "Set Default\n");
		} else {
			g_MpSetupFile.defaultsetup = selected + 1;
			strcpy(g_LabelSetDefault, "Clear Default\n");
		}

		mpsetupSaveCurrentFile();
	}

	return 0;
}

/* public functions */

s32 mpsetupLoadCurrentFile(void)
{
	return mpsetupLoadFile(&g_MpSetupFile, MPSETUP_OP_DEFAULT);
}

s32 mpsetupSaveCurrentFile(void)
{
	g_MpSetupFile.version = MPSETUP_VERSION;
	s32 err = mpsetupSaveFile(MPSETUP_OP_DEFAULT, &g_MpSetupFile);

	// The [MpSetup.*] sections are bound with live pointers; mark the ini
	// dirty and let the save queue write it, which flushes profiles first.
	// Never configSave from here directly: a save before every section has
	// been bound drops the unbound ones - measured, an early save from the
	// probe lost every [MpPlayer.*] block. The guid, the one part that must
	// be on disk before anything names it, is in the block written above.
	saveQueueMarkConfig();

	return err;
}

s32 mpsetupSaveSetup(s32 slotindex, u8 savefile)
{
	struct savebuffer setup;

	// request to add a new setup
	if (slotindex == g_MpSetupFile.numsetups) {
		g_MpCurrentSetup = g_MpSetupFile.numsetups++;
	}

	mpsetupStoreExt(slotindex);

	savebufferClear(&setup);
	mpsetupfileSaveWad(&setup);

	memcpy(g_MpSetupFile.setups[slotindex].bytes, setup.bytes, MPSETUP_BLOCKSIZE);

	return savefile ? mpsetupSaveCurrentFile() : 0;
}

void mpsetupLoadSetup(s32 index)
{
	struct savebuffer buffer;
	savebufferClear(&buffer);
	struct setupblock *block = &g_MpSetupFile.setups[index];
	memcpy(&buffer.bytes, block->bytes, MPSETUP_BLOCKSIZE);
	mpsetupfileLoadWad(&buffer, g_MpSetupFile.version);
	g_MpCurrentSetup = index;
	mpsetupApplyExt(index);
}

void mpsetupCopyAllFromPak(void)
{
	if (fsFileSize("$S/" MPSETUP_FILENAME ".bin") > 0) {
		return;
	}

	filelistCreate(1, FILETYPE_MPSETUP);
	filelistsTick();

	for (int i = 0; i < g_FileLists[1]->numfiles; ++i) {
		struct savebuffer buffer;
		savebufferClear(&buffer);
		struct filelistfile *file = &g_FileLists[1]->files[i];
		s32 device = pakFindBySerial(file->deviceserial);
		s32 err = pakReadBodyAtGuid(device, file->fileid, buffer.bytes, 0);

		if (err != 0) {
			sysLogPrintf(LOG_ERROR, "Unable to read pak. device %d fileid %d deviceserial %d err %d",
				device, file->fileid, file->deviceserial, err);
			continue;
		}

		mpsetupfileLoadWad(&buffer, 0);

		// save the file when writing the last setup
		u8 savefile = i == g_FileLists[1]->numfiles - 1;
		mpsetupSaveSetup(g_MpSetupFile.numsetups, savefile);
	}

	// to reset the mp setup
	mpInit(false);
}

void mpsetupProbeFromArgs(void)
{
	if (!sysArgCheck("--mpsetup-probe")) {
		return;
	}

	if (mpsetupLoadFile(&g_MpSetupFile, MPSETUP_OP_DEFAULT) < 0) {
		sysLogPrintf(LOG_ERROR, "mpsetup-probe: no setup file");
		_exit(1);
	}

	sysLogPrintf(LOG_NOTE, "mpsetup-probe: file version %u, %u setup(s), default %u",
			g_MpSetupFile.version, g_MpSetupFile.numsetups, g_MpSetupFile.defaultsetup);

	for (s32 i = 0; i < g_MpSetupFile.numsetups; ++i) {
		mpsetupLoadSetup(i);
		sysLogPrintf(LOG_NOTE, "mpsetup-probe: %d '%s' guid %08x stage 0x%02x (%s) section [MpSetup.%08x] stage='%s'",
				i, g_MpSetup.name, g_MpSetupGuid, g_MpSetup.stagenum,
				stageGetName(g_MpSetup.stagenum) ? stageGetName(g_MpSetup.stagenum) : "?",
				g_MpSetupExt[i].guid, g_MpSetupExt[i].stage);
	}

	fflush(stdout);
	// Not exit(): main.c's atexit cleanup writes pd.ini unconditionally, and
	// a probe exits before the guest players' [MpPlayer.PlayerN] sections
	// are bound, so that write would drop them. Measured: 30 lines gone.
	_exit(0);
}

