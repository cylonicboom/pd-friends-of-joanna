#include "types.h"
#include "constants.h"
#include "game/menu.h"
#include "game/savebuffer.h"
#include "game/mplayer/mplayer.h"
#include <string.h>
#include "fs.h"
#include "system.h"

/*
MP Setup File Format
	# header
	[version{1}]
	[defaultsetup{1}]
	[numsetups{1}]
	[numgroups{1}]
	# setups
	[setup_1{64}]
	...
	[setup_n{64}]
	# groups
	[offset_index_1{1}]
	[name_1{1-32}]
	...
	[offset_index_n{1}]
	[name_n{1-32}]
 */

extern struct menudialogdef g_MpSaveSetupNameMenuDialog;
struct menudialogdef g_MpGroupNameMenuDialog;

#define MPSETUP_FILE "mpsetups.bin"
u8 filebuffer[sizeof(struct mpsetupfile)];

struct mpsetupfile g_MpSetupFile;
s16 g_MpCurrentSetup = -1;
s16 g_MpCurrentGroup = -1;

#define BUF_READ(dst, buf, type) { dst = *(type*)((buf)); buf += sizeof(type); }
#define BUF_WRITE(src, buf, type) { *((type*)(buf)) = src; buf += sizeof(type); }
#define BUF_WRITEBLOCK(buf, src, size) { \
	memcpy(buf, src, size); \
	buf += size; }

// #### Group List
MenuItemHandlerResult mpSelectGroupHandler(s32 operation, struct menuitem *item, union handlerdata *data);

struct menuitem g_MpSetupGroupsDialogItems[] = {
	{
		MENUITEMTYPE_LIST,
		0,
		MENUITEMFLAG_LABEL_CUSTOMCOLOUR,
		0x00000078,
		0x00000042,
		mpSelectGroupHandler,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_MpSetupGroupsDialog = {
		MENUDIALOGTYPE_DEFAULT,
		(uintptr_t) "Groups",
		g_MpSetupGroupsDialogItems,
		NULL,
		MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_CLOSEONSELECT,
		NULL,
};

MenuItemHandlerResult mpSelectGroupHandler(s32 operation, struct menuitem *item, union handlerdata *data)
{
	switch (operation) {
	case MENUOP_GETCOLOUR: {
		u8 disabled = data->list.value == g_MpSetupFile.numgroups && g_MpSetupFile.numsetups == 0;
		u32 colour = data->list.unk04;
		data->list.unk04 = disabled ? 0xff333300 | (colour & 0xff) : colour;
	}
	case MENUOP_GETOPTIONCOUNT:
		data->list.value = g_MpSetupFile.numgroups + 1;
		break;
	case MENUOP_GETOPTIONTEXT:
		if (data->list.value == g_MpSetupFile.numgroups) {
			return (uintptr_t) "[New Group]\n";
		}
		return (uintptr_t) g_MpSetupFile.groups[data->list.value].name;
	case MENUOP_SET:
	{
		u8 newgroupdisabled = data->list.value == g_MpSetupFile.numgroups && g_MpSetupFile.numsetups == 0;
		if (newgroupdisabled) {
			return 0;
		}

		menuPopDialog();
		if (data->list.value == g_MpSetupFile.numgroups) {
			menuPushDialog(&g_MpGroupNameMenuDialog);
		}
		else {
			g_MpCurrentGroup = data->list.value;
			menuPushDialog(&g_MpSaveSetupNameMenuDialog);
		}
	}
	case MENUOP_GETSELECTEDINDEX:
		data->list.value = 0xfffff;
		break;
	}

	return 0;
}

static void createGroup(char *name)
{
	g_MpCurrentGroup = g_MpSetupFile.numgroups++;
	struct setupgroup *group = &g_MpSetupFile.groups[g_MpCurrentGroup];
	strcpy(group->name, name);
	group->offset_index = g_MpSetupFile.numsetups;
}

// #### Group name dialog
MenuItemHandlerResult menuhandlerMpSetupGroupName(s32 operation, struct menuitem *item, union handlerdata *data)
{
	char *name = data->keyboard.string;
	struct setupgroup *group = &g_MpSetupFile.groups[g_MpSetupFile.numgroups];

	switch (operation) {
		case MENUOP_GETTEXT:
			strcpy(name, group->name);
			break;
		case MENUOP_SETTEXT:
			strcpy(group->name, name);
			break;
		case MENUOP_SET:
			createGroup(name);
			menuPushDialog(&g_MpSaveSetupNameMenuDialog);
			break;
	}

	return 0;
}

struct menuitem g_MpGroupNameMenuItems[] = {
#if VERSION != VERSION_JPN_FINAL
		{
				MENUITEMTYPE_LABEL,
				0,
				MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_LITERAL_TEXT,
				(uintptr_t) "Enter the group name:\n",
				0,
				NULL,
		},
#endif
		{
				MENUITEMTYPE_KEYBOARD,
				0,
				0,
				0,
				0,
				menuhandlerMpSetupGroupName,
		},
		{ MENUITEMTYPE_END },
};

struct menudialogdef g_MpGroupNameMenuDialog = {
		MENUDIALOGTYPE_DEFAULT,
		(uintptr_t) "Group Name\n",
		g_MpGroupNameMenuItems,
		NULL,
		MENUDIALOGFLAG_LITERAL_TEXT,
		NULL,
};


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

static void mpsetupfileRead(struct mpsetupfile *setupfile, u8 *buffer)
{
	BUF_READ(setupfile->version, buffer, u8);
	BUF_READ(setupfile->defaultsetup, buffer, u8);
	BUF_READ(setupfile->numsetups, buffer, u8);
	BUF_READ(setupfile->numgroups, buffer, u8);

	for (int i = 0; i < setupfile->numsetups; ++i) {
		memcpy(setupfile->setups[i].bytes, buffer, MPSETUP_BLOCKSIZE);
		buffer += MPSETUP_BLOCKSIZE;
	}

	for (int i = 0; i < setupfile->numgroups; ++i) {
		struct setupgroup *group = &setupfile->groups[i];
		BUF_READ(group->offset_index, buffer, u8);
		buffer += readStr(group->name, buffer);
	}
}

void mpsetupfileWrite(u8 *buffer, struct mpsetupfile *setupfile, struct savebuffer *setup, u8 setupidx, u8 overwrite)
{
	BUF_WRITE(setupfile->version, buffer, u8);
	BUF_WRITE(setupfile->defaultsetup, buffer, u8);
	BUF_WRITE(setupfile->numsetups, buffer, u8);
	BUF_WRITE(setupfile->numgroups, buffer, u8);

	for (int i = 0; i < setupfile->numsetups; ++i) {
		if (i == setupidx) {
			BUF_WRITEBLOCK(buffer, setup->bytes, MPSETUP_BLOCKSIZE);

			if (!overwrite) {
				BUF_WRITEBLOCK(buffer, setupfile->setups[i].bytes, MPSETUP_BLOCKSIZE)
			}
			continue;
		}

		BUF_WRITEBLOCK(buffer, setupfile->setups[i].bytes, MPSETUP_BLOCKSIZE)
	}

	for (int i = 0; i < setupfile->numgroups; ++i) {
		struct setupgroup *group = &setupfile->groups[i];
		BUF_WRITE(group->offset_index, buffer, u8);
		buffer += readStr(buffer, group->name);
	}
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
		sysLogPrintf(LOG_NOTE, "Unable to open mp setup file");
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

	mpsetupfileRead(&g_MpSetupFile, filebuffer);

	if (g_MpSetupFile.defaultsetup != 0) {
		g_MpCurrentSetup = g_MpSetupFile.defaultsetup;
	}

	// setup a default group if there is none
	if (g_MpSetupFile.numgroups == 0) {
		g_MpSetupFile.numgroups++;
		strcpy(g_MpSetupFile.groups[0].name, "Default\n");
		g_MpSetupFile.groups[0].offset_index = 0;
		g_MpCurrentSetup = -1;
	}

	fsFileFree(f);

	return 0;
}

static s32 getNewSetupIndex()
{
	if (g_MpSetupFile.numsetups == 0) {
		return 0;
	}

	if (g_MpCurrentGroup == g_MpSetupFile.numgroups - 1)
		return g_MpSetupFile.numsetups;

	return g_MpSetupFile.groups[g_MpCurrentGroup + 1].offset_index;
}

static s32 getGroupIndex(s32 setupIndex)
{
	s32 index = 0;
	struct setupgroup *groups = g_MpSetupFile.groups;
	for (int i = 0; i < g_MpSetupFile.numgroups; ++i) {
		if (setupIndex >= groups[i].offset_index) {
			index = groups[i].offset_index;
		}
	}

	return index;
}

s32 mpsetupSaveFile(const char *name, u8 overwrite)
{
	struct savebuffer setup;

	if (!overwrite) {
		g_MpCurrentSetup = getNewSetupIndex();
		g_MpSetupFile.numsetups++;

		for (int i = g_MpCurrentGroup + 1; i < g_MpSetupFile.numgroups; ++i) {
			g_MpSetupFile.groups[i].offset_index++;
		}
	}

	savebufferClear(&setup);
	mpsetupfileSaveWad(&setup);

	zeroFileBuffer();
	mpsetupfileWrite(filebuffer, &g_MpSetupFile, &setup, g_MpCurrentSetup, overwrite);

	FILE *f = openMpSetupFile('w');
	if (f == NULL) {
		return -1;
	}

	fwrite(filebuffer, sizeof(struct mpsetupfile), 1, f);
	fsFileFree(f);

	// reload the file to update in-memory structs
	mpsetupLoadFile();
	return 0;
}

void mpsetupLoadSetup(s32 index)
{
	struct savebuffer buffer;
	savebufferClear(&buffer);
	struct setupblock *block = &g_MpSetupFile.setups[index];
	memcpy(&buffer.bytes, block->bytes, MPSETUP_BLOCKSIZE);
	mpsetupfileLoadWad(&buffer);
	g_MpCurrentSetup = index;
	g_MpCurrentGroup = getGroupIndex(index);
}
