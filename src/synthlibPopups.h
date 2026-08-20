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

#ifndef __SYNTHLIB_POPUPS_H__
#define __SYNTHLIB_POPUPS_H__

// THE POPUP LIFECYCLE COORDINATOR.
//
// SynthLib has owned the popup WIDGETS for a long time — the file and bank browsers, the alert
// dialog, the context menu, the menu bar — but not their ORCHESTRATION, so each application
// re-implemented the same four things:
//
//   * the render order, back to front, as a hand-kept sequence of calls;
//   * the modal click cascade: if (file_browser_active()) {...return;} if (bank_browser_active())
//     {...} if (alert_dialog_active()) {...} — 13 routing references in G2-Edit's mouseHandle.c and
//     11 in SynthEdit's, character-identical between them;
//   * the same cascade again for keys;
//   * a per-frame hover tick the host has to remember to call for each popup.
//
// That last one had already shipped real bugs rather than merely being untidy: SynthEdit's own
// comments record hover handlers that were "never actually called anywhere until now", twice. An
// order-dependent contract spread across three hosts is fragile by construction — nothing anywhere
// states the order, so nothing can check it.
//
// So the order lives HERE, as data, and it is a LAYER on each popup rather than a call sequence.
// That is the part that makes it more than a tidy-up: an application registers its own panels into
// the same ordering as SynthLib's, so a host's popups and the library's cannot disagree about who is
// in front — and the layer that decides drawing is by construction the layer that decides which
// popup gets the click.
//
// FLOATING PANELS KEEP THEIR OWN REGISTRY (floatingPanel.h) and should still be registered there:
// their order among THEMSELVES is dynamic — they raise on click, so it is a property of the panels
// rather than a constant, and no fixed layer could describe it. What belongs here is where that
// whole group sits relative to everything else, which IS a constant. A host registers one entry
// whose mouse handler walks its own sorted panel list; see G2-Edit's "floatingPanels".
//
// That is what let the menu bar's clicks move in here at all — see the note above gLibPopups in the
// .c. Two orderings, each authoritative over a different thing, is the arrangement; two orderings
// that both claim the same thing is the bug this file exists to remove.

#include "synthlibTypes.h"
#include "menuBar.h"

#ifdef __cplusplus
extern "C" {
#endif

// Layers for SynthLib's own popups, exposed so an application can place its panels BETWEEN them
// rather than guessing a number. Higher is nearer the front.
//
// The gaps are deliberate and are the whole point: G2-Edit's patch-notes editor and its progress
// panels belong above the context menu and below the browsers, and its device-busy overlay belongs
// above the browsers and below the alert. Before this, that was expressed only by the order of five
// calls in one function — correct, unstated, and one careless insertion away from being wrong.
#define SYNTHLIB_POPUP_LAYER_MENU_BAR        (100)
#define SYNTHLIB_POPUP_LAYER_CONTEXT_MENU    (200)
#define SYNTHLIB_POPUP_LAYER_BROWSERS        (400)
#define SYNTHLIB_POPUP_LAYER_ALERT           (600)

typedef struct {
    const char * name;                // for diagnostics; never rendered
    int          layer;               // higher is nearer the front — see the constants above
    bool         modal;               // while active, swallows input aimed at anything behind it

    bool (*active)(void);             // NULL means "always active", i.e. render decides for itself
    void (*render)(void);             // NULL for a popup that draws itself elsewhere
    void (*tick)(void);               // per-frame hover/dwell update; NULL if it has none

    // Return true if the event was consumed. A modal popup should consume everything that reaches
    // it, whether or not the coordinate landed on it — that is what modal means.
    bool (*mouse)(tCoord coord, tMouseButton mouseButton);
    bool (*key)(int key, int mods, int action);

    // The other two input channels, which are cascaded by hand in every host for exactly the same
    // reason the clicks were. Only the browsers implement scroll; only the file browser takes text.
    bool (*scroll)(double yDelta);
    bool (*character)(unsigned int codepoint);
} tSynthLibPopup;

// Registers the application's own popups. SynthLib's five are always present and need no
// registration; these are merged into the same layer ordering. The array is NOT copied — pass
// something with static storage duration.
void synthlib_popups_register(const tSynthLibPopup * popups, uint32_t count);

// The application's menu bar, which is the one SynthLib popup that cannot be self-describing: its
// items and its rectangle belong to the host. barRect is a function rather than a value because the
// bar moves with the window.
void synthlib_popups_set_menu_bar(const tMenuBarItem * items, tRectangle (*barRect)(void));

// Draws every registered popup that is active, back to front. One call replaces the hand-kept
// sequence each host used to carry.
void synthlib_popups_render(void);

// Per-frame update for hover and dwell. Replaces the host remembering to call
// update_context_menu_hover() / update_menu_bar_hover() / update_bank_browser_hover() itself — the
// omission that has already shipped twice.
void synthlib_popups_tick(void);

// Offers the event to registered popups, front to back, stopping at the first that consumes it or
// at the frontmost modal popup, whichever comes first. Returns true if the host should treat the
// event as handled and do nothing further with it.
bool synthlib_popups_dispatch_click(tCoord coord, tMouseButton mouseButton);
bool synthlib_popups_dispatch_key(int key, int mods, int action);
bool synthlib_popups_dispatch_scroll(double yDelta);
bool synthlib_popups_dispatch_char(unsigned int codepoint);

// Is a modal popup up? For a host that needs to suppress something of its own — a canvas drag, a
// keyboard shortcut — without caring which popup it is.
bool synthlib_popups_modal_active(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_POPUPS_H__
