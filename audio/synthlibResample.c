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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "synthlibResample.h"

#define PHASES        (512u)        // notes §1
#define HALF_WIDTH    (48u)         // notes §1
#define PASSBAND      (0.952)       // notes §1
#define KAISER_BETA   (9.0)

static double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;

    for (int k = 1; (k < 64) && (term > (sum * 1e-14)); k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum  += term;
    }
    return sum;
}

static double kernel(double x, double cutoff, double half) {
    double r      = x / half;

    if (fabs(r) >= 1.0) {
        return 0.0;
    }
    double window = bessel_i0(KAISER_BETA * sqrt(1.0 - (r * r))) / bessel_i0(KAISER_BETA);
    double t      = M_PI * cutoff * x;

    return cutoff * ((fabs(t) < 1e-9) ? 1.0 : (sin(t) / t)) * window;
}

bool synthlib_resample_init(tSynthLibResampler * r, double inRate, double outRate) {
    double down   = (outRate < inRate) ? (outRate / inRate) : 1.0;

    synthlib_resample_free(r);
    r->step    = inRate / outRate;
    r->half    = (uint32_t)ceil(HALF_WIDTH / down);
    r->width   = r->half * 2u;
    r->latency = (uint32_t)lround(r->half / r->step);
    r->table   = malloc(sizeof(float) * (PHASES + 1u) * r->width);
    r->ring    = malloc(sizeof(float) * r->width * 2u);

    if ((r->table == NULL) || (r->ring == NULL)) {
        synthlib_resample_free(r);
        return false;
    }

    for (uint32_t p = 0; p <= PHASES; p++) {
        float  * row  = &r->table[p * r->width];
        double   sum  = 0.0;
        double   frac = (double)p / PHASES;

        for (uint32_t j = 0; j < r->width; j++) {
            double h = kernel((double)j - (double)r->half + 1.0 - frac, PASSBAND * down, (double)r->half);

            row[j] = (float)h;
            sum   += h;
        }

        for (uint32_t j = 0; j < r->width; j++) {
            row[j] = (float)(row[j] / sum);
        }
    }
    synthlib_resample_reset(r);
    return true;
}

void synthlib_resample_free(tSynthLibResampler * r) {
    free(r->table);
    free(r->ring);
    memset(r, 0, sizeof(*r));
}

void synthlib_resample_reset(tSynthLibResampler * r) {
    r->phase = 0.0;
    r->write = 0;
    memset(r->ring, 0, sizeof(float) * r->width * 2u);
}

uint32_t synthlib_resample_needed(const tSynthLibResampler * r, uint32_t outFrames) {
    double   phase = r->phase;
    uint32_t total = 0;

    // summed the way process() steps, so rounding cannot part the two
    for (uint32_t i = 0; i < outFrames; i++) {
        phase += r->step;
        uint32_t n = (uint32_t)phase;

        total += n;
        phase -= n;
    }
    return total;
}

void synthlib_resample_process(tSynthLibResampler * r, const float * in, float * out, uint32_t outFrames) {
    for (uint32_t i = 0; i < outFrames; i++) {
        double        place  = r->phase * PHASES;
        uint32_t      index  = (uint32_t)place;
        float         blend  = (float)(place - index);
        const float * a      = &r->table[index * r->width];
        const float * b      = a + r->width;
        const float * window = &r->ring[r->write];
        float         sum    = 0.0f;

        for (uint32_t j = 0; j < r->width; j++) {
            sum += window[j] * (a[j] + (blend * (b[j] - a[j])));
        }
        out[i]    = sum;
        r->phase += r->step;
        uint32_t n = (uint32_t)r->phase;

        r->phase -= n;

        for (uint32_t k = 0; k < n; k++) {
            r->ring[r->write]            = *in;
            r->ring[r->write + r->width] = *in++;
            r->write                     = (r->write + 1u < r->width) ? (r->write + 1u) : 0u;
        }
    }
}
