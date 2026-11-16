/*
 * SynthLib - shared code for the synth editors and their plug-ins.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If
 * not, see <https://www.gnu.org/licenses/>.
 */
// Notes: Docs/code-notes/synthlibCtlPoints.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_CTL_POINTS_H__
#define __SYNTHLIB_CTL_POINTS_H__

#include <stdbool.h>
#include <stdint.h>

#include "synthlibPlugin.h"

#define SYNTHLIB_CTL_POINTS    (128)

// notes §1: a block's values for the performance controllers (wheel, bend, pressure...), each at its own frame, in
// frame order - kept by paramPoints(), played by the process loop as it renders up to each
typedef struct {
    uint32_t id;
    uint32_t offset;
    double   value;
} tSynthLibCtlPoint;

typedef struct {
    tSynthLibCtlPoint point[SYNTHLIB_CTL_POINTS];
    uint32_t          count, next;
} tSynthLibCtlPoints;

// one parameter's points into the block's list, kept in frame order (a later id's point at the same frame after)
static inline void synthlib_ctl_points_add(tSynthLibCtlPoints * c, uint32_t id, const tSynthLibParamPoint * points, uint32_t count) {
    for (uint32_t k = 0; k < count; k++) {
        if (c->count == SYNTHLIB_CTL_POINTS) {
            c->count--;   // full: the last slot takes every later point
        }
        uint32_t at = c->count;

        while ((at > 0) && (c->point[at - 1].offset > points[k].sampleOffset)) {
            c->point[at] = c->point[at - 1];
            at--;
        }
        c->point[at] = (tSynthLibCtlPoint){id, points[k].sampleOffset, points[k].value};
        c->count++;
    }
}

// the next point due by frame `upTo`, if any - the loop renders up to its offset, then applies it
static inline bool synthlib_ctl_points_due(tSynthLibCtlPoints * c, uint32_t upTo, tSynthLibCtlPoint * out) {
    if ((c->next >= c->count) || (c->point[c->next].offset > upTo)) {
        return false;
    }
    *out = c->point[c->next++];
    return true;
}

// the block is done: whatever was not reached (a frame past its end) is applied by the caller, then the list empties
static inline void synthlib_ctl_points_clear(tSynthLibCtlPoints * c) {
    c->count = 0;
    c->next  = 0;
}

#endif
