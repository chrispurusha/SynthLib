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

#ifndef __SYNTHLIB_HOST_H__
#define __SYNTHLIB_HOST_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Injection point for the one thing every SynthLib popup/panel mechanism (contextMenu.c,
// menuBar.c, alertDialog.cpp, bankBrowser.cpp, fileBrowser.cpp) still needs from the embedding
// app that SynthLib itself can't provide: the current mouse position, in the app's own
// logical/scaled coordinate space (depends on the app's own window and GLFW cursor query).
// Previously each of those 5 files also declared its own
// `extern "C" _Atomic bool gReDraw;` for requesting a redraw — gReDraw itself now lives in
// SynthLib (synthlibGlobals.h's synthlib_request_redraw()), so that half of this mechanism was
// retired; only the mouse-coord half remains app-specific.
typedef void (*tSynthLibMouseCoordFn)(tCoord * coord);

typedef struct {
    tSynthLibMouseCoordFn mouseCoord; // NULL if the app never opens a mechanism that needs it
} tSynthLibHost;

// Call once, before any SynthLib UI mechanism (contextMenu, menuBar, alertDialog, bankBrowser,
// fileBrowser) is used — in practice, right alongside configure_synthlib_theme() at the top of the
// app's own init_graphics().
void synthlib_host_init(tSynthLibHost host);

// Writes {0, 0} into *coord if synthlib_host_init() was never called, or was called with
// mouseCoord == NULL, rather than leaving it uninitialised.
void synthlib_host_mouse_coord(tCoord * coord);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_HOST_H__
