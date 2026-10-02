#include <stdio.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "data.h"
#include "bss.h"
#include "constants.h"
#include "textrace.h"

/*
 * See textrace.h. The ring is static BSS and recording is a store and an
 * increment, so a traced run has the same I/O pattern as an untraced one.
 * When the ring wraps, the oldest records are lost and the dump says so.
 */

#define TEXTRACE_RING 8192

struct textracerec {
	s32 frame;
	s32 kind;
	s32 v[5];
};

u8 g_TexTraceEnabled = 0;

static struct textracerec s_Ring[TEXTRACE_RING];
static u32 s_Count = 0;

static const char *s_KindNames[] = {
	"modeldef", "modeldef_end", "modtex", "portmiss", "poolfull", "drop", "cacheclr", "menumodel", "menupool",
	"import", "importpal",
};

static const char *s_LaneNames[] = {
	"nomodctx", "nofile", "loadnull", "baserom", "toolarge", "bytes", "loose", "miss",
};

void texTraceInit(void)
{
	g_TexTraceEnabled = sysArgCheck("--textrace") ? 1 : 0;
	if (g_TexTraceEnabled) {
		sysLogPrintf(LOG_NOTE, "textrace: recording, dump at shutdown to $S/textrace.txt");
	}
}

void texTraceRecord(s32 kind, s32 a, s32 b, s32 c, s32 d, s32 e)
{
	struct textracerec *r = &s_Ring[s_Count % TEXTRACE_RING];
	r->frame = g_Vars.lvframenum;
	r->kind = kind;
	r->v[0] = a;
	r->v[1] = b;
	r->v[2] = c;
	r->v[3] = d;
	r->v[4] = e;
	s_Count++;
}

void texTraceDump(void)
{
	if (!g_TexTraceEnabled) {
		return;
	}

	const char *path = fsFullPath("$S/textrace.txt");
	FILE *f = fopen(path, "wb");
	if (!f) {
		sysLogPrintf(LOG_ERROR, "textrace: could not open %s", path);
		return;
	}

	u32 first = (s_Count > TEXTRACE_RING) ? s_Count - TEXTRACE_RING : 0;
	fprintf(f, "# textrace: %u records, %u kept%s\n", s_Count, s_Count - first,
			first ? " (ring wrapped, oldest lost)" : "");
	fprintf(f, "# frame kind values...\n");

	for (u32 i = first; i < s_Count; ++i) {
		const struct textracerec *r = &s_Ring[i % TEXTRACE_RING];
		const char *kn = (r->kind >= 0 && r->kind < (s32)ARRAYCOUNT(s_KindNames)) ? s_KindNames[r->kind] : "?";

		switch (r->kind) {
		case TEXTRACE_MODELDEF:
			fprintf(f, "%6d %-12s fileid=0x%08x modIdx=%d raw=0x%04x\n", r->frame, kn, r->v[0], r->v[1], r->v[2]);
			break;
		case TEXTRACE_MODELDEF_END:
			fprintf(f, "%6d %-12s fileid=0x%08x loadedsize=0x%x\n", r->frame, kn, r->v[0], r->v[1]);
			break;
		case TEXTRACE_MODTEX: {
			const char *ln = (r->v[4] >= 0 && r->v[4] < (s32)ARRAYCOUNT(s_LaneNames)) ? s_LaneNames[r->v[4]] : "?";
			fprintf(f, "%6d %-12s tex=0x%04x mod=%d model=0x%04x file=%d lane=%s\n", r->frame, kn, r->v[0], r->v[1], r->v[2], r->v[3], ln);
			break;
		}
		case TEXTRACE_PORTMISS:
			fprintf(f, "%6d %-12s tex=0x%04x texmod=%d shared=%d\n", r->frame, kn, r->v[0], r->v[1], r->v[2]);
			break;
		case TEXTRACE_POOLFULL:
			fprintf(f, "%6d %-12s tex=0x%04x free=%d zlib=%d\n", r->frame, kn, r->v[0], r->v[1], r->v[2]);
			break;
		case TEXTRACE_DROP:
			fprintf(f, "%6d %-12s tex=0x%04x slot=%d texmod=%d model=0x%04x\n", r->frame, kn, r->v[0], r->v[1], r->v[2], r->v[3]);
			break;
		case TEXTRACE_MENUMODEL:
			fprintf(f, "%6d %-12s alloclen=0x%x totalfilelen=0x%x bodyfinal=0x%x headfinal=0x%x poolused=0x%x\n", r->frame, kn, r->v[0], r->v[1], r->v[2], r->v[3], r->v[4]);
			break;
		case TEXTRACE_IMPORT:
			fprintf(f, "%6d %-12s tex=0x%04x id=0x%04x fmt=%d siz=%d hit=%d type=%d %ux%u sum=0x%08x\n", r->frame, kn,
					r->v[0], r->v[1], (r->v[2] >> 24) & 0xff, (r->v[2] >> 16) & 0xff, (r->v[2] >> 8) & 0xff, r->v[2] & 0xff,
					(u32)r->v[3] >> 16, (u32)r->v[3] & 0xffff, (u32)r->v[4]);
			break;
		case TEXTRACE_IMPORTPAL:
			fprintf(f, "%6d %-12s tex=0x%04x id=0x%04x palidx=%d palsum=0x%08x glid=%d\n", r->frame, kn, r->v[0], r->v[1], r->v[2], (u32)r->v[3], r->v[4]);
			break;
		case TEXTRACE_MENUPOOL:
			fprintf(f, "%6d %-12s poolofs=0x%x left=0x%x right=0x%x end=0x%x\n", r->frame, kn, r->v[0], r->v[1], r->v[2], r->v[3]);
			break;
		default:
			fprintf(f, "%6d %-12s %d %d %d %d %d\n", r->frame, kn, r->v[0], r->v[1], r->v[2], r->v[3], r->v[4]);
			break;
		}
	}

	fclose(f);
	sysLogPrintf(LOG_NOTE, "textrace: wrote %u records to %s", s_Count - first, path);
}
