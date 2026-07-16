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

#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>

#include <algorithm>
#include <string>
#include <vector>

#include "synthlibDefs.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "bankBrowser.h"

// Supplied by the embedding app under these exact names — see bankBrowser.h.
extern "C" _Atomic bool gReDraw;
extern "C" void get_global_gui_scaled_mouse_coord(tCoord * coord);

namespace {

enum tBankBrowserRowKind {
    rowNormal,
    rowSeparator,
    rowHeader,
};

// One row of the flattened, rebuilt-per-sort-mode display list — mirrors fileDialogue.mm's
// G2BankLocationListSource rowLabels/rowKinds/rowBanks/rowLocs, collapsed into one struct.
// itemIndex is -1 for separator/header rows (nothing to select).
struct tBankBrowserRow {
    std::string          label;
    tBankBrowserRowKind  kind;
    int32_t              itemIndex;
};

struct tBankBrowserItemInternal {
    std::string name;
    uint8_t     category;
    uint32_t    bank1Indexed;
    uint32_t    location1Indexed;
};

struct tBankBrowserState {
    bool                 active         = false;
    std::string          title;
    std::string          confirmLabel;
    tBankBrowserCallback callback       = nullptr;

    std::vector<tBankBrowserItemInternal> items;
    std::vector<std::string>              categoryNames;
    std::vector<std::string>              messageLines;

    int32_t               sortMode      = 0;    // 0 = Bank/Loc, 1 = Category, 2 = A-Z
    std::vector<tBankBrowserRow> rows;
    int32_t               selectedRow   = -1;
    double                scrollOffset  = 0.0;

    tRectangle             panelRect     = {0};
    bool                  closePressed  = false;
    bool                  cancelPressed = false;
    bool                  confirmPressed = false;
};

tBankBrowserState sState;

const double kRowHeight      = STANDARD_TEXT_HEIGHT + 8.0;
const int    kVisibleRows    = 10;
const double kPanelWidth     = 480.0;
const double kTitleH         = 26.0;
const double kMessageLineH   = STANDARD_TEXT_HEIGHT + 4.0;

// draw_button() sizes its label text directly off the height of the rectangle passed in (see
// utilsGraphics.cpp), so every button/segment here uses STANDARD_TEXT_HEIGHT for its height,
// matching the text size every other button in the app renders at.
const double kButtonH        = STANDARD_TEXT_HEIGHT;
const double kSortControlH   = STANDARD_TEXT_HEIGHT + 6.0;

double content_x(void) {
    return sState.panelRect.coord.x + 10.0;
}

// render_text() doesn't clip to its rectangle — shortens from the end with an ellipsis until it
// fits maxWidth (same technique as fileBrowser.cpp's truncate_to_width(), duplicated here since
// the two .cpp files don't share a private helper header).
std::string truncate_to_width(const std::string & text, double maxWidth) {
    if (get_text_width(text.c_str(), STANDARD_TEXT_HEIGHT, eNoCache) <= maxWidth) {
        return text;
    }

    std::string truncated = text;

    while (!truncated.empty()) {
        truncated.pop_back();
        std::string withEllipsis = truncated + "...";

        if (get_text_width(withEllipsis.c_str(), STANDARD_TEXT_HEIGHT, eNoCache) <= maxWidth) {
            return withEllipsis;
        }
    }
    return "...";
}

// Greedy word-wrap: breaks text into lines no wider than maxWidth, measured the same way
// render_text() will draw them. There's no wrapping render call anywhere else in the app to
// reuse — every other panel's text is either fixed-length or pre-truncated by hand.
std::vector<std::string> wrap_text(const std::string & text, double maxWidth) {
    std::vector<std::string> lines;
    std::string               current;
    size_t                    pos = 0;

    while (pos <= text.size()) {
        size_t      spacePos = text.find(' ', pos);
        size_t      wordEnd  = (spacePos == std::string::npos) ? text.size() : spacePos;
        std::string word     = text.substr(pos, wordEnd - pos);
        std::string candidate = current.empty() ? word : (current + " " + word);

        if (current.empty() || (get_text_width(candidate.c_str(), STANDARD_TEXT_HEIGHT, eNoCache) <= maxWidth)) {
            current = candidate;
        } else {
            lines.push_back(current);
            current = word;
        }

        if (spacePos == std::string::npos) {
            break;
        }
        pos = spacePos + 1;
    }

    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

const std::string & category_name_for(uint8_t category) {
    static const std::string unknown = "Unknown";

    if ((size_t)category < sState.categoryNames.size()) {
        return sState.categoryNames[(size_t)category];
    }
    return unknown;
}

// Rebuilds the flattened display list from sState.items for the current sState.sortMode — mirrors
// fileDialogue.mm's G2BankLocationListSource.rebuildForSortMode:. mode 0 keeps the caller's raw
// order (assumed Bank/Loc already) with a separator between banks; mode 1 groups alphabetically by
// category with a header row per group (skipped entirely if the caller supplied no category
// names); mode 2 is fully alphabetical with no grouping.
void rebuild_rows(void) {
    std::vector<int32_t> order(sState.items.size());

    for (size_t i = 0; i < order.size(); i++) {
        order[i] = (int32_t)i;
    }

    if ((sState.sortMode == 1) && !sState.categoryNames.empty()) {
        std::sort(order.begin(), order.end(), [](int32_t a, int32_t b) {
            const std::string & ca = category_name_for(sState.items[(size_t)a].category);
            const std::string & cb = category_name_for(sState.items[(size_t)b].category);

            if (ca != cb) {
                return ca < cb;
            }
            return sState.items[(size_t)a].name < sState.items[(size_t)b].name;
        });
    } else if (sState.sortMode == 2) {
        std::sort(order.begin(), order.end(), [](int32_t a, int32_t b) {
            return sState.items[(size_t)a].name < sState.items[(size_t)b].name;
        });
    }
    // sortMode 0: raw order, nothing to sort.

    bool        groupByCategory = (sState.sortMode == 1) && !sState.categoryNames.empty();
    std::string lastGroupKey;
    bool        haveLastGroupKey = false;

    sState.rows.clear();

    for (int32_t idx : order) {
        const tBankBrowserItemInternal & item = sState.items[(size_t)idx];
        std::string groupKey  = groupByCategory ? category_name_for(item.category) : std::to_string(item.bank1Indexed);
        bool        newGroup  = !haveLastGroupKey || (groupKey != lastGroupKey);

        if ((sState.sortMode == 0) || groupByCategory) {
            if (newGroup) {
                if (groupByCategory) {
                    sState.rows.push_back({groupKey, rowHeader, -1});
                } else if (haveLastGroupKey) {
                    sState.rows.push_back({"", rowSeparator, -1});
                }
                lastGroupKey     = groupKey;
                haveLastGroupKey = true;
            }
        }

        std::string label = "Bank " + std::to_string(item.bank1Indexed) + ", Loc " +
                            std::to_string(item.location1Indexed) + ": " + item.name;
        sState.rows.push_back({label, rowNormal, idx});
    }

    sState.selectedRow  = -1;
    sState.scrollOffset = 0.0;
}

tRectangle close_button_rect(void) {
    double w = get_text_width("Close", kButtonH, eCache) + 4.0;

    return (tRectangle){
        {
            sState.panelRect.coord.x + sState.panelRect.size.w - w - 8.0 - BORDER_LINE_WIDTH, sState.panelRect.coord.y + 4.0
        }, {
            w, kButtonH
        }
    };
}

double message_y(void) {
    return sState.panelRect.coord.y + kTitleH + 10.0;
}

tRectangle sort_button_rect(int index) {
    double totalW = sState.panelRect.size.w - 20.0;
    double gap    = 6.0;
    double w      = (totalW - (2.0 * gap)) / 3.0;
    double x      = content_x() + ((double)index * (w + gap));
    double y      = message_y() + ((double)sState.messageLines.size() * kMessageLineH) + 8.0;

    return (tRectangle){
        {
            x, y
        }, {
            w, kSortControlH
        }
    };
}

tRectangle list_area_rect(void) {
    tRectangle sortRect = sort_button_rect(0);
    double     y         = sortRect.coord.y + kSortControlH + 8.0;

    return (tRectangle){
        {
            content_x(), y
        }, {
            sState.panelRect.size.w - 20.0, kRowHeight * kVisibleRows
        }
    };
}

double button_row_y(void) {
    return sState.panelRect.coord.y + sState.panelRect.size.h - 10.0 - kButtonH;
}

// fromRight counts button-widths in from the panel's right edge — 0 is rightmost. x is computed
// from the button's right edge inward so it can never extend past the panel (matches
// fileBrowser.cpp's button_rect(), which fixed the same bug once already).
tRectangle button_rect(int fromRight, double y) {
    double w         = 64.0;
    double gap       = 8.0;
    double rightEdge = sState.panelRect.coord.x + sState.panelRect.size.w - 10.0 - ((double)fromRight * (w + gap));

    return (tRectangle){
        {
            rightEdge - w, y
        }, {
            w, kButtonH
        }
    };
}

int32_t row_count_scrolled(void) {
    return (int32_t)((sState.rows.size() > (size_t)kVisibleRows) ? (sState.rows.size() - (size_t)kVisibleRows) : 0);
}

void finish_browse(bool confirmed, uint32_t bank1Indexed, uint32_t location1Indexed) {
    tBankBrowserCallback callback = sState.callback;

    sState.active   = false;
    sState.callback = nullptr;
    gReDraw         = true;

    if (callback != nullptr) {
        callback(confirmed, bank1Indexed, location1Indexed);
    }
}

void confirm_current(void) {
    if ((sState.selectedRow < 0) || (sState.selectedRow >= (int32_t)sState.rows.size())) {
        return;
    }
    const tBankBrowserRow & row = sState.rows[(size_t)sState.selectedRow];

    if ((row.kind != rowNormal) || (row.itemIndex < 0)) {
        return;
    }
    const tBankBrowserItemInternal & item = sState.items[(size_t)row.itemIndex];

    finish_browse(true, item.bank1Indexed, item.location1Indexed);
}

} // namespace

extern "C" {

void open_bank_browser(const char * title, const char * message, const char * confirmButtonTitle,
                       const tBankBrowserItem * items, uint32_t itemCount,
                       const char * const * categoryNames, uint32_t categoryNameCount,
                       tBankBrowserCallback callback) {
    sState.active       = true;
    sState.title        = (title != nullptr) ? title : "";
    sState.confirmLabel = ((confirmButtonTitle != nullptr) && (confirmButtonTitle[0] != '\0')) ? confirmButtonTitle : "Next...";
    sState.callback     = callback;

    sState.items.clear();
    sState.items.reserve(itemCount);

    for (uint32_t i = 0; i < itemCount; i++) {
        sState.items.push_back({
            (items[i].name != nullptr) ? items[i].name : "", items[i].category,
            items[i].bank1Indexed, items[i].location1Indexed
        });
    }

    sState.categoryNames.clear();
    sState.categoryNames.reserve(categoryNameCount);

    for (uint32_t i = 0; i < categoryNameCount; i++) {
        sState.categoryNames.push_back((categoryNames[i] != nullptr) ? categoryNames[i] : "");
    }

    sState.sortMode = 0;
    rebuild_rows();

    sState.messageLines = wrap_text((message != nullptr) ? message : "", kPanelWidth - 20.0);

    double messageH    = (double)sState.messageLines.size() * kMessageLineH;
    double panelHeight = kTitleH + 10.0 + messageH + 8.0 + kSortControlH + 8.0 +
                         (kRowHeight * kVisibleRows) + 10.0 + kButtonH + 10.0;

    double renderW = get_render_width() / gGlobalGuiScale;
    double renderH = get_render_height() / gGlobalGuiScale;

    sState.panelRect = (tRectangle){
        {
            (renderW - kPanelWidth) / 2.0, (renderH - panelHeight) / 2.0
        }, {
            kPanelWidth, panelHeight
        }
    };

    gReDraw = true;
}

bool bank_browser_active(void) {
    return sState.active;
}

// Called on mouse-down while the browser is active so Close/Cancel/Confirm can show a pressed
// state while held — matches the rest of the app's convention (gTopbarControls[i].isPressed,
// fileBrowser.cpp's sState.closePressed, ...) of darkening a button's fill from mouse-down to
// mouse-up rather than only reacting on click.
void handle_bank_browser_mouse_down(tCoord coord) {
    if (!sState.active) {
        return;
    }
    sState.closePressed   = within_rectangle(coord, close_button_rect());
    sState.cancelPressed  = within_rectangle(coord, button_rect(1, button_row_y()));
    sState.confirmPressed = within_rectangle(coord, button_rect(0, button_row_y()));
    gReDraw = true;
}

bool handle_bank_browser_click(tCoord coord) {
    if (!sState.active) {
        return false;
    }
    sState.closePressed   = false;
    sState.cancelPressed  = false;
    sState.confirmPressed = false;

    if (!within_rectangle(coord, sState.panelRect)) {
        return true; // Modal — swallow clicks outside without closing (matches other G2-Edit popups)
    }

    if (within_rectangle(coord, close_button_rect())) {
        finish_browse(false, 0, 0);
        return true;
    }

    for (int i = 0; i < 3; i++) {
        if (within_rectangle(coord, sort_button_rect(i))) {
            bool disabled = (i == 1) && sState.categoryNames.empty();

            if (!disabled && (sState.sortMode != i)) {
                sState.sortMode = i;
                rebuild_rows();
                gReDraw          = true;
            }
            return true;
        }
    }

    tRectangle listRect = list_area_rect();

    if (within_rectangle(coord, listRect)) {
        int32_t rowInView = (int32_t)((coord.y - listRect.coord.y) / kRowHeight);
        int32_t index      = rowInView + (int32_t)sState.scrollOffset;

        if ((index >= 0) && (index < (int32_t)sState.rows.size()) && (sState.rows[(size_t)index].kind == rowNormal)) {
            sState.selectedRow = index;
            gReDraw              = true;
        }
        return true;
    }

    bool wantsConfirm = within_rectangle(coord, button_rect(0, button_row_y()));
    bool wantsCancel  = within_rectangle(coord, button_rect(1, button_row_y()));

    if (wantsCancel) {
        finish_browse(false, 0, 0);
        return true;
    }

    if (wantsConfirm) {
        confirm_current();
        return true;
    }

    return true;
}

void handle_bank_browser_key(int key, int action) {
    if (!sState.active) {
        return;
    }

    if ((action != GLFW_PRESS) && (action != GLFW_REPEAT)) {
        return;
    }

    if (key == GLFW_KEY_ESCAPE) {
        finish_browse(false, 0, 0);
        return;
    }

    if ((key == GLFW_KEY_ENTER) || (key == GLFW_KEY_KP_ENTER)) {
        confirm_current();
        return;
    }
}

void handle_bank_browser_scroll(double yDelta) {
    if (!sState.active) {
        return;
    }
    sState.scrollOffset -= yDelta;
    sState.scrollOffset  = std::max(0.0, std::min(sState.scrollOffset, (double)row_count_scrolled()));
    gReDraw = true;
}

void render_bank_browser(void) {
    if (!sState.active) {
        return;
    }

    tCoord mouseCoord = {0};

    get_global_gui_scaled_mouse_coord(&mouseCoord);

    // Dim background overlay — solid, not translucent, matching every other modal panel in the
    // app (see fileBrowser.cpp's identical comment).
    set_rgb_colour((tRgb)RGB_GREY_2);
    render_rectangle(mainArea, (tRectangle){
        {0.0, 0.0}, {get_render_width() / gGlobalGuiScale, get_render_height() / gGlobalGuiScale}
    });

    // Panel chrome — replicates graphics.cpp's draw_panel_chrome()/draw_panel_close_button()
    // pixel-for-pixel (this file can't call those G2-Edit-local helpers directly).
    set_rgb_colour((tRgb)RGB_GREY_5);
    render_rectangle_with_border(mainArea, sState.panelRect);
    set_rgb_colour((tRgb)RGB_GREY_3);
    render_rectangle(mainArea, (tRectangle){
        {sState.panelRect.coord.x + BORDER_LINE_WIDTH, sState.panelRect.coord.y + BORDER_LINE_WIDTH},
        {sState.panelRect.size.w - (2.0 * BORDER_LINE_WIDTH), kTitleH - BORDER_LINE_WIDTH}
    });
    set_rgb_colour((tRgb)RGB_WHITE);
    render_text(mainArea, (tRectangle){
        {sState.panelRect.coord.x + 10.0, sState.panelRect.coord.y + 6.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
    }, sState.title.c_str());

    draw_button(mainArea, close_button_rect(), "Close", sState.closePressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY);

    // Message
    set_rgb_colour((tRgb)RGB_BLACK);
    for (size_t i = 0; i < sState.messageLines.size(); i++) {
        render_text(mainArea, (tRectangle){
            {content_x(), message_y() + ((double)i * kMessageLineH)}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, sState.messageLines[i].c_str());
    }

    // Sort segmented control — active segment highlighted green, same "exclusive highlight"
    // convention as the topbar's VA/FX buttons; a Category segment with no category names supplied
    // renders permanently grey/disabled rather than clickable.
    static const char *const kSortLabels[3] = {"Bank/Loc", "Category", "A-Z"};

    for (int i = 0; i < 3; i++) {
        bool disabled = (i == 1) && sState.categoryNames.empty();
        bool active   = (sState.sortMode == i) && !disabled;
        tRgb colour   = disabled ? (tRgb)RGB_GREY_5 : (active ? (tRgb)RGB_GREEN_ON : (tRgb)RGB_BACKGROUND_GREY);

        draw_button(mainArea, sort_button_rect(i), kSortLabels[i], colour);
    }

    // List
    tRectangle listRect = list_area_rect();

    set_rgb_colour((tRgb)RGB_WHITE);
    render_rectangle(mainArea, listRect);
    set_rgb_colour((tRgb)RGB_GREY_5);
    render_rectangle_with_border(mainArea, listRect);

    for (int row = 0; row < kVisibleRows; row++) {
        int32_t index = row + (int32_t)sState.scrollOffset;

        if ((index < 0) || (index >= (int32_t)sState.rows.size())) {
            continue;
        }
        const tBankBrowserRow & r       = sState.rows[(size_t)index];
        tRectangle               rowRect = {
            {
                listRect.coord.x, listRect.coord.y + ((double)row * kRowHeight)
            }, {
                listRect.size.w, kRowHeight
            }
        };

        if (r.kind == rowSeparator) {
            set_rgb_colour((tRgb)RGB_GREY_5);
            render_rectangle(mainArea, (tRectangle){
                {rowRect.coord.x + 4.0, rowRect.coord.y + (kRowHeight / 2.0) - 1.0}, {rowRect.size.w - 8.0, 1.0}
            });
            continue;
        }

        if (r.kind == rowHeader) {
            set_rgb_colour((tRgb)RGB_GREY_3);
            render_text(mainArea, (tRectangle){
                {rowRect.coord.x + 6.0, rowRect.coord.y + 4.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
            }, truncate_to_width(r.label, rowRect.size.w - 12.0).c_str());
            continue;
        }

        bool selected = (index == sState.selectedRow);
        bool hovered   = within_rectangle(mouseCoord, rowRect);

        if (selected) {
            set_rgb_colour((tRgb)RGB_ORANGE_2);
            render_rectangle(mainArea, rowRect);
        } else if (hovered) {
            set_rgb_colour((tRgb)RGB_GREY_7);
            render_rectangle(mainArea, rowRect);
        }

        set_rgb_colour((tRgb)RGB_BLACK);
        render_text(mainArea, (tRectangle){
            {rowRect.coord.x + 6.0, rowRect.coord.y + 4.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, truncate_to_width(r.label, rowRect.size.w - 12.0).c_str());
    }

    // Confirm / Cancel buttons — Confirm rightmost (primary action), Cancel to its left.
    double buttonY        = button_row_y();
    bool   confirmEnabled = (sState.selectedRow >= 0) && (sState.selectedRow < (int32_t)sState.rows.size()) &&
                            (sState.rows[(size_t)sState.selectedRow].kind == rowNormal);
    tRgb   confirmColour  = sState.confirmPressed ? (tRgb)RGB_GREY_7 : (confirmEnabled ? (tRgb)RGB_GREEN_ON : (tRgb)RGB_GREY_5);
    tRgb   cancelColour   = sState.cancelPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY;

    draw_button(mainArea, button_rect(0, buttonY), sState.confirmLabel.c_str(), confirmColour);
    draw_button(mainArea, button_rect(1, buttonY), "Cancel", cancelColour);
}

} // extern "C"
