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

// See synthlibPopups.h for what this is and why the orchestration stopped being each host's job.

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>
#include <string.h>

// GLFW's key constants, as alertDialog.c and every other SynthLib file that handles keys already
// does. The HEADER stays platform-free; only this side knows.
#include <GLFW/glfw3.h>

#include "synthlibDefs.h"
#include "synthlibPopups.h"
#include "contextMenu.h"
#include "fileBrowser.h"
#include "bankBrowser.h"
#include "alertDialog.h"
#include "menuBar.h"

#define MAX_APP_POPUPS    (16)

static const tSynthLibPopup * gAppPopups     = NULL;
static uint32_t               gAppPopupCount = 0;

static const tMenuBarItem *   gMenuBarItems  = NULL;
static tRectangle             (*gMenuBarRect)(void) = NULL;

// ── Adapters for SynthLib's own popups ───────────────────────────────────────
//
// Each of these is the quirk that used to be copy-pasted into every host, written once. They are
// small on purpose: the value is not the code, it is that there is now exactly one copy of each.

// MODAL MEANS SWALLOW THE PRESS TOO, not just the click. The browsers only need the mouse-UP to do
// their work, and every host originally passed the mouse-DOWN straight through to whatever was
// underneath — so the down-half of a click on a browser's Cancel button also pressed a module
// control on the canvas behind it, started a rubber-band selection, or grabbed a scrollbar.
static bool file_browser_mouse(tCoord coord, tMouseButton mouseButton) {
    if (mouseButton == mouseButtonLeftDown) {
        handle_file_browser_mouse_down(coord);
    } else if (mouseButton == mouseButtonLeftUp) {
        handle_file_browser_click(coord);
    }
    return true;
}

static bool file_browser_key(int key, int action) {
    handle_file_browser_key(key, action);
    return true;
}

static bool file_browser_scroll(double yDelta) {
    handle_file_browser_scroll(yDelta);
    return true;
}

static bool file_browser_char(unsigned int codepoint) {
    handle_file_browser_char(codepoint);
    return true;
}

static bool bank_browser_mouse(tCoord coord, tMouseButton mouseButton) {
    if (mouseButton == mouseButtonLeftDown) {
        handle_bank_browser_mouse_down(coord);
    } else if (mouseButton == mouseButtonLeftUp) {
        handle_bank_browser_click(coord);
    }
    return true;
}

static bool bank_browser_key(int key, int action) {
    handle_bank_browser_key(key, action);
    return true;
}

static bool bank_browser_scroll(double yDelta) {
    handle_bank_browser_scroll(yDelta);
    return true;
}

// THE ALERT DIALOG HAS TO ROUTE AROUND ITS OWN DROPDOWN. show_bank_confirm()'s bank picker is opened
// through the shared context-menu system, so once that flyout is up the clicks belong to it and not
// to the dialog panel underneath — exactly as a menu bar defers to the menu it opened. Swallowing
// them here as if they had landed on the dialog is what makes the picker unusable.
static bool alert_dialog_mouse(tCoord coord, tMouseButton mouseButton) {
    if (mouseButton == mouseButtonLeftDown) {
        if (!gContextMenu.active) {
            handle_alert_dialog_mouse_down(coord);
        }
    } else if (mouseButton == mouseButtonLeftUp) {
        if (gContextMenu.active) {
            if (!handle_context_menu_click(coord)) {
                gContextMenu.active = false;
            }
        } else {
            handle_alert_dialog_click(coord);
        }
    }
    return true;
}

// Escape closes just the picker first, if it is open, rather than the whole dialog underneath it —
// the same precedence a host's own menu-versus-Escape handling uses.
static bool alert_dialog_key(int key, int action) {
    if (gContextMenu.active && (key == GLFW_KEY_ESCAPE) && (action == GLFW_PRESS)) {
        gContextMenu.active = false;
    } else {
        handle_alert_dialog_key(key, action);
    }
    return true;
}

static bool context_menu_active(void) {
    return gContextMenu.active;
}

static bool menu_bar_active(void) {
    return (gMenuBarItems != NULL) && (gMenuBarRect != NULL);
}

static void menu_bar_tick(void) {
    update_menu_bar_hover(gMenuBarItems, gMenuBarRect());
}

// SynthLib's own five, in one table so that the order is stated rather than implied by the sequence
// of calls in somebody else's render function.
// CLICKS FOR THE MENU BAR AND THE CONTEXT MENU ARE STILL THE HOST'S, deliberately, and the mouse
// entries below are NULL to say so. Both sit BEHIND the floating panels in every host's input
// pipeline — a panel is allowed to overlap the top bar, so a click where a panel covers the bar
// belongs to the panel — and this coordinator is invoked before the panels are consulted. Dispatching
// them here would silently move them in front. Their RENDER and their HOVER TICK move now, which is
// where the duplication actually caused bugs; their click routing follows once the hosts' own
// pipeline steps are registered here too, and not before.
static const tSynthLibPopup gLibPopups[] = {
    {"menuBar",     SYNTHLIB_POPUP_LAYER_MENU_BAR,     false, menu_bar_active,     NULL,                menu_bar_tick,             NULL,               NULL,             NULL,                NULL             },
    {"contextMenu", SYNTHLIB_POPUP_LAYER_CONTEXT_MENU, false, context_menu_active, render_context_menu, update_context_menu_hover, NULL,               NULL,             NULL,                NULL             },
    {"fileBrowser", SYNTHLIB_POPUP_LAYER_BROWSERS,     true,  file_browser_active, render_file_browser, NULL,                      file_browser_mouse, file_browser_key, file_browser_scroll, file_browser_char},
    {"bankBrowser", SYNTHLIB_POPUP_LAYER_BROWSERS + 1, true,  bank_browser_active, render_bank_browser, update_bank_browser_hover, bank_browser_mouse, bank_browser_key, bank_browser_scroll, NULL             },
    {"alertDialog", SYNTHLIB_POPUP_LAYER_ALERT,        true,  alert_dialog_active, render_alert_dialog, NULL,                      alert_dialog_mouse, alert_dialog_key, NULL,                NULL             },
};

#define LIB_POPUP_COUNT    ((uint32_t)(sizeof(gLibPopups) / sizeof(gLibPopups[0])))

// ── The merged, layer-ordered view ───────────────────────────────────────────

// Built fresh per call rather than kept: the app's array can be re-registered, and the cost is a
// sort of at most 21 pointers against a frame that is about to draw a whole patch.
static uint32_t collect(const tSynthLibPopup ** out, uint32_t max) {
    uint32_t count = 0;

    for (uint32_t i = 0; (i < LIB_POPUP_COUNT) && (count < max); i++) {
        out[count++] = &gLibPopups[i];
    }

    for (uint32_t i = 0; (i < gAppPopupCount) && (count < max); i++) {
        out[count++] = &gAppPopups[i];
    }

    // Insertion sort, ascending by layer: back to front. Stable, so two popups sharing a layer keep
    // their registration order — which is what lets the two browsers share one constant.
    for (uint32_t i = 1; i < count; i++) {
        const tSynthLibPopup * held = out[i];
        uint32_t               j    = i;

        while ((j > 0) && (out[j - 1]->layer > held->layer)) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = held;
    }

    return count;
}

static bool popup_is_active(const tSynthLibPopup * popup) {
    return (popup->active == NULL) || popup->active();
}

void synthlib_popups_register(const tSynthLibPopup * popups, uint32_t count) {
    if (count > MAX_APP_POPUPS) {
        LOG_ERROR("synthlib_popups_register: %u popups exceeds the %u supported\n",
                  (unsigned)count, (unsigned)MAX_APP_POPUPS);
        count = MAX_APP_POPUPS;
    }
    gAppPopups     = popups;
    gAppPopupCount = count;
}

void synthlib_popups_set_menu_bar(const tMenuBarItem * items, tRectangle (*barRect)(void)) {
    gMenuBarItems = items;
    gMenuBarRect  = barRect;
}

void synthlib_popups_render(void) {
    const tSynthLibPopup * ordered[LIB_POPUP_COUNT + MAX_APP_POPUPS];
    uint32_t               count = collect(ordered, (uint32_t)(sizeof(ordered) / sizeof(ordered[0])));

    for (uint32_t i = 0; i < count; i++) {
        // No active() test before rendering: every one of these render functions already returns
        // immediately when its popup is down, and several of them do work in that early return.
        if (ordered[i]->render != NULL) {
            ordered[i]->render();
        }
    }
}

void synthlib_popups_tick(void) {
    const tSynthLibPopup * ordered[LIB_POPUP_COUNT + MAX_APP_POPUPS];
    uint32_t               count = collect(ordered, (uint32_t)(sizeof(ordered) / sizeof(ordered[0])));

    for (uint32_t i = 0; i < count; i++) {
        if (ordered[i]->tick != NULL) {
            ordered[i]->tick();
        }
    }
}

bool synthlib_popups_modal_active(void) {
    for (uint32_t i = 0; i < LIB_POPUP_COUNT; i++) {
        if (gLibPopups[i].modal && popup_is_active(&gLibPopups[i])) {
            return true;
        }
    }

    for (uint32_t i = 0; i < gAppPopupCount; i++) {
        if (gAppPopups[i].modal && popup_is_active(&gAppPopups[i])) {
            return true;
        }
    }

    return false;
}

// Front to back, stopping at the frontmost active modal: nothing behind a modal popup may see the
// event, whether or not the modal itself wanted it. Returning at the modal rather than continuing is
// the difference between "modal" and "gets first refusal".
typedef enum {
    eDispatchMouse = 0,
    eDispatchKey,
    eDispatchScroll,
    eDispatchChar
} eDispatchKind;

static bool dispatch(eDispatchKind kind, tCoord coord, tMouseButton mouseButton, int key, int action,
                     double yDelta, unsigned int codepoint) {
    const tSynthLibPopup * ordered[LIB_POPUP_COUNT + MAX_APP_POPUPS];
    uint32_t               count = collect(ordered, (uint32_t)(sizeof(ordered) / sizeof(ordered[0])));

    for (uint32_t i = count; i > 0; i--) {
        const tSynthLibPopup * popup = ordered[i - 1];

        if (!popup_is_active(popup)) {
            continue;
        }

        switch (kind) {
            case eDispatchKey:

                if ((popup->key != NULL) && popup->key(key, action)) {
                    return true;
                }
                break;

            case eDispatchScroll:

                if ((popup->scroll != NULL) && popup->scroll(yDelta)) {
                    return true;
                }
                break;

            case eDispatchChar:

                if ((popup->character != NULL) && popup->character(codepoint)) {
                    return true;
                }
                break;

            case eDispatchMouse:
            default:

                if ((popup->mouse != NULL) && popup->mouse(coord, mouseButton)) {
                    return true;
                }
                break;
        }

        if (popup->modal) {
            return true;
        }
    }

    return false;
}

static const tCoord kNoCoord = {0.0, 0.0};

bool synthlib_popups_dispatch_click(tCoord coord, tMouseButton mouseButton) {
    return dispatch(eDispatchMouse, coord, mouseButton, 0, 0, 0.0, 0);
}

bool synthlib_popups_dispatch_key(int key, int action) {
    return dispatch(eDispatchKey, kNoCoord, mouseButtonNone, key, action, 0.0, 0);
}

bool synthlib_popups_dispatch_scroll(double yDelta) {
    return dispatch(eDispatchScroll, kNoCoord, mouseButtonNone, 0, 0, yDelta, 0);
}

bool synthlib_popups_dispatch_char(unsigned int codepoint) {
    return dispatch(eDispatchChar, kNoCoord, mouseButtonNone, 0, 0, 0.0, codepoint);
}

#ifdef __cplusplus
}
#endif
