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

#include "synthlibDefs.h"
#include "synthlibTypes.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "synthlibGlobals.h"
#include "inputState.h"   // ctrl_modifier_held() — ctrl turns the whole panel into a drag handle
#include "floatingPanel.h"

// Each newly placed panel is offset from the last so a second one does not land exactly on top of
// the first. Centring them all — which is what the modal versions did — is the one placement that
// guarantees they hide each other, and the complaint that started this work was precisely that these
// panels take the whole screen.
#define PANEL_CASCADE_STEP    (28.0)
#define PANEL_CASCADE_WRAP    (6)

static uint32_t   sCascade  = 0;

// Monotonic, so "most recently raised" is simply the largest. Never reset: at one raise per click it
// would take longer than any session to wrap a uint32_t.
//
// A panel is raised when it is OPENED, not when it is first placed. Placement happens inside the
// render pass, by which point that frame has already been sorted using the old order — so a newly
// opened panel was drawn BEHIND the others for that frame, and since the app only redraws on demand,
// that wrong stacking then stayed on screen until something else asked for a frame.
static uint32_t   sTopOrder = 0;

void floating_panel_raise(tFloatingPanel * panel) {
    sTopOrder++;
    panel->order = sTopOrder;
}

bool floating_panel_in_front_of(const tFloatingPanel * a, const tFloatingPanel * b) {
    return a->order > b->order;
}

// Zero size means "not set": fall back to the whole render area, which is what an app with no edge
// chrome wants and what every app did before bounds existed.
static tRectangle sBounds   = {0};

void floating_panel_set_bounds(tRectangle bounds) {
    sBounds = bounds;
}

static tRectangle panel_bounds(void) {
    if ((sBounds.size.w <= 0.0) || (sBounds.size.h <= 0.0)) {
        return (tRectangle){{
                                0.0, 0.0
                            }, {
                                get_render_width() / gGlobalGuiScale, get_render_height() / gGlobalGuiScale
                            }
        };
    }
    return sBounds;
}

tRectangle floating_panel_place(tFloatingPanel * panel, double width, double height) {
    tRectangle bounds  = panel_bounds();
    double     renderW = bounds.size.w;
    double     renderH = bounds.size.h;

    panel->rect.size = (tSize){
        width, height
    };

    if (panel->placed == false) {
        double step = (double)(sCascade % PANEL_CASCADE_WRAP) * PANEL_CASCADE_STEP;

        panel->rect.coord = (tCoord){
            bounds.coord.x + ((renderW - width) / 2.0) + step,
            bounds.coord.y + ((renderH - height) / 2.0) + step
        };
        panel->placed     = true;
        sCascade++;
    }
    // KEPT WHOLLY INSIDE THE BOUNDS, and re-clamped every frame rather than only on placement — a
    // window resize can otherwise strand a panel outside them, and a panel parked off-screen cannot
    // be dragged back. Because this runs after the drag has moved the panel, it is also what stops a
    // drag at the edge: the panel simply will not go further.
    //
    // This used to clamp only enough to keep the TITLE BAR reachable, letting the rest hang off the
    // bottom and right. That was fine against a bare window edge and wrong against chrome — a panel
    // lying over the canvas scrollbars reads as a mistake rather than as a panel in front.
    double minX = bounds.coord.x;
    double minY = bounds.coord.y;
    double maxX = (bounds.coord.x + renderW) - width;
    double maxY = (bounds.coord.y + renderH) - height;

    // Bigger than the space it must live in: pin to the top-left and let it overflow. Refusing to
    // place it is not an option, and the top-left corner is the one that keeps the title bar — and
    // so the close button and the drag handle — where they can be reached.
    if (maxX < minX) {
        maxX = minX;
    }

    if (maxY < minY) {
        maxY = minY;
    }

    if (panel->rect.coord.x > maxX) {
        panel->rect.coord.x = maxX;
    }

    if (panel->rect.coord.y > maxY) {
        panel->rect.coord.y = maxY;
    }

    if (panel->rect.coord.x < minX) {
        panel->rect.coord.x = minX;
    }

    if (panel->rect.coord.y < minY) {
        panel->rect.coord.y = minY;
    }
    return panel->rect;
}

bool floating_panel_contains(const tFloatingPanel * panel, tCoord coord) {
    return within_rectangle(coord, panel->rect);
}

bool floating_panel_press(tFloatingPanel * panel, tCoord coord) {
    // The close button is part of the title bar's rectangle, so it has to be carved out before the
    // drag test or a press on it becomes a move and the panel can never be closed.
    if (within_rectangle(coord, panel->closeRect)) {
        floating_panel_raise(panel);
        return false;   // not a move — the panel's own handler deals with it
    }
    bool onTitle = within_rectangle(coord, panel->titleBarRect);
    bool ctrlAny = ctrl_modifier_held() && within_rectangle(coord, panel->rect);

    // Raise on ANY press the panel claims, not just one that starts a move — clicking a key or a
    // knob on a partly covered panel should bring it forward, which is what makes two overlapping
    // panels usable at all.
    if (within_rectangle(coord, panel->rect)) {
        floating_panel_raise(panel);
    }

    if (!onTitle && !ctrlAny) {
        return false;
    }
    panel->dragging       = true;
    panel->dragMouseStart = coord;
    panel->dragPanelStart = panel->rect.coord;
    return true;
}

bool floating_panel_drag(tFloatingPanel * panel, tCoord coord) {
    if (panel->dragging == false) {
        return false;
    }
    panel->rect.coord.x = panel->dragPanelStart.x + (coord.x - panel->dragMouseStart.x);
    panel->rect.coord.y = panel->dragPanelStart.y + (coord.y - panel->dragMouseStart.y);
    synthlib_request_redraw();
    return true;
}

void floating_panel_release(tFloatingPanel * panel) {
    panel->dragging = false;
}

// Insertion sort: the list is three or four entries long, it runs once per frame and once per click,
// and being stable keeps two never-raised panels in their registration order rather than shuffling
// them about between frames.
void floating_panel_sort(tFloatingPanelEntry * entries, uint32_t count) {
    for (uint32_t i = 1; i < count; i++) {
        tFloatingPanelEntry key = entries[i];
        uint32_t            j   = i;

        while ((j > 0) && floating_panel_in_front_of(entries[j - 1].panel, key.panel)) {
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = key;
    }
}

void floating_panel_unplace(tFloatingPanel * panel) {
    panel->placed = false;
}

#ifdef __cplusplus
}
#endif

eFloatingPanelHit floating_panel_mouse(tFloatingPanel * panel, tCoord coord, tMouseButton mouseButton, bool stillOurs) {
    if (panel == NULL) {
        return eFloatingPanelPassThrough;
    }

    if (mouseButton == mouseButtonLeftDown) {
        // Title bar, or ctrl-drag anywhere on the face: a move, not a click on the content.
        if (floating_panel_press(panel, coord)) {
            return eFloatingPanelConsumed;
        }

        if (!floating_panel_contains(panel, coord)) {
            return eFloatingPanelPassThrough;
        }
    } else if (mouseButton == mouseButtonLeftUp) {
        bool wasDragging = panel->dragging;

        floating_panel_release(panel);

        // A release that ends a move belongs to the move, wherever the pointer has got to by then.
        if (wasDragging) {
            return eFloatingPanelConsumed;
        }

        if (!floating_panel_contains(panel, coord) && !stillOurs) {
            return eFloatingPanelPassThrough;
        }
    }
    return eFloatingPanelContent;
}
