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

// Single injection point for the two things every SynthLib popup/panel mechanism (contextMenu.c,
// menuBar.c, alertDialog.cpp, bankBrowser.cpp, fileBrowser.cpp) needs from the embedding app:
// a way to request a redraw, and the current mouse position in the app's own logical/scaled
// coordinate space. Previously each of those 5 files declared its own
// `extern "C" _Atomic bool gReDraw;` / `extern "C" void get_global_gui_scaled_mouse_coord(tCoord *);`,
// trusting the app to define symbols under those exact names — harmless while there was only ever
// one app-side implementation of each, but it meant every new file that needed a redraw had to
// remember to redeclare both externs verbatim, and nothing enforced that the names or signatures
// actually matched what the app provided. This replaces that with one real function call: the app
// calls synthlib_host_init() once at startup (see configure_synthlib_theme()'s own call site for
// the existing "tell SynthLib about the app" precedent this mirrors), and every file below calls
// synthlib_request_redraw()/synthlib_host_mouse_coord() instead of declaring its own extern.
typedef void (*tSynthLibRequestRedrawFn)(void);
typedef void (*tSynthLibMouseCoordFn)(tCoord * coord);

typedef struct {
    tSynthLibRequestRedrawFn requestRedraw; // must not be NULL
    tSynthLibMouseCoordFn    mouseCoord;    // NULL if the app never opens a mechanism that needs it
} tSynthLibHost;

// Call once, before any SynthLib UI mechanism (contextMenu, menuBar, alertDialog, bankBrowser,
// fileBrowser) is used — in practice, right alongside configure_synthlib_theme() at the top of the
// app's own init_graphics().
void synthlib_host_init(tSynthLibHost host);

// A no-op if synthlib_host_init() was never called, or was called with requestRedraw == NULL —
// safe to call unconditionally.
void synthlib_request_redraw(void);

// Writes {0, 0} into *coord if synthlib_host_init() was never called, or was called with
// mouseCoord == NULL, rather than leaving it uninitialised.
void synthlib_host_mouse_coord(tCoord * coord);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_HOST_H__
