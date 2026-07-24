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

#include <stddef.h>
#include <stdatomic.h>

#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>

#include "synthlibGlobals.h"

static _Atomic bool sQuitAll  = false;
static _Atomic bool sReDraw   = true;
static void *       sWindow   = NULL;
// eDialModeVertical matches EmuUtility's and SynthEdit's own previous default — G2-Edit's default
// was eDialModeRotary instead, so it calls synthlib_set_dial_mode() explicitly at the top of its
// own init_graphics(), before load_saved_settings() can overwrite it from a real saved value
// anyway (see that call site's own comment).
static tDialMode    sDialMode = eDialModeVertical;

void synthlib_request_quit(void) {
    sQuitAll = true;
}

bool synthlib_quit_requested(void) {
    return sQuitAll;
}

void synthlib_request_redraw(void) {
    sReDraw = true;
    glfwPostEmptyEvent(); // safe to call unconditionally from any thread once GLFW is initialised
}

void synthlib_clear_redraw(void) {
    sReDraw = false;
}

bool synthlib_consume_redraw(void) {
    return atomic_exchange(&sReDraw, false);
}

void synthlib_set_window(void * glfwWindow) {
    sWindow = glfwWindow;
}

void * synthlib_window(void) {
    return sWindow;
}

void synthlib_set_dial_mode(tDialMode mode) {
    sDialMode = mode;
}

tDialMode synthlib_dial_mode(void) {
    return sDialMode;
}

#ifdef __cplusplus
}
#endif
