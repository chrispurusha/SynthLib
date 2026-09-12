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
// Notes: Docs/code-notes/synthlibPopups.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_POPUPS_H__
#define __SYNTHLIB_POPUPS_H__

// notes §1

#include "synthlibTypes.h"
#include "menuBar.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §2
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
