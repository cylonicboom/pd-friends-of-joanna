#ifndef _IN_NET_NETMODS_H
#define _IN_NET_NETMODS_H

#include "types.h"

struct netbuf;

// The mod handshake: "I have these mods / this server has these mods".
//
// One entry per mounted mod dir that has a modconfig.txt or a filetable.dat.
// A mod's name on the wire is its FOLDER name (the basename of its modDirs[]
// entry), never the modconfig `modname` key - the folder name is the mod's
// internal name, and when the two disagree the folder wins.
//
// Match rule: the SET of {name, version} must be equal on both ends. Order is
// not compared - load order is treated as a bug where it matters, not as part
// of a mod's identity. (Until the precedence law is settled, two installs with
// the same set in a different --moddir order can still resolve a contested
// path differently. That is a known gap, not something this check covers.)
//
// The hash is FNV-1a 32 over the mod's modconfig.txt followed by its
// filetable.dat. It catches "same version, different build" - an edited
// modconfig or a rebuilt filetable - but NOT a changed blob under files/ that
// left the filetable byte-identical. A hash difference WARNS; it never kicks.
//
// Wire (rides CLC_AUTH after the password, and SVC_MODLIST):
//   u8 count, then count x { str name, str version, u32 hash }

#define NET_MAX_MODS 32
#define NET_MOD_NAMELEN 64
#define NET_MOD_VERLEN 32

struct netmodentry {
	char name[NET_MOD_NAMELEN];
	char version[NET_MOD_VERLEN];
	u32 hash;
};

struct netmodlist {
	u8 count;
	struct netmodentry mods[NET_MAX_MODS];
};

// The SVC_MODLIST verdict byte.
#define NETMODS_ACCEPTED 0
#define NETMODS_REJECTED 1

// This install's list, built on first call and cached (mods don't change
// under a running process).
const struct netmodlist *netModsLocal(void);

u32 netModsWrite(struct netbuf *dst, const struct netmodlist *list);
u32 netModsRead(struct netbuf *src, struct netmodlist *out);

// Compares `mine` against `theirs` and emits one line per difference through
// `emit` (may be NULL). `mineLabel` / `theirLabel` name the two sides in those
// lines ("server", "client 3", "you"). Returns the number of HARD differences
// (missing, extra, version); *outHashWarns, if given, gets the number of
// same-version entries whose hash differs.
s32 netModsDiff(const struct netmodlist *mine, const struct netmodlist *theirs,
		const char *mineLabel, const char *theirLabel,
		s32 *outHashWarns, void (*emit)(const char *line, void *ctx), void *ctx);

// Client side: the last SVC_MODLIST the server sent, for the lobby window to
// show. valid is 0 until one arrives. Kept after a rejection (so the reason
// can still be shown once the kick lands) and cleared when netStartClient
// opens the next connection.
struct netservermods {
	u8 valid;
	u8 verdict;
	struct netmodlist list;
};
extern struct netservermods g_NetServerMods;

// Server side: each admitted client's CLC_AUTH list, for the host's window.
// Set when the client is let in (so it matches the server's by {name,
// version}; only content hashes can differ), cleared on its disconnect.
// netModsClient returns NULL for an id with no list.
void netModsSetClient(u32 id, const struct netmodlist *list);
void netModsClearClient(u32 id);
const struct netmodlist *netModsClient(u32 id);

// One row per mod name in the union of two lists, for side-by-side display.
// a/b point at the entry on each side, NULL when that side lacks it.
struct netmodrow {
	const struct netmodentry *a;
	const struct netmodentry *b;
};
// Fills rows (a's order first, then b-only names) and returns the count.
s32 netModsRows(const struct netmodlist *a, const struct netmodlist *b,
		struct netmodrow *rows, s32 maxrows);

#endif
