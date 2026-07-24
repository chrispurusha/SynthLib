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

#ifndef __SYNTHLIB_PERSISTENCE_H__
#define __SYNTHLIB_PERSISTENCE_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Window position/size + dial-mode persistence — identical prefs.h keys ("windowX", "windowY",
// "windowWidth", "dialMode") and identical restore logic previously copy-pasted across G2-Edit's,
// EmuUtility's, and SynthEdit's own persistence.c. Each app also has its own extra settings (zoom
// factor, last-browsed folder, device config, etc.) that stay local — this only covers the shared
// core, operating on synthlib_window()/synthlib_dial_mode() (synthlibGlobals.h).
void synthlib_save_dial_mode(tDialMode mode);
void synthlib_save_window_size(int width);
void synthlib_save_window_pos(int x, int y);

// Call once at startup, after prefs_init() (prefs.h) and after init_graphics() has created the
// real window (synthlib_window() must already be non-NULL) — same "needs the window to already
// exist" requirement each app's own resize_window()/reposition_window() always had.
// targetFrameBuffWidth/targetFrameBuffHeight are the app's own TARGET_FRAME_BUFF_WIDTH/HEIGHT
// constants, needed to restore the saved width at the correct aspect ratio.
void synthlib_load_window_and_dial_mode(int targetFrameBuffWidth, int targetFrameBuffHeight);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PERSISTENCE_H__
