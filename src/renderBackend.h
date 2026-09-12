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
// Notes: Docs/code-notes/renderBackend.h.md - "// notes §k" refers there.

#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "synthlibTypes.h"

// notes §1

// notes §2
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

// notes §3

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
    void (*detach_window)(void * nativeWindow);      // may be NULL for backends with one window
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

// notes §4
void gfx_init(void);

// Viewport plus the projection that maps the tVertex space above onto the device. Called on any
// framebuffer size change. A backend should remember the height: gfx_scissor() needs it if its
// scissor origin is not top-left.
void gfx_set_surface(int width, int height);

// Clears the colour buffer. Colour is the whole frame, there being no depth.
void gfx_clear(tRgb colour);

// notes §5
void gfx_submit(const tVertex * verts, size_t count, uint32_t texture);

// notes §6
void gfx_scissor(int x, int y, int width, int height);

// notes §7
bool gfx_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out);

// notes §8
uint32_t gfx_texture_alloc(int width, int height, const uint8_t * rgba, tTextureFilter filter);  // rgba may be NULL
void gfx_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba);
void gfx_texture_free(uint32_t texture);

// ── Presenting ──────────────────────────────────────────────────────────────

// notes §9
void gfx_attach_window(void * nativeWindow);

// Releases whatever a window owned. Call it when the surface goes away - a plug-in editor being
// closed, say. Backends that only ever have one window may leave this unimplemented.
void gfx_detach_window(void * nativeWindow);

// Puts the finished frame on screen. Called once per frame by render_present(), which submits the
// batch first. NOT called by the VST3 plug-in: a plug-in draws into a view the HOST presents, so
// g2GlDraw.c ends its frame at render_backend_flush() and g2GlView.m takes it from there.
void gfx_present(void);

#endif // RENDER_BACKEND_H
