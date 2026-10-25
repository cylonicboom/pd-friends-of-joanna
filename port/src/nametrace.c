#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "constants.h"
#include "bss.h"
#include "data.h"
#include "system.h"
#include "nametrace.h"

/*
 * NAMETRACE - temporary, see nametrace.h. Rip the whole file out (and every
 * call, `grep -rn nameTrace`) once the bug where a hard close eats a
 * profile's name is found.
 */

s32 g_NameTraceExiting = 0;

static u32 g_NameTraceFrame = 0;

#define NAMETRACE_SEATS MAX_PLAYERS

static char g_NameTraceLastName[NAMETRACE_SEATS][16];
static struct fileguid g_NameTraceLastGuid[NAMETRACE_SEATS];
static u8 g_NameTraceLastCtrl[NAMETRACE_SEATS];
static s32 g_NameTracePrimed = 0;

// 'catherine\n' [63 61 74 ...]
static void nameTraceFmt(const u8 *src, s32 len, char *out, size_t outlen)
{
	size_t o = 0;
	s32 i;
	s32 end = len;

	for (i = 0; i < len; i++) {
		if (src[i] == '\0') {
			end = i;
			break;
		}
	}

	o += snprintf(out + o, outlen - o, "'");

	for (i = 0; i < end && o + 6 < outlen; i++) {
		const u8 c = src[i];

		if (c == '\n') {
			o += snprintf(out + o, outlen - o, "\\n");
		} else if (c >= 0x20 && c < 0x7f) {
			out[o++] = (char)c;
			out[o] = '\0';
		} else {
			o += snprintf(out + o, outlen - o, "\\x%02x", c);
		}
	}

	o += snprintf(out + o, outlen - o, "' [");

	for (i = 0; i < len && o + 4 < outlen; i++) {
		o += snprintf(out + o, outlen - o, i ? " %02x" : "%02x", src[i]);
	}

	snprintf(out + o, outlen - o, "]");
}

void nameTracef(const char *fmt, ...)
{
	char line[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	sysLogPrintf(LOG_NOTE, "NAMETRACE f=%u exit=%d stage=0x%x %s",
			g_NameTraceFrame, g_NameTraceExiting, g_StageNum, line);
}

void nameTraceSeat(const char *what, s32 seat)
{
	char name[160];
	const struct mpplayerconfig *cfg;

	if (seat < 0 || seat >= MAX_MPPLAYERCONFIGS) {
		nameTracef("%s seat=%d (out of range)", what, seat);
		return;
	}

	cfg = &g_PlayerConfigsArray[seat];
	nameTraceFmt((const u8 *)cfg->base.name, sizeof(cfg->base.name), name, sizeof(name));
	nameTracef("%s seat=%d guid=%x-%x ctrl=%d cfgidx=%d name=%s",
			what, seat, cfg->fileguid.deviceserial, cfg->fileguid.fileid,
			cfg->controlmode, cfg->configindex, name);
}

void nameTraceBytes(const char *what, s32 seat, const u8 *bytes, s32 fileid, u16 serial, s32 extra)
{
	char name[160];

	nameTraceFmt(bytes, 10, name, sizeof(name));
	nameTracef("%s seat=%d file=%x-%x extra=%d wadname=%s", what, seat, serial, fileid, extra, name);
}

void nameTraceDumpSeats(const char *why)
{
	s32 i;
	char what[64];

	for (i = 0; i < NAMETRACE_SEATS; i++) {
		snprintf(what, sizeof(what), "%s", why);
		nameTraceSeat(what, i);
	}
}

void nameTraceWatch(void)
{
	s32 i;

	g_NameTraceFrame++;

	for (i = 0; i < NAMETRACE_SEATS; i++) {
		const struct mpplayerconfig *cfg = &g_PlayerConfigsArray[i];
		const s32 namechanged = memcmp(g_NameTraceLastName[i], cfg->base.name, sizeof(cfg->base.name)) != 0;
		const s32 guidchanged = g_NameTraceLastGuid[i].fileid != cfg->fileguid.fileid
			|| g_NameTraceLastGuid[i].deviceserial != cfg->fileguid.deviceserial;
		const s32 ctrlchanged = g_NameTraceLastCtrl[i] != cfg->controlmode;

		if (g_NameTracePrimed && (namechanged || guidchanged || ctrlchanged)) {
			char before[160];

			nameTraceFmt((const u8 *)g_NameTraceLastName[i], sizeof(cfg->base.name), before, sizeof(before));
			nameTracef("watch seat=%d was guid=%x-%x ctrl=%d name=%s",
					i, g_NameTraceLastGuid[i].deviceserial, g_NameTraceLastGuid[i].fileid,
					g_NameTraceLastCtrl[i], before);
			nameTraceSeat("watch   now", i);
		}

		memcpy(g_NameTraceLastName[i], cfg->base.name, sizeof(cfg->base.name));
		g_NameTraceLastGuid[i] = cfg->fileguid;
		g_NameTraceLastCtrl[i] = cfg->controlmode;
	}

	if (!g_NameTracePrimed) {
		g_NameTracePrimed = 1;
		// the anchor for resolving caller=%p: nm pd.arm64 | grep nameTraceWatch
		nameTracef("anchor nameTraceWatch=%p", (void *)nameTraceWatch);
		nameTraceDumpSeats("watch start");
	}
}
