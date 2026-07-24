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

#ifndef __SYNTHLIB_GLOBALS_H__
#define __SYNTHLIB_GLOBALS_H__

#include <stdbool.h>

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle/window state identical across G2-Edit, EmuUtility, and SynthEdit — owned here as
// accessor functions rather than raw externs, so nothing outside this file ever touches the
// underlying storage directly (unlike gGlobalGuiScale/gScrollState in geometry.h, which predate
// this and stayed plain externs — this is the stricter pattern going forward).

// ── Lifecycle ────────────────────────────────────────────────────────────────
void synthlib_request_quit(void);   // call to begin app shutdown
bool synthlib_quit_requested(void); // checked by the render loop and any background-thread loop

// ── Redraw ───────────────────────────────────────────────────────────────────
// Sets the redraw flag and wakes a possibly-blocked glfwWaitEvents()/glfwWaitEventsTimeout() —
// every SynthLib popup/panel mechanism (contextMenu.c, menuBar.c, alertDialog.cpp,
// bankBrowser.cpp, fileBrowser.cpp) calls this, and so does every app, in place of what used to
// be a raw `gReDraw = true;`.
void synthlib_request_redraw(void);
// Explicit clear, e.g. right before a window closes so no further frame is drawn during teardown.
void synthlib_clear_redraw(void);
// Atomically reads the redraw flag and clears it in one step — call once per iteration of the
// render loop ("was a redraw requested since the last time I checked?").
bool synthlib_consume_redraw(void);

// ── GLFW window ──────────────────────────────────────────────────────────────
// void* keeps this header GLFW-free — cast to GLFWwindow* at the call site, same convention every
// app's own (now-retired) gWindow used.
void   synthlib_set_window(void * glfwWindow);
void * synthlib_window(void);

// ── Dial mode ────────────────────────────────────────────────────────────────
void      synthlib_set_dial_mode(tDialMode mode);
tDialMode synthlib_dial_mode(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_GLOBALS_H__
