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
// Notes: Docs/code-notes/synthlibWindow.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_WINDOW_H__
#define __SYNTHLIB_WINDOW_H__

// notes §1

#include <GLFW/glfw3.h>

#include "synthlibTypes.h"
#include "synthlibGlobals.h"
#include "synthlibHost.h"
#include "utilsGraphics.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §2
typedef struct {
    void (*mouseButton)(tCoord coord, tMouseButton button, int mods);
    void (*cursorPos)(tCoord coord);
    void (*key)(int key, int scancode, int action, int mods);
    void (*character)(unsigned int codepoint);
    void (*scroll)(double dx, double dy);
    void (*windowFocus)(bool focused);
    void (*windowRefresh)(void);
} tSynthLibInputHandlers;

typedef struct {
    const char *               title;           // the whole title, composed by the app: it owns __DATE__/__TIME__
    int                        targetWidth;     // design-space framebuffer size, and the locked aspect ratio
    int                        targetHeight;
    int                        minDivisor;      // window minimum is target/this; 0 means the shared default of 4
    tDialMode                  dialMode;        // G2-Edit is rotary, the other two vertical
    tSynthLibTheme             theme;           // built from the app's own colour/metric macros
    tSynthLibMouseCoordFn      mouseCoord;      // synthlib_host_init's injection point, see synthlibHost.h
    tSynthLibPointerCapturedFn pointerCaptured; // ditto: true while a drag has the pointer hidden

    // Normalised handlers. When non-NULL, SynthLib installs its own GLFW shims for the events these
    // cover, and the matching raw callbacks are ignored — see tSynthLibInputHandlers above.
    const tSynthLibInputHandlers * handlers;
} tSynthLibWindowConfig;

// Every one of these is optional. NULL simply leaves that GLFW callback unregistered, which is what
// an app that has nothing to do with the event wants — EmuUtility takes no character input, and
// G2-Edit has no use for a refresh callback.
typedef struct {
    GLFWkeyfun           key;
    GLFWcharfun          character;
    GLFWcursorposfun     cursorPos;
    GLFWmousebuttonfun   mouseButton;
    GLFWscrollfun        scroll;
    GLFWwindowfocusfun   windowFocus;
    GLFWwindowrefreshfun windowRefresh;
} tSynthLibWindowCallbacks;

// notes §3
void * synthlib_window_create(const tSynthLibWindowConfig * config, const tSynthLibWindowCallbacks * callbacks);

// notes §4

// notes §5
void synthlib_window_close(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_WINDOW_H__
