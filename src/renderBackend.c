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

// ── Which backend, and the one line each call takes to reach it ─────────────────────────────────
//
// The gfx_* functions everything else calls, forwarded to whichever backend was chosen at start-up.
// The choice exists so a user can try Metal against OpenGL without a rebuild — see renderBackend.h
// for why it cannot change while the app is running.

#include "renderBackendSelect.h"
#include "renderBackend.h"

const tGfxBackend * gfx_backend_gl_table(void);

#ifdef __APPLE__
const tGfxBackend * gfx_backend_metal_table(void);
#endif

static tRenderBackendId    gCurrent = RENDER_BACKEND_DEFAULT;
static const tGfxBackend * gBackend = NULL;

static const tGfxBackend * table_for(tRenderBackendId which) {
    switch (which) {
#ifdef __APPLE__
        case eRenderBackendMetal:
            return gfx_backend_metal_table();
#endif
        case eRenderBackendOpenGL:
        default:
            return gfx_backend_gl_table();
    }
}

// Resolved on first use as well as on an explicit choice, so that a caller who never selects one
// still gets a working backend rather than a null pointer.
static const tGfxBackend * backend(void) {
    if (gBackend == NULL) {
        gBackend = table_for(gCurrent);
    }
    return gBackend;
}

bool gfx_backend_available(tRenderBackendId which) {
    if (which == eRenderBackendOpenGL) {
        return true;    // renderBackendGL.c is OpenGL 1.1 and builds everywhere
    }

#ifdef __APPLE__

    if (which == eRenderBackendMetal) {
        return true;
    }

#endif
    return false;
}

bool gfx_backend_choose(tRenderBackendId which) {
    if (!gfx_backend_available(which)) {
        return false;
    }
    gCurrent = which;
    gBackend = table_for(which);
    return true;
}

tRenderBackendId gfx_backend_current(void) {
    return gCurrent;
}

const char * gfx_backend_name(tRenderBackendId which) {
    return gfx_backend_available(which) ? table_for(which)->name : "unavailable";
}

// ── The forwarding ──────────────────────────────────────────────────────────

void gfx_init(void) {
    backend()->init();
}

void gfx_set_surface(int width, int height) {
    backend()->set_surface(width, height);
}

void gfx_clear(tRgb colour) {
    backend()->clear(colour);
}

void gfx_submit(const tVertex * verts, size_t count, uint32_t texture) {
    backend()->submit(verts, count, texture);
}

void gfx_scissor(int x, int y, int width, int height) {
    backend()->scissor(x, y, width, height);
}

bool gfx_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out) {
    return backend()->read_pixels_rgb(x, y, width, height, out);
}

uint32_t gfx_texture_alloc(int width, int height, const uint8_t * rgba, tTextureFilter filter) {
    return backend()->texture_alloc(width, height, rgba, filter);
}

void gfx_texture_write(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba) {
    backend()->texture_write(texture, x, y, width, height, rgba);
}

void gfx_texture_free(uint32_t texture) {
    backend()->texture_free(texture);
}

void gfx_attach_window(void * nativeWindow) {
    backend()->attach_window(nativeWindow);
}

void gfx_present(void) {
    backend()->present();
}
