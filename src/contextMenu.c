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

#ifdef __cplusplus
extern "C" {
#endif

#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>

#include <stdatomic.h>
#include <string.h>

#include "synthlibDefs.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "contextMenu.h"

// Supplied by the embedding app under these exact names — see contextMenu.h.
extern _Atomic bool gReDraw;
void get_global_gui_scaled_mouse_coord(tCoord * coord);

tContextMenu        gContextMenu = {0};

// ── Core mechanism ───────────────────────────────────────────────────────────
//
// gContextMenu.frame[0..depth-1] is the stack of currently visible levels —
// frame[0] is the original top-level menu, frame[depth-1] the deepest open
// flyout. Every level stays visible and clickable while a deeper one is open.

static double menu_cell_width(const tMenuFrame * frame) {
    if (frame->cellWidth > 0.0) {
        return frame->cellWidth;
    }
    double itemHeight  = STANDARD_TEXT_HEIGHT;
    double largestSize = 0.0;

    for (int i = 0; frame->items[i].label != NULL; i++) {
        double size = get_text_width(frame->items[i].label, itemHeight, eNoCache);

        if (size > largestSize) {
            largestSize = size;
        }
    }

    return (largestSize + (5 * 2) > itemHeight) ? largestSize + (5 * 2) : itemHeight;
}

static uint32_t menu_columns(const tMenuFrame * frame) {
    return (frame->columns > 1) ? frame->columns : 1;
}

static tRectangle menu_item_rect(const tMenuFrame * frame, int index) {
    double   cellW   = menu_cell_width(frame);
    double   cellH   = STANDARD_TEXT_HEIGHT + (5 * 2);
    uint32_t columns = menu_columns(frame);
    int      col     = index % (int)columns;
    int      row     = index / (int)columns;

    return (tRectangle){
        {
            frame->coord.x + col * cellW, frame->coord.y + row * cellH
        },
        {
            cellW, cellH
        }
    };
}

// Returns the index of the item in frame that contains coord, or -1.
static int32_t menu_hit_test(const tMenuFrame * frame, tCoord coord) {
    for (int i = 0; frame->items[i].label != NULL; i++) {
        if (within_rectangle(coord, menu_item_rect(frame, i))) {
            return i;
        }
    }

    return -1;
}

static void clamp_menu_to_screen(tMenuFrame * frame) {
    int      count        = 0;
    double   renderWidth  = get_render_width() / gGlobalGuiScale;
    double   renderHeight = get_render_height() / gGlobalGuiScale;
    double   cellH        = STANDARD_TEXT_HEIGHT + (5 * 2);
    uint32_t cols         = menu_columns(frame);

    while (frame->items[count].label != NULL) {
        count++;
    }
    int      rows         = (count + (int)cols - 1) / (int)cols;
    double   menuHeight   = rows * cellH;

    if (frame->coord.y + menuHeight > (renderHeight - SCROLLBAR_WIDTH)) {
        frame->coord.y = (renderHeight - SCROLLBAR_WIDTH) - menuHeight;
    }

    if (frame->coord.y < 0.0) {
        frame->coord.y = 0.0;
    }
    double   menuWidth    = menu_cell_width(frame) * (double)cols;

    if (frame->coord.x + menuWidth > renderWidth - SCROLLBAR_WIDTH) {
        frame->coord.x = renderWidth - SCROLLBAR_WIDTH - menuWidth;
    }

    if (frame->coord.x < 0.0) {
        frame->coord.x = 0.0;
    }
}

void open_context_menu(tCoord coord, tMenuItem * items, uint32_t columns, double cellWidth) {
    gContextMenu.frame[0]       = (tMenuFrame){
        coord, items, columns, cellWidth
    };
    gContextMenu.depth          = 1;
    gContextMenu.active         = true;
    gContextMenu.hoverFrame     = -1;
    gContextMenu.hoverIndex     = -1;
    gContextMenu.hoverStartTime = 0.0;
    clamp_menu_to_screen(&gContextMenu.frame[0]);
}

void close_context_menu(void) {
    memset(&gContextMenu, 0, sizeof(gContextMenu));
    gContextMenu.hoverFrame = -1;
    gContextMenu.hoverIndex = -1;
}

// Collapses the stack down to newDepth open frames; 0 closes the whole menu.
static void pop_menu_frames_to(uint32_t newDepth) {
    if (newDepth == 0) {
        close_context_menu();
        return;
    }
    gContextMenu.depth = newDepth;
}

// Deliberately leaves gContextMenu.hoverFrame/hoverIndex/hoverStartTime alone:
// the mouse is still physically sitting over whichever item just triggered
// this push (that's true whether the trigger was a click or a hover-dwell), so
// clearing them here would make the very next update_context_menu_hover() tick
// see that item as "newly hovered" and immediately collapse the frame just
// pushed. Callers that push from somewhere other than the current hover
// target (none today) are responsible for updating hover state themselves.
static void push_menu_frame(tCoord coord, tMenuItem * items, uint32_t columns, double cellWidth) {
    if (gContextMenu.depth >= MAX_MENU_DEPTH) {
        return;
    }
    gContextMenu.frame[gContextMenu.depth] = (tMenuFrame){
        coord, items, columns, cellWidth
    };
    gContextMenu.depth++;
    clamp_menu_to_screen(&gContextMenu.frame[gContextMenu.depth - 1]);
}

bool handle_context_menu_click(tCoord coord) {
    if (gContextMenu.active == false) {
        return false;
    }

    for (int f = (int)gContextMenu.depth - 1; f >= 0; f--) {
        tMenuFrame * frame = &gContextMenu.frame[f];
        int32_t      index = menu_hit_test(frame, coord);

        if (index < 0) {
            continue;
        }
        gContextMenu.items = frame->items; // action(index) below reads gContextMenu.items[index].param

        tMenuItem *  item  = &frame->items[index];

        if (item->subMenu != NULL) {
            tRectangle itemRect = menu_item_rect(frame, index);

            pop_menu_frames_to((uint32_t)f + 1);
            push_menu_frame(side_of_rect(itemRect), item->subMenu, item->subMenuColumns, item->subMenuCellWidth);
            gContextMenu.hoverFrame     = f;
            gContextMenu.hoverIndex     = index;
            gContextMenu.hoverStartTime = glfwGetTime();
        } else if (item->action != NULL) {
            void (*action)(int) = item->action;
            action(index);
            close_context_menu();
        } else {
            close_context_menu();
        }
        return true;
    }

    close_context_menu();

    return false;
}

// Called once per frame while gContextMenu.active (see the embedding app's
// main render loop) — tracks which item the mouse is over and, if it has a
// subMenu and the mouse dwells on it for MENU_HOVER_DELAY_SECS, opens it
// exactly as a click would. Hovering a different item at a still-visible
// ancestor level collapses whatever flyout was open beneath it, same as real
// menus.
void update_context_menu_hover(void) {
    if (gContextMenu.active == false) {
        return;
    }
    tCoord  mouseCoord = {0};

    get_global_gui_scaled_mouse_coord(&mouseCoord);

    int32_t hitFrame   = -1;
    int32_t hitIndex   = -1;

    for (int f = (int)gContextMenu.depth - 1; (f >= 0) && (hitIndex < 0); f--) {
        int32_t index = menu_hit_test(&gContextMenu.frame[f], mouseCoord);

        if (index >= 0) {
            hitFrame = f;
            hitIndex = index;
        }
    }

    if (hitIndex < 0) {
        gContextMenu.hoverFrame = -1;
        gContextMenu.hoverIndex = -1;
        return;
    }

    if ((hitFrame != gContextMenu.hoverFrame) || (hitIndex != gContextMenu.hoverIndex)) {
        if (hitFrame < (int)gContextMenu.depth - 1) {
            pop_menu_frames_to((uint32_t)hitFrame + 1);
        }
        gContextMenu.hoverFrame     = hitFrame;
        gContextMenu.hoverIndex     = hitIndex;
        gContextMenu.hoverStartTime = glfwGetTime();
        gReDraw                     = true;
        return;
    }
    tMenuItem * item = &gContextMenu.frame[hitFrame].items[hitIndex];

    if (  (item->subMenu != NULL)
       && (hitFrame == (int)gContextMenu.depth - 1)
       && ((glfwGetTime() - gContextMenu.hoverStartTime) >= MENU_HOVER_DELAY_SECS)) {
        tRectangle itemRect = menu_item_rect(&gContextMenu.frame[hitFrame], hitIndex);

        push_menu_frame(side_of_rect(itemRect), item->subMenu, item->subMenuColumns, item->subMenuCellWidth);
        gReDraw = true;
    }
}

// ── Rendering ────────────────────────────────────────────────────────────────
//
// Renders every currently open level (gContextMenu.frame[0..depth-1]) —
// ancestors are drawn first so the deepest, frontmost flyout paints on top.

static void render_menu_frame(const tMenuFrame * frameData, tCoord mouseCoord) {
    double     size        = 0.0;
    double     largestSize = 0.0;
    tRectangle menuItem    = {0};
    double     itemHeight  = STANDARD_TEXT_HEIGHT;
    uint32_t   columns     = (frameData->columns > 1) ? frameData->columns : 1;

    if (frameData->items == NULL) {
        return;
    }

    for (int i = 0; frameData->items[i].label != NULL; i++) {
        size = get_text_width(frameData->items[i].label, itemHeight, eNoCache);

        if (size > largestSize) {
            largestSize = size;
        }
    }

    double     computed    = (largestSize + (5 * 2) > itemHeight) ? largestSize + (5 * 2) : itemHeight;
    double     cellW       = (frameData->cellWidth > 0.0) ? frameData->cellWidth : computed;
    double     cellH       = itemHeight + (5 * 2);

    for (int i = 0; frameData->items[i].label != NULL; i++) {
        int    col = (int)(i % columns);
        int    row = (int)(i / columns);
        double x   = frameData->coord.x + col * cellW;
        double y   = frameData->coord.y + row * cellH;
        menuItem = (tRectangle){
            {
                x, y
            }, {
                cellW, cellH
            }
        };

        set_rgb_colour(frameData->items[i].colour);
        render_rectangle(mainArea, menuItem);

        set_rgb_colour((tRgb)RGB_WHITE);
        render_text(mainArea, (tRectangle){
            {x + 5, y + 5}, {BLANK_SIZE, itemHeight}
        }, frameData->items[i].label);

        if (columns > 1) {
            set_rgb_colour((tRgb)RGB_BLACK);
            render_line(mainArea, (tCoord){x, y}, (tCoord){x + cellW, y}, 1);
            render_line(mainArea, (tCoord){x + cellW, y}, (tCoord){x + cellW, y + cellH}, 1);
            render_line(mainArea, (tCoord){x, y + cellH}, (tCoord){x + cellW, y + cellH}, 1);
            render_line(mainArea, (tCoord){x, y}, (tCoord){x, y + cellH}, 1);
        }
    }

    for (int i = 0; frameData->items[i].label != NULL; i++) {
        int    col = (int)(i % columns);
        int    row = (int)(i / columns);
        double x   = frameData->coord.x + col * cellW;
        double y   = frameData->coord.y + row * cellH;
        menuItem = (tRectangle){
            {
                x, y
            }, {
                cellW, cellH
            }
        };

        if (within_rectangle(mouseCoord, menuItem)) {
            set_rgb_colour((tRgb)RGB_BLACK);
            render_line(mainArea, (tCoord){menuItem.coord.x, menuItem.coord.y}, (tCoord){menuItem.coord.x + menuItem.size.w, menuItem.coord.y}, 1);
            render_line(mainArea, (tCoord){menuItem.coord.x + menuItem.size.w, menuItem.coord.y}, (tCoord){menuItem.coord.x + menuItem.size.w, menuItem.coord.y + menuItem.size.h}, 1);
            render_line(mainArea, (tCoord){menuItem.coord.x, menuItem.coord.y}, (tCoord){menuItem.coord.x, menuItem.coord.y + menuItem.size.h}, 1);
            render_line(mainArea, (tCoord){menuItem.coord.x, menuItem.coord.y + menuItem.size.h}, (tCoord){menuItem.coord.x + menuItem.size.w, menuItem.coord.y + menuItem.size.h}, 1);
            set_rgb_colour((tRgb)RGB_WHITE);
            render_line(mainArea, (tCoord){menuItem.coord.x + 1, menuItem.coord.y + 1}, (tCoord){(menuItem.coord.x + menuItem.size.w) - 1, menuItem.coord.y + 1}, 1);
            render_line(mainArea, (tCoord){(menuItem.coord.x + menuItem.size.w - 1), menuItem.coord.y + 1}, (tCoord){(menuItem.coord.x + menuItem.size.w) - 1, (menuItem.coord.y + menuItem.size.h) - 1}, 1);
            render_line(mainArea, (tCoord){menuItem.coord.x + 1, menuItem.coord.y + 1}, (tCoord){menuItem.coord.x + 1, (menuItem.coord.y + menuItem.size.h) - 1}, 1);
            render_line(mainArea, (tCoord){menuItem.coord.x + 1, (menuItem.coord.y + menuItem.size.h) - 1}, (tCoord){(menuItem.coord.x + menuItem.size.w) - 1, (menuItem.coord.y + menuItem.size.h) - 1}, 1);
        }
    }
}

void render_context_menu(void) {
    tCoord mouseCoord = {0};

    if (!gContextMenu.active) {
        return;
    }
    get_global_gui_scaled_mouse_coord(&mouseCoord);

    for (uint32_t f = 0; f < gContextMenu.depth; f++) {
        render_menu_frame(&gContextMenu.frame[f], mouseCoord);
    }
}

// ── Helpers ──────────────────────────────────────────────────────────────────

tCoord below_rect(tRectangle r) {
    tCoord c = {r.coord.x, r.coord.y + r.size.h};

    return c;
}

tCoord side_of_rect(tRectangle r) {
    tCoord c = {r.coord.x + r.size.w, r.coord.y};

    return c;
}

#ifdef __cplusplus
}
#endif
