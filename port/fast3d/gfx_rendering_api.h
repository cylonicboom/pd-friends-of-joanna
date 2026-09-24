#ifndef GFX_RENDERING_API_H
#define GFX_RENDERING_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct ShaderProgram;

struct GfxClipParameters {
    bool z_is_from_0_to_1;
    bool invert_y;
};

enum FilteringMode { FILTER_NONE, FILTER_LINEAR, FILTER_THREE_POINT };
enum MipmapFilteringMode { MIPMAP_DISABLED, MIPMAP_NEAREST, MIPMAP_LINEAR };

// Skin match (SHADER_OPT_SKINMATCH): everything the fragment shader needs for
// one bracket of body draws. Filled by gfx_pc from port/src/skinmatch.c, in
// Oklab (body/head: dark xyz then light xyz) and linear RGB (gain/off, four
// garment layers). mask_texture_id is the companion R=skin G=layer texture.
struct SkinMatchUniforms {
    uint32_t mask_texture_id;
    uint32_t cms, cmt;
    float body[6];
    float head[6];
    float gain[12];
    float off[12];
    float params[2]; // strength, detail
};

struct GfxRenderingAPI {
    const char* (*get_name)(void);
    int (*get_max_texture_size)(void);
    struct GfxClipParameters (*get_clip_parameters)(void);
    void (*unload_shader)(struct ShaderProgram* old_prg);
    void (*load_shader)(struct ShaderProgram* new_prg);
    struct ShaderProgram* (*create_and_load_new_shader)(uint64_t shader_id0, uint32_t shader_id1);
    struct ShaderProgram* (*lookup_shader)(uint64_t shader_id0, uint32_t shader_id1);
    void (*shader_get_info)(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]);
    void (*clear_shaders)(void);
    uint32_t (*new_texture)(void);
    void (*select_texture)(int tile, uint32_t texture_id, bool linear_filter);
    // gen_mipmaps: build the mip chain for this upload (texture drawn with
    // G_TL_LOD); three-point filtering always builds one
    void (*upload_texture)(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps);
    // mipmaps: the bound texture has a mip chain and may be sampled through it
    void (*set_sampler_parameters)(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps);
    void (*set_depth_mode)(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode);
    void (*set_depth_range)(float znear, float zfar);
    void (*set_viewport)(int x, int y, int width, int height);
    void (*set_scissor)(int x, int y, int width, int height);
    void (*set_use_alpha)(bool use_alpha, bool modulate);
    void (*draw_triangles)(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris);
    void (*init)(void);
    void (*on_resize)(void);
    void (*start_frame)(void);
    void (*end_frame)(void);
    void (*finish_render)(void);
    int (*create_framebuffer)();
    void (*update_framebuffer_parameters)(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                          bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                          bool can_extract_depth);
    bool (*start_draw_to_framebuffer)(int fb_id, float noise_scale);
    void (*copy_framebuffer)(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back);
    void (*clear_framebuffer)(bool clear_color, bool clear_depth);
    void (*resolve_msaa_color_buffer)(int fb_id_target, int fb_id_source);
    void* (*get_framebuffer_texture_id)(int fb_id);
    void (*select_texture_fb)(int fb_id);
    void (*delete_texture)(uint32_t texID);
    void (*set_texture_filter)(enum FilteringMode mode);
    enum FilteringMode (*get_texture_filter)(void);
    // pd.* fx post filter (pixelate / colour modes / CRT / lens / rotate):
    // filter the finished frame in place. Runs from gfx_run's tail. Nullable.
    void (*retro_filter)(int pixw, int pixh, int cmode, int clevels, int fx, float warp);
    // Skin match: (re)upload a companion mask as an RGBA8 texture, returning
    // its id (pass 0 to create). Bound on the third unit, never on 0/1.
    uint32_t (*skinmask_upload)(uint32_t texture_id, const uint8_t* rgba32_buf, uint32_t width, uint32_t height);
    // Skin match: bind the mask on the third unit and load the uniforms into
    // the currently loaded shader program. Nullable.
    void (*set_skinmatch)(const struct SkinMatchUniforms* u);
    // mipmaps and anisotropic filtering (upstream c303c81f8)
    void (*set_mipmap_filter)(enum MipmapFilteringMode mode);
    void (*set_anisotropy_level)(int level);
    int (*get_max_anisotropy_level)(void);
};

#endif
