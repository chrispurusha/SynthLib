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

#ifdef __cplusplus
}
#endif
