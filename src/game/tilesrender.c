#include <ultra64.h>
#include <math.h>
#include "constants.h"
#include "game/tiles.h"
#include "game/bg.h"
#include "game/camera.h"
#include "game/gfxmemory.h"
#include "gbiex.h"
#include "game/player.h"
#include "lib/mtx.h"
#include "system.h"
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
 * already, so nothing needs rebasing), and PD's 12-byte Vtx whose colour
 * byte is an offset into a Col block (fast3d reads colour >> 2).
 *
 * The tiles never move, so nothing here is built per frame. The first frame
 * after a tiles file loads (or after the mode changes the colours) builds
 * each room's mesh once into the heap as a display list of its own; every
 * frame after that is one gSPDisplayList per room on screen and one matrix.
 * It used to emit every tile into the frame's master list and vertex pool,
 * which on CI training with every room on was ~12.5k commands (~200 KiB of
 * a 240 KiB list) and ~267 KiB of vertices against a 98 KiB pool: the list
 * ran off its end and the next thing to draw - the pause menu - crashed.
 *
 * Inside a room's list, small tiles share loads: up to 16 vertices and 16
 * colours per gSPVertex/gSPColor pair (gSPTri4 indices are four bits), each
 * tile fanned from its own first vertex. A tile over 16 vertices is loaded
 * in runs with its vertex 0 repeated at the head of each run.
 *
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

/* One room's prebuilt mesh: gdl, vertices and colours in one allocation. */
struct tilesrenderroom {
	Gfx *gdl;
};

static struct tilesrenderroom *g_TilesRenderRooms = NULL;
static s32 g_TilesRenderNumRooms = 0;
static s32 g_TilesRenderBuiltMode = 0;
static u8 *g_TilesRenderBuiltFrom = NULL;
static s32 g_TilesRenderLoggedBudget = false;

/* A batch being filled: what one gSPColor + gSPVertex pair loads. */
struct tilesrenderbatch {
	Vtx verts[16];
	u32 cols[16];
	u8 tris[16][3];
	s32 nv;
	s32 nc;
	s32 nt;
};

/* Where flushed batches go. With gdl NULL it only counts, which is the
 * sizing pass. */
struct tilesrenderout {
	Gfx *gdl;
	Vtx *vtx;
	Col *col;
	s32 ngfx;
	s32 nvtx;
	s32 ncol;
};

static void tilesRenderFlush(struct tilesrenderbatch *b, struct tilesrenderout *out)
{
	s32 i;

	if (b->nv == 0) {
		return;
	}

	if (out->gdl) {
		Vtx *vtx = out->vtx + out->nvtx;
		Col *col = out->col + out->ncol;

		for (i = 0; i < b->nv; i++) {
			vtx[i] = b->verts[i];
		}

		for (i = 0; i < b->nc; i++) {
			col[i].word = PD_BE32(b->cols[i]);
		}

		gSPColor(out->gdl++, osVirtualToPhysical(col), b->nc);
		gSPVertex(out->gdl++, osVirtualToPhysical(vtx), b->nv, 0);

		/* four per packet; an all-zero triple is "no triangle", which is
		 * what gSPTri3/2/1 already rely on */
		for (i = 0; i < b->nt; i += 4) {
			u8 a[4][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
			s32 k;

			for (k = 0; k < 4 && i + k < b->nt; k++) {
				a[k][0] = b->tris[i + k][0];
				a[k][1] = b->tris[i + k][1];
				a[k][2] = b->tris[i + k][2];
			}

			gSPTri4(out->gdl++,
					a[0][0], a[0][1], a[0][2],
					a[1][0], a[1][1], a[1][2],
					a[2][0], a[2][1], a[2][2],
					a[3][0], a[3][1], a[3][2]);
		}
	}

	out->ngfx += 2 + (b->nt + 3) / 4;
	out->nvtx += b->nv;
	out->ncol += b->nc;

	b->nv = 0;
	b->nc = 0;
	b->nt = 0;
}

static void tilesRenderPutVert(struct tilesrenderbatch *b, const f32 *v, s32 ci)
{
	Vtx *d = &b->verts[b->nv++];

	d->x = (s16)v[0];
	d->y = (s16)v[1];
	d->z = (s16)v[2];
	d->flags = 0;
	d->colour = ci * 4;
	d->s = 0;
	d->t = 0;
}

static void tilesRenderPoly(struct tilesrenderbatch *b, struct tilesrenderout *out,
		s32 room, const f32 (*verts)[3], s32 numverts)
{
	f32 r, g, bl, shade;
	u32 rgba;
	s32 base, ci, i;

	if (numverts < 3) {
		return;
	}

	tilesRenderRoomColour(room, &r, &g, &bl);
	shade = g_TilesRenderMode == 1 ? tilesRenderShade(verts[0], verts[1], verts[2]) : 0.9f;

	rgba = ((u32)(r * shade * 255.0f) << 24)
		| ((u32)(g * shade * 255.0f) << 16)
		| ((u32)(bl * shade * 255.0f) << 8)
		| 0xff;

	if (numverts <= 16) {
		/* shares a load with its neighbours when it fits */
		if (b->nv + numverts > 16 || b->nc >= 16) {
			tilesRenderFlush(b, out);
		}

		ci = b->nc++;
		b->cols[ci] = rgba;
		base = b->nv;

		for (i = 0; i < numverts; i++) {
			tilesRenderPutVert(b, verts[i], ci);
		}

		for (i = 1; i + 1 < numverts; i++) {
			b->tris[b->nt][0] = base;
			b->tris[b->nt][1] = base + i;
			b->tris[b->nt][2] = base + i + 1;
			b->nt++;
		}

		return;
	}

	/* too big to share: runs of 16 with vertex 0 at the head of each */
	tilesRenderFlush(b, out);

	for (base = 1; base < numverts - 1; base += 15) {
		s32 count = numverts - base;

		if (count > 15) count = 15;

		b->cols[0] = rgba;
		b->nc = 1;
		tilesRenderPutVert(b, verts[0], 0);

		for (i = 0; i < count; i++) {
			tilesRenderPutVert(b, verts[base + i], 0);
		}

		for (i = 1; i + 1 < count + 1; i++) {
			b->tris[b->nt][0] = 0;
			b->tris[b->nt][1] = i;
			b->tris[b->nt][2] = i + 1;
			b->nt++;
		}

		tilesRenderFlush(b, out);
	}
}

/* Walk one room's tiles into out: counting when out->gdl is NULL. */
static void tilesRenderWalkRoom(s32 room, struct tilesrenderout *out)
{
	struct geo *geo = (struct geo *)(g_TileFileData.u8 + g_TileRooms[room]);
	struct geo *end = (struct geo *)(g_TileFileData.u8 + g_TileRooms[room + 1]);
	struct tilesrenderbatch b;
	f32 verts[64][3];

	b.nv = 0;
	b.nc = 0;
	b.nt = 0;

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

			tilesRenderPoly(&b, out, room, verts, n);
			geo = (struct geo *)((u8 *)geo + 0x0e + geo->numvertices * 6); /* tilesreset.c:44 */
		} else if (geo->type == GEOTYPE_TILE_F) {
			struct geotilef *tile = (struct geotilef *)geo;
			s32 n = geo->numvertices > 64 ? 64 : geo->numvertices;

			for (i = 0; i < n; i++) {
				verts[i][0] = tile->vertices[i].x;
				verts[i][1] = tile->vertices[i].y;
				verts[i][2] = tile->vertices[i].z;
			}

			tilesRenderPoly(&b, out, room, verts, n);
			geo = (struct geo *)((u8 *)geo + 0x10 + geo->numvertices * 12);
		} else if (geo->type == GEOTYPE_BLOCK) {
			geo = (struct geo *)((u8 *)geo + sizeof(struct geoblock));
		} else if (geo->type == GEOTYPE_CYL) {
			geo = (struct geo *)((u8 *)geo + sizeof(struct geocyl));
		} else {
			break; /* not a shape we know; do not walk off the room */
		}
	}

	tilesRenderFlush(&b, out);
}

#define TILESRENDER_ALIGN16(x) (((x) + 15) & ~(uintptr_t)15)

/**
 * Drop the prebuilt meshes. tilesReset calls this when a tiles file loads,
 * because a reload of the same stage can hand back the same address with
 * different bytes behind it.
 */
void tilesRenderInvalidate(void)
{
	s32 i;

	if (g_TilesRenderRooms) {
		for (i = 0; i < g_TilesRenderNumRooms; i++) {
			if (g_TilesRenderRooms[i].gdl) {
				sysMemFree(g_TilesRenderRooms[i].gdl);
			}
		}

		sysMemFree(g_TilesRenderRooms);
	}

	g_TilesRenderRooms = NULL;
	g_TilesRenderNumRooms = 0;
	g_TilesRenderBuiltFrom = NULL;
	g_TilesRenderLoggedBudget = false;
}

static void tilesRenderBuild(void)
{
	s32 room;
	u32 total = 0;

	tilesRenderInvalidate();

	g_TilesRenderRooms = sysMemZeroAlloc(sizeof(struct tilesrenderroom) * g_TileNumRooms);

	if (!g_TilesRenderRooms) {
		return;
	}

	g_TilesRenderNumRooms = g_TileNumRooms;
	g_TilesRenderBuiltFrom = g_TileFileData.u8;
	g_TilesRenderBuiltMode = g_TilesRenderMode;

	for (room = 0; room < g_TileNumRooms; room++) {
		struct tilesrenderout out = { NULL, NULL, NULL, 0, 0, 0 };
		uintptr_t gfxsize, vtxsize, colsize;
		u8 *mem;

		tilesRenderWalkRoom(room, &out);

		if (out.ngfx == 0) {
			continue;
		}

		gfxsize = TILESRENDER_ALIGN16((out.ngfx + 1) * sizeof(Gfx));
		vtxsize = TILESRENDER_ALIGN16(out.nvtx * sizeof(Vtx));
		colsize = TILESRENDER_ALIGN16(out.ncol * sizeof(Col));

		mem = sysMemAlloc(gfxsize + vtxsize + colsize);

		if (!mem) {
			continue;
		}

		out.gdl = (Gfx *)mem;
		out.vtx = (Vtx *)(mem + gfxsize);
		out.col = (Col *)(mem + gfxsize + vtxsize);
		out.ngfx = 0;
		out.nvtx = 0;
		out.ncol = 0;

		g_TilesRenderRooms[room].gdl = (Gfx *)mem;
		tilesRenderWalkRoom(room, &out);
		gSPEndDisplayList(out.gdl++);

		total += gfxsize + vtxsize + colsize;
	}

	sysLogPrintf(LOG_NOTE, "tiles: built %d rooms of tile mesh, %u KiB", g_TileNumRooms, total / 1024);
}

Gfx *tilesRender(Gfx *gdl)
{
	Mtxf *mtx;
	Mtxf worldtoscreen;
	s32 room;

	if (g_TilesRenderMode == 0 || !g_TileFileData.u8 || !g_TileRooms || g_TileNumRooms <= 0) {
		return gdl;
	}

	if (!g_TilesRenderRooms
			|| g_TilesRenderBuiltFrom != g_TileFileData.u8
			|| g_TilesRenderNumRooms != g_TileNumRooms
			|| g_TilesRenderBuiltMode != g_TilesRenderMode) {
		tilesRenderBuild();

		if (!g_TilesRenderRooms) {
			return gdl;
		}
	}

	/* one command per room plus the setup, and a wide margin for the HUD
	 * and menus that draw after us: the master list has no bounds check */
	if (gfxGetFreeGfx(gdl) < g_TileNumRooms + 32 + 4096) {
		if (!g_TilesRenderLoggedBudget) {
			g_TilesRenderLoggedBudget = true;
			sysLogPrintf(LOG_WARNING, "tiles: master display list nearly full (%d free), tiles skipped",
					gfxGetFreeGfx(gdl));
		}

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
		/* rooms the portal walk put on screen this frame, unless asked
		 * for the lot; a level with no bg has no walk, so on-screen is
		 * empty and every room is what you get */
		if (!g_TilesRenderAllRooms && g_Rooms && room > 0 && room < g_Vars.roomcount
				&& !(g_Rooms[room].flags & ROOMFLAG_ONSCREEN)) {
			continue;
		}

		if (g_TilesRenderRooms[room].gdl) {
			gSPDisplayList(gdl++, g_TilesRenderRooms[room].gdl);
		}
	}

	gDPPipeSync(gdl++);

	/* hand back the modelview bgRender left for everything after us */
	gdl = playerLoadMatrix(gdl);

	return gdl;
}

#endif
