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

#include "prefs.h"
#include "synthlibGlobals.h"
#include "synthlibPersistence.h"

void synthlib_save_dial_mode(tDialMode mode) {
    prefs_set_int("dialMode", (long)mode);
}

void synthlib_save_window_size(int width) {
    prefs_set_int("windowWidth", width);
}

void synthlib_save_window_pos(int x, int y) {
    prefs_set_int("windowX", x);
    prefs_set_int("windowY", y);
}

void synthlib_load_window_and_dial_mode(int targetFrameBuffWidth, int targetFrameBuffHeight) {
    synthlib_set_dial_mode((tDialMode)prefs_get_int("dialMode", (long)synthlib_dial_mode()));

    long savedW = prefs_get_int("windowWidth", 0);

    if (savedW > 0) {
        int savedH = (int)savedW * targetFrameBuffHeight / targetFrameBuffWidth;

        glfwSetWindowSize((GLFWwindow *)synthlib_window(), (int)savedW, savedH);
    }

    if (prefs_has_key("windowX") && prefs_has_key("windowY")) {
        int savedX = (int)prefs_get_int("windowX", 0);
        int savedY = (int)prefs_get_int("windowY", 0);

        glfwSetWindowPos((GLFWwindow *)synthlib_window(), savedX, savedY);
    }
}

#ifdef __cplusplus
}
#endif
