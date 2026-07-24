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

#include <stdio.h>

#include "synthlibDefs.h"
#include "geometry.h"
#include "clickRegion.h"

typedef struct {
    tRectangle    rect;
    eClickLayer   layer;
    tClickHandler handler;
    void *        userData;
} tClickRegion;

static tClickRegion sRegions[MAX_CLICK_REGIONS];
static uint32_t     sRegionCount = 0;

void clear_click_regions(void) {
    sRegionCount = 0;
}

void register_click_region(tRectangle rect, eClickLayer layer, tClickHandler handler, void * userData) {
    if (sRegionCount >= MAX_CLICK_REGIONS) {
        LOG_WARNING("MAX_CLICK_REGIONS exceeded, dropping region\n");
        return;
    }
    sRegions[sRegionCount].rect     = rect;
    sRegions[sRegionCount].layer    = layer;
    sRegions[sRegionCount].handler  = handler;
    sRegions[sRegionCount].userData = userData;
    sRegionCount++;
}

bool dispatch_click_region(tCoord coord, eClickPhase phase) {
    for (eClickLayer layer = eClickLayerModal; ; layer--) {
        for (int32_t i = (int32_t)sRegionCount - 1; i >= 0; i--) {
            if ((sRegions[i].layer == layer) && within_rectangle(coord, sRegions[i].rect)) {
                sRegions[i].handler(coord, phase, sRegions[i].userData);
                return true;
            }
        }

        if (layer == eClickLayerCanvas) {
            break;
        }
    }

    return false;
}

#ifdef __cplusplus
}
#endif
