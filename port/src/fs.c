// madvise() is a BSD/Linux extension that glibc hides behind a strict -std;
// this has to precede the first system header.
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <PR/ultratypes.h>
#include "constants.h"
#include "config.h"
#include "mod.h"
#include "system.h"
#include "platform.h"
#include "utils.h"
#include "fs.h"
#include "romdata.h"
#ifdef PLATFORM_WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#endif

#define DEFAULT_BASEDIR_NAME "data"

static char baseDir[FS_MAXPATH + 1]; // replaces $B
static char modDir[FS_MAXPATH + 1];  // replaces $M
static char saveDir[FS_MAXPATH + 1]; // replaces $S
static char homeDir[FS_MAXPATH + 1]; // replaces $H
static char exeDir[FS_MAXPATH + 1];  // replaces $E

char modDirs[64][FS_MAXPATH + 1];        // mod directories
static char gexModDir[FS_MAXPATH + 1];          // GoldenEye X Mod

u32 g_NumModDirs = 0;

// s32, not u32: src/include/data.h has always declared this `extern s32`, so
// every TU but this one already read it as signed, and port/src/mod.c leans on
// that - modSwitch() assigns modNumFromStage(), which returns -1 for a stage no
// mod claims, and repairs it with `if (g_ModNum < 0)`. Only fs.c saw the u32,
// which made the `g_ModNum >= 0` half of fsGetModDir()'s guard vacuously true;
// the guard survived a -1 solely because the other half then compared unsigned
// and 0xffffffff is not < 64.
//
// The two remaining unguarded uses in this file are modDirs[g_ModNum] in
// fsModFullPath(); they were out of bounds for a negative value under either
// type. modSwitch() repairs g_ModNum before it returns, and nothing between
// the assignment and the repair reaches fs.c, so neither is live today.
s32 g_ModNum = 0; // ie the boot mod

static s32 fsPathIsWritable(const char *path)
{
#ifdef PLATFORM_WIN32
	// on windows access() on directories will only check if the directory exists, so
	char tmp[FS_MAXPATH + 1] = { 0 };
	snprintf(tmp, sizeof(tmp), "%s/.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (f) {
		fclose(f);
		remove(tmp);
		return 1;
	}
	return 0;
#else
	return (access(path, W_OK) == 0);
#endif
}

s32 fsPathIsAbsolute(const char *path)
{
 return (path[0] == '/' || (isalpha(path[0]) && path[1] == ':'));
}

s32 fsPathIsCwdRelative(const char *path)
{
	// ., .., ./, ../
	return (path[0] == '.' && (path[1] == '.' || path[1] == '/' || path[1] == '\\' || path[1] == '\0'));
}
static inline const bool fsModFullPathCheck(const char *relPath, const char *modDir, char *pathBuf)
{
	if (modDir[0]) {
		if (!fsPathIsAbsolute(relPath)) {
			snprintf(pathBuf, FS_MAXPATH, "%s/%s", modDir, relPath);
		}
		if (fsFileSize(pathBuf) >= 0) {
			return true;
		}
	}

	return false;
}

static inline const bool fsModFullPath(char *pathBuf, const char *relPath)
{
	// if relPath doesn't contain textures/ or files/ subdir, don't even try to look in mod dirs
	// and return false
	// sysLogPrintf(LOG_NOTE, "fsModFullPath: relPath=%s\n", relPath);
	if (!strstr(relPath, "textures") && !strstr(relPath, "files") && !strstr(relPath, "modconfig.txt") && !strstr(relPath, "sequences")) {
		// Bare printf on the common path: this fires for every lookup that is
		// not a mod asset, which is most of them, and unlike the sysLogPrintf
		// calls around it nothing filters it. Same level as its siblings now.
		sysLogPrintf(LOG_NOTE, "fsModFullPath: %s is not a mod asset path", relPath);
		return false;
	}
	// sysLogPrintf(LOG_NOTE, "fsModFullPath: relPath=%s. Switch on g_ModNum\n", relPath, g_ModNum);
	// iterate over all mods, annd if fsModFullPathCheck returns true, return true immediately
	// otherwise return false at the end



	// for Lang files and special files, check all mods, starting wiwht mod 0
	if (strstr(relPath, "files/L") ||
				strstr(relPath, "files/C") ||
				strstr(relPath, "files/G") ||
				strstr(relPath, "files/U")
				){
		sysLogPrintf(LOG_NOTE, "fsModFullPath: files - checking current mod first\n");
		if (fsModFullPathCheck(relPath, modDirs[g_ModNum], pathBuf)) {
			sysLogPrintf(LOG_NOTE, "fsModFullPath: found in modDir=%s\n", modDirs[g_ModNum]);
			return true;
		}
		sysLogPrintf(LOG_NOTE, "fsModFullPath: not found in current mod, checking all mods in order\n");
		// modDirs[] is 0-based and only [0, g_NumModDirs) is populated; the
		// inclusive bound read one entry past the last mod, and modDirs is
		// exactly 64 rows, so with 64 --moddir entries it read off the array.
		// The sibling loop below already uses the exclusive bound.
		for (s32 i = 0; i < g_NumModDirs; ++i) {
			if (fsModFullPathCheck(relPath, (const char*)modDirs[i], pathBuf)) {
				sysLogPrintf(LOG_NOTE, "fsModFullPath: %s found in modDir=%s\n", relPath, modDirs[i]);
				return true;
			}
		}
		sysLogPrintf(LOG_NOTE, "fsModFullPath: not found in any mod\n");
		return false;
	} else {
		// Check current mod first
		if (fsModFullPathCheck(relPath, modDirs[g_ModNum], pathBuf)) {
			sysLogPrintf(LOG_NOTE, "fsModFullPath: found in modDir=%s\n", modDirs[g_ModNum]);
			return true;
		}

		// Fallback: Check all other mods
		// This ensures assets (like textures) in other mods (like AIO) are found
		// even if the current mod is different (e.g. boot mod)
		for (s32 i = 0; i < g_NumModDirs; ++i) {
			if (i == g_ModNum) continue;
			if (fsModFullPathCheck(relPath, (const char*)modDirs[i], pathBuf)) {
				sysLogPrintf(LOG_NOTE, "fsModFullPath: %s found in modDir=%s\n", relPath, modDirs[i]);
				return true;
			}
		}

		return false;
	}

}

const char *fsFullPath(const char *relPath)
{
	static char pathBuf[FS_MAXPATH + 1];

	if (relPath[0] == '$') {
		// expandable placeholder $X; will be replaced with the corresponding path, if any
		const char *expStr = NULL;
		switch (relPath[1]) {
			case 'E': expStr = exeDir; break;
			case 'H': expStr = homeDir; break;
			case 'M': expStr = modDir; break;
			case 'B': expStr = baseDir; break;
			case 'S': expStr = saveDir; break;
			default: break;
		}
		if (expStr) {
			const u32 len = strlen(expStr);
			if (len > 0) {
				memcpy(pathBuf, expStr, len);
				strncpy(pathBuf + len, relPath + 2, FS_MAXPATH - len);
				return pathBuf;
			}
		}
		// couldn't expand anything, return as is
		return relPath;
	} else if (!baseDir[0] || fsPathIsAbsolute(relPath) || fsPathIsCwdRelative(relPath)) {
		// user explicitly wants working directory or this is an absolute path or we have no baseDir set up yet
		return relPath;
	}

	// path relative to mod or base dir; this will be a read request, so check where the file actually is
	if (fsModFullPath(pathBuf, relPath)) {
		// found in mod dir
		return pathBuf;
	}

	// fall back to basedir
	snprintf(pathBuf, FS_MAXPATH, "%s/%s", baseDir, relPath);
	return pathBuf;
}



static inline void modDirInit(char* path, char* outModDir, s32 portable){
	if (path) {
		if (fsPathIsAbsolute(path) || fsPathIsCwdRelative(path) || path[0] == '$') {
			// path is explicit; check as-is
			if (fsFileSize(path) >= 0) {
				strncpy(outModDir, fsFullPath(path), FS_MAXPATH);
			}
		} else {
			// path is relative to workdir; try to find it
			const char *priority[] = { "$M",".", "$E", "$H" };
			for (s32 i = 0; i < 2 + (portable != 0); ++i) {
				sysLogPrintf(LOG_NOTE,"looking for moddir `%s` in: %s\n", path, priority[i]);
				char *tmp = strFmt("%s/%s", priority[i], path);
				if (fsFileSize(tmp) >= 0) {
					strncpy(outModDir, fsFullPath(tmp), FS_MAXPATH);
					break;
				}
			}
		}
		if (!outModDir[0]) {
			sysLogPrintf(LOG_WARNING, "could not find specified moddir `%s`", path);
			sysLogPrintf(LOG_WARNING, "outModDir: %s\n", outModDir);
		}
	}
}

/*
 * Modlists: named mod rosters in $B/mods/modlists.txt, in the modconfig
 * grammar, the shape of yadloom's wadLists/profiles:
 *
 *   modlist aio  { mod_aio_characters mod_aio_gex ... }
 *   modlist fojo { mod_fojo-stages-fojo mod_fojo-characters-port4p ... }
 *   modlist "fojo+aio" { $fojo $aio }
 *   default "fojo+aio"
 *
 * An entry is a folder name under $B/mods, a path (anything with a '/'), or
 * $NAME to pull in another modlist. The list is picked by --modlist NAME,
 * else pd.ini's [Game] ModList (default "fojo+aio"), else the file's
 * `default` when that key is empty. --moddir, when given at all, wins over
 * all of them, so a one-off roster still works from the command line.
 *
 * Order still matters the way it always has (index 0 is the boot mod's
 * fallback row, the "mod_fojo" substring in pdmain.c finds the first match),
 * so a list says its order out loud. A mod named twice through two lists is
 * kept once, where it first appears, and the repeat is logged.
 */
#define MODLIST_FNAME "$B/mods/modlists.txt"
#define MODLIST_MAX_DEPTH 8

// pd.ini [Game] ModList: the modlist mounted when no --modlist/--moddir is
// given. Read on its own by fsInit, which runs before configInit.
static char g_ModListName[64] = "fojo+aio";

PD_CONSTRUCTOR static void modListConfigInit(void)
{
	configRegisterString("Game.ModList", g_ModListName, sizeof(g_ModListName) - 1);
}

// The body of `modlist NAME { ... }`, positioned just after the brace.
static char *modListFind(char *data, const char *name, char *token)
{
	char *p = strParseToken(data, token, NULL);
	while (p && token[0]) {
		if (!strcmp(token, "modlist")) {
			char listname[64];
			p = strParseToken(p, token, NULL);
			strncpy(listname, strUnquote(token), sizeof(listname) - 1);
			listname[sizeof(listname) - 1] = '\0';
			p = strParseToken(p, token, NULL);
			if (!p || strcmp(token, "{")) {
				sysLogPrintf(LOG_ERROR, "modlist: '%s' has no opening brace", listname);
				return NULL;
			}
			if (!strcmp(listname, name)) {
				return p;
			}
			while (p && token[0] && strcmp(token, "}")) {
				p = strParseToken(p, token, NULL);
			}
		}
		p = strParseToken(p, token, NULL);
	}
	return NULL;
}

static s32 modListExpand(char *data, const char *name, s32 depth, s32 count, s32 max)
{
	char token[UTIL_MAX_TOKEN + 1];

	if (depth > MODLIST_MAX_DEPTH) {
		sysLogPrintf(LOG_ERROR, "modlist: '%s' nests more than %d deep; is it including itself?", name, MODLIST_MAX_DEPTH);
		return count;
	}

	char *p = modListFind(data, name, token);
	if (!p) {
		sysLogPrintf(LOG_ERROR, "modlist: no modlist named '%s' in %s", name, MODLIST_FNAME);
		return count;
	}

	while ((p = strParseToken(p, token, NULL)) && token[0] && strcmp(token, "}")) {
		char *entry = strUnquote(token);

		if (entry[0] == '$' && !strchr(entry, '/')) {
			char sub[64];
			strncpy(sub, entry + 1, sizeof(sub) - 1);
			sub[sizeof(sub) - 1] = '\0';
			count = modListExpand(data, sub, depth + 1, count, max);
			continue;
		}

		char dir[FS_MAXPATH + 1];
		if (strchr(entry, '/')) {
			snprintf(dir, sizeof(dir), "%s", entry);
		} else {
			snprintf(dir, sizeof(dir), "$B/mods/%s", entry);
		}

		bool dup = false;
		for (s32 i = 0; i < count; ++i) {
			if (!strcmp(modDirs[i], dir)) {
				dup = true;
				break;
			}
		}
		if (dup) {
			sysLogPrintf(LOG_WARNING, "modlist: '%s' is in the roster twice (again via '%s'); keeping the first", entry, name);
			continue;
		}
		if (count >= max) {
			sysLogPrintf(LOG_ERROR, "modlist: more than %d mods; '%s' and after dropped", max, entry);
			break;
		}
		strncpy(modDirs[count], dir, FS_MAXPATH);
		modDirs[count][FS_MAXPATH] = '\0';
		count++;
	}

	return count;
}

// Fill modDirs[] from the modlist named `name`, or the file's `default`.
static s32 modListLoad(const char *name, s32 max)
{
	u32 len = 0;
	char *data = fsFileSize(MODLIST_FNAME) >= 0 ? fsFileLoad(MODLIST_FNAME, &len) : NULL;
	if (!data) {
		sysLogPrintf(LOG_ERROR, "modlist: no %s and no --moddir, so no mods are mounted", MODLIST_FNAME);
		return 0;
	}

	char token[UTIL_MAX_TOKEN + 1];
	char deflt[64] = "";
	if (!name) {
		char *p = strParseToken(data, token, NULL);
		while (p && token[0]) {
			if (!strcmp(token, "default")) {
				p = strParseToken(p, token, NULL);
				strncpy(deflt, strUnquote(token), sizeof(deflt) - 1);
				break;
			}
			if (!strcmp(token, "modlist")) {
				// skip the name and the block, so a mod called "default" in a list is not misread
				p = strParseToken(p, token, NULL);
				p = strParseToken(p, token, NULL);
				while (p && token[0] && strcmp(token, "}")) {
					p = strParseToken(p, token, NULL);
				}
			}
			p = strParseToken(p, token, NULL);
		}
		if (!deflt[0]) {
			sysLogPrintf(LOG_ERROR, "modlist: no --modlist given and %s has no `default`", MODLIST_FNAME);
			sysMemFree(data);
			return 0;
		}
		name = deflt;
	}

	const s32 count = modListExpand(data, name, 0, 0, max);
	sysLogPrintf(LOG_NOTE, "modlist: '%s' mounts %d mods%s", name, count, deflt[0] ? " (the default)" : "");
	sysMemFree(data);
	return count;
}

s32 fsInit(void)
{
	sysGetExecutablePath(exeDir, FS_MAXPATH);

	// if this is set, default to exe path for everything
	const s32 portable = sysArgCheck("--portable");
	if (portable) {
		strcpy(homeDir, exeDir);
	} else {
		sysGetHomePath(homeDir, FS_MAXPATH);
	}

	// get path to base dir and expand it if needed
	const char *path = sysArgGetString("--basedir");
	if (!path) {
		path = "$H";
		// Prefer a `data/` directory next to the executable so launching
		// `pd.arm64` from outside the basedir still finds its bundled assets.
		// Only fall back to cwd or home when there is no executable-adjacent
		// bundle to use.
		if (fsFileSize("$E/" DEFAULT_BASEDIR_NAME) >= 0) {
			path = "$E/" DEFAULT_BASEDIR_NAME;
		} else
		if (!portable) {
			if (fsFileSize("./" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "./" DEFAULT_BASEDIR_NAME;
			} else if (fsFileSize("$H/" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "$H/" DEFAULT_BASEDIR_NAME;
			}
		}
	}
	strncpy(baseDir, fsFullPath(path), FS_MAXPATH);

	// get path to mod dir and expand it if needed
	// mod directory is overlaid on top of base directory

	s32 numModDirs = getModDirCount("--moddir", sizeof(modDirs)/sizeof(modDirs[0]));

	// get path to save dir and expand it if needed
	path = sysArgGetString("--savedir");
	if (!path) {
		if (portable) {
			path = "$E";
		} else {
#if defined(PLATFORM_LINUX) || defined(PLATFORM_OSX)
			// Prefer a config next to the executable/basedir bundle, then cwd, then homeDir.
			if (fsFileSize("$E/" CONFIG_FNAME) >= 0) {
				path = "$E";
			} else if (fsFileSize("./" CONFIG_FNAME) >= 0) {
				path = ".";
			} else {
				path = "$H";
			}
#else
			// check if working directory is writable, otherwise default to homeDir
			if (fsPathIsWritable("./")) {
				path = ".";
			} else {
				sysLogPrintf(LOG_WARNING, "cannot write to working directory, will use %s for saves instead", homeDir);
				path = "$H";
			}
#endif
		}
	}

	strncpy(saveDir, fsFullPath(path), FS_MAXPATH);
	for (s32 i = 0; i < g_NumModDirs; ++i) {
		sysLogPrintf(LOG_NOTE, " mod dir %d: %s", i, modDirs[i]);
	}

	sysLogPrintf(LOG_NOTE, "base dir: %s", baseDir);
	sysLogPrintf(LOG_NOTE, "save dir: %s", saveDir);

	// No --moddir: the roster comes from a modlist. --modlist wins, then
	// pd.ini's Game.ModList, then modlists.txt's `default`. pd.ini is not
	// loaded until configInit, which needs saveDir from above, so the one key
	// is read here on its own.
	if (numModDirs == 0) {
		const char *listname = sysArgGetString("--modlist");
		if (!listname) {
			if (fsFileSize(CONFIG_PATH) > 0) {
				configLoadKey(CONFIG_PATH, "Game.ModList");
			}
			if (g_ModListName[0]) {
				listname = g_ModListName;
			}
		}
		numModDirs = modListLoad(listname, sizeof(modDirs) / sizeof(modDirs[0]));
	}
	for (s32 i = 0; i < numModDirs; ++i) {
		sysLogPrintf(LOG_NOTE, " mod dir %d: %s", i, modDirs[i]);
	}
	fileSlotsInit(numModDirs);
	g_NumModDirs = numModDirs;

	return 0;
}

const char *fsGetModDir(void)
{
	// `<=` admitted modDirs[64] on a 64-row array; g_ModNum is a 0-based
	// modDirs subscript, so the last valid value is one below the count.
	// The `>= 0` half only started meaning anything once g_ModNum was defined
	// s32 here to match the `extern s32` every other TU already saw.
	if (g_ModNum >= 0 && g_ModNum < (s32)(sizeof(modDirs)/sizeof(modDirs[0]))) {
		if (modDirs[g_ModNum][0]) {
			return modDirs[g_ModNum];
		}
	}

	return NULL;
}

const char *fsGetBaseDir(void)
{
	return baseDir;
}

const char *fsGetSaveDir(void)
{
	return saveDir;
}

s32 fsFileLoadTo(const char *name, void *dst, u32 dstSize)
{
	const char *fullName = fsFullPath(name);

	FILE *f = fopen(fullName, "rb");
	if (!f) {
		// sysLogPrintf(LOG_ERROR, "fsFileLoadTo: could not find file: %s", fullName);
		return -1;
	}

	fseek(f, 0, SEEK_END);
	const s32 size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileLoadTo: empty file or invalid size (%d): %s", size, fullName);
		fclose(f);
		return -1;
	}

	if ((u32)size > dstSize) {
		sysLogPrintf(LOG_ERROR, "fsFileLoadTo: file too big for buffer (%u > %u): %s", size, dstSize, fullName);
		fclose(f);
		return -1;
	}

	fread(dst, 1, size, f);
	fclose(f);

	return size;
}

void *fsFileLoad(const char *name, u32 *outSize)
{
	const char *fullName = fsFullPath(name);

	FILE *f = fopen(fullName, "rb");
	if (!f) {
		sysLogPrintf(LOG_ERROR, "fsFileLoad: could not find file: %s", fullName);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	const s32 size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileLoad: empty file or invalid size (%d): %s", size, fullName);
		fclose(f);
		return NULL;
	}

	void *buf = NULL;
	if (size) {
		buf = sysMemZeroAlloc(size + 1); // sick hack for a free null terminator
		if (!buf) {
			sysLogPrintf(LOG_ERROR, "fsFileLoad: could not alloc %d bytes for file: %s", size, fullName);
			fclose(f);
			return NULL;
		}
		fread(buf, 1, size, f);
	}

	fclose(f);

	if (outSize) {
		*outSize = size;
	}

	return buf;
}

// A private, copy-on-write view of a whole file, for the ROM images.
//
// fsFileLoad() copies a file into the heap; for a 32 MiB ROM image that is a
// read of the whole thing at boot, once per image, before the first byte is
// wanted. The loader never needs the image as a buffer, only as a backing
// store: romdataFileLoad() hands out pointers into it and copies ranges out
// of it, which is the port's DMA. So the image is mapped instead, and pages
// arrive on first touch and stay shared with the page cache.
//
// The view is COPY-ON-WRITE, not read-only, on purpose: preprocessTexturesList
// rewrites the textureslist segment in place inside g_RomFile, and file
// preprocessors run over data that may alias the image. A written page is
// copied for this process; untouched pages are never copied. Nothing that
// relies on fsFileLoad()'s trailing NUL byte may use this - a mapping is
// exactly the file's length.
//
// Falls back to fsFileLoad() when the platform refuses the mapping, so a
// caller sees the same bytes either way; fsFileUnmap() tells the two apart.
void *fsFileMap(const char *name, u32 *outSize)
{
	const char *fullName = fsFullPath(name);
	void *view = NULL;
	u32 size = 0;

#ifdef PLATFORM_WIN32
	HANDLE f = CreateFileA(fullName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) {
		sysLogPrintf(LOG_ERROR, "fsFileMap: could not find file: %s", fullName);
		return NULL;
	}
	LARGE_INTEGER li;
	if (!GetFileSizeEx(f, &li) || li.QuadPart <= 0 || li.QuadPart > 0x7fffffff) {
		CloseHandle(f);
		sysLogPrintf(LOG_ERROR, "fsFileMap: empty file or invalid size: %s", fullName);
		return NULL;
	}
	size = (u32)li.QuadPart;
	HANDLE m = CreateFileMappingA(f, NULL, PAGE_WRITECOPY, 0, 0, NULL);
	if (m) {
		view = MapViewOfFile(m, FILE_MAP_COPY, 0, 0, 0);
		CloseHandle(m); // the view keeps the mapping alive
	}
	CloseHandle(f);
#else
	int fd = open(fullName, O_RDONLY);
	if (fd < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileMap: could not find file: %s", fullName);
		return NULL;
	}
	struct stat st;
	if (fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > 0x7fffffff) {
		close(fd);
		sysLogPrintf(LOG_ERROR, "fsFileMap: empty file or invalid size: %s", fullName);
		return NULL;
	}
	size = (u32)st.st_size;
	view = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (view == MAP_FAILED) {
		view = NULL;
	}
	close(fd); // the mapping keeps the file alive
#endif

	if (!view) {
		sysLogPrintf(LOG_WARNING, "fsFileMap: could not map %s, reading it instead", fullName);
		return fsFileLoad(name, outSize);
	}

	if (outSize) {
		*outSize = size;
	}

	return view;
}

// Give a mapping's resident pages back without unmapping it: the next touch
// pages them in from the file again. For a view that was walked once (a
// patch's checksum pass over its base) and will be read sparsely after.
// Only meaningful for a mapping; harmless on the fsFileLoad() fallback.
void fsFileMapRelease(void *p, u32 size)
{
	if (!p) {
		return;
	}
#ifdef PLATFORM_WIN32
	(void)size; // no equivalent worth having; the working set trims itself
#else
	// MAP_PRIVATE pages this process never wrote are discarded, not lost;
	// written (COW) pages are reset to the file's, which a caller that
	// releases must be fine with. On a heap fallback this fails EINVAL.
	madvise(p, size, MADV_DONTNEED);
#endif
}

void fsFileUnmap(void *p, u32 size)
{
	if (!p) {
		return;
	}
#ifdef PLATFORM_WIN32
	(void)size;
	if (!UnmapViewOfFile(p)) {
		sysMemFree(p); // it was the fsFileLoad() fallback
	}
#else
	if (munmap(p, size) < 0) {
		sysMemFree(p); // it was the fsFileLoad() fallback
	}
#endif
}

s32 fsFileSize(const char *name)
{
	const char *fullName = fsFullPath(name);
	struct stat st;
	if (stat(fullName, &st) < 0) {
		return -1;
	} else {
		return st.st_size;
	}
}

FILE *fsFileOpenWrite(const char *name)
{
	return fopen(fsFullPath(name), "wb");
}

FILE *fsFileOpenRead(const char *name)
{
	return fopen(fsFullPath(name), "rb");
}

/*
 * Atomic replace for save files (eeprom.bin, pd.ini).
 *
 * fsFileOpenWrite truncates first and writes second, so a crash, kill or power
 * loss in between leaves the file short or empty - the whole gamepak, or every
 * setting, not one record. This writes "<name>.tmp" instead, and commit
 * flushes it to disk and renames it over <name>. Rename replaces in one step
 * on POSIX; on Windows plain rename() refuses an existing target, so that
 * path is MoveFileEx with REPLACE_EXISTING. Either way a reader sees the old
 * file or the new one, never half of one.
 *
 * Not done: syncing the directory after the rename. Without it a power cut
 * right after a save can bring back the previous version - still whole.
 */
static void fsAtomicTmpPath(const char *name, char *out, size_t outlen)
{
	snprintf(out, outlen, "%s.tmp", fsFullPath(name));
}

FILE *fsFileOpenWriteAtomic(const char *name)
{
	char tmp[FS_MAXPATH + 8];
	fsAtomicTmpPath(name, tmp, sizeof(tmp));
	return fopen(tmp, "wb");
}

s32 fsFileCommitAtomic(FILE *f, const char *name)
{
	char tmp[FS_MAXPATH + 8];
	char dst[FS_MAXPATH + 1];
	s32 ok;

	fsAtomicTmpPath(name, tmp, sizeof(tmp));
	snprintf(dst, sizeof(dst), "%s", fsFullPath(name));

	ok = fflush(f) == 0;
#ifdef PLATFORM_WIN32
	ok = ok && _commit(_fileno(f)) == 0;
#else
	ok = ok && fsync(fileno(f)) == 0;
#endif
	ok = (fclose(f) == 0) && ok;

	if (ok) {
#ifdef PLATFORM_WIN32
		ok = MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
		ok = rename(tmp, dst) == 0;
#endif
	}

	if (!ok) {
		sysLogPrintf(LOG_ERROR, "fs: could not replace %s, left as it was", dst);
		remove(tmp);
		return 0;
	}

	return 1;
}

void fsFileFree(FILE *f)
{
	fclose(f);
}

s32 fsCreateDir(const char *path)
{
#ifdef PLATFORM_WIN32
	return _mkdir(fsFullPath(path));
#else
	return mkdir(fsFullPath(path), 0777);
#endif
}
