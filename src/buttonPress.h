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
// Notes: Docs/code-notes/buttonPress.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_BUTTON_PRESS_H__
#define __SYNTHLIB_BUTTON_PRESS_H__

#include <stdbool.h>
#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §1 - a press arms a button, the release acts if it lands on the same one
void synthlib_button_press(const void * id);
bool synthlib_button_held(const void * id);
const void * synthlib_button_release(void);
bool synthlib_button_clicked(const void * held, const void * id, tRectangle bounds, tCoord at);
void synthlib_button_cancel(void);

// notes §2
tRgb synthlib_button_held_colour(tRgb colour);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_BUTTON_PRESS_H__
