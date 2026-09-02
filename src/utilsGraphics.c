/*
 * SynthLib - common library for synthesizer editor applications.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef __cplusplus
extern "C" {
#endif

// NO GRAPHICS HEADER, and that is the point of the split (2026-08-28). Everything this file used
// to do with OpenGL directly now goes through renderBackend.h's nine gfx_* calls, which exactly
// one renderBackend*.c implements. FreeType stays: rasterizing a glyph produces a buffer in RAM
// and is not a graphics-API operation.
//
// The compiler enforces it. A drawing primitive here cannot reach for a GL call, because there is
// no declaration for one — which is the same guarantee moduleGraphics.c and paramOverlay.c got
// when the seam was first drawn, now extended to the file that drew it.

// Disable warnings from external library headers etc.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"

#include <ft2build.h>
#include FT_FREETYPE_H
#pragma clang diagnostic pop

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "synthlibDefs.h"
#include "clickRegion.h"
#include "geometry.h"
#include "renderBackend.h"
#include "utilsGraphics.h"

// Glyph atlases.
//
// Text is rendered from a glyph bitmap rasterized at the integer pixel height it is actually
// drawn at, and blitted 1:1 to integer framebuffer pixels. Anything else resamples the glyph:
// a fixed rasterization size has to be scaled to fit, and that final bilinear resample at an
// arbitrary sub-pixel phase smears away exactly the stem alignment FreeType's hinter just
// produced. On a 2x display there are enough pixels to hide it; at 1x, where the UI can scale
// text down to ~6px, it turns small text to mush.
//
// So atlases are keyed by integer drawn height and cached (in practice a running app needs only
// two or three: G2-Edit at a 700px window on a 1x display draws text at just 6.6px and 4.6px).
// The height is the drawn height rather than anything derived from gGlobalGuiScale, because
// module text is additionally scaled by gZoomFactor and only the drawn height sees both.
//
// Layout is deliberately NOT driven by these atlases. get_text_width() and everything built on
// it stay on one canonical set of metrics (gCanonInfo, taken once at a reference size), so text
// widths remain continuous and proportional and boxes fit exactly as before. Only the glyph
// images and their final pixel positions come from the sized atlas.
#define GLYPH_ATLAS_PADDING    (2)       // transparent gap between glyphs, in atlas pixels
// SMALL TEXT IS RASTERIZED BIG AND SCALED DOWN. Below this drawn height the 1:1 blit costs more
// than it buys: the atlas em is an INTEGER, and the glyph bitmaps that come out of it are integers
// too, so at an em of 6 one pixel of rounding is 17% of the text's height. Measured on a 1280x720
// display, the same button's text filled 0.385 of its box where the Retina build gives 0.526 — the
// 14% CT could see. Rasterizing at twice the size and drawing at the true fractional height puts
// the quantisation back under a few percent, at the cost of softer stems, which is the better
// trade this small. Above the threshold nothing changes and the crisp 1:1 path is untouched.
//
// THE TRIGGER IS THE DRAWN HEIGHT, NOT THE DISPLAY. "Retina or not" is a proxy and a leaky one:
// gGlobalGuiScale runs continuously with the window size (0.69, 0.88 and 2.37 were all measured on
// one machine in one afternoon), so a zoomed-out canvas on a Retina display draws 6px text too and
// wants exactly the same treatment.
#define GLYPH_SUPERSAMPLE_BELOW_PX    (12)
#define GLYPH_SUPERSAMPLE_FACTOR      (2)

#define GLYPH_ATLAS_MIN_PX            (4)    // below this there is no glyph left to draw
#define GLYPH_ATLAS_MAX_PX            (256)
#define GLYPH_ATLAS_CACHE_SIZE        (6)    // distinct drawn text heights kept resident
#define GLYPH_REFERENCE_PX            (72.0) // size the canonical layout metrics are taken at

typedef struct {
    int       pixelHeight;                   // key: drawn text height this atlas serves, 0 when unused
    int       supersample;                   // 1 = rasterized at the drawn size and blitted 1:1
    uint32_t  texture;                       // opaque backend handle, not a GLuint — see utilsGraphics.h
    int       width;
    int       height;
    uint64_t  lastUsed;
    GlyphInfo info[MAX_GLYPH_CHAR];
} tSizedAtlas;

static tSizedAtlas    gAtlasCache[GLYPH_ATLAS_CACHE_SIZE]    = {};
static uint64_t       gAtlasUseCounter                       = 0;
static GlyphInfo      gCanonInfo[MAX_GLYPH_CHAR]             = {0};  // canonical layout metrics, never drawn from
static char *         gFontPath                              = NULL; // retained so atlases can be built on demand
static double         gCanonEmPx                             = 0.0;  // gMaxAscent + gMaxDescent at GLYPH_REFERENCE_PX

// Text widths are derived from the glyph metrics, so the cache has to be dropped whenever the
// atlas is re-rasterized (hinting means widths are not exactly proportional across sizes).
#define TEXT_WIDTH_CACHE_SIZE    (64)
static struct {
    const char * text;
    double       height;
    double       width;
}                     gTextWidthCache[TEXT_WIDTH_CACHE_SIZE] = {0};
static int            gTextWidthCacheCount                   = 0;

static void clear_text_width_cache(void) {
    gTextWidthCacheCount = 0;
}

static tSizedAtlas * atlas_for_height(int pixelHeight);             // defined with the glyph atlas code below
static double         gMaxAscent                             = 0.0; // Used for dealing with preloaded text character height
static double         gMaxDescent                            = 0.0;
static double         gMetricsHeight                         = 0.0;
// One pane of the module canvas — see the module-pane block in utilsGraphics.h. Scroll is per
// pane; zoom deliberately is not. top/height are fractions of the canvas band, so pane 0's
// {0.0, 1.0} is the whole band and reproduces the single-canvas behaviour exactly.
typedef struct {
    double xScrollPercent;
    double yScrollPercent;
    double top;
    double height;
} tModulePane;

static tModulePane    gModulePane[MAX_MODULE_PANES]          = {
    {0.0, 0.0, 0.0, 1.0},
    {0.0, 0.0, 0.0, 0.0},
};
static uint32_t       gModulePaneCount                       = 1;
static uint32_t       gCurrentModulePane                     = 0;

static double         gZoomFactor                            = NO_ZOOM;
static int            gRenderWidth                           = 0;
static int            gRenderHeight                          = 0;
static tSynthLibTheme gTheme                                 = {0}; // Set once via configure_synthlib_theme() — see utilsGraphics.h

void configure_synthlib_theme(tSynthLibTheme theme) {
    gTheme = theme;
}

static inline double scale(double value) {
    return value * gZoomFactor;
}

static inline double global_scale(double value) {
    return value * gGlobalGuiScale;
}

void set_module_pane(uint32_t pane) {
    if (pane < MAX_MODULE_PANES) {
        gCurrentModulePane = pane;
    }
}

uint32_t module_pane(void) {
    return gCurrentModulePane;
}

uint32_t module_pane_count(void) {
    return gModulePaneCount;
}

void set_module_pane_count(uint32_t count) {
    if ((count >= 1) && (count <= MAX_MODULE_PANES)) {
        gModulePaneCount = count;
    }
}

void set_module_pane_extent(uint32_t pane, double topFraction, double heightFraction) {
    if (pane >= MAX_MODULE_PANES) {
        return;
    }
    gModulePane[pane].top    = topFraction;
    gModulePane[pane].height = heightFraction;
}

double calc_scroll_x(void) {
    tRectangle area  = module_area();
    double     value = 0.0;

    value = (gModulePane[gCurrentModulePane].xScrollPercent * (scale((MAX_COLUMNS + 1) * MODULE_X_SPAN) - area.size.w)) / 100.0;

    if (value < 0.0) {
        value = 0.0;
    }
    return value;
}

double calc_scroll_y(void) {
    tRectangle area  = module_area();
    double     value = 0.0;

    value = (gModulePane[gCurrentModulePane].yScrollPercent * (scale(((MAX_ROWS + 1) + (MAX_ROWS_MODULE - 1)) * MODULE_Y_SPAN) - area.size.h)) / 100.0;

    if (value < 0.0) {
        value = 0.0;
    }
    return value;
}

static tCoord scale_coord(tCoord coord) {
    return (tCoord){
        scale(coord.x), scale(coord.y)
    };
}

static tSize scale_size(tSize size) {
    return (tSize){
        scale(size.w), scale(size.h)
    };
}

static tRectangle scale_rectangle(tRectangle rectangle) {
    return (tRectangle){
        scale_coord(rectangle.coord), scale_size(rectangle.size)
    };
}

static tCoord global_scale_coord(tCoord coord) {
    return (tCoord){
        global_scale(coord.x), global_scale(coord.y)
    };
}

static tSize global_scale_size(tSize size) {
    return (tSize){
        global_scale(size.w), global_scale(size.h)
    };
}

static tRectangle global_scale_rectangle(tRectangle rectangle) {
    return (tRectangle){
        global_scale_coord(rectangle.coord), global_scale_size(rectangle.size)
    };
}

static tCoord adjust_to_module_area_coord(tCoord coord) {
    tRectangle area = module_area();

    coord.x += area.coord.x;
    coord.y += area.coord.y;
    return coord;
}

static tRectangle adjust_to_module_area_rectangle(tRectangle rectangle) {
    return (tRectangle){
        adjust_to_module_area_coord(rectangle.coord), rectangle.size
    };
}

static tCoord adjust_scroll_coord(tCoord coord) {
    coord.x -= calc_scroll_x();
    coord.y -= calc_scroll_y();
    return coord;
}

static tRectangle adjust_scroll_rectangle(tRectangle rectangle) {
    return (tRectangle){
        adjust_scroll_coord(rectangle.coord), rectangle.size
    };
}

static tCoord scale_scroll_adjust_coord(tCoord coord) {
    coord = scale_coord(coord);
    coord = adjust_scroll_coord(coord);
    coord = adjust_to_module_area_coord(coord);
    return coord;
}

static tRectangle scale_scroll_adjust_rectangle(tRectangle rectangle) {
    rectangle = scale_rectangle(rectangle);
    rectangle = adjust_scroll_rectangle(rectangle);
    rectangle = adjust_to_module_area_rectangle(rectangle);
    return rectangle;
}

tRectangle module_area_for_pane(uint32_t pane) {
    // The canvas BAND: everything between the top bar and the scrollbars. This is what the single
    // canvas used to be, and it is what the panes divide up between them.
    double left       = MODULE_MARGIN;
    double bandTop    = gTheme.topBarHeight + MODULE_MARGIN;
    double width      = (gRenderWidth / gGlobalGuiScale) - MODULE_SCROLLBAR_WIDTH - (MODULE_MARGIN * 2.0);
    double bandHeight = (gRenderHeight / gGlobalGuiScale) - gTheme.topBarHeight - MODULE_SCROLLBAR_WIDTH - (MODULE_MARGIN * 2.0);

    if (pane >= MAX_MODULE_PANES) {
        pane = 0;
    }
    // Sliced by fraction rather than by pixels so a window resize redistributes the split
    // proportionally instead of stranding one pane at a fixed height. With pane 0 at {0.0, 1.0}
    // this is arithmetically the whole band, i.e. exactly the rectangle this function used to
    // return before panes existed.
    return (tRectangle){{
                            left, bandTop + (bandHeight * gModulePane[pane].top)
                        }, {
                            width, bandHeight * gModulePane[pane].height
                        }
    };
}

tRectangle module_area(void) {
    return module_area_for_pane(gCurrentModulePane);
}

// ── Geometry batching ────────────────────────────────────────────────────────
//
// Every primitive below used to be a glBegin/glVertex/glEnd burst — immediate mode, one GL call
// per vertex. Nothing after OpenGL has an immediate mode: Metal, D3D and Vulkan all want a buffer
// of vertices and one submission. So the primitives no longer talk to GL at all; they append
// triangles here, and the batch is submitted as a single array.
//
// WHAT THIS IS NOT: a reordering. A 2D UI is painted back to front and every overlap depends on
// draw order, so the batch is flushed the moment anything that would change how subsequent
// vertices rasterize changes — the bound texture, the scissor rect, a clear, a read-back, or the
// end of the frame. Consecutive draws that share all of those merge; nothing else does. That keeps
// the pixels bit-identical while still collapsing the common runs (a module face's rectangles, a
// string's glyphs) into one call.
//
// The four topologies the old code used (GL_QUADS, GL_POLYGON, GL_TRIANGLE_FAN, GL_TRIANGLE_STRIP)
// all become plain triangle lists, which is the one topology every backend agrees on. Winding is
// not normalised because nothing in any of the three apps enables face culling.

// tVertex is renderBackend.h's — it is the wire format between this file and whichever backend
// is compiled, so it is declared where the contract is.

static tVertex * gBatchVerts    = NULL;
static size_t    gBatchCount    = 0;
static size_t    gBatchCap      = 0;
static uint32_t  gBatchTexture  = 0;   // 0 = untextured

// THE CURRENT COLOUR, which used to be GL's. set_rgb_colour()/set_rgba_colour() are called ~188
// times across the three apps to set the colour of whatever is drawn next; that was glColor3f/4f,
// i.e. GL state. It is a plain variable now, written into each vertex at append time.
static tRgba     gCurrentColour = {1.0, 1.0, 1.0, 1.0};

static void batch_push(float x, float y, float u, float v, tRgba c) {
    if (gBatchCount == gBatchCap) {
        size_t    newCap = (gBatchCap == 0) ? 4096 : (gBatchCap * 2);
        tVertex * grown  = (tVertex *)realloc(gBatchVerts, newCap * sizeof(tVertex));

        if (grown == NULL) {
            return;    // Drop the vertex rather than die mid-frame; the frame redraws constantly.
        }
        gBatchVerts = grown;
        gBatchCap   = newCap;
    }
    gBatchVerts[gBatchCount++] = (tVertex){
        x, y, u, v,
        (float)c.red, (float)c.green, (float)c.blue, (float)c.alpha
    };
}

// The batch's public face, and the whole of its dealing with the backend: one array of finished
// triangles and the texture they sample. WHICH triangles ended up in it — the merging rules, the
// sticky texture, the forced submissions — is decided above and is identical on every platform,
// which is why none of it is in renderBackend.h.
void render_backend_flush(void) {
    if (gBatchCount == 0) {
        return;
    }
    gfx_submit(gBatchVerts, gBatchCount, gBatchTexture);
    gBatchCount = 0;
}

void render_present(void) {
    render_backend_flush();
    gfx_present();
}

// Anything that changes how following vertices rasterize flushes what is already queued first.
static void batch_set_texture(uint32_t texture) {
    if (texture != gBatchTexture) {
        render_backend_flush();
        gBatchTexture = texture;
    }
}

// UNTEXTURED geometry has to say so, and this is the one thing about the batch that is not
// obvious: the texture is STICKY. In immediate mode every draw stated its own texturing —
// internal_render_text() and internal_render_texture() each ended by unbinding and disabling
// GL_TEXTURE_2D, so a rectangle drawn afterwards was untextured because the last textured draw
// had cleaned up after itself. Nothing cleans up here: the batch carries whatever texture was
// last selected until something selects another. Without this call every shape drawn after the
// first string samples texel (0,0) of the glyph atlas — which is empty, so alpha 0, so the
// entire UI except its text renders INVISIBLE. Called from the two places that append plain
// triangles, which is every primitive that is not text or render_texture().
static void batch_untextured(void) {
    batch_set_texture(0);
}

// ── Topology helpers ─────────────────────────────────────────────────────────
// Each mirrors one of the four glBegin modes the old code used.

static void batch_tri(float x0, float y0, float x1, float y1, float x2, float y2, tRgba c) {
    batch_untextured();
    batch_push(x0, y0, 0.0f, 0.0f, c);
    batch_push(x1, y1, 0.0f, 0.0f, c);
    batch_push(x2, y2, 0.0f, 0.0f, c);
}

// GL_QUADS, one quad: corners in the same order the old glVertex calls used.
static void batch_quad(float x0, float y0, float x1, float y1,
                       float x2, float y2, float x3, float y3, tRgba c) {
    batch_tri(x0, y0, x1, y1, x2, y2, c);
    batch_tri(x0, y0, x2, y2, x3, y3, c);
}

// GL_QUADS with texture coordinates, for the glyph atlas and render_texture().
static void batch_quad_uv(float x0, float y0, float u0, float v0,
                          float x1, float y1, float u1, float v1,
                          float x2, float y2, float u2, float v2,
                          float x3, float y3, float u3, float v3, tRgba c) {
    batch_push(x0, y0, u0, v0, c);
    batch_push(x1, y1, u1, v1, c);
    batch_push(x2, y2, u2, v2, c);

    batch_push(x0, y0, u0, v0, c);
    batch_push(x2, y2, u2, v2, c);
    batch_push(x3, y3, u3, v3, c);
}

// GL_TRIANGLE_STRIP, fed one vertex at a time exactly as the old loops did.
typedef struct {
    float x[2], y[2];
    tRgba c[2];
    int   n;
} tStripBuild;

static void strip_add(tStripBuild * s, float x, float y, tRgba c) {
    batch_untextured();

    if (s->n >= 2) {
        batch_push(s->x[0], s->y[0], 0.0f, 0.0f, s->c[0]);
        batch_push(s->x[1], s->y[1], 0.0f, 0.0f, s->c[1]);
        batch_push(x, y, 0.0f, 0.0f, c);

        s->x[0] = s->x[1];
        s->y[0] = s->y[1];
        s->c[0] = s->c[1];
        s->x[1] = x;
        s->y[1] = y;
        s->c[1] = c;
    } else {
        s->x[s->n] = x;
        s->y[s->n] = y;
        s->c[s->n] = c;
        s->n++;
    }
}

// GL_TRIANGLE_FAN. First vertex is the hub, as it was for glBegin(GL_TRIANGLE_FAN).
typedef struct {
    float cx, cy, px, py;
    int   n;
} tFanBuild;

static void fan_add(tFanBuild * f, float x, float y, tRgba c) {
    if (f->n == 0) {
        f->cx = x;
        f->cy = y;
    } else if (f->n == 1) {
        f->px = x;
        f->py = y;
    } else {
        batch_tri(f->cx, f->cy, f->px, f->py, x, y, c);
        f->px = x;
        f->py = y;
    }
    f->n++;
}

void module_pane_clip_begin(void) {
    tRectangle pane = module_area();

    // Framebuffer pixels, origin TOP LEFT — the same space the vertices are in. The flip to
    // whatever the backend's scissor origin is belongs to the backend and used to be here.
    double     x    = pane.coord.x * gGlobalGuiScale;
    double     y    = pane.coord.y * gGlobalGuiScale;
    double     w    = pane.size.w * gGlobalGuiScale;
    double     h    = pane.size.h * gGlobalGuiScale;

    if (w < 0.0) {
        w = 0.0;
    }

    if (h < 0.0) {
        h = 0.0;
    }
    // Anything already queued was drawn UNCLIPPED and must go out before the clip exists.
    render_backend_flush();

    // ROUNDED HERE, ONCE, so that every backend clips the same whole pixels. Truncating both
    // EDGES — rather than the origin and the size separately — makes the rectangle cover exactly
    // the pixels whose top-left corner lies inside it, which is the obvious reading of a
    // fractional rect and, unlike leaving it to each backend, one they cannot disagree about.
    // They did disagree: it was the single defect the Metal port turned up, two rows out of 1704
    // at the pane boundary. See gfx_scissor() in renderBackend.h.
    gfx_scissor((int)x, (int)y, (int)(x + w) - (int)x, (int)(y + h) - (int)y);

    // The CLICK regions get the same clip as the pixels, set here so the two cannot drift apart. A
    // module scrolled past the bottom of its pane is not drawn there — and must not be clickable
    // there either, which it was: in the split view a Voice Area module scrolled under the FX pane
    // could still be selected through it. See set_click_region_clip().
    set_click_region_clip(&pane);
}

void module_pane_clip_end(void) {
    // ...and what was queued INSIDE the clip must go out before it is dropped.
    render_backend_flush();

    gfx_scissor(0, 0, -1, 0);   // negative width = clipping off
    set_click_region_clip(NULL);
}

// Returns true if any part of `rectangle` (moduleArea-local coordinates, i.e. the same
// space passed to render_rectangle(moduleArea, ...) / render_module()'s own moduleRectangle)
// would land within the currently visible, scrolled/zoomed module canvas. Callers can use
// this to skip rendering work for things that are entirely off-screen — it applies the exact
// same scale/scroll transform real rendering does, so it can't drift out of sync with what's
// actually drawn. A rectangle straddling the edge of the viewport still counts as visible.
bool rectangle_visible_in_module_area(tRectangle rectangle) {
    tRectangle screenRect = scale_scroll_adjust_rectangle(rectangle);
    tRectangle viewport   = module_area();

    return !(  (screenRect.coord.x + screenRect.size.w) < viewport.coord.x
            || screenRect.coord.x > (viewport.coord.x + viewport.size.w)
            || (screenRect.coord.y + screenRect.size.h) < viewport.coord.y
            || screenRect.coord.y > (viewport.coord.y + viewport.size.h));
}

static void internal_render_line(tCoord start, tCoord end, double thickness) {
    double      half_thickness = thickness * 0.5;
    double      dx             = end.x - start.x;
    double      dy             = end.y - start.y;
    double      length         = sqrt(dx * dx + dy * dy);

    if (length == 0.0) {
        return;
    }
    // Normalize direction
    double      nx             = dx / length;
    double      ny             = dy / length;

    // Perpendicular vector (for thickness)
    double      px             = -ny * half_thickness;
    double      py             = nx * half_thickness;

    // Draw the thick line as a rectangle. Fed through strip_add in the same order the four
    // glVertex calls used, so the two triangles cover exactly what GL_TRIANGLE_STRIP covered.
    tStripBuild strip          = {0};

    strip_add(&strip, (float)(start.x - px), (float)(start.y - py), gCurrentColour);
    strip_add(&strip, (float)(start.x + px), (float)(start.y + py), gCurrentColour);
    strip_add(&strip, (float)(end.x - px), (float)(end.y - py), gCurrentColour);
    strip_add(&strip, (float)(end.x + px), (float)(end.y + py), gCurrentColour);
}

static void internal_render_rectangle(tRectangle rectangle) {
    if ((rectangle.size.w > 0.0) && (rectangle.size.h > 0.0)) {
        batch_quad((float)rectangle.coord.x, (float)rectangle.coord.y,
                   (float)(rectangle.coord.x + rectangle.size.w), (float)rectangle.coord.y,
                   (float)(rectangle.coord.x + rectangle.size.w), (float)(rectangle.coord.y + rectangle.size.h),
                   (float)rectangle.coord.x, (float)(rectangle.coord.y + rectangle.size.h),
                   gCurrentColour);
    }
}

static void internal_render_texture(tRectangle rectangle, uint32_t texture) {
    if ((rectangle.size.w > 0.0) && (rectangle.size.h > 0.0)) {
        batch_set_texture(texture);

        // White, so the texture blits untinted. The old code set the GL colour here and never
        // put it back, leaving white current for whatever drew next; that is kept rather than
        // tidied — this is EmuUtility's LCD, its only caller, and what follows it on screen was
        // drawn against that colour.
        gCurrentColour = (tRgba){
            1.0, 1.0, 1.0, 1.0
        };

        batch_quad_uv((float)rectangle.coord.x, (float)rectangle.coord.y, 0.0f, 0.0f,
                      (float)(rectangle.coord.x + rectangle.size.w), (float)rectangle.coord.y, 1.0f, 0.0f,
                      (float)(rectangle.coord.x + rectangle.size.w), (float)(rectangle.coord.y + rectangle.size.h), 1.0f, 1.0f,
                      (float)rectangle.coord.x, (float)(rectangle.coord.y + rectangle.size.h), 0.0f, 1.0f,
                      gCurrentColour);
    }
}

static void internal_render_circle_part(tCoord coord, double radius, int segments, int startSeg, int numSegs) {
    double    angle = 0.0;
    double    x     = 0.0;
    double    y     = 0.0;
    int       i     = 0;

    // seg 0 starting point = horizontel, right

    tFanBuild fan   = {0};

    fan_add(&fan, (float)coord.x, (float)coord.y, gCurrentColour);  // Center

    for (i = 0; i <= numSegs; i++) {
        angle = 2.0f * M_PI * (double)(i + startSeg) / (double)segments;
        x     = coord.x + cos(angle) * radius;
        y     = coord.y + sin(angle) * radius;
        fan_add(&fan, (float)x, (float)y, gCurrentColour);
    }
}

static void internal_render_circle_line_part_angle(tCoord coord, double radius, double startAngle, double endAngle, double thickness, int numSteps) {
    const double DEG_TO_RAD     = M_PI / 180.0;
    double       angle, x_inner, y_inner, x_outer, y_outer;
    double       half_thickness = thickness * 0.5;

    // Normalize angle range
    double       sweep          = fmod((endAngle - startAngle + 360.0), 360.0);

    if (sweep == 0) {
        return;                    // Avoid rendering nothing
    }
    // GL_LINE_SMOOTH used to be enabled here. It only ever applied to GL_LINES, and this has
    // drawn triangles for as long as it has existed, so it did nothing at all.
    tStripBuild  strip          = {0};

    for (int i = 0; i <= numSteps; i++) {
        double interpAngle = startAngle + (sweep * (double)i / (double)numSteps);
        angle   = (interpAngle - 90.0) * DEG_TO_RAD;   // 0° is at the top

        // Inner edge
        x_inner = coord.x + cos(angle) * (radius - half_thickness);
        y_inner = coord.y + sin(angle) * (radius - half_thickness);

        // Outer edge
        x_outer = coord.x + cos(angle) * (radius + half_thickness);
        y_outer = coord.y + sin(angle) * (radius + half_thickness);

        strip_add(&strip, (float)x_inner, (float)y_inner, gCurrentColour);
        strip_add(&strip, (float)x_outer, (float)y_outer, gCurrentColour);
    }
}

// ── UTF-8, reduced to the glyph table ────────────────────────────────────────
//
// THE ATLAS HOLDS ASCII ONLY (see MAX_GLYPH_CHAR), and every byte above it used to be replaced by
// '?' one byte at a time. A single em dash is three bytes of UTF-8, so a perfectly ordinary status
// line — "Sent — the connected device's live edit buffer..." — reached the screen as "Sent ??? the
// connected...". Owner-reported on SynthEdit's restore alert, but nothing about it was specific to
// that string: the source of all three apps carries around ninety em dashes inside string literals,
// and any of them that reaches a drawn string does the same thing.
//
// So a whole UTF-8 sequence is consumed at once and turned into ONE glyph. The handful of
// codepoints that actually occur in these apps' strings get a sensible ASCII stand-in; anything
// else still becomes a single '?', which is a fair report of "this font has no such character"
// rather than a count of how many bytes it took to encode.
//
// Not a general Unicode layer, and deliberately not: the fix needed is that non-ASCII punctuation
// stops multiplying, and a transliteration table does that in one place. Real non-Latin text would
// need a real font atlas, which is a different job.
typedef struct {
    unsigned char glyph;    // what to draw from the ASCII atlas
    uint32_t      bytes;    // how many bytes of the input this consumed; never 0
} tGlyphStep;

static tGlyphStep next_glyph(const char * ch) {
    unsigned char first     = (unsigned char)ch[0];

    if (first < MAX_GLYPH_CHAR) {
        return (tGlyphStep){
            first, 1
        };
    }
    // Decode one UTF-8 sequence. A truncated or malformed one consumes its lead byte only, so a
    // corrupt string still terminates rather than walking off the end.
    uint32_t      codePoint = 0;
    uint32_t      length    = 0;

    if ((first & 0xE0) == 0xC0) {
        codePoint = first & 0x1Fu;
        length    = 2;
    } else if ((first & 0xF0) == 0xE0) {
        codePoint = first & 0x0Fu;
        length    = 3;
    } else if ((first & 0xF8) == 0xF0) {
        codePoint = first & 0x07u;
        length    = 4;
    } else {
        return (tGlyphStep){
            '?', 1
        };
    }

    for (uint32_t i = 1; i < length; i++) {
        if ((ch[i] & 0xC0) != 0x80) {
            return (tGlyphStep){
                '?', 1
            };
        }
        codePoint = (codePoint << 6) | ((unsigned char)ch[i] & 0x3Fu);
    }

    switch (codePoint) {
        case 0x2013:            // en dash
        case 0x2014: return (tGlyphStep){
                '-', length
            };                                           // em dash — by far the common one here

        case 0x2018:
        case 0x2019: return (tGlyphStep){
                '\'', length
            };                                           // curly single quotes

        case 0x201C:
        case 0x201D: return (tGlyphStep){
                '"', length
            };                                           // curly double quotes

        case 0x2022: return (tGlyphStep){
                '*', length
            };                                           // bullet

        case 0x00B0: return (tGlyphStep){
                'o', length
            };                                           // degree

        case 0x00D7: return (tGlyphStep){
                'x', length
            };                                           // multiplication sign

        case 0x2192: return (tGlyphStep){
                '>', length
            };                                           // rightwards arrow

        case 0x00A0: return (tGlyphStep){
                ' ', length
            };                                           // non-breaking space

        default:     return (tGlyphStep){
                '?', length
            };
    }
}

static void internal_render_text(tRectangle rectangle, const char * text) {
    double        scaleFactor = 0.0;
    const char *  ch          = NULL;

    if (text == NULL) {
        //LOG_ERROR("render_text text=NULL\n");
        return;
    }

    if (*text == '\0') {
        return;
    }
    // rectangle is already in framebuffer pixels, so this is the true on-screen text height.
    // Round it to whole pixels and draw from a glyph atlas rasterized at exactly that size.
    int           pixelHeight = (int)lround(rectangle.size.h);
    tSizedAtlas * atlas       = atlas_for_height(pixelHeight);

    if (atlas == NULL) {
        return;
    }
    // No blend enable/disable here: render_backend_init() turns blending on for the
    // whole session. This function used to enable it and then DISABLE it on the way
    // out, which revoked the session-wide enable the moment the first string was
    // drawn — see the invariant in utilsGraphics.h.
    //
    // The atlas is SELECTED, not bound: batch_set_texture() flushes whatever is queued against
    // the previous texture and records this one, and render_backend_flush() does the binding.
    // Consecutive strings drawn at the same size therefore leave as a single call.
    batch_set_texture(atlas->texture);

    // No glScalef: glyphs are blitted at their rasterized size, one texel per pixel. Positions are
    // snapped to whole pixels so that stays true for every glyph in the string.
    scaleFactor = rectangle.size.h / (gMaxAscent + gMaxDescent);

    // The baseline comes from the CANONICAL ascent, exactly where it sat before glyphs were
    // rasterized per size, and is rounded once. Deriving it from the atlas's own ascent instead
    // would round a second time against a rasterization whose em height only approximates the
    // requested one — which drops the text up to a pixel, and by differing amounts for different
    // text sizes, so some labels sit low and others don't.
    double originX     = round(rectangle.coord.x);
    double baselineY   = round(rectangle.coord.y + (gMaxAscent * scaleFactor));

    // The pen advances by the CANONICAL advance, scaled to this size, so a string occupies
    // exactly the width get_text_width() predicted for it. Only the position each glyph is
    // finally drawn at is rounded, which costs sub-pixel letter spacing and buys pixel
    // alignment — the right trade at these sizes.
    double xCharOffset = 0.0;

    // How much to shrink this atlas's bitmaps by when drawing them. 1.0 for a 1:1 atlas. For a
    // supersampled one it is 1/factor, CORRECTED by the ratio between the height actually asked
    // for and the integer the atlas was built for — which is what removes the last of the
    // quantisation rather than merely reducing it.
    double glyphScale  = 1.0;

    if (atlas->supersample > 1) {
        glyphScale = (rectangle.size.h / (double)pixelHeight) / (double)atlas->supersample;
    }
    ch = text;

    while (*ch) {
        // A whole UTF-8 sequence at a time, not a byte — see next_glyph().
        tGlyphStep    step      = next_glyph(ch);
        unsigned char character = step.glyph;
        GlyphInfo *   glyph     = &atlas->info[character];

        // Texture coordinates for the glyph
        double        u1        = glyph->u1;
        double        v1        = glyph->v1;
        double        u2        = glyph->u2;
        double        v2        = glyph->v2;

        // Placement. In the 1:1 case the pen is snapped to a whole pixel so the glyph lands on
        // exact texels — the crisp path, unchanged. In the supersampled case the bitmap is being
        // scaled anyway, so snapping buys nothing and costs the even letter spacing that made the
        // text look wrong in the first place: the position stays fractional.
        double        xPen      = (glyphScale == 1.0) ? round(originX + xCharOffset)
                                  : (originX + xCharOffset);
        double        xPos      = xPen + (glyph->offset_x * glyphScale);
        double        yPos      = baselineY - (glyph->offset_y * glyphScale);
        double        w         = glyph->width * glyphScale;
        double        h         = glyph->height * glyphScale;

        if ((w > 0.0) && (h > 0.0)) {
            batch_quad_uv((float)xPos, (float)yPos, (float)u1, (float)v1,               // Bottom-left
                          (float)(xPos + w), (float)yPos, (float)u2, (float)v1,         // Bottom-right
                          (float)(xPos + w), (float)(yPos + h), (float)u2, (float)v2,   // Top-right
                          (float)xPos, (float)(yPos + h), (float)u1, (float)v2,         // Top-left
                          gCurrentColour);
        }
        xCharOffset += gCanonInfo[character].advance_x * scaleFactor;

        ch          += step.bytes;
    }
}

tRectangle render_line(tArea area, tCoord start, tCoord end, double thickness) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        start     = scale_scroll_adjust_coord(start);
        end       = scale_scroll_adjust_coord(end);
        thickness = scale(thickness);
    }
    retRectangle = (tRectangle){{
                                    0.0, 0.0
                                }, {
                                    0.0, 0.0
                                }
    };

    start        = global_scale_coord(start);
    end          = global_scale_coord(end);
    thickness    = global_scale(thickness);

    internal_render_line(start, end, thickness);

    return retRectangle;
}

tRectangle render_rectangle(tArea area, tRectangle rectangle) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        rectangle = scale_scroll_adjust_rectangle(rectangle);
    }
    retRectangle = rectangle;

    rectangle    = global_scale_rectangle(rectangle);

    internal_render_rectangle(rectangle);

    return retRectangle;
}

tRectangle render_texture(tArea area, tRectangle rectangle, uint32_t texture) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        rectangle = scale_scroll_adjust_rectangle(rectangle);
    }
    retRectangle = rectangle;

    rectangle    = global_scale_rectangle(rectangle);

    internal_render_texture(rectangle, texture);

    return retRectangle;
}

tRectangle render_rectangle_with_border(tArea area, tRectangle rectangle) {
    tRectangle retRectangle    = {0};

    double     borderLineWidth = BORDER_LINE_WIDTH;

    if (area == moduleArea) {
        rectangle       = scale_scroll_adjust_rectangle(rectangle);
        borderLineWidth = scale(borderLineWidth);
    }
    retRectangle    = rectangle;

    rectangle       = global_scale_rectangle(rectangle);
    borderLineWidth = global_scale(borderLineWidth);

    tRectangle line            = {0};

    internal_render_rectangle(rectangle);

    set_rgb_colour((tRgb)RGB_BLACK);
    line            = (tRectangle){{
                                       rectangle.coord.x, rectangle.coord.y + rectangle.size.h - borderLineWidth
                                   }, {
                                       rectangle.size.w, borderLineWidth
                                   }
    };
    internal_render_rectangle(line); //Bottom
    set_rgb_colour((tRgb)RGB_WHITE);
    line            = (tRectangle){{
                                       rectangle.coord.x, rectangle.coord.y
                                   }, {
                                       borderLineWidth, rectangle.size.h
                                   }
    };
    internal_render_rectangle(line); //Left
    set_rgb_colour((tRgb)RGB_WHITE);
    line            = (tRectangle){{
                                       rectangle.coord.x, rectangle.coord.y
                                   }, {
                                       rectangle.size.w, borderLineWidth
                                   }
    };
    internal_render_rectangle(line); // Top
    set_rgb_colour((tRgb)RGB_BLACK);
    line            = (tRectangle){{
                                       rectangle.coord.x + rectangle.size.w - borderLineWidth, rectangle.coord.y
                                   }, {
                                       borderLineWidth, rectangle.size.h
                                   }
    };
    internal_render_rectangle(line); // Right

    return retRectangle;
}

tRectangle render_triangle(tArea area, tTriangle triangle) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        triangle.coord1    = scale_scroll_adjust_coord(triangle.coord1);
        triangle.coord2rel = scale_scroll_adjust_coord(triangle.coord2rel);
        triangle.coord3rel = scale_scroll_adjust_coord(triangle.coord3rel);
    }
    retRectangle       = (tRectangle){{
                                          0.0, 0.0
                                      }, {
                                          0.0, 0.0
                                      }
    };

    triangle.coord1    = global_scale_coord(triangle.coord1);
    triangle.coord2rel = global_scale_coord(triangle.coord2rel);
    triangle.coord3rel = global_scale_coord(triangle.coord3rel);

    batch_tri((float)triangle.coord1.x, (float)triangle.coord1.y,
              (float)(triangle.coord1.x + triangle.coord2rel.x), (float)(triangle.coord1.y + triangle.coord2rel.y),
              (float)(triangle.coord1.x + triangle.coord3rel.x), (float)(triangle.coord1.y + triangle.coord3rel.y),
              gCurrentColour);

    return retRectangle;
}

tRectangle render_circle_line(tArea area, tCoord coord, double radius, int segments, double thickness) {
    tRectangle   retRectangle   = {0};

    if (area == moduleArea) {
        coord     = scale_scroll_adjust_coord(coord);
        radius    = scale(radius);
        thickness = scale(thickness); // WAS OUTSIDE. Hmmmm
    }
    retRectangle = (tRectangle){{
                                    coord.x - radius, coord.y - radius
                                }, {
                                    radius *2.0, radius *2.0
                                }
    };

    coord        = global_scale_coord(coord);
    radius       = global_scale(radius);
    thickness    = global_scale(thickness);

    const double DEG_TO_RAD     = 2.0 * M_PI / (double)segments;
    double       half_thickness = thickness * 0.5;

    // GL_LINE_SMOOTH was enabled here too, and did nothing here too — see
    // internal_render_circle_line_part_angle().
    tStripBuild  strip          = {0};

    for (int i = 0; i <= segments; i++) {
        double angle = i * DEG_TO_RAD;
        double cos_a = cos(angle);
        double sin_a = sin(angle);

        // Compute inner and outer edge vertices
        strip_add(&strip, (float)(coord.x + cos_a * (radius - half_thickness)),
                  (float)(coord.y + sin_a * (radius - half_thickness)), gCurrentColour);

        strip_add(&strip, (float)(coord.x + cos_a * (radius + half_thickness)),
                  (float)(coord.y + sin_a * (radius + half_thickness)), gCurrentColour);
    }

    return retRectangle;
}

tRectangle render_circle_part(tArea area, tCoord coord, double radius, int segments, int startSeg, int numSegs) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        coord  = scale_scroll_adjust_coord(coord);
        radius = scale(radius);
    }
    retRectangle = (tRectangle){{
                                    coord.x - radius, coord.y - radius
                                }, {
                                    radius *2.0, radius *2.0
                                }
    };

    coord        = global_scale_coord(coord);
    radius       = global_scale(radius);

    internal_render_circle_part(coord, radius, segments, startSeg, numSegs);

    return retRectangle;
}

tRectangle render_circle_part_angle(tArea area, tCoord coord, double radius, double startAngle, double endAngle, int numSteps) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        coord  = scale_scroll_adjust_coord(coord);
        radius = scale(radius);
    }
    retRectangle = (tRectangle){{
                                    coord.x - radius, coord.y - radius
                                }, {
                                    radius *2.0, radius *2.0
                                }
    };

    coord        = global_scale_coord(coord);
    radius       = global_scale(radius);

    double     angle        = 0.0;
    double     x            = 0.0;
    double     y            = 0.0;
    int        i            = 0;

    tFanBuild  fan          = {0};

    fan_add(&fan, (float)coord.x, (float)coord.y, gCurrentColour);  // Center of the circle

    // Handle cases where the arc spans across 0°
    if (endAngle < startAngle) {
        endAngle += 360.0;     // Ensure interpolation works correctly across 0°
    }

    for (i = 0; i <= numSteps; i++) {
        // Interpolate between startAngle and endAngle
        double interpAngle = startAngle + (endAngle - startAngle) * (double)i / (double)numSteps;

        if (interpAngle >= 360.0) {
            interpAngle -= 360.0;
        }
        // Convert to radians and adjust so 0° is at the top
        angle = (interpAngle - 90.0) * (M_PI / 180.0);

        // Compute vertex position
        x     = coord.x + cos(angle) * radius;
        y     = coord.y + sin(angle) * radius;
        fan_add(&fan, (float)x, (float)y, gCurrentColour);
    }

    return retRectangle;
}

tRectangle render_radial_line(tArea area, tCoord coord, double radius, double angleDegrees, double thickness) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        coord     = scale_scroll_adjust_coord(coord);
        radius    = scale(radius);
        thickness = scale(thickness);
    }
    retRectangle = (tRectangle){{
                                    coord.x - radius, coord.y - radius
                                }, {
                                    radius *2.0, radius *2.0
                                }
    };

    coord        = global_scale_coord(coord);
    radius       = global_scale(radius);
    thickness    = global_scale(thickness);

    double     angle        = 0.0;
    double     x            = 0.0;
    double     y            = 0.0;

    // Adjust so 0° is at the top
    angle        = (angleDegrees - 90.0) * (M_PI / 180.0);

    // Calculate endpoint of the line
    x            = coord.x + cos(angle) * radius;
    y            = coord.y + sin(angle) * radius;

    // Draw the line
    //render_line(xPos, yPos, x, y, thickness);
    internal_render_line((tCoord){coord.x, coord.y}, (tCoord){x, y}, thickness);

    return retRectangle;
}

// ── Render backend seam ──────────────────────────────────────────────────────
// What this is, and why it is only four functions, is in utilsGraphics.h.

// The four below keep the application-facing names, because three apps and the plug-in call them.
// Each is the portable half of its operation — the batching rule — in front of the gfx_* call that
// does the platform work. Where there is no rule to apply, the wrapper is honestly empty.
void render_backend_init(void) {
    gfx_init();
}

void render_backend_set_surface(int width, int height) {
    render_backend_flush();    // queued vertices belong to the old projection
    gfx_set_surface(width, height);
}

void render_backend_clear(tRgb colour) {
    // A clear wipes the buffer, so anything queued has to be resolved against the OLD contents
    // first. In practice the batch is empty here — the frame loop clears before it draws — but
    // this is the general rule for the seam, not an assumption about one caller.
    render_backend_flush();
    gfx_clear(colour);
}

bool render_backend_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out) {
    // The frame is not finished until the batch is submitted — without this a SCREENSHOT taken
    // straight after a render would miss everything drawn since the last flush.
    render_backend_flush();
    return gfx_read_pixels_rgb(x, y, width, height, out);
}

// glColor3f/4f, as a plain variable. Nothing is submitted here — the colour is written into
// each vertex as it is appended, so a colour change between two draws no longer splits them
// into separate submissions the way a GL state change would have.
// Creating one has no bearing on queued geometry — nothing can be sampling a texture that does
// not exist yet — so this is a straight pass-through.
uint32_t render_backend_texture_create(int width, int height, const uint8_t * rgba, tTextureFilter filter) {
    return gfx_texture_alloc(width, height, rgba, filter);
}

void render_backend_texture_update(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba) {
    // A HAZARD THAT DID NOT EXIST BEFORE BATCHING: in immediate mode every draw was submitted
    // before the next statement ran, so an upload could never overtake one. Queued vertices now
    // outlive the call that appended them, and they were appended to sample what this texture
    // held THEN. Uploading underneath them would redraw already-issued geometry with new pixels.
    //
    // The rule lives HERE rather than in the backend, so that a new backend cannot forget it:
    // gfx_texture_write() is handed a texture nothing is waiting on.
    if (texture == gBatchTexture) {
        render_backend_flush();
    }
    gfx_texture_write(texture, x, y, width, height, rgba);
}

void render_backend_texture_destroy(uint32_t texture) {
    // Same reason as _update(), one step further: nothing queued may outlive its texture at all.
    // Owning the rule here rather than at each call site is why the glyph-atlas eviction and
    // free_textures() do not flush by hand.
    if ((texture != 0) && (texture == gBatchTexture)) {
        render_backend_flush();
        gBatchTexture = 0;
    }
    gfx_texture_free(texture);
}

void set_rgb_colour(tRgb rgb) {
    // glColor3f set alpha to 1.0; so does this.
    gCurrentColour = (tRgba){
        rgb.red, rgb.green, rgb.blue, 1.0
    };
}

void set_rgba_colour(tRgba rgba) {
    gCurrentColour = rgba;
}

tRgb contrasting_text_colour(tRgb bg) {
    // These three sit just under the 0.5 luminance line but read fine with
    // the black text draw_button() always used pre-luminance — pinned here
    // rather than following the general rule below, since flipping already-
    // fine buttons to white wasn't asked for (2026-07-13 user call):
    //   - SynthEdit's RGB_GREEN_ON (0.0, 0.8, 0.0) — the on/off toggle
    //     "on" state, explicitly meant to be left alone by that same call.
    //   - G2-Edit's RGB_GREEN_7 (0.0, 0.7, 0.0) — comms Online / Tx / Rx.
    //   - G2-Edit's RGB_RED_5 (0.7, 0.2, 0.2) — voice-count conflict.
    // Compared by literal value, not the macros, since not all of these
    // names are defined outside their own app's synthlibDefs.h branch.
    if (  ((bg.red == 0.0) && (bg.green == 0.8) && (bg.blue == 0.0))
       || ((bg.red == 0.0) && (bg.green == 0.7) && (bg.blue == 0.0))
       || ((bg.red == 0.7) && (bg.green == 0.2) && (bg.blue == 0.2))) {
        return (tRgb)RGB_BLACK;
    }
    double luminance = (0.299 * bg.red) + (0.587 * bg.green) + (0.114 * bg.blue);

    // >= not > : RGB_GREY_5 (0.5 exactly, e.g. render_page_tabs()'s pressed
    // state) sits right on the boundary and every caller of draw_button()
    // used to get fixed black text, so the midpoint keeps resolving to black
    // rather than flipping existing UI to white on a change nobody asked for.
    return (luminance >= 0.5) ? (tRgb)RGB_BLACK : (tRgb)RGB_WHITE;
}

tRectangle render_bezier_curve(tArea area, tCoord start, tCoord control, tCoord end, double thickness, int segments) {
    tRectangle   retRectangle   = {0};

    // The base colour the lighting is derived from. This used to be READ BACK OUT OF GL with
    // glGetFloatv(GL_CURRENT_COLOR) — the one place in the file that queried the graphics API
    // rather than driving it, and a pipeline stall to recover a value the caller had just set.
    // The colour is ours now, so it is simply read.
    double       baseR          = gCurrentColour.red;
    double       baseG          = gCurrentColour.green;
    double       baseB          = gCurrentColour.blue;
    double       baseA          = gCurrentColour.alpha;

    // Derive highlight (top-lit) and shadow colours from base
    // Light source assumed at top — normal pointing up (negative screen-y) = highlight
    const double highlightBoost = 0.20;
    const double shadowDrop     = 0.15;

    tRgb         highlight      = {
        fmin(1.0, baseR + highlightBoost),
        fmin(1.0, baseG + highlightBoost),
        fmin(1.0, baseB + highlightBoost)
    };
    tRgb         shadow         = {
        fmax(0.0, baseR - shadowDrop),
        fmax(0.0, baseG - shadowDrop),
        fmax(0.0, baseB - shadowDrop)
    };

    if (area == moduleArea) {
        start     = scale_scroll_adjust_coord(start);
        control   = scale_scroll_adjust_coord(control);
        end       = scale_scroll_adjust_coord(end);
        thickness = scale(thickness);
    }
    retRectangle = (tRectangle){{
                                    0.0, 0.0
                                }, {
                                    0.0, 0.0
                                }
    };

    start        = global_scale_coord(start);
    control      = global_scale_coord(control);
    end          = global_scale_coord(end);
    thickness    = global_scale(thickness);

    tRgba        lit   = {highlight.red, highlight.green, highlight.blue, baseA};
    tRgba        unlit = {shadow.red, shadow.green, shadow.blue, baseA};
    tStripBuild  strip = {0};

    for (int i = 0; i <= segments; i++) {
        double t   = (double)i / (double)segments;

        double x   = (1 - t) * (1 - t) * start.x + 2 * (1 - t) * t * control.x + t * t * end.x;
        double y   = (1 - t) * (1 - t) * start.y + 2 * (1 - t) * t * control.y + t * t * end.y;

        double tx  = 2 * (1 - t) * (control.x - start.x) + 2 * t * (end.x - control.x);
        double ty  = 2 * (1 - t) * (control.y - start.y) + 2 * t * (end.y - control.y);

        double len = sqrt(tx * tx + ty * ty);

        if (len > 0.0) {
            tx /= len;
            ty /= len;
        }
        // Normal perpendicular to tangent: (-ty, tx)
        // Vertex A: normal pointing in (-ty, tx) direction
        // Vertex B: normal pointing in (+ty, -tx) direction
        // In screen space, negative y = upward = towards light source
        double nx  = -ty * thickness * 0.5;
        double ny  = tx * thickness * 0.5;

        // The vertex whose normal has a more negative y component faces the light
        // ny for vertex A = tx * thickness * 0.5
        // ny for vertex B = -tx * thickness * 0.5
        // So if tx > 0, vertex A faces up (highlight); if tx < 0, vertex B faces up
        if (ny < 0.0) {
            // Vertex A faces upward — highlight
            strip_add(&strip, (float)(x + nx), (float)(y + ny), lit);
            strip_add(&strip, (float)(x - nx), (float)(y - ny), unlit);
        } else {
            // Vertex B faces upward — highlight
            strip_add(&strip, (float)(x + nx), (float)(y + ny), unlit);
            strip_add(&strip, (float)(x - nx), (float)(y - ny), lit);
        }
    }

    // The end caps take the base colour, which gCurrentColour still holds: the lighting above
    // is per-vertex, so unlike the glColor4f calls it replaced there is nothing to put back.
    internal_render_circle_part(start, thickness / 2.0, 10, 0, 10);
    internal_render_circle_part(end, thickness / 2.0, 10, 0, 10);

    return retRectangle;
}

// Draw the power button symbol
tRectangle draw_power_button(tArea area, tRectangle rectangle, bool active) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        rectangle = scale_scroll_adjust_rectangle(rectangle);
    }
    retRectangle  = rectangle;

    rectangle     = global_scale_rectangle(rectangle);

    if (active) {
        set_rgb_colour(gTheme.greenOn);         // Green when ON
    } else {
        set_rgb_colour(gTheme.backgroundGrey);  // Grey when OFF
    }
    internal_render_rectangle(rectangle);

    set_rgb_colour((tRgb)RGB_BLACK);
    tCoord     circleCentre = {rectangle.coord.x + (rectangle.size.w / 2.0), rectangle.coord.y + (rectangle.size.h / 2.0)};
    double     circleRadius = (rectangle.size.h / 2.0);
    circleRadius *= 0.75;

    internal_render_circle_line_part_angle(circleCentre, circleRadius, 30.0, 330.0, rectangle.size.w * 0.1, 10);
    internal_render_line((tCoord){circleCentre.x, rectangle.coord.y + (rectangle.size.h * 0.05)}, (tCoord){circleCentre.x, rectangle.coord.y + (rectangle.size.h * 0.05) + (rectangle.size.h * 0.5)}, rectangle.size.w * 0.1);

    return retRectangle;
}

// draw_button() draws a box DRAW_BUTTON_MARGIN pixels larger than the rect it is
// handed (padding around the text), anchored at the same top-left — so the button
// visually extends DRAW_BUTTON_MARGIN*2 further right and down than the input rect.
// draw_button() RETURNS that true drawn rect, and callers must hit-test against it,
// not the pre-expansion rect, or the bottom/right padding strip is visible-but-dead.
// draw_button_bounds() reports the same rect without drawing, for the many hit-test
// sites that recompute a button's rect separately from where it is drawn (the popup
// dialogs, panel buttons) rather than storing draw_button()'s return value.
tRectangle draw_button_bounds(tRectangle rectangle) {
    rectangle.size.w += (2 * DRAW_BUTTON_MARGIN);
    rectangle.size.h += (2 * DRAW_BUTTON_MARGIN);
    return rectangle;
}

// Exact comparison is right here rather than an epsilon: both sides originate as the same RGB_*
// macro, so "the caller passed one colour twice" is the only thing this needs to detect.
static bool rgb_same(tRgb a, tRgb b) {
    return (a.red == b.red) && (a.green == b.green) && (a.blue == b.blue);
}

tRectangle draw_button_split(tArea area, tRectangle rectangle, const char * text, tRgb topColour, tRgb bottomColour) {
    tRectangle retRectangle    = {0};
    double     borderLineWidth = 1.0;
    double     margin          = DRAW_BUTTON_MARGIN;
    tRectangle textRectangle   = rectangle;

    rectangle.size.w       = rectangle.size.w + (2 * margin);
    rectangle.size.h       = rectangle.size.h + (2 * margin);
    textRectangle.coord.x += margin;
    textRectangle.coord.y += margin;

    if (area == moduleArea) {
        rectangle     = scale_scroll_adjust_rectangle(rectangle);
        textRectangle = scale_scroll_adjust_rectangle(textRectangle);
    }
    retRectangle           = rectangle;

    rectangle              = global_scale_rectangle(rectangle);
    textRectangle          = global_scale_rectangle(textRectangle);

    //if (isPressed == true) {
    //    set_rgb_colour((tRgb)RGB_GREY_7);
    //}
    set_rgb_colour(topColour);
    internal_render_rectangle(rectangle);

    // The whole face is painted in the top colour first and the lower half painted over it, rather
    // than two half-height fills: at these sizes a button is only a dozen-odd pixels tall, and two
    // independently rounded rectangles can leave a background-coloured hairline along the seam.
    if (!rgb_same(topColour, bottomColour)) {
        double     halfHeight = rectangle.size.h / 2.0;
        tRectangle bottomHalf = {{
                                     rectangle.coord.x, rectangle.coord.y + halfHeight
                                 },{
                                     rectangle.size.w, rectangle.size.h - halfHeight
                                 }};

        set_rgb_colour(bottomColour);
        internal_render_rectangle(bottomHalf);
    }
    set_rgb_colour((tRgb)RGB_BLACK);

    tRectangle line = {0};
    line = (tRectangle){{
                            rectangle.coord.x, rectangle.coord.y + rectangle.size.h - borderLineWidth
                        }, {
                            rectangle.size.w, borderLineWidth
                        }
    };
    internal_render_rectangle(line); // Bottom
    line = (tRectangle){{
                            rectangle.coord.x, rectangle.coord.y
                        }, {
                            borderLineWidth, rectangle.size.h
                        }
    };
    internal_render_rectangle(line); // Left
    line = (tRectangle){{
                            rectangle.coord.x, rectangle.coord.y
                        }, {
                            rectangle.size.w, borderLineWidth
                        }
    };
    internal_render_rectangle(line); // Top
    line = (tRectangle){{
                            rectangle.coord.x + rectangle.size.w - borderLineWidth, rectangle.coord.y
                        }, {
                            borderLineWidth, rectangle.size.h
                        }
    };
    internal_render_rectangle(line); // Right

    // Contrast is taken from the TOP colour. The glyph crosses the seam, so no single choice is
    // ideal, but the two colours a split button is ever given are both light enough to want the same
    // black text — and if that ever stops being true, the top half is where the label's mass sits.
    set_rgb_colour(contrasting_text_colour(topColour));
    internal_render_text(textRectangle, text);

    return retRectangle;
}

tRectangle draw_button(tArea area, tRectangle rectangle, const char * text, tRgb backgroundColour) {
    return draw_button_split(area, rectangle, text, backgroundColour, backgroundColour);
}

tRectangle draw_slider(tArea area, tRectangle rectangle, uint32_t value, uint32_t range, uint32_t morphRange, tRgb colour) {
    tRectangle retRectangle    = {0};
    double     borderLineWidth = 1.0;
    double     trackH          = 0.0;
    double     fillHeight      = 0.0;
    double     fillY           = 0.0;
    tRectangle line            = {0};

    if (area == moduleArea) {
        rectangle = scale_scroll_adjust_rectangle(rectangle);
    }
    retRectangle = rectangle;
    rectangle    = global_scale_rectangle(rectangle);

    trackH       = rectangle.size.h;
    fillHeight   = (range > 1) ? ((double)value / (double)(range - 1)) * trackH : 0.0;
    fillY        = rectangle.coord.y + trackH - fillHeight;

    set_rgb_colour(gTheme.backgroundGrey);
    internal_render_rectangle(rectangle);

    if (morphRange == 0) {
        set_rgb_colour(colour);
    } else {
        set_rgb_colour(gTheme.orange2);
    }

    if (fillHeight > 0.0) {
        internal_render_rectangle((tRectangle){{rectangle.coord.x, fillY}, {rectangle.size.w, fillHeight}});
    }

    if (morphRange != 0) {
        int32_t signedMorphRange = (morphRange < 128) ? (int32_t)morphRange : (int32_t)morphRange - 256;
        int32_t morphPos         = (int32_t)value + signedMorphRange;

        if (morphPos < 0) {
            morphPos = 0;
        } else if (morphPos > (int32_t)(range - 1)) {
            morphPos = (int32_t)(range - 1);
        }
        double  morphHeight      = ((double)morphPos / (double)(range - 1)) * trackH;
        double  morphY           = rectangle.coord.y + trackH - morphHeight;
        double  loY              = (fillY < morphY) ? fillY : morphY;
        double  hiY              = (fillY > morphY) ? fillY : morphY;

        set_rgb_colour(gTheme.orange1);
        internal_render_rectangle((tRectangle){{rectangle.coord.x, loY}, {rectangle.size.w, hiY - loY}});
    }

    if (fillHeight > 0.0) {
        set_rgb_colour((tRgb)RGB_BLACK);
        internal_render_rectangle((tRectangle){{rectangle.coord.x, fillY}, {rectangle.size.w, borderLineWidth}});
    }
    line = (tRectangle){{
                            rectangle.coord.x, rectangle.coord.y + trackH - borderLineWidth
                        }, {
                            rectangle.size.w, borderLineWidth
                        }
    };
    internal_render_rectangle(line); // Bottom
    line = (tRectangle){{
                            rectangle.coord.x, rectangle.coord.y
                        }, {
                            borderLineWidth, trackH
                        }
    };
    internal_render_rectangle(line); // Left
    line = (tRectangle){{
                            rectangle.coord.x, rectangle.coord.y
                        }, {
                            rectangle.size.w, borderLineWidth
                        }
    };
    internal_render_rectangle(line); // Top
    line = (tRectangle){{
                            rectangle.coord.x + rectangle.size.w - borderLineWidth, rectangle.coord.y
                        }, {
                            borderLineWidth, trackH
                        }
    };
    internal_render_rectangle(line); // Right

    return retRectangle;
}

tRectangle render_text(tArea area, tRectangle rectangle, const char * text) {
    tRectangle retRectangle = {0};

    if (area == moduleArea) {
        rectangle = scale_scroll_adjust_rectangle(rectangle);
    }
    retRectangle = rectangle;

    rectangle    = global_scale_rectangle(rectangle);

    internal_render_text(rectangle, text);

    return retRectangle;
}

// One rasterized glyph, held between the FreeType pass and the atlas upload.
typedef struct {
    unsigned char * alpha;      // 8-bit coverage, width * height, NULL for a blank glyph (e.g. space)
    int             width;
    int             height;
    int             atlasX;     // placement, filled in by layout_glyph_atlas()
    int             atlasY;
    double          advanceX;
    int             offsetX;
    int             offsetY;
    bool            valid;
} tGlyphRaster;

static void free_glyph_rasters(tGlyphRaster * raster) {
    for (int charCode = 0; charCode < MAX_GLYPH_CHAR; charCode++) {
        free(raster[charCode].alpha);
        raster[charCode].alpha = NULL;
    }
}

// Row-packs the rasterized glyphs, choosing the atlas that wastes the least memory. Both
// dimensions are powers of two (this is a legacy GL 2.1 context, so NPOT support isn't assumed).
// Returns false if the glyphs won't fit any supported atlas.
static bool layout_glyph_atlas(tGlyphRaster * raster, int * outWidth, int * outHeight) {
    const int padding    = GLYPH_ATLAS_PADDING;
    int       bestWidth  = 0;
    int       bestHeight = 0;

    for (int width = 512; width <= 8192; width *= 2) {
        int  x         = padding;
        int  y         = padding;
        int  rowHeight = 0;
        bool fits      = true;

        for (int charCode = 0; charCode < MAX_GLYPH_CHAR; charCode++) {
            if (!raster[charCode].valid || (raster[charCode].width == 0)) {
                continue;
            }

            if ((raster[charCode].width + (padding * 2)) > width) {
                fits = false;   // a single glyph is wider than this candidate atlas
                break;
            }

            if ((x + raster[charCode].width + padding) > width) {
                x         = padding;            // wrap to the next row
                y        += rowHeight + padding;
                rowHeight = 0;
            }
            raster[charCode].atlasX = x;
            raster[charCode].atlasY = y;
            x                      += raster[charCode].width + padding;

            if (raster[charCode].height > rowHeight) {
                rowHeight = raster[charCode].height;
            }
        }

        if (!fits) {
            continue;
        }
        int height = 1;

        while (height < (y + rowHeight + padding)) {
            height *= 2;
        }

        if (height > 8192) {
            continue;
        }

        if ((bestWidth == 0) || ((width * height) < (bestWidth * bestHeight))) {
            bestWidth  = width;
            bestHeight = height;
        }
    }

    if (bestWidth == 0) {
        return false;
    }
    // Re-run the packing for the chosen width, since the loop above left the placements
    // from whichever candidate it tried last.
    int x         = padding;
    int y         = padding;
    int rowHeight = 0;

    for (int charCode = 0; charCode < MAX_GLYPH_CHAR; charCode++) {
        if (!raster[charCode].valid || (raster[charCode].width == 0)) {
            continue;
        }

        if ((x + raster[charCode].width + padding) > bestWidth) {
            x         = padding;
            y        += rowHeight + padding;
            rowHeight = 0;
        }
        raster[charCode].atlasX = x;
        raster[charCode].atlasY = y;
        x                      += raster[charCode].width + padding;

        if (raster[charCode].height > rowHeight) {
            rowHeight = raster[charCode].height;
        }
    }

    *outWidth  = bestWidth;
    *outHeight = bestHeight;
    return true;
}

// Rasterizes the font at fontSize pixels into a self-contained atlas. Nothing global is touched
// until it has fully succeeded, so a failure here leaves existing text rendering untouched.
// When outAtlas is NULL only the metrics are produced (used for the canonical layout metrics,
// which never need a texture).
static bool build_glyph_atlas(const char * fontPath, double fontSize, tSizedAtlas * outAtlas, tTextureFilter filter, GlyphInfo * outMetrics, double * outAscent, double * outDescent) {
    FT_Library   ftLibrary              = {0};
    FT_Face      face                   = {0};
    tGlyphRaster raster[MAX_GLYPH_CHAR] = {};
    double       maxAscent              = 0.0;
    double       maxDescent             = 0.0;

    if (fontPath == NULL) {
        return false;
    }

    if (FT_Init_FreeType(&ftLibrary)) {
        LOG_DEBUG("Failed to initialize FreeType.\n");
        return false;
    }

    if (FT_New_Face(ftLibrary, fontPath, 0, &face)) {
        LOG_DEBUG("Failed to load font: %s\n", fontPath);
        FT_Done_FreeType(ftLibrary);
        return false;
    }

    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)fontSize)) {
        LOG_DEBUG("Failed to set font size.\n");
        FT_Done_Face(face);
        FT_Done_FreeType(ftLibrary);
        return false;
    }

    for (int charCode = 32; charCode < MAX_GLYPH_CHAR; charCode++) {
        FT_UInt     glyphIndex   = FT_Get_Char_Index(face, charCode);

        if (glyphIndex == 0) {
            LOG_DEBUG("Glyph index not found for character %d %c\n", charCode, charCode);
            continue;
        }

        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_TARGET_LIGHT)) {
            LOG_DEBUG("Failed to load glyph for character %c\n", charCode);
            continue;
        }

        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
            LOG_DEBUG("Failed to render glyph for character %c\n", charCode);
            continue;
        }
        // Track the highest ascent and lowest descent
        double      glyphAscent  = face->glyph->bitmap_top;

        if (glyphAscent < 0.0) {
            glyphAscent = 0.0;
        }

        if (glyphAscent > maxAscent) {
            maxAscent = glyphAscent;
        }
        double      glyphDescent = (double)face->glyph->bitmap.rows - (double)face->glyph->bitmap_top;

        if (glyphDescent < 0.0) {
            glyphDescent = 0.0;
        }

        if (glyphDescent > maxDescent) {
            maxDescent = glyphDescent;
        }
        FT_Bitmap * bitmap       = &face->glyph->bitmap;

        raster[charCode].valid    = true;
        raster[charCode].width    = (int)bitmap->width;
        raster[charCode].height   = (int)bitmap->rows;
        raster[charCode].offsetX  = face->glyph->bitmap_left;
        raster[charCode].offsetY  = face->glyph->bitmap_top;
        // The extra spacing is expressed in atlas pixels, so it has to scale with the
        // rasterization size — otherwise letter spacing would shift between sizes.
        raster[charCode].advanceX = ((double)face->glyph->advance.x / 64.0) + (fontSize / GLYPH_REFERENCE_PX);

        if ((bitmap->width > 0) && (bitmap->rows > 0)) {
            raster[charCode].alpha = (unsigned char *)malloc((size_t)bitmap->width * (size_t)bitmap->rows);

            if (raster[charCode].alpha == NULL) {
                LOG_ERROR("Out of memory rasterizing glyph %c\n", charCode);
                free_glyph_rasters(raster);
                FT_Done_Face(face);
                FT_Done_FreeType(ftLibrary);
                return false;
            }

            for (unsigned int row = 0; row < bitmap->rows; row++) {
                memcpy(&raster[charCode].alpha[row * bitmap->width], &bitmap->buffer[row * bitmap->pitch], bitmap->width);
            }
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ftLibrary);

    if ((maxAscent + maxDescent) <= 0.0) {
        LOG_ERROR("Font %s produced no usable glyphs\n", fontPath);
        free_glyph_rasters(raster);
        return false;
    }

    if (outAscent != NULL) {
        *outAscent = maxAscent;
    }

    if (outDescent != NULL) {
        *outDescent = maxDescent;
    }

    if (outAtlas == NULL) {
        // Metrics-only build: fill the caller's metrics table straight from the rasters.
        for (int charCode = 0; charCode < MAX_GLYPH_CHAR; charCode++) {
            if (!raster[charCode].valid) {
                continue;
            }
            outMetrics[charCode].advance_x = raster[charCode].advanceX;
            outMetrics[charCode].width     = raster[charCode].width;
            outMetrics[charCode].height    = raster[charCode].height;
            outMetrics[charCode].offset_x  = raster[charCode].offsetX;
            outMetrics[charCode].offset_y  = raster[charCode].offsetY;
        }

        free_glyph_rasters(raster);
        return true;
    }
    int             newWidth    = 0;
    int             newHeight   = 0;

    if (!layout_glyph_atlas(raster, &newWidth, &newHeight)) {
        LOG_ERROR("Glyphs at %.0fpx do not fit a supported atlas\n", fontSize);
        free_glyph_rasters(raster);
        return false;
    }
    // Compose the whole atlas on the CPU and upload it in one go. Every texel is initialised
    // (white, fully transparent) so that bilinear filtering at glyph edges blends towards
    // transparent white rather than towards undefined texture memory.
    unsigned char * atlasBuffer = (unsigned char *)malloc((size_t)newWidth * (size_t)newHeight * 4);

    if (atlasBuffer == NULL) {
        LOG_ERROR("Out of memory allocating %dx%d glyph atlas\n", newWidth, newHeight);
        free_glyph_rasters(raster);
        return false;
    }
    memset(atlasBuffer, 0xFF, (size_t)newWidth * (size_t)newHeight * 4);

    for (size_t texel = 0; texel < ((size_t)newWidth * (size_t)newHeight); texel++) {
        atlasBuffer[(texel * 4) + 3] = 0;
    }

    for (int charCode = 0; charCode < MAX_GLYPH_CHAR; charCode++) {
        if (!raster[charCode].valid) {
            continue;
        }

        for (int row = 0; row < raster[charCode].height; row++) {
            for (int col = 0; col < raster[charCode].width; col++) {
                size_t dest = (((size_t)(raster[charCode].atlasY + row) * (size_t)newWidth) + (size_t)(raster[charCode].atlasX + col)) * 4;
                atlasBuffer[dest + 3] = raster[charCode].alpha[(row * raster[charCode].width) + col];
            }
        }

        outAtlas->info[charCode].u1        = (double)raster[charCode].atlasX / (double)newWidth;
        outAtlas->info[charCode].v1        = (double)raster[charCode].atlasY / (double)newHeight;
        outAtlas->info[charCode].u2        = (double)(raster[charCode].atlasX + raster[charCode].width) / (double)newWidth;
        outAtlas->info[charCode].v2        = (double)(raster[charCode].atlasY + raster[charCode].height) / (double)newHeight;
        outAtlas->info[charCode].advance_x = raster[charCode].advanceX;
        outAtlas->info[charCode].width     = raster[charCode].width;
        outAtlas->info[charCode].height    = raster[charCode].height;
        outAtlas->info[charCode].offset_x  = raster[charCode].offsetX;
        outAtlas->info[charCode].offset_y  = raster[charCode].offsetY;
    }

    free_glyph_rasters(raster);

    if (outAtlas->texture != 0) {
        render_backend_texture_destroy(outAtlas->texture);
        outAtlas->texture = 0;
    }
    outAtlas->texture = render_backend_texture_create(newWidth, newHeight, atlasBuffer, filter);

    free(atlasBuffer);

    outAtlas->width   = newWidth;
    outAtlas->height  = newHeight;

    LOG_DEBUG("Glyph atlas built at %.0fpx (%dx%d)\n", fontSize, newWidth, newHeight);
    return true;
}

// Returns the atlas rasterized for text of this drawn pixel height, building it on first use and
// evicting the least recently used entry when the cache is full. In practice an app settles on
// two or three sizes, so this builds a handful of times at startup and then never again until the
// window is resized or zoomed.
static tSizedAtlas * atlas_for_height(int pixelHeight) {
    if ((gFontPath == NULL) || (gCanonEmPx <= 0.0)) {
        return NULL;
    }

    if (pixelHeight < GLYPH_ATLAS_MIN_PX) {
        pixelHeight = GLYPH_ATLAS_MIN_PX;
    }

    if (pixelHeight > GLYPH_ATLAS_MAX_PX) {
        pixelHeight = GLYPH_ATLAS_MAX_PX;
    }
    tSizedAtlas * victim      = &gAtlasCache[0];

    for (int i = 0; i < GLYPH_ATLAS_CACHE_SIZE; i++) {
        if (gAtlasCache[i].pixelHeight == pixelHeight) {
            gAtlasCache[i].lastUsed = ++gAtlasUseCounter;
            return &gAtlasCache[i];
        }

        if ((gAtlasCache[i].pixelHeight == 0) || (gAtlasCache[i].lastUsed < victim->lastUsed)) {
            victim = &gAtlasCache[i];
        }
    }

    // Small text is rasterized at a multiple of its drawn size and scaled down when it is drawn;
    // everything else keeps the one-texel-per-pixel blit. See GLYPH_SUPERSAMPLE_BELOW_PX.
    int           supersample = (pixelHeight < GLYPH_SUPERSAMPLE_BELOW_PX) ? GLYPH_SUPERSAMPLE_FACTOR : 1;

    // FreeType is asked for a pixel size, not an em height, so convert through the ratio the
    // reference rasterization actually produced rather than assuming a relationship between them.
    double        fontSize    = ((double)(pixelHeight * supersample) * GLYPH_REFERENCE_PX) / gCanonEmPx;

    if (fontSize < 1.0) {
        fontSize = 1.0;
    }
    tSizedAtlas   built       = {};

    built.texture     = victim->texture; // reuse the evicted handle; build_glyph_atlas destroys it

    if (!build_glyph_atlas(gFontPath, fontSize, &built,
                           (supersample > 1) ? eTextureLinear : eTextureNearest,
                           NULL, NULL, NULL)) {
        return NULL;
    }
    built.pixelHeight = pixelHeight;
    built.supersample = supersample;
    built.lastUsed    = ++gAtlasUseCounter;
    *victim           = built;
    return victim;
}

// Establishes the canonical layout metrics. fontSize is only the size those metrics are measured
// at — it no longer fixes how text is rasterized, since widths are normalised by the em height
// and the drawn glyphs come from an atlas built per drawn size. Validating the font here keeps
// the existing "try each path until one loads" behaviour in the embedding apps working.
bool preload_glyph_textures(const char * fontPath, double fontSize) {
    GlyphInfo canonInfo[MAX_GLYPH_CHAR] = {0};
    double    maxAscent                 = 0.0;
    double    maxDescent                = 0.0;

    (void)fontSize;

    if (fontPath == NULL) {
        return false;
    }

    if (!build_glyph_atlas(fontPath, GLYPH_REFERENCE_PX, NULL, eTextureNearest, canonInfo, &maxAscent, &maxDescent)) {
        return false;
    }
    free_textures();    // drop any atlases built for a previously loaded font

    memcpy(gCanonInfo, canonInfo, sizeof(gCanonInfo));
    gMaxAscent     = maxAscent;
    gMaxDescent    = maxDescent;
    gMetricsHeight = maxAscent + maxDescent;
    gCanonEmPx     = maxAscent + maxDescent;
    gFontPath      = strdup(fontPath);   // retained so atlases can be built per drawn size
    clear_text_width_cache();
    return true;
}

double get_char_width(char ch, double targetHeight) {
    // char is signed here, so anything above 127 would index the glyph table negatively
    unsigned char character = (unsigned char)ch;

    if (character >= MAX_GLYPH_CHAR) {
        character = '?';
    }
    double        width     = gCanonInfo[character].advance_x;

    if ((gMaxAscent + gMaxDescent) <= 0.0) {
        return 0.0;
    }
    double        scaleVal  = targetHeight / (gMaxAscent + gMaxDescent);
    return width * scaleVal;
}

double get_text_width(const char * text, double targetHeight, tCache useCache) {
    if (text == NULL) {
        return 0.0;
    }

    if (useCache == eCache) {
        for (int i = 0; i < gTextWidthCacheCount; i++) {
            if ((gTextWidthCache[i].text == text) && (gTextWidthCache[i].height == targetHeight)) {
                return gTextWidthCache[i].width;
            }
        }
    }
    double       width = 0.0;
    const char * ch    = text;

    // Walked with next_glyph(), exactly as render_text() walks it. If these two ever disagree about
    // how many glyphs a string is, every centred label and every width-fitted box is wrong by the
    // difference — so the decode lives in one function and both callers use it.
    while (*ch) {
        tGlyphStep step = next_glyph(ch);

        width += get_char_width((char)step.glyph, targetHeight);
        ch    += step.bytes;
    }

    if ((useCache == eCache) && (gTextWidthCacheCount < TEXT_WIDTH_CACHE_SIZE)) {
        gTextWidthCache[gTextWidthCacheCount].text   = text;
        gTextWidthCache[gTextWidthCacheCount].height = targetHeight;
        gTextWidthCache[gTextWidthCacheCount].width  = width;
        gTextWidthCacheCount++;
    }
    return width;
}

double largest_text_width(int numItems, const char ** text, double targetHeight, tCache useCache) {
    if (text == NULL) {
        return 0.0;
    }
    int    i       = 0;
    double size    = 0;
    double maxSize = 0;

    for (i = 0; i < numItems; i++) {
        size = get_text_width(text[i], targetHeight, useCache);

        if (size > maxSize) {
            maxSize = size;
        }
    }

    return maxSize;
}

void free_textures(void) {
    for (int i = 0; i < GLYPH_ATLAS_CACHE_SIZE; i++) {
        if (gAtlasCache[i].texture != 0) {
            render_backend_texture_destroy(gAtlasCache[i].texture);
        }
        gAtlasCache[i] = (tSizedAtlas){};
    }

    free(gFontPath);
    gFontPath = NULL;
    clear_text_width_cache();
}

// Converts normalized value [0,127] back to an angle (-135° to 135°)
double clamp_scroll_bar(double value, double max_value) {
    max_value /= gGlobalGuiScale;

    double half_length = SCROLLBAR_LENGTH / 2.0;
    double min_limit   = half_length + SCROLLBAR_MARGIN;
    double max_limit   = max_value - (half_length + SCROLLBAR_MARGIN);

    if (value < min_limit) {
        return min_limit;
    }

    if (value > max_limit) {
        return max_limit;
    }
    return value;
}

double get_scroll_bar_percent(double scrollBar, double renderSize) {
    double half_length = SCROLLBAR_LENGTH / 2.0;
    double low         = half_length + SCROLLBAR_MARGIN;
    double high        = renderSize - (half_length + SCROLLBAR_MARGIN);

    return ((scrollBar - low) / (high - low)) * 100.0;
}

double set_scroll_bar_percent(double percent, double renderSize) {
    double half_length = SCROLLBAR_LENGTH / 2.0;
    double low         = half_length + SCROLLBAR_MARGIN;
    double high        = renderSize - (half_length + SCROLLBAR_MARGIN);

    // Convert percentage back to actual position on the scrollbar
    return low + (percent / 100.0) * (high - low);
}

// Both act on the CURRENT pane, so the scrollbar drag that calls them scrolls whichever pane is
// selected — which, once the split bar exists, is the one the user is working in.
void set_x_scroll_percent(double percent) {
    gModulePane[gCurrentModulePane].xScrollPercent = percent;
}

void set_y_scroll_percent(double percent) {
    gModulePane[gCurrentModulePane].yScrollPercent = percent;
}

// Read back the current pane's scroll, so a caller drawing that pane's own scrollbar can put the
// thumb where the pane actually is rather than tracking it separately and drifting.
double get_x_scroll_percent(void) {
    return gModulePane[gCurrentModulePane].xScrollPercent;
}

double get_y_scroll_percent(void) {
    return gModulePane[gCurrentModulePane].yScrollPercent;
}

// ── List scrollbar (bankBrowser.cpp, fileBrowser.cpp, and similar) ──────────────────────────────
//
// Drag state is file-static rather than passed in/out by each caller — safe only because the
// callers are mutually exclusive modals, so at most one list scrollbar can ever be mid-drag.

static bool       sListScrollbarDragging    = false;
static double     sListScrollbarGrabOffset  = 0.0; // distance from the thumb's own top edge to the
                                                   // initial click Y, so the thumb doesn't jump to
                                                   // re-centre under the cursor on grab
static tRectangle sListScrollbarListRect    = {0};
static int32_t    sListScrollbarTotalRows   = 0;
static int32_t    sListScrollbarVisibleRows = 0;

// Thumb height, shared by the rect builder and the drag handler. These MUST agree: the drag maps the
// cursor onto (trackH - thumbH) of travel, so if the two ever computed different heights the thumb
// would drift away from the cursor as it was dragged. Hence one helper rather than two expressions.
static double list_scrollbar_thumb_height(double trackH, int32_t totalRows, int32_t visibleRows) {
    double proportional = (trackH * (double)visibleRows) / (double)totalRows;

    return fmin(trackH, fmax(proportional, LIST_SCROLLBAR_MIN_THUMB));
}

tRectangle list_scrollbar_thumb_rect(tRectangle listRect, int32_t totalRows, int32_t visibleRows, double scrollOffset) {
    double trackX    = listRect.coord.x + listRect.size.w - LIST_SCROLLBAR_WIDTH;
    double trackH    = listRect.size.h;
    double thumbH    = list_scrollbar_thumb_height(trackH, totalRows, visibleRows);
    double maxScroll = (double)(totalRows - visibleRows);
    double travel    = trackH - thumbH;
    double thumbY    = listRect.coord.y + ((maxScroll > 0.0) ? (travel * (scrollOffset / maxScroll)) : 0.0);

    return (tRectangle){{
                            trackX, thumbY
                        }, {
                            LIST_SCROLLBAR_WIDTH, thumbH
                        }
    };
}

void render_list_scrollbar(tRectangle listRect, int32_t totalRows, int32_t visibleRows, double scrollOffset) {
    if (totalRows <= visibleRows) {
        return; // Nothing to scroll - no scrollbar shown, matching native list behaviour.
    }
    double     trackX    = listRect.coord.x + listRect.size.w - LIST_SCROLLBAR_WIDTH;
    tRectangle trackRect = {{trackX, listRect.coord.y}, {LIST_SCROLLBAR_WIDTH, listRect.size.h}};

    set_rgb_colour((tRgb)RGB_GREY_3);
    render_rectangle(mainArea, trackRect);

    set_rgb_colour((tRgb)RGB_GREY_7);
    render_rectangle(mainArea, list_scrollbar_thumb_rect(listRect, totalRows, visibleRows, scrollOffset));
}

bool list_scrollbar_mouse_down(tRectangle listRect, int32_t totalRows, int32_t visibleRows, double scrollOffset, tCoord coord) {
    if (totalRows <= visibleRows) {
        return false;
    }
    tRectangle thumbRect = list_scrollbar_thumb_rect(listRect, totalRows, visibleRows, scrollOffset);

    if (!within_rectangle(coord, thumbRect)) {
        return false;
    }
    sListScrollbarDragging    = true;
    sListScrollbarGrabOffset  = coord.y - thumbRect.coord.y;
    sListScrollbarListRect    = listRect;
    sListScrollbarTotalRows   = totalRows;
    sListScrollbarVisibleRows = visibleRows;
    return true;
}

bool list_scrollbar_dragging(void) {
    return sListScrollbarDragging;
}

double list_scrollbar_mouse_drag(tCoord coord) {
    double trackH    = sListScrollbarListRect.size.h;
    double thumbH    = list_scrollbar_thumb_height(trackH, sListScrollbarTotalRows, sListScrollbarVisibleRows);
    double travel    = trackH - thumbH;
    double maxScroll = (double)(sListScrollbarTotalRows - sListScrollbarVisibleRows);

    if (travel <= 0.0) {
        return 0.0;
    }
    double thumbY    = (coord.y - sListScrollbarGrabOffset) - sListScrollbarListRect.coord.y;
    double percent   = fmax(0.0, fmin(1.0, thumbY / travel));

    return percent * maxScroll;
}

void list_scrollbar_mouse_up(void) {
    sListScrollbarDragging = false;
}

void set_zoom_factor(double newZoom, tCoord mouseCoord) {
    if (newZoom < 0.25) {
        newZoom = 0.25;
    }

    if (newZoom > 2.0) {
        newZoom = 2.0;
    }
    tRectangle area        = module_area();
    double     totalWidth  = (double)(MAX_COLUMNS + 1) * MODULE_X_SPAN;
    double     totalHeight = (double)((MAX_ROWS + 1) + (MAX_ROWS_MODULE - 1)) * MODULE_Y_SPAN;

    // Mouse position relative to module area origin
    double     mouseRelX   = mouseCoord.x - area.coord.x;
    double     mouseRelY   = mouseCoord.y - area.coord.y;

    // Clamp to module area bounds
    if (mouseRelX < 0.0) {
        mouseRelX = 0.0;
    }

    if (mouseRelY < 0.0) {
        mouseRelY = 0.0;
    }

    if (mouseRelX > area.size.w) {
        mouseRelX = area.size.w;
    }

    if (mouseRelY > area.size.h) {
        mouseRelY = area.size.h;
    }
    // Content point under cursor in unscaled coords
    double     oldScrollX  = calc_scroll_x();
    double     oldScrollY  = calc_scroll_y();
    double     cx          = (oldScrollX + mouseRelX) / gZoomFactor;
    double     cy          = (oldScrollY + mouseRelY) / gZoomFactor;

    // Apply new zoom
    gZoomFactor = newZoom;

    // Scroll to keep cx/cy under the cursor
    double     newScrollX  = cx * gZoomFactor - mouseRelX;
    double     newScrollY  = cy * gZoomFactor - mouseRelY;

    // Max scroll at new zoom
    double     maxScrollX  = gZoomFactor * totalWidth - area.size.w;
    double     maxScrollY  = gZoomFactor * totalHeight - area.size.h;

    // Clamp
    if (newScrollX < 0.0) {
        newScrollX = 0.0;
    }

    if (newScrollY < 0.0) {
        newScrollY = 0.0;
    }

    if (maxScrollX > 0.0 && newScrollX > maxScrollX) {
        newScrollX = maxScrollX;
    }

    if (maxScrollY > 0.0 && newScrollY > maxScrollY) {
        newScrollY = maxScrollY;
    }
    // Convert back to percent. The zoom itself is global — both panes always draw at one scale —
    // but the scroll it lands on belongs to the pane being zoomed, since keeping the point under
    // the cursor fixed is a per-pane calculation.
    gModulePane[gCurrentModulePane].xScrollPercent = (maxScrollX > 0.0) ? (newScrollX / maxScrollX) * 100.0 : 0.0;
    gModulePane[gCurrentModulePane].yScrollPercent = (maxScrollY > 0.0) ? (newScrollY / maxScrollY) * 100.0 : 0.0;

    // Sync scrollbar thumb positions to match new percent
    double renderWidth  = get_render_width() / gGlobalGuiScale;
    double renderHeight = get_render_height() / gGlobalGuiScale;

    gScrollState.xBar                              = set_scroll_bar_percent(gModulePane[gCurrentModulePane].xScrollPercent, renderWidth);
    gScrollState.yBar                              = set_scroll_bar_percent(gModulePane[gCurrentModulePane].yScrollPercent, renderHeight);
}

double get_zoom_factor(void) {
    return gZoomFactor;
}

void set_render_width(int width) {
    gRenderWidth = width;
}

void set_render_height(int height) {
    gRenderHeight = height;
}

int get_render_width(void) {
    return gRenderWidth;
}

int get_render_height(void) {
    return gRenderHeight;
}

double scale_from_percent(double val) {
    return (val * MODULE_WIDTH) / 100;
}

tRectangle rectangle_scale_from_percent(tRectangle rectangle) {
    // Scaling is done by module width, so Y percentage may be greater than 100% in some cases

    rectangle.coord.x = scale_from_percent(rectangle.coord.x);
    rectangle.coord.y = scale_from_percent(rectangle.coord.y);
    rectangle.size.w  = scale_from_percent(rectangle.size.w);
    rectangle.size.h  = scale_from_percent(rectangle.size.h);

    return rectangle;
}


#define X_POS_FROM_PERCENT(x)    ((MODULE_WIDTH * (double)x) / 100.0)
#define Y_POS_FROM_PERCENT(x)    ((MODULE_HEIGHT * (double)x) / 100.0)

tRectangle render_dial(tArea area, tRectangle rectangle, uint32_t value, uint32_t range, uint32_t morphRange, tRgb colour) {
    double  angle            = 0.0;
    double  morphAngle       = 0.0;
    double  radius           = 0.0;
    double  x                = 0;
    double  y                = 0;
    int32_t signedMorphRange = 0;
    int32_t signedValue      = 0;
    int32_t morphPos         = 0;

    radius = (rectangle.size.w / 2.0);
    x      = rectangle.coord.x + radius;
    y      = rectangle.coord.y + radius;
    angle  = value_to_angle(value, range);

    if (morphRange == 0) {
        set_rgb_colour(colour);
    } else {
        set_rgb_colour(gTheme.orange2);
    }
    render_circle_part_angle(area, (tCoord){x, y}, radius, 0.0, 360.0, 25);

    if (morphRange != 0) {
        signedValue = (int32_t)value;

        if (morphRange < 128) {
            signedMorphRange = (int32_t)morphRange;
        } else {
            signedMorphRange = (int32_t)morphRange - 256;
        }
        morphPos    = signedValue + signedMorphRange;

        if (morphPos < 0) {
            morphPos = 0;
        } else if (morphPos > (int32_t)(range - 1)) {
            morphPos = range - 1;
        }
        morphAngle  = value_to_angle((uint32_t)morphPos, range);
        set_rgb_colour(gTheme.orange1);

        if (morphAngle > angle) {
            render_circle_part_angle(area, (tCoord){x, y}, radius, angle, morphAngle, 25);
        } else {
            render_circle_part_angle(area, (tCoord){x, y}, radius, morphAngle, angle, 25);
        }
    }
    set_rgb_colour((tRgb)RGB_BLACK);
    render_radial_line(area, (tCoord){x, y}, radius, angle, 2.0);
    set_rgb_colour((tRgb)RGB_BLACK);
    return render_circle_line(area, (tCoord){x, y}, radius, 25, 1.0);
}

// ─── Shared panel chrome ─────────────────────────────────────────────────────

#define PANEL_CLOSE_INSET    6.0     // from the panel's top-left corner to the button
#define PANEL_CLOSE_SIZE     14.0    // the button is square
#define PANEL_CLOSE_CROSS    4.0     // how far the cross is inset inside the button
// BORDER_LINE_WIDTH is sized for a whole panel and reads as a slab around a button this small.
// The cross is drawn slightly heavier than its frame: the frame's lines are axis-aligned and stay
// crisp, while the diagonals get antialiased and would otherwise look the lighter of the two, so
// matching the numbers makes the box dominate the mark it exists to present.
#define PANEL_CLOSE_BOX_LINE      1.0
#define PANEL_CLOSE_CROSS_LINE    1.5
#define PANEL_TITLE_GAP           8.0 // between the close button and the title text

tRectangle panel_close_button_rect(tRectangle box) {
    return (tRectangle){
        {
            box.coord.x + PANEL_CLOSE_INSET + BORDER_LINE_WIDTH,
            box.coord.y + PANEL_CLOSE_INSET
        },
        {
            PANEL_CLOSE_SIZE, PANEL_CLOSE_SIZE
        }
    };
}

tRectangle draw_panel_close_button(tArea area, tRectangle box, bool closePressed) {
    tRectangle rectangle = panel_close_button_rect(box);
    double     inset     = PANEL_CLOSE_CROSS;
    double     right     = rectangle.coord.x + rectangle.size.w;
    double     bottom    = rectangle.coord.y + rectangle.size.h;

    // Deliberately NOT RGB_BACKGROUND_GREY. This file compiles without G2_EDIT defined, so that
    // macro resolves to the dark 0.30 of synthlibDefs.h's other branch - the very same value as
    // the RGB_GREY_3 title bar drawn below, which made the button vanish into the banner and left
    // a black cross on dark grey. RGB_GREY_3 and RGB_GREY_7 are 0.30 and 0.70 in BOTH branches, so
    // deriving from those and letting contrasting_text_colour() choose the stroke is stable
    // whichever app is compiling.
    tRgb       banner    = (tRgb)RGB_GREY_3;    // must stay the colour draw_panel_chrome() fills
    tRgb       fill      = closePressed ? (tRgb)RGB_GREY_7 : banner;
    tRgb       stroke    = contrasting_text_colour(fill);

    if (closePressed) {
        set_rgb_colour(fill);
        render_rectangle(area, rectangle);
    }
    // Outlined by hand rather than through render_rectangle_with_border(), which has no say over
    // its line width.
    set_rgb_colour(stroke);
    render_line(area, (tCoord){rectangle.coord.x, rectangle.coord.y}, (tCoord){right, rectangle.coord.y}, PANEL_CLOSE_BOX_LINE);
    render_line(area, (tCoord){rectangle.coord.x, bottom}, (tCoord){right, bottom}, PANEL_CLOSE_BOX_LINE);
    render_line(area, (tCoord){rectangle.coord.x, rectangle.coord.y}, (tCoord){rectangle.coord.x, bottom}, PANEL_CLOSE_BOX_LINE);
    render_line(area, (tCoord){right, rectangle.coord.y}, (tCoord){right, bottom}, PANEL_CLOSE_BOX_LINE);

    // Two drawn diagonals rather than the character 'X'. A glyph would sit on the text baseline
    // instead of centred in the box, and wouldn't scale with the button - it has to be lines.
    render_line(area,
                (tCoord){rectangle.coord.x + inset, rectangle.coord.y + inset},
                (tCoord){right - inset, bottom - inset},
                PANEL_CLOSE_CROSS_LINE);
    render_line(area,
                (tCoord){right - inset, rectangle.coord.y + inset},
                (tCoord){rectangle.coord.x + inset, bottom - inset},
                PANEL_CLOSE_CROSS_LINE);
    return rectangle;
}

tRectangle draw_panel_chrome(tArea area, tRectangle box, double titleH, const char * title) {
    tRectangle titleBar  = {box.coord, {box.size.w, titleH}};

    set_rgb_colour((tRgb)RGB_GREY_5);
    render_rectangle_with_border(area, box);

    set_rgb_colour((tRgb)RGB_GREY_3);
    render_rectangle(area, (tRectangle){
        {box.coord.x + BORDER_LINE_WIDTH, box.coord.y + BORDER_LINE_WIDTH},
        {box.size.w - (2.0 * BORDER_LINE_WIDTH), titleH - BORDER_LINE_WIDTH}
    });

    // Indented past the close button, which now occupies the corner the title used to start in.
    tRectangle closeRect = panel_close_button_rect(box);

    set_rgb_colour((tRgb)RGB_WHITE);
    render_text(area, (tRectangle){
        {closeRect.coord.x + closeRect.size.w + PANEL_TITLE_GAP, box.coord.y + 6.0},
        {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
    }, title);

    return titleBar;
}

// The rectangle IS THE DIAL: its coord is the top-left of the circle's bounding square and its
// width the diameter. The value string is drawn one row directly above the dial and the label one
// row above that, growing upwards, so the dial sits exactly where the caller put it whatever text
// it does or doesn't carry.
//
// That anchoring is the point. This used to take the top-left of the whole label+value+dial block
// and work downwards, which made the dial's position depend on how many of the two strings were
// non-NULL - a dial with no label rode a row higher than its neighbours, and the only way to line
// a row of them up was to pass "" instead of NULL so the row was reserved but blank. NULL and ""
// now do the same thing, because neither can move the dial.
tRectangle render_dial_with_text(tArea area, tRectangle rectangle, const char * label, const char * buff, double labelH, uint32_t value, uint32_t range, uint32_t morphRange, tRgb colour) {
    set_rgb_colour((tRgb)RGB_BLACK);

    if (buff != NULL) {
        render_text(area, (tRectangle){{rectangle.coord.x, rectangle.coord.y - labelH}, {BLANK_SIZE, labelH}}, buff);
    }

    if (label != NULL) {
        render_text(area, (tRectangle){{rectangle.coord.x, rectangle.coord.y - (labelH * 2.0)}, {BLANK_SIZE, labelH}}, label);
    }
    return render_dial(area, (tRectangle){{rectangle.coord.x, rectangle.coord.y}, {rectangle.size.w, rectangle.size.w}}, value, range, morphRange, colour);
}

#ifdef __cplusplus
}
#endif

// Dims the whole canvas behind a modal panel. Was in G2-Edit's graphics.c; it is six lines of
// drawing with no window in it, and the VST3 plug-in needs it for the same panels.
void draw_dialog_background_overlay(void) {
    double renderW = get_render_width() / gGlobalGuiScale;
    double renderH = get_render_height() / gGlobalGuiScale;

    set_rgb_colour((tRgb)RGB_GREY_2);
    render_rectangle(mainArea, (tRectangle){{0.0, 0.0}, {renderW, renderH}});
}
