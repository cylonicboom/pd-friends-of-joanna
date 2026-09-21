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
 * already exists for that kind of asset, and the drives never share a
 * numbering: a texture port and a file slot are different numbers on
 * different drives. What they share is the grammar and the verbs below.
 *
 *   drive:/owner/item          file:/mod_fojo/UsetupstatZ
 *   drive:/item                stage:/0x20, seg:/mpconfigs
 *   drive:/item/sub            stage:/0x20/bg
 *
 * Owners are mod names (g_ModNames) or mod dirs, plus the literal `vanilla`
 * for what the ROM itself supplies. The resolver splits at the drive and
 * hands the rest to that drive, which treats its last segment as opaque:
 * 181 vanilla file names contain '/'.
 *
 * An unqualified file:/item refuses when more than one mod has the item
 * (ASSET_AMBIGUOUS) rather than picking one by scan order. That is this
 * layer's rule only; romdataFileLoad's union walk is unchanged by it.
 *
 * Sigils are the debugger bar's business (# / %), not this layer's. Word
 * drives are what get printed, logged and passed around.
 *
 * Read-only by decision: declarations stay in modconfig and PDFT.
 *
 * Drives:
 *   file:   fileSlots rows. owner = mod or vanilla, id = raw fileNum
 *   tex:    texture numbers. tex:/vanilla/<n> is a ROM texnum,
 *           tex:/<mod>/<local> a mod-local id (sub = its port id),
 *           tex:/port/<n> an assigned port id resolved back to (mod, local)
 *   stage:  g_Stages by stagenum, hex or decimal, or by stageGetName.
 *           stage:/<x>/<field> for bg tiles pads setup mpsetup; assetLink
 *           follows the field to the file: it names, owner and all
 *   head:   head names -> g_HeadsAndBodies index (id), mp slot (sub)
 *   body:   likewise for bodies
 *   hand:   mod-declared hand names -> their file id
 *   seg:    ROMSEG_LIST segments by name
 *   rom:    mounted rom sources by id
 *   mod:    the mod roster
 */

enum assetdrive {
	ASSET_DRIVE_NONE,
	ASSET_DRIVE_FILE,
	ASSET_DRIVE_TEX,
	ASSET_DRIVE_STAGE,
	ASSET_DRIVE_HEAD,
	ASSET_DRIVE_BODY,
	ASSET_DRIVE_HAND,
	ASSET_DRIVE_SEG,
	ASSET_DRIVE_ROM,
	ASSET_DRIVE_MOD,
	ASSET_DRIVE_COUNT
};

/* stage:/<x>/<field>; values match enum modStageField's first five */
enum assetstagefield {
	ASSET_STAGE_BG,
	ASSET_STAGE_TILES,
	ASSET_STAGE_PADS,
	ASSET_STAGE_SETUP,
	ASSET_STAGE_MPSETUP,
	ASSET_STAGE_FIELD_COUNT
};

#define ASSET_OWNER_VANILLA (-1)

struct assetref {
	u8 drive;   /* enum assetdrive */
	s8 owner;   /* mod index 0..63, ASSET_OWNER_VANILLA, or -1 when the drive has no owner concept */
	s32 id;     /* drive-typed, see the drive list above */
	s32 sub;    /* drive-typed sub-item: stage field, tex port id, mp slot; -1 when none */
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
 * panel filters on. Drives whose items are numbers (tex:) return NULL; use
 * assetFormat. NULL if the ref does not name anything. */
const char *assetName(const struct assetref *ref);

/* Follow a cross-drive link: stage field -> file, head/body/hand -> file.
 * ASSET_NOTFOUND when the ref has no link or the field is empty. */
s32 assetLink(const struct assetref *ref, struct assetref *out);

/* Get-ChildItem. "drive:" or "drive:/" walks everything the drive has;
 * "drive:/<owner>" one owner where the drive has owners. Returns the number of
 * items visited, or an assetResolve error code. The callback returns 0 to
 * stop early. `name` is the item's display name, valid for the callback only. */
typedef s32 (*assetenumfn)(const struct assetref *ref, const char *name, void *ctx);
s32 assetEnumerate(const char *path, assetenumfn fn, void *ctx);

const char *assetDriveName(s32 drive);

#ifdef __cplusplus
}
#endif

#endif
