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
// Notes: Docs/code-notes/contextMenu.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_CONTEXT_MENU_H__
#define __SYNTHLIB_CONTEXT_MENU_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §1

extern tContextMenu gContextMenu;

void open_context_menu(tCoord coord, tMenuItem * items, uint32_t columns, double cellWidth);
void close_context_menu(void);
bool handle_context_menu_click(tCoord coord);
// notes §2
bool context_menu_contains(tCoord coord);

void update_context_menu_hover(void);

// Scroll the open menu by a number of ROWS, positive = further down the list. For an app that routes
// a scroll wheel; hovering the top or bottom edge of a menu that does not fit already scrolls it
// without this, so an app that never calls it still reaches every item.
void context_menu_scroll(double rows);
void render_context_menu(void);

tCoord below_rect(tRectangle r);
tCoord side_of_rect(tRectangle r);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_CONTEXT_MENU_H__
