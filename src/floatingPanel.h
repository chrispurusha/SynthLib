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
// Notes: Docs/code-notes/floatingPanel.h.md - "// notes §k" refers there.

#ifndef __FLOATING_PANEL_H__
#define __FLOATING_PANEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "synthlibTypes.h"

// notes §1
typedef struct {
    tRectangle rect;           // where the panel IS: position kept across frames, size set by content
    tRectangle titleBarRect;   // the drag handle; the render pass fills this in from draw_panel_chrome()

    // The close button, which sits INSIDE the title bar (panel_close_button_rect() insets it from the
    // panel's top-left). Without excluding it, the title-bar drag claims the press first and the
    // close never registers — which is exactly what broke closing the Virtual Keyboard.
    tRectangle closeRect;
    bool       placed;         // false until first shown — a position is chosen ONCE, never per frame
    bool       dragging;
    tCoord     dragMouseStart;
    tCoord     dragPanelStart;

    // notes §2
    uint32_t   order;
} tFloatingPanel;

// notes §3
void floating_panel_set_bounds(tRectangle bounds);

// Call at the top of the panel's render pass with the size its content wants. Chooses a position the
// first time the panel is shown and leaves it wherever the user has since dragged it. Returns the
// rect to draw into.
tRectangle floating_panel_place(tFloatingPanel * panel, double width, double height);

// Is this coordinate inside the panel? A floating panel's mouse handler must claim ONLY its own
// clicks — the modal versions returned true for everything, which is exactly what stopped a second
// panel, or the canvas, from ever seeing a click.
bool floating_panel_contains(const tFloatingPanel * panel, tCoord coord);

// Press routing. Starts a move when the press lands on the title bar, or anywhere in the panel with
// CTRL held — the whole face is a drag handle then, which is the only practical way to move a panel
// whose title bar is behind another one.
bool floating_panel_press(tFloatingPanel * panel, tCoord coord);

// Call from the cursor-position handler. Returns true while it is actually moving something.
bool floating_panel_drag(tFloatingPanel * panel, tCoord coord);

void floating_panel_release(tFloatingPanel * panel);

// Forget the chosen position, so the next open places the panel afresh. For a panel being closed
// that should not remember where it was — none currently, but a "reset window positions" action
// would want it.
void floating_panel_unplace(tFloatingPanel * panel);

// Bring to the front. Called automatically by floating_panel_press() for any press the panel claims,
// so clicking a panel raises it exactly as a window manager would.
void floating_panel_raise(tFloatingPanel * panel);

// Is a in front of b? Hit-test panels in front-to-back order and draw them back-to-front. A panel
// that has never been raised sorts behind one that has.
bool floating_panel_in_front_of(const tFloatingPanel * a, const tFloatingPanel * b);

// notes §4
typedef struct {
    tFloatingPanel * panel;
    void (*render)(void);
    bool (*mouse)(tCoord coord, tMouseButton mouseButton);

    // Keys are ordered too, not just clicks: Escape has to close the panel you are looking at, and a
    // fixed call order closed whichever handler came first regardless of what was in front.
    bool (*key)(int key, int mods, int action);

    // notes §5
    const bool * visible;
} tFloatingPanelEntry;

// Is this entry's panel currently shown? Reads the flag above, treating NULL as always.
bool floating_panel_entry_visible(const tFloatingPanelEntry * entry);

// notes §6
typedef enum {
    eFloatingPanelPassThrough = 0, // not this panel's press — the caller must return false
    eFloatingPanelConsumed,        // a move started, continued or ended: the caller must return true
    eFloatingPanelContent          // it landed on the panel's own content: the caller handles it
} eFloatingPanelHit;

// notes §7
eFloatingPanelHit floating_panel_mouse(tFloatingPanel * panel, tCoord coord, tMouseButton mouseButton, bool stillOurs);

// Sorts entries BACK TO FRONT: draw in this order, hit-test in reverse.
void floating_panel_sort(tFloatingPanelEntry * entries, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif // __FLOATING_PANEL_H__
