/*
 * SynthLib - fixed-ratio resampling.
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
// Notes: Docs/code-notes/synthlibResample.md

#ifndef __SYNTHLIB_RESAMPLE_H__
#define __SYNTHLIB_RESAMPLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// A fixed-ratio streaming resampler, mono float, for an engine that runs at its own rate
typedef struct {
    double   step;      // input samples per output sample
    double   phase;     // 0..1
    uint32_t half;      // input samples either side of the output point
    uint32_t width;     // half * 2
    uint32_t write;     // the ring's next slot
    uint32_t latency;   // in output samples
    float  * table;     // one row of width per phase
    float  * ring;      // width doubled, so a window is always contiguous
} tSynthLibResampler;

bool synthlib_resample_init(tSynthLibResampler * r, double inRate, double outRate);
void synthlib_resample_free(tSynthLibResampler * r);
void synthlib_resample_reset(tSynthLibResampler * r);
// How many input samples the next outFrames output samples consume
uint32_t synthlib_resample_needed(const tSynthLibResampler * r, uint32_t outFrames);
// in holds exactly synthlib_resample_needed(r, outFrames) samples
void synthlib_resample_process(tSynthLibResampler * r, const float * in, float * out, uint32_t outFrames);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_RESAMPLE_H__
