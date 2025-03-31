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

/*
MP Setup File Format
	# header
	[version{1}]
	[defaultsetup{1}]
	[numsetups{1}]
	# setups
	[setup_1{64}]
	...
	[setup_n{64}]
 */

#define MPSETUP_VERSION 1

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

u8 filebuffer[sizeof(struct mpsetupfile)];

struct mpsetupfile g_MpSetupFile;
struct mpsetupfile g_ImportMpSetupFile;
s16 g_MpCurrentSetup = -1;
u64 g_MpImportExportFilter[2];

#define BUF_READ(dst, buf, type) { dst = *(type*)((buf)); buf += sizeof(type); }
#define BUF_WRITE(src, buf, type) { *((type*)(buf)) = src; buf += sizeof(type); }
#define BUF_WRITEBLOCK(buf, src, size) { \
	memcpy(buf, src, size); \
	buf += size; }

static inline void zeroFileBuffer()
{
	memset(filebuffer, 0, sizeof(struct mpsetupfile));
}

char g_StatusText[128];
struct menuitem g_StatusOkMenuItems[] = {
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

struct menudialogdef g_StatusOkDialog = {
	MENUDIALOGTYPE_SUCCESS,
	L_OPTIONS_345, // "Cool!"
	g_StatusOkMenuItems,
	NULL,
	MENUDIALOGFLAG_DISABLEBANNER,
	NULL,
};

struct menuitem g_StatusErrorMenuItems[] = {
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

struct menudialogdef g_StatusErrorDialog = {
		MENUDIALOGTYPE_DANGER,
		L_OPTIONS_277, // "Failed"
		g_StatusErrorMenuItems,
		NULL,
		MENUDIALOGFLAG_DISABLEBANNER,
		NULL,
};

// #### Rename setup dialog
MenuItemHandlerResult menuhandlerRenameSetup(s32 operation, struct menuitem *item, union handlerdata *data)
{
	char *name = data->keyboard.string;
	s32 slotindex = g_Menus[g_MpPlayerNum].mpsetup.slotindex;
	struct setupblock *setup = &g_MpSetupFile.setups[slotindex];
	s32 err;

	switch (operation) {
		case MENUOP_GETTEXT:
//			strcpy(name, g_MpSetup.name);
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
			}
			else {
				// TODO
//				menuPushDialog(&g_FilemgrFileSavedMenuDialog);
			}
			break;
	}

	return 0;
}

struct menuitem g_RenameSetupItems[] = {
#if VERSION != VERSION_JPN_FINAL
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LESSLEFTPADDING,
		L_MPMENU_189, // "Enter a name for your game setup file:"
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

struct menudialogdef g_RenameSetupDialog = {
		MENUDIALOGTYPE_DEFAULT,
		(uintptr_t)"Setup Name:\n",
		g_RenameSetupItems,
		NULL,
		MENUDIALOGFLAG_LITERAL_TEXT,
		NULL,
};

// #### Delete setup dialog
static s32 deleteSetup();
MenuItemHandlerResult menuhandlerDeleteSetup(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		s32 err = deleteSetup();
		if (!err) {
			menuPopDialog();
			menuPopDialog();
		}
		else {
			// TODO
		}
	}

	return 0;
}

struct menuitem g_DeleteSetupItems[] = {
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

struct menudialogdef g_DeleteSetupDialog = {
		MENUDIALOGTYPE_DANGER,
		(uintptr_t)"Delete Setup\n",
		g_DeleteSetupItems,
		NULL,
		MENUDIALOGFLAG_LITERAL_TEXT,
		NULL,
};

// #### Import (or Export) settings dialog
s32 importMpSetupFile(u8 op, u8 skipOverlap);
s32 exportMpSetupFile();
struct menudialogdef g_ManageSettingsDialog;
struct menudialogdef g_ImportOverrideDialog;
MenuItemHandlerResult menuhandlerImportOrExportSettings(s32 operation, struct menuitem *item, union handlerdata *data)
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
	s32 err;

	switch (operation) {
		case MENUOP_GETCOLOUR: {
			u32 colour = data->list.unk04;
			if (data->list.value == numitems - 1) {
				data->list.unk04 = hasselection ? colour : (colour & 0xffffff00) | 0x66;
			}
			break;
		}
		case MENUOP_GETOPTIONCOUNT:
			data->list.value = numitems;
			break;
		case MENUOP_GETOPTIONTEXT:
			if (data->list.value < numsetups) {
				return (uintptr_t) setupfile->setups[data->list.value].bytes;
			}
			else {
				// creates an offset to select the "Export" or "Import" label for the last item
				u8 ofs = (data->list.value == numitems - 1) ? op - 1 : 0;
				return (intptr_t)labels[data->list.value - numsetups + ofs];
			}
		case MENUOP_SET: {
			if (data->list.value < setupfile->numsetups) {
				g_MpImportExportFilter[bank] ^= (1 << bit);
			}
			else {
				s32 index = data->list.value - setupfile->numsetups;

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
						err = op == MPSETUP_OP_IMPORT ? importMpSetupFile(0, false) : exportMpSetupFile();
						if (!err) {
							menuPopDialog();
							if (op == MPSETUP_OP_IMPORT) {
								// back to the 'Manage Settings' screen
								menuPopDialog();
								menuPushDialog(&g_ManageSettingsDialog);
							}
							else {
								sprintf(g_StatusText, "File %s.bin\nwritten to the folder 'exported'\n", MPSETUP_FILENAME_EXP);
								menuPushDialog(&g_StatusOkDialog);
							}
						}
						else {
							if (err == MPSETUP_IMPORT_CONFLICT) {
								menuPushDialog(&g_ImportOverrideDialog);
							}
							else {
								menuPushDialog(&g_StatusErrorDialog);
							}
						}
					}
					break;
				}
			}
		}
		case MENUOP_GETSELECTEDINDEX:
			data->list.value = 0x000fffff;
			break;
		case MENUOP_GETLISTITEMCHECKBOX: {
			if (data->list.value < numsetups) {
				data->list.unk04 = (g_MpImportExportFilter[bank] & (1 << bit));
			}
			break;
		}
	}

	return 0;
}

struct menuitem g_ImportExportItems[] = {
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

char g_TitleImportExportDialog[20];
struct menudialogdef g_ImportExportDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t) g_TitleImportExportDialog,
	g_ImportExportItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
};


// #### Manage Import/Export settings dialog
s32 mpsetupLoadFile(struct mpsetupfile *setupfile, u8 op);
MenuItemHandlerResult menuhandlerOpenImportExportDialog(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
		case MENUOP_SET:
		{
			if (item->param == MPSETUP_OP_IMPORT) {
				strcpy(g_TitleImportExportDialog, "Import Settings\n");
				if (fsFileSize("$S/" MPSETUP_FILENAME_EXP ".bin") < 0) {
					sprintf(g_StatusText,
							"No import file found.\n"
							"Place the file %s.bin\n"
							"Next to your %s.bin file\n", MPSETUP_FILENAME_EXP, MPSETUP_FILENAME);
					menuPushDialog(&g_StatusErrorDialog);
					return 0;
				}

				mpsetupLoadFile(&g_ImportMpSetupFile, MPSETUP_OP_IMPORT);
			}
			else {
				strcpy(g_TitleImportExportDialog, "Export Settings\n");
			}
			g_Menus[g_MpPlayerNum].mpsetup.unke24 = item->param;
			g_MpImportExportFilter[0] = g_MpImportExportFilter[1] = -1;
			menuPushDialog(&g_ImportExportDialog);
		}
	}

	return 0;
}

struct menuitem g_ManageImportExportItems[] = {
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

struct menudialogdef g_ManageImportExportDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t) "Import/Export\n",
	g_ManageImportExportItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
};

// #### Manage settings dialog
struct menudialogdef g_ManageSetupDialog;
char g_LabelSetDefault[20] = "Set Default\n";
MenuItemHandlerResult mpSelectSetupHandler(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GETCOLOUR: {
		u32 colour = data->list.unk04;
		if (data->list.value + 1 == g_MpSetupFile.defaultsetup) {
			data->list.unk04 = (colour & 0xffff0000) | 0xff;
		}
		break;
	}
	case MENUOP_GETOPTIONCOUNT:
		data->list.value = g_MpSetupFile.numsetups;
		break;
	case MENUOP_GETOPTIONTEXT:
		return (uintptr_t) g_MpSetupFile.setups[data->list.value].bytes;
	case MENUOP_SET:
	{
		g_Menus[g_MpPlayerNum].mpsetup.slotindex = data->list.value;
		if (data->list.value == g_MpSetupFile.defaultsetup - 1) {
			strcpy(g_LabelSetDefault, "Clear Default\n");
		}
		else {
			strcpy(g_LabelSetDefault, "Set Default\n");
		}
		menuPushDialog(&g_ManageSetupDialog);
	}
	case MENUOP_GETSELECTEDINDEX:
		data->list.value = 0xfffff;
		break;
	}

	return 0;
}

struct menuitem g_MpManageSettingsListItems[] = {
	{
		MENUITEMTYPE_LIST,
		0,
		MENUITEMFLAG_LABEL_CUSTOMCOLOUR,
		160,
		0x00000042,
		mpSelectSetupHandler,
	},
	{ MENUITEMTYPE_END },

};

struct menudialogdef g_ManageSettingsDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t) "Manage Settings\n",
	g_MpManageSettingsListItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	&g_ManageImportExportDialog,
};

struct menudialogdef g_RenameSetupDialog;
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
			g_MpSetupFile.defaultsetup = -1;
			strcpy(g_LabelSetDefault, "Set Default\n");
		}
		else {
			g_MpSetupFile.defaultsetup = selected + 1;
			strcpy(g_LabelSetDefault, "Clear Default\n");
		}

		mpsetupSaveCurrentFile();
	}

	return 0;
}

struct menuitem g_ManageSetupItems[] = {
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

struct menudialogdef g_ManageSetupDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Manage Setup",
	g_ManageSetupItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
};

// #### Import: overwrite dialog
MenuItemHandlerResult menuhandlerImportAction(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		s32 err = importMpSetupFile(item->param, true);
		if (!err) {
			menuPopDialog();
			menuPushDialog(&g_ManageSettingsDialog);
		}
		else {
			menuPushDialog(&g_StatusErrorDialog);
		}
	}

	return 0;
}

struct menuitem g_ImportOverrideItems[] = {
	{
		MENUITEMTYPE_LABEL,
		0,
//		MENUITEMFLAG_LITERAL_TEXT | MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_SMALLFONT,
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

struct menudialogdef g_ImportOverrideDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Name Conflicts\n",
	g_ImportOverrideItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
};
// --------------------------------------------------------
static void mpsetupDeserialize(struct mpsetupfile *setupfile, u8 *buffer)
{
	BUF_READ(setupfile->version, buffer, u8);
	BUF_READ(setupfile->defaultsetup, buffer, u8);
	BUF_READ(setupfile->numsetups, buffer, u8);

	for (int i = 0; i < setupfile->numsetups; ++i) {
		memcpy(setupfile->setups[i].bytes, buffer, MPSETUP_BLOCKSIZE);
		buffer += MPSETUP_BLOCKSIZE;
	}
}

void mpsetupSerialize(u8 *buffer, struct mpsetupfile *setupfile)
{
	BUF_WRITE(setupfile->version, buffer, u8);
	BUF_WRITE(setupfile->defaultsetup, buffer, u8);
	BUF_WRITE(setupfile->numsetups, buffer, u8);

	for (int i = 0; i < setupfile->numsetups; ++i) {
		BUF_WRITEBLOCK(buffer, setupfile->setups[i].bytes, MPSETUP_BLOCKSIZE)
	}
}

static s32 mpsetupSaveFile(u8 op, struct mpsetupfile *setupfile);
static s32 deleteSetup()
{
	s32 slotindex = g_Menus[g_MpPlayerNum].mpsetup.slotindex;
	for (int i = slotindex; i < g_MpSetupFile.numsetups - 1; ++i) {
		u8* dst = g_MpSetupFile.setups[i].bytes;
		u8* src = g_MpSetupFile.setups[i+1].bytes;

		memcpy(dst, src, MPSETUP_BLOCKSIZE);
	}

	if (g_MpCurrentSetup > slotindex) {
		g_MpCurrentSetup--;
	}
	else if (g_MpCurrentSetup == slotindex) {
		g_MpCurrentSetup = -1;
	}

	 if (g_MpSetupFile.defaultsetup == slotindex + 1) {
		 g_MpSetupFile.defaultsetup = 0;
	 }

	g_MpSetupFile.numsetups--;
	return mpsetupSaveCurrentFile();
}

static FILE *openMpSetupFile(u8 mode, u8 op) {
	const char *filename = fsFullPath("$S/" MPSETUP_FILENAME ".bin");

	if (op == MPSETUP_OP_EXPORT) {
		// create export directory if it doesn't exist
		if (fsFileSize(MPSETUP_EXPORTDIR) < 0) {
			if (fsCreateDir(MPSETUP_EXPORTDIR) != 0) {
				return NULL;
			}
		}
		filename = fsFullPath(MPSETUP_EXPORTDIR MPSETUP_FILENAME_EXP ".bin");
	}
	else if (op == MPSETUP_OP_IMPORT) {
		// same name as export but different folder
		filename = fsFullPath("$S/" MPSETUP_FILENAME_EXP ".bin");
	}

	FILE *f;

	if (fsFileSize(filename) < 0) {
		// setup file doesn't exist: create one
		f = fsFileOpenWrite(filename);
		fsFileFree(f);
	}

	if (mode == 'r') {
		f = fsFileOpenRead(filename);
	}
	else if (mode == 'w') {
		f = fsFileOpenWrite(filename);
	}
	else {
		sysLogPrintf(LOG_ERROR, "openMpSetupFile: invalid file mode");
		return NULL;
	}

	if (f == NULL) {
		sysLogPrintf(LOG_ERROR, "Unable to open mp setup file");
		return NULL;
	}

	return f;
}

s32 mpsetupLoadFile(struct mpsetupfile *setupfile, u8 op)
{
	FILE *f = openMpSetupFile('r', op);
	if (f == NULL) {
		return -1;
	}

	zeroFileBuffer();

	fseek(f, 0L, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);

	fread(filebuffer, 1, size, f);

	mpsetupDeserialize(setupfile, filebuffer);

	if (op == MPSETUP_OP_DEFAULT && setupfile->defaultsetup > 0) {
		mpsetupLoadSetup(setupfile->defaultsetup - 1);
	}

	fsFileFree(f);

	return 0;
}

s32 mpsetupLoadCurrentFile()
{
	return mpsetupLoadFile(&g_MpSetupFile, MPSETUP_OP_DEFAULT);
}

s32 mpsetupSaveCurrentFile()
{
	g_MpSetupFile.version = MPSETUP_VERSION;
	return mpsetupSaveFile(MPSETUP_OP_DEFAULT, &g_MpSetupFile);
}

s32 importMpSetupFile(u8 op, u8 skipOverlap)
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
				sprintf(g_StatusText, "Number of setups exceeds %d\n", MPSETUP_MAXSETUPS);
				return -1;
			}
			importIdx = g_MpSetupFile.numsetups++;
		}

		memcpy(g_MpSetupFile.setups[importIdx].bytes, g_ImportMpSetupFile.setups[i].bytes, MPSETUP_BLOCKSIZE);
	}

	return mpsetupSaveCurrentFile();
}

s32 exportMpSetupFile()
{
	struct mpsetupfile expMpSetupFile;
	expMpSetupFile.numsetups = 0;
	
	u8 maxsetups = g_MpImportExportFilter[1] == 0 ? 64 : 128;
	maxsetups = MIN(maxsetups, g_MpSetupFile.numsetups);
	for (int i = 0; i < maxsetups; ++i) {
		u8 bank = i < 64 ? 0 : 1;
		u8 bit = i - bank * 64;
		if (g_MpImportExportFilter[bank] & (1 << bit)) {
			u8 n = expMpSetupFile.numsetups;
			expMpSetupFile.numsetups++;
			char *name = g_MpSetupFile.setups[i].bytes;
			sysLogPrintf(LOG_NOTE, "%s", name);
			memcpy(expMpSetupFile.setups[n].bytes, g_MpSetupFile.setups[i].bytes, MPSETUP_BLOCKSIZE);
			expMpSetupFile.numsetups = n + 1;
		}
	}

	s32 err = mpsetupSaveFile(MPSETUP_OP_EXPORT, &expMpSetupFile);
	if (err) {
		sprintf(g_StatusText, "Unable to write\nsetup file\n");
	}

	return err;
}

static s32 mpsetupSaveFile(u8 op, struct mpsetupfile *setupfile)
{
	zeroFileBuffer();
	mpsetupSerialize(filebuffer, setupfile);

	FILE *f = openMpSetupFile('w', op);
	if (f == NULL) {
		return -1;
	}

	size_t nwritten = fwrite(filebuffer, sizeof(struct mpsetupfile), 1, f);
	if (nwritten < 1) {
		fsFileFree(f);
		sysLogPrintf(LOG_ERROR, "Unable to write the MP setup file");
		return -1;
	}

	fsFileFree(f);

	// reload the file to update in-memory structs
	return mpsetupLoadCurrentFile();
}

s32 mpsetupSaveSetup(s32 slotindex, u8 savefile)
{
	struct savebuffer setup;

	// request to add a new setup
	if (slotindex == g_MpSetupFile.numsetups) {
		g_MpCurrentSetup = g_MpSetupFile.numsetups++;
	}

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
}

void mpsetupCopyAllFromPak()
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
	mpInit();
}
