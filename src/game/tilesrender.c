#include <ultra64.h>
#include <math.h>
#include "constants.h"
#include "game/tiles.h"
#include "game/bg.h"
#include "game/camera.h"
#include "game/gfxmemory.h"
#include "gbiex.h"
#include "lib/mtx.h"
#include "bss.h"
#include "data.h"
#include "types.h"

#ifndef PLATFORM_N64

/**
 * Tiles drawn as geometry, for the level editor (port only).
 *
 * The collision tiles are the first thing a blocked-out level has and the
 * last thing you can see: nothing in the shipped renderer draws them (the
 * beta's debug pass is compiled out at lv.c under VERSION < NTSC_1_0). So
 * this walks g_TileFileData after stageParseTiles and emits every tile of
 * every room the portal walk put on screen as flat-shaded triangles, one
 * hue per room so neighbours read apart, lit by the tile's own normal.
 *
 * Same idiom as nbombRender: a modelview that is just the camera's
 * world-to-screen, vertices in absolute world units (a geotilei's are s16
 * already, so nothing needs rebasing), one Col per tile and PD's 12-byte
 * Vtx whose colour byte is an offset into that block. gSPTri4 packs vertex
 * indices in four bits, so a tile is loaded in runs of at most 16 verts and
 * fanned from its first vertex.
 *
 * Modes: 0 off, 1 shaded (her pick), 2 solid. Session state, set from the
 * debugger; nothing in a level or a mod touches it.
 */

s32 g_TilesRenderMode = 0;
s32 g_TilesRenderAllRooms = 0;

void tilesRenderSetMode(s32 mode)
{
	g_TilesRenderMode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
}

s32 tilesRenderGetMode(void)
{
	return g_TilesRenderMode;
}

/* Golden-angle hue per room, kept away from the greys the bg draws in.
 * Rooms next to each other in number are usually next to each other in
 * the level too, and 137.5 degrees keeps neighbours apart. */
static void tilesRenderRoomColour(s32 room, f32 *r, f32 *g, f32 *b)
{
	f32 h = (f32)((room * 137) % 360) / 60.0f;
	f32 s = 0.55f;
	f32 v = 0.85f;
	s32 i = (s32)h;
	f32 f = h - (f32)i;
	f32 p = v * (1.0f - s);
	f32 q = v * (1.0f - s * f);
	f32 t = v * (1.0f - s * (1.0f - f));

	switch (i % 6) {
	case 0: *r = v; *g = t; *b = p; break;
	case 1: *r = q; *g = v; *b = p; break;
	case 2: *r = p; *g = v; *b = t; break;
	case 3: *r = p; *g = q; *b = v; break;
	case 4: *r = t; *g = p; *b = v; break;
	default: *r = v; *g = p; *b = q; break;
	}
}

/* The light: from above and a little to one side, so floors are bright,
 * walls take their tone from which way they face, and a ceiling reads
 * darkest. Shade factor 0.35..1.0 on the room hue. */
static f32 tilesRenderShade(const f32 *v0, const f32 *v1, const f32 *v2)
{
	f32 ax = v1[0] - v0[0], ay = v1[1] - v0[1], az = v1[2] - v0[2];
	f32 bx = v2[0] - v0[0], by = v2[1] - v0[1], bz = v2[2] - v0[2];
	f32 nx = ay * bz - az * by;
	f32 ny = az * bx - ax * bz;
	f32 nz = ax * by - ay * bx;
	f32 len = sqrtf(nx * nx + ny * ny + nz * nz);
	f32 d;

	if (len < 0.0001f) {
		return 0.7f;
	}

	nx /= len; ny /= len; nz /= len;

	/* both faces light the same: a tile has no side */
	d = nx * 0.30f + ny * 0.85f + nz * 0.44f;
	if (d < 0.0f) d = -d;

	return 0.35f + 0.65f * d;
}

static Gfx *tilesRenderPoly(Gfx *gdl, s32 room, const f32 (*verts)[3], s32 numverts)
{
	f32 r, g, b, shade;
	u32 rgba;
	Col *colours;
	Vtx *vertices;
	s32 base;
	s32 i;

	if (numverts < 3) {
		return gdl;
	}

	tilesRenderRoomColour(room, &r, &g, &b);
	shade = g_TilesRenderMode == 1 ? tilesRenderShade(verts[0], verts[1], verts[2]) : 0.9f;

	rgba = ((u32)(r * shade * 255.0f) << 24)
		| ((u32)(g * shade * 255.0f) << 16)
		| ((u32)(b * shade * 255.0f) << 8)
		| 0xff;

	colours = gfxAllocateColours(1);
	colours[0].word = PD_BE32(rgba);
	gSPColor(gdl++, osVirtualToPhysical(colours), 1);

	/* gSPTri4 indexes are 4 bits: load runs of up to 16 with vertex 0 in
	 * slot 0 of every run, and fan from it */
	for (base = 1; base < numverts - 1; base += 15) {
		s32 count = numverts - base;
		s32 n;

		if (count > 15) count = 15;
		n = count + 1;

		vertices = gfxAllocateVertices(n);

		vertices[0].x = (s16)verts[0][0];
		vertices[0].y = (s16)verts[0][1];
		vertices[0].z = (s16)verts[0][2];
		vertices[0].flags = 0;
		vertices[0].colour = 0;
		vertices[0].s = 0;
		vertices[0].t = 0;

		for (i = 0; i < count; i++) {
			vertices[i + 1].x = (s16)verts[base + i][0];
			vertices[i + 1].y = (s16)verts[base + i][1];
			vertices[i + 1].z = (s16)verts[base + i][2];
			vertices[i + 1].flags = 0;
			vertices[i + 1].colour = 0;
			vertices[i + 1].s = 0;
			vertices[i + 1].t = 0;
		}

		gSPVertex(gdl++, osVirtualToPhysical(vertices), n, 0);

		/* triangles (0, k, k+1) for k in 1..n-2, four per packet; an
		 * all-zero triple is "no triangle", which is what gSPTri3/2/1
		 * already rely on */
		for (i = 1; i + 1 < n; i += 4) {
			s32 a[4][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
			s32 k;

			for (k = 0; k < 4 && i + k + 1 < n; k++) {
				a[k][0] = 0;
				a[k][1] = i + k;
				a[k][2] = i + k + 1;
			}

			gSPTri4(gdl++,
					a[0][0], a[0][1], a[0][2],
					a[1][0], a[1][1], a[1][2],
					a[2][0], a[2][1], a[2][2],
					a[3][0], a[3][1], a[3][2]);
		}
	}

	return gdl;
}

Gfx *tilesRender(Gfx *gdl)
{
	Mtxf *mtx;
	Mtxf worldtoscreen;
	s32 room;
	f32 verts[64][3];

	if (g_TilesRenderMode == 0 || !g_TileFileData.u8 || !g_TileRooms || g_TileNumRooms <= 0) {
		return gdl;
	}

	gDPPipeSync(gdl++);
	gDPSetCycleType(gdl++, G_CYC_1CYCLE);
	gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
	gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
	gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
	gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_LIGHTING | G_TEXTURE_GEN);
	gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);

	mtx = gfxAllocateMatrix();
	mtx4LoadIdentity(&worldtoscreen);
	mtx00015be0(camGetWorldToScreenMtxf(), &worldtoscreen);
	mtxF2L(&worldtoscreen, mtx);
	gSPMatrix(gdl++, osVirtualToPhysical(mtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

	for (room = 0; room < g_TileNumRooms; room++) {
		struct geo *geo = (struct geo *)(g_TileFileData.u8 + g_TileRooms[room]);
		struct geo *end = (struct geo *)(g_TileFileData.u8 + g_TileRooms[room + 1]);

		/* rooms the portal walk put on screen this frame, unless asked
		 * for the lot; a level with no bg has no walk, so on-screen is
		 * empty and every room is what you get */
		if (!g_TilesRenderAllRooms && g_Rooms && room > 0 && room < g_Vars.roomcount
				&& !(g_Rooms[room].flags & ROOMFLAG_ONSCREEN)) {
			continue;
		}

		while (geo < end) {
			s32 i;

			if (geo->type == GEOTYPE_TILE_I) {
				struct geotilei *tile = (struct geotilei *)geo;
				s32 n = geo->numvertices > 64 ? 64 : geo->numvertices;

				for (i = 0; i < n; i++) {
					verts[i][0] = tile->vertices[i][0];
					verts[i][1] = tile->vertices[i][1];
					verts[i][2] = tile->vertices[i][2];
				}

				gdl = tilesRenderPoly(gdl, room, verts, n);
				geo = (struct geo *)((u8 *)geo + 0x0e + geo->numvertices * 6); /* tilesreset.c:44 */
			} else if (geo->type == GEOTYPE_TILE_F) {
				struct geotilef *tile = (struct geotilef *)geo;
				s32 n = geo->numvertices > 64 ? 64 : geo->numvertices;

				for (i = 0; i < n; i++) {
					verts[i][0] = tile->vertices[i].x;
					verts[i][1] = tile->vertices[i].y;
					verts[i][2] = tile->vertices[i].z;
				}

				gdl = tilesRenderPoly(gdl, room, verts, n);
				geo = (struct geo *)((u8 *)geo + 0x10 + geo->numvertices * 12);
			} else if (geo->type == GEOTYPE_BLOCK) {
				geo = (struct geo *)((u8 *)geo + sizeof(struct geoblock));
			} else if (geo->type == GEOTYPE_CYL) {
				geo = (struct geo *)((u8 *)geo + sizeof(struct geocyl));
			} else {
				break; /* not a shape we know; do not walk off the room */
			}
		}
	}

	gDPPipeSync(gdl++);

	return gdl;
}

#endif
