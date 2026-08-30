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

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "synthlibHost.h"

static tSynthLibHost sHost = {0};

void synthlib_host_init(tSynthLibHost host) {
    sHost = host;
}

void synthlib_host_mouse_coord(tCoord * coord) {
    if (coord == NULL) {
        return;
    }

    if (sHost.mouseCoord != NULL) {
        sHost.mouseCoord(coord);
    } else {
        *coord = (tCoord){
            0
        };
    }
}

bool synthlib_host_pointer_captured(void) {
    return (sHost.pointerCaptured != NULL) ? sHost.pointerCaptured() : false;
}

#ifdef __cplusplus
}
#endif
