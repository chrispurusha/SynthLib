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

// See midiPortDialog.h. Drawn with the same chrome, buttons and colours as alertDialog.c, so it
// reads as one of SynthLib's panels rather than something bolted on.

#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "synthlibDefs.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "synthlibHost.h"
#include "synthlibGlobals.h"
#include "synthlibMidi.h"
#include "midiPortDialog.h"

#define PORT_LIST_MAX        (64)
#define PORT_TITLE_SIZE      (96)
#define PORT_STATUS_SIZE     (160)
#define PORT_VISIBLE_ROWS    (12)
#define CHANNEL_CELLS        (17)    // Auto, then 1-16: a cell's index IS the channel it chooses

typedef enum {
    ePortSideInput  = 0,
    ePortSideOutput = 1,
    ePortSideCount
} tPortSide;

// One column. Row 0 is "Automatic" and is drawn rather than stored; rows 1..count are the ports
// present; one more row follows when the chosen port is NOT present, so an unplugged interface stays
// visibly chosen instead of the list quietly showing something else selected.
typedef struct {
    char     name[PORT_LIST_MAX][SYNTHLIB_MIDI_PORT_NAME_MAX];
    uint32_t count;
    char     chosen[SYNTHLIB_MIDI_PORT_NAME_MAX];
    bool     chosenAbsent;
    double   scroll;    // first visible row, fractional so a trackpad scrolls smoothly
} tPortList;

typedef struct {
    bool                active;
    tMidiPortDialogHost host;
    char                title[PORT_TITLE_SIZE];
    tPortList           list[ePortSideCount];
    tRectangle          panelRect;
    double              refreshedAt;

    // Press state, so a control acts on release over the same thing it was pressed on - the
    // behaviour of every other button in these applications.
    bool     closePressed;
    int      buttonPressed;               // -1 none, 0 Close, 1 Scan
    int      rowPressedSide;              // -1 none
    uint32_t rowPressed;
    uint32_t channel;                     // SYNTHLIB_MIDI_CHANNEL_AUTOMATIC or 1-16
    int      channelPressed;              // -1 none, else a cell
} tPortDialogState;

static tPortDialogState sState         = {.buttonPressed = -1, .rowPressedSide = -1, .channelPressed = -1};

static const double     kPanelWidth    = 620.0;
static const double     kTitleH        = 26.0;
static const double     kPad           = 10.0;
static const double     kHeaderH       = STANDARD_TEXT_HEIGHT + 6.0;
static const double     kRowH          = STANDARD_TEXT_HEIGHT + 6.0;
static const double     kStatusH       = STANDARD_TEXT_HEIGHT + 8.0;
static const double     kChannelLabelW = 64.0;
static const double     kChannelAutoW  = 72.0;

// draw_button() sizes its label from the rectangle's height, so buttons are STANDARD_TEXT_HEIGHT
// tall exactly as alertDialog.c's are.
static const double     kButtonH       = STANDARD_TEXT_HEIGHT;

static double now_seconds(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + ((double)t.tv_nsec / 1.0e9);
}

// ── Layout ──────────────────────────────────────────────────────────────────────────────────────

static double panel_height(void) {
    return kTitleH + kPad + kHeaderH + (PORT_VISIBLE_ROWS * kRowH) + kPad + kRowH + kPad + kStatusH + kPad + kButtonH + kPad;
}

// Centred afresh every frame rather than once at open, so resizing the window keeps it centred;
// clicks are tested against whatever was last drawn.
static void layout(void) {
    double renderW = get_render_width() / gGlobalGuiScale;
    double renderH = get_render_height() / gGlobalGuiScale;

    sState.panelRect = (tRectangle){
        {
            (renderW - kPanelWidth) / 2.0, (renderH - panel_height()) / 2.0
        }, {
            kPanelWidth, panel_height()
        }
    };
}

static double column_width(void) {
    return (kPanelWidth - (3.0 * kPad)) / 2.0;
}

static tRectangle list_rect(tPortSide side) {
    double x = sState.panelRect.coord.x + kPad + ((double)side * (column_width() + kPad));
    double y = sState.panelRect.coord.y + kTitleH + kPad + kHeaderH;

    return (tRectangle){{
                            x, y
                        }, {
                            column_width(), PORT_VISIBLE_ROWS * kRowH
                        }
    };
}

static tRectangle row_rect(tPortSide side, uint32_t visibleIndex) {
    tRectangle box = list_rect(side);

    return (tRectangle){{
                            box.coord.x, box.coord.y + ((double)visibleIndex * kRowH)
                        }, {
                            box.size.w, kRowH
                        }
    };
}

static double channel_row_y(void) {
    tRectangle box = list_rect(ePortSideInput);

    return box.coord.y + box.size.h + kPad;
}

static double status_y(void) {
    return channel_row_y() + kRowH + kPad;
}

static tRectangle channel_cell_rect(uint32_t cell) {
    double x     = sState.panelRect.coord.x + kPad + kChannelLabelW;
    double width = (kPanelWidth - (2.0 * kPad) - kChannelLabelW - kChannelAutoW) / (double)(CHANNEL_CELLS - 1);

    if (cell > 0) {
        x += kChannelAutoW + ((double)(cell - 1) * width);
    }
    return (tRectangle){{
                            x, channel_row_y()
                        }, {
                            (cell == 0) ? kChannelAutoW : width, kRowH
                        }
    };
}

static int channel_cell_at(tCoord coord) {
    for (uint32_t cell = 0; cell < CHANNEL_CELLS; cell++) {
        if (within_rectangle(coord, channel_cell_rect(cell))) {
            return (int)cell;
        }
    }

    return -1;
}

static double button_row_y(void) {
    return sState.panelRect.coord.y + sState.panelRect.size.h - kPad - kButtonH;
}

// 0 is rightmost, as alertDialog.c's button_rect().
static tRectangle button_rect(int fromRight) {
    double w         = 64.0;
    double gap       = 8.0;
    double rightEdge = sState.panelRect.coord.x + sState.panelRect.size.w - kPad - ((double)fromRight * (w + gap));

    return (tRectangle){{
                            rightEdge - w, button_row_y()
                        }, {
                            w, kButtonH
                        }
    };
}

// ── Rows ────────────────────────────────────────────────────────────────────────────────────────

static uint32_t row_count(tPortSide side) {
    const tPortList * list = &sState.list[side];

    return 1u + list->count + (list->chosenAbsent ? 1u : 0u);
}

static uint32_t first_visible(tPortSide side) {
    double limit = (double)row_count(side) - PORT_VISIBLE_ROWS;
    double at    = sState.list[side].scroll;

    if (at > limit) {
        at = limit;
    }

    if (at < 0.0) {
        at = 0.0;
    }
    return (uint32_t)floor(at);
}

// The name a row stands for; "" for Automatic.
static const char * row_name(tPortSide side, uint32_t row) {
    const tPortList * list = &sState.list[side];

    if (row == 0) {
        return "";
    }

    if (row <= list->count) {
        return list->name[row - 1];
    }
    return list->chosen;    // the absent row
}

static bool row_selected(tPortSide side, uint32_t row) {
    const tPortList * list = &sState.list[side];

    if (list->chosen[0] == '\0') {
        return row == 0;
    }

    if (list->chosenAbsent) {
        return row == (list->count + 1u);
    }
    return (row > 0) && (strcmp(row_name(side, row), list->chosen) == 0);
}

// Re-reads the ports present and the current choice. Called on open, on every choice, on Scan and
// once a second while the dialogue is up, so an interface plugged in while it is open appears.
static void refresh(void) {
    char chosenIn[SYNTHLIB_MIDI_PORT_NAME_MAX]  = {0};
    char chosenOut[SYNTHLIB_MIDI_PORT_NAME_MAX] = {0};

    synthlib_midi_ports_chosen(chosenIn, sizeof(chosenIn), chosenOut, sizeof(chosenOut));

    for (int side = 0; side < (int)ePortSideCount; side++) {
        tPortList * list = &sState.list[side];

        list->count        = synthlib_midi_port_names(side == ePortSideInput, list->name, PORT_LIST_MAX);
        snprintf(list->chosen, sizeof(list->chosen), "%s", (side == ePortSideInput) ? chosenIn : chosenOut);
        list->chosenAbsent = false;

        if (list->chosen[0] != '\0') {
            list->chosenAbsent = true;

            for (uint32_t i = 0; i < list->count; i++) {
                if (strcmp(list->name[i], list->chosen) == 0) {
                    list->chosenAbsent = false;
                    break;
                }
            }
        }
    }

    sState.channel     = synthlib_midi_channel_chosen();
    sState.refreshedAt = now_seconds();
}

static void choose_channel(uint32_t channel) {
    synthlib_midi_channel_choose(channel);
    refresh();

    if (sState.host.changed != NULL) {
        sState.host.changed();
    }
}

static void choose(tPortSide side, uint32_t row) {
    char input[SYNTHLIB_MIDI_PORT_NAME_MAX]  = {0};
    char output[SYNTHLIB_MIDI_PORT_NAME_MAX] = {0};

    snprintf(input, sizeof(input), "%s", sState.list[ePortSideInput].chosen);
    snprintf(output, sizeof(output), "%s", sState.list[ePortSideOutput].chosen);

    if (side == ePortSideInput) {
        snprintf(input, sizeof(input), "%s", row_name(side, row));
    } else {
        snprintf(output, sizeof(output), "%s", row_name(side, row));
    }
    synthlib_midi_ports_choose(input, output);
    refresh();

    if (sState.host.changed != NULL) {
        sState.host.changed();
    }
}

static bool row_at(tCoord coord, tPortSide * side, uint32_t * row) {
    for (int s = 0; s < (int)ePortSideCount; s++) {
        if (!within_rectangle(coord, list_rect((tPortSide)s))) {
            continue;
        }
        uint32_t visible = (uint32_t)((coord.y - list_rect((tPortSide)s).coord.y) / kRowH);
        uint32_t index   = first_visible((tPortSide)s) + visible;

        if (index >= row_count((tPortSide)s)) {
            return false;    // below the last row: empty list space, not a row
        }
        *side = (tPortSide)s;
        *row  = index;
        return true;
    }

    return false;
}

static int button_at(tCoord coord) {
    for (int i = 0; i < 2; i++) {
        if (within_rectangle(coord, draw_button_bounds(button_rect(i)))) {
            return i;
        }
    }

    return -1;
}

static void close_dialog(void) {
    sState.active = false;
    synthlib_request_redraw();
}

// ── Drawing ─────────────────────────────────────────────────────────────────────────────────────

// Shortened with "..." until it fits. eNoCache: get_text_width()'s cache is keyed on the POINTER,
// and every candidate here lives in the same stack buffer.
static void fit_text(const char * text, double width, char * out, size_t size) {
    char   candidate[SYNTHLIB_MIDI_PORT_NAME_MAX + 8] = {0};
    size_t len                                        = 0;

    snprintf(out, size, "%s", text);

    if (get_text_width(out, STANDARD_TEXT_HEIGHT, eNoCache) <= width) {
        return;
    }
    len = strlen(out);

    while (len > 0) {
        len--;
        snprintf(candidate, sizeof(candidate), "%.*s...", (int)len, text);

        if (get_text_width(candidate, STANDARD_TEXT_HEIGHT, eNoCache) <= width) {
            snprintf(out, size, "%s", candidate);
            return;
        }
    }
    snprintf(out, size, "...");
}

static void render_list(tPortSide side) {
    tRectangle        box   = list_rect(side);
    const tPortList * list  = &sState.list[side];
    uint32_t          first = first_visible(side);
    uint32_t          total = row_count(side);

    set_rgb_colour((tRgb)RGB_BLACK);
    render_text(mainArea, (tRectangle){
        {box.coord.x, box.coord.y - kHeaderH + 2.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
    }, (side == ePortSideInput) ? "Input - the synth is heard on" : "Output - the synth is played through");

    set_rgb_colour((tRgb)RGB_WHITE);
    render_rectangle(mainArea, box);

    for (uint32_t v = 0; v < PORT_VISIBLE_ROWS; v++) {
        uint32_t   row                                     = first + v;
        tRectangle r                                       = row_rect(side, v);
        char       label[SYNTHLIB_MIDI_PORT_NAME_MAX + 32] = {0};
        char       shown[SYNTHLIB_MIDI_PORT_NAME_MAX + 32] = {0};

        if (row >= total) {
            break;
        }

        if (row_selected(side, row)) {
            set_rgb_colour((tRgb)RGB_GREEN_ON);
            render_rectangle(mainArea, r);
        } else if ((sState.rowPressedSide == (int)side) && (sState.rowPressed == row)) {
            set_rgb_colour((tRgb)RGB_GREY_7);
            render_rectangle(mainArea, r);
        }

        if (row == 0) {
            snprintf(label, sizeof(label), "Automatic (found by Scan)");
        } else if (row <= list->count) {
            snprintf(label, sizeof(label), "%s", list->name[row - 1]);
        } else {
            snprintf(label, sizeof(label), "%s (not present)", list->chosen);
        }
        fit_text(label, r.size.w - 8.0, shown, sizeof(shown));
        set_rgb_colour((row > list->count) ? (tRgb)RGB_GREY_5 : (tRgb)RGB_BLACK);
        render_text(mainArea, (tRectangle){
            {r.coord.x + 4.0, r.coord.y + 3.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, shown);
    }

    // A thumb, only when there is somewhere to scroll to: without it a thirteenth port is simply
    // invisible and nothing says the wheel will reach it.
    if (total > PORT_VISIBLE_ROWS) {
        double span   = box.size.h * ((double)PORT_VISIBLE_ROWS / (double)total);
        double travel = box.size.h - span;
        double at     = travel * ((double)first / (double)(total - PORT_VISIBLE_ROWS));

        set_rgb_colour((tRgb)RGB_GREY_5);
        render_rectangle(mainArea, (tRectangle){
            {box.coord.x + box.size.w - 4.0, box.coord.y + at}, {4.0, span}
        });
    }
}

static void render_channel_row(void) {
    uint32_t inUse = (sState.host.channelInUse != NULL) ? sState.host.channelInUse() : 0;

    set_rgb_colour((tRgb)RGB_BLACK);
    render_text(mainArea, (tRectangle){
        {sState.panelRect.coord.x + kPad, channel_row_y() + 3.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
    }, "Channel");

    for (uint32_t cell = 0; cell < CHANNEL_CELLS; cell++) {
        tRectangle r         = channel_cell_rect(cell);
        char       label[16] = {0};
        double     width     = 0.0;

        if (cell == sState.channel) {
            set_rgb_colour((tRgb)RGB_GREEN_ON);
        } else if (sState.channelPressed == (int)cell) {
            set_rgb_colour((tRgb)RGB_GREY_7);
        } else {
            set_rgb_colour((tRgb)RGB_WHITE);
        }
        render_rectangle(mainArea, (tRectangle){
            {r.coord.x + 1.0, r.coord.y}, {r.size.w - 2.0, r.size.h}
        });

        if (cell > 0) {
            snprintf(label, sizeof(label), "%u", (unsigned)cell);
        } else if ((sState.channel == SYNTHLIB_MIDI_CHANNEL_AUTOMATIC) && (inUse >= 1) && (inUse <= 16)) {
            snprintf(label, sizeof(label), "Auto (%u)", (unsigned)inUse);
        } else {
            snprintf(label, sizeof(label), "Auto");
        }
        width = get_text_width(label, STANDARD_TEXT_HEIGHT, eNoCache);
        set_rgb_colour((tRgb)RGB_BLACK);
        render_text(mainArea, (tRectangle){
            {r.coord.x + ((r.size.w - width) / 2.0), r.coord.y + 3.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, label);
    }
}

static void render_dialog(void) {
    char status[PORT_STATUS_SIZE] = {0};

    if (!sState.active) {
        return;
    }

    if ((now_seconds() - sState.refreshedAt) >= 1.0) {
        refresh();
    }
    layout();

    // Solid, not translucent, as every other modal panel here (see alertDialog.c).
    set_rgb_colour((tRgb)RGB_GREY_2);
    render_rectangle(mainArea, (tRectangle){
        {0.0, 0.0}, {get_render_width() / gGlobalGuiScale, get_render_height() / gGlobalGuiScale}
    });

    draw_panel_chrome(mainArea, sState.panelRect, kTitleH, sState.title);
    draw_panel_close_button(mainArea, sState.panelRect, sState.closePressed);

    render_list(ePortSideInput);
    render_list(ePortSideOutput);
    render_channel_row();

    if (sState.host.status != NULL) {
        char shown[PORT_STATUS_SIZE] = {0};

        sState.host.status(status, sizeof(status));
        fit_text(status, sState.panelRect.size.w - (2.0 * kPad), shown, sizeof(shown));
        set_rgb_colour((tRgb)RGB_BLACK);
        render_text(mainArea, (tRectangle){
            {sState.panelRect.coord.x + kPad, status_y() + 2.0}, {BLANK_SIZE, STANDARD_TEXT_HEIGHT}
        }, shown);
    }
    draw_button(mainArea, button_rect(0), "Close",
                (sState.buttonPressed == 0) ? (tRgb)RGB_GREY_7 : (tRgb)RGB_GREEN_ON);
    draw_button(mainArea, button_rect(1), "Scan",
                (sState.buttonPressed == 1) ? (tRgb)RGB_GREY_7 : (tRgb)RGB_BACKGROUND_GREY);
}

// ── Input ───────────────────────────────────────────────────────────────────────────────────────

static bool dialog_active(void) {
    return sState.active;
}

// Modal: every click is consumed, whether or not it lands on the panel, as alertDialog.c does.
static bool dialog_mouse(tCoord coord, tMouseButton mouseButton) {
    tPortSide side = ePortSideInput;
    uint32_t  row  = 0;

    if (!sState.active) {
        return false;
    }

    if (mouseButton == mouseButtonLeftDown) {
        sState.closePressed   = within_rectangle(coord, panel_close_button_rect(sState.panelRect));
        sState.buttonPressed  = button_at(coord);
        sState.rowPressedSide = -1;
        sState.channelPressed = channel_cell_at(coord);

        if (row_at(coord, &side, &row)) {
            sState.rowPressedSide = (int)side;
            sState.rowPressed     = row;
        }
        synthlib_request_redraw();
        return true;
    }

    if (mouseButton != mouseButtonLeftUp) {
        return true;
    }
    bool closeHit   = sState.closePressed && within_rectangle(coord, panel_close_button_rect(sState.panelRect));
    int  button     = ((sState.buttonPressed >= 0) && (button_at(coord) == sState.buttonPressed)) ? sState.buttonPressed : -1;
    bool rowHit     = (sState.rowPressedSide >= 0) && row_at(coord, &side, &row)
                      && ((int)side == sState.rowPressedSide) && (row == sState.rowPressed);
    int  channelHit = ((sState.channelPressed >= 0) && (channel_cell_at(coord) == sState.channelPressed))
                      ? sState.channelPressed : -1;

    sState.closePressed   = false;
    sState.buttonPressed  = -1;
    sState.rowPressedSide = -1;
    sState.channelPressed = -1;
    synthlib_request_redraw();

    if (closeHit || (button == 0)) {
        close_dialog();
    } else if (button == 1) {
        refresh();

        if (sState.host.scan != NULL) {
            sState.host.scan();
        }
    } else if (rowHit && !row_selected(side, row)) {
        choose(side, row);
    } else if ((channelHit >= 0) && ((uint32_t)channelHit != sState.channel)) {
        choose_channel((uint32_t)channelHit);
    }
    return true;
}

static bool dialog_key(int key, int mods, int action) {
    (void)mods;

    if (!sState.active) {
        return false;
    }

    if (  (action == GLFW_PRESS)
       && ((key == GLFW_KEY_ESCAPE) || (key == GLFW_KEY_ENTER) || (key == GLFW_KEY_KP_ENTER))) {
        close_dialog();
    }
    return true;
}

// The list under the pointer scrolls; the sign matches bankBrowser.cpp's, so the wheel moves the
// same way in both.
static bool dialog_scroll(double yDelta) {
    tCoord coord = {0.0, 0.0};

    if (!sState.active) {
        return false;
    }
    synthlib_host_mouse_coord(&coord);

    for (int s = 0; s < (int)ePortSideCount; s++) {
        if (within_rectangle(coord, list_rect((tPortSide)s))) {
            double limit = (double)row_count((tPortSide)s) - PORT_VISIBLE_ROWS;
            double at    = sState.list[s].scroll - yDelta;

            sState.list[s].scroll = (at > limit) ? limit : ((at < 0.0) ? 0.0 : at);
            synthlib_request_redraw();
        }
    }

    return true;
}

// Between the browsers and the alert: an alert raised while it is open (a failed connect, say)
// must land in front of it.
static const tSynthLibPopup kPopup[] = {
    {
        "midiPortDialog", SYNTHLIB_POPUP_LAYER_BROWSERS + 50, true, dialog_active, render_dialog, NULL,
        dialog_mouse, dialog_key, dialog_scroll, NULL
    },
};

// ── Public ──────────────────────────────────────────────────────────────────────────────────────

void midi_port_dialog_open(const tMidiPortDialogHost * host) {
    if (host != NULL) {
        sState.host = *host;
    } else {
        memset(&sState.host, 0, sizeof(sState.host));
    }
    snprintf(sState.title, sizeof(sState.title), "%s",
             ((host != NULL) && (host->title != NULL)) ? host->title : "MIDI Ports");
    sState.host.title                   = NULL; // copied above; the caller's string need not outlive the call
    sState.closePressed                 = false;
    sState.buttonPressed                = -1;
    sState.rowPressedSide               = -1;
    sState.channelPressed               = -1;
    sState.list[ePortSideInput].scroll  = 0.0;
    sState.list[ePortSideOutput].scroll = 0.0;
    refresh();
    layout();
    sState.active                       = true;
    synthlib_request_redraw();
}

bool midi_port_dialog_active(void) {
    return sState.active;
}

const tSynthLibPopup * midi_port_dialog_popup(void) {
    return kPopup;
}
