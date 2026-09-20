#ifndef _IN_GBIEX_H
#define _IN_GBIEX_H

/**
 * 07 gSPColor - copy colors from segment + offset into DMEM
 *
 * upper word
 * 00FF0000	- number of bytes to copy minus 4 (eg. 4 colours = 0x0c)
 * 0000FFFF	- nubmer of bytes to copy
 *
 * lower word
 * 0F000000	- segment
 * 00FFFFFF	- offset in color table
 */
#define gSPColor(pkt, v, n)                           \
    gDma1p(pkt, G_COL, v, sizeof(Col)*(n),((n)-1)<<2)

#define gsSPColor(v, n, v0)                        \
    gsDma1p(G_COL, v, sizeof(Col)*(n), ((n)-1)<<2)

/**
 * B1	rsp_tri4
 * Draws up to four triangles at a time.
 * Expects values from 0-F, corresponding with # points declared by vertex command.
 * Triangles with all points set to 0 are not drawn.
 *
 * upper word
 * 0000F000	z4
 * 00000F00	z3
 * 000000F0	z2
 * 0000000F	z1
 *
 * lower word
 * F0000000	y4
 * 0F000000	x4
 * 00F00000	y3
 * 000F0000	x3
 * 0000F000	y2
 * 00000F00	x2
 * 000000F0	y1
 * 0000000F	x1
 */
#define	gSPTri4(pkt, x1, y1, z1, x2, y2, z2, x3, y3, z3, x4, y4, z4) \
{                                                                    \
    Gfx *_g = (Gfx *)(pkt);                                          \
    _g->words.w0 = (_SHIFTL(G_TRI4, 24, 8)                           \
            | _SHIFTL(z4, 12, 4)                                     \
            | _SHIFTL(z3, 8, 4)                                      \
            | _SHIFTL(z2, 4, 4)                                      \
            | _SHIFTL(z1, 0, 4));                                    \
    _g->words.w1 = (_SHIFTL(y4, 28, 4)                               \
            | _SHIFTL(x4, 24, 4)                                     \
            | _SHIFTL(y3, 20, 4)                                     \
            | _SHIFTL(x3, 16, 4)                                     \
            | _SHIFTL(y2, 12, 4)                                     \
            | _SHIFTL(x2, 8, 4)                                      \
            | _SHIFTL(y1, 4, 4)                                      \
            | _SHIFTL(x1, 0, 4));                                    \
}

#define gSPTri3(pkt, x1, y1, z1, x2, y2, z2, x3, y3, z3)      \
    gSPTri4(pkt, x1, y1, z1, x2, y2, z2, x3, y3, z3, 0, 0, 0)

#define gSPTri2(pkt, x1, y1, z1, x2, y2, z2)               \
    gSPTri4(pkt, x1, y1, z1, x2, y2, z2, 0, 0, 0, 0, 0, 0)

#define gSPTri1(pkt, x1, y1, z1)                        \
    gSPTri4(pkt, x1, y1, z1, 0, 0, 0, 0, 0, 0, 0, 0, 0)

#define	gDPLoadTLUT06(pkt, a, b, c, d)				                                        \
{                                                                                           \
	Gfx *_g = (Gfx *)pkt;                                                                   \
	_g->words.w0 = _SHIFTL(G_LOADTLUT, 24, 8) | _SHIFTL((a), 14, 10) | _SHIFTL((b), 2, 10); \
	_g->words.w1 = _SHIFTL(0x06, 24, 8) | _SHIFTL((c), 14, 10) | _SHIFTL((d), 2, 10);       \
}

#define	gDPLoadTLUT07(pkt, a, b)				             \
{                                                            \
	Gfx *_g = (Gfx *)pkt;                                    \
	_g->words.w0 = _SHIFTL(G_LOADTLUT2, 24, 8);              \
	_g->words.w1 = _SHIFTL((a), 16, 16) | _SHIFTL((b), 0, 16); \
}

/**
 * Like gDPSetPrimColor, but is useful when the input colour is already in
 * RGBA format. It avoids unnecessary bitshifting and masking.
 */
#define	gDPSetPrimColorViaWord(pkt, m, l, rgba)     \
{                                                   \
	Gfx *_g = (Gfx *)(pkt);                         \
	_g->words.w0 =	(_SHIFTL(G_SETPRIMCOLOR, 24, 8) \
			| _SHIFTL(m, 8, 8)                      \
			| _SHIFTL(l, 0, 8));                    \
	_g->words.w1 =  (rgba);                         \
}

#define	gDPSetEnvColorViaWord(pkt, rgba) gDPSetColor(pkt, G_SETENVCOLOR, rgba)
#define	gDPSetFogColorViaWord(pkt, rgba) gDPSetColor(pkt, G_SETFOGCOLOR, rgba)

/**
 * gDPFillRectangleScaled - a wrapper around gDPFillRectangle which applies
 * g_ScaleX to the X coordinates.
 *
 * g_ScaleX is normally 1, but 2 when using hi-res.
 */
#define gDPFillRectangleScaled(pkt, x1, y1, x2, y2) gDPFillRectangle(pkt, (x1) * g_ScaleX, y1, (x2) * g_ScaleX, y2)

#define gDPHudRectangle(pkt, x1, y1, x2, y2) gDPFillRectangle(pkt, (x1) * g_ScaleX, y1, ((x2 + 1)) * g_ScaleX, (y2) + 1)

/**
 * Custom combiner modes.
 *
 * Modes 6-10 are copies of the following but they replace
 * SHADE with ENVIRONMENT in the alpha half:
 *
 * 06: G_CC_MODULATEIA2
 * 07: G_CC_MODULATEIA
 * 08: G_CC_MODULATEI2
 * 09: G_CC_MODULATEI
 * 10: G_CC_SHADE
 *
 * Modes 12-16 are copies of the following but they replace
 * SHADE with SCALE in the colour half:
 *
 * 12: G_CC_MODULATEI
 * 13: G_CC_MODULATEIA
 * 14: G_CC_SHADE
 * 15: G_CC_MODULATEI2
 * 16: G_CC_MODULATEIA2
 *
 * Summary of modes:
 * 00:  T0*EV          T0*EV
 * 01:  CM             (1-CM)*PR+CM
 * 02:  PR             T0*PR
 * 03:  CM*EV          CM*EV
 * 04:  SH             T0*SH
 * 05:  T1*SC          T1*PL
 * 06:  CM*SH          CM*EV
 * 07:  T0*SH          T0*EV
 * 08:  CM*SH          EV
 * 09:  T0*SH          EV
 * 10:  SH             EV
 * 11:  (T1-T0)*LF+T0  T1
 * 12:  T0*SC          SH
 * 13:  T0*SC          T0*SH
 * 14:  SC             SH
 * 15:  CM*SC          SH
 * 16:  CM*SC          CM*SH
 * 17:  (T0-EV)*SA+EV  (T0-EV)*SH+EV
 * 18:  CM*SH          CM
 * 19:  (T0-EV)*SA+EV  T0*EV
 * 20:  CM*SH          CM*SH+PR
 * 21:  (T1-T0)*LF+T0  SH+EV
 * 22:  (T1-T0)*LF+T0  (SH-EV)*T0
 * 23:  CM*SH          PR+CM
 * 24:  (T1-T0)*LF+T0  (1-SH)*EV
 * 25:  (T1-T0)*LF+T0  EV
 * 26:  (T1-T0)*LF+T0  T0*EV
 * 27:  (PR-EV)*T0+EV  (PR-EV)*T0+EV
 */
#define G_CC_CUSTOM_00  TEXEL0,    0,           ENVIRONMENT,  0,           TEXEL0,    0,           ENVIRONMENT,   0
#define G_CC_CUSTOM_01  0,         0,           0,            COMBINED,    1,         COMBINED,    PRIMITIVE,     COMBINED
#define G_CC_CUSTOM_02  0,         0,           0,            PRIMITIVE,   TEXEL0,    0,           PRIMITIVE,     0
#define G_CC_CUSTOM_03  COMBINED,  0,           ENVIRONMENT,  0,           COMBINED,  0,           ENVIRONMENT,   0
#define G_CC_CUSTOM_04  0,         0,           0,            SHADE,       TEXEL0,    0,           SHADE,         0
#define G_CC_CUSTOM_05  TEXEL1,    0,           SCALE,        0,           TEXEL1,    0,           PRIM_LOD_FRAC, 0
#define G_CC_CUSTOM_06  COMBINED,  0,           SHADE,        0,           COMBINED,  0,           ENVIRONMENT,   0
#define G_CC_CUSTOM_07  TEXEL0,    0,           SHADE,        0,           TEXEL0,    0,           ENVIRONMENT,   0
#define G_CC_CUSTOM_08  COMBINED,  0,           SHADE,        0,           0,         0,           0,             ENVIRONMENT
#define G_CC_CUSTOM_09  TEXEL0,    0,           SHADE,        0,           0,         0,           0,             ENVIRONMENT
#define G_CC_CUSTOM_10  0,         0,           0,            SHADE,       0,         0,           0,             ENVIRONMENT
#define G_CC_CUSTOM_11  TEXEL1,    TEXEL0,      LOD_FRACTION, TEXEL0,      1,         0,           TEXEL1,        0
#define G_CC_CUSTOM_12  TEXEL0,    0,           SCALE,        0,           0,         0,           0,             SHADE
#define G_CC_CUSTOM_13  TEXEL0,    0,           SCALE,        0,           TEXEL0,    0,           SHADE,         0
#define G_CC_CUSTOM_14  1,         0,           SCALE,        0,           0,         0,           0,             SHADE
#define G_CC_CUSTOM_15  COMBINED,  0,           SCALE,        0,           0,         0,           0,             SHADE
#define G_CC_CUSTOM_16  COMBINED,  0,           SCALE,        0,           COMBINED,  0,           SHADE,         0
#define G_CC_CUSTOM_17  TEXEL0,    ENVIRONMENT, SHADE_ALPHA,  ENVIRONMENT, TEXEL0,    ENVIRONMENT, SHADE,         ENVIRONMENT
#define G_CC_CUSTOM_18  COMBINED,  0,           SHADE,        0,           0,         0,           0,             COMBINED
#define G_CC_CUSTOM_19  TEXEL0,    ENVIRONMENT, SHADE_ALPHA,  ENVIRONMENT, TEXEL0,    0,           ENVIRONMENT,   0
#define G_CC_CUSTOM_20  COMBINED,  0,           SHADE,        0,           COMBINED,  0,           SHADE,         PRIMITIVE
#define G_CC_CUSTOM_21  TEXEL1,    TEXEL0,      LOD_FRACTION, TEXEL0,      1,         0,           SHADE,         ENVIRONMENT
#define G_CC_CUSTOM_22  TEXEL1,    TEXEL0,      LOD_FRACTION, TEXEL0,      SHADE,     ENVIRONMENT, TEXEL0,        0
#define G_CC_CUSTOM_23  COMBINED,  0,           SHADE,        0,           1,         0,           PRIMITIVE,     COMBINED
#define G_CC_CUSTOM_24  TEXEL1,    TEXEL0,      LOD_FRACTION, TEXEL0,      1,         SHADE,       ENVIRONMENT,   0
#define G_CC_CUSTOM_25  TEXEL1,    TEXEL0,      LOD_FRACTION, TEXEL0,      1,         0,           ENVIRONMENT,   0
#define G_CC_CUSTOM_26  TEXEL1,    TEXEL0,      LOD_FRACTION, TEXEL0,      TEXEL0,    0,           ENVIRONMENT,   0
#define G_CC_CUSTOM_27  PRIMITIVE, ENVIRONMENT, TEXEL0,       ENVIRONMENT, PRIMITIVE, ENVIRONMENT, TEXEL0,        ENVIRONMENT

#ifndef PLATFORM_N64

/* Extended commands */

#define G_SETFB_EXT                  0x21
#define G_SETTIMG_FB_EXT             0x23
#define G_INVALTEXCACHE_EXT          0x34
#define G_TEXRECT_WIDE_EXT           0x37
#define G_FILLRECT_WIDE_EXT          0x38
#define G_SETGRAYSCALE_EXT           0x39
#define G_EXTRAGEOMETRYMODE_EXT      0x3a
#define G_SETINTENSITY_EXT           0x40
#define G_COPYFB_EXT                 0x41
#define G_IMAGERECT_EXT              0x42
#define G_RDPFLUSH_EXT               0x43
#define G_CLEAR_DEPTH_EXT            0x44
#define G_SETSUBPIXELOFFSET_EXT      0x45
#define G_LOADTLUT2                  0x46
#define G_SETTEXINFO_EXT             0x47

// pd.* fx effects (Lua API; from the Perfect Dark Kai fork, be46717, where
// they are 0x4c-0x4e and documented in docs/PORT_CHAOS.md). Honoured by the
// fast3d renderer; the game emits them only while the matching effect is on.
//
// Scoped wireframe bracket ("wireframe enemies"): draws issued while the scope
// is set render as polygon outlines. w1 = 1 begin / 0 end. Emitted around
// hostile chr models in prop.c's render dispatch; the renderer flushes on
// toggle, and force-clears the scope each gfx_start_frame so a lost END can't
// leak.
#define G_CHRWIREFRAME_EXT           0x48
// HUDVD: every subsequent 2D rect is translated by the renderer-owned offset
// of the slot in w0's low 16 bits until a slot of -1 ends the bracket.
// Emitted around each HUD element so they bounce DVD-style in independent
// directions; force-cleared each gfx_start_frame.
#define G_HUDOFFSET_EXT              0x49
// "iPod Ad": while the silhouette mode is active, set the flat fill colour
// used for subsequent depth-tested 3D geometry (w0 low bit = 1, colour in w1),
// or reset to the standing wall default (bit 0). Emitted around each prop
// class in propRender and around the viewmodel.
#define G_FLATFILL_EXT               0x4a
// Skin match: bracket a chr's body draws with the head whose skin the body
// texture's masked texels should take. w0 low 16 = head model file number
// (untagged), w1 = 1 begin / 0 end. Emitted by chrRender around modelRender
// only when the setting is on and both body and head pass the eligibility
// switches in port/src/skinmatch.c; the renderer flushes on toggle and
// force-clears the scope each gfx_start_frame. Allocated unilaterally in the
// band both forks are stamping (board #162) - one define and one case to move.
#define G_SKINMATCH_EXT              0x4b

/* G_EXTRAGEOMETRYMODE flags */

#define G_INVERT_CULLING_EXT     0x00000001
#define G_ASPECT_LEFT_EXT        0x00000010
#define G_ASPECT_RIGHT_EXT       0x00000020
#define G_ASPECT_WIDE_EXT        0x00000040
#define G_ASPECT_CENTER_EXT      (G_ASPECT_LEFT_EXT | G_ASPECT_RIGHT_EXT)
#define G_ASPECT_MODE_EXT        (G_ASPECT_CENTER_EXT | G_ASPECT_WIDE_EXT)
#define G_NO_CLIPPING_EXT        0x00000100
#define G_MODULATE_EXT           0x00000200 // this should really go into OTHERMODE_H, but for some reason I can't get it to work
// Geometry tagged with this bit is exempt from the pd.* fx vertex effects
// (screen roll, wobble, shiny). Kai (be46717) uses it to keep 2D UI drawn as
// 3D geometry un-mirrored under its mirror cheat, which this port lacks; the
// renderer honours it the same way.
#define G_NOMIRROR_EXT           0x00000400

/* Extra texture filtering mode */

#define G_TF_BLUR_EXT (1 << G_MDSFT_TEXTFILT)

/* Texture Info Types */

#define G_TEXTYPE_NONE        0x00
#define G_TEXTYPE_GENERAL     0x01
#define G_TEXTYPE_FONT        0x02
#define G_TEXTYPE_MODEL       0x03

/* Extended command macros */

#define gDPSetFramebufferTargetEXT(pkt, f, s, w, i) \
    gSetImage(pkt, G_SETFB_EXT, f, s, w, i)

#define gDPSetFramebufferTextureEXT(pkt, f, s, w, i) \
    gSetImage(pkt, G_SETTIMG_FB_EXT, f, s, w, i)

#define gDPCopyFramebufferEXT(pkt, dst, src, uls, ult, back)   \
{                                                              \
    Gfx *_g = (Gfx *)(pkt);                                    \
                                                               \
    _g->words.w0 = _SHIFTL(G_COPYFB_EXT, 24, 8)                \
        | _SHIFTL(dst, 11, 11) | _SHIFTL(src, 0, 11) |         \
          _SHIFTL(back, 22, 1);                                \
    _g->words.w1 = _SHIFTL(uls, 16, 16) | _SHIFTL(ult, 0, 16); \
}

#define gDPInvalTexCacheEXT(pkt, addr)                 \
{                                                      \
    Gfx *_g = (Gfx *)(pkt);                            \
                                                       \
    _g->words.w0 = _SHIFTL(G_INVALTEXCACHE_EXT, 24, 8);\
    _g->words.w1 = (uintptr_t)(addr);                  \
}

#define gDPGrayscaleEXT(pkt, state)                    \
{                                                      \
    Gfx* _g = (Gfx*)(pkt);                             \
                                                       \
    _g->words.w0 = _SHIFTL(G_SETGRAYSCALE_EXT, 24, 8); \
    _g->words.w1 = state;                              \
}

#define gDPSetGrayscaleColorEXT(pkt, r, g, b, lerp) DPRGBColor(pkt, G_SETINTENSITY_EXT, r, g, b, lerp)

#define gDPChrWireframeEXT(pkt, state)                 \
{                                                      \
    Gfx* _g = (Gfx*)(pkt);                             \
                                                       \
    _g->words.w0 = _SHIFTL(G_CHRWIREFRAME_EXT, 24, 8); \
    _g->words.w1 = state;                              \
}

// HUDVD: begin the bracket of slot x (y unused, 0); a slot of -1 ends it.
#define gDPHudOffsetEXT(pkt, x, y)                                             \
{                                                                             \
    Gfx* _g = (Gfx*)(pkt);                                                     \
                                                                             \
    _g->words.w0 = _SHIFTL(G_HUDOFFSET_EXT, 24, 8) | _SHIFTL((s16)(x), 0, 16); \
    _g->words.w1 = _SHIFTL((s16)(y), 0, 16);                                  \
}

// iPod Ad: set the flat fill colour for subsequent 3D geometry (w0 low bit =
// 1), or reset to the wall default (bit 0). Colour is 0..255 per channel.
#define gDPFlatFillEXT(pkt, r, g, b)                                    \
{                                                                       \
    Gfx* _g = (Gfx*)(pkt);                                              \
                                                                        \
    _g->words.w0 = _SHIFTL(G_FLATFILL_EXT, 24, 8) | 1;                  \
    _g->words.w1 = (((u32)(r) & 0xff) << 16) | (((u32)(g) & 0xff) << 8) \
            | ((u32)(b) & 0xff);                                        \
}

// Skin match bracket: headfile is the head model's untagged file number.
#define gDPSkinMatchEXT(pkt, headfile, state)                                  \
{                                                                              \
    Gfx* _g = (Gfx*)(pkt);                                                     \
                                                                               \
    _g->words.w0 = _SHIFTL(G_SKINMATCH_EXT, 24, 8) | _SHIFTL((headfile), 0, 16); \
    _g->words.w1 = (state);                                                    \
}

#define gDPFlatFillResetEXT(pkt)                       \
{                                                      \
    Gfx* _g = (Gfx*)(pkt);                             \
                                                       \
    _g->words.w0 = _SHIFTL(G_FLATFILL_EXT, 24, 8);     \
    _g->words.w1 = 0;                                  \
}

// NOTE: these will function correctly only if you pass `gdl++` as `pkt`

#define gDPFillRectangleWideEXT(pkt, ulx, uly, lrx, lry)                         \
{                                                                                \
    Gfx *_g0 = (Gfx*)(pkt), *_g1 = (Gfx*)(pkt);                                  \
                                                                                 \
    _g0->words.w0 = _SHIFTL(G_FILLRECT_WIDE_EXT, 24, 8) | _SHIFTL((lrx), 2, 22); \
    _g0->words.w1 = _SHIFTL((lry), 2, 22);                                       \
    _g1->words.w0 = _SHIFTL((ulx), 2, 22);                                       \
    _g1->words.w1 = _SHIFTL((uly), 2, 22);                                       \
}

#define gSPTextureRectangleWideEXT(pkt, xl, yl, xh, yh, tile, s, t, dsdx, dtdy, flip)         \
{                                                                                             \
    Gfx *_g0 = (Gfx*)(pkt), *_g1 = (Gfx*)(pkt), *_g2 = (Gfx*)(pkt);                           \
                                                                                              \
    _g0->words.w0 = _SHIFTL(G_TEXRECT_WIDE_EXT, 24, 8) | _SHIFTL((xh), 0, 24);                \
    _g0->words.w1 = (_SHIFTL((yh), 0, 24) | _SHIFTL((tile), 24, 3) | _SHIFTL((flip), 27, 1)); \
    _g1->words.w0 = _SHIFTL((xl), 0, 24);                                                     \
    _g1->words.w1 = _SHIFTL((yl), 0, 24);                                                     \
    _g2->words.w0 = (_SHIFTL(s, 16, 16) | _SHIFTL(t, 0, 16));                                 \
    _g2->words.w1 = (_SHIFTL(dsdx, 16, 16) | _SHIFTL(dtdy, 0, 16));                           \
}

#define gSPImageRectangleEXT(pkt, x0, y0, s0, t0, x1, y1, s1, t1, tile, iw, ih) \
{                                                                               \
    Gfx *_g0 = (Gfx*)(pkt), *_g1 = (Gfx*)(pkt), *_g2 = (Gfx*)(pkt);             \
                                                                                \
    _g0->words.w0 = _SHIFTL(G_IMAGERECT_EXT, 24, 8) | _SHIFTL((tile), 0, 3);    \
    _g0->words.w1 = _SHIFTL((iw), 16, 16) | _SHIFTL((ih), 0, 16);               \
    _g1->words.w0 = _SHIFTL((x0), 16, 16) | _SHIFTL((y0), 0, 16);               \
    _g1->words.w1 = _SHIFTL((s0), 16, 16) | _SHIFTL((t0), 0, 16);               \
    _g2->words.w0 = _SHIFTL((x1), 16, 16) | _SHIFTL((y1), 0, 16);               \
    _g2->words.w1 = _SHIFTL((s1), 16, 16) | _SHIFTL((t1), 0, 16);               \
}

#define gSPExtraGeometryModeEXT(pkt, c, s)                                              \
{                                                                                       \
    Gfx* _g = (Gfx*)(pkt);                                                              \
                                                                                        \
    _g->words.w0 = _SHIFTL(G_EXTRAGEOMETRYMODE_EXT, 24, 8) | _SHIFTL(~(u32)(c), 0, 24); \
    _g->words.w1 = (u32)(s);                                                            \
}

#define gDPSetSubpixelOffsetEXT(pkt, x, y)                                             \
{                                                                                      \
    Gfx *_g = (Gfx*)(pkt);                                                             \
                                                                                       \
    _g->words.w0 = _SHIFTL(G_SETSUBPIXELOFFSET_EXT, 24, 8) | _SHIFTL((s16)(x), 0, 16); \
    _g->words.w1 = _SHIFTL((s16)(y), 0, 16);                                           \
}

#define gSPSetExtraGeometryModeEXT(pkt, word) gSPExtraGeometryModeEXT((pkt), 0, word)
#define gSPClearExtraGeometryModeEXT(pkt, word) gSPExtraGeometryModeEXT((pkt), word, 0)

#define gDPFillRectangleEXT gDPFillRectangleWideEXT
#define gSPTextureRectangleEXT(p, xl, yl, xh, yh, tile, s, t, ds, dt) gSPTextureRectangleWideEXT(p, xl, yl, xh, yh, tile, s, t, ds, dt, G_OFF)
#define gSPTextureRectangleFlipEXT(p, xl, yl, xh, yh, tile, s, t, ds, dt) gSPTextureRectangleWideEXT(p, xl, yl, xh, yh, tile, s, t, ds, dt, G_ON)

#define gDPFlushEXT(pkt) gDPNoParam(pkt, G_RDPFLUSH_EXT)

#define gDPClearDepthEXT(pkt) gDPNoParam(pkt, G_CLEAR_DEPTH_EXT)

#undef gDPFillRectangleScaled
#define gDPFillRectangleScaled(pkt, x1, y1, x2, y2) gDPFillRectangleEXT(pkt, (x1) * g_ScaleX, y1, (x2) * g_ScaleX, y2)

#undef gDPHudRectangle
#define gDPHudRectangle(pkt, x1, y1, x2, y2) gDPFillRectangleEXT(pkt, (x1) * g_ScaleX, y1, ((x2 + 1)) * g_ScaleX, (y2) + 1)

#else // PLATFORM_N64

#define gDPFillRectangleEXT gDPFillRectangle
#define gSPTextureRectangleEXT gSPTextureRectangle

#endif // PLATFORM_N64

/**
 * The two texture slots inside the repurposed G_NOOP (0xc0) command.
 *
 * THIS IS THE ONLY PLACE THE LAYOUT IS WRITTEN DOWN. Every reader and every
 * writer in the engine and in the tools goes through these. If the measurement
 * below ever turns out wrong for some asset, moving to different spare bits is
 * an edit to the two shift macros and nothing else - not a hunt through six
 * files that each open-coded a mask.
 *
 * A slot used to be 12 bits, wholly inside w1, which capped a texture id at
 * 4095. The shipped mod set already wants 633 slots against the 496 that left
 * available, so a slot is now 15 bits: the low 12 stay where they were and
 * three more come from w0.
 *
 * WHERE THE w0 BITS COME FROM. w0 is cmd:8 | unk08:2 | unk0a:2 | unk0c:2 |
 * unk0e:4 | unk12:4 | flags:7 | subcmd:3, so `flags` is w0 bits 9..3. Only
 * flags bit 9 is read anywhere in the tree (tex.c tests w0 & 0x200); bits 8..3
 * are read nowhere. Across all 31,148 0xc0 commands in vanilla ntsc/jpn/pal
 * ROM data - 686 model files and 60 bgdata stages, extracted and tree-walked -
 * `flags` takes exactly two values, 0 and 64, i.e. bit 9 or nothing. Bits 8..3
 * are zero in 31,148 of 31,148, so vanilla data decodes byte-identically under
 * the wider layout.
 *
 * w1 bits 31..24 are NOT spare: that is unk20, the RDP prim min-LOD, which
 * tex.c reads on both subcmds and emits into a real G_SETPRIMCOLOR.
 *
 * This is the engine's own asset encoding, not a port concern, so it is not
 * guarded by PLATFORM_N64. The N64 build decodes the same bytes.
 *
 * `slot` is 0 or 1 and is evaluated more than once; every call site passes a
 * constant or a loop index.
 */
#define G_NOOP_TEXSLOT_W1_SHIFT(slot) ((slot) ? 12 : 0)
#define G_NOOP_TEXSLOT_W0_SHIFT(slot) ((slot) ? 3 : 6)

/* Widest id a slot can name. */
#define G_NOOP_TEXSLOT_MAX 0x7fffu

/* Read slot `slot` out of a command's two words. */
#define G_NOOP_TEXSLOT(w0, w1, slot)                                          \
    ((((unsigned int)(w1) >> G_NOOP_TEXSLOT_W1_SHIFT(slot)) & 0xfffu)         \
        | ((((unsigned int)(w0) >> G_NOOP_TEXSLOT_W0_SHIFT(slot)) & 0x7u) << 12))

/* The two halves of a write. Each returns the whole updated word, leaving
 * every bit outside its own three or twelve alone, so a caller that needs to
 * rewrite one slot of a command does:
 *
 *     w0 = G_NOOP_SET_TEXSLOT_W0(w0, slot, id);
 *     w1 = G_NOOP_SET_TEXSLOT_W1(w1, slot, id);
 *
 * and must do both, or it writes the low 12 bits of a 15-bit id.
 *
 * Both are x ^ ((x ^ new) & mask) rather than the more obvious
 * (x & ~mask) | new, because a Gfx word is uintptr_t and on a 64-bit host that
 * is 64 bits wide. ~mask would be computed as a 32-bit unsigned, then
 * zero-extended, and the write would silently clear the top half of the word.
 * The XOR form never forms a complement, so it preserves a word of any width
 * and works unchanged in the 32-bit host tools. */
#define G_NOOP_SET_TEXSLOT_W1(w1, slot, id)                                   \
    ((w1) ^ (((w1) ^ (((unsigned int)(id) & 0xfffu)                           \
            << G_NOOP_TEXSLOT_W1_SHIFT(slot)))                                \
        & (0xfffu << G_NOOP_TEXSLOT_W1_SHIFT(slot))))

#define G_NOOP_SET_TEXSLOT_W0(w0, slot, id)                                   \
    ((w0) ^ (((w0) ^ ((((unsigned int)(id) >> 12) & 0x7u)                     \
            << G_NOOP_TEXSLOT_W0_SHIFT(slot)))                                \
        & (0x7u << G_NOOP_TEXSLOT_W0_SHIFT(slot))))

#define gSetTexInfoEXT(pkt, cmd, type, id, texnum, idmask)        \
{                                                                 \
    Gfx *_g = (Gfx *)(pkt);                                       \
                                                                  \
    _g->words.w0 = _SHIFTL(cmd, 24, 8) | _SHIFTL(idmask, 8, 8)    \
		| _SHIFTL(type, 0, 8);                                    \
    _g->words.w1 = _SHIFTL(id, 20, 12) | _SHIFTL(texnum, 0, 20);  \
}

#define gsSetTexInfoEXT(cmd, type, id, texnum, idmask) \
{                                                      \
    _SHIFTL(cmd, 24, 8) | _SHIFTL(idmask, 8, 8)        \
		| _SHIFTL(type, 0, 8),                         \
    _SHIFTL(id, 20, 12) | _SHIFTL(texnum, 0, 20)       \
}

#define gDPSetTextureInfoEXT(pkt, type, id, texnum, idmask) gSetTexInfoEXT(pkt, G_SETTEXINFO_EXT, type, id, texnum, idmask)
#define gsDPSetTextureInfoEXT(type, id, texnum, idmask)     gsSetTexInfoEXT(G_SETTEXINFO_EXT, type, id, texnum, idmask)

#endif
