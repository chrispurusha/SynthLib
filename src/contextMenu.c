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

#include <stdatomic.h>
#include <string.h>

#include "synthlibDefs.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "synthlibHost.h"
#include "synthlibGlobals.h"
#include "utils.h"   // get_time_ms(): a clock, which is all glfwGetTime() ever was here
#include "contextMenu.h"

tContextMenu gContextMenu = {0};

// ── Core mechanism ───────────────────────────────────────────────────────────
//
// gContextMenu.frame[0..depth-1] is the stack of currently visible levels —
// frame[0] is the original top-level menu, frame[depth-1] the deepest open
// flyout. Every level stays visible and clickable while a deeper one is open.

// Forward declarations: the scroll strips are consulted by handle_context_menu_click(), which sits
// above the scrolling block that defines them - and moving that block up would put the geometry
// helpers it depends on further from the geometry they belong with.
static int  menu_edge_zone(const tMenuFrame * frame, tCoord coord);
static void context_menu_scroll_frame(tMenuFrame * frame, double rows);

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

// HOW MANY ROWS THIS FRAME HAS, and how many of them fit below where it opens.
//
// A menu that will not fit used to be MOVED up until it did, and once it was taller than the window
// that failed silently: it landed at the top and the surplus ran off the bottom, drawn nowhere and
// clickable never. That is fine while every list is short and becomes a correctness bug the moment
// one is not - a device list is as long as the machine says it is.
static int32_t menu_total_rows(const tMenuFrame * frame) {
    int      count   = 0;
    uint32_t columns = menu_columns(frame);

    while (frame->items[count].label != NULL) {
        count++;
    }

    return (int32_t)(((uint32_t)count + columns - 1) / columns);
}

static double menu_cell_height(void) {
    return STANDARD_TEXT_HEIGHT + (5 * 2);
}

// The rows that fit between where the frame sits and the bottom of the window. Never less than one -
// a frame opened right at the bottom edge is moved up by clamp_menu_to_screen() rather than being
// given zero rows to draw.
static int32_t menu_rows_that_fit(double topY) {
    double available = (get_render_height() / gGlobalGuiScale) - SCROLLBAR_WIDTH - topY;
    int32_t rows     = (int32_t)(available / menu_cell_height());

    return (rows < 1) ? 1 : rows;
}

static bool menu_scrolls(const tMenuFrame * frame) {
    return (frame->visibleRows > 0) && (frame->visibleRows < menu_total_rows(frame));
}

static tRectangle menu_item_rect(const tMenuFrame * frame, int index) {
    double   cellW   = menu_cell_width(frame);
    double   cellH   = STANDARD_TEXT_HEIGHT + (5 * 2);
    uint32_t columns = menu_columns(frame);
    int      col     = index % (int)columns;
    int      row     = index / (int)columns;

    return (tRectangle){
        {
            frame->coord.x + col * cellW,
            frame->coord.y + ((double)row - frame->scrollRow) * cellH
        },
        {
            cellW, cellH
        }
    };
}

// Is this item within the scrolled view? Everything outside is neither drawn nor hit - without the
// second half a click could land on an item that is not on screen.
static bool menu_row_visible(const tMenuFrame * frame, int index) {
    int32_t row = index / (int)menu_columns(frame);

    if (frame->visibleRows <= 0) {
        return true;
    }

    return ((double)row >= frame->scrollRow)
           && ((double)row < (frame->scrollRow + (double)frame->visibleRows));
}

// Returns the index of the item in frame that contains coord, or -1.
static int32_t menu_hit_test(const tMenuFrame * frame, tCoord coord) {
    for (int i = 0; frame->items[i].label != NULL; i++) {
        if (menu_row_visible(frame, i) && within_rectangle(coord, menu_item_rect(frame, i))) {
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

    // MOVE IT UP FIRST, and only scroll what still will not fit. Sliding a menu up costs the user
    // nothing, while scrolling costs them a gesture - so a list that fits anywhere on screen is
    // never made to scroll merely because it opened low down.
    if (frame->coord.y + menuHeight > (renderHeight - SCROLLBAR_WIDTH)) {
        frame->coord.y = (renderHeight - SCROLLBAR_WIDTH) - menuHeight;
    }

    if (frame->coord.y < 0.0) {
        frame->coord.y = 0.0;
    }
    frame->visibleRows = menu_rows_that_fit(frame->coord.y);

    if (frame->visibleRows > rows) {
        frame->visibleRows = rows;      // everything fits; nothing about this frame scrolls
    }

    // Keep the offset inside the list, which also matters on a RESIZE: a window made shorter while
    // a menu is open reduces visibleRows under a scrollRow that was valid a frame ago.
    double maxScroll = (double)(rows - frame->visibleRows);

    if (frame->scrollRow > maxScroll) {
        frame->scrollRow = maxScroll;
    }

    if (frame->scrollRow < 0.0) {
        frame->scrollRow = 0.0;
    }
    double   menuWidth    = menu_cell_width(frame) * (double)cols;

    if (frame->coord.x + menuWidth > renderWidth - SCROLLBAR_WIDTH) {
        frame->coord.x = renderWidth - SCROLLBAR_WIDTH - menuWidth;
    }

    if (frame->coord.x < 0.0) {
        frame->coord.x = 0.0;
    }
}

// A COLLAPSE THAT HAS BEEN ASKED FOR BUT NOT YET DONE. Set when the pointer touches an item at a
// level above the deepest open flyout; acted on only once it has stayed there for
// MENU_SUBMENU_CLOSE_DELAY_SECS. Reaching the flyout, or leaving the items altogether, cancels it.
static int32_t sCollapseArmedFrame = -1;
static int32_t sCollapseArmedIndex = -1;
static double  sCollapseArmedTime  = 0.0;

static void cancel_pending_collapse(void) {
    sCollapseArmedFrame = -1;
    sCollapseArmedIndex = -1;
    sCollapseArmedTime  = 0.0;
}

void open_context_menu(tCoord coord, tMenuItem * items, uint32_t columns, double cellWidth) {
    cancel_pending_collapse();
    gContextMenu.frame[0]       = (tMenuFrame){
        coord, items, columns, cellWidth, 0, 0.0
    };
    gContextMenu.depth          = 1;
    gContextMenu.active         = true;
    gContextMenu.hoverFrame     = -1;
    gContextMenu.hoverIndex     = -1;
    gContextMenu.hoverStartTime = 0.0;
    clamp_menu_to_screen(&gContextMenu.frame[0]);
}

void close_context_menu(void) {
    cancel_pending_collapse();
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
        coord, items, columns, cellWidth, 0, 0.0
    };
    gContextMenu.depth++;
    clamp_menu_to_screen(&gContextMenu.frame[gContextMenu.depth - 1]);
}

bool context_menu_contains(tCoord coord) {
    if (gContextMenu.active == false) {
        return false;
    }

    for (int f = (int)gContextMenu.depth - 1; f >= 0; f--) {
        if (menu_hit_test(&gContextMenu.frame[f], coord) >= 0) {
            return true;
        }
    }

    return false;
}

bool handle_context_menu_click(tCoord coord) {
    if (gContextMenu.active == false) {
        return false;
    }

    for (int f = (int)gContextMenu.depth - 1; f >= 0; f--) {
        tMenuFrame * frame = &gContextMenu.frame[f];

        // A CLICK IN A SCROLL STRIP SCROLLS RATHER THAN CHOOSING, which is what a menu does
        // everywhere else and is also the only way the strips are usable with a trackpad: a tap
        // reports a position and no motion, so without this the tap would pick whatever item happens
        // to sit under the strip. A page at a time, less one row of overlap so nothing is stepped
        // over between pages.
        int zone = menu_edge_zone(frame, coord);

        if (zone != 0) {
            context_menu_scroll_frame(frame, (double)zone * (double)(frame->visibleRows - 1));
            return true;
        }
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
            gContextMenu.hoverStartTime = (get_time_ms() / 1000.0);
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
// HOW A MENU TOO LONG TO FIT IS SCROLLED.
//
// Hovering the top or bottom edge of a scrolling frame scrolls it, continuously, while the pointer
// stays there. That is what a macOS menu does when it outgrows the screen, and it is the right shape
// for this code for a second reason: it needs nothing but the pointer position, which
// update_context_menu_hover() is already given every frame. A dragged scrollbar - SynthLib's own
// idiom for the file and bank browsers - would need mouse-up delivered to the menu, and no app
// routes that here today.
//
// The strip is one cell tall, so it is exactly as big as the thing it scrolls by, and it is only
// live on a frame that actually scrolls: a menu that fits has no edge behaviour at all.
#define MENU_SCROLL_ROWS_PER_SEC    (12.0)

// Which scroll strip a point is in: -1 top, +1 bottom, 0 neither. Only ever non-zero on a frame that
// scrolls AND in the direction there is more to see, so the bottom strip stops being live once the
// end of the list is reached and the last row becomes an ordinary item again.
static int menu_edge_zone(const tMenuFrame * frame, tCoord coord) {
    if (!menu_scrolls(frame)) {
        return 0;
    }
    double cellH   = menu_cell_height();
    double width   = menu_cell_width(frame) * (double)menu_columns(frame);
    double top     = frame->coord.y;
    double bottom  = top + ((double)frame->visibleRows * cellH);
    double maximum = (double)(menu_total_rows(frame) - frame->visibleRows);

    if ((coord.x < frame->coord.x) || (coord.x > (frame->coord.x + width))) {
        return 0;
    }

    if ((coord.y >= top) && (coord.y < (top + cellH)) && (frame->scrollRow > 0.0)) {
        return -1;
    }

    if ((coord.y <= bottom) && (coord.y > (bottom - cellH)) && (frame->scrollRow < maximum)) {
        return 1;
    }

    return 0;
}

static double sLastScrollTime = 0.0;

static void update_menu_edge_scroll(tCoord mouseCoord) {
    double now     = get_time_ms() / 1000.0;
    double elapsed = now - sLastScrollTime;

    sLastScrollTime = now;

    // A frame that has just opened, or a stalled render loop, must not jump the list.
    if ((elapsed <= 0.0) || (elapsed > 0.25)) {
        return;
    }

    for (uint32_t f = 0; f < gContextMenu.depth; f++) {
        tMenuFrame * frame = &gContextMenu.frame[f];

        if (!menu_scrolls(frame)) {
            continue;
        }
        int    zone    = menu_edge_zone(frame, mouseCoord);
        double maximum = (double)(menu_total_rows(frame) - frame->visibleRows);

        if (zone == 0) {
            continue;
        }
        frame->scrollRow += (double)zone * MENU_SCROLL_ROWS_PER_SEC * elapsed;

        if (frame->scrollRow < 0.0) {
            frame->scrollRow = 0.0;
        } else if (frame->scrollRow > maximum) {
            frame->scrollRow = maximum;
        }
        synthlib_request_redraw();
    }
}

static void context_menu_scroll_frame(tMenuFrame * frame, double rows) {
    if (!menu_scrolls(frame)) {
        return;
    }
    double maximum = (double)(menu_total_rows(frame) - frame->visibleRows);

    frame->scrollRow += rows;

    if (frame->scrollRow < 0.0) {
        frame->scrollRow = 0.0;
    } else if (frame->scrollRow > maximum) {
        frame->scrollRow = maximum;
    }
    synthlib_request_redraw();
}

void context_menu_scroll(double rows) {
    if (!gContextMenu.active || (gContextMenu.depth == 0)) {
        return;
    }
    // The deepest frame, which is the one the pointer is working in.
    context_menu_scroll_frame(&gContextMenu.frame[gContextMenu.depth - 1], rows);
}

void update_context_menu_hover(void) {
    if (gContextMenu.active == false) {
        return;
    }
    tCoord  mouseCoord   = {0};

    synthlib_host_mouse_coord(&mouseCoord);

    // BEFORE the hit test below, so the item reported as hovered is the one under the pointer AFTER
    // this frame's scroll rather than the one that was there before it.
    if (!synthlib_host_pointer_captured()) {
        update_menu_edge_scroll(mouseCoord);
    }

    int32_t hitFrame     = -1;
    int32_t hitIndex     = -1;

    for (int f = (int)gContextMenu.depth - 1; (f >= 0) && (hitIndex < 0); f--) {
        int32_t index = menu_hit_test(&gContextMenu.frame[f], mouseCoord);

        if (index >= 0) {
            hitFrame = f;
            hitIndex = index;
        }
    }

    double  now          = (get_time_ms() / 1000.0);

    if (hitIndex < 0) {
        // Off the items entirely — which is most of the journey across to a flyout. The open frames
        // are deliberately left alone (see this function's header), and any armed collapse is
        // dropped: the pointer is no longer on the item that asked for it.
        cancel_pending_collapse();
        gContextMenu.hoverFrame = -1;
        gContextMenu.hoverIndex = -1;
        return;
    }
    // Is the pointer on a level ABOVE the deepest open flyout? That is the case that used to close
    // the flyout on the spot, and is now the case that arms the wait.
    bool    aboveDeepest = (hitFrame < (int)gContextMenu.depth - 1);

    if ((hitFrame != gContextMenu.hoverFrame) || (hitIndex != gContextMenu.hoverIndex)) {
        // The highlight always follows the pointer immediately — only the collapse waits, so the
        // menu still feels live while the flyout it would destroy is given a moment's grace.
        gContextMenu.hoverFrame     = hitFrame;
        gContextMenu.hoverIndex     = hitIndex;
        gContextMenu.hoverStartTime = now;

        if (aboveDeepest) {
            sCollapseArmedFrame = hitFrame;
            sCollapseArmedIndex = hitIndex;
            sCollapseArmedTime  = now;
        } else {
            cancel_pending_collapse();   // arrived at the flyout: it has earned its place
        }
        synthlib_request_redraw();
        return;
    }

    if (aboveDeepest) {
        // Still on the same parent item. Collapse once it has been dwelt on long enough to mean it.
        if (  (sCollapseArmedFrame == hitFrame)
           && (sCollapseArmedIndex == hitIndex)
           && ((now - sCollapseArmedTime) >= MENU_SUBMENU_CLOSE_DELAY_SECS)) {
            pop_menu_frames_to((uint32_t)hitFrame + 1);
            cancel_pending_collapse();
            // Restart this item's own dwell, so its flyout (if it has one) opens a hover delay from
            // NOW rather than instantly on the next tick — otherwise moving along a row of items
            // with submenus would snap the new one open the moment the old one went.
            gContextMenu.hoverStartTime = now;
            synthlib_request_redraw();
        }
        return;
    }
    cancel_pending_collapse();

    tMenuItem * item = &gContextMenu.frame[hitFrame].items[hitIndex];

    if (  (item->subMenu != NULL)
       && (hitFrame == (int)gContextMenu.depth - 1)
       && ((now - gContextMenu.hoverStartTime) >= MENU_HOVER_DELAY_SECS)) {
        tRectangle itemRect = menu_item_rect(&gContextMenu.frame[hitFrame], hitIndex);

        push_menu_frame(side_of_rect(itemRect), item->subMenu, item->subMenuColumns, item->subMenuCellWidth);
        synthlib_request_redraw();
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
        // ONE GEOMETRY FUNCTION. Both passes below used to recompute the cell rectangle inline,
        // which was a second and a third copy of menu_item_rect() - and the moment a frame could
        // scroll, three copies would have had to learn about it together or the menu would draw in
        // one place and be clickable in another.
        if (!menu_row_visible(frameData, i)) {
            continue;
        }
        menuItem = menu_item_rect(frameData, i);

        double x = menuItem.coord.x;
        double y = menuItem.coord.y;

        set_rgb_colour(frameData->items[i].colour);
        render_rectangle(mainArea, menuItem);

        set_rgb_colour(contrasting_text_colour(frameData->items[i].colour));

        if (frameData->items[i].drawItem != NULL) {
            frameData->items[i].drawItem(menuItem, frameData->items[i].param);
        } else {
            render_text(mainArea, (tRectangle){
                {x + 5, y + 5}, {BLANK_SIZE, itemHeight}
            }, frameData->items[i].label);
        }

        if (columns > 1) {
            set_rgb_colour((tRgb)RGB_BLACK);
            render_line(mainArea, (tCoord){x, y}, (tCoord){x + cellW, y}, 1);
            render_line(mainArea, (tCoord){x + cellW, y}, (tCoord){x + cellW, y + cellH}, 1);
            render_line(mainArea, (tCoord){x, y + cellH}, (tCoord){x + cellW, y + cellH}, 1);
            render_line(mainArea, (tCoord){x, y}, (tCoord){x, y + cellH}, 1);
        }
    }

    for (int i = 0; frameData->items[i].label != NULL; i++) {
        if (!menu_row_visible(frameData, i)) {
            continue;
        }
        menuItem = menu_item_rect(frameData, i);

        // Not while the pointer is captured for a drag - the coordinate is a relative-delta
        // accumulator then, not a place on screen. Same reason as menuBar.c; unlikely to be reached
        // (a drag and an open menu rarely coexist) but the defect is identical.
        if (!synthlib_host_pointer_captured() && within_rectangle(mouseCoord, menuItem)) {
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

// THE STANDARD AFFORDANCE FOR A MENU THAT DOES NOT FIT: a strip at the edge with a chevron in it,
// exactly where a macOS menu puts its scroll arrow, drawn OVER the first or last visible row.
//
// Overdrawing a row is not a loss, because a strip is only live while there is more in that
// direction - reach the end of the list and the bottom strip goes inactive, the row beneath it stops
// being swallowed, and the last item is clickable again. So every item is still reachable, which is
// the whole reason the scrolling exists.
static void render_menu_scroll_strips(const tMenuFrame * frame) {
    if (!menu_scrolls(frame)) {
        return;
    }
    double cellH   = menu_cell_height();
    double width   = menu_cell_width(frame) * (double)menu_columns(frame);
    double top     = frame->coord.y;
    double bottom  = top + ((double)frame->visibleRows * cellH);
    double maximum = (double)(menu_total_rows(frame) - frame->visibleRows);
    double midX    = frame->coord.x + (width / 2.0);
    double arm     = 6.0;

    for (int edge = 0; edge < 2; edge++) {
        bool   isTop = (edge == 0);
        bool   live  = isTop ? (frame->scrollRow > 0.0) : (frame->scrollRow < maximum);

        if (!live) {
            continue;
        }
        double stripY = isTop ? top : (bottom - cellH);

        set_rgb_colour((tRgb)RGB_GREY_2);
        render_rectangle(mainArea, (tRectangle){ { frame->coord.x, stripY }, { width, cellH } });

        // A chevron rather than a glyph: the atlas is ASCII, and "^"/"v" read as punctuation at this
        // size rather than as a direction.
        double  tipY  = isTop ? (stripY + (cellH * 0.35)) : (stripY + (cellH * 0.65));
        double  baseY = isTop ? (stripY + (cellH * 0.65)) : (stripY + (cellH * 0.35));

        set_rgb_colour((tRgb)RGB_WHITE);
        render_line(mainArea, (tCoord){ midX - arm, baseY }, (tCoord){ midX, tipY }, 1.5);
        render_line(mainArea, (tCoord){ midX, tipY }, (tCoord){ midX + arm, baseY }, 1.5);
    }
}

void render_context_menu(void) {
    tCoord mouseCoord = {0};

    if (!gContextMenu.active) {
        return;
    }
    synthlib_host_mouse_coord(&mouseCoord);

    for (uint32_t f = 0; f < gContextMenu.depth; f++) {
        render_menu_frame(&gContextMenu.frame[f], mouseCoord);
        render_menu_scroll_strips(&gContextMenu.frame[f]);
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
