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
	TEXTRACE_IMPORT,       // gfx import_texture upload: texnum, id, (fmt<<24|siz<<16|hit<<8|type), (w<<16|h), rgba checksum
	TEXTRACE_IMPORTPAL,    // same upload, CI only: texnum, id, palette_index, palette checksum, texture_id
	TEXTRACE_STAMP,        // gfx load_block/load_tile received a binding: texnum, id, type, (tile<<8 | tex_lod), addr low 32
	TEXTRACE_TRI,          // gfx drew a textured tri batch: texnum0, id0, (used0<<8|used1), changed0<<8|changed1, texture_id0
	TEXTRACE_TLUT,         // load_tlut: texnum being loaded, tile, that tile's tmem, count, base low 32
	TEXTRACE_SETTILE6,     // gfx_dp_set_tile on tile 6: tmem, fmt<<8|siz, line, texture_to_load.texnum, texture_to_load.id
	TEXTRACE_TILE,         // import_texture tile view: texnum, (unit<<24|tile<<16|first<<8|maxlod), (fmt<<24|siz<<16|lod<<8|detail), tmem<<16|line, loaded_texture texnum
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
