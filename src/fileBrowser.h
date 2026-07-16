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

#ifndef __SYNTHLIB_FILE_BROWSER_H__
#define __SYNTHLIB_FILE_BROWSER_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// In-window, cross-platform replacement for native NSOpenPanel/NSSavePanel-style dialogs — a
// real directory browser (not just a typed-path field), drawn with the same GLFW/OpenGL
// primitives as everything else, so it behaves identically on macOS/Windows/Linux. Directory
// listing is backed by std::filesystem internally (fileBrowser.cpp); this header stays a plain C
// API like every other SynthLib component.
//
// Interaction model, deliberately simpler than a native panel to avoid needing double-click
// timing or drag-selection: clicking a folder row always navigates into it; clicking a file row
// selects it (Open mode) or copies its name into the filename field (Save mode); a single
// Confirm button commits — in Choose Folder mode it always targets the currently-open directory,
// not a clicked row, matching how NSOpenPanel's own folder mode works.
//
// The embedding app must provide the same two symbols contextMenu.c/menuBar.c require (see
// contextMenu.h) — gReDraw and get_global_gui_scaled_mouse_coord() — and must, once per frame,
// call render_file_browser(); route mouse-down clicks through handle_file_browser_click() ahead
// of other click handling (it returns true if the click landed inside the browser, whether or
// not it hit something specific); route key/char events through handle_file_browser_key()/
// handle_file_browser_char(); and route scroll-wheel deltas through handle_file_browser_scroll().
// All four are safe to call unconditionally — they no-op when file_browser_active() is false.

typedef enum {
    fileBrowserModeOpenFile = 0,   // Pick an existing file
    fileBrowserModeSaveFile = 1,   // Choose a folder + type a filename (may not yet exist)
    fileBrowserModeChooseFolder = 2, // Pick an existing folder — Confirm always targets the open directory
} tFileBrowserMode;

// NULL path means the user cancelled.
typedef void (*tFileBrowserCallback)(const char * path);

void open_file_browser_read(tFileBrowserCallback callback);
void open_file_browser_write(tFileBrowserCallback callback, const char * defaultName);
void open_file_browser_folder(tFileBrowserCallback callback, const char * title);

// fileBrowser.c has no cross-launch persistence of its own (SynthLib doesn't know how a given
// app prefers to store preferences — NSUserDefaults, a config file, ...). Instead: the embedding
// app calls set_file_browser_start_directory() once at startup with whatever it last saved, to
// seed where the browser opens; and registers a callback via
// set_file_browser_directory_changed_callback(), invoked with the new directory every time a
// browse session completes successfully, so the app can persist it for next launch.
void set_file_browser_start_directory(const char * path);
typedef void (*tFileBrowserDirectoryChangedCallback)(const char * path);
void set_file_browser_directory_changed_callback(tFileBrowserDirectoryChangedCallback callback);

bool file_browser_active(void);
void handle_file_browser_mouse_down(tCoord coord);
bool handle_file_browser_click(tCoord coord);
void handle_file_browser_key(int key, int action);
void handle_file_browser_char(unsigned int codepoint);
void handle_file_browser_scroll(double yDelta);
void render_file_browser(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_FILE_BROWSER_H__
