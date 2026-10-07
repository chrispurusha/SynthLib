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
// Notes: Docs/code-notes/breakpointGraph.h.md - "// notes §k" refers there.

#ifndef __BREAKPOINT_GRAPH_H__
#define __BREAKPOINT_GRAPH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "synthlibTypes.h"

#define GRAPH_MAX_POINTS    16

// notes §1
typedef struct {
    double x;      // 0..1 across the graph
    double y;      // 0..1 up from the bottom
    bool   movesX; // a drag may change x
    bool   movesY; // a drag may change y
} tGraphPoint;

typedef struct {
    tRgb   frame;
    tRgb   line;
    tRgb   point;
    tRgb   activePoint;
    double axisY; // a horizontal reference line at this height (0..1), e.g. a bipolar level's zero; < 0 for none
} tGraphStyle;

// Draws the frame, the reference line, the points joined in order, and a handle on every movable point
// (the active one, being dragged or under the mouse, in its own colour; -1 for none).
void graph_draw(tArea area, tRectangle rect, const tGraphPoint * points, uint32_t count, const tGraphStyle * style, int32_t activePoint);

// notes §2
int32_t graph_hit_point(tRectangle rect, const tGraphPoint * points, uint32_t count, tCoord at);

// `at` as a graph position, 0..1 each way (y up), clamped to the graph.
tCoord graph_position(tRectangle rect, tCoord at);

#ifdef __cplusplus
}
#endif

#endif // __BREAKPOINT_GRAPH_H__
