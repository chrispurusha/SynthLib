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

// See inputState.h for why the modifiers are pushed rather than polled.

#ifdef __cplusplus
extern "C" {
#endif

#include "inputState.h"

// UI-thread only, like every other piece of UI state here: the shell writes it from an event
// callback and the readers all run in the same event or render pass.
static uint32_t gModifiers = eModifierNone;

void set_modifier_state(uint32_t modifiers) {
    gModifiers = modifiers;
}

uint32_t modifier_state(void) {
    return gModifiers;
}

bool shift_modifier_held(void) {
    return (gModifiers & (uint32_t)eModifierShift) != 0;
}

bool cmd_modifier_held(void) {
    return (gModifiers & (uint32_t)eModifierCmd) != 0;
}

bool alt_modifier_held(void) {
    return (gModifiers & (uint32_t)eModifierAlt) != 0;
}

bool ctrl_modifier_held(void) {
    return (gModifiers & (uint32_t)eModifierCtrl) != 0;
}

bool multi_select_modifier_held(void) {
    return (gModifiers & ((uint32_t)eModifierShift | (uint32_t)eModifierCmd)) != 0;
}

#ifdef __cplusplus
}
#endif
