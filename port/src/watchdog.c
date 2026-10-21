/**
 * --watchdog <secs>: a stall tripwire for unattended runs. A side thread
 * watches the main loop's frame counter; if it stops moving for <secs>, the
 * main thread gets a SIGSEGV so the crash handler prints where it was stuck
 * (and the run ends instead of burning until the timeout). Off by default.
 */
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <PR/ultratypes.h>
#include "system.h"
#include "watchdog.h"

volatile u32 g_WatchdogFrame = 0;

static pthread_t s_mainThread;
static s32 s_stallSecs = 0;

static void *watchdogThread(void *arg)
{
	u32 last = g_WatchdogFrame;
	s32 still = 0;
	(void)arg;

	for (;;) {
		sleep(1);
		const u32 now = g_WatchdogFrame;
		if (now == last) {
			still++;
			if (still >= s_stallSecs) {
				fprintf(stderr, "watchdog: main loop stalled for %d s at frame %u - raising SIGSEGV on the main thread for a backtrace\n", still, now);
				fflush(stderr);
				pthread_kill(s_mainThread, SIGSEGV);
				return NULL;
			}
		} else {
			still = 0;
			last = now;
		}
	}
	return NULL;
}

void watchdogInit(void)
{
	const s32 secs = sysArgGetInt("--watchdog", 0);
	if (secs <= 0) {
		return;
	}
	s_stallSecs = secs;
	s_mainThread = pthread_self();
	pthread_t th;
	if (pthread_create(&th, NULL, watchdogThread, NULL) == 0) {
		pthread_detach(th);
		sysLogPrintf(LOG_NOTE, "watchdog: armed, %d s stall tolerance", secs);
	}
}
