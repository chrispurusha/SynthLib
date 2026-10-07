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
// Notes: Docs/code-notes/breakpointGraph.c.md - "// notes §k" refers there.

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>

#include "breakpointGraph.h"
#include "utilsGraphics.h"

#define GRAPH_POINT_RADIUS    4.0
#define GRAPH_HIT_RADIUS      9.0

static tCoord to_screen(tRectangle rect, const tGraphPoint * point) {
    return (tCoord){
               rect.coord.x + (point->x * rect.size.w), rect.coord.y + ((1.0 - point->y) * rect.size.h)
    };
}

void graph_draw(tArea area, tRectangle rect, const tGraphPoint * points, uint32_t count, const tGraphStyle * style, int32_t activePoint) {
    tCoord topLeft     = rect.coord;
    tCoord topRight    = {rect.coord.x + rect.size.w, rect.coord.y};
    tCoord bottomLeft  = {rect.coord.x, rect.coord.y + rect.size.h};
    tCoord bottomRight = {rect.coord.x + rect.size.w, rect.coord.y + rect.size.h};

    set_rgb_colour(style->frame);
    render_line(area, topLeft, topRight, 1.0);
    render_line(area, topRight, bottomRight, 1.0);
    render_line(area, bottomRight, bottomLeft, 1.0);
    render_line(area, bottomLeft, topLeft, 1.0);

    if (style->axisY >= 0.0) {
        double y = rect.coord.y + ((1.0 - style->axisY) * rect.size.h);

        render_line(area, (tCoord){rect.coord.x, y}, (tCoord){rect.coord.x + rect.size.w, y}, 1.0);
    }
    set_rgb_colour(style->line);

    for (uint32_t i = 1; i < count; i++) {
        render_line(area, to_screen(rect, &points[i - 1]), to_screen(rect, &points[i]), 2.0);
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!points[i].movesX && !points[i].movesY) {
            continue;
        }
        set_rgb_colour(((int32_t)i == activePoint) ? style->activePoint : style->point);
        render_circle_part(area, to_screen(rect, &points[i]), GRAPH_POINT_RADIUS, 16, 0, 16);
    }
}

int32_t graph_hit_point(tRectangle rect, const tGraphPoint * points, uint32_t count, tCoord at) {
    int32_t best     = -1;
    double  bestDist = GRAPH_HIT_RADIUS;

    // notes §1
    for (uint32_t i = 0; i < count; i++) {
        if (!points[i].movesX && !points[i].movesY) {
            continue;
        }
        tCoord p    = to_screen(rect, &points[i]);
        double dist = hypot(at.x - p.x, at.y - p.y);

        if (dist <= bestDist) {
            bestDist = dist;
            best     = (int32_t)i;
        }
    }

    return best;
}

tCoord graph_position(tRectangle rect, tCoord at) {
    double x = (rect.size.w > 0.0) ? (at.x - rect.coord.x) / rect.size.w : 0.0;
    double y = (rect.size.h > 0.0) ? 1.0 - ((at.y - rect.coord.y) / rect.size.h) : 0.0;

    return (tCoord){
               (x < 0.0) ? 0.0 : (x > 1.0) ? 1.0 : x, (y < 0.0) ? 0.0 : (y > 1.0) ? 1.0 : y
    };
}

#ifdef __cplusplus
}
#endif
