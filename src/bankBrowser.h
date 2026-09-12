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
// Notes: Docs/code-notes/bankBrowser.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_BANK_BROWSER_H__
#define __SYNTHLIB_BANK_BROWSER_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §1

// notes §2
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
void open_bank_browser(const char * title, const char * message, const char * confirmButtonTitle, const tBankBrowserItem * items, uint32_t itemCount, const char *const * categoryNames, uint32_t categoryNameCount, tBankBrowserCallback callback);

// notes §3
void bank_browser_set_priority_categories(const char *const * categoryNames, uint32_t count);

bool bank_browser_active(void);
void handle_bank_browser_mouse_down(tCoord coord);
bool handle_bank_browser_mouse_move(tCoord coord);
bool handle_bank_browser_click(tCoord coord);
void handle_bank_browser_key(int key, int action);
void handle_bank_browser_scroll(double yDelta);
void update_bank_browser_hover(void);
void render_bank_browser(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_BANK_BROWSER_H__
