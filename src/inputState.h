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

#ifndef __INPUT_STATE_H__
#define __INPUT_STATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "synthlibTypes.h"   // tCoord / tMouseButton, for the coordinate and button helpers
#include <stdint.h>

// WHICH MODIFIER KEYS ARE HELD, AS PUSHED STATE RATHER THAN SOMETHING TO POLL.
//
// glfwGetKey() is a PULL api: it asks the window system, right now, on this thread. That works for an
// application built around GLFW and not at all for a plug-in, which is only ever HANDED events by its
// host and has no window to interrogate. Every widget that wanted to know about Shift therefore had
// to be given a platform seam of its own, and there were three of those with several copies each.
//
// The seam is now ONE WRITER AND MANY READERS. Each shell translates whatever its own toolkit gives
// it — GLFW's `mods` argument, an NSEvent's modifierFlags — into the bits below and pushes them here
// once; everything else reads the predicates. No reader needs to know a window exists, so the same
// code answers correctly in an application, in a plug-in, and in a headless test that simply sets
// the state it wants to exercise.
//
// This file deliberately has no GLFW, no Cocoa and no platform header of any kind in it. That is the
// point of it: the translation belongs to the shell, which is the only part that knows what it is
// translating from.
typedef enum {
    eModifierNone  = 0,
    eModifierShift = 1u << 0,
    eModifierCmd   = 1u << 1,   // Command / Super / Windows
    eModifierAlt   = 1u << 2,   // Option
    eModifierCtrl  = 1u << 3,
} tModifierBits;

// Called by the SHELL, from whichever events carry modifier state. Pass the complete set each time —
// this replaces the stored value rather than merging into it, so a released key needs no separate
// call. Clear it (eModifierNone) when the window loses focus: a key released while another
// application has the keyboard is a release the shell will never be told about, and a modifier stuck
// on is worse than one missed.
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

// THE GLFW SHELL'S ONE CALL. Pass the `mods` argument GLFW already gives a key or mouse-button
// callback and it translates and stores it. Declared here but implemented in inputStateGlfw.c, so
// this header stays free of platform headers and a plug-in links inputState.c alone — see that file
// for why the mapping is shared rather than repeated in each application.
void set_modifier_state_from_glfw(int glfwMods);

//
// The transform from window pixels to the logical, GUI-scaled space everything above the GLFW layer
// works in. It was written out three times: character-identical in EmuUtility and SynthEdit as a
// static window_to_logical(), and inlined into G2-Edit's get_global_gui_scaled_mouse_coord() — where
// it had lost the divide-by-zero guard the other two kept, so a window reporting a zero dimension (it
// happens while minimising) would have divided by it.
//
// No window parameter: SynthLib owns the window (synthlib_window()), and every call site was passing
// that same window back in.
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
