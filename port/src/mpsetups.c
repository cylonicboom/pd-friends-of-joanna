#include "types.h"
#include "constants.h"
#include "bss.h"
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

extern struct menudialogdef g_MpSaveSetupNameMenuDialog;

#define MPSETUP_FILE "mpsetups.bin"
u8 filebuffer[sizeof(struct mpsetupfile)];

struct mpsetupfile g_MpSetupFile;
s16 g_MpCurrentSetup = -1;

#define BUF_READ(dst, buf, type) { dst = *(type*)((buf)); buf += sizeof(type); }
#define BUF_WRITE(src, buf, type) { *((type*)(buf)) = src; buf += sizeof(type); }
#define BUF_WRITEBLOCK(buf, src, size) { \
	memcpy(buf, src, size); \
	buf += size; }

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
			err = mpsetupSaveSetup(slotindex);
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

// #### Manage settings dialog
struct menudialogdef g_ManageSetupDialog;
MenuItemHandlerResult mpSelectSettingHandler(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GETOPTIONCOUNT:
		data->list.value = g_MpSetupFile.numsetups;
		break;
	case MENUOP_GETOPTIONTEXT:
		return (uintptr_t) g_MpSetupFile.setups[data->list.value].bytes;
	case MENUOP_SET:
	{
		g_Menus[g_MpPlayerNum].mpsetup.slotindex = data->list.value;
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
		0,
		160,
		0x00000042,
		mpSelectSettingHandler,
	},
	{ MENUITEMTYPE_END },

};

struct menudialogdef g_MpManageSettingsDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t) "Manage Settings\n",
	g_MpManageSettingsListItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT,
	NULL,
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

// -----------------------
static size_t readStr(char *dst, char *src)
{
	char *start = dst;
	while (*src) {
		*dst = *src;
		dst++;
		src++;
	}
	*dst = '\0';
	dst++;
	return dst - start;
}

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

static s32 mpsetupSaveFile();
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

	g_MpSetupFile.numsetups--;
	return mpsetupSaveFile();
}

static FILE *openMpSetupFile(u8 mode) {
	FILE *f;

	if (fsFileSize(MPSETUP_FILE) < 0) {
		// setup file doesn't exist: create one
		f = fsFileOpenWrite(MPSETUP_FILE);
		fsFileFree(f);
	}

	if (mode == 'r') {
		f = fsFileOpenRead(MPSETUP_FILE);
	}
	else if (mode == 'w') {
		f = fsFileOpenWrite(MPSETUP_FILE);
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

void zeroFileBuffer()
{
	memset(filebuffer, 0, sizeof(struct mpsetupfile));
}

s32 mpsetupLoadFile()
{
	FILE *f = openMpSetupFile('r');
	if (f == NULL) {
		return -1;
	}

	zeroFileBuffer();

	fseek(f, 0L, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);

	fread(filebuffer, 1, size, f);

	mpsetupDeserialize(&g_MpSetupFile, filebuffer);

	if (g_MpSetupFile.defaultsetup != 0) {
		g_MpCurrentSetup = g_MpSetupFile.defaultsetup;
	}

	fsFileFree(f);

	return 0;
}

static s32 mpsetupSaveFile()
{
	zeroFileBuffer();
	mpsetupSerialize(filebuffer, &g_MpSetupFile);

	FILE *f = openMpSetupFile('w');
	if (f == NULL) {
		return -1;
	}

	fwrite(filebuffer, sizeof(struct mpsetupfile), 1, f);
	fsFileFree(f);

	// reload the file to update in-memory structs
	return mpsetupLoadFile();
}

s32 mpsetupSaveSetup(s32 slotindex)
{
	struct savebuffer setup;

	// request to add a new setup
	if (slotindex == g_MpSetupFile.numsetups) {
		g_MpCurrentSetup = g_MpSetupFile.numsetups++;
	}

	savebufferClear(&setup);
	mpsetupfileSaveWad(&setup);

	memcpy(g_MpSetupFile.setups[slotindex].bytes, &setup.bytes, MPSETUP_BLOCKSIZE);

	return mpsetupSaveFile();
}

void mpsetupLoadSetup(s32 index)
{
	struct savebuffer buffer;
	savebufferClear(&buffer);
	struct setupblock *block = &g_MpSetupFile.setups[index];
	memcpy(&buffer.bytes, block->bytes, MPSETUP_BLOCKSIZE);
	mpsetupfileLoadWad(&buffer);
	g_MpCurrentSetup = index;
}
