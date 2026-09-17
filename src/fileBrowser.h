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
// Notes: Docs/code-notes/fileBrowser.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_FILE_BROWSER_H__
#define __SYNTHLIB_FILE_BROWSER_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §1

typedef enum {
    fileBrowserModeOpenFile     = 0, // Pick an existing file
    fileBrowserModeSaveFile     = 1, // Choose a folder + type a filename (may not yet exist)
    fileBrowserModeChooseFolder = 2, // Pick an existing folder — Confirm always targets the open directory
} tFileBrowserMode;

// NULL path means the user cancelled.
typedef void (*tFileBrowserCallback)(const char * path);

void open_file_browser_read(tFileBrowserCallback callback);
void open_file_browser_write(tFileBrowserCallback callback, const char * defaultName);
void open_file_browser_folder(tFileBrowserCallback callback, const char * title);

// notes §2
void set_file_browser_start_directory(const char * path);
typedef void (*tFileBrowserDirectoryChangedCallback)(const char * path);
void set_file_browser_directory_changed_callback(tFileBrowserDirectoryChangedCallback callback);
// Asked each time the browser opens; a non-NULL existing folder becomes where it starts.
typedef const char * (*tFileBrowserStartDirectoryProvider)(void);
void set_file_browser_start_directory_provider(tFileBrowserStartDirectoryProvider provider);

bool file_browser_active(void);
void handle_file_browser_mouse_down(tCoord coord);
bool handle_file_browser_mouse_move(tCoord coord);
bool handle_file_browser_click(tCoord coord);
void handle_file_browser_key(int key, int action);
void handle_file_browser_char(unsigned int codepoint);
void handle_file_browser_scroll(double yDelta);
void render_file_browser(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_FILE_BROWSER_H__
