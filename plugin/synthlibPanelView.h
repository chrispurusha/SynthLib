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
// Notes: Docs/code-notes/synthlibPanelView.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_PANEL_VIEW_H__
#define __SYNTHLIB_PANEL_VIEW_H__

// notes §1

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // The panel's logical width. Every coordinate the plug-in sees is in these units, scaled to
    // whatever size the host makes the view, so the panel is the same shape at any size.
    double canvasWidth;

    // Once per view, after its drawing surface exists.
    void (*init)(void);

    // BEFORE EVERY FRAME AND EVERY CLICK, on the main thread: put THIS editor's state into the draw
    // layer. Both siblings keep that state file-scope, so with two editors open whichever set it last
    // would otherwise draw - and, worse, hit-test - for both.
    void (*sync)(void * user);

    // One frame, in PHYSICAL pixels.
    void (*frame)(void * user, int pixelWidth, int pixelHeight);

    // A click, in LOGICAL units. True if it hit something; the plug-in acts on it itself - the view
    // only redraws.
    bool (*click)(void * user, double x, double y);

    // Where the pointer is, in logical units, so an open drop-down can highlight under it.
    void (*pointer)(double x, double y);

    // Is a drop-down open? A bare mouse move repaints only then.
    bool (*menuActive)(void);
} tSynthLibPanel;

// An NSView *, RETAINED (+1), as a void * - which is what SynthLib's createView() contract wants. The
// panel description is NOT copied: pass something with static storage. `user` goes back to every call.
void * synthlib_panel_view_create(const tSynthLibPanel * panel, void * user, double width, double height);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PANEL_VIEW_H__
