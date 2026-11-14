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
// Notes: Docs/code-notes/buttonPress.c.md - "// notes §k" refers there.

#include "buttonPress.h"
#include "geometry.h"

static const void * sHeld;   // notes §1 - one pointer, so one button held at a time

void synthlib_button_press(const void * id) {
    sHeld = id;
}

bool synthlib_button_held(const void * id) {
    return (id != NULL) && (sHeld == id);
}

const void * synthlib_button_release(void) {
    const void * held = sHeld;

    sHeld = NULL;
    return held;
}

bool synthlib_button_clicked(const void * held, const void * id, tRectangle bounds, tCoord at) {
    return (held != NULL) && (held == id) && within_rectangle(at, bounds);
}

void synthlib_button_cancel(void) {
    sHeld = NULL;
}

// notes §2
tRgb synthlib_button_held_colour(tRgb colour) {
    return (tRgb){
        colour.red + ((1.0 - colour.red) * 0.5), colour.green + ((1.0 - colour.green) * 0.5), colour.blue + ((1.0 - colour.blue) * 0.5)
    };
}
