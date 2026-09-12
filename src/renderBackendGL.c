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
// Notes: Docs/code-notes/renderBackendGL.c.md - "// notes §k" refers there.

#include "renderBackendSelect.h"

// notes §1
#ifndef SYNTHLIB_NO_GL_BACKEND


// notes §2

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

// notes §3
static int gSurfaceHeight = 0;

static void gl_init(void) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#if GFX_MSAA_SAMPLES > 1
    // notes §4
    glEnable(GL_MULTISAMPLE);
#endif

    // The canvas is 2D and painted back to front. GL leaves depth testing off by default and the
    // app relied on that; the plug-in's context said so explicitly. Stated once here so the two
    // cannot differ.
    glDisable(GL_DEPTH_TEST);
}

static void gl_set_surface(int width, int height) {
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

static void gl_clear(tRgb colour) {
    glClearColor((GLfloat)colour.red, (GLfloat)colour.green, (GLfloat)colour.blue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void gl_submit(const tVertex * verts, size_t count, uint32_t texture) {
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

static void gl_scissor(int x, int y, int width, int height) {
    if (width < 0) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    // notes §5
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, gSurfaceHeight - (y + height), width, height);
}

static bool gl_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out) {
    if ((width <= 0) || (height <= 0) || (out == NULL)) {
        return false;
    }
    // notes §6
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, out);
    return true;
}

static uint32_t gl_texture_alloc(int width, int height, const uint8_t * rgba, tTextureFilter filter) {
    GLuint texture = 0;

    if ((width <= 0) || (height <= 0)) {
        return 0;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    // Nearest where a texel maps to a pixel, linear where it does not — see tTextureFilter.
    GLint  mode    = (filter == eTextureLinear) ? GL_LINEAR : GL_NEAREST;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Left unbound. gl_submit() binds what it needs, so nothing depends on what happens to be
    // bound when this returns.
    glBindTexture(GL_TEXTURE_2D, 0);
    return (uint32_t)texture;
}

static void gl_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba) {
    if ((texture == 0) || (width <= 0) || (height <= 0) || (rgba == NULL)) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void gl_attach_window(void * nativeWindow) {
    // Nothing to do: GLFW created a context alongside the window and made it current before this
    // is reached, so the surface is already the one being drawn into. Metal has to be told.
    (void)nativeWindow;
}

static void gl_present(void) {
// SYNTHLIB_PLUGIN_BUILD is the name since there were two plug-in FORMATS to build. It was
// G2_VST3_BUILD before that, and this accepted both until the last of the three plug-ins moved onto
// SynthLib's shared wrappers on 2026-09-11; nothing passes the old spelling any more.
#if defined (SYNTHLIB_PLUGIN_BUILD)
    // notes §7
#else
    // The context GLFW made current — asking it, rather than being handed the window, keeps this
    // file out of SynthLib's window layer.
    GLFWwindow * window = glfwGetCurrentContext();

    if (window != NULL) {
        glfwSwapBuffers(window);
    }
#endif
}

static void gl_texture_free(uint32_t texture) {
    GLuint name = (GLuint)texture;

    if (texture == 0) {
        return;
    }
    glDeleteTextures(1, &name);
}

// The table. These eleven are the whole of what this file offers; everything above is static, so
// there is no way to reach OpenGL from anywhere else even by accident.
static const tGfxBackend kGlBackend = {
    .name            = "OpenGL",
    .init            = gl_init,
    .set_surface     = gl_set_surface,
    .clear           = gl_clear,
    .submit          = gl_submit,
    .scissor         = gl_scissor,
    .read_pixels_rgb = gl_read_pixels_rgb,
    .texture_alloc   = gl_texture_alloc,
    .texture_write   = gl_texture_write,
    .texture_free    = gl_texture_free,
    .attach_window   = gl_attach_window,
    .present         = gl_present,
};

const tGfxBackend * gfx_backend_gl_table(void) {
    return &kGlBackend;
}

#ifdef __cplusplus
}
#endif

#endif // SYNTHLIB_NO_GL_BACKEND
