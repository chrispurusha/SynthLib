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
#include "synthlibHost.h"
#include "synthlibGlobals.h"
#include "contextMenu.h"
#include "alertDialog.h"

namespace
{

enum tAlertKind {
    alertKindInfo,
    alertKindConfirm,
    alertKindBankConfirm,
    alertKindChoice,
};

struct tAlertState {
    bool                      active               = false;
    tAlertKind                kind                 = alertKindInfo;
    std::string               title;
    std::vector<std::string>  messageLines;
    std::string               confirmLabel;
    std::string               fieldLabel;
    uint32_t                  selectedBank1Indexed = 1;
    uint32_t                  maxBank1Indexed      = 1;
    tAlertConfirmCallback     confirmCallback      = nullptr;
    tAlertBankConfirmCallback bankCallback         = nullptr;

    // alertKindChoice only: labels right-to-left (choiceLabel[0] is rightmost), unused ones empty.
    std::string               choiceLabel[3];
    uint32_t                  choiceCount          = 0;
    int                       choicePressed        = -1;
    tAlertChoiceCallback      choiceCallback       = nullptr;

    tRectangle                panelRect            = {0};
    bool                      closePressed         = false;
    bool                      cancelPressed        = false;
    bool                      confirmPressed       = false;
    bool                      pickerPressed        = false;

    // Backing storage for the bank-picker dropdown's items — bankLabels must never be resized
    // once bankMenuItems has been built from it (see build_bank_picker_items()): growing a
    // std::vector<std::string> can relocate every element's character buffer, invalidating any
    // c_str() pointer already handed to a tMenuItem.
    std::vector<std::string> bankLabels;
    std::vector<tMenuItem>   bankMenuItems;
};

tAlertState  sState;

const double kPanelWidth   = 420.0;
const double kTitleH       = 26.0;
const double kMessageLineH = STANDARD_TEXT_HEIGHT + 4.0;

// draw_button() sizes its label text directly off the height of the rectangle passed in (see
// utilsGraphics.cpp), so every button here uses STANDARD_TEXT_HEIGHT for its height, matching the
// text size every other button in the app renders at.
const double kButtonH      = STANDARD_TEXT_HEIGHT;

double content_x(void) {
    return sState.panelRect.coord.x + 10.0;
}

double message_y(void) {
    return sState.panelRect.coord.y + kTitleH + 10.0;
}

// Greedy word-wrap: breaks text into lines no wider than maxWidth, measured the same way
// render_text() will draw them (same technique bankBrowser.cpp uses for its own message text).
// A '\n' in the caller's message is a HARD break (and an empty line between two of them is a
// blank line, i.e. a paragraph gap). Without this the newline is swallowed into whichever word
// it is touching and two sentences render as "disagree.Your edits" — nothing tells the reader
// where the caller meant one thought to end and the next to begin.
std::vector<std::string> wrap_paragraph(const std::string &text, double maxWidth) {
    std::vector<std::string> lines;
    std::string              current;
    size_t                   pos = 0;

    while (pos <= text.size()) {
        size_t      spacePos  = text.find(' ', pos);
        size_t      wordEnd   = (spacePos == std::string::npos) ? text.size() : spacePos;
        std::string word      = text.substr(pos, wordEnd - pos);
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

std::vector<std::string> wrap_text(const std::string &text, double maxWidth) {
    std::vector<std::string> lines;
    size_t                   pos = 0;

    while (pos <= text.size()) {
        size_t      breakPos  = text.find('\n', pos);
        size_t      lineEnd   = (breakPos == std::string::npos) ? text.size() : breakPos;
        std::string paragraph = text.substr(pos, lineEnd - pos);

        if (paragraph.empty()) {
            lines.push_back("");  // Deliberate blank line between paragraphs
        } else {
            std::vector<std::string> wrapped = wrap_paragraph(paragraph, maxWidth);

            lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        }

        if (breakPos == std::string::npos) {
            break;
        }
        pos = breakPos + 1;
    }
    return lines;
}

tRectangle close_button_rect(void) {
    double w = get_text_width("Close", kButtonH, eCache) + 4.0;

    return (tRectangle){
        {
            sState.panelRect.coord.x + sState.panelRect.size.w - w - 8.0 - BORDER_LINE_WIDTH, sState.panelRect.coord.y + 4.0
        }, {w, kButtonH}
    };
}

tRectangle picker_row_rect(void) {
    double y = message_y() + ((double)sState.messageLines.size() * kMessageLineH) + 8.0;

    return (tRectangle){
        {
            content_x(), y
        }, {sState.panelRect.size.w - 20.0, kButtonH}
    };
}

tRectangle picker_button_rect(void) {
    tRectangle row = picker_row_rect();
    double     w   = 100.0;

    return (tRectangle){
        {
            row.coord.x + row.size.w - w, row.coord.y
        }, {w, kButtonH}
    };
}

double button_row_y(void) {
    return sState.panelRect.coord.y + sState.panelRect.size.h - 10.0 - kButtonH;
}

// fromRight counts button-widths in from the panel's right edge — 0 is rightmost. x is computed
// from the button's right edge inward so it can never extend past the panel (matches
// fileBrowser.cpp/bankBrowser.cpp's identical button_rect()).
tRectangle button_rect(int fromRight, double y) {
    double w         = 64.0;
    double gap       = 8.0;
    double rightEdge = sState.panelRect.coord.x + sState.panelRect.size.w - 10.0 - ((double)fromRight * (w + gap));

    return (tRectangle){
        {
            rightEdge - w, y
        }, {w, kButtonH}
    };
}

// Rebuilds the bank-picker dropdown's items for the current sState.maxBank1Indexed. Must run
// before the picker button can be clicked (called from show_bank_confirm()) — see tAlertState's
// bankLabels/bankMenuItems comment for why the two loops below can't be merged.
void on_bank_picker_item_chosen(int index) {
    sState.selectedBank1Indexed = gContextMenu.items[index].param;
    synthlib_request_redraw();
}

void build_bank_picker_items(void) {
    sState.bankLabels.clear();
    sState.bankLabels.reserve(sState.maxBank1Indexed);

    for (uint32_t i = 1; i <= sState.maxBank1Indexed; i++) {
        sState.bankLabels.push_back("Bank " + std::to_string(i));
    }

    sState.bankMenuItems.clear();
    sState.bankMenuItems.reserve((size_t)sState.maxBank1Indexed + 1);

    for (uint32_t i = 0; i < sState.maxBank1Indexed; i++) {
        tMenuItem item = {0};

        item.label  = const_cast<char *>(sState.bankLabels[i].c_str());
        item.colour = (tRgb)RGB_GREY_3;
        item.action = on_bank_picker_item_chosen;
        item.param  = i + 1;
        sState.bankMenuItems.push_back(item);
    }

    tMenuItem terminator = {0};
    sState.bankMenuItems.push_back(terminator);
}

// choice is meaningful only for alertKindChoice; every other kind passes -1 and is driven by
// `confirmed`. Dismissal (Close/Escape) reaches a choice dialog as confirmed == false.
void finish_choice_dialog(bool confirmed, int choice) {
    tAlertKind                kind      = sState.kind;
    tAlertConfirmCallback     confirmCb = sState.confirmCallback;
    tAlertBankConfirmCallback bankCb    = sState.bankCallback;
    tAlertChoiceCallback      choiceCb  = sState.choiceCallback;
    uint32_t                  bank      = sState.selectedBank1Indexed;

    sState.active          = false;
    sState.confirmCallback = nullptr;
    sState.bankCallback    = nullptr;
    sState.choiceCallback  = nullptr;
    synthlib_request_redraw();

    if ((kind == alertKindChoice) && (choiceCb != nullptr)) {
        choiceCb(confirmed ? choice : -1);
        return;
    }

    if ((kind == alertKindConfirm) && (confirmCb != nullptr)) {
        confirmCb(confirmed);
    } else if ((kind == alertKindBankConfirm) && (bankCb != nullptr)) {
        bankCb(confirmed, bank);
    }
}

// The pre-existing two-button entry point. On a choice dialog, Enter/affirmative means the
// rightmost button, which is the one show_choice()'s caller passed first.
void finish_dialog(bool confirmed) {
    finish_choice_dialog(confirmed, 0);
}

double choice_row_width(void);  // Defined below; begin_dialog() sizes the panel around it

void begin_dialog(tAlertKind kind, const char * title, const char * message, const char * confirmLabel) {
    sState.active         = true;
    sState.kind           = kind;
    sState.title          = (title != nullptr) ? title : "";
    sState.confirmLabel   = ((confirmLabel != nullptr) && (confirmLabel[0] != '\0')) ? confirmLabel : "OK";

    // A choice dialog's buttons spell out whole actions, so the row can be wider than the standard
    // panel. Widen to fit rather than let it overflow — callers set the labels before calling in.
    double panelWidth  = kPanelWidth;

    if (kind == alertKindChoice) {
        double needed = choice_row_width() + 20.0;

        panelWidth = (needed > panelWidth) ? needed : panelWidth;
    }
    sState.messageLines   = wrap_text((message != nullptr) ? message : "", panelWidth - 20.0);

    bool   hasPicker   = (kind == alertKindBankConfirm);
    double messageH    = (double)sState.messageLines.size() * kMessageLineH;
    double panelHeight = kTitleH + 10.0 + messageH + 8.0 + (hasPicker ? (kButtonH + 8.0) : 0.0) + 10.0 + kButtonH + 10.0;

    double renderW     = get_render_width() / gGlobalGuiScale;
    double renderH     = get_render_height() / gGlobalGuiScale;

    sState.panelRect      = (tRectangle){
        {
            (renderW - panelWidth) / 2.0, (renderH - panelHeight) / 2.0
        }, {panelWidth, panelHeight}
    };

    sState.closePressed   = false;
    sState.cancelPressed  = false;
    sState.confirmPressed = false;
    sState.pickerPressed  = false;
    sState.choicePressed  = -1;
    synthlib_request_redraw();
}

// Choice buttons carry real labels ("Pull from Synth"), not the one-word Confirm/Cancel that the
// fixed-width button_rect() was sized for, so each is sized to its own text and the row is packed
// from the right edge inward. At a fixed 64px they overlap each other and spill past the panel.
double choice_button_width(uint32_t index) {
    double w = get_text_width(sState.choiceLabel[index].c_str(), kButtonH, eCache) + 12.0;

    return (w < 64.0) ? 64.0 : w;
}

tRectangle choice_button_rect(uint32_t index, double y) {
    const double gap       = 8.0;
    double       rightEdge = sState.panelRect.coord.x + sState.panelRect.size.w - 10.0;

    for (uint32_t i = 0; i < index; i++) {
        rightEdge -= (choice_button_width(i) + gap);
    }

    double       w         = choice_button_width(index);

    return (tRectangle){
        {
            rightEdge - w, y
        }, {w, kButtonH}
    };
}

// What the whole row needs, so begin_dialog() can widen the panel instead of letting it overflow.
double choice_row_width(void) {
    double total = 0.0;

    for (uint32_t i = 0; i < sState.choiceCount; i++) {
        total += choice_button_width(i) + ((i > 0) ? 8.0 : 0.0);
    }

    return total;
}

int choice_button_at(tCoord coord) {
    for (uint32_t i = 0; i < sState.choiceCount; i++) {
        if (within_rectangle(coord, draw_button_bounds(choice_button_rect(i, button_row_y())))) {
            return (int)i;
        }
    }

    return -1;
}

} // namespace

extern "C" {
void show_alert(const char * title, const char * message) {
    begin_dialog(alertKindInfo, title, message, "OK");
    sState.confirmCallback = nullptr;
    sState.bankCallback    = nullptr;
}

void show_confirm(const char * title, const char * message, const char * confirmLabel, tAlertConfirmCallback callback) {
    begin_dialog(alertKindConfirm, title, message, confirmLabel);
    sState.confirmCallback = callback;
    sState.bankCallback    = nullptr;
}

void show_choice(const char * title, const char * message,
                 const char * label0, const char * label1, const char * label2,
                 tAlertChoiceCallback callback) {
    const char * labels[3] = {label0, label1, label2};

    // Labels first: begin_dialog() measures them to decide how wide the panel has to be.
    sState.choiceCount     = 0;

    for (uint32_t i = 0; i < 3; i++) {
        sState.choiceLabel[i].clear();

        if ((labels[i] != nullptr) && (labels[i][0] != '\0')) {
            sState.choiceLabel[i] = labels[i];
            sState.choiceCount    = i + 1;
        }
    }

    begin_dialog(alertKindChoice, title, message, label0);

    sState.confirmCallback = nullptr;
    sState.bankCallback    = nullptr;
    sState.choiceCallback  = callback;
}

void show_bank_confirm(const char * title, const char * message, const char * confirmLabel, const char * fieldLabel,
                       uint32_t defaultBank1Indexed, uint32_t maxBank1Indexed, tAlertBankConfirmCallback callback) {
    begin_dialog(alertKindBankConfirm, title, message, confirmLabel);
    sState.fieldLabel           = ((fieldLabel != nullptr) && (fieldLabel[0] != '\0')) ? fieldLabel : "Bank:";
    sState.maxBank1Indexed      = (maxBank1Indexed >= 1) ? maxBank1Indexed : 1;
    sState.selectedBank1Indexed = ((defaultBank1Indexed >= 1) && (defaultBank1Indexed <= sState.maxBank1Indexed)) ? defaultBank1Indexed : 1;
    sState.confirmCallback      = nullptr;
    sState.bankCallback         = callback;
    build_bank_picker_items();
}

bool alert_dialog_active(void) {
    return sState.active;
}

void handle_alert_dialog_mouse_down(tCoord coord) {
    if (!sState.active) {
        return;
    }
    sState.closePressed  = within_rectangle(coord, draw_button_bounds(close_button_rect()));
    sState.pickerPressed = (sState.kind == alertKindBankConfirm) && within_rectangle(coord, draw_button_bounds(picker_button_rect()));

    if (sState.kind == alertKindChoice) {
        sState.choicePressed = choice_button_at(coord);
    } else if (sState.kind == alertKindInfo) {
        sState.confirmPressed = within_rectangle(coord, draw_button_bounds(button_rect(0, button_row_y())));
    } else {
        sState.cancelPressed  = within_rectangle(coord, draw_button_bounds(button_rect(1, button_row_y())));
        sState.confirmPressed = within_rectangle(coord, draw_button_bounds(button_rect(0, button_row_y())));
    }
    synthlib_request_redraw();
}

bool handle_alert_dialog_click(tCoord coord) {
    if (!sState.active) {
        return false;
    }
    sState.closePressed   = false;
    sState.cancelPressed  = false;
    sState.confirmPressed = false;
    sState.pickerPressed  = false;
    sState.choicePressed  = -1;

    if (!within_rectangle(coord, sState.panelRect)) {
        return true; // Modal — swallow clicks outside without closing (matches other G2-Edit popups)
    }

    if (within_rectangle(coord, draw_button_bounds(close_button_rect()))) {
        finish_dialog(false);
        return true;
    }

    if ((sState.kind == alertKindBankConfirm) && within_rectangle(coord, draw_button_bounds(picker_button_rect()))) {
        open_context_menu(below_rect(picker_button_rect()), sState.bankMenuItems.data(),
                          (uint32_t)std::min<uint32_t>(8, sState.maxBank1Indexed), 0.0);
        return true;
    }

    if (sState.kind == alertKindChoice) {
        int choice = choice_button_at(coord);

        if (choice >= 0) {
            finish_choice_dialog(true, choice);
        }
        return true;
    }

    if (sState.kind == alertKindInfo) {
        if (within_rectangle(coord, draw_button_bounds(button_rect(0, button_row_y())))) {
            finish_dialog(true);
        }
        return true;
    }
    bool wantsConfirm = within_rectangle(coord, draw_button_bounds(button_rect(0, button_row_y())));
    bool wantsCancel  = within_rectangle(coord, draw_button_bounds(button_rect(1, button_row_y())));

    if (wantsCancel) {
        finish_dialog(false);
        return true;
    }

    if (wantsConfirm) {
        finish_dialog(true);
        return true;
    }
    return true;
}

void handle_alert_dialog_key(int key, int action) {
    if (!sState.active) {
        return;
    }

    if ((action != GLFW_PRESS) && (action != GLFW_REPEAT)) {
        return;
    }

    if (key == GLFW_KEY_ESCAPE) {
        finish_dialog(false);
        return;
    }

    if ((key == GLFW_KEY_ENTER) || (key == GLFW_KEY_KP_ENTER)) {
        finish_dialog(true);
        return;
    }
}

void render_alert_dialog(void) {
    if (!sState.active) {
        return;
    }
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

    // Bank picker
    if (sState.kind == alertKindBankConfirm) {
        tRectangle  row         = picker_row_rect();

        set_rgb_colour((tRgb)RGB_BLACK);
        render_text(mainArea, (tRectangle){
                {row.coord.x, row.coord.y + 2.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
            }, sState.fieldLabel.c_str());

        std::string pickerLabel = "Bank " + std::to_string(sState.selectedBank1Indexed);
        draw_button(mainArea, picker_button_rect(), pickerLabel.c_str(),
                    sState.pickerPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY);
    }
    // OK / Confirm / Cancel buttons — Confirm (or OK) rightmost (primary action), Cancel to its left.
    double buttonY = button_row_y();

    if (sState.kind == alertKindInfo) {
        tRgb okColour = sState.confirmPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_GREEN_ON;

        draw_button(mainArea, button_rect(0, buttonY), sState.confirmLabel.c_str(), okColour);
    } else if (sState.kind == alertKindChoice) {
        // Rightmost is the affirmative (green), the rest neutral — same visual grammar as the
        // confirm dialog's Confirm/Cancel pair, extended along the row.
        for (uint32_t i = 0; i < sState.choiceCount; i++) {
            tRgb colour = (sState.choicePressed == (int)i)
                          ? (tRgb)RGB_GREY_7
                          : ((i == 0) ? (tRgb)RGB_GREEN_ON : (tRgb)RGB_BACKGROUND_GREY);

            draw_button(mainArea, choice_button_rect(i, buttonY), sState.choiceLabel[i].c_str(), colour);
        }
    } else {
        tRgb confirmColour = sState.confirmPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_GREEN_ON;
        tRgb cancelColour  = sState.cancelPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY;

        draw_button(mainArea, button_rect(0, buttonY), sState.confirmLabel.c_str(), confirmColour);
        draw_button(mainArea, button_rect(1, buttonY), "Cancel", cancelColour);
    }

    // The bank picker's dropdown is opened (from handle_alert_dialog_click()) on top of this modal
    // panel — the app's own render_context_menu() call elsewhere in the frame may run before or
    // after render_alert_dialog(), so re-invoking it here (a harmless no-op redraw when it's not
    // this dialog's own picker that's open) guarantees the flyout always ends up painted last.
    if (gContextMenu.active) {
        render_context_menu();
    }
}
} // extern "C"
