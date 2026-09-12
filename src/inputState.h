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
// Notes: Docs/code-notes/inputState.h.md - "// notes §k" refers there.

#ifndef __INPUT_STATE_H__
#define __INPUT_STATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "synthlibTypes.h"   // tCoord / tMouseButton, for the coordinate and button helpers
#include <stdint.h>

// notes §1
typedef enum {
    eModifierNone  = 0,
    eModifierShift = 1u << 0,
    eModifierCmd   = 1u << 1,   // Command / Super / Windows
    eModifierAlt   = 1u << 2,   // Option
    eModifierCtrl  = 1u << 3,
} tModifierBits;

// notes §2
void set_modifier_state(uint32_t modifiers);
uint32_t modifier_state(void);

bool shift_modifier_held(void);
bool cmd_modifier_held(void);
bool alt_modifier_held(void);
bool ctrl_modifier_held(void);

// Add-to-selection rather than replace-selection. Its own function because it is a POLICY — all
// three editors accept either Shift or Command, and a caller asking "should this extend the
// selection?" should not have to restate which keys mean that.
bool multi_select_modifier_held(void);

// notes §3
void set_modifier_state_from_glfw(int glfwMods);

// notes §4
tCoord synthlib_window_to_logical(double x, double y);

// Where the cursor is now, in logical coordinates. This is what an app hands to synthlib_host_init()
// as its mouseCoord, and what the popups and panels ask for when they need the pointer outside an
// event.
void synthlib_mouse_coord(tCoord * coord);

// GLFW's (button, action) pair as a tMouseButton. Pure decode, no state — it was G2-Edit's alone,
// and the other two apps compared raw GLFW constants at each call site instead.
tMouseButton synthlib_mouse_button(int glfwButton, int glfwAction);

#ifdef __cplusplus
}
#endif

#endif // __INPUT_STATE_H__
