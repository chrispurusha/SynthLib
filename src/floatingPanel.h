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

#ifndef __FLOATING_PANEL_H__
#define __FLOATING_PANEL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "synthlibTypes.h"

// A panel that sits ON the canvas rather than over it: movable, non-modal, and able to share the
// screen with other panels and with the patch underneath.
//
// The Virtual Keyboard and the Patch Adjuster were both written as MODAL dialogues — each computed
// (renderW - boxW) / 2 every frame, so it re-centred itself continuously and could not be moved, and
// each drew draw_dialog_background_overlay() over the whole window and returned true from its mouse
// handler for every click anywhere. Two of those cannot be open at once in any useful sense. This
// holds the small amount of state that turns such a panel into a floating one, so the behaviour is
// written once instead of once per panel.
//
// The Patch Mutator already floats, by hand, with its own copy of these four fields. It is left
// alone deliberately — it works, and porting it buys nothing but risk — but it is the model this
// follows, and it could be migrated later.
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

    // Stacking order: higher is nearer the front. Panels overlap, so the one drawn on top must also
    // be the one that gets the click — without this the hit-test order is whatever order the
    // handlers happen to be called in, and a panel underneath silently swallows presses aimed at the
    // panel above it.
    uint32_t order;
} tFloatingPanel;

// The area panels are kept inside. Defaults to the whole render area; an app with chrome down an
// edge sets the inner rect instead, and G2-Edit does: its canvas scrollbars sit along the bottom and
// the right, and a panel dragged over them looked wrong — overlapping the TOP bar is fine, since a
// panel has to start somewhere and the bar is not something you scroll.
//
// Set it per frame if it moves with the window; it is one assignment, and a stale bound is worse
// than a recomputed one — the clamp runs every frame precisely so a window resize cannot strand a
// panel outside it.
//
// A panel LARGER than the bounds is pinned to the top-left and allowed to overflow, rather than
// being refused a position it cannot have: at G2-Edit's 640x360 minimum window, Synth Settings is
// taller than the whole canvas.
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

// One floating panel, paired with how to draw it and how to offer it a click. Registering them in an
// array and sorting means the draw order and the hit-test order come from ONE place. Hand-written
// two-way branches worked while there were two panels and become combinatorial at three, which is
// exactly the sort of duplication that lets the two orders drift apart — and if they drift, a panel
// is drawn on top of one that is taking its clicks.
typedef struct {
    tFloatingPanel * panel;
    void (*render)(void);
    bool (*mouse)(tCoord coord, tMouseButton mouseButton);

    // Keys are ordered too, not just clicks: Escape has to close the panel you are looking at, and a
    // fixed call order closed whichever handler came first regardless of what was in front.
    bool (*key)(int key, int mods, int action);
} tFloatingPanelEntry;

// What a press or release on a floating panel turned out to be.
//
// Every panel handler opens with the same three-way question — is this a move, is it mine at all, or
// is it a click on my content? — and every one of them used to answer it by hand, in slightly
// different words. Getting it wrong has two failure modes, both of which have been seen here: claim
// too much and the panel is modal again (the canvas and every other panel go dead while it is open);
// claim too little and a drag started on the title bar is dropped the moment the pointer leaves it.
typedef enum {
    eFloatingPanelPassThrough = 0, // not this panel's press — the caller must return false
    eFloatingPanelConsumed,        // a move started, continued or ended: the caller must return true
    eFloatingPanelContent          // it landed on the panel's own content: the caller handles it
} eFloatingPanelHit;

// The shared preamble. `stillOurs` is for a panel with something outstanding that a release must
// reach even when the pointer has left the panel — the Virtual Keyboard's sounding note is the case
// that proves it: press a key, slide off the panel, release, and without this the note hangs. Pass
// false when the panel has no such state.
eFloatingPanelHit floating_panel_mouse(tFloatingPanel * panel, tCoord coord, tMouseButton mouseButton, bool stillOurs);

// Sorts entries BACK TO FRONT: draw in this order, hit-test in reverse.
void floating_panel_sort(tFloatingPanelEntry * entries, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif // __FLOATING_PANEL_H__
