#ifndef _IN_TEXTRACE_H
#define _IN_TEXTRACE_H

#include <PR/ultratypes.h>

/*
 * Texture-load trace: a fixed ring of small records, written with no I/O so
 * recording does not perturb whatever is being traced, and dumped to
 * $S/textrace.txt from cleanup(). Off unless the process was started with
 * --textrace; the gate is one global compare per record.
 *
 * Record kinds and what the five values mean for each are listed next to the
 * enum. Every record also carries g_Vars.lvframenum.
 */

enum textracekind {
	TEXTRACE_MODELDEF,     // modeldefLoad enter:  fileid, modIdx, rawFileNum, -, -
	TEXTRACE_MODELDEF_END, // modeldefLoad leave:  fileid, loadedsize, -, -, -
	TEXTRACE_MODTEX,       // modTextureLoad:      texnum, modNum, modelFileNum, fileNum, lane
	TEXTRACE_PORTMISS,     // texLoad port-range id with no bytes: texnum, g_TexModNum, pool-is-shared, -, -
	TEXTRACE_POOLFULL,     // texLoad out of pool: texnum, freebytes, iszlib, -, -
	TEXTRACE_DROP,         // texLoadFromGdl dropped a C0: texnum, slot(0/1), g_TexModNum, modelFileNum, -
	TEXTRACE_CACHECLR,     // videoResetTextureCache: -, -, -, -, -
	TEXTRACE_MENUMODEL,    // menu model loaded: alloclen, totalfilelen, bodyfinal, headfinal, poolused
	TEXTRACE_MENUPOOL,     // menu model pool after load: poolstart-allocstart, leftpos-poolstart, rightpos-poolstart, end-poolstart, -
};

// lanes reported by TEXTRACE_MODTEX
enum textracelane {
	TEXTRACE_LANE_NOMODCTX = 0,  // g_TexModNum < 0, returned 0
	TEXTRACE_LANE_NOFILE = 1,    // name resolution found no file in the mod
	TEXTRACE_LANE_LOADNULL = 2,  // romdataFileLoad returned NULL
	TEXTRACE_LANE_BASEROM = 3,   // bytes point into the base rom, returned 0 for DMA
	TEXTRACE_LANE_TOOLARGE = 4,  // bytes larger than the caller's buffer
	TEXTRACE_LANE_BYTES = 5,     // served bytes (alt-rom / self / loose-in-filetable)
	TEXTRACE_LANE_LOOSE = 6,     // served from <mod>/textures/<num>.bin
	TEXTRACE_LANE_MISS = 7,      // nothing anywhere, returned 0
};

extern u8 g_TexTraceEnabled;

void texTraceInit(void);
void texTraceRecord(s32 kind, s32 a, s32 b, s32 c, s32 d, s32 e);
void texTraceDump(void);

#define TEXTRACE(kind, a, b, c, d, e) \
	do { if (g_TexTraceEnabled) texTraceRecord((kind), (s32)(a), (s32)(b), (s32)(c), (s32)(d), (s32)(e)); } while (0)

#endif
