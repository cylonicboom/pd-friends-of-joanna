#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>
#include "system.h"
#include "platform.h"

#define CRASH_LOG_FNAME "pd.crash.log"
#define CRASH_MAX_MSG 8192
#define CRASH_MAX_SYM 256
#define CRASH_MAX_FRAMES 32
#define CRASH_MSG(...) \
	if (msglen < CRASH_MAX_MSG) msglen += snprintf(msg + msglen, CRASH_MAX_MSG - msglen, __VA_ARGS__)

#if defined(PLATFORM_WIN32) || defined(PLATFORM_LINUX)

// fojo (#352): the always-loaded text banks are kept read-only (game/lang.c),
// so a stray write into them faults right where it happens. say so in the
// report, or the fault reads like any other access violation.
extern u8 *g_LangRoBase;
extern u32 g_LangRoSize;
extern u8 *g_MemaGuardStart;
extern u32 g_MemaGuardSize;
s32 textFontRoContains(const void *addr);

static const char *crashFaultRegion(const void *addr)
{
	const uintptr_t a = (uintptr_t)addr;
	const uintptr_t base = (uintptr_t)g_LangRoBase;

	if (base && a >= base && a < base + g_LangRoSize) {
		return " -- inside the read-only text banks: PC / frame #00 is the stray writer (#352)";
	}

	if (g_MemaGuardStart && a >= (uintptr_t)g_MemaGuardStart && a < (uintptr_t)g_MemaGuardStart + g_MemaGuardSize) {
		return " -- in the guard past the mema heap: PC / frame #00 overran the heap (#352)";
	}

	if (textFontRoContains(addr)) {
		return " -- inside a read-only font: PC / frame #00 is the stray writer (#352)";
	}

	return "";
}

#endif

#if defined(PLATFORM_WIN32)

#include <windows.h>
#include <dbghelp.h>
#include <inttypes.h>
#include <excpt.h>

// NOTE: game builds with gcc, which means we have no PDBs for the windows version
// this means that you generally won't get any symbol names in the main executable

static LPTOP_LEVEL_EXCEPTION_FILTER prevExFilter;

static void *crashGetModuleBase(const void *addr)
{
	HMODULE h = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, addr, &h);
	return (void *)h;
}

static void crashStackTrace(char *msg, PEXCEPTION_POINTERS exinfo)
{
	CONTEXT context = *exinfo->ContextRecord;
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	SymSetOptions(SymGetOptions() | SYMOPT_DEBUG | SYMOPT_LOAD_LINES);
	SymInitialize(process, NULL, TRUE);

	STACKFRAME64 stackframe;
	memset(&stackframe, 0, sizeof(stackframe));

	DWORD image;
#ifdef PLATFORM_X86
	image = IMAGE_FILE_MACHINE_I386;
	stackframe.AddrPC.Offset = context.Eip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Ebp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Esp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#elif defined(PLATFORM_X86_64)
	image = IMAGE_FILE_MACHINE_AMD64;
	stackframe.AddrPC.Offset = context.Rip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Rsp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Rsp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#else
	snprintf(msg, CRASH_MAX_MSG, "no stack trace available on this arch\n");
	return;
#endif

	DWORD disp = 0;
	DWORD64 disp64 = 0;
	IMAGEHLP_LINE64 line;
	DWORD msglen = 0;

	CRASH_MSG("EXCEPTION: 0x%08lx\n", exinfo->ExceptionRecord->ExceptionCode);
	CRASH_MSG("PC: %p", exinfo->ExceptionRecord->ExceptionAddress);
	if (SymGetLineFromAddr64(process, (uintptr_t)exinfo->ExceptionRecord->ExceptionAddress, &disp, &line)) {
		CRASH_MSG(": %s:%lu+%lu", line.FileName, line.LineNumber, disp);
	}
	CRASH_MSG("\n");

	if (exinfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
			&& exinfo->ExceptionRecord->NumberParameters >= 2) {
		const ULONG_PTR access = exinfo->ExceptionRecord->ExceptionInformation[0];
		const void *addr = (const void *)exinfo->ExceptionRecord->ExceptionInformation[1];
		CRASH_MSG("FAULT: %s %p%s\n", access == 1 ? "write to" : access == 8 ? "execute at" : "read from",
				addr, crashFaultRegion(addr));
	}

	if (g_LangRoBase) {
		CRASH_MSG("LANGRO: [%p] +%08lx\n", (void *)g_LangRoBase, (unsigned long)g_LangRoSize);
	}

	CRASH_MSG("MODULE: [%p]\n", crashGetModuleBase(exinfo->ExceptionRecord->ExceptionAddress));
	CRASH_MSG("MAIN MODULE: [%p]\n", crashGetModuleBase(crashInit));

	// with no PDB the frames below are bare offsets, and offsets only tell
	// the truth when they are read against the exact executable that produced
	// them -- a different build of the same source resolves all of them and
	// lies about all of them. the linker timestamp and mapped size are per
	// build and can be read back out of the .exe, so printing them here is
	// enough to match a report to a binary. tools/pdsym checks this line.
	{
		const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)crashGetModuleBase(crashInit);
		if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE) {
			const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const u8 *)dos + dos->e_lfanew);
			if (nt->Signature == IMAGE_NT_SIGNATURE) {
				CRASH_MSG("IMAGE: link=%08lx size=%08lx sum=%08lx\n",
						(unsigned long)nt->FileHeader.TimeDateStamp,
						(unsigned long)nt->OptionalHeader.SizeOfImage,
						(unsigned long)nt->OptionalHeader.CheckSum);
			}
		}
	}

	CRASH_MSG("\nBACKTRACE:\n");

	char symbuf[sizeof(SYMBOL_INFO) + CRASH_MAX_SYM * sizeof(TCHAR)];
	PSYMBOL_INFO sym = (PSYMBOL_INFO)symbuf;
	sym->SizeOfStruct = sizeof(*sym);
	sym->MaxNameLen = CRASH_MAX_SYM;

	s32 i;
	for (i = 0; i < CRASH_MAX_FRAMES; ++i) {
		const BOOL res = StackWalk64(image, process, thread, &stackframe, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);
		if (!res) {
			break;
		}

		CRASH_MSG("#%02d: %p", i, (void *)(uintptr_t)stackframe.AddrPC.Offset);

		if (SymFromAddr(process, stackframe.AddrPC.Offset, &disp64, sym)) {
			CRASH_MSG(": %s+%llu", sym->Name, disp64);
		} else if (process, stackframe.AddrPC.Offset) {
			const uintptr_t modbase = (uintptr_t)crashGetModuleBase((void *)(uintptr_t)stackframe.AddrPC.Offset);
			const uintptr_t modofs = (uintptr_t)stackframe.AddrPC.Offset - modbase;
			CRASH_MSG(": [%p]+%p", (void *)modbase, (void *)modofs);
		}

		if(SymGetLineFromAddr64(process, stackframe.AddrPC.Offset, &disp, &line)) {
			CRASH_MSG(" (%s:%lu+%lu)", line.FileName, line.LineNumber, disp);
		}

		CRASH_MSG("\n");
	}

	if (i <= 1) {
		CRASH_MSG("no information\n");
	} else if (i == CRASH_MAX_FRAMES) {
		CRASH_MSG("...\n");
	}

	SymCleanup(process);
}

static long __stdcall crashHandler(PEXCEPTION_POINTERS exinfo)
{
	char msg[CRASH_MAX_MSG + 1] = { 0 };

	if (IsDebuggerPresent()) {
		if (prevExFilter) {
			return prevExFilter(exinfo);
		}
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	sysLogPrintf(LOG_ERROR, "FATAL: Crashed: PC=%p CODE=0x%08lx", exinfo->ExceptionRecord->ExceptionAddress, exinfo->ExceptionRecord->ExceptionCode);

	fflush(stderr);
	fflush(stdout);

	crashStackTrace(msg, exinfo);

	// open log file for the crash dump if one hasn't been opened yet
	if (!sysLogIsOpen()) {
		FILE *f = fopen(CRASH_LOG_FNAME, "wb");
		if (f) {
			fprintf(f, "Crash!\n\n%s", msg);
			fclose(f);
		}
	}

	sysFatalError("Crash!\n\n%s", msg);

	return EXCEPTION_CONTINUE_EXECUTION;
}

#elif defined(PLATFORM_LINUX)

#include <ucontext.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <ctype.h>
#include <dlfcn.h>
#include <sys/fcntl.h>

static struct sigaction prevSigAction;

static s32 crashIsDebuggerPresent(void)
{
	static s32 result = -1;

	if (result >= 0) {
		return result;
	}

	char buf[4096] = { 0 };

	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0) {
		result = 0;
		return 0;
	}

	int rx = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (rx <= 0) {
		result = 0;
		return 0;
	}

	buf[rx] = 0;

	char *str = strstr(buf, "TracerPid:");
	if (!str) {
		result = 0;
		return 0;
	}

	str += 10;

	while (*str && !isdigit(*str)) {
		if (*str == '\n') {
			result = 0;
			return 0;
		}
		++str;
	}

	result = (atoi(str) != 0);
	return result;
}

static void *crashGetModuleBase(const void *addr)
{
	Dl_info info;
	if (dladdr(addr, &info)) {
		return info.dli_fbase;
	}
	return NULL;
}

static void crashStackTrace(char *msg, s32 sig, void *pc, void *faultaddr)
{
	u32 msglen = 0;
	void *frames[CRASH_MAX_FRAMES] = { NULL };

	const s32 nframes = backtrace(frames, CRASH_MAX_FRAMES);
	if (nframes <= 0) {
		CRASH_MSG("no information\n");
		return;
	}

	char **strings = backtrace_symbols(frames, nframes);

	CRASH_MSG("SIGNAL: %d\n", sig);
	CRASH_MSG("PC: ");
	if (pc) {
		CRASH_MSG("%p\n", pc);
	} else if (strings) {
		CRASH_MSG("%s\n", strings[0]);
	} else {
		CRASH_MSG("%p\n", frames[0]);
	}

	if (sig == SIGSEGV || sig == SIGBUS) {
		CRASH_MSG("FAULT: %p%s\n", faultaddr, crashFaultRegion(faultaddr));
	}

	if (g_LangRoBase) {
		CRASH_MSG("LANGRO: %p +%08lx\n", (void *)g_LangRoBase, (unsigned long)g_LangRoSize);
	}

	CRASH_MSG("MODULE: %p\n", crashGetModuleBase(frames[0]));
	CRASH_MSG("MAIN MODULE: %p\n", crashGetModuleBase(crashInit));
	CRASH_MSG("\nBACKTRACE:\n");

	s32 i;
	for (i = 0; i < nframes; ++i) {
		CRASH_MSG("#%02d: ", i);
		if (strings && strings[i]) {
			CRASH_MSG("%s\n", strings[i]);
		} else {
			CRASH_MSG("%p\n", frames[i]);
		}
	}

	if (i == CRASH_MAX_FRAMES) {
		CRASH_MSG("...\n");
	} else if (i <= 1) {
		CRASH_MSG("no information\n");
	}

	free(strings);
}

static void crashHandler(s32 sig, siginfo_t *siginfo, void *ctx)
{
	char msg[CRASH_MAX_MSG + 1] = { 0 };

	if (crashIsDebuggerPresent()) {
		return;
	}

	void *pc = NULL;
	if (ctx) {
		ucontext_t *ucontext = (ucontext_t *)ctx;
#ifdef PLATFORM_X86
		pc = (void *)ucontext->uc_mcontext.gregs[REG_EIP];
#elif defined(PLATFORM_X86_64)
		pc = (void *)ucontext->uc_mcontext.gregs[REG_RIP];
#elif defined(PLATFORM_ARM) && defined(PLATFORM_64BIT)
		pc = (void *)ucontext->uc_mcontext.pc;
#elif defined(PLATFORM_ARM)
		pc = (void *)ucontext->uc_mcontext.arm_pc;
#endif
	}

	sysLogPrintf(LOG_ERROR, "FATAL: Crashed: PC=%p SIGNAL=%d", pc, sig);

	fflush(stderr);
	fflush(stdout);

	crashStackTrace(msg, sig, pc, siginfo ? siginfo->si_addr : NULL);

	sysFatalError("Crash!\n\n%s", msg);
}

#endif

s32 g_CrashEnabled = 0;

static char crashMsg[1024];

void crashInit(void)
{
#ifdef PLATFORM_WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS);
	prevExFilter = SetUnhandledExceptionFilter(crashHandler);
	g_CrashEnabled = 1;
#elif defined(PLATFORM_LINUX)
	struct sigaction sigact = { 0 };
	sigact.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigact.sa_sigaction = crashHandler;
	sigaction(SIGSEGV, &sigact, &prevSigAction);
	sigaction(SIGABRT, &sigact, &prevSigAction);
	sigaction(SIGBUS,  &sigact, &prevSigAction);
	sigaction(SIGILL,  &sigact, &prevSigAction);
	g_CrashEnabled = 1;
#endif
}

void crashShutdown(void)
{
	if (!g_CrashEnabled) {
		return;
	}
#ifdef PLATFORM_WIN32
	if (prevExFilter) {
		SetUnhandledExceptionFilter(prevExFilter);
	}
#elif defined(PLATFORM_LINUX)
	sigaction(SIGSEGV, &prevSigAction, NULL);
	sigaction(SIGABRT, &prevSigAction, NULL);
	sigaction(SIGBUS,  &prevSigAction, NULL);
	sigaction(SIGILL,  &prevSigAction, NULL);
#endif
	g_CrashEnabled = 0;
}

void crashCreateThread(void)
{

}

void crashSetMessage(char *string)
{

}

void crashReset(void)
{

}

void crashAppendChar(char c)
{

}
