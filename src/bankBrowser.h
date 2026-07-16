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

#ifndef __SYNTHLIB_BANK_BROWSER_H__
#define __SYNTHLIB_BANK_BROWSER_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// In-window, cross-platform replacement for the Cocoa NSAlert+NSTableView bank/location picker
// this panel replaced — a scrollable named list with a Bank/Loc, Category, A-Z sort switcher,
// drawn with the same GLFW/OpenGL primitives as the rest of the app. Every call site rebuilds and
// passes its own item list, so the picker has no notion
// of where the data came from — a device-cached name-table sweep (G2-Edit), a live device query,
// or anything else a future caller (Z1-Edit, EmuUtility) might dynamically populate the list from.
//
// The embedding app must provide the same symbols contextMenu.c/fileBrowser.c require (gReDraw,
// get_global_gui_scaled_mouse_coord()) and must, once per frame, call render_bank_browser(); route
// mouse-down through handle_bank_browser_mouse_down() (press-state only, no action); route
// mouse-up through handle_bank_browser_click() ahead of other click handling (returns true if the
// click landed inside the browser); route key events through handle_bank_browser_key(); and route
// scroll-wheel deltas through handle_bank_browser_scroll(). All four are safe to call
// unconditionally — they no-op when bank_browser_active() is false.

// One row's worth of data. name is copied internally before open_bank_browser() returns, so the
// caller's array/strings only need to survive the call itself. category indexes into the
// categoryNames array passed to open_bank_browser() — pass 0xFF (or any value >= categoryNameCount)
// for "no category", which sorts into an "Unknown" bucket under Category mode.
typedef struct {
    const char * name;
    uint8_t      category;
    uint32_t     bank1Indexed;
    uint32_t     location1Indexed;
} tBankBrowserItem;

// confirmed is false if the user cancelled (Close, Cancel, or Escape) — bank1Indexed/
// location1Indexed are only meaningful when confirmed is true.
typedef void (*tBankBrowserCallback)(bool confirmed, uint32_t bank1Indexed, uint32_t location1Indexed);

// Presents items (itemCount entries) as a scrollable, sortable list. categoryNames must have at
// least categoryNameCount entries; pass categoryNameCount 0 to disable Category sort mode entirely
// (e.g. a domain with no meaningful per-item category). message is word-wrapped to the panel width.
void open_bank_browser(const char * title, const char * message, const char * confirmButtonTitle,
                       const tBankBrowserItem * items, uint32_t itemCount,
                       const char * const * categoryNames, uint32_t categoryNameCount,
                       tBankBrowserCallback callback);

bool bank_browser_active(void);
void handle_bank_browser_mouse_down(tCoord coord);
bool handle_bank_browser_click(tCoord coord);
void handle_bank_browser_key(int key, int action);
void handle_bank_browser_scroll(double yDelta);
void render_bank_browser(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_BANK_BROWSER_H__
