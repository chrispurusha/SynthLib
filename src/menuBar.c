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

#include <stdatomic.h>
#include <stddef.h>

#include "synthlibDefs.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "contextMenu.h"
#include "menuBar.h"

// Supplied by the embedding app under these exact names — see menuBar.h.
extern _Atomic bool gReDraw;
void get_global_gui_scaled_mouse_coord(tCoord * coord);

#define MENU_BAR_ITEM_PADDING_X    (15.0)

// Index into the caller's items[] of the top-level label whose dropdown is
// currently open, -1 = none. This is the only state this file owns — the
// dropdown itself is entirely tracked by gContextMenu.
static int32_t sOpenIndex = -1;

static double menu_bar_item_width(const char * label) {
    return get_text_width(label, STANDARD_TEXT_HEIGHT, eNoCache) + (MENU_BAR_ITEM_PADDING_X * 2.0);
}

static tRectangle menu_bar_item_rect(const tMenuBarItem * items, tRectangle bar, int index) {
    double x = bar.coord.x;

    for (int i = 0; i < index; i++) {
        x += menu_bar_item_width(items[i].label);
    }

    return (tRectangle){
        {
            x, bar.coord.y
        }, {
            menu_bar_item_width(items[index].label), bar.size.h
        }
    };
}

static int32_t menu_bar_hit_test(const tMenuBarItem * items, tRectangle bar, tCoord coord) {
    for (int i = 0; items[i].label != NULL; i++) {
        if (within_rectangle(coord, menu_bar_item_rect(items, bar, i))) {
            return i;
        }
    }

    return -1;
}

static void open_menu_bar_item(const tMenuBarItem * items, tRectangle bar, int32_t index) {
    close_context_menu();
    sOpenIndex = index;
    items[index].open(below_rect(menu_bar_item_rect(items, bar, index)));
    gReDraw = true;
}

void render_menu_bar(const tMenuBarItem * items, tRectangle bar) {
    tCoord mouseCoord = {0};

    get_global_gui_scaled_mouse_coord(&mouseCoord);

    if ((sOpenIndex >= 0) && !gContextMenu.active) {
        sOpenIndex = -1;
    }

    set_rgb_colour((tRgb)RGB_GREY_3);
    render_rectangle(mainArea, bar);

    for (int i = 0; items[i].label != NULL; i++) {
        tRectangle itemRect = menu_bar_item_rect(items, bar, i);

        if ((sOpenIndex == i) || within_rectangle(mouseCoord, itemRect)) {
            set_rgb_colour((tRgb)RGB_GREY_5);
            render_rectangle(mainArea, itemRect);
        }

        set_rgb_colour((tRgb)RGB_WHITE);
        render_text(mainArea, (tRectangle){
            {
                itemRect.coord.x + MENU_BAR_ITEM_PADDING_X, itemRect.coord.y + ((itemRect.size.h - STANDARD_TEXT_HEIGHT) / 2.0)
            }, {
                BLANK_SIZE, STANDARD_TEXT_HEIGHT
            }
        }, items[i].label);
    }
}

bool handle_menu_bar_click(const tMenuBarItem * items, tRectangle bar, tCoord coord) {
    if (!within_rectangle(coord, bar)) {
        return false;
    }

    int32_t index = menu_bar_hit_test(items, bar, coord);

    if (index < 0) {
        return true; // Click landed in the bar's background, not on a label
    }

    if ((index == sOpenIndex) && gContextMenu.active) {
        close_context_menu();
        sOpenIndex = -1;
        gReDraw    = true;
        return true;
    }

    open_menu_bar_item(items, bar, index);

    return true;
}

void update_menu_bar_hover(const tMenuBarItem * items, tRectangle bar) {
    if (sOpenIndex < 0) {
        return;
    }

    if (!gContextMenu.active) {
        sOpenIndex = -1;
        return;
    }

    tCoord mouseCoord = {0};

    get_global_gui_scaled_mouse_coord(&mouseCoord);

    int32_t index = menu_bar_hit_test(items, bar, mouseCoord);

    if ((index >= 0) && (index != sOpenIndex)) {
        open_menu_bar_item(items, bar, index);
    }
}

#ifdef __cplusplus
}
#endif
