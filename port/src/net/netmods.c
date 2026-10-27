#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "types.h"
#include "system.h"
#include "fs.h"
#include "mod.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmods.h"

// See netmods.h for the rules. Nothing in here knows about any particular mod.

struct netservermods g_NetServerMods;

static struct netmodlist g_NetModsLocal;
static s32 g_NetModsLocalBuilt = 0;

static u32 netModsFnv(u32 h, const u8 *data, u32 len)
{
	for (u32 i = 0; i < len; ++i) {
		h ^= data[i];
		h *= 16777619u;
	}
	return h;
}

// Folder name of a modDirs[] entry: the last path component, trailing
// separators ignored ("$B/mods/mod_fojo/" -> "mod_fojo").
static void netModsFolderName(const char *dir, char *out, u32 outlen)
{
	u32 end = (u32)strlen(dir);
	while (end > 0 && (dir[end - 1] == '/' || dir[end - 1] == '\\')) {
		--end;
	}
	u32 start = end;
	while (start > 0 && dir[start - 1] != '/' && dir[start - 1] != '\\') {
		--start;
	}
	u32 len = end - start;
	if (len >= outlen) {
		len = outlen - 1;
	}
	memcpy(out, dir + start, len);
	out[len] = '\0';
}

// Hashes one file into h. Returns 1 if the file existed.
static s32 netModsHashFile(const char *dir, const char *fname, u32 *h)
{
	char path[FS_MAXPATH];
	snprintf(path, sizeof(path), "%s/%s", dir, fname);
	u32 len = 0;
	u8 *data = fsFileLoad(path, &len);
	if (!data) {
		return 0;
	}
	*h = netModsFnv(*h, data, len);
	sysMemFree(data);
	return 1;
}

const struct netmodlist *netModsLocal(void)
{
	if (g_NetModsLocalBuilt) {
		return &g_NetModsLocal;
	}

	modScanAllMods();
	memset(&g_NetModsLocal, 0, sizeof(g_NetModsLocal));

	for (s32 i = 0; i < (s32)g_NumModDirs; ++i) {
		if (!modDirs[i][0]) {
			continue;
		}

		u32 h = 2166136261u;
		const s32 hasConfig = netModsHashFile(modDirs[i], "modconfig.txt", &h);
		const s32 hasTable = netModsHashFile(modDirs[i], "filetable.dat", &h);
		if (!hasConfig && !hasTable) {
			// Named on the roster (the shipped default lists dirs a basedir
			// may not carry) but nothing there: not mounted in any sense
			// that changes the game.
			continue;
		}

		if (g_NetModsLocal.count >= NET_MAX_MODS) {
			sysLogPrintf(LOG_WARNING, "NET: mod list: more than %d mods mounted, %s and later are not in the handshake",
					NET_MAX_MODS, modDirs[i]);
			break;
		}

		struct netmodentry *e = &g_NetModsLocal.mods[g_NetModsLocal.count++];
		netModsFolderName(modDirs[i], e->name, sizeof(e->name));
		if (i < 64) {
			snprintf(e->version, sizeof(e->version), "%s", g_ModVersions[i]);
		}
		e->hash = h;

		sysLogPrintf(LOG_NOTE, "NET: mod list: %s %s (%08x)%s",
				e->name, e->version[0] ? e->version : "(no version)", e->hash,
				hasConfig ? "" : " [no modconfig]");
	}

	g_NetModsLocalBuilt = 1;
	return &g_NetModsLocal;
}

u32 netModsWrite(struct netbuf *dst, const struct netmodlist *list)
{
	netbufWriteU8(dst, list->count);
	for (u8 i = 0; i < list->count; ++i) {
		netbufWriteStr(dst, list->mods[i].name);
		netbufWriteStr(dst, list->mods[i].version);
		netbufWriteU32(dst, list->mods[i].hash);
	}
	return dst->error;
}

u32 netModsRead(struct netbuf *src, struct netmodlist *out)
{
	memset(out, 0, sizeof(*out));
	const u8 count = netbufReadU8(src);
	if (src->error) {
		return src->error;
	}
	if (count > NET_MAX_MODS) {
		src->error = 1;
		return src->error;
	}
	for (u8 i = 0; i < count; ++i) {
		const char *name = netbufReadStr(src);
		const char *ver = netbufReadStr(src);
		const u32 hash = netbufReadU32(src);
		if (src->error || !name || !ver) {
			src->error = 1;
			return src->error;
		}
		snprintf(out->mods[i].name, sizeof(out->mods[i].name), "%s", name);
		snprintf(out->mods[i].version, sizeof(out->mods[i].version), "%s", ver);
		out->mods[i].hash = hash;
	}
	out->count = count;
	return src->error;
}

static const struct netmodentry *netModsFind(const struct netmodlist *list, const char *name)
{
	for (u8 i = 0; i < list->count; ++i) {
		if (strcasecmp(list->mods[i].name, name) == 0) {
			return &list->mods[i];
		}
	}
	return NULL;
}

static const char *netModsVer(const char *v)
{
	return v[0] ? v : "(no version)";
}

s32 netModsDiff(const struct netmodlist *mine, const struct netmodlist *theirs,
		const char *mineLabel, const char *theirLabel,
		s32 *outHashWarns, void (*emit)(const char *line, void *ctx), void *ctx)
{
	char line[NET_CHATLOG_LEN];
	s32 hard = 0;
	s32 warns = 0;

	for (u8 i = 0; i < mine->count; ++i) {
		const struct netmodentry *m = &mine->mods[i];
		const struct netmodentry *t = netModsFind(theirs, m->name);
		if (!t) {
			++hard;
			snprintf(line, sizeof(line), "mods: %s %s - only on %s",
					m->name, netModsVer(m->version), mineLabel);
		} else if (strcmp(m->version, t->version) != 0) {
			++hard;
			snprintf(line, sizeof(line), "mods: %s - %s: %s, %s: %s",
					m->name, mineLabel, netModsVer(m->version), theirLabel, netModsVer(t->version));
		} else if (m->hash != t->hash) {
			++warns;
			snprintf(line, sizeof(line), "mods: %s %s differs in content - %s: %08x, %s: %08x",
					m->name, netModsVer(m->version), mineLabel, m->hash, theirLabel, t->hash);
		} else {
			continue;
		}
		if (emit) {
			emit(line, ctx);
		}
	}

	for (u8 i = 0; i < theirs->count; ++i) {
		const struct netmodentry *t = &theirs->mods[i];
		if (!netModsFind(mine, t->name)) {
			++hard;
			snprintf(line, sizeof(line), "mods: %s %s - only on %s",
					t->name, netModsVer(t->version), theirLabel);
			if (emit) {
				emit(line, ctx);
			}
		}
	}

	if (outHashWarns) {
		*outHashWarns = warns;
	}
	return hard;
}

static struct netmodlist g_NetClientMods[NET_MAX_CLIENTS + 1];
static u8 g_NetClientModsValid[NET_MAX_CLIENTS + 1];

void netModsSetClient(u32 id, const struct netmodlist *list)
{
	if (id >= NET_MAX_CLIENTS + 1 || !list) {
		return;
	}
	g_NetClientMods[id] = *list;
	g_NetClientModsValid[id] = 1;
}

void netModsClearClient(u32 id)
{
	if (id >= NET_MAX_CLIENTS + 1) {
		return;
	}
	g_NetClientModsValid[id] = 0;
	memset(&g_NetClientMods[id], 0, sizeof(g_NetClientMods[id]));
}

const struct netmodlist *netModsClient(u32 id)
{
	if (id >= NET_MAX_CLIENTS + 1 || !g_NetClientModsValid[id]) {
		return NULL;
	}
	return &g_NetClientMods[id];
}

s32 netModsRows(const struct netmodlist *a, const struct netmodlist *b,
		struct netmodrow *rows, s32 maxrows)
{
	s32 n = 0;
	for (u8 i = 0; a && i < a->count && n < maxrows; ++i) {
		rows[n].a = &a->mods[i];
		rows[n].b = b ? netModsFind(b, a->mods[i].name) : NULL;
		++n;
	}
	for (u8 i = 0; b && i < b->count && n < maxrows; ++i) {
		if (a && netModsFind(a, b->mods[i].name)) {
			continue;
		}
		rows[n].a = NULL;
		rows[n].b = &b->mods[i];
		++n;
	}
	return n;
}
