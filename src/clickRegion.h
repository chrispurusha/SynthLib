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
// Notes: Docs/code-notes/clickRegion.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_CLICK_REGION_H__
#define __SYNTHLIB_CLICK_REGION_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §1
typedef enum {
    eClickLayerCanvas = 0, // scrollable patch/module canvas content
    eClickLayerPanel,      // static chrome: menu bar, side panels, dial grids
    eClickLayerModal,      // context menus, value-menu popups, file/bank browser overlays
    eClickLayerCount
} eClickLayer;

// notes §2
typedef enum {
    eClickPress = 0,
    eClickRelease,        // release inside the capturing region — the "clicked" case
    eClickReleaseOutside, // release after dragging off the capturing region — the "cancelled" case
    eClickDrag
} eClickPhase;

typedef void (*tClickHandler)(tCoord coord, eClickPhase phase, void * userData);

// notes §3
void cancel_click_region_capture(void);

void clear_click_regions(void);
void register_click_region(tRectangle rect, eClickLayer layer, tClickHandler handler, void * userData);
// notes §4
void set_click_region_clip(const tRectangle * clip);

bool dispatch_click_region(tCoord coord, eClickPhase phase);

// notes §5
void * click_region_at(tCoord coord);
// notes §6
bool click_region_capture_rect(tRectangle * rect);


// The same, restricted to one layer: "what canvas widget is under the pointer" while ignoring the
// chrome and popups drawn over it.
void * click_region_at_layer(tCoord coord, eClickLayer layer);


#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_CLICK_REGION_H__
