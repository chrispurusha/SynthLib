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

#ifndef __SYNTHLIB_MENU_BAR_H__
#define __SYNTHLIB_MENU_BAR_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Persistent horizontal row of top-level labels (File, Settings, ...), each
// opening its own dropdown through the existing context-menu engine
// (contextMenu.h) rather than duplicating any menu-rendering here. An item's
// open() callback is expected to build its own tMenuItem[] (same
// static-array-per-call pattern already used throughout the app's own
// menus.c dropdowns) and call open_context_menu(anchor, items, columns,
// cellWidth) itself — anchor is the coord this file passes in, already
// positioned below the clicked/hovered label.
//
// items is a NULL-label-terminated array, mirroring tMenuItem.
//
// The embedding app must provide the same two symbols contextMenu.c requires
// (see contextMenu.h) — gReDraw and get_global_gui_scaled_mouse_coord() —
// and must, once per frame:
//   - call update_menu_bar_hover() so moving the mouse from one open
//     top-level label to another switches the dropdown immediately, the way
//     a native menu bar does, without requiring a second click;
//   - call render_menu_bar() to draw the bar and whichever label is
//     currently highlighted/open.
// handle_menu_bar_click() should be called from the app's mouse-down
// routing, ahead of any other click handling for the region the bar
// occupies; it returns true if the click landed inside the bar (whether or
// not it hit a specific label), so the caller knows not to also treat it as
// a click on whatever the bar is drawn over.
typedef struct {
    const char * label;
    void (*open)(tCoord anchor);
} tMenuBarItem;

void render_menu_bar(const tMenuBarItem * items, tRectangle bar);
bool handle_menu_bar_click(const tMenuBarItem * items, tRectangle bar, tCoord coord);
void update_menu_bar_hover(const tMenuBarItem * items, tRectangle bar);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_MENU_BAR_H__
