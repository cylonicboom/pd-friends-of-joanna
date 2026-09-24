/**
 * pd.* API, fx group: screen effects (fades, shakes, the renderer and
 * post-filter effects, HUD toggles) and the draw helpers (sprites, external
 * images, the texture override, HUD messages).
 *
 * From the Perfect Dark Kai fork (be46717), where the whole API was one file,
 * src/game/luaai_api.c. The comments are Kai's. The core registers this group
 * through luaApiRegisterFx and calls the hooks at the bottom (image overlays,
 * overlay text, per-state reset).
 *
 * External images: Kai opens scripts/chaos/images/<name> relative to the
 * working directory. Here extImageLoad (port/src/ext_tex.c) looks in that
 * subdirectory of each mod dir, then the base dir, then the working
 * directory, and only takes plain file names.
 */

#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "ext_tex.h"
#include "video.h"
#include "game/chaosstate.h"
#include "game/gfxmemory.h"
#include "game/hudmsg.h"
#include "game/lang.h"
#include "game/luaai.h"
#include "game/tex.h"
#include "lib/vi.h"
#include "luaai_api_internal.h"

#ifndef PLATFORM_N64

/* External images loaded from scripts/chaos/images/ via pd.load_image. Each
 * keeps its own RGBA5551 buffer so pd.draw_image can blit it. This is a
 * loading HOOK for future effects. */
#define LUA_MAX_IMAGES 16
struct luaimage {
	u8 *data;   /* owned RGBA5551, big-endian (freed on state reset) */
	u32 w, h;   /* real pixel dimensions */
};
static struct luaimage g_LuaImages[LUA_MAX_IMAGES];
static s32 g_LuaImageCount = 0;

/* ------------------------------------------------------------------------- *
 * Draw helpers
 * ------------------------------------------------------------------------- */

/* pd.draw_sprite(texnum, x, y, w, h, [color], [secs]). Paint a game wall-hit
 * texture (e.g. WALLHITTEX_BLOOD1..4 = 0x09..0x0c) as a HUD quad, tinted by
 * color (default black-opaque). Used by Blooper to splat black blood on the
 * screen. */
static int l_pd_draw_sprite(lua_State *L)
{
	s32 texnum = (s32)luaL_checkinteger(L, 1);
	s32 x = (s32)luaL_checkinteger(L, 2);
	s32 y = (s32)luaL_checkinteger(L, 3);
	s32 w = (s32)luaL_checkinteger(L, 4);
	s32 h = (s32)luaL_checkinteger(L, 5);
	u32 color = (u32)luaL_optinteger(L, 6, 0x000000ffu);
	f32 secs = luaApiOptNum(L, 7, 0.0f);

	luaOverlayAdd(OVL_SPRITE, x, y, w, h, color, NULL, texnum, 0.f, secs);
	return 0;
}

/* Build "<name>[.png]" for extImageLoad. */
static void luaImageFileName(char *out, size_t outlen, const char *name)
{
	const char *dot = strrchr(name, '.');

	snprintf(out, outlen, "%s%s", name, dot ? "" : ".png");
}

/* pd.tex_override([name]): replace EVERY game texture's RGB with an external
 * image (scripts/chaos/images/<name>[.png]) via flat-texture mode 3 — the
 * renderer re-imports the whole texture cache through the override filter
 * (per-pixel alpha preserved, so cutouts/glyphs keep their shapes). No arg =
 * restore normal textures and free the image. */
static u8 *g_LuaTexOverride = NULL;

void luaTexOverrideReset(void)
{
	extern s32 gfx_flattex_mode;
	extern u8 *gfx_flattex_image;

	if (gfx_flattex_mode == 3) {
		gfx_flattex_mode = 0;
	}
	gfx_flattex_image = NULL;
	if (g_LuaTexOverride) {
		extImageFree(g_LuaTexOverride);
		g_LuaTexOverride = NULL;
	}
}

static int l_pd_tex_override(lua_State *L)
{
	extern s32 gfx_flattex_mode;
	extern u8 *gfx_flattex_image;
	extern s32 gfx_flattex_image_w;
	extern s32 gfx_flattex_image_h;
	const char *name = luaL_optstring(L, 1, NULL);
	char file[256];
	u8 *data;
	u32 w = 0, h = 0;
	s32 wasactive = (gfx_flattex_mode == 3 && g_LuaTexOverride != NULL);

	luaTexOverrideReset();

	if (name == NULL) {
		lua_pushboolean(L, 1);
		return 1;
	}

	luaImageFileName(file, sizeof(file), name);

	data = extImageLoad(file, &w, &h);
	if (!data || w == 0 || h == 0) {
		if (data) extImageFree(data);
		lua_pushboolean(L, 0);
		return 1;
	}

	if (wasactive) {
		/* Swapping one override for another leaves the mode at 3, which the
		 * renderer's frame-boundary check can't see: drop the textures that
		 * were stamped with the old image now. */
		videoResetTextureCache();
	}

	g_LuaTexOverride = data;
	gfx_flattex_image = data;
	gfx_flattex_image_w = (s32)w;
	gfx_flattex_image_h = (s32)h;
	gfx_flattex_mode = 3; // mode change -> gfx_start_frame clears the texture cache
	lua_pushboolean(L, 1);
	return 1;
}

/* pd.load_image(name) -> handle | nil. Load a PNG from scripts/chaos/images/<name>
 * (".png" appended if it has no extension) into an RGBA texture and return an
 * opaque handle for pd.draw_image. A LOADING HOOK for future effects.
 * Handles + buffers are freed when the Lua state resets (stage change).
 * SIZE LIMIT: reliable max is 64x64. Past 64 wide the single-tile load halves
 * the rows (top part shows doubled), so keep both dimensions <= 64 and
 * power-of-two. Oversize still loads but renders partial (a warning is logged).
 * To fill a bigger area, draw a 64x64 source at a larger w/h (draw_image scales). */
static int l_pd_load_image(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	char file[256];
	struct luaimage *im;
	u8 *data;
	u32 w = 0, h = 0;

	if (g_LuaImageCount >= LUA_MAX_IMAGES) {
		lua_pushnil(L);
		return 1;
	}

	luaImageFileName(file, sizeof(file), name);

	data = extImageLoad(file, &w, &h);
	if (!data || w == 0 || h == 0) {
		if (data) extImageFree(data);
		lua_pushnil(L);
		return 1;
	}

	// Reliable max is 64x64: the single-tile load path halves the rows once
	// the width goes past 64 (the top part shows doubled). Warn but still load
	// so oversize is obvious in-game. Use power-of-two dimensions.
	if (w > 64 || h > 64) {
		char msg[192];
		snprintf(msg, sizeof(msg),
				"pd.load_image('%s'): %ux%u exceeds the reliable 64x64 max - "
				"it will render only partially (top rows doubled).",
				file, w, h);
		luaApiLog(msg);
	}

	// Convert RGBA8888 -> RGBA5551 (16-bit, big-endian) — the game's normal
	// texture format. Kai found the RGBA32 load path computing the wrong tile
	// dimensions for raw configs; the RGBA16 path sizes correctly. Best on
	// power-of-two dimensions.
	{
		u8 *rgba16 = (u8 *)malloc((size_t)w * h * 2);
		u32 i, n = w * h;
		if (!rgba16) {
			extImageFree(data);
			lua_pushnil(L);
			return 1;
		}
		for (i = 0; i < n; i++) {
			u8 r = data[i * 4 + 0], g = data[i * 4 + 1], b = data[i * 4 + 2], a = data[i * 4 + 3];
			u16 v = (u16)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >> 7));
			rgba16[i * 2 + 0] = (u8)(v >> 8);
			rgba16[i * 2 + 1] = (u8)(v & 0xff);
		}
		extImageFree(data); // done with the 8888 source

		im = &g_LuaImages[g_LuaImageCount];
		im->data = rgba16;
		im->w = w;
		im->h = h;
	}

	lua_pushinteger(L, g_LuaImageCount);
	g_LuaImageCount++;
	return 1;
}

/* pd.draw_image(handle, cx, cy, w, h, [angle_deg], [color], [secs]). Blit a
 * pd.load_image texture as a w*h HUD quad CENTRED at (cx,cy), rotated angle_deg
 * degrees. Default color = white opaque (untinted); pass a color to tint. */
static int l_pd_draw_image(lua_State *L)
{
	s32 handle = (s32)luaL_checkinteger(L, 1);
	s32 cx = (s32)luaL_checkinteger(L, 2);
	s32 cy = (s32)luaL_checkinteger(L, 3);
	s32 w = (s32)luaL_checkinteger(L, 4);
	s32 h = (s32)luaL_checkinteger(L, 5);
	/* deg -> rad. 4 turns either way is as much as a spin reads as. */
	f32 angle = luaApiOptNumR(L, 6, 0.0f, -1440.0f, 1440.0f) * (f32)(3.14159265358979 / 180.0);
	u32 color = (u32)luaL_optinteger(L, 7, 0xffffffffu);
	f32 secs = luaApiOptNum(L, 8, 0.0f);

	luaOverlayAdd(OVL_IMAGE, cx, cy, w, h, color, NULL, handle, angle, secs);
	return 0;
}

/* Longest unbroken run of non-space bytes pd.hud_message will emit before
 * inserting a soft space. textWrap's per-word scratch is char[32] and it
 * keeps two bytes for a multibyte pair plus the terminator. */
#define LUA_HUDMSG_MAXWORD 28

/* pd.hud_message(text, [type]): HUD message. Default type is
 * HUDMSGTYPE_DEFAULT — the standard BOTTOM line (pickup style) — so chatter
 * stays out of the middle of the screen; pass an explicit type
 * (1 = objective complete, 2 = objective failed) for the big centred banner.
 * No-op when there's no live local player. */
static int l_pd_hud_message(lua_State *L)
{
	const char *text = luaL_checkstring(L, 1);
	s32 type = (s32)luaL_optinteger(L, 2, HUDMSGTYPE_DEFAULT);
	char buf[256];
	size_t n = 0;
	size_t i;
	s32 run = 0;

	// The hudmsg text MUST end with '\n': textMeasure only advances the height
	// on a newline, so a message without one measures height 0 and its box
	// collapses to a sliver under the text (and centred types mis-position).
	// The engine's own messages are all '\n'-terminated; Lua strings aren't.
	//
	// Word-break as we copy: textWrap accumulates each whitespace-delimited
	// word into a 32-byte stack buffer, so a script passing one long unbroken
	// run (string.rep("W", 200)) walks off it. textWrap now drops the tail of
	// an over-long word; break it here as well so the text still renders in
	// full instead of being silently truncated at 30 characters.
	for (i = 0; text[i] && n < sizeof(buf) - 2; i++) {
		char c = text[i];

		// Unsigned: luaApiTextScrub below turns every byte >= 0x80 into '?',
		// a word character, so they have to count toward the run here too.
		if ((u8)c > ' ') {
			if (run >= LUA_HUDMSG_MAXWORD) {
				buf[n++] = ' ';
				run = 0;

				if (n >= sizeof(buf) - 2) {
					break;
				}
			}

			run++;
		} else {
			run = 0;
		}

		buf[n++] = c;
	}

	// The HUD font is ASCII-only. Any byte >= 0x80 is routed by the text
	// renderer into the JPN multibyte glyph path, whose cache table is NULL in
	// a non-JPN ROM -> null deref crash. Scripts feed arbitrary text here, so
	// scrub high bytes to '?' before it reaches the hudmsg queue.
	buf[n] = '\0';
	luaApiTextScrub(buf);

	if (n == 0 || buf[n - 1] != '\n') {
		buf[n++] = '\n';
	}
	buf[n] = '\0';

	hudmsgCreateLua(buf, type);
	return 0;
}

/* pd.list_images() -> { "gras", "red", ... }. The basenames (no extension) of
 * every .png in the scripts/chaos/images/ dirs. */
static int l_pd_list_images(lua_State *L)
{
	static char names[64][64];
	s32 n = extImageList(names, 64);
	s32 i;

	lua_createtable(L, n, 0);
	for (i = 0; i < n; i++) {
		lua_pushstring(L, names[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

/* ------------------------------------------------------------------------- *
 * Screen effects
 * ------------------------------------------------------------------------- */

/* pd.fade(r,g,b,a,time60) -> bool. Viewport fade (flashbang/blink effects). */
static int l_pd_fade(lua_State *L)
{
	s32 r = (s32)luaL_checkinteger(L, 1);
	s32 g = (s32)luaL_checkinteger(L, 2);
	s32 b = (s32)luaL_checkinteger(L, 3);
	s32 a = (s32)luaL_checkinteger(L, 4);
	f32 time60 = luaApiNum(L, 5);
	lua_pushboolean(L, chraiLuaScreenFade(r, g, b, a, time60) != 0);
	return 1;
}

/* pd.flattex(mode) -> bool. 0 normal / 1 white textures (vertex shading only)
 * / 2 average-colour textures. Cosmetic only. */
static int l_pd_flattex(lua_State *L)
{
	s32 mode = (s32)luaL_optinteger(L, 1, 0);
	lua_pushboolean(L, chraiLuaFlatTex(mode) != 0);
	return 1;
}

/* pd.grayscale(on) -> bool. Force the renderer grayscale path (film noir). */
static int l_pd_grayscale(lua_State *L)
{
	lua_pushboolean(L, chraiLuaGrayscale(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.shiny(mode) -> bool. 0 off / 1 fake-chrome screen-space UVs on every 3D
 * surface / 2 the same plus a gold tint (Midas mode). Cosmetic only. */
static int l_pd_shiny(lua_State *L)
{
	s32 mode = (s32)luaL_optinteger(L, 1, 0);
	lua_pushboolean(L, chraiLuaShiny(mode) != 0);
	return 1;
}

/* pd.hud_squish(frac) -> bool. Scale every 2D HUD/text rect toward the
 * horizontal centre (Vertical Form Content; 0.425 matches the pirate-mode-4
 * pillars). 0/absent = off. */
static int l_pd_hud_squish(lua_State *L)
{
	/* a fraction of the HUD height; past a few the HUD is off screen */
	lua_pushboolean(L, chraiLuaHudSquish(luaApiOptNumR(L, 1, 0.0f, 0.0f, 8.0f)) != 0);
	return 1;
}

/* pd.terminator(on) -> bool. Terminator Vision: infrared filter with NO goggle
 * cutout, and the CMP150 threat-detector boxes on whatever gun is held. */
static int l_pd_terminator(lua_State *L)
{
	lua_pushboolean(L, chraiLuaTerminator(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.chr_wireframe(on) -> bool. Hostile chrs render as polygon outlines. */
static int l_pd_chr_wireframe(lua_State *L)
{
	lua_pushboolean(L, chraiLuaChrWireframe(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.fps_cap(fps) -> bool. OG mode: hard render-FPS override; the sim's
 * variable tick soaks the low rate like the N64 did. 0 or no arg restores. */
static int l_pd_fps_cap(lua_State *L)
{
	lua_pushboolean(L, chraiLuaFpsCap((s32)luaL_optinteger(L, 1, 0)) != 0);
	return 1;
}

/* pd.internal_res(height) -> bool. OG mode: TRUE internal render resolution —
 * the frame renders at this height and NEAREST-upscales to the window.
 * 0 or no arg restores. */
static int l_pd_internal_res(lua_State *L)
{
	lua_pushboolean(L, chraiLuaInternalRes((s32)luaL_optinteger(L, 1, 0)) != 0);
	return 1;
}

/* pd.paintball(on) -> bool. Force paintball visuals for everyone. */
static int l_pd_paintball(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPaintball(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.shake(ticks) -> bool. Explosion-style screen shake for N ticks. */
static int l_pd_shake(lua_State *L)
{
	lua_pushboolean(L, chraiLuaShake((s32)luaL_optinteger(L, 1, 24)) != 0);
	return 1;
}

/* pd.screen_tint(r, g, b) -> bool. Full-screen luminance tint (sepia,
 * terminal green, ...). pd.screen_tint() clears it. */
static int l_pd_screen_tint(lua_State *L)
{
	if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
		lua_pushboolean(L, chraiLuaScreenTint(0, 0, 0, 0) != 0);
		return 1;
	}
	lua_pushboolean(L, chraiLuaScreenTint(
			(s32)luaL_checkinteger(L, 1),
			(s32)luaL_checkinteger(L, 2),
			(s32)luaL_checkinteger(L, 3), 1) != 0);
	return 1;
}

/* pd.upside_down(on) -> bool. Australia mode: rotate the whole frame 180 and
 * reverse the controls. */
static int l_pd_upside_down(lua_State *L)
{
	lua_pushboolean(L, chraiLuaUpsideDown(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.screen_roll(deg) -> bool. Do a Barrel Roll: rotate the 3D view about the
 * screen centre by an absolute angle in degrees (0 = upright/off). Animate by
 * re-setting each tick. */
static int l_pd_screen_roll(lua_State *L)
{
	f32 deg = luaApiOptNumR(L, 1, 0.0f, -3600.0f, 3600.0f);
	lua_pushboolean(L, chraiLuaScreenRoll(deg) != 0);
	return 1;
}

/* pd.fake_crash(secs) -> bool. Freeze the sim for secs of real time, so the
 * game looks hung. Self-releasing (see chraiLuaFakeCrash) — there is
 * deliberately no off switch. */
static int l_pd_fake_crash(lua_State *L)
{
	lua_pushboolean(L, chraiLuaFakeCrash(luaApiOptNum(L, 1, 3.0f)) != 0);
	return 1;
}

/* pd.hud_off(on) -> bool. No HUD: hide every HUD element. */
static int l_pd_hud_off(lua_State *L)
{
	lua_pushboolean(L, chraiLuaHudOff(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.ipod_ad(on [, r, g, b]) -> bool. Silhouette mode: bright walls, black
 * chrs, white objects/weapons, white wireframe edges. */
static int l_pd_ipod_ad(lua_State *L)
{
	s32 on = lua_toboolean(L, 1);
	s32 r = (s32)luaL_optinteger(L, 2, 0);
	s32 g = (s32)luaL_optinteger(L, 3, 217);
	s32 b = (s32)luaL_optinteger(L, 4, 140);
	lua_pushboolean(L, chraiLuaIpodAd(on, r, g, b) != 0);
	return 1;
}

/* pd.double_vision(on) -> bool. One too many: blend rotated ghosts of the
 * frame over the normal one (drunk double-vision). */
static int l_pd_double_vision(lua_State *L)
{
	lua_pushboolean(L, chraiLuaDoubleVision(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.pixelate(w, h, colours) -> bool. Pixelate the rendered frame down to a
 * w x h grid; colours 4 = 4-level greyscale, 256 = 256-colour RGB 3-3-2,
 * 1000 = invert, 1001 = Game Boy greens, 1002 = thermal palette, 0/absent =
 * keep colours. w = 0 with a colour set = colour mode at full resolution.
 * pd.pixelate() turns it off. */
static int l_pd_pixelate(lua_State *L)
{
	if (lua_gettop(L) == 0 || lua_isnil(L, 1)) {
		lua_pushboolean(L, chraiLuaPixelate(0, 0, 0) != 0);
		return 1;
	}
	lua_pushboolean(L, chraiLuaPixelate(
			(s32)luaL_checkinteger(L, 1),
			(s32)luaL_checkinteger(L, 2),
			(s32)luaL_optinteger(L, 3, 0)) != 0);
	return 1;
}

/* pd.screen_fx(bits, on) -> bool. Set/clear post-filter effect bits: 1 =
 * scanlines, 2 = RGB grille, 4 = CRT curvature, 8 = vignette, 16 = VHS,
 * 32 = underwater wobble. Bits compose across effects. */
static int l_pd_screen_fx(lua_State *L)
{
	lua_pushboolean(L, chraiLuaScreenFx(
			(s32)luaL_checkinteger(L, 1), lua_toboolean(L, 2)) != 0);
	return 1;
}

/* pd.pirate(side) -> bool. "Pirate" eyepatch: black out one half of the finished
 * frame (HUD included, as a post-process). side 1 = left, 2 = right, 0/absent =
 * off. */
static int l_pd_pirate(lua_State *L)
{
	lua_pushboolean(L, chraiLuaPirate((s32)luaL_optinteger(L, 1, 0)) != 0);
	return 1;
}

/* pd.half_mirror(side) -> bool. Mirror one half of the finished frame onto the
 * other about the vertical centre line (HUD included, as a post-process).
 * side 1 = left half onto the right, 2 = right half onto the left, 0/absent =
 * off. */
static int l_pd_half_mirror(lua_State *L)
{
	lua_pushboolean(L, chraiLuaHalfMirror((s32)luaL_optinteger(L, 1, 0)) != 0);
	return 1;
}

/* pd.vertex_wobble([amp, freq, phase, sag, desync, nearfade]) -> bool.
 * "Jelly"/"Acid": deform every vertex in eye space by sines of position. amp
 * world units (0/absent = off), freq radians per world unit, phase the
 * animation angle (advance it each tick), sag an extra always-downward melt
 * droop (world units), desync a per-vertex rate spread (0 = lockstep; higher =
 * vertices flow at different speeds and arrive out of step). */
static int l_pd_vertex_wobble(lua_State *L)
{
	f32 amp = luaApiOptNum(L, 1, 0.0f);
	/* radians per world unit: past a few the wobble is per-vertex noise */
	f32 freq = luaApiOptNumR(L, 2, 0.03f, -16.0f, 16.0f);
	/* the animation angle, advanced by the caller every tick; it wraps */
	f32 phase = luaApiOptNumR(L, 3, 0.0f, -1.0e5f, 1.0e5f);
	f32 sag = luaApiOptNum(L, 4, 0.0f);
	f32 desync = luaApiOptNum(L, 5, 0.0f);
	/* nearfade: world-unit radius the wobble ramps in over, so geometry close
	 * to the camera barely strays from its true position. 0 = off. */
	f32 nearfade = luaApiOptNumR(L, 6, 0.0f, 0.0f, 1.0e5f); /* world units */
	lua_pushboolean(L, chraiLuaVertexWobble(amp, freq, phase, sag, desync, nearfade) != 0);
	return 1;
}

/* pd.hall_of_mirrors(on) -> bool. Skip the framebuffer colour clear so the frame
 * smears (Doom HOM / acid-trip trails). */
static int l_pd_hall_of_mirrors(lua_State *L)
{
	lua_pushboolean(L, chraiLuaHallOfMirrors(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.texlabels(n) -> prev. Name tags on the n (max 64) textures nearest the
 * screen centre: the overlay draws a marker and "0x<texnum>" (bg) or
 * "0x<texnum> m:0x<model file num>" (model) at each. 0 = off. Cleared on a
 * stage change with the other renderer knobs. */
static int l_pd_texlabels(lua_State *L)
{
	lua_pushinteger(L, chraiLuaTexLabels((s32)luaL_optinteger(L, 1, 0)));
	return 1;
}

/* pd.tex_visible([n]) -> array of {texnum=, id=, type=, x=, y=, tris=}, the
 * textures drawn last frame, nearest the screen centre first, at most n (max
 * 64, default 16). x,y are fractions of the window (0..1, top-left origin);
 * id is the model file num (0 for bg); type is G_TEXTYPE_*. Works without
 * pd.texlabels: the call itself turns capture on for the next two frames, so
 * the first call after a quiet spell returns an empty array and the ones
 * after it are populated. */
static int l_pd_tex_visible(lua_State *L)
{
	struct videotexlabel labels[64];
	s32 max = (s32)luaL_optinteger(L, 1, 16);
	s32 n, i;

	if (max < 1) max = 1;
	if (max > 64) max = 64;

	videoRequestTexLabels();
	n = videoCollectTexLabels(labels, max);

	lua_createtable(L, n, 0);

	for (i = 0; i < n; i++) {
		lua_createtable(L, 0, 6);
		lua_pushinteger(L, labels[i].texnum);
		lua_setfield(L, -2, "texnum");
		lua_pushinteger(L, labels[i].id);
		lua_setfield(L, -2, "id");
		lua_pushinteger(L, labels[i].type);
		lua_setfield(L, -2, "type");
		lua_pushnumber(L, labels[i].x);
		lua_setfield(L, -2, "x");
		lua_pushnumber(L, labels[i].y);
		lua_setfield(L, -2, "y");
		lua_pushinteger(L, labels[i].tris);
		lua_setfield(L, -2, "tris");
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

/* pd.hudvd(on) -> nil. HUDVD: each HUD element group (health, crosshair,
 * ammo, radar, messages, active menu) bounces DVD-style in its own random
 * diagonal. Purely cosmetic — aim/hit-detection are untouched. */
static int l_pd_hudvd(lua_State *L)
{
	extern void hudvdSetActive(bool on);
	hudvdSetActive((bool)lua_toboolean(L, 1));
	return 0;
}

/* pd.crt(on) -> bool. The full CRT look: curved scanlines + RGB aperture
 * grille + tube curvature + vignette (screen_fx bits 1|2|4|8). */
static int l_pd_crt(lua_State *L)
{
	lua_pushboolean(L, chraiLuaScreenFx(1 | 2 | 4 | 8, lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.lens(k) -> bool. Fisheye lens warp (centre magnified, corners pinned);
 * k ~ 1.4 = peephole, negative = pincushion, 0/absent = off. */
static int l_pd_lens(lua_State *L)
{
	/* the bridge clamps to [-0.8, 4]; NaN passed both of its tests */
	lua_pushboolean(L, chraiLuaLens(luaApiOptNumR(L, 1, 0.0f, -0.8f, 4.0f)) != 0);
	return 1;
}

/* pd.t_pose(on) -> bool. Every skeletal model renders in its bind pose. */
static int l_pd_t_pose(lua_State *L)
{
	lua_pushboolean(L, chraiLuaTPose(lua_toboolean(L, 1)) != 0);
	return 1;
}

/* pd.aspect_scale([mult]) -> bool. Projection aspect multiplier: 2 = extra
 * wide, 0.5 = extra tall, 1 / no arg = normal. */
static int l_pd_aspect_scale(lua_State *L)
{
	f32 mult = luaApiOptNum(L, 1, 1.0f);
	lua_pushboolean(L, chraiLuaAspectScale(mult) != 0);
	return 1;
}

/* pd.fov_scale([mult]) -> bool. Vertical-FOV multiplier: >1 fisheye,
 * <1 tunnel vision, 1 / no arg = normal. */
static int l_pd_fov_scale(lua_State *L)
{
	f32 mult = luaApiOptNum(L, 1, 1.0f);
	lua_pushboolean(L, chraiLuaFovScale(mult) != 0);
	return 1;
}

static const luaL_Reg g_LuaApiFx[] = {
	/* draw */
	{ "draw_sprite",     l_pd_draw_sprite },
	{ "load_image",      l_pd_load_image },
	{ "tex_override",    l_pd_tex_override },
	{ "draw_image",      l_pd_draw_image },
	{ "hud_message",     l_pd_hud_message },
	{ "list_images",     l_pd_list_images },
	/* screen */
	{ "fade",            l_pd_fade },
	{ "flattex",         l_pd_flattex },
	{ "texlabels",       l_pd_texlabels },
	{ "tex_visible",     l_pd_tex_visible },
	{ "shiny",           l_pd_shiny },
	{ "hud_squish",      l_pd_hud_squish },
	{ "terminator",      l_pd_terminator },
	{ "chr_wireframe",   l_pd_chr_wireframe },
	{ "fps_cap",         l_pd_fps_cap },
	{ "internal_res",    l_pd_internal_res },
	{ "paintball",       l_pd_paintball },
	{ "shake",           l_pd_shake },
	{ "screen_tint",     l_pd_screen_tint },
	{ "upside_down",     l_pd_upside_down },
	{ "screen_roll",     l_pd_screen_roll },
	{ "fake_crash",      l_pd_fake_crash },
	{ "hud_off",         l_pd_hud_off },
	{ "ipod_ad",         l_pd_ipod_ad },
	{ "double_vision",   l_pd_double_vision },
	{ "t_pose",          l_pd_t_pose },
	{ "grayscale",       l_pd_grayscale },
	{ "aspect_scale",    l_pd_aspect_scale },
	{ "fov_scale",       l_pd_fov_scale },
	{ "pixelate",        l_pd_pixelate },
	{ "screen_fx",       l_pd_screen_fx },
	{ "hudvd",           l_pd_hudvd },
	{ "crt",             l_pd_crt },
	{ "pirate",          l_pd_pirate },
	{ "half_mirror",     l_pd_half_mirror },
	{ "vertex_wobble",   l_pd_vertex_wobble },
	{ "hall_of_mirrors", l_pd_hall_of_mirrors },
	{ "lens",            l_pd_lens },
	{ NULL, NULL },
};

void luaApiRegisterFx(lua_State *L)
{
	luaL_setfuncs(L, g_LuaApiFx, 0);
}

/* ------------------------------------------------------------------------- *
 * Core hooks (luaai_api_internal.h)
 * ------------------------------------------------------------------------- */

/* Free pd.load_image buffers (our RGBA5551 conversions — plain malloc, so
 * plain free). The Lua-side handles die with the state. The renderer caches
 * textures by buffer address, so drop each one from the cache first or a
 * later allocation at the same address would draw the old pixels. */
void luaApiResetFx(void)
{
	s32 i;

	for (i = 0; i < g_LuaImageCount; i++) {
		if (g_LuaImages[i].data) {
			videoFreeCachedTexture(g_LuaImages[i].data);
			free(g_LuaImages[i].data);
			g_LuaImages[i].data = NULL;
		}
	}
	g_LuaImageCount = 0;
}

// Draw a loaded image as a w*h quad centred at (cx,cy), rotated by `angle`
// radians. Uses textured triangles (rotation needs real geometry, unlike the
// axis-aligned texrect), a MODULATE combine (colour = TEXEL0 * PRIMITIVE) so
// the image shows its OWN colours — pass white for untinted, a colour to tint.
Gfx *luaApiDrawImage(Gfx *gdl, s32 handle, s32 cx, s32 cy, s32 w, s32 h, f32 angle, u32 color)
{
	struct luaimage *im;
	s32 iw, ih;

	if (handle < 0 || handle >= g_LuaImageCount || g_LuaImages[handle].data == NULL) {
		return gdl;
	}
	im = &g_LuaImages[handle];
	iw = (s32)im->w;
	ih = (s32)im->h;
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	// Load the RGBA5551 buffer with the standard gDPLoadTextureBlock macro
	// (the same path menugfx uses for the raw RGBA16 blur buffer — it sets up
	// the load/render tiles + sizes correctly). Combine: colour = TEXEL0 *
	// PRIMITIVE, alpha = TEXEL0 * PRIMITIVE, so white PRIM = the image
	// untouched and a coloured PRIM tints it.
	gDPPipeSync(gdl++);
	gSPTexture(gdl++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
	gDPLoadTextureBlock(gdl++, im->data, G_IM_FMT_RGBA, G_IM_SIZ_16b, iw, ih, 0,
			G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP,
			texGetMask(iw), texGetMask(ih), G_TX_NOLOD, G_TX_NOLOD);
	gDPSetCycleType(gdl++, G_CYC_1CYCLE);
	gDPSetAlphaCompare(gdl++, G_AC_NONE);
	gDPSetCombineLERP(gdl++,
			TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0,
			TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0);
	gDPSetPrimColorViaWord(gdl++, 0, 0, color);
	gSPClearGeometryMode(gdl++, G_CULL_BOTH);
	gDPSetTextureFilter(gdl++, G_TF_BILERP);
	gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);

	if (angle == 0.0f) {
		// Axis-aligned: a screen-space texrect (fast, no vertices).
		gSPTextureRectangle(gdl++,
				(cx - w / 2) * 4, (cy - h / 2) * 4, (cx + w / 2) * 4, (cy + h / 2) * 4,
				G_TX_RENDERTILE, 0, 0, (iw << 10) / w, (ih << 10) / h);
	} else {
		// Rotated: textured quad (texrects can't rotate). Two traps the
		// axis-aligned texrect path doesn't hit:
		// 1. The overlay bracket loads NO matrices, so tris go through
		//    whatever lvRender left behind — load an explicit pixel-space
		//    ortho projection + identity modelview (x10 for subpixel).
		// 2. text0f153628 sets G_TP_NONE, and triangle texcoords are HALVED
		//    when texture persp is off (texrects are exempt) — bake a 2x
		//    into the S10.5 coords (<<6 instead of <<5).
		Vtx *vertices = gfxAllocateVertices(4);
		Mtx *ortho = gfxAllocateMatrix();
		Mtx *ident = gfxAllocateMatrix();
		f32 co = cosf(angle), si = sinf(angle), hw = w * 0.5f, hh = h * 0.5f;
		s16 smax = (s16)(iw << 6), tmax = (s16)(ih << 6);
		const f32 dx[4] = { -1.f, 1.f, 1.f, -1.f };
		const f32 dy[4] = { -1.f, -1.f, 1.f, 1.f };
		s32 i;

		guOrtho(ortho, 0, viGetWidth() * 10.0f, viGetHeight() * 10.0f, 0, -10, 10, 1);
		guMtxIdent(ident);
		gSPMatrix(gdl++, osVirtualToPhysical(ortho), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
		gSPMatrix(gdl++, osVirtualToPhysical(ident), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

		for (i = 0; i < 4; i++) {
			f32 lx = dx[i] * hw, ly = dy[i] * hh;
			vertices[i].x = (s16)((cx + (lx * co - ly * si)) * 10);
			vertices[i].y = (s16)((cy + (lx * si + ly * co)) * 10);
			vertices[i].z = 0;
			vertices[i].s = (i == 1 || i == 2) ? smax : 0;
			vertices[i].t = (i >= 2) ? tmax : 0;
			vertices[i].colour = 0;
		}
		gSPVertex(gdl++, osVirtualToPhysical(vertices), 4, 0);
		gSPTri2(gdl++, 0, 1, 2, 2, 3, 0);
	}

	return gdl;
}

/* Kai passes overlay text through langChaosTransform so the text gags (uwu,
 * pig latin, buttsbot) cover the Lua overlays too. The transform lives with
 * the text gags; until it lands here the text is drawn as given. */
const char *luaApiOverlayText(const char *text)
{
	return langChaosTransform((char *)text);
}

#else /* PLATFORM_N64 */

void luaApiRegisterFx(lua_State *L)
{
	(void)L;
}

void luaApiResetFx(void)
{
}

Gfx *luaApiDrawImage(Gfx *gdl, s32 handle, s32 cx, s32 cy, s32 w, s32 h, f32 angle, u32 color)
{
	return gdl;
}

const char *luaApiOverlayText(const char *text)
{
	return text;
}

#endif /* PLATFORM_N64 */
