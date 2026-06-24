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

#ifndef __GEOMETRY_H__
#define __GEOMETRY_H__

#include <stdint.h>
#include <stdbool.h>

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

#ifdef __cplusplus
extern "C" {
#endif

double value_to_angle(uint32_t value, uint32_t range);
uint32_t angle_to_value(double angle, uint32_t range);
double calculate_mouse_angle(tCoord mouseCoord, tRectangle rectangle);
bool within_rectangle(tCoord coord, tRectangle rectangle);
bool within_lower_half_of_rectangle(tCoord coord, tRectangle rectangle);

#ifdef __cplusplus
}
#endif

#endif // __GEOMETRY_H__
