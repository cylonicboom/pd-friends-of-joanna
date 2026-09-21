#ifndef _IN_ASSET_H
#define _IN_ASSET_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One path grammar over the engine's asset tables, one drive per table.
 *
 * Nothing here owns storage. A drive is a veneer over the lookup surface that
 * already exists for that kind of asset - file: over romdata's fileSlots,
 * later tex: over the texmap and ext_tex, stage: over g_Stages and the stage
 * registry - and the drives never share a numbering. What they share is the
 * grammar and the verbs below.
 *
 *   drive:/owner/item
 *
 * The owner segment is a mod name (g_ModNames) or mod dir. The resolver
 * splits at the drive and hands the rest to that drive, which treats its last
 * segment as opaque: 181 vanilla file names contain '/'.
 *
 * An unqualified path, drive:/item, refuses when more than one owner has the
 * item (ASSET_AMBIGUOUS) rather than picking one by scan order. That is the
 * asset API's rule only; romdataFileLoad's union walk is unchanged by it.
 *
 * Sigils are the debugger bar's business (# for file:), not this layer's.
 * Word drives are what get printed, logged and passed around.
 *
 * Read-only by decision: declarations stay in modconfig and PDFT.
 */

enum assetdrive {
	ASSET_DRIVE_NONE,
	ASSET_DRIVE_FILE,
	ASSET_DRIVE_COUNT
};

struct assetref {
	u8 drive;   /* enum assetdrive */
	s8 owner;   /* mod index 0..63, or -1 when the drive has no owner concept */
	s32 id;     /* drive-typed: for file:, the raw fileNum in that mod's row */
	s32 sub;    /* reserved for sub-items (stage fields, texture port ids); -1 */
};

/* assetResolve return codes. Positive is a hit. */
#define ASSET_OK           1
#define ASSET_NOTFOUND     0
#define ASSET_AMBIGUOUS   -1   /* unqualified, and more than one owner has it */
#define ASSET_BADPATH     -2   /* no drive, unknown drive, or malformed */
#define ASSET_UNSUPPORTED -3   /* drive known, verb or form not implemented */

s32 assetResolve(const char *path, struct assetref *out);

/* Canonical path back out: "file:/mod_fojo/UsetupstatZ". Returns the length
 * written, or -1 if the ref does not name anything. */
s32 assetFormat(const struct assetref *ref, char *dst, u32 len);

/* The item's own name inside its drive, without the drive or owner - what a
 * panel filters on. NULL if the ref does not name anything. */
const char *assetName(const struct assetref *ref);

/* Get-ChildItem. "file:" or "file:/" walks every owner; "file:/<owner>" one.
 * Returns the number of items visited, or an assetResolve error code. The
 * callback returns 0 to stop early. */
typedef s32 (*assetenumfn)(const struct assetref *ref, const char *name, void *ctx);
s32 assetEnumerate(const char *path, assetenumfn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
