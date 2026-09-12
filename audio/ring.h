/*
 * SynthLib - single-producer / single-consumer ring buffer.
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
// Notes: Docs/code-notes/ring.h.md - "// notes §k" refers there.

#ifndef RING_H
#define RING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

// notes §1

typedef struct {
    float *          buffer;       // interleaved, frames * channels
    uint32_t         frames;       // capacity in frames
    uint32_t         channels;
    _Atomic uint64_t writePos;     // absolute frames written by the producer
    _Atomic uint64_t readPos;      // absolute frames consumed by the consumer
    _Atomic uint32_t overflows;    // producer found no room
    _Atomic uint32_t underflows;   // consumer found too little data
} tRing;

bool     ring_init(tRing * ring, uint32_t frames, uint32_t channels);
void     ring_free(tRing * ring);
void     ring_reset(tRing * ring);

// Producer side. Returns false and counts an overflow if the write would overlap unread data,
// in which case nothing is written - dropping a block is better than tearing one.
bool     ring_write(tRing * ring, const float * src, uint32_t frames);

// Consumer side. Returns false and counts an underflow if fewer than 'frames' are available,
// in which case dst is zero-filled so the caller always has something to hand its device.
bool     ring_read(tRing * ring, float * dst, uint32_t frames);

// Frames written but not yet read. This is the quantity the drift loop steers.
uint64_t ring_fill(const tRing * ring);

// notes §2
void     ring_resync(tRing * ring, uint32_t targetFrames);

#ifdef __cplusplus
}
#endif

#endif // RING_H
