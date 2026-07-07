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

#ifndef __SYNTHLIB_CONTEXT_MENU_H__
#define __SYNTHLIB_CONTEXT_MENU_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mac-style nested context menu: click opens the top-level menu; from there,
// clicking or hovering (for MENU_HOVER_DELAY_SECS) an item with a subMenu
// opens it as a flyout beside that item while every ancestor level stays
// visible and clickable; hovering a different item at any still-visible level
// collapses whatever was open beneath it. Clicking a plain item runs its
// action and closes the whole stack.
//
// tMenuItem/tMenuFrame/tContextMenu (see synthlibTypes.h) and everything
// below know only about screen rectangles and an opaque action(index)
// callback — nothing here knows what a "module" or "param" is. An app that
// needs to recall what a menu was raised against keeps that in its own
// app-local struct (see G2-Edit's tMenuContext), set before opening the menu
// and read back inside its own action callbacks.
//
// The embedding app must provide two symbols under these exact names for
// contextMenu.c to link against:
//   _Atomic bool gReDraw;                              — set true to request a redraw
//   void get_global_gui_scaled_mouse_coord(tCoord *);   — current mouse position, in the
//                                                          same logical space menu coords are opened in
// It must also call update_context_menu_hover() once per frame (e.g. from its
// main render loop) for the hover-dwell timer to elapse even while the mouse
// sits still, and should keep that loop polling at a short timeout (rather
// than blocking indefinitely on events) while gContextMenu.active is true.

extern tContextMenu gContextMenu;

void open_context_menu(tCoord coord, tMenuItem * items, uint32_t columns, double cellWidth);
void close_context_menu(void);
bool handle_context_menu_click(tCoord coord);
void update_context_menu_hover(void);
void render_context_menu(void);

tCoord below_rect(tRectangle r);
tCoord side_of_rect(tRectangle r);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_CONTEXT_MENU_H__
