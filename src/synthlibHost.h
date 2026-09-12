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
// Notes: Docs/code-notes/synthlibHost.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_HOST_H__
#define __SYNTHLIB_HOST_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §1
typedef void (*tSynthLibMouseCoordFn)(tCoord * coord);

// notes §2
typedef bool (*tSynthLibPointerCapturedFn)(void);

typedef struct {
    tSynthLibMouseCoordFn      mouseCoord;      // NULL if the app never opens a mechanism that needs it
    tSynthLibPointerCapturedFn pointerCaptured; // NULL if the app never hides the pointer
} tSynthLibHost;

// Call once, before any SynthLib UI mechanism (contextMenu, menuBar, alertDialog, bankBrowser,
// fileBrowser) is used — in practice, right alongside configure_synthlib_theme() at the top of the
// app's own init_graphics().
void synthlib_host_init(tSynthLibHost host);

// Writes {0, 0} into *coord if synthlib_host_init() was never called, or was called with
// mouseCoord == NULL, rather than leaving it uninitialised.
void synthlib_host_mouse_coord(tCoord * coord);

// Whether the pointer is currently captured for a drag. False when the host supplied no predicate,
// so a caller can use it unconditionally. Hover highlighting must consult this - see the note above.
bool synthlib_host_pointer_captured(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_HOST_H__
