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

// eClickPress CAPTURES: the region that handles a press owns the matching release, wherever the
// cursor has moved to by then (standard mouse-capture behaviour — the registry used to look the
// release up by coordinate like any other event, so a press-and-drag-off delivered the release to
// whatever happened to be under the cursor, or to nothing at all). The captured handler is called
// with eClickRelease if the release landed back inside its rect, or eClickReleaseOutside if not.
//
// A handler that only tests `phase == eClickRelease` therefore gets press-and-drag-off-cancels for
// free, which is what a click is normally expected to do. Handlers that must act on the release
// either way — anything that latched state or sent something on the press that has to be undone,
// e.g. a device key-down needing its key-up — should treat both the same, typically by testing
// `phase == eClickPress` for the down case and handling everything else as the up case.
typedef enum {
    eClickPress = 0,
    eClickRelease,        // release inside the capturing region — the "clicked" case
    eClickReleaseOutside, // release after dragging off the capturing region — the "cancelled" case
    eClickDrag
} eClickPhase;

typedef void (*tClickHandler)(tCoord coord, eClickPhase phase, void * userData);

// Drops any in-flight press capture. Only needed when something outside the registry takes over
// input mid-gesture (a modal opening on top, a device disconnect abandoning the interaction) and the
// captured handler must NOT be told about the eventual release. Normal press/release pairs clear the
// capture themselves.
void cancel_click_region_capture(void);

void clear_click_regions(void);
void register_click_region(tRectangle rect, eClickLayer layer, tClickHandler handler, void * userData);
bool dispatch_click_region(tCoord coord, eClickPhase phase);

// WHAT IS UNDER THIS COORDINATE, without delivering anything to it.
//
// dispatch_click_region() answers an EVENT: it finds the front-most region and calls its handler.
// Several things need the same question answered without an event — a right-click that wants to open
// a menu FOR whatever is under the cursor, a keyboard nudge that acts on the widget being pointed at,
// an overlay deciding what to describe. Each of those used to walk the app's own render-time
// rectangle arrays instead, which is a second, parallel description of where every widget is: it can
// disagree with this registry about z-order, it goes stale in exactly the frames where a widget was
// drawn but deliberately not registered, and it kept G2-Edit's 6MB gParamRectangle readable.
//
// Answers with the SAME front-to-back walk dispatch uses, so a query and a click can never disagree
// about which widget is in front. Returns the region's userData — the app's own handle for whatever
// it registered — or NULL if nothing is there. Does not touch the press capture.
void * click_region_at(tCoord coord);
// The rectangle of the region that owns the press currently in flight, if there is one.
//
// dispatch_click_region() already captures the whole region on eClickPress — it has to, so the
// matching release goes to the same handler wherever the cursor ends up. This exposes the rect it
// captured, so a handler starting a gesture can keep the geometry it was clicked on without going
// back to whatever array the app happened to record it in. That matters for a rotary drag, which
// needs the widget's centre on every mouse-move: re-deriving it per event is a lookup that can go
// stale or disagree, where the press already knew the answer exactly.
//
// Valid only from inside a press handler, which is the only time a capture is armed. Returns false
// otherwise and leaves rect untouched.
bool click_region_capture_rect(tRectangle * rect);


// The same, restricted to one layer: "what canvas widget is under the pointer" while ignoring the
// chrome and popups drawn over it.
void * click_region_at_layer(tCoord coord, eClickLayer layer);


#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_CLICK_REGION_H__
