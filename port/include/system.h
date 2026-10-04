#ifndef _IN_SYSTEM_H
#define _IN_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <PR/ultratypes.h>

enum LogLevel {
  LOG_NOTE,
  LOG_WARNING,
  LOG_ERROR,
};

void sysInitArgs(s32 argc, const char **argv);
void sysInit(void);

s32 getModDirCount(const char *arg, int max_values);

s32 sysArgCheck(const char *arg);
const char *sysArgGetString(const char *arg);
const char *sysArgGetString2(const char *arg);
s32 sysArgGetInt(const char *arg, s32 defval);

u64 sysGetMicroseconds(void);

void sysFatalError(const char *fmt, ...) __attribute__((noreturn));

s32 sysLogIsOpen(void);
void sysLogPrintf(s32 level, const char *fmt, ...);

// BRIEFDIAG (temporary): the null-briefing-buffer probe, card #364.
// Straight to stdout and flushed per line - no file open per call, and a
// redirected stdout is fully buffered, so an unflushed line dies with the crash.
#include <stdio.h>
// The #352 question: does a menu model buffer ever cover a live lang bank,
// and do the MPMENU offsets the picker reads move between calls.
void briefdiagCheck(const char *where);
#define BRIEFDIAG(fmt, ...) do { printf("BRIEFDIAG: " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while (0)
extern u8 g_SysLogToStderr;

void sysGetExecutablePath(char *outPath, const u32 outLen);
void sysGetHomePath(char *outPath, const u32 outLen);

void *sysMemAlloc(const u32 size);
void *sysMemZeroAlloc(const u32 size);
void *sysMemRealloc(void *ptr, const u32 newSize);
void sysMemFree(void *ptr);

// whole pages of their own, outside every pool, that can be made read-only.
// sysMemPagesAlloc rounds size up to the page size and returns zeroed,
// writable, page-aligned memory (NULL on failure). sysMemPagesProtect returns
// false where the platform cannot protect pages (the memory still works, it
// just is not guarded). pass the same size to all three.
void *sysMemPagesAlloc(const u32 size);
void sysMemPagesFree(void *ptr, const u32 size);
s32 sysMemPagesProtect(void *ptr, const u32 size, const s32 readonly);

// hns is specified in 100ns units
void sysSleep(const s64 hns);

// yield CPU if supported (e.g. during a busy loop)
void sysCpuRelax(void);

void crashInit(void);
void crashShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
