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

#include <stdbool.h>

#ifndef __SYNTHLIB_TYPES_H__
#define __SYNTHLIB_TYPES_H__

typedef struct {
    double x;
    double y;
} tCoord;

typedef struct {
    double w;
    double h;
} tSize;

typedef struct {
    tCoord coord;
    tSize  size;
} tRectangle;

typedef struct {
    tCoord coord1;
    tCoord coord2rel;
    tCoord coord3rel;
} tTriangle;

typedef struct {
    double u1;
    double v1;
    double u2;
    double v2;
    double advance_x;
    int    width;
    int    height;
    int    offset_x;
    int    offset_y;
} GlyphInfo;

typedef struct {
    double     xBar;
    bool       xBarDragging;
    double     xGrabOffset;
    tRectangle xThumb;
    double     yBar;
    bool       yBarDragging;
    double     yGrabOffset;
    tRectangle yThumb;
} tScrollState;

typedef struct {
    double red;
    double green;
    double blue;
} tRgb;

typedef struct {
    double red;
    double green;
    double blue;
    double alpha;
} tRgba;

typedef enum {
    mainArea   = 0,
    moduleArea = 1,  // Only applies to G2-Edit
} tArea;

typedef enum {
    eNoCache = 0,
    eCache   = 1,
} tCache;

#endif // __SYNTHLIB_TYPES_H__
