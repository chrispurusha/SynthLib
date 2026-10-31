/*
 * SynthLib - a plug-in's diagnostic log, switched on by a file.
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
// Notes: Docs/code-notes/synthlibSwapFade.md - "// notes §k" refers there.

#include <time.h>

#include "synthlibSwapFade.h"

#define FADE_SECONDS       (0.005)
#define HOLD_SECONDS       (0.020)   // notes §2
#define WAIT_MILLISECONDS  (100)     // notes §3

void synthlib_swap_fade_init(tSynthLibSwapFade * fade) {
    atomic_init(&fade->holding, false);
    atomic_init(&fade->silent, false);
    fade->gain     = 1.0f;
    fade->holdLeft = 0u;
}

void synthlib_swap_fade_start(tSynthLibSwapFade * fade) {
    atomic_store(&fade->silent, false);
    atomic_store(&fade->holding, true);
}

bool synthlib_swap_fade_silent(tSynthLibSwapFade * fade) {
    return atomic_load(&fade->silent);
}

void synthlib_swap_fade_begin(tSynthLibSwapFade * fade) {
    const struct timespec tick = {0, 1000000};

    synthlib_swap_fade_start(fade);

    for (int ms = 0; (ms < WAIT_MILLISECONDS) && !atomic_load(&fade->silent); ms++) {
        nanosleep(&tick, NULL);
    }
}

void synthlib_swap_fade_end(tSynthLibSwapFade * fade) {
    atomic_store(&fade->holding, false);
}

void synthlib_swap_fade_apply(tSynthLibSwapFade * fade, float * left, float * right, uint32_t frames, double rate) {
    bool  holding = atomic_load(&fade->holding);
    float step    = (float)(1.0 / (rate * FADE_SECONDS));

    if (!holding && (fade->gain >= 1.0f) && (fade->holdLeft == 0u)) {
        return;
    }

    for (uint32_t f = 0; f < frames; f++) {
        if (holding) {
            fade->gain     = (fade->gain > step) ? (fade->gain - step) : 0.0f;
            fade->holdLeft = (uint32_t)(rate * HOLD_SECONDS);
        } else if (fade->holdLeft > 0u) {
            fade->holdLeft--;
        } else {
            fade->gain = ((fade->gain + step) < 1.0f) ? (fade->gain + step) : 1.0f;
        }
        left[f] *= fade->gain;

        if (right != NULL) {
            right[f] *= fade->gain;
        }
    }

    if (holding && (fade->gain <= 0.0f)) {
        atomic_store(&fade->silent, true);
    }
}
