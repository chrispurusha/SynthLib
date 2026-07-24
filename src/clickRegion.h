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

#ifndef __SYNTHLIB_CLICK_REGION_H__
#define __SYNTHLIB_CLICK_REGION_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Generic clickable-rectangle registry, replacing scattered per-widget
// "if (within_rectangle(coord, thing->rectangle))" checks in each app's
// mouseHandle.c. Nothing here knows what a "module" or "dial" is (same
// principle as contextMenu.h) — a render function registers a rect, layer,
// and callback right where it already computes that rect for drawing; the
// app's mouse callback asks dispatch_click_region() to find and fire the
// match instead of re-deriving hit-testing itself.
//
// Lifecycle, once per frame:
//   1. clear_click_regions() — call once at the start of the render pass.
//   2. register_click_region(...) — call from each render function, right
//      where it already computes the rect it's about to draw. Because this
//      happens fresh every frame, "activating/deactivating" a widget on the
//      fly needs no separate enable/disable call — a render function simply
//      skips registering (while still drawing, e.g. greyed out) when a
//      widget is currently non-interactive.
//   3. dispatch_click_region(...) — call from the app's GLFW mouse callback.
//
// Layer order is fixed priority (eClickLayerModal checked before
// eClickLayerPanel before eClickLayerCanvas), matching how popups must win
// over whatever they're drawn on top of. Within a layer, the most recently
// registered region wins ties on overlapping rects — i.e. registration order
// should match paint order (last drawn = topmost = checked first), the same
// invariant apps already rely on today (e.g. morph group dials drawn after,
// and hit-tested before, regular module dials).
typedef enum {
    eClickLayerCanvas = 0, // scrollable patch/module canvas content
    eClickLayerPanel,      // static chrome: menu bar, side panels, dial grids
    eClickLayerModal,      // context menus, value-menu popups, file/bank browser overlays
    eClickLayerCount
} eClickLayer;

typedef enum {
    eClickPress = 0,
    eClickRelease,
    eClickDrag
} eClickPhase;

typedef void (*tClickHandler)(tCoord coord, eClickPhase phase, void * userData);

void clear_click_regions(void);
void register_click_region(tRectangle rect, eClickLayer layer, tClickHandler handler, void * userData);
bool dispatch_click_region(tCoord coord, eClickPhase phase);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_CLICK_REGION_H__
