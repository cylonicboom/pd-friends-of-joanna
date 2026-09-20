#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "constants.h"
#include "gbiex.h"
#include "system.h"
#include "config.h"
#include "fs.h"
#include "skinmatch.h"

#include "external/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

// See skinmatch.h for the model. This file is the measurement side: it never
// touches GL and never decides who is drawn under whom - gfx_pc.cpp asks it
// questions at texture upload and at draw, chr.c decides when to open the
// bracket, and the imgui panel edits the entries in place.

s32 g_SkinMatchEnabled = 1;

struct skinmatchsidecar {
	u8 type;
	u8 kind;
	u16 id;
	s32 texnum;
	s8 ownerMod;
	char path[FS_MAXPATH + 1];
};

static struct skinmatchsidecar g_SkinSidecars[SKINMATCH_MAX_SIDECARS];
static s32 g_NumSkinSidecars = 0;
static struct skinmatchbody g_SkinBodies[SKINMATCH_MAX_BODIES];
static struct skinmatchhead g_SkinHeads[SKINMATCH_MAX_HEADS];

// ---------------------------------------------------------------- colour math

static f32 srgbToLin(u8 c)
{
	f32 v = c / 255.0f;
	return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

static u8 linToSrgb(f32 v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
	s32 i = (s32)(v * 255.0f + 0.5f);
	return i < 0 ? 0 : i > 255 ? 255 : (u8)i;
}

static void linToOklab(const f32 c[3], f32 out[3])
{
	f32 l = cbrtf(0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2]);
	f32 m = cbrtf(0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2]);
	f32 s = cbrtf(0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2]);
	out[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
	out[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
	out[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

static void oklabToLin(const f32 lab[3], f32 out[3])
{
	f32 l = lab[0] + 0.3963377774f * lab[1] + 0.2158037573f * lab[2];
	f32 m = lab[0] - 0.1055613458f * lab[1] - 0.0638541728f * lab[2];
	f32 s = lab[0] - 0.0894841775f * lab[1] - 1.2914855480f * lab[2];
	l = l * l * l;
	m = m * m * m;
	s = s * s * s;
	out[0] = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
	out[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
	out[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

void skinmatchSrgbToOklab(u8 r, u8 g, u8 b, f32 out[3])
{
	f32 lin[3] = { srgbToLin(r), srgbToLin(g), srgbToLin(b) };
	linToOklab(lin, out);
}

void skinmatchOklabToSrgb(const f32 lab[3], u8 out[3])
{
	f32 lin[3];
	oklabToLin(lab, lin);
	out[0] = linToSrgb(lin[0]);
	out[1] = linToSrgb(lin[1]);
	out[2] = linToSrgb(lin[2]);
}

// ---------------------------------------------------------------- eligibility

bool skinmatchBodyEligible(s32 bodynum)
{
	switch (bodynum) {
	case BODY_SKEDAR:
	case BODY_MINISKEDAR:
	case BODY_SKEDARKING:
	case BODY_DRCAROLL:
	case BODY_EYESPY:
	case BODY_CHICROB:
	case BODY_ELVIS1:
	case BODY_ELVISWAISTCOAT:
	case BODY_THEKING:
	case BODY_MAIAN_SOLDIER:
		return false;
	}

	return bodynum >= 0;
}

bool skinmatchHeadEligible(s32 headnum)
{
	switch (headnum) {
	case HEAD_ELVIS:
	case HEAD_ELVIS_GOGS:
	case HEAD_THEKING:
	case HEAD_MAIAN_S:
	case HEAD_A51FACEPLATE:
		return false;
	}

	return headnum >= 0;
}

// ---------------------------------------------------------------- registry

void skinmatchInit(void)
{
	configRegisterInt("Video.SkinMatch", &g_SkinMatchEnabled, 0, 1);
}

void skinmatchRegisterSidecar(u8 type, u16 id, s32 texnum, s8 ownerMod, const char *path, s32 kind)
{
	s32 i;

	for (i = 0; i < g_NumSkinSidecars; i++) {
		struct skinmatchsidecar *sc = &g_SkinSidecars[i];

		if (sc->id == id && sc->texnum == texnum && sc->kind == kind) {
			// first writer wins, the same policy readModelTextures applies to
			// the textures themselves
			return;
		}
	}

	if (g_NumSkinSidecars >= SKINMATCH_MAX_SIDECARS) {
		sysLogPrintf(LOG_WARNING, "skinmatch: sidecar table full, ignoring %s", path);
		return;
	}

	struct skinmatchsidecar *sc = &g_SkinSidecars[g_NumSkinSidecars++];
	sc->type = type;
	sc->kind = (u8)kind;
	sc->id = id;
	sc->texnum = texnum;
	sc->ownerMod = ownerMod;
	strncpy(sc->path, path, FS_MAXPATH);
	sc->path[FS_MAXPATH] = '\0';
}

s32 skinmatchNumSidecars(void)
{
	return g_NumSkinSidecars;
}

/**
 * Identity is (model file, texture number), never the type: tex.c stamps every
 * model texture load as G_TEXTYPE_GENERAL with the model's raw fileNum in id
 * and the global texture number in texnum (texWriteLoadToTmemAddr), while the
 * scan registers a model directory's sidecars as G_TEXTYPE_MODEL. A sidecar
 * from the top-level ext_tex directory has id 0 and answers for any model.
 */
static struct skinmatchsidecar *findSidecar(u16 id, s32 texnum, s32 kind)
{
	struct skinmatchsidecar *global = NULL;
	s32 i;

	for (i = 0; i < g_NumSkinSidecars; i++) {
		struct skinmatchsidecar *sc = &g_SkinSidecars[i];

		if (sc->texnum != texnum || sc->kind != kind) {
			continue;
		}

		if (sc->id == id) {
			return sc;
		}

		if (sc->id == 0 && !global) {
			global = sc;
		}
	}

	return global;
}

// ---------------------------------------------------------------- measuring

struct lkey {
	f32 L;
	u32 i;
};

static int cmpLkey(const void *a, const void *b)
{
	f32 d = ((const struct lkey *)a)->L - ((const struct lkey *)b)->L;
	return d < 0 ? -1 : d > 0 ? 1 : 0;
}

/**
 * Gather the texels a predicate admits, sorted by Oklab L. Returns the count;
 * keys must hold width*height entries.
 */
static s32 gatherSorted(const struct skinmatchbody *body, s32 layer, struct lkey *keys)
{
	u32 n = (u32)body->width * body->height;
	s32 count = 0;
	u32 i;

	for (i = 0; i < n; i++) {
		u8 r = body->mask[i * 2];
		u8 g = body->mask[i * 2 + 1];
		bool admit = layer == 0 ? (r > 127 && g == 0) : (g == layer);

		if (!admit) {
			continue;
		}

		f32 lab[3];
		skinmatchSrgbToOklab(body->rgba[i * 4], body->rgba[i * 4 + 1], body->rgba[i * 4 + 2], lab);
		keys[count].L = lab[0];
		keys[count].i = i;
		count++;
	}

	qsort(keys, count, sizeof(struct lkey), cmpLkey);
	return count;
}

/** Mean Oklab of the k texels starting at s in a sorted key list. */
static void meanOklab(const struct skinmatchbody *body, const struct lkey *keys, s32 s, s32 k, f32 out[3])
{
	f32 acc[3] = { 0, 0, 0 };
	s32 j;

	for (j = s; j < s + k; j++) {
		f32 lab[3];
		u32 i = keys[j].i;
		skinmatchSrgbToOklab(body->rgba[i * 4], body->rgba[i * 4 + 1], body->rgba[i * 4 + 2], lab);
		acc[0] += lab[0];
		acc[1] += lab[1];
		acc[2] += lab[2];
	}

	out[0] = acc[0] / k;
	out[1] = acc[1] / k;
	out[2] = acc[2] / k;
}

/** K linear-RGB bin means over a sorted key list (quantile profile). */
static void quantileProfile(const struct skinmatchbody *body, const struct lkey *keys, s32 count, s32 K, f32 *out)
{
	s32 q;

	for (q = 0; q < K; q++) {
		s32 s = (s32)((s64)count * q / K);
		s32 e = (s32)((s64)count * (q + 1) / K);
		f32 acc[3] = { 0, 0, 0 };
		s32 j;

		for (j = s; j < e; j++) {
			u32 i = keys[j].i;
			acc[0] += srgbToLin(body->rgba[i * 4]);
			acc[1] += srgbToLin(body->rgba[i * 4 + 1]);
			acc[2] += srgbToLin(body->rgba[i * 4 + 2]);
		}

		out[q * 3] = acc[0] / (e - s);
		out[q * 3 + 1] = acc[1] / (e - s);
		out[q * 3 + 2] = acc[2] / (e - s);
	}
}

static f32 clampf(f32 v, f32 lo, f32 hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/**
 * Sheer garment fit: covered = gain * bare + offset per channel in linear RGB,
 * least squares over K quantile-matched bins of bare skin against the layer.
 * A layer with no shading range falls back to a multiplicative fit. The nudge
 * is applied as an opacity delta and folded back into gain/offset.
 */
static void fitGarment(struct skinmatchbody *body, s32 layer, struct lkey *bare, s32 nbare, struct lkey *keys)
{
	enum { K = 8 };
	struct skinmatchgarment *g = &body->garment[layer - 1];
	s32 ncov = gatherSorted(body, layer, keys);
	f32 bq[K * 3], cq[K * 3];
	s32 c;

	memset(g, 0, sizeof(*g));
	g->ntexels = ncov;

	if (ncov < 8 || nbare < 8) {
		return;
	}

	quantileProfile(body, bare, nbare, K, bq);
	quantileProfile(body, keys, ncov, K, cq);

	for (c = 0; c < 3; c++) {
		f32 sx = 0, sy = 0, sxx = 0, sxy = 0;
		s32 q;

		for (q = 0; q < K; q++) {
			f32 x = bq[q * 3 + c], y = cq[q * 3 + c];
			sx += x;
			sy += y;
			sxx += x * x;
			sxy += x * y;
		}

		f32 varx = sxx - sx * sx / K;
		f32 covspan = cq[(K - 1) * 3 + c] - cq[c];

		if (varx < 1e-5f || fabsf(covspan) < 0.004f) {
			g->flat = 1;
			g->gain[c] = sx > 1e-3f ? sy / sx : 0.0f;
			g->off[c] = 0.0f;
		} else {
			g->gain[c] = (sxy - sx * sy / K) / varx;
			g->off[c] = (sy - g->gain[c] * sx) / K;
		}

		g->gain[c] = clampf(g->gain[c], 0.0f, 1.5f);
		g->off[c] = clampf(g->off[c], -0.2f, 0.6f);
	}

	g->alpha = clampf(1.0f - (g->gain[0] + g->gain[1] + g->gain[2]) / 3.0f, 0.02f, 0.98f);

	for (c = 0; c < 3; c++) {
		g->tint[c] = clampf(g->off[c] / g->alpha, 0.0f, 1.0f);
	}

	f32 a2 = clampf(g->alpha + body->nudge[layer - 1] / 100.0f, 0.02f, 0.98f);

	for (c = 0; c < 3; c++) {
		g->gain[c] = g->gain[c] * (1.0f - a2) / (1.0f - g->alpha);
		g->off[c] = g->tint[c] * a2;
	}

	g->opaque = (g->gain[0] + g->gain[1] + g->gain[2]) / 3.0f < 0.15f;
	g->valid = 1;
}

void skinmatchBodyRemeasure(struct skinmatchbody *body)
{
	body->ready = 0;

	if (!body->mask || !body->rgba || !body->width || !body->height) {
		return;
	}

	u32 n = (u32)body->width * body->height;
	struct lkey *bare = malloc(n * sizeof(struct lkey));
	struct lkey *keys = malloc(n * sizeof(struct lkey));

	if (!bare || !keys) {
		free(bare);
		free(keys);
		return;
	}

	s32 nbare = gatherSorted(body, 0, bare);
	body->nbare = nbare;

	if (nbare >= 4) {
		// the 6% tails at either end, the same picks the mockup settled on
		s32 k = nbare * 6 / 100;
		if (k < 1) k = 1;
		meanOklab(body, bare, 0, k, body->dark);
		meanOklab(body, bare, nbare - k, k, body->light);
		body->ready = 1;
	}

	s32 layer;
	for (layer = 1; layer <= SKINMATCH_MAX_LAYERS; layer++) {
		fitGarment(body, layer, bare, nbare, keys);
	}

	free(bare);
	free(keys);
}

static void sampleTag(const struct skinmatchhead *head, const struct skinmatchtag *tag, f32 out[3])
{
	f32 acc[3] = { 0, 0, 0 };
	s32 n = 0;
	s32 x, y;

	for (y = tag->y - tag->r; y <= tag->y + tag->r; y++) {
		for (x = tag->x - tag->r; x <= tag->x + tag->r; x++) {
			if (x < 0 || y < 0 || x >= head->width || y >= head->height) {
				continue;
			}

			f32 lab[3];
			u32 i = (u32)y * head->width + x;
			skinmatchSrgbToOklab(head->rgba[i * 4], head->rgba[i * 4 + 1], head->rgba[i * 4 + 2], lab);
			acc[0] += lab[0];
			acc[1] += lab[1];
			acc[2] += lab[2];
			n++;
		}
	}

	if (n == 0) {
		out[0] = out[1] = out[2] = 0;
		return;
	}

	out[0] = acc[0] / n;
	out[1] = acc[1] / n;
	out[2] = acc[2] / n;
}

void skinmatchHeadRemeasure(struct skinmatchhead *head)
{
	head->ready = 0;

	if (!head->rgba || head->ntags <= 0) {
		return;
	}

	f32 dark[3], light[3], s[3];
	s32 i;

	sampleTag(head, &head->tags[0], dark);
	memcpy(light, dark, sizeof(light));

	for (i = 1; i < head->ntags; i++) {
		sampleTag(head, &head->tags[i], s);

		if (s[0] < dark[0]) memcpy(dark, s, sizeof(dark));
		if (s[0] > light[0]) memcpy(light, s, sizeof(light));
	}

	if (head->ntags == 1) {
		// one tap is the mid tone; give it a span to remap onto
		dark[0] -= 0.08f;
		light[0] += 0.08f;
	}

	dark[0] += head->nudgedark / 100.0f;
	light[0] += head->nudgelight / 100.0f;

	memcpy(head->dark, dark, sizeof(dark));
	memcpy(head->light, light, sizeof(light));
	head->ready = 1;
}

// ---------------------------------------------------------------- sidecar io

static bool loadMaskPng(struct skinmatchbody *body, const char *path)
{
	int w = 0, h = 0, ch = 0;

	// same orientation as the override PNGs ext_tex loads
	stbi_set_flip_vertically_on_load(1);
	u8 *px = stbi_load(path, &w, &h, &ch, 4);
	stbi_set_flip_vertically_on_load(0);

	if (!px) {
		sysLogPrintf(LOG_WARNING, "skinmatch: cannot read mask %s", path);
		return false;
	}

	free(body->mask);
	body->mask = malloc((size_t)w * h * 2);

	if (!body->mask) {
		stbi_image_free(px);
		return false;
	}

	s32 i;
	for (i = 0; i < w * h; i++) {
		body->mask[i * 2] = px[i * 4];
		u8 g = px[i * 4 + 1];
		body->mask[i * 2 + 1] = g > SKINMATCH_MAX_LAYERS ? 0 : g;
	}

	stbi_image_free(px);
	body->width = (u16)w;
	body->height = (u16)h;
	strncpy(body->path, path, FS_MAXPATH);
	body->path[FS_MAXPATH] = '\0';
	return true;
}

/**
 * Tags file: {"tags":[{"x":15,"y":12,"r":1},...],"nudge":[0,0]}. Read with a
 * scanner rather than a parser - the file is tiny, written by the panel, and
 * the only structure worth honouring is the order of the numbers.
 */
static bool loadTagsJson(struct skinmatchhead *head, const char *path)
{
	FILE *fp = fopen(path, "rb");

	if (!fp) {
		sysLogPrintf(LOG_WARNING, "skinmatch: cannot read tags %s", path);
		return false;
	}

	char buf[2048];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	buf[len] = '\0';

	head->ntags = 0;
	head->nudgedark = head->nudgelight = 0;

	const char *p = buf;
	while ((p = strstr(p, "\"x\"")) != NULL && head->ntags < SKINMATCH_MAX_TAGS) {
		int x = 0, y = 0, r = 1;
		const char *py, *pr;
		x = (int)strtol(strchr(p, ':') + 1, NULL, 10);
		py = strstr(p, "\"y\"");
		pr = strstr(p, "\"r\"");
		if (!py) break;
		y = (int)strtol(strchr(py, ':') + 1, NULL, 10);
		if (pr && pr < strchr(py, '}')) {
			r = (int)strtol(strchr(pr, ':') + 1, NULL, 10);
		}
		head->tags[head->ntags].x = (s16)x;
		head->tags[head->ntags].y = (s16)y;
		head->tags[head->ntags].r = (u8)(r < 0 ? 0 : r > 7 ? 7 : r);
		head->ntags++;
		p = py + 1;
	}

	const char *pn = strstr(buf, "\"nudge\"");
	if (pn) {
		const char *pb = strchr(pn, '[');
		if (pb) {
			char *end;
			head->nudgedark = strtof(pb + 1, &end);
			if (*end == ',') {
				head->nudgelight = strtof(end + 1, NULL);
			}
		}
	}

	strncpy(head->path, path, FS_MAXPATH);
	head->path[FS_MAXPATH] = '\0';
	return true;
}

static bool loadBodyNudges(struct skinmatchbody *body, const char *maskpath)
{
	// <tex>.skin.json beside the mask: {"garments":{"1":{"nudge":5},...}}
	char path[FS_MAXPATH + 1];
	size_t n = strlen(maskpath);

	if (n < 4) return false;
	snprintf(path, sizeof(path), "%.*sjson", (int)(n - 3), maskpath);

	FILE *fp = fopen(path, "rb");
	if (!fp) return false;

	char buf[1024];
	size_t len = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	buf[len] = '\0';

	s32 layer;
	for (layer = 1; layer <= SKINMATCH_MAX_LAYERS; layer++) {
		char key[8];
		snprintf(key, sizeof(key), "\"%d\"", layer);
		const char *p = strstr(buf, key);
		if (!p) continue;
		const char *pn = strstr(p, "\"nudge\"");
		if (!pn) continue;
		body->nudge[layer - 1] = strtof(strchr(pn, ':') + 1, NULL);
	}

	return true;
}

s32 skinmatchBodySave(struct skinmatchbody *body, const char *dir)
{
	if (!body->mask) return 1;

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/%04x.skin.png", dir, body->texnum);

	// PNG rows are top-down and the loader flips, so write bottom-up
	u32 w = body->width, h = body->height;
	u8 *px = malloc((size_t)w * h * 4);
	if (!px) return 1;

	u32 y, x;
	for (y = 0; y < h; y++) {
		const u8 *src = body->mask + (size_t)(h - 1 - y) * w * 2;
		u8 *dst = px + (size_t)y * w * 4;
		for (x = 0; x < w; x++) {
			dst[x * 4] = src[x * 2];
			dst[x * 4 + 1] = src[x * 2 + 1];
			dst[x * 4 + 2] = 0;
			dst[x * 4 + 3] = 255;
		}
	}

	s32 ok = stbi_write_png(path, w, h, 4, px, w * 4);
	free(px);

	if (!ok) {
		sysLogPrintf(LOG_WARNING, "skinmatch: cannot write %s", path);
		return 1;
	}

	strncpy(body->path, path, FS_MAXPATH);
	body->path[FS_MAXPATH] = '\0';
	skinmatchRegisterSidecar(body->type, body->id, body->texnum, body->ownerMod, path, SKINMATCH_SIDECAR_MASK);

	snprintf(path, sizeof(path), "%s/%04x.skin.json", dir, body->texnum);
	FILE *fp = fopen(path, "wb");
	if (fp) {
		s32 layer;
		fprintf(fp, "{\"garments\":{");
		for (layer = 1; layer <= SKINMATCH_MAX_LAYERS; layer++) {
			fprintf(fp, "%s\"%d\":{\"nudge\":%g}", layer > 1 ? "," : "", layer, body->nudge[layer - 1]);
		}
		fprintf(fp, "}}\n");
		fclose(fp);
	}

	sysLogPrintf(LOG_NOTE, "skinmatch: wrote %s", path);
	return 0;
}

s32 skinmatchHeadSave(struct skinmatchhead *head, const char *dir)
{
	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/%04x.skin.json", dir, head->texnum);

	FILE *fp = fopen(path, "wb");
	if (!fp) {
		sysLogPrintf(LOG_WARNING, "skinmatch: cannot write %s", path);
		return 1;
	}

	s32 i;
	fprintf(fp, "{\"tags\":[");
	for (i = 0; i < head->ntags; i++) {
		fprintf(fp, "%s{\"x\":%d,\"y\":%d,\"r\":%d}", i ? "," : "", head->tags[i].x, head->tags[i].y, head->tags[i].r);
	}
	fprintf(fp, "],\"nudge\":[%g,%g]}\n", head->nudgedark, head->nudgelight);
	fclose(fp);

	strncpy(head->path, path, FS_MAXPATH);
	head->path[FS_MAXPATH] = '\0';
	skinmatchRegisterSidecar(G_TEXTYPE_GENERAL, head->id, head->texnum, head->ownerMod, path, SKINMATCH_SIDECAR_TAGS);
	sysLogPrintf(LOG_NOTE, "skinmatch: wrote %s", path);
	return 0;
}

// ---------------------------------------------------------------- entries

static struct skinmatchbody *allocBody(void)
{
	s32 i;

	for (i = 0; i < SKINMATCH_MAX_BODIES; i++) {
		if (!g_SkinBodies[i].used) {
			memset(&g_SkinBodies[i], 0, sizeof(g_SkinBodies[i]));
			g_SkinBodies[i].used = 1;
			return &g_SkinBodies[i];
		}
	}

	return NULL;
}

static struct skinmatchhead *allocHead(void)
{
	s32 i;

	for (i = 0; i < SKINMATCH_MAX_HEADS; i++) {
		if (!g_SkinHeads[i].used) {
			memset(&g_SkinHeads[i], 0, sizeof(g_SkinHeads[i]));
			g_SkinHeads[i].used = 1;
			return &g_SkinHeads[i];
		}
	}

	return NULL;
}

struct skinmatchbody *skinmatchBodyFor(u8 type, u16 id, s32 texnum, bool create)
{
	s32 i;

	for (i = 0; i < SKINMATCH_MAX_BODIES; i++) {
		struct skinmatchbody *b = &g_SkinBodies[i];

		if (b->used && b->id == id && b->texnum == texnum) {
			return b;
		}
	}

	if (!create) {
		return NULL;
	}

	struct skinmatchsidecar *sc = findSidecar(id, texnum, SKINMATCH_SIDECAR_MASK);

	if (!sc) {
		return NULL;
	}

	struct skinmatchbody *b = allocBody();

	if (!b) {
		return NULL;
	}

	b->type = type;
	b->id = id;
	b->texnum = texnum;
	b->ownerMod = sc->ownerMod;

	if (!loadMaskPng(b, sc->path)) {
		b->used = 0;
		return NULL;
	}

	loadBodyNudges(b, sc->path);
	b->pending = 1;
	return b;
}

struct skinmatchbody *skinmatchBodyCreateBlank(u8 type, u16 id, s32 texnum, u16 width, u16 height)
{
	struct skinmatchbody *b = skinmatchBodyFor(type, id, texnum, false);

	if (b) {
		return b;
	}

	b = allocBody();

	if (!b) {
		return NULL;
	}

	b->type = type;
	b->id = id;
	b->texnum = texnum;
	b->ownerMod = -1;
	b->width = width;
	b->height = height;
	b->mask = calloc((size_t)width * height, 2);
	b->pending = 1;
	return b;
}

struct skinmatchhead *skinmatchHeadForTex(u16 id, s32 texnum)
{
	s32 i;

	for (i = 0; i < SKINMATCH_MAX_HEADS; i++) {
		struct skinmatchhead *h = &g_SkinHeads[i];

		if (h->used && h->id == id && h->texnum == texnum) {
			return h;
		}
	}

	return NULL;
}

struct skinmatchhead *skinmatchHeadFor(u16 fileid, bool create)
{
	s32 i;

	for (i = 0; i < SKINMATCH_MAX_HEADS; i++) {
		struct skinmatchhead *h = &g_SkinHeads[i];

		if (h->used && h->id == fileid) {
			return h;
		}
	}

	if (!create) {
		return NULL;
	}

	for (i = 0; i < g_NumSkinSidecars; i++) {
		struct skinmatchsidecar *sc = &g_SkinSidecars[i];

		if (sc->id != fileid || sc->kind != SKINMATCH_SIDECAR_TAGS) {
			continue;
		}

		struct skinmatchhead *h = allocHead();

		if (!h) {
			return NULL;
		}

		h->id = fileid;
		h->texnum = sc->texnum;
		h->ownerMod = sc->ownerMod;

		if (!loadTagsJson(h, sc->path)) {
			h->used = 0;
			return NULL;
		}

		h->pending = 1;
		return h;
	}

	return NULL;
}

struct skinmatchhead *skinmatchHeadCreateBlank(u16 id, s32 texnum)
{
	struct skinmatchhead *h = skinmatchHeadForTex(id, texnum);

	if (h) {
		return h;
	}

	h = allocHead();

	if (!h) {
		return NULL;
	}

	h->id = id;
	h->texnum = texnum;
	h->ownerMod = -1;
	h->pending = 1;
	return h;
}

bool skinmatchWantsPixels(u8 type, u16 id, s32 texnum)
{
	struct skinmatchbody *b = skinmatchBodyFor(type, id, texnum, false);

	if (b && b->pending) {
		return true;
	}

	if (id != 0) {
		struct skinmatchhead *h = skinmatchHeadForTex(id, texnum);

		if (h && h->pending) {
			return true;
		}
	}

	return false;
}

void skinmatchOnTexturePixels(u8 type, u16 id, s32 texnum, const u8 *rgba, u32 width, u32 height)
{
	struct skinmatchbody *b = skinmatchBodyFor(type, id, texnum, false);

	if (b && (b->pending || !b->rgba)) {
		if (b->width != width || b->height != height) {
			if (!b->denied) {
				sysLogPrintf(LOG_WARNING, "skinmatch: mask %s is %ux%u, texture is %ux%u - ignoring it",
					b->path, b->width, b->height, width, height);
			}
			b->denied = 1;
			b->pending = 0;
			b->ready = 0;
		} else {
			free(b->rgba);
			b->rgba = malloc((size_t)width * height * 4);

			if (b->rgba) {
				memcpy(b->rgba, rgba, (size_t)width * height * 4);
				skinmatchBodyRemeasure(b);
			}

			b->pending = 0;
		}
	}

	if (id != 0) {
		struct skinmatchhead *h = skinmatchHeadForTex(id, texnum);

		if (h && (h->pending || !h->rgba)) {
			free(h->rgba);
			h->rgba = malloc((size_t)width * height * 4);
			h->width = (u16)width;
			h->height = (u16)height;

			if (h->rgba) {
				memcpy(h->rgba, rgba, (size_t)width * height * 4);
				skinmatchHeadRemeasure(h);
			}

			h->pending = 0;
		}
	}
}

bool skinmatchResolve(const struct skinmatchbody *body, const struct skinmatchhead *head, f32 *out)
{
	if (!body || !head || !body->ready || !head->ready || body->denied) {
		return false;
	}

	memcpy(out, body->dark, 3 * sizeof(f32));
	memcpy(out + 3, body->light, 3 * sizeof(f32));
	memcpy(out + 6, head->dark, 3 * sizeof(f32));
	memcpy(out + 9, head->light, 3 * sizeof(f32));

	s32 layer;
	for (layer = 0; layer < SKINMATCH_MAX_LAYERS; layer++) {
		const struct skinmatchgarment *g = &body->garment[layer];
		f32 *gain = out + 12 + layer * 3;
		f32 *off = out + 24 + layer * 3;

		if (g->valid && !g->opaque) {
			memcpy(gain, g->gain, 3 * sizeof(f32));
			memcpy(off, g->off, 3 * sizeof(f32));
		} else {
			// identity: an unfitted or opaque layer draws its texels unchanged
			// as skin, which is the least surprising thing to do with them
			gain[0] = gain[1] = gain[2] = 1.0f;
			off[0] = off[1] = off[2] = 0.0f;
		}
	}

	return true;
}

void skinmatchOnTextureCacheClear(void)
{
	// renderer texture ids are gone with the cache; masks re-upload on demand
	s32 i;

	for (i = 0; i < SKINMATCH_MAX_BODIES; i++) {
		g_SkinBodies[i].maskgl = 0;
	}
}

s32 skinmatchBodyIndex(const struct skinmatchbody *body)
{
	return body ? (s32)(body - g_SkinBodies) : -1;
}

struct skinmatchbody *skinmatchBodyAt(s32 index)
{
	if (index < 0 || index >= SKINMATCH_MAX_BODIES || !g_SkinBodies[index].used) {
		return NULL;
	}

	return &g_SkinBodies[index];
}

struct skinmatchhead *skinmatchHeadAt(s32 index)
{
	if (index < 0 || index >= SKINMATCH_MAX_HEADS || !g_SkinHeads[index].used) {
		return NULL;
	}

	return &g_SkinHeads[index];
}
