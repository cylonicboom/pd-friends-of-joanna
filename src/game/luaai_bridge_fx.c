/**
 * chraiLua* bridges for the fx group of the pd.* API: screen fades and
 * shakes, the fast3d renderer effects, the HUD toggles and the few game-side
 * render overrides (T-pose, paintball, Terminator Vision, iPod Ad).
 *
 * From the Perfect Dark Kai fork (be46717), where the bridges sat in
 * src/game/chraction.c. The comments are Kai's. Kai's net client tests are
 * gone: this build has no netplay.
 *
 * Also here: the two engine hooks the fx group needs outside a bridge,
 * luaFxResetPerStage (lvReset) and luaFxFakeCrashRelease (lvTick).
 */

#include <ultra64.h>
#include "audio.h"
#include "constants.h"
#include "types.h"
#include "bss.h"
#include "data.h"
#include "video.h"
#include "game/chaosstate.h"
#include "game/player.h"
#include "luaai_api_internal.h"

#ifndef PLATFORM_N64

// fast3d renderer knobs (port/fast3d/gfx_pc.cpp). C++ int == s32, float ==
// f32; the two 1-byte bools are unsigned char on both sides.
extern s32 gfx_flattex_mode;
extern s32 gfx_force_grayscale;
extern s32 gfx_shiny_mode;
extern f32 gfx_screen_roll;
extern f32 gfx_vtx_wobble_amp;
extern f32 gfx_vtx_wobble_freq;
extern f32 gfx_vtx_wobble_phase;
extern f32 gfx_vtx_wobble_sag;
extern f32 gfx_vtx_wobble_desync;
extern f32 gfx_vtx_wobble_nearfade;
extern s32 gfx_hom_mode;
extern s32 gfx_screen_tint;
extern s32 gfx_retro_pixel_w;
extern s32 gfx_retro_pixel_h;
extern s32 gfx_retro_colors;
extern s32 gfx_retro_fx;
extern f32 gfx_retro_warp;
extern u8 gfx_rotate180_mode;
extern u8 gfx_doublevision_mode;
extern s32 gfx_internal_res_chaos;
extern void gfx_set_hud_squish(f32 frac);

// pd.fade(r,g,b,a,time60): start a screen fade on the local player's viewport
// (the cutscene fade machinery — playerSetFadeColour + a full-fraction fade
// over time60 ticks). Chaos uses it for blink/flashbang-style effects.
s32 chraiLuaScreenFade(s32 r, s32 g, s32 b, s32 a, f32 time60)
{
	f32 frac;

	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	// Flash instantly to (r,g,b) at intensity `a` (0-255), then fade back to the
	// game over time60 frames — one clean flash. The colour fraction is 0..1, so
	// `a` must be normalised: passing it raw (e.g. 200) set frac far over 1 (an
	// overflowing full-white strobe) and the animation then targeted 1, i.e. it
	// faded TO white and stuck there ("flashes white and never comes back").
	if (a < 0) {
		a = 0;
	} else if (a > 255) {
		a = 255;
	}
	frac = a * (1.0f / 255.0f);

	playerSetFadeColour(r, g, b, frac); // instant flash to the colour + HOLD

	// time60 > 0: fade back out to the game over time60 frames.
	// time60 <= 0: leave it held at the colour — the caller drives the fade-out
	// later with a second pd.fade (e.g. Snap/Blink hold white 1s then fade 2s).
	if (time60 > 0) {
		playerSetFadeFrac(time60, 0);
	}
	return 1;
}

// pd.fps_cap(fps): OG mode — hard render-rate override (video.c
// vidFpsOverride). The sim's variable tick (lvupdate) absorbs the low rate
// exactly like the N64 did, so no interpolation work is needed. 0 restores
// the user's own limit.
s32 chraiLuaFpsCap(s32 fps)
{
	videoSetFpsOverride(fps);
	return 1;
}

// pd.internal_res(height): OG mode — TRUE internal render resolution: the
// frame rasterizes into an offscreen target this many lines tall and is
// NEAREST-upscaled to the window (fast3d gfx_internal_res_chaos). Never
// persists. 0 restores.
s32 chraiLuaInternalRes(s32 height)
{
	gfx_internal_res_chaos = (height > 0) ? height : 0;
	return 1;
}

// pd.hud_off(on): "No HUD" — skip every HUD element render (the seven
// hudvd-wrapped sites in player.c/lv.c). Chaos overlays still draw.
s32 chraiLuaHudOff(s32 on)
{
	g_ChaosHudOff = on ? 1 : 0;
	return 1;
}

// pd.ipod_ad(on [, r, g, b]): "iPod Ad" silhouette — walls the given bright
// colour (default vivid green), chrs black, objects/weapons white, white
// wireframe edges (the fills are per-prop G_FLATFILL brackets in propRender +
// player.c; the enable + wall colour sync in bgTickPortals).
s32 chraiLuaIpodAd(s32 on, s32 r, s32 g, s32 b)
{
	g_ChaosIpodAd = on ? 1 : 0;
	if (on) {
		g_ChaosIpodWall[0] = (u8)(r < 0 ? 0 : r > 255 ? 255 : r);
		g_ChaosIpodWall[1] = (u8)(g < 0 ? 0 : g > 255 ? 255 : g);
		g_ChaosIpodWall[2] = (u8)(b < 0 ? 0 : b > 255 ? 255 : b);
	}
	return 1;
}

// pd.chr_wireframe(on): hostile chrs render as polygon outlines (prop.c
// G_CHRWIREFRAME_EXT bracket around chrRender).
s32 chraiLuaChrWireframe(s32 on)
{
	g_ChaosWireframeChrs = on ? 1 : 0;
	return 1;
}

// pd.fov_scale(mult): stretch the vertical FOV — >1 fisheye, <1 tunnel
// vision. Same self-restoring setter-hook pattern as pd.aspect_scale.
s32 chraiLuaFovScale(f32 mult)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (mult <= 0.0f) mult = 1.0f;
	if (mult < 0.4f) mult = 0.4f;
	if (mult > 2.2f) mult = 2.2f;
	g_ChaosFovMult = mult;
	return 1;
}

// pd.aspect_scale(mult): stretch the projection aspect — 2.0 = extra wide
// (2:1-style CinemaScope), 0.5 = extra tall. 1.0 (or no arg) restores;
// playerTick re-derives the natural aspect every tick so restore is instant.
s32 chraiLuaAspectScale(f32 mult)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (mult <= 0.0f) mult = 1.0f;
	if (mult < 0.25f) mult = 0.25f;
	if (mult > 4.0f) mult = 4.0f;
	g_ChaosAspectMult = mult;
	return 1;
}

// pd.flattex(mode): 0 = normal textures, 1 = all-white (pure vertex shading),
// 2 = every texture flooded with its own average colour. Alpha is preserved
// so HUD text stays readable. Applied renderer-side at the next frame
// boundary via a texture-cache reimport; purely cosmetic, save-safe.
s32 chraiLuaFlatTex(s32 mode)
{
	if (mode < 0) mode = 0;
	if (mode > 2) mode = 2;
	gfx_flattex_mode = mode;
	return 1;
}

// pd.texlabels(n): the overlay tags the n textures nearest the screen centre
// with their texnum (and model file num); 0 = off. Returns the previous n.
// Cosmetic, save-safe; the renderer captures only while n > 0 or a
// pd.tex_visible call asked for the next frames.
s32 chraiLuaTexLabels(s32 n)
{
	s32 prev = videoGetTexLabelsMax();

	if (n < 0) n = 0;
	if (n > 64) n = 64;
	videoSetTexLabelsMax(n);
	return prev;
}

// pd.grayscale(on): force the renderer's grayscale shader path (film noir).
s32 chraiLuaGrayscale(s32 on)
{
	gfx_force_grayscale = on ? 1 : 0;
	return 1;
}

// pd.shiny(mode): 0 = off, 1 = every 3D surface gets fake-chrome screen-space
// UVs ("everything is reflective"), 2 = the same plus a gold tint via the
// grayscale shader (Midas mode). HUD texrects are exempt renderer-side.
// Cosmetic only, save-safe.
s32 chraiLuaShiny(s32 mode)
{
	if (mode < 0) mode = 0;
	if (mode > 2) mode = 2;
	gfx_shiny_mode = mode;
	return 1;
}

// pd.terminator(on): "Terminator Vision" render/aim overrides — drop the IR
// goggle cutout so the infrared filter fills the view, and force the CMP150
// threat detector on for any weapon (both consumed in lv.c / player.c).
s32 chraiLuaTerminator(s32 on)
{
	g_ChaosTerminator = on ? 1 : 0;
	return 1;
}

// pd.paintball(on): force paintball visuals for everyone (wallhit.c).
s32 chraiLuaPaintball(s32 on)
{
	g_ChaosPaintball = on ? 1 : 0;
	return 1;
}

// pd.shake(ticks): kick the explosion screen-shake for N ticks (~60/s).
s32 chraiLuaShake(s32 ticks)
{
	if (ticks < 1) ticks = 1;
	if (ticks > 120) ticks = 120;
	g_ExplosionShakeTotalTimer = ticks;
	g_ExplosionShakeIntensityTimer = ticks;
	return 1;
}

// pd.screen_tint(r,g,b) / pd.screen_tint(): full-screen luminance tint via
// the grayscale shader path (the Midas gold mechanism with a custom colour).
s32 chraiLuaScreenTint(s32 r, s32 g, s32 b, s32 on)
{
	if (!on) {
		gfx_screen_tint = 0;
		return 1;
	}
	r = r < 0 ? 0 : r > 255 ? 255 : r;
	g = g < 0 ? 0 : g > 255 ? 255 : g;
	b = b < 0 ? 0 : b > 255 ? 255 : b;
	gfx_screen_tint = (r << 16) | (g << 8) | b;
	if (gfx_screen_tint == 0) {
		gfx_screen_tint = 1; // black tint, still distinct from "off"
	}
	return 1;
}

// pd.upside_down(on): "Australia mode" — rotate the whole finished frame 180
// via the retro post filter (renderer 1-byte bool), and reverse the controls
// game-side (g_ChaosControlReverse, bondmove.c). The post rotation flips
// world + HUD together and, with the controls reversed, aim tracks the
// rotated view.
s32 chraiLuaUpsideDown(s32 on)
{
	gfx_rotate180_mode = on ? 1 : 0;
	g_ChaosControlReverse = on ? 1 : 0;
	return 1;
}

// pd.screen_roll(deg): "Speen" — rotate the 3D view about the screen centre
// by an absolute angle in DEGREES (0 = off/upright). The renderer applies an
// aspect-corrected clip-space rotation in gfx_sp_vertex (gfx_screen_roll,
// C++ float == f32); HUD texrects stay upright. The caller animates by
// re-setting the angle each tick.
s32 chraiLuaScreenRoll(f32 deg)
{
	gfx_screen_roll = deg * (3.14159265f / 180.0f);
	return 1;
}

// pd.vertex_wobble(amp, freq, phase, sag, desync, nearfade): "Jelly" —
// deform every vertex in eye space. amp in world units (0 = off), freq in
// radians per world unit, phase the animation angle, advanced by the caller
// each tick. Consumed in gfx_sp_vertex.
s32 chraiLuaVertexWobble(f32 amp, f32 freq, f32 phase, f32 sag, f32 desync, f32 nearfade)
{
	if (amp < 0.0f) {
		amp = 0.0f;
	}
	if (amp > 200.0f) {
		amp = 200.0f; // sanity clamp so a stray value can't fold the scene
	}
	if (sag < 0.0f) {
		sag = 0.0f;
	}
	if (sag > 200.0f) {
		sag = 200.0f;
	}
	if (desync < 0.0f) {
		desync = 0.0f;
	}
	if (desync > 4.0f) {
		desync = 4.0f; // beyond this the phase spread just looks like noise
	}
	gfx_vtx_wobble_amp = amp;
	gfx_vtx_wobble_freq = freq;
	gfx_vtx_wobble_phase = phase;
	gfx_vtx_wobble_sag = sag;
	if (nearfade < 0.0f) {
		nearfade = 0.0f; // 0 = off: uniform wobble at every distance
	}
	if (nearfade > 4000.0f) {
		nearfade = 4000.0f; // past this nothing in a normal room wobbles at all
	}
	gfx_vtx_wobble_desync = desync;
	gfx_vtx_wobble_nearfade = nearfade;
	return 1;
}

// pd.hall_of_mirrors(on): skip the framebuffer colour clear so the frame smears
// (Doom HOM / acid-trip trails). Consumed in gfx_pc.cpp; cleared in lvReset.
s32 chraiLuaHallOfMirrors(s32 on)
{
	gfx_hom_mode = on ? 1 : 0;
	return 1;
}

// pd.double_vision(on): "One too many" — blend rotated ghosts of the finished
// frame over the normal frame (retro post filter; renderer 1-byte bool). The
// world stays put and the ghosts are overlaid.
s32 chraiLuaDoubleVision(s32 on)
{
	gfx_doublevision_mode = on ? 1 : 0;
	return 1;
}

// pd.pixelate(w, h, colors): pixelate the finished frame down to a w x h
// grid via the retro post filter (gfx_retro.cpp), optionally applying a
// colour mode (2..64 = N-level greyscale, 256 = RGB 3-3-2, 1000 = invert,
// 1001 = Game Boy greens, 1002 = thermal palette). w = 0 with a colour set =
// colour mode at full resolution; everything <= 0 = off.
s32 chraiLuaPixelate(s32 w, s32 h, s32 colors)
{
	if (w > 0 && h > 0) {
		gfx_retro_pixel_w = w < 8 ? 8 : w > 1024 ? 1024 : w;
		gfx_retro_pixel_h = h < 8 ? 8 : h > 1024 ? 1024 : h;
	} else {
		gfx_retro_pixel_w = 0;
		gfx_retro_pixel_h = 0;
	}
	gfx_retro_colors = colors < 0 ? 0 : colors;
	return 1;
}

// pd.screen_fx(bits, on): set/clear retro post-filter effect bits (1 =
// scanlines, 2 = RGB grille, 4 = CRT curvature, 8 = vignette, 16 = VHS,
// 32 = underwater wobble, 1024 = side-by-side eyes). Bits compose, so
// simultaneous effects stack.
s32 chraiLuaScreenFx(s32 bits, s32 on)
{
	bits &= 0x43f;
	if (on) {
		gfx_retro_fx |= bits;
	} else {
		gfx_retro_fx &= ~bits;
	}
	return 1;
}

// pd.pirate(side): "Pirate" eyepatch — black out one half of the finished frame
// (post-process retro-fx bits 0x800 = left, 0x1000 = right), so the HUD in that
// half goes dark too. side 1 = left, 2 = right, 3 = the centre 9:16 band,
// 4 = the sides around it, anything else = off. Only one side is ever set.
s32 chraiLuaPirate(s32 side)
{
	gfx_retro_fx &= ~(0x800 | 0x1000 | 0x8000 | 0x10000); // clear all pirate bits first
	if (side == 1) {
		gfx_retro_fx |= 0x800;  // black the LEFT half
	} else if (side == 2) {
		gfx_retro_fx |= 0x1000; // black the RIGHT half
	} else if (side == 3) {
		// black the CENTER 9:16 band (Anti Brainrot) — same post-process as
		// the eyepatch halves, so the HUD inside the band goes dark too
		gfx_retro_fx |= 0x8000;
	} else if (side == 4) {
		// black the SIDES, leaving the centre band (Vertical Form Content)
		gfx_retro_fx |= 0x10000;
	}
	return 1;
}

// pd.hud_squish(frac): scale every 2D HUD/text rect toward the horizontal
// centre so it fits a portrait band (Vertical Form Content pairs it with
// pirate mode 4). 0/absent = off; 0.425 matches the mode-4 pillars.
s32 chraiLuaHudSquish(f32 frac)
{
	gfx_set_hud_squish(frac);
	return 1;
}

// pd.half_mirror(side): mirror one half of the finished frame onto the other
// about the vertical centre line (post-process retro-fx bits 0x2000 = left
// half onto the right, 0x4000 = right half onto the left), HUD included —
// kaleidoscope style. side 1 = left source, 2 = right source, anything else =
// off. Only one side is ever set at a time (both bits together would swap the
// halves instead).
s32 chraiLuaHalfMirror(s32 side)
{
	gfx_retro_fx &= ~(0x2000 | 0x4000); // clear both mirror bits first
	if (side == 1) {
		gfx_retro_fx |= 0x2000; // LEFT half mirrored onto the right
	} else if (side == 2) {
		gfx_retro_fx |= 0x4000; // RIGHT half mirrored onto the left
	}
	return 1;
}

// pd.lens(k): fisheye lens warp on the rendered frame — centre magnified,
// corners pinned. 0 = off; negative = pincushion (clamped shy of the pole).
s32 chraiLuaLens(f32 k)
{
	gfx_retro_warp = k < -0.8f ? -0.8f : k > 4.0f ? 4.0f : k;
	return 1;
}

// pd.fake_crash(secs): freeze the sim for secs of REAL time so the game looks
// hung.
//
// It releases ITSELF from lv.c's real-time countdown. It cannot be released by
// the caller: effect timers advance on sim ticks, which is exactly what this
// stops, so a Lua-side stop() would never run and the freeze would be
// permanent. Hence secs is clamped to something survivable even if a caller
// passes nonsense.
//
// Like Kai, the audio output is held on its last buffer (audioSetHold in
// port/src/audio.c) until the release.
s32 chraiLuaFakeCrash(f32 secs)
{
	if (apLuaPlayerChr() == NULL) {
		return 0;
	}
	if (secs < 0.1f) secs = 0.1f;
	if (secs > 10.0f) secs = 10.0f;

	g_ChaosFakeCrash240 = (s32)(secs * 240.0f);
	audioSetHold(1);
	return 1;
}

// pd.t_pose(on): every skeletal model renders in its bind pose (anim.c joint
// rotations read as zero). Root motion still applies — T-posers glide.
s32 chraiLuaTPose(s32 on)
{
	g_ChaosTPose = on ? 1 : 0;
	return 1;
}

// lvTick: the pd.fake_crash countdown ran out. Kai drops the audio hold here
// (see chraiLuaFakeCrash); nothing else needs releasing.
void luaFxFakeCrashRelease(void)
{
	audioSetHold(0);
}

// lvReset: the fx state that chaosStateResetPerStage can't reach — the
// renderer knobs, the fps cap, the tex_override image and hudvd. Kai clears
// most of these in lvReset / lvResetChaosPerStage; the Lua state is torn down
// on a stage change without running any effect's stop(), so none may carry
// over. (Kai leaves the flat-texture, grayscale, shiny, tint, retro-filter
// and hudvd knobs to the scripts; here they are cleared too.)
void luaFxResetPerStage(void)
{
	extern void hudvdSetActive(bool on);

	gfx_rotate180_mode = 0;
	gfx_doublevision_mode = 0;
	gfx_set_hud_squish(0.0f);
	gfx_screen_roll = 0.0f;
	gfx_vtx_wobble_amp = 0.0f;
	gfx_vtx_wobble_sag = 0.0f;
	gfx_vtx_wobble_desync = 0.0f;
	gfx_hom_mode = 0;
	luaTexOverrideReset();
	videoSetFpsOverride(0);
	gfx_internal_res_chaos = 0;

	gfx_flattex_mode = 0;
	gfx_force_grayscale = 0;
	videoSetTexLabelsMax(0);
	gfx_shiny_mode = 0;
	gfx_screen_tint = 0;
	gfx_retro_pixel_w = 0;
	gfx_retro_pixel_h = 0;
	gfx_retro_colors = 0;
	gfx_retro_fx = 0;
	gfx_retro_warp = 0.0f;
	hudvdSetActive(false);
	audioSetHold(0);
}

#endif /* PLATFORM_N64 */
