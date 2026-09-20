#ifndef _IN_SKINMATCH_H
#define _IN_SKINMATCH_H

#include <PR/ultratypes.h>
#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Skin match: a body texture borrows the tone of the head it is drawn under.
 *
 * Nothing is baked. A body texture carries a companion mask (<tex>.skin.png,
 * R = skin weight, G = sheer-garment layer id 1-4) and a head texture carries
 * tag points (<tex>.skin.json). At texture upload the engine measures both
 * against the real texel0 pixels: the body's bare-skin dark/light axis in
 * Oklab, one affine gain/offset per garment layer in linear RGB, and the
 * head's dark/light from its tag points. chrRender brackets a chr's body draws
 * with G_SKINMATCH_EXT naming the head; fast3d sets SHADER_OPT_SKINMATCH for
 * masked textures inside the bracket and the fragment shader does the rest.
 *
 * Eligibility is three layers: the Video.SkinMatch setting, then the hardcoded
 * denylist below (robots, Skedar, Maians - both body and head side), then the
 * presence of a sidecar. Mod-added bodies are eligible by default; a mod that
 * ships a non-human body simply ships no mask for it.
 */

#define SKINMATCH_MAX_LAYERS 4
#define SKINMATCH_MAX_TAGS 16
#define SKINMATCH_MAX_BODIES 64
#define SKINMATCH_MAX_HEADS 64
#define SKINMATCH_MAX_SIDECARS 512

#define SKINMATCH_SIDECAR_MASK 0
#define SKINMATCH_SIDECAR_TAGS 1

struct skinmatchgarment {
	f32 gain[3];
	f32 off[3];
	f32 alpha;     // 1 - mean(gain) before nudge, the human-facing opacity
	f32 tint[3];   // off / alpha, linear RGB
	s32 ntexels;
	u8 valid;
	u8 flat;       // covered region had no shading range: multiplicative fallback
	u8 opaque;     // mean gain < 0.15: this is cloth, not a sheer layer
};

struct skinmatchbody {
	u8 used;
	u8 type;
	u16 id;
	s32 texnum;
	s8 ownerMod;
	u16 width;
	u16 height;
	u8 *mask;       // 2 bytes per texel (R skin weight, G layer), rows in upload order
	u8 *rgba;       // copy of the last texel0 pixels the descriptors were measured on
	u32 maskgl;     // renderer texture id, 0 until uploaded
	u8 maskdirty;   // mask edited since the last upload
	u8 ready;       // descriptors measured against real pixels
	u8 pending;     // needs the next pixel upload to measure against
	u8 denied;      // sidecar exists but the body is on the denylist (logged once)
	f32 dark[3];
	f32 light[3];
	s32 nbare;
	struct skinmatchgarment garment[SKINMATCH_MAX_LAYERS];
	f32 nudge[SKINMATCH_MAX_LAYERS]; // opacity delta, percent
	char path[FS_MAXPATH + 1];
};

struct skinmatchtag {
	s16 x;
	s16 y;   // texture row in upload order (same convention as the mask)
	u8 r;
};

struct skinmatchhead {
	u8 used;
	u16 id;
	s32 texnum;
	s8 ownerMod;
	struct skinmatchtag tags[SKINMATCH_MAX_TAGS];
	s32 ntags;
	f32 nudgedark;   // Oklab L offsets, in percent of L
	f32 nudgelight;
	u8 ready;
	u8 pending;
	u16 width;
	u16 height;
	u8 *rgba;
	f32 dark[3];
	f32 light[3];
	char path[FS_MAXPATH + 1];
};

extern s32 g_SkinMatchEnabled;

void skinmatchInit(void);
void skinmatchOnTextureCacheClear(void);

// ext_tex scan: a <hex>.skin.png / <hex>.skin.json found beside the overrides.
void skinmatchRegisterSidecar(u8 type, u16 id, s32 texnum, s8 ownerMod, const char *path, s32 kind);
s32 skinmatchNumSidecars(void);

// Eligibility switches. Vanilla non-humans by id; everything else is eligible.
bool skinmatchBodyEligible(s32 bodynum);
bool skinmatchHeadEligible(s32 headnum);

// Renderer side. Lookups create the entry from a registered sidecar on first
// use; the pixel hook measures the descriptors when the texture is decoded.
struct skinmatchbody *skinmatchBodyFor(u8 type, u16 id, s32 texnum, bool create);
struct skinmatchhead *skinmatchHeadFor(u16 fileid, bool create);
struct skinmatchhead *skinmatchHeadForTex(u16 id, s32 texnum);
bool skinmatchWantsPixels(u8 type, u16 id, s32 texnum);
void skinmatchOnTexturePixels(u8 type, u16 id, s32 texnum, const u8 *rgba, u32 width, u32 height);
// Fills body/head/gain/off (36 floats: 6 + 6 + 12 + 12). False when either
// side is not ready.
bool skinmatchResolve(const struct skinmatchbody *body, const struct skinmatchhead *head, f32 *out);

// Editor side (imgui panel).
struct skinmatchbody *skinmatchBodyCreateBlank(u8 type, u16 id, s32 texnum, u16 width, u16 height);
struct skinmatchhead *skinmatchHeadCreateBlank(u16 id, s32 texnum);
void skinmatchBodyRemeasure(struct skinmatchbody *body);
void skinmatchHeadRemeasure(struct skinmatchhead *head);
s32 skinmatchBodySave(struct skinmatchbody *body, const char *dir);
s32 skinmatchHeadSave(struct skinmatchhead *head, const char *dir);
s32 skinmatchBodyIndex(const struct skinmatchbody *body);
struct skinmatchbody *skinmatchBodyAt(s32 index);
struct skinmatchhead *skinmatchHeadAt(s32 index);

// Colour math shared with the panel.
void skinmatchSrgbToOklab(u8 r, u8 g, u8 b, f32 out[3]);
void skinmatchOklabToSrgb(const f32 lab[3], u8 out[3]);

#ifdef __cplusplus
}
#endif

#endif
