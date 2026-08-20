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
static uint32_t     sRegionCount   = 0;

// In-flight press capture — see eClickPhase's comment in clickRegion.h. Deliberately a COPY of the
// region rather than an index into sRegions: regions are rebuilt from scratch every frame
// (clear_click_regions), so an index would dangle or silently point at a different widget by the
// time the release arrives. The copy also keeps the capture valid if the widget stops registering
// mid-gesture (scrolled away, page switched), which is exactly when the release still has to be
// delivered so the handler can unwind whatever the press started.
static tClickRegion sCapture       = {0};
static bool         sCaptureActive = false;

void cancel_click_region_capture(void) {
    sCaptureActive = false;
}

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

// The one walk both the query and the dispatch use. Front to back: modal layer first, and within a
// layer the most recently registered wins, which is registration order = paint order = topmost.
static const tClickRegion * region_at(tCoord coord, eClickLayer onlyLayer, bool anyLayer) {
    for (eClickLayer layer = eClickLayerModal; ; layer--) {
        if (anyLayer || (layer == onlyLayer)) {
            for (int32_t i = (int32_t)sRegionCount - 1; i >= 0; i--) {
                if ((sRegions[i].layer == layer) && within_rectangle(coord, sRegions[i].rect)) {
                    return &sRegions[i];
                }
            }
        }

        if (layer == eClickLayerCanvas) {
            break;
        }
    }

    return NULL;
}

bool click_region_capture_rect(tRectangle * rect) {
    if ((rect == NULL) || !sCaptureActive) {
        return false;
    }
    *rect = sCapture.rect;
    return true;
}

void * click_region_at(tCoord coord) {
    const tClickRegion * region = region_at(coord, eClickLayerCanvas, true);

    return (region != NULL) ? region->userData : NULL;
}

void * click_region_at_layer(tCoord coord, eClickLayer layer) {
    const tClickRegion * region = region_at(coord, layer, false);

    return (region != NULL) ? region->userData : NULL;
}

bool dispatch_click_region(tCoord coord, eClickPhase phase) {
    // A captured press owns its release outright — no coordinate lookup, so dragging off the widget
    // (or onto a different one) still delivers to whoever started the gesture.
    if ((phase == eClickRelease) && sCaptureActive) {
        bool          inside   = within_rectangle(coord, sCapture.rect);
        tClickHandler handler  = sCapture.handler;
        void *        userData = sCapture.userData;

        // Cleared BEFORE the call: a handler is free to open a modal, trigger a rescan, or otherwise
        // re-enter dispatch, and must not find a stale capture still armed when it does.
        sCaptureActive = false;
        handler(coord, inside ? eClickRelease : eClickReleaseOutside, userData);
        return true;
    }

    for (eClickLayer layer = eClickLayerModal; ; layer--) {
        for (int32_t i = (int32_t)sRegionCount - 1; i >= 0; i--) {
            if ((sRegions[i].layer == layer) && within_rectangle(coord, sRegions[i].rect)) {
                if (phase == eClickPress) {
                    sCapture       = sRegions[i];
                    sCaptureActive = true;
                }
                sRegions[i].handler(coord, phase, sRegions[i].userData);
                return true;
            }
        }

        if (layer == eClickLayerCanvas) {
            break;
        }
    }

    // A press that hit nothing leaves no capture, so the matching release falls through to the
    // coordinate lookup above exactly as it always did — apps that route releases here for widgets
    // whose press was consumed elsewhere (before dispatch_click_region was ever reached) keep their
    // existing behaviour.
    return false;
}

#ifdef __cplusplus
}
#endif
