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

// THE GLFW HALF OF THE MODIFIER SEAM, AND THE ONLY PART OF IT THAT KNOWS GLFW EXISTS.
//
// Separate from inputState.c on purpose. That file is the state and the predicates, with no platform
// header of any kind in it, which is what lets a VST3 plug-in link it and push from an NSEvent
// instead. This file is what a GLFW-hosted application adds on top, and a plug-in simply does not
// compile it.
//
// It is shared rather than written out in each application because the mapping is a DECISION, not a
// formality: which physical key counts as "Cmd" differs by platform, and two editors that each
// decided for themselves would eventually disagree. G2-Edit and SynthEdit now cannot.

#ifdef __cplusplus
extern "C" {
#endif

#include <GLFW/glfw3.h>

#include "synthlibGlobals.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "synthlibTypes.h"
#include "inputState.h"

// NOT A CAST, EVEN THOUGH BOTH SIDES ARE BIT FLAGS. GLFW orders them Shift, Control, Alt, Super
// (1, 2, 4, 8) while tModifierBits orders them Shift, Cmd, Alt, Ctrl — Cmd and Ctrl are swapped, so
// a cast would silently read Control as Command on every platform. It also whitelists: GLFW reports
// Caps Lock and Num Lock in the same word, and neither is a modifier this UI has any use for.
void set_modifier_state_from_glfw(int glfwMods) {
    uint32_t bits = (uint32_t)eModifierNone;

    if ((glfwMods & GLFW_MOD_SHIFT) != 0) {
        bits |= (uint32_t)eModifierShift;
    }

    if ((glfwMods & GLFW_MOD_SUPER) != 0) {
        bits |= (uint32_t)eModifierCmd;
    }

    if ((glfwMods & GLFW_MOD_ALT) != 0) {
        bits |= (uint32_t)eModifierAlt;
    }

    if ((glfwMods & GLFW_MOD_CONTROL) != 0) {
        bits |= (uint32_t)eModifierCtrl;
    }
    set_modifier_state(bits);
}

tCoord synthlib_window_to_logical(double x, double y) {
    GLFWwindow * window = (GLFWwindow *)synthlib_window();
    int          winW   = 0;
    int          winH   = 0;

    if (window == NULL) {
        return (tCoord){
            x, y
        };
    }
    glfwGetWindowSize(window, &winW, &winH);

    // The guards are not decoration: a window can report a zero dimension while it is being
    // minimised, and the un-guarded copy of this maths would have divided by it.
    return (tCoord){
        .x = (winW > 0) ? (x / (double)winW) * (get_render_width() / gGlobalGuiScale) : x,
        .y = (winH > 0) ? (y / (double)winH) * (get_render_height() / gGlobalGuiScale) : y,
    };
}

void synthlib_mouse_coord(tCoord * coord) {
    GLFWwindow * window = (GLFWwindow *)synthlib_window();
    double       x      = 0.0;
    double       y      = 0.0;

    if ((coord == NULL) || (window == NULL)) {
        return;
    }
    glfwGetCursorPos(window, &x, &y);
    *coord = synthlib_window_to_logical(x, y);
}

tMouseButton synthlib_mouse_button(int glfwButton, int glfwAction) {
    if (glfwAction == GLFW_PRESS) {
        if (glfwButton == GLFW_MOUSE_BUTTON_LEFT) {
            return mouseButtonLeftDown;
        }

        if (glfwButton == GLFW_MOUSE_BUTTON_RIGHT) {
            return mouseButtonRightDown;
        }
    } else if (glfwAction == GLFW_RELEASE) {
        if (glfwButton == GLFW_MOUSE_BUTTON_LEFT) {
            return mouseButtonLeftUp;
        }

        if (glfwButton == GLFW_MOUSE_BUTTON_RIGHT) {
            return mouseButtonRightUp;
        }
    }
    return mouseButtonNone;
}

#ifdef __cplusplus
}
#endif
