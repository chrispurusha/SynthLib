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

#include <stdio.h>
#include <string.h>

#include "synthlibDefs.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "synthlibHost.h"
#include "synthlibGlobals.h"
#include "contextMenu.h"
#include "alertDialog.h"

// Every buffer here is a fixed size chosen against what the dialog can actually hold: the panel is
// kPanelWidth wide and the message is word-wrapped into it, so both the line length and the line
// count have hard ceilings. Overlong input truncates rather than growing an allocation.
#define ALERT_TITLE_SIZE           (64)
#define ALERT_LABEL_SIZE           (32)
#define ALERT_CHOICE_LABEL_SIZE    (32)
#define ALERT_LINE_SIZE            (128)   // One wrapped line of message text
#define ALERT_MAX_LINES            (32)
#define ALERT_MAX_BANKS            (32)    // NUM_PATCH_BANKS, the larger of the two bank domains

typedef enum {
    alertKindInfo,
    alertKindConfirm,
    alertKindBankConfirm,
    alertKindChoice,
} tAlertKind;

typedef struct {
    bool                      active;
    tAlertKind                kind;
    char                      title[ALERT_TITLE_SIZE];
    char                      messageLine[ALERT_MAX_LINES][ALERT_LINE_SIZE];
    uint32_t                  lineCount;
    char                      confirmLabel[ALERT_LABEL_SIZE];
    char                      fieldLabel[ALERT_LABEL_SIZE];
    uint32_t                  selectedBank1Indexed;
    uint32_t                  maxBank1Indexed;
    tAlertConfirmCallback     confirmCallback;
    tAlertBankConfirmCallback bankCallback;

    // alertKindChoice only: labels right-to-left (choiceLabel[0] is rightmost), unused ones empty.
    char                      choiceLabel[3][ALERT_CHOICE_LABEL_SIZE];
    uint32_t                  choiceCount;
    int                       choicePressed;
    tAlertChoiceCallback      choiceCallback;

    tRectangle                panelRect;
    bool                      closePressed;
    bool                      cancelPressed;
    bool                      confirmPressed;
    bool                      pickerPressed;

    // Backing storage for the bank-picker dropdown. bankMenuItems[i].label points into
    // bankLabel[i], so both live for as long as the dialog does and neither ever moves.
    char      bankLabel[ALERT_MAX_BANKS][16];
    tMenuItem bankMenuItems[ALERT_MAX_BANKS + 1];                  // + NULL terminator
} tAlertState;

static tAlertState  sState;

static const double kPanelWidth   = 420.0;
static const double kTitleH       = 26.0;
static const double kMessageLineH = STANDARD_TEXT_HEIGHT + 4.0;

// draw_button() sizes its label text directly off the height of the rectangle passed in (see
// utilsGraphics.cpp), so every button here uses STANDARD_TEXT_HEIGHT for its height, matching the
// text size every other button in the app renders at.
static const double kButtonH      = STANDARD_TEXT_HEIGHT;

static double content_x(void) {
    return sState.panelRect.coord.x + 10.0;
}

static double message_y(void) {
    return sState.panelRect.coord.y + kTitleH + 10.0;
}

static void copy_to(char * dest, size_t destSize, const char * src, const char * fallback) {
    const char * text = ((src != NULL) && (src[0] != '\0')) ? src : fallback;

    if (text == NULL) {
        text = "";
    }
    snprintf(dest, destSize, "%s", text);
}

static void push_line(const char * line) {
    if (sState.lineCount >= ALERT_MAX_LINES) {
        return;  // Message longer than the panel can show; the rest is dropped rather than wrapped
    }
    snprintf(sState.messageLine[sState.lineCount], ALERT_LINE_SIZE, "%s", line);
    sState.lineCount++;
}

// Greedy word-wrap into messageLine[], measured the same way render_text() will draw it (the same
// technique bankBrowser.cpp uses for its own message text). A '\n' is a HARD break, and two in a
// row leave a deliberate blank line — without that the newline is swallowed into whichever word it
// touches and two sentences render as "disagree.Your edits", with nothing to show where one
// thought ended and the next began.
static void wrap_message(const char * text, double maxWidth) {
    char   current[ALERT_LINE_SIZE] = {0};
    size_t pos                      = 0;
    size_t len                      = 0;

    sState.lineCount = 0;

    if (text == NULL) {
        return;
    }
    len              = strlen(text);

    while (pos <= len) {
        size_t wordEnd = pos;

        while ((wordEnd < len) && (text[wordEnd] != ' ') && (text[wordEnd] != '\n')) {
            wordEnd++;
        }
        size_t wordLen = wordEnd - pos;

        if (wordLen > 0) {
            char word[ALERT_LINE_SIZE]      = {0};
            char candidate[ALERT_LINE_SIZE] = {0};

            if (wordLen >= ALERT_LINE_SIZE) {
                wordLen = ALERT_LINE_SIZE - 1;
            }
            memcpy(word, &text[pos], wordLen);
            word[wordLen] = '\0';

            if (current[0] == '\0') {
                snprintf(candidate, sizeof(candidate), "%s", word);
            } else {
                snprintf(candidate, sizeof(candidate), "%s %s", current, word);
            }

            if ((current[0] == '\0') || (get_text_width(candidate, STANDARD_TEXT_HEIGHT, eNoCache) <= maxWidth)) {
                snprintf(current, sizeof(current), "%s", candidate);
            } else {
                push_line(current);
                snprintf(current, sizeof(current), "%s", word);
            }
        }

        if (wordEnd >= len) {
            break;
        }

        if (text[wordEnd] == '\n') {
            push_line(current);  // Flush, even when empty — that is the blank line
            current[0] = '\0';
        }
        pos = wordEnd + 1;
    }

    if (current[0] != '\0') {
        push_line(current);
    }
}

static tRectangle close_button_rect(void) {
    double w = get_text_width("Close", kButtonH, eCache) + 4.0;

    return (tRectangle){
        {
            sState.panelRect.coord.x + sState.panelRect.size.w - w - 8.0 - BORDER_LINE_WIDTH, sState.panelRect.coord.y + 4.0
        }, {
            w, kButtonH
        }
    };
}

static tRectangle picker_row_rect(void) {
    double y = message_y() + ((double)sState.lineCount * kMessageLineH) + 8.0;

    return (tRectangle){
        {
            content_x(), y
        }, {
            sState.panelRect.size.w - 20.0, kButtonH
        }
    };
}

static tRectangle picker_button_rect(void) {
    tRectangle row = picker_row_rect();
    double     w   = 100.0;

    return (tRectangle){
        {
            row.coord.x + row.size.w - w, row.coord.y
        }, {
            w, kButtonH
        }
    };
}

static double button_row_y(void) {
    return sState.panelRect.coord.y + sState.panelRect.size.h - 10.0 - kButtonH;
}

// fromRight counts button-widths in from the panel's right edge — 0 is rightmost. x is computed
// from the button's right edge inward so it can never extend past the panel (matches
// fileBrowser.cpp/bankBrowser.cpp's identical button_rect()).
static tRectangle button_rect(int fromRight, double y) {
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

static void on_bank_picker_item_chosen(int index) {
    sState.selectedBank1Indexed = gContextMenu.items[index].param;
    synthlib_request_redraw();
}

// Rebuilds the bank-picker dropdown's items for the current sState.maxBank1Indexed. Must run
// before the picker button can be clicked (called from show_bank_confirm()).
static void build_bank_picker_items(void) {
    uint32_t i = 0;

    for (i = 0; i < sState.maxBank1Indexed; i++) {
        snprintf(sState.bankLabel[i], sizeof(sState.bankLabel[i]), "Bank %u", i + 1);

        memset(&sState.bankMenuItems[i], 0, sizeof(sState.bankMenuItems[i]));
        sState.bankMenuItems[i].label  = sState.bankLabel[i];
        sState.bankMenuItems[i].colour = (tRgb)RGB_GREY_3;
        sState.bankMenuItems[i].action = on_bank_picker_item_chosen;
        sState.bankMenuItems[i].param  = i + 1;
    }

    memset(&sState.bankMenuItems[i], 0, sizeof(sState.bankMenuItems[i]));  // Terminator
}

// Choice buttons carry real labels ("Pull from Synth"), not the one-word Confirm/Cancel that the
// fixed-width button_rect() was sized for, so each is sized to its own text and the row is packed
// from the right edge inward. At a fixed 64px they overlap each other and spill past the panel.
static double choice_button_width(uint32_t index) {
    double w = get_text_width(sState.choiceLabel[index], kButtonH, eCache) + 12.0;

    return (w < 64.0) ? 64.0 : w;
}

static tRectangle choice_button_rect(uint32_t index, double y) {
    const double gap       = 8.0;
    double       rightEdge = sState.panelRect.coord.x + sState.panelRect.size.w - 10.0;

    for (uint32_t i = 0; i < index; i++) {
        rightEdge -= (choice_button_width(i) + gap);
    }

    double       w         = choice_button_width(index);

    return (tRectangle){
        {
            rightEdge - w, y
        }, {
            w, kButtonH
        }
    };
}

// What the whole row needs, so begin_dialog() can widen the panel instead of letting it overflow.
static double choice_row_width(void) {
    double total = 0.0;

    for (uint32_t i = 0; i < sState.choiceCount; i++) {
        total += choice_button_width(i) + ((i > 0) ? 8.0 : 0.0);
    }

    return total;
}

static int choice_button_at(tCoord coord) {
    for (uint32_t i = 0; i < sState.choiceCount; i++) {
        if (within_rectangle(coord, draw_button_bounds(choice_button_rect(i, button_row_y())))) {
            return (int)i;
        }
    }

    return -1;
}

// choice is meaningful only for alertKindChoice; every other kind passes -1 and is driven by
// `confirmed`. Dismissal (Close/Escape) reaches a choice dialog as confirmed == false.
static void finish_choice_dialog(bool confirmed, int choice) {
    tAlertKind                kind      = sState.kind;
    tAlertConfirmCallback     confirmCb = sState.confirmCallback;
    tAlertBankConfirmCallback bankCb    = sState.bankCallback;
    tAlertChoiceCallback      choiceCb  = sState.choiceCallback;
    uint32_t                  bank      = sState.selectedBank1Indexed;

    sState.active          = false;
    sState.confirmCallback = NULL;
    sState.bankCallback    = NULL;
    sState.choiceCallback  = NULL;
    synthlib_request_redraw();

    if ((kind == alertKindChoice) && (choiceCb != NULL)) {
        choiceCb(confirmed ? choice : -1);
        return;
    }

    if ((kind == alertKindConfirm) && (confirmCb != NULL)) {
        confirmCb(confirmed);
    } else if ((kind == alertKindBankConfirm) && (bankCb != NULL)) {
        bankCb(confirmed, bank);
    }
}

// The two-button entry point. On a choice dialog, Enter/affirmative means the rightmost button,
// which is the one show_choice()'s caller passed first.
static void finish_dialog(bool confirmed) {
    finish_choice_dialog(confirmed, 0);
}

static void begin_dialog(tAlertKind kind, const char * title, const char * message, const char * confirmLabel) {
    sState.active = true;
    sState.kind   = kind;
    copy_to(sState.title, sizeof(sState.title), title, "");
    copy_to(sState.confirmLabel, sizeof(sState.confirmLabel), confirmLabel, "OK");

    // A choice dialog's buttons spell out whole actions, so the row can be wider than the standard
    // panel. Widen to fit rather than let it overflow — callers set the labels before calling in.
    double panelWidth  = kPanelWidth;

    if (kind == alertKindChoice) {
        double needed = choice_row_width() + 20.0;

        panelWidth = (needed > panelWidth) ? needed : panelWidth;
    }
    wrap_message(message, panelWidth - 20.0);

    bool   hasPicker   = (kind == alertKindBankConfirm);
    double messageH    = (double)sState.lineCount * kMessageLineH;
    double panelHeight = kTitleH + 10.0 + messageH + 8.0 + (hasPicker ? (kButtonH + 8.0) : 0.0) + 10.0 + kButtonH + 10.0;

    double renderW     = get_render_width() / gGlobalGuiScale;
    double renderH     = get_render_height() / gGlobalGuiScale;

    sState.panelRect      = (tRectangle){
        {
            (renderW - panelWidth) / 2.0, (renderH - panelHeight) / 2.0
        }, {
            panelWidth, panelHeight
        }
    };

    sState.closePressed   = false;
    sState.cancelPressed  = false;
    sState.confirmPressed = false;
    sState.pickerPressed  = false;
    sState.choicePressed  = -1;
    synthlib_request_redraw();
}

void show_alert(const char * title, const char * message) {
    begin_dialog(alertKindInfo, title, message, "OK");
    sState.confirmCallback = NULL;
    sState.bankCallback    = NULL;
}

void show_confirm(const char * title, const char * message, const char * confirmLabel, tAlertConfirmCallback callback) {
    begin_dialog(alertKindConfirm, title, message, confirmLabel);
    sState.confirmCallback = callback;
    sState.bankCallback    = NULL;
}

void show_choice(const char * title, const char * message,
                 const char * label0, const char * label1, const char * label2,
                 tAlertChoiceCallback callback) {
    const char * labels[3] = {label0, label1, label2};

    // Labels first: begin_dialog() measures them to decide how wide the panel has to be.
    sState.choiceCount     = 0;

    for (uint32_t i = 0; i < 3; i++) {
        sState.choiceLabel[i][0] = '\0';

        if ((labels[i] != NULL) && (labels[i][0] != '\0')) {
            snprintf(sState.choiceLabel[i], ALERT_CHOICE_LABEL_SIZE, "%s", labels[i]);
            sState.choiceCount = i + 1;
        }
    }

    begin_dialog(alertKindChoice, title, message, label0);

    sState.confirmCallback = NULL;
    sState.bankCallback    = NULL;
    sState.choiceCallback  = callback;
}

void show_bank_confirm(const char * title, const char * message, const char * confirmLabel, const char * fieldLabel,
                       uint32_t defaultBank1Indexed, uint32_t maxBank1Indexed, tAlertBankConfirmCallback callback) {
    begin_dialog(alertKindBankConfirm, title, message, confirmLabel);
    copy_to(sState.fieldLabel, sizeof(sState.fieldLabel), fieldLabel, "Bank:");

    sState.maxBank1Indexed      = (maxBank1Indexed >= 1) ? maxBank1Indexed : 1;

    if (sState.maxBank1Indexed > ALERT_MAX_BANKS) {
        sState.maxBank1Indexed = ALERT_MAX_BANKS;
    }
    sState.selectedBank1Indexed = ((defaultBank1Indexed >= 1) && (defaultBank1Indexed <= sState.maxBank1Indexed)) ? defaultBank1Indexed : 1;
    sState.confirmCallback      = NULL;
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
        uint32_t visibleRows = (sState.maxBank1Indexed < 8) ? sState.maxBank1Indexed : 8;

        open_context_menu(below_rect(picker_button_rect()), sState.bankMenuItems, visibleRows, 0.0);
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
    }, sState.title);

    draw_button(mainArea, close_button_rect(), "Close", sState.closePressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY);

    // Message
    set_rgb_colour((tRgb)RGB_BLACK);

    for (uint32_t i = 0; i < sState.lineCount; i++) {
        render_text(mainArea, (tRectangle){
            {content_x(), message_y() + ((double)i * kMessageLineH)}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, sState.messageLine[i]);
    }

    // Bank picker
    if (sState.kind == alertKindBankConfirm) {
        tRectangle row             = picker_row_rect();
        char       pickerLabel[16] = {0};

        set_rgb_colour((tRgb)RGB_BLACK);
        render_text(mainArea, (tRectangle){
            {row.coord.x, row.coord.y + 2.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, sState.fieldLabel);

        snprintf(pickerLabel, sizeof(pickerLabel), "Bank %u", sState.selectedBank1Indexed);
        draw_button(mainArea, picker_button_rect(), pickerLabel,
                    sState.pickerPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY);
    }
    // OK / Confirm / Cancel buttons — Confirm (or OK) rightmost (primary action), Cancel to its left.
    double buttonY = button_row_y();

    if (sState.kind == alertKindInfo) {
        tRgb okColour = sState.confirmPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_GREEN_ON;

        draw_button(mainArea, button_rect(0, buttonY), sState.confirmLabel, okColour);
    } else if (sState.kind == alertKindChoice) {
        // Rightmost is the affirmative (green), the rest neutral — same visual grammar as the
        // confirm dialog's Confirm/Cancel pair, extended along the row.
        for (uint32_t i = 0; i < sState.choiceCount; i++) {
            tRgb colour = (sState.choicePressed == (int)i)
                          ? (tRgb)RGB_GREY_7
                          : ((i == 0) ? (tRgb)RGB_GREEN_ON : (tRgb)RGB_BACKGROUND_GREY);

            draw_button(mainArea, choice_button_rect(i, buttonY), sState.choiceLabel[i], colour);
        }
    } else {
        tRgb confirmColour = sState.confirmPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_GREEN_ON;
        tRgb cancelColour  = sState.cancelPressed ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY;

        draw_button(mainArea, button_rect(0, buttonY), sState.confirmLabel, confirmColour);
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
