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

#include "renderBackendSelect.h"

#if RENDER_BACKEND == RENDER_BACKEND_GL

// ── The OpenGL backend ──────────────────────────────────────────────────────────────────────────
//
// The nine functions of renderBackend.h, and THE ONLY FILE IN SYNTHLIB OR IN ANY OF THE THREE
// APPLICATIONS THAT NAMES OPENGL. Everything above it draws by appending triangles.
//
// EVERY CALL BELOW IS OPENGL 1.1 OR EARLIER. That is not nostalgia, it is the reason this file
// covers Windows and Linux as well as macOS without a second thought: glDrawArrays, the
// client-side vertex array pointers, glTexImage2D, glScissor, glOrtho and glBlendFunc were all
// there in 1997, so there is no driver on any of the three platforms that lacks them. macOS is
// the only one of the three where OpenGL is deprecated, and it is deprecated rather than gone.
//
// So the eventual arrangement is not four backends. It is this file on Windows and Linux, and
// this file OR renderBackendMetal.m on macOS — with this one kept alive there precisely so the
// two can be run against each other on one machine and diffed, which is the only cheap way to
// prove the Metal port moved no pixel.

#ifdef __cplusplus
extern "C" {
#endif

// Disable warnings from external library headers etc.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"

#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>
#pragma clang diagnostic pop

#include "synthlibDefs.h"
#include "renderBackend.h"

// The surface height, remembered because gfx_scissor() needs it: GL's scissor origin is the
// BOTTOM left where every coordinate handed to this file is top-left. gfx_set_surface() is always
// called with the same height as set_render_height() — both from synthlibScale.c in the
// applications and from g2_gl_draw_frame() in the plug-in — but this file keeps its own rather
// than reaching for that global, so the flip cannot silently disagree with the projection.
static int gSurfaceHeight = 0;

void gfx_init(void) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The canvas is 2D and painted back to front. GL leaves depth testing off by default and the
    // app relied on that; the plug-in's context said so explicitly. Stated once here so the two
    // cannot differ.
    glDisable(GL_DEPTH_TEST);
}

void gfx_set_surface(int width, int height) {
    gSurfaceHeight = height;

    glViewport(0, 0, width, height);

    // Origin top-left, y increasing downwards, one unit per physical pixel — the tVertex space.
    // The near/far of -1..1 is GL's own convention and means nothing here; nothing writes depth.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void gfx_clear(tRgb colour) {
    glClearColor((GLfloat)colour.red, (GLfloat)colour.green, (GLfloat)colour.blue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void gfx_submit(const tVertex * verts, size_t count, uint32_t texture) {
    if ((verts == NULL) || (count == 0)) {
        return;
    }

    if (texture != 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
        // Modulate: the glyph atlas is white coverage, tinted by the vertex colour. This was set
        // once inside the old text renderer and never reset, so it already applied to both
        // textured paths; stating it per submission removes the dependency on that ordering.
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glVertexPointer(2, GL_FLOAT, sizeof(tVertex), &verts[0].x);
    glColorPointer(4, GL_FLOAT, sizeof(tVertex), &verts[0].r);
    glTexCoordPointer(2, GL_FLOAT, sizeof(tVertex), &verts[0].u);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)count);

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    if (texture != 0) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
}

void gfx_scissor(int x, int y, int width, int height) {
    if (width < 0) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    // THE FLIP LIVES HERE, because a bottom-left scissor origin is a property of OpenGL and not of
    // the UI — Metal's is top-left and needs none. It is exact: the rectangle arrives in whole
    // pixels, so nothing is truncated and both backends cover the same rows. It did not used to
    // be, and that was the one thing the Metal port actually got wrong — see renderBackend.h.
    //
    // GL clamps an out-of-bounds rectangle silently, so there is nothing to do about that here.
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, gSurfaceHeight - (y + height), width, height);
}

bool gfx_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out) {
    if ((width <= 0) || (height <= 0) || (out == NULL)) {
        return false;
    }
    // Tightly-packed rows (width*3 bytes). Without this, glReadPixels' default GL_PACK_ALIGNMENT
    // of 4 pads each row up to a 4-byte multiple whenever width*3 isn't already one (i.e. any
    // width not a multiple of 4) — which both shears the saved PNG (row stride mismatch vs stbi's
    // width*3) AND overruns the width*height*3 buffer. Only bit us at odd window sizes; Retina
    // captures were multiples of 4.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, out);
    return true;
}

uint32_t gfx_texture_alloc(int width, int height, const uint8_t * rgba) {
    GLuint texture = 0;

    if ((width <= 0) || (height <= 0)) {
        return 0;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    // GL_NEAREST: every caller blits one texel per pixel, so filtering could only blur a sample
    // that already lands dead centre on its texel.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Left unbound. gfx_submit() binds what it needs, so nothing depends on what happens to be
    // bound when this returns.
    glBindTexture(GL_TEXTURE_2D, 0);
    return (uint32_t)texture;
}

void gfx_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba) {
    if ((texture == 0) || (width <= 0) || (height <= 0) || (rgba == NULL)) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void gfx_texture_free(uint32_t texture) {
    GLuint name = (GLuint)texture;

    if (texture == 0) {
        return;
    }
    glDeleteTextures(1, &name);
}

#ifdef __cplusplus
}
#endif

#endif // RENDER_BACKEND == RENDER_BACKEND_GL
