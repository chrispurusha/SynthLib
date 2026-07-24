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

#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>

#include "synthlibDefs.h"
#include "geometry.h"
#include "utilsGraphics.h"
#include "synthlibScale.h"

// 2.0 (Retina-only) matches every embedding app's own previous hardcoded default — overwritten by
// a real value the first time synthlib_scale_query_initial()/synthlib_scale_set_content_scale()
// runs, same as before this was pushed down from each app's graphics.cpp.
static float gContentScale        = 2.0f;
static int   gTargetFrameBuffWidth = 1;

void synthlib_scale_init(int targetFrameBuffWidth) {
    gTargetFrameBuffWidth = (targetFrameBuffWidth > 0) ? targetFrameBuffWidth : 1;
}

void synthlib_scale_query_initial(void * glfwWindow) {
    float xscale = 0.0f;
    float yscale = 0.0f;

    glfwGetWindowContentScale((GLFWwindow *)glfwWindow, &xscale, &yscale);

    if (xscale > 0.0f) {
        gContentScale = xscale;
    }
}

void synthlib_scale_update(int width, int height) {
    glViewport(0, 0, width, height);

    set_render_width(width);
    set_render_height(height);
    gGlobalGuiScale = (double)gContentScale * (double)width / (double)gTargetFrameBuffWidth;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void synthlib_scale_set_content_scale(void * glfwWindow, float xscale) {
    gContentScale = xscale;

    int fbWidth  = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize((GLFWwindow *)glfwWindow, &fbWidth, &fbHeight);
    synthlib_scale_update(fbWidth, fbHeight);
}

#ifdef __cplusplus
}
#endif
