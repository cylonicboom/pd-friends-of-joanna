#ifndef GFX_API_H
#define GFX_API_H

#ifndef __cplusplus
#include <stdint.h>
#include <stdbool.h>
#endif

#include "gfx_rendering_api.h"
#include "gfx_window_manager_api.h"

struct XYWidthHeight {
    int16_t x, y;
    uint32_t width, height;
};

struct GfxDimensions {
    float internal_mul;
    uint32_t width, height;
    float aspect_ratio;
};

struct GfxInitSettings {
    struct GfxWindowManagerAPI *wapi;
    struct GfxRenderingAPI *rapi;
    struct GfxWindowInitSettings window_settings;
};

extern struct GfxDimensions gfx_current_window_dimensions; // The dimensions of the window
extern struct GfxDimensions
    gfx_current_dimensions; // The dimensions of the draw area the game draws to, before scaling (if applicable)
extern struct XYWidthHeight
    gfx_current_game_window_viewport; // The area of the window the game is drawn to, (0, 0) is top-left corner
extern uint32_t gfx_msaa_level;
extern struct XYWidthHeight gfx_current_native_viewport; // The internal/native video mode of the game
extern float gfx_current_native_aspect; // The aspect ratio of the above mode
extern bool gfx_framebuffers_enabled;
extern bool gfx_detail_textures_enabled;
extern bool gfx_external_textures_enabled;

// Renderer side of the pd.* fx effects (Lua API; from the Perfect Dark Kai
// fork, be46717, where they are documented in docs/PORT_CHAOS.md). Every knob
// defaults to "off"; the game sets them through the chraiLua* fx bridges.
extern bool gfx_wireframe_scope;             // scoped wireframe bracket (G_CHRWIREFRAME_EXT)
extern float gfx_hud_squish;                 // 2D rect squish toward the centre; 0 = off
extern int gfx_flattex_mode;                 // 1 = white textures (vertex shading only), 2 = per-texture average colour, 3 = gfx_flattex_image
extern unsigned char *gfx_flattex_image;     // mode 3 override image, RGBA8888 (owned by the game side)
extern int gfx_flattex_image_w;
extern int gfx_flattex_image_h;
extern int gfx_force_grayscale;              // force the grayscale shader path with a neutral colour
extern int gfx_shiny_mode;                   // 1 = fake-chrome screen-space UVs on all 3D geometry, 2 = + gold tint
extern float gfx_screen_roll;                // roll the 3D view about the screen centre, radians; 0 = off
extern float gfx_vtx_wobble_amp;             // eye-space vertex wobble, world units; 0 = off
extern float gfx_vtx_wobble_freq;
extern float gfx_vtx_wobble_phase;
extern float gfx_vtx_wobble_sag;
extern float gfx_vtx_wobble_desync;
extern float gfx_vtx_wobble_nearfade;
extern int gfx_hom_mode;                     // skip the per-frame colour clear (trails)
extern int gfx_screen_tint;                  // 0x00RRGGBB luminance tint via the grayscale path; 0 = off
extern int gfx_retro_pixel_w;                // pixelation grid width (pd.pixelate); 0 = off
extern int gfx_retro_pixel_h;                // pixelation grid height
extern int gfx_retro_colors;                 // 0 keep, 2..64 grey, >= 256 RGB332, 1000 invert, 1001 gameboy, 1002 thermal, 1003 virtualboy reds, 1004 hue-rotate, 1005 hue field
extern int gfx_retro_fx;                     // 1 scanlines, 2 grille, 4 CRT curve, 8 vignette, 16 VHS, 32 wobble, 0x800/0x1000/0x8000/0x10000 blackouts, 0x2000/0x4000 half mirror
extern float gfx_retro_warp;                 // fisheye lens strength (pd.lens); 0 = off
extern unsigned char gfx_rotate180_mode;     // rotate the whole finished frame 180 (pd.upside_down)
extern unsigned char gfx_doublevision_mode;  // blend rotated ghosts over the frame (pd.double_vision)
extern int gfx_internal_res_chaos;           // true internal render height override; 0 = native
extern int gfx_silhouette;                   // "iPod Ad": flat-fill 3D geometry
extern float gfx_silhouette_wall_color[3];   // bright default fill (walls/sky), 0..1 RGB
extern float gfx_silhouette_color[3];        // current fill scope colour (G_FLATFILL_EXT)
extern int gfx_silhouette_edges;             // 1 = draw white wireframe edges (walls only)
extern float gfx_wireframe_line_width;       // wire thickness in pixels

void gfx_set_hud_squish(float frac);
void gfx_hudvd_reset(void);
void gfx_hudvd_set_active(int on);

void gfx_init(const struct GfxInitSettings *settings);
void gfx_destroy(void);
struct GfxRenderingAPI* gfx_get_current_rendering_api(void);
void gfx_start_frame(void);
void gfx_run(Gfx* commands);
void gfx_end_frame(void);
void gfx_set_target_fps(int);
void gfx_set_texture_filter(enum FilteringMode mode);
void gfx_set_mipmap_filter(enum MipmapFilteringMode mode);
void gfx_texture_cache_clear(void);
void gfx_texture_cache_delete(const uint8_t *orig_addr);
int gfx_create_framebuffer(uint32_t width, uint32_t height, int upscale, int autoresize);
void gfx_resize_framebuffer(int fb, uint32_t width, uint32_t height, int upscale, int autoresize);
void gfx_set_framebuffer(int fb, float noise_scale) ;
void gfx_reset_framebuffer(void);
void gfx_copy_framebuffer(int fb_dst, int fb_src, int left, int top, int use_back);

#endif
