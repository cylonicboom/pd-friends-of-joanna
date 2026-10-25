#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "fs.h"

#ifdef PLATFORM_WIN32

#include <windows.h>

// on win32 we use waitable timers instead of nanosleep
typedef HANDLE WINAPI (*CREATEWAITABLETIMEREXAFN)(LPSECURITY_ATTRIBUTES, LPCSTR, DWORD, DWORD);
static HANDLE timer;
static CREATEWAITABLETIMEREXAFN pfnCreateWaitableTimerExA;

// winapi also provides a yield macro
#define DO_YIELD() YieldProcessor()

// ask system for high performance GPU, if any
__attribute__((dllexport)) u32 NvOptimusEnablement = 1;
__attribute__((dllexport)) u32 AmdPowerXpressRequestHighPerformance = 1;

#else

#include <unistd.h>

// figure out how to yield
#if defined(PLATFORM_X86) || defined(PLATFORM_X86_64)
// this should work even if the code is not built with SSE enabled, at least on gcc and clang,
// but if it doesn't we'll have to use  __builtin_ia32_pause() or something
#include <immintrin.h>
#define DO_YIELD() _mm_pause()
#elif defined(PLATFORM_ARM) && (defined(PLATFORM_64BIT) || PLATFORM_ARM == 7 || PLATFORM_ARM == 8)
// same as YieldProcessor() on ARM Windows
#define DO_YIELD() __asm__ volatile("dmb ishst\n\tyield":::"memory")
#else
// fuck it
#define DO_YIELD() do { } while (0)
#endif

#endif

#define LOG_FNAME "pd.log"
#define CRASHLOG_FNAME "pd.crash.log"
#define USEC_IN_SEC 1000000ULL

static u64 startTick = 0;
static char logPath[2048];

static s32 sysArgc;
static const char **sysArgv;

// Log rotation. logKeep is how many old logs sit beside pd.log (pd.log.1 is
// the newest); logMaxBytes rotates a running log once it passes that size
// (0 = only at startup). Both are 0 unless asked for, or unless this process
// is a dedicated server: a server runs for days through lobby loops and
// restarts, and the log is the only record of what happened on it.
static s32 logKeep = 0;
static u64 logMaxBytes = 0;
static u64 logBytes = 0;

static void sysLogRotate(void)
{
	char src[sizeof(logPath) + 16];
	char dst[sizeof(logPath) + 16];

	// drop the oldest, shift the rest up one, pd.log becomes pd.log.1.
	// remove before rename: windows' rename refuses an existing target.
	snprintf(dst, sizeof(dst), "%s.%d", logPath, logKeep);
	remove(dst);
	for (s32 i = logKeep - 1; i >= 1; --i) {
		snprintf(src, sizeof(src), "%s.%d", logPath, i);
		snprintf(dst, sizeof(dst), "%s.%d", logPath, i + 1);
		rename(src, dst);
	}
	snprintf(dst, sizeof(dst), "%s.1", logPath);
	rename(logPath, dst);
}

// open logPath for a fresh run: rotate what the last run left there, then
// truncate. Returns 0 if the location isn't writable.
static s32 sysLogStartAt(void)
{
	FILE *f = fopen(logPath, "ab");
	if (!f) {
		return 0;
	}
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fclose(f);

	if (size > 0 && logKeep > 0) {
		sysLogRotate();
	}

	f = fopen(logPath, "wb");
	if (f) {
		fclose(f);
	}
	logBytes = 0;
	return f != NULL;
}

static inline void sysLogSetPath(const char *fname)
{
	// figure out where the log is and clear it
	// try working dir first
	snprintf(logPath, sizeof(logPath), "./%s", fname);
	if (!sysLogStartAt()) {
		// try home dir
		sysGetHomePath(logPath, sizeof(logPath) - 1);
		strncat(logPath, "/", sizeof(logPath) - 1);
		strncat(logPath, fname, sizeof(logPath) - 1);
		sysLogStartAt();
	}
}

void sysInitArgs(s32 argc, const char **argv)
{
	sysArgc = argc;
	sysArgv = argv;
}

void sysInit(void)
{
	startTick = sysGetMicroseconds();

	// pre-release builds log by default (FOJO_LOG_DEFAULT from cmake): one
	// pd.log carrying the version line, the file loads and the crash dump
	// is what a tester can actually send back. --no-log turns it off there;
	// --log turns it on everywhere else.
	// A dedicated server (--dedicated / --dedicated-windowed that hosts, not
	// a --connect / --headless-client that joins) always logs, and rotates:
	// five old logs kept, a running log rotated past 16 MB. --no-log, and
	// --log-keep N / --log-max-mb N (0 = off), override; outside a server
	// those two flags turn rotation on for an ordinary --log.
	const s32 isServer = (sysArgCheck("--dedicated") || sysArgCheck("--dedicated-windowed"))
		&& !sysArgGetString("--connect") && !sysArgGetString("--headless-client");
#if defined(FOJO_LOG_DEFAULT) && FOJO_LOG_DEFAULT
	const s32 wantLog = !sysArgCheck("--no-log");
#else
	const s32 wantLog = !sysArgCheck("--no-log") && (sysArgCheck("--log") || isServer);
#endif
	logKeep = sysArgGetInt("--log-keep", isServer ? 5 : 0);
	const s32 maxmb = sysArgGetInt("--log-max-mb", isServer ? 16 : 0);
	logMaxBytes = maxmb > 0 ? (u64)maxmb * 1024 * 1024 : 0;
	if (logKeep < 0) {
		logKeep = 0;
	}
	if (logMaxBytes && logKeep == 0) {
		// a size cap with nowhere to rotate to would just lose the log
		logKeep = 1;
	}
	if (wantLog) {
		sysLogSetPath(LOG_FNAME);
	}

#ifdef VERSION_HASH
	sysLogPrintf(LOG_NOTE, "version: " VERSION_BRANCH " " VERSION_HASH " (" VERSION_TARGET ")");
#endif

	char timestr[256];
	const time_t curtime = time(NULL);
	strftime(timestr, sizeof(timestr), "%d %b %Y %H:%M:%S", localtime(&curtime));
	sysLogPrintf(LOG_NOTE, "startup date: %s", timestr);

#ifdef PLATFORM_WIN32
	// this function is only present on Vista+, so try to import it from kernel32 by hand
	pfnCreateWaitableTimerExA = (CREATEWAITABLETIMEREXAFN)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExA");
	if (pfnCreateWaitableTimerExA) {
		// function exists, try to create a hires timer
		timer = pfnCreateWaitableTimerExA(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	}
	if (!timer) {
		// no function or hires timers not supported, fallback to lower resolution timer
		sysLogPrintf(LOG_WARNING, "SYS: hires waitable timers not available");
		timer = CreateWaitableTimerA(NULL, FALSE, NULL);
	}
#endif
}

s32 sysArgCheck(const char *arg)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			return 1;
		}
	}
	return 0;
}
extern char modDirs[64][FS_MAXPATH + 1];        // mod directories
s32 getModDirCount(const char *arg, int max_values)
{
	s32 count = 0;
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			if (i < sysArgc - 1 && count < max_values) {
				strcpy(modDirs[count++], sysArgv[i + 1]);
				sysLogPrintf(LOG_NOTE, "Found arg %s: %s", arg, sysArgv[i + 1]);
			}
		}
	}
	return count;
}


const char *sysArgGetString(const char *arg)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			if (i < sysArgc - 1) {
				return sysArgv[i + 1];
			}
		}
	}
	return NULL;
}

s32 sysArgCount(void)
{
	return sysArgc;
}

const char *sysArgAt(s32 index)
{
	return (index >= 0 && index < sysArgc) ? sysArgv[index] : NULL;
}

// the token two after `arg`: `--asset ls <path>` wants the path
const char *sysArgGetString2(const char *arg)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			if (i < sysArgc - 2) {
				return sysArgv[i + 2];
			}
		}
	}
	return NULL;
}

s32 sysArgGetInt(const char *arg, s32 defval)
{
	for (s32 i = 1; i < sysArgc; ++i) {
		if (!strcasecmp(sysArgv[i], arg)) {
			if (i < sysArgc - 1) {
				return strtol(sysArgv[i + 1], NULL, 0);
			}
		}
	}
	return defval;
}

u64 sysGetMicroseconds(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((u64)tv.tv_sec * USEC_IN_SEC + (u64)tv.tv_usec) - startTick;
}

s32 sysLogIsOpen(void)
{
	return (logPath[0] != '\0');
}

// Set by a command-line mode whose stdout is a result stream (--asset):
// every log line goes to stderr so the result is the only thing on stdout.
u8 g_SysLogToStderr = 0;

void sysLogPrintf(s32 level, const char *fmt, ...)
{
	static const char *prefix[3] = {
		"", "WARNING: ", "ERROR: "
	};

	char logmsg[2048];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(logmsg, sizeof(logmsg), fmt, ap);
	va_end(ap);

	if (logPath[0]) {
		FILE *f = fopen(logPath, "ab");
		if (f) {
			const int n = fprintf(f, "%s%s\n", prefix[level], logmsg);
			fclose(f);
			if (n > 0) {
				logBytes += (u64)n;
			}
		}
		if (logMaxBytes && logBytes >= logMaxBytes) {
			sysLogRotate();
			logBytes = 0;
			f = fopen(logPath, "wb");
			if (f) {
				const int n = fprintf(f, "log rotated, earlier lines are in %s.1\n", logPath);
				fclose(f);
				logBytes = n > 0 ? (u64)n : 0;
			}
		}
	}

	FILE *fout = (level == LOG_NOTE && !g_SysLogToStderr) ? stdout : stderr;
	fprintf(fout, "%s%s\n", prefix[level], logmsg);
}

void sysFatalError(const char *fmt, ...)
{
	static s32 alreadyCrashed = 0;

	if (alreadyCrashed) {
		abort();
	}

	char errmsg[2048] = { 0 };

	alreadyCrashed = 1;

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(errmsg, sizeof(errmsg), fmt, ap);
	va_end(ap);

	sysLogPrintf(LOG_ERROR, "FATAL: %s", errmsg);

	fflush(stdout);
	fflush(stderr);

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", errmsg, NULL);

	exit(1);
}

void sysGetExecutablePath(char *outPath, const u32 outLen)
{
	// try asking SDL
	char *sdlPath = SDL_GetBasePath();

	if (sdlPath && *sdlPath) {
		// -1 to trim trailing slash
		const u32 len = strlen(sdlPath) - 1;
		if (len < outLen) {
			memcpy(outPath, sdlPath, len);
			outPath[len] = '\0';
		}
	} else if (sysArgc && sysArgv[0] && sysArgv[0][0]) {
		// get exe path from argv[0]
		strncpy(outPath, sysArgv[0], outLen - 1);
		outPath[outLen - 1] = '\0';
	} else if (outLen > 1) {
		// give up, use working directory instead
		outPath[0] = '.';
		outPath[1] = '\0';
	}

#ifdef PLATFORM_WIN32
	// replace all backslashes with forward slashes, windows supports both
	for (u32 i = 0; i < outLen && outPath[i]; ++i) {
		if (outPath[i] == '\\') {
			outPath[i] = '/';
		}
	}
#endif

	SDL_free(sdlPath);
}

void sysGetHomePath(char *outPath, const u32 outLen)
{
	// try asking SDL
	char *sdlPath = SDL_GetPrefPath("", "perfectdark-friends-of-joanna-v0.4");

	if (sdlPath && *sdlPath) {
		// -1 to trim trailing slash
		const u32 len = strlen(sdlPath) - 1;
		if (len < outLen) {
			memcpy(outPath, sdlPath, len);
			outPath[len] = '\0';
		}
	} else if (outLen > 1) {
		// give up, use working directory instead
		outPath[0] = '.';
		outPath[1] = '\0';
	}

#ifdef PLATFORM_WIN32
	// replace all backslashes with forward slashes, windows supports both
	for (u32 i = 0; i < outLen && outPath[i]; ++i) {
		if (outPath[i] == '\\') {
			outPath[i] = '/';
		}
	}
#endif

	SDL_free(sdlPath);
}

void *sysMemAlloc(const u32 size)
{
	return malloc(size);
}

void *sysMemZeroAlloc(const u32 size)
{
	return calloc(1, size);
}

void *sysMemRealloc(void *ptr, const u32 newSize)
{
	return realloc(ptr, newSize);
}

void sysMemFree(void *ptr)
{
	free(ptr);
}

#if defined(PLATFORM_POSIX) && !defined(PLATFORM_NSWITCH)
#include <sys/mman.h>
#endif

static u32 sysMemPageRound(const u32 size)
{
	u32 page = 4096;
#ifdef PLATFORM_WIN32
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	if (si.dwPageSize) {
		page = si.dwPageSize;
	}
#elif defined(PLATFORM_POSIX) && !defined(PLATFORM_NSWITCH)
	const long sc = sysconf(_SC_PAGESIZE);
	if (sc > 0) {
		page = (u32)sc;
	}
#endif
	return (size + page - 1) / page * page;
}

void *sysMemPagesAlloc(const u32 size)
{
	const u32 len = sysMemPageRound(size);
#ifdef PLATFORM_WIN32
	return VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(PLATFORM_POSIX) && !defined(PLATFORM_NSWITCH)
	void *ptr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return ptr == MAP_FAILED ? NULL : ptr;
#else
	return calloc(1, len);
#endif
}

void sysMemPagesFree(void *ptr, const u32 size)
{
	if (!ptr) {
		return;
	}
#ifdef PLATFORM_WIN32
	(void)size;
	VirtualFree(ptr, 0, MEM_RELEASE);
#elif defined(PLATFORM_POSIX) && !defined(PLATFORM_NSWITCH)
	munmap(ptr, sysMemPageRound(size));
#else
	(void)size;
	free(ptr);
#endif
}

s32 sysMemPagesProtect(void *ptr, const u32 size, const s32 readonly)
{
	if (!ptr) {
		return 0;
	}
#ifdef PLATFORM_WIN32
	DWORD old;
	return VirtualProtect(ptr, sysMemPageRound(size), readonly ? PAGE_READONLY : PAGE_READWRITE, &old) != 0;
#elif defined(PLATFORM_POSIX) && !defined(PLATFORM_NSWITCH)
	return mprotect(ptr, sysMemPageRound(size), readonly ? PROT_READ : (PROT_READ | PROT_WRITE)) == 0;
#else
	(void)size;
	(void)readonly;
	return 0;
#endif
}

void sysSleep(const s64 hns)
{
#ifdef PLATFORM_WIN32
	static LARGE_INTEGER li;
	li.QuadPart = -hns;
	SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE);
	WaitForSingleObject(timer, INFINITE);
#else
	const struct timespec spec = { 0, hns * 100 };
	nanosleep(&spec, NULL);
#endif
}

void sysCpuRelax(void)
{
	DO_YIELD();
}
