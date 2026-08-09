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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "synthlibTypes.h"

extern double       gGlobalGuiScale;
extern tScrollState gScrollState;

double value_to_angle(uint32_t value, uint32_t range);
uint32_t angle_to_value(double angle, uint32_t range);

// HOW MANY PIXELS OF TRAVEL AN INCREMENTAL DIAL DRAG SPREADS THE FULL RANGE OVER: 200 normally, and
// with Shift held the larger of the dial's own range and a fine-drag floor. Divide the drag delta by
// this — so a bigger number is a SLOWER, finer drag.
//
// Shared rather than written out per application because it is a POLICY, and one the two editors must
// not disagree about: "Shift = finer" has to feel the same in G2-Edit as in SynthEdit, and the floor
// has to stay above the unmodified 200 or Shift would speed the drag UP instead of slowing it. That
// last part is not hypothetical — the first version of this floored at the range itself, which for any
// dial narrower than 200 units evaluated to exactly 200 and made Shift do nothing whatsoever. Nearly
// every dial in both editors is narrower than 200 (G2 parameters are 0-127), so "nothing whatsoever"
// was the behaviour almost everywhere.
//
// Reads the pushed modifier state (inputState.h), so it needs no window and works in a plug-in.
double dial_drag_pixels_for_full_range(uint32_t range);
double calculate_mouse_angle(tCoord mouseCoord, tRectangle rectangle);
bool within_rectangle(tCoord coord, tRectangle rectangle);
bool within_lower_half_of_rectangle(tCoord coord, tRectangle rectangle);

#ifdef __cplusplus
}
#endif

#endif // __GEOMETRY_H__
