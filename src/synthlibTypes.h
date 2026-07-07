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

#include <stdbool.h>
#include <stdint.h>

#ifndef __SYNTHLIB_TYPES_H__
#define __SYNTHLIB_TYPES_H__

#include "synthlibDefs.h"

typedef struct {
    double x;
    double y;
} tCoord;

typedef struct {
    double w;
    double h;
} tSize;

typedef struct {
    tCoord coord;
    tSize  size;
} tRectangle;

typedef struct {
    tCoord coord1;
    tCoord coord2rel;
    tCoord coord3rel;
} tTriangle;

typedef struct {
    double u1;
    double v1;
    double u2;
    double v2;
    double advance_x;
    int    width;
    int    height;
    int    offset_x;
    int    offset_y;
} GlyphInfo;

typedef struct {
    double     xBar;
    bool       xBarDragging;
    double     xGrabOffset;
    tRectangle xThumb;
    double     yBar;
    bool       yBarDragging;
    double     yGrabOffset;
    tRectangle yThumb;
} tScrollState;

typedef struct {
    double red;
    double green;
    double blue;
} tRgb;

typedef struct {
    double red;
    double green;
    double blue;
    double alpha;
} tRgba;

typedef enum {
    mainArea   = 0,
    moduleArea = 1,  // Only applies to G2-Edit
} tArea;

typedef enum {
    eNoCache = 0,
    eCache   = 1,
} tCache;

// ── Context menu (see contextMenu.h) ────────────────────────────────────────
//
// Deliberately app-agnostic: an item knows only its label, colour, an
// action(index) callback, an opaque param the callback can read back out, and
// optionally a subMenu it opens instead of running that action. Nothing here
// knows what a "module" or "param" is — an app that needs to recall what a
// menu was raised against (e.g. G2-Edit's moduleKey/paramIndex) keeps that in
// its own app-local struct, set before opening the menu and read back from
// inside its own action callbacks.
typedef struct _struct_menuItem {
    char *                    label;
    tRgb                      colour;
    void (*action)(int index);
    uint32_t                  param;
    struct _struct_menuItem * subMenu;          // Non-NULL: hovering (after MENU_HOVER_DELAY_SECS) or clicking this item opens it as a flyout instead of running action
    uint32_t                  subMenuColumns;   // Layout for that flyout — same meaning as tMenuFrame's own columns (0/1 = single column)
    double                    subMenuCellWidth; // Layout for that flyout — 0 = auto width
} tMenuItem;

// One visible level of the menu — the top-level menu plus zero or more
// submenu flyouts opened beneath it (see push_menu_frame() in contextMenu.c).
typedef struct {
    tCoord      coord;      // Position of this level (may be clamped to stay on screen)
    tMenuItem * items;      // Pointer to a NULL-terminated array of menu items
    uint32_t    columns;    // 0 or 1 = single column; >1 = multi-column grid
    double      cellWidth;  // Override cell width when non-zero
} tMenuFrame;

typedef struct {
    bool        active;                // Is any menu level currently visible?
    tCoord      originCoord;           // Original click position that opened the top level, unmodified by clamping
    tMenuFrame  frame[MAX_MENU_DEPTH]; // frame[0] = top-level menu; frame[depth-1] = deepest open flyout
    uint32_t    depth;                 // Number of currently open frames, 0 = menu fully closed
    tMenuItem * items;                 // Array containing the most recently hovered/clicked item — kept in sync by
                                       // handle_context_menu_click()/update_context_menu_hover() so action(index)
                                       // callbacks can read gContextMenu.items[index].param unchanged regardless of
                                       // which frame the item actually lives in
    int32_t hoverFrame;                // Frame index the mouse is currently over an item of, -1 = none
    int32_t hoverIndex;                // Item index within hoverFrame, -1 = none
    double  hoverStartTime;            // glfwGetTime() timestamp when (hoverFrame, hoverIndex) last changed
} tContextMenu;

#endif // __SYNTHLIB_TYPES_H__
