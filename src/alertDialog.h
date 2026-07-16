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

#ifndef __SYNTHLIB_ALERT_DIALOG_H__
#define __SYNTHLIB_ALERT_DIALOG_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// In-window replacement for NSAlert-based info/confirm/bank-number-picker dialogs, drawn with the
// same GLFW/OpenGL primitives as fileBrowser.h/bankBrowser.h. Three flavours share one modal panel:
//   show_alert()         - message + single OK button, no callback (fire and forget)
//   show_confirm()       - message + Cancel/confirmLabel buttons
//   show_bank_confirm()  - show_confirm() plus a "Bank N" picker button that opens a dropdown
//                          (built on the app's own context-menu system, see contextMenu.h) for
//                          choosing a bank number in [1, maxBank1Indexed]
//
// The embedding app must provide the same symbols contextMenu.h/fileBrowser.h require (gReDraw,
// get_global_gui_scaled_mouse_coord()) and must, once per frame, call render_alert_dialog() (order
// relative to its own render_context_menu() call doesn't matter — render_alert_dialog() re-invokes
// render_context_menu() itself when the picker dropdown is open, so that flyout always paints over
// this panel regardless of where the app's own call happens to sit). Route mouse-down through
// handle_alert_dialog_mouse_down() and mouse-up through handle_alert_dialog_click() ahead of other
// click handling; route key events through handle_alert_dialog_key(). All are safe to call
// unconditionally — they no-op when alert_dialog_active() is false.

typedef void (*tAlertConfirmCallback)(bool confirmed);
typedef void (*tAlertBankConfirmCallback)(bool confirmed, uint32_t bank1Indexed);

void show_alert(const char * title, const char * message);
void show_confirm(const char * title, const char * message, const char * confirmLabel, tAlertConfirmCallback callback);
void show_bank_confirm(const char * title, const char * message, const char * confirmLabel, const char * fieldLabel,
                       uint32_t defaultBank1Indexed, uint32_t maxBank1Indexed, tAlertBankConfirmCallback callback);

bool alert_dialog_active(void);
void handle_alert_dialog_mouse_down(tCoord coord);
bool handle_alert_dialog_click(tCoord coord);
void handle_alert_dialog_key(int key, int action);
void render_alert_dialog(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_ALERT_DIALOG_H__
