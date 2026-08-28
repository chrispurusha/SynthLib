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
//   - anything about windows, contexts, drawables or presenting. The window layer owns that; a
//     backend is handed a surface that is already current.

// One vertex, as submitted. Position is in framebuffer pixels with the origin TOP LEFT and y
// increasing downwards — the space the whole UI is laid out in — so a backend whose device
// coordinates differ converts in gfx_set_surface()'s projection, not per vertex.
typedef struct {
    float x, y;        // position, framebuffer pixels, origin top-left
    float u, v;        // texture coordinates
    float r, g, b, a;  // per-vertex colour; render_bezier_curve() genuinely varies it along a strip
} tVertex;

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
// Always RGBA8, nearest-filtered, clamped to edge — see utilsGraphics.h for why there is no
// parameter for any of that. A handle is opaque and need not be a driver object: under OpenGL it
// is the GLuint, under Metal it will index a table the backend keeps, because an id<MTLTexture>
// does not fit in an integer. 0 is "no texture" and is never returned by a successful alloc.

uint32_t gfx_texture_alloc(int width, int height, const uint8_t * rgba);  // rgba may be NULL
void gfx_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba);
void gfx_texture_free(uint32_t texture);

#endif // RENDER_BACKEND_H
