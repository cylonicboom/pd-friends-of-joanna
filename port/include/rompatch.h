#ifndef _IN_ROMPATCH_H
#define _IN_ROMPATCH_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rompatchkind {
	ROMPATCH_NONE = 0,
	ROMPATCH_XDELTA,
	ROMPATCH_BPS,
	ROMPATCH_IPS,
};

/**
 * What kind of patch these opening bytes announce, or ROMPATCH_NONE.
 */
s32 rompatchIdentify(const u8 *head, u32 len);

/**
 * Whether the file name has a patch extension (.xdelta, .vcdiff, .bps, .ips).
 */
s32 rompatchIsPatchName(const char *name);

const char *rompatchKindName(s32 kind);

/**
 * Applies a patch to a ROM. On success returns the kind and hands back a
 * malloc'd patched ROM in *out; on failure returns -1 with the reason in err.
 *
 * xdelta patches are decoded as plain VCDIFF (RFC 3284) with xdelta3's
 * application header and per-window checksum. Secondary compression and
 * custom code tables are refused with a message, not silently misread.
 */
s32 rompatchApply(const u8 *rom, u32 romlen, const u8 *patch, u32 patchlen,
		u8 **out, u32 *outlen, char *err, u32 errlen);

/* -- overlay ----------------------------------------------------------------
 *
 * A patched image WITHOUT the image: the patch decoded into a sorted list of
 * segments over the base rom, each either "these n bytes are base[src..]" or
 * "these n bytes are literal, from the patch". The base is never copied and
 * the target is never built; a COPY from the base is a pointer run, an ADD or
 * RUN goes into a literal pool, and a copy out of the target resolves to the
 * segments already recorded. Memory held is the segment table plus the
 * literal pool - the patch's own new bytes - not the target's size.
 *
 * Reading composes: rompatchOverlayRead() fills a buffer from a target range;
 * rompatchOverlayPeek() hands back a direct pointer when the whole range lies
 * inside ONE segment, which is the common case for a file the patch did not
 * touch (a pointer into the base mapping) or wrote whole (a pointer into the
 * pool), and NULL otherwise, so a caller keeps zero-copy where it can and
 * copies only across an edit.
 *
 * xdelta only. BPS and IPS refuse with a message; apply those flat.
 */
#define ROMPATCH_LITERAL 0xffffffffu

struct rompatchseg {
	u32 tgt;   /* target offset this segment starts at */
	u32 len;
	u32 src;   /* base offset, or ROMPATCH_LITERAL */
	u32 lit;   /* offset into the literal pool when src is ROMPATCH_LITERAL */
};

struct romoverlay {
	const u8 *base;
	u32 baselen;
	u32 size;           /* the target's size */
	struct rompatchseg *segs;
	u32 numsegs, capsegs;
	u8 *lit;
	u32 litlen, litcap;
};

s32 rompatchOverlay(const u8 *rom, u32 romlen, const u8 *patch, u32 patchlen,
		struct romoverlay **out, char *err, u32 errlen);
void rompatchOverlayFree(struct romoverlay *ov);

/* bytes this overlay holds in memory: segment table + literal pool */
u32 rompatchOverlayCost(const struct romoverlay *ov);

/* 1 and dst filled, or 0 if [ofs, ofs+len) is outside the target */
s32 rompatchOverlayRead(const struct romoverlay *ov, u32 ofs, u32 len, u8 *dst);

/* a direct pointer to [ofs, ofs+len) if one segment covers it, else NULL */
const u8 *rompatchOverlayPeek(const struct romoverlay *ov, u32 ofs, u32 len);

#ifdef __cplusplus
}
#endif

#endif
