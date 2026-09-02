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
// The embedding app must call synthlib_host_init() (synthlibHost.h) once at startup, before
// opening any menu, wiring up a redraw request and the current mouse position (in the same
// logical space menu coords are opened in) — contextMenu.c calls synthlib_request_redraw()/
// synthlib_host_mouse_coord() rather than declaring its own externs for either. It must also call
// update_context_menu_hover() once per frame (e.g. from its main render loop) for the hover-dwell
// timer to elapse even while the mouse sits still, and should keep that loop polling at a short
// timeout (rather than blocking indefinitely on events) while gContextMenu.active is true.

extern tContextMenu gContextMenu;

void open_context_menu(tCoord coord, tMenuItem * items, uint32_t columns, double cellWidth);
void close_context_menu(void);
bool handle_context_menu_click(tCoord coord);
// Is this coordinate over any part of the open menu?
//
// A menu frame is drawn as exactly its grid of item cells — no border or padding beyond them — so
// this is precisely "the pointer is on something the menu painted". Needed because the menu is drawn
// OVER everything on the canvas but, in a host that hit-tests by a fixed sequence of ifs, was tested
// after the canvas chrome underneath it: a right-click menu overlapping a scrollbar or a split bar
// passed its clicks straight through to them, and an item sitting over a scrollbar could not be
// selected at all. Whatever is drawn on top has to be asked first.
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
