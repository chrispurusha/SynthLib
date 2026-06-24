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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "geometry.h"

double value_to_angle(uint32_t value, uint32_t range) {
    if (range < 2) {
        fprintf(stderr, "value_to_angle: can't deal with a range of %u\n", range);
        exit(1);
    }
    return ((double)value * (135.0 * 2.0) / (double)(range - 1)) - 135.0;
}

uint32_t angle_to_value(double angle, uint32_t range) {
    if (angle > 135.0 && angle < 180.0) {
        angle = 135.0;
    } else if (angle >= 180.0 && angle < 360.0 - 135.0) {
        angle = 360.0 - 135.0;
    }
    angle = fmod(angle + 135.0, 360.0);

    uint32_t value = (uint32_t)floor((angle * (double)range) / 270.0);

    if (value >= range) {
        value = range - 1;
    }
    return value;
}

double calculate_mouse_angle(tCoord mouseCoord, tRectangle rectangle) {
    double centerX = rectangle.coord.x + (rectangle.size.w / 2.0);
    double centerY = rectangle.coord.y + (rectangle.size.h / 2.0);
    double dx      = mouseCoord.x - centerX;
    double dy      = mouseCoord.y - centerY;
    double angle   = atan2(dx, -dy) * (180.0 / M_PI);

    return (angle < 0) ? angle + 360.0 : angle;
}

bool within_rectangle(tCoord coord, tRectangle rectangle) {
    return coord.x >= rectangle.coord.x
           && coord.x <= rectangle.coord.x + rectangle.size.w
           && coord.y >= rectangle.coord.y
           && coord.y <= rectangle.coord.y + rectangle.size.h;
}

bool within_lower_half_of_rectangle(tCoord coord, tRectangle rectangle) {
    return within_rectangle(coord, rectangle)
           && coord.y >= rectangle.coord.y + rectangle.size.h / 2;
}
