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

#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "synthlibTypes.h"

// ── The platform contract ───────────────────────────────────────────────────────────────────────
//
// NINE FUNCTIONS. Exactly one renderBackend*.c implements them (renderBackendSelect.h picks it),
// and that file is the only one in SynthLib or in any of the three applications permitted to name
// a graphics API. Everything else — utilsGraphics.c included — draws by appending triangles and
// calling gfx_submit().
//
// THE NAMING SAYS WHICH SIDE OF THE LINE A CALL IS ON. render_* is the portable drawing API the
// applications use and is implemented once, in utilsGraphics.c. gfx_* is this contract and is
// implemented once per platform. A call site tells you which it is without looking anything up.
//
// WHAT IS DELIBERATELY NOT HERE, because it is policy rather than platform:
//   - the batch. Which triangles get merged into one submission, and the rule that a submission is
//     forced by anything that would change how following vertices rasterize, is identical on every
//     backend and lives in utilsGraphics.c. A backend receives finished triangles and draws them.
//   - the flush-before-you-change-it rule around textures. render_backend_texture_update() and
//     _destroy() submit the batch first when it still references that texture; gfx_texture_write()
//     and gfx_texture_free() below are the raw operations underneath and know nothing about it. A
//     new backend therefore cannot forget the rule, because it never sees it.
//   - anything about WHICH window exists, where it is, or what events it delivers. The window
//     layer owns all of that and hands the backend one native handle.
//
// PRESENTING IS IN THE CONTRACT, which contradicts what this comment said when it was first
// written — the correction is worth stating rather than quietly editing away. Presenting looked
// like a window concern, and it is not separable from the API: under OpenGL it is a buffer swap on
// the context GLFW created, and under Metal it is a blit into a drawable a CAMetalLayer vends,
// using the same device the frame was rendered with. There is no form of words covering both
// without naming an API, so gfx_attach_window() and gfx_present() are here.

// MULTISAMPLING. Every geometry edge in this UI is hard-rasterized: circles and arcs are polygon
// fans, cables are triangle strips, dial pointers are rotated quads. At 2x a jagged step is half a
// pixel and reads as smooth; at 1x it is a whole pixel and reads as jagged, which is exactly what
// CT saw on a 1280x720 display.
//
// IT IS NOT SELECTIVE, and that is the thing to understand before turning it up. Multisampling is a
// property of the render pass, so it smooths EVERY edge at once — the curves and diagonals it is
// aimed at, and also axis-aligned rectangle edges that land on fractional coordinates, which are
// currently hard. At 1x that is an improvement (a one-pixel border that presently vanishes or
// doubles becomes a consistent soft line) but it is a visible change everywhere, not a targeted one.
//
// It does NOTHING for text: a glyph is a textured quad, so the only edges multisampling can see are
// the corners of that quad, not the letterform inside it. Small text is handled separately, by the
// supersampled atlas in utilsGraphics.c.
//
// 0 or 1 disables it.
#define GFX_MSAA_SAMPLES    (4)

// One vertex, as submitted. Position is in framebuffer pixels with the origin TOP LEFT and y
// increasing downwards — the space the whole UI is laid out in — so a backend whose device
// coordinates differ converts in gfx_set_surface()'s projection, not per vertex.
typedef struct {
    float x, y;        // position, framebuffer pixels, origin top-left
    float u, v;        // texture coordinates
    float r, g, b, a;  // per-vertex colour; render_bezier_curve() genuinely varies it along a strip
} tVertex;

// How a texture is sampled. Declared here, above the backend table that names it — see the
// Textures section below for why there is a choice at all.
typedef enum {
    eTextureNearest = 0,   // one texel per pixel — the glyph atlas at normal sizes, and the LCD
    eTextureLinear  = 1,   // sampled at a scale that is not 1:1 — the supersampled small-text atlas
} tTextureFilter;

// ── Choosing one ────────────────────────────────────────────────────────────
//
// BOTH BACKENDS ARE COMPILED IN and one is chosen at start-up, so a user can try the other without
// a rebuild. That is why each backend's entry points are file-local and reached only through this
// table: two files defining gfx_init() would not link, and the alternative — a compile-time #if
// picking one — is what this replaced.
//
// The indirection costs one call through a pointer PER BATCH SUBMISSION, not per primitive. A
// frame makes a few dozen of those, so it is free in any sense that matters.
//
// IT CANNOT CHANGE WHILE RUNNING. The window itself is built differently for each: OpenGL needs
// GLFW to create a context alongside it, Metal needs GLFW to create none (GLFW_NO_API) and then
// takes the NSWindow. Switching means destroying and recreating the window, and with it the
// context, every glyph atlas and texture, and all the callbacks established around it. So the
// choice is read once, before the window is made, and a change takes effect on the next launch.

typedef enum {
    eRenderBackendOpenGL = 0,
    eRenderBackendMetal  = 1,
} tRenderBackendId;

typedef struct {
    const char * name;
    void (*init)(void);
    void (*set_surface)(int width, int height);
    void (*clear)(tRgb colour);
    void (*submit)(const tVertex * verts, size_t count, uint32_t texture);
    void (*scissor)(int x, int y, int width, int height);
    bool (*read_pixels_rgb)(int x, int y, int width, int height, uint8_t * out);
    uint32_t (*texture_alloc)(int width, int height, const uint8_t * rgba, tTextureFilter filter);
    void (*texture_write)(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba);
    void (*texture_free)(uint32_t texture);
    void (*attach_window)(void * nativeWindow);
    void (*present)(void);
} tGfxBackend;

// True if this build can actually run that backend — Metal is macOS only, and nothing else exists
// on Windows or Linux, where renderBackendMetal.m compiles to nothing.
bool gfx_backend_available(tRenderBackendId which);

// Selects it. MUST be called before the window is created, because the window layer asks which
// backend is in force to decide whether to make a context at all. False if unavailable, leaving
// the previous choice standing.
bool gfx_backend_choose(tRenderBackendId which);

tRenderBackendId gfx_backend_current(void);
const char * gfx_backend_name(tRenderBackendId which);

// ── The calls themselves ────────────────────────────────────────────────────

// Session-wide drawing state. Called once, after a surface is current and before anything is
// drawn. BLENDING IS ON FOR THE WHOLE SESSION and no drawing code ever turns it off: an opaque
// draw resolves to the source colour either way, so the invariant costs opaque drawing nothing
// and spares every translucent caller an enable/disable pair. There is no depth buffer — the
// canvas is 2D and painted back to front.
void gfx_init(void);

// Viewport plus the projection that maps the tVertex space above onto the device. Called on any
// framebuffer size change. A backend should remember the height: gfx_scissor() needs it if its
// scissor origin is not top-left.
void gfx_set_surface(int width, int height);

// Clears the colour buffer. Colour is the whole frame, there being no depth.
void gfx_clear(tRgb colour);

// Draws `count` vertices as a plain triangle list — three vertices per triangle, no strips or
// fans, which is the one topology every API agrees on. `texture` is 0 for untextured geometry,
// otherwise a handle from gfx_texture_alloc(), sampled and MODULATED by the vertex colour (the
// glyph atlas is white coverage tinted by the colour the caller set). Winding is not normalised
// and must not be culled. `count` is always a multiple of three and never zero.
void gfx_submit(const tVertex * verts, size_t count, uint32_t texture);

// Clips subsequent drawing to a rectangle, in WHOLE framebuffer pixels with the origin TOP LEFT
// to match tVertex: the columns x up to x+width, and the rows y up to y+height. A NEGATIVE width
// turns clipping off.
//
// INTEGERS, and the rounding happens ONCE, in module_pane_clip_begin(), rather than here. It used
// to be a double rect that each backend truncated for itself, and that is exactly how the two
// backends disagreed: OpenGL truncates the flipped bottom edge and the height separately, which
// lands its rectangle a row lower than truncating the top and bottom edges does. Two rows out of
// 1704, at the pane boundary, and invisible — but it cost the ability to say the two backends
// produce identical frames, which is the only cheap proof a port is correct. Rounded here, both
// get the same whole pixels and the flip below is exact.
//
// A backend must CLAMP to the render target. OpenGL does it silently; Metal treats an out-of-
// bounds scissor as a validation failure.
void gfx_scissor(int x, int y, int width, int height);

// Reads the frame back as tightly-packed RGB triples, BOTTOM row first, into a caller-supplied
// buffer of at least width*height*3 bytes. Backs the backdoor SCREENSHOT command in all three
// apps, which is how a rendering change is proved to have changed no pixel — so a backend that
// cannot do this cannot be verified. False if the arguments are unusable.
bool gfx_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out);

// ── Textures ────────────────────────────────────────────────────────────────
// Always RGBA8 and clamped to edge. A handle is opaque and need not be a driver object: under
// OpenGL it is the GLuint, under Metal it indexes a table the backend keeps, because an
// id<MTLTexture> does not fit in an integer. 0 is "no texture" and is never returned by a
// successful alloc.
//
// THE FILTER IS A PARAMETER, which it was not until 2026-08-28. This header said there was no need
// for one "because both callers blit one texel per pixel" and that a filter should be added "then,
// with the case in front of you". The case arrived: small text is now rasterized at twice its
// drawn size and scaled down, which point sampling would simply throw half the texels away.
uint32_t gfx_texture_alloc(int width, int height, const uint8_t * rgba, tTextureFilter filter);  // rgba may be NULL
void gfx_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba);
void gfx_texture_free(uint32_t texture);

// ── Presenting ──────────────────────────────────────────────────────────────

// Hands the backend the native window it will present into, once, at window creation. On macOS
// that is the NSWindow from glfwGetCocoaWindow(). A backend that got its surface another way — the
// OpenGL one, where GLFW has already created a context and made it current before this is reached
// — ignores it.
void gfx_attach_window(void * nativeWindow);

// Puts the finished frame on screen. Called once per frame by render_present(), which submits the
// batch first. NOT called by the VST3 plug-in: a plug-in draws into a view the HOST presents, so
// g2GlDraw.c ends its frame at render_backend_flush() and g2GlView.m takes it from there.
void gfx_present(void);

#endif // RENDER_BACKEND_H
