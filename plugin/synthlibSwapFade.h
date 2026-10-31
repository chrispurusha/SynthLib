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

#ifndef __SYNTHLIB_SWAP_FADE_H__
#define __SYNTHLIB_SWAP_FADE_H__

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

// notes §1: output down, the whole sound changed in silence, output up
typedef struct {
    atomic_bool holding;  // main thread
    atomic_bool silent;   // audio thread
    float       gain;
    uint32_t    holdLeft;
} tSynthLibSwapFade;

void synthlib_swap_fade_init(tSynthLibSwapFade * fade);

// Main thread, around a change of sound. begin returns once the output is silent, or after a timeout
void synthlib_swap_fade_begin(tSynthLibSwapFade * fade);
void synthlib_swap_fade_end(tSynthLibSwapFade * fade);

// notes §4: the audio thread cannot wait - start, change the sound once silent() says so, then end
void synthlib_swap_fade_start(tSynthLibSwapFade * fade);
bool synthlib_swap_fade_silent(tSynthLibSwapFade * fade);

// Audio thread, on the finished block. right may be NULL
void synthlib_swap_fade_apply(tSynthLibSwapFade * fade, float * left, float * right, uint32_t frames, double rate);

#endif // __SYNTHLIB_SWAP_FADE_H__
