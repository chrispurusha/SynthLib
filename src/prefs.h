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

#ifndef __SYNTHLIB_PREFS_H__
#define __SYNTHLIB_PREFS_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Small flat-file key/value settings persistence — a cross-platform stand-in for
// NSUserDefaults/the Windows Registry/etc, backed by one plain "key=value" text file per app
// under a per-OS standard config directory (see prefs.cpp's config_dir() for exact paths, and a
// TODO there on the Windows/Linux branches — written to the documented convention for each, but
// not build/run-verified since no SynthLib-based app has a Windows or Linux target yet).
//
// prefs_init() must be called once at startup with the app's own name (e.g. "G2-Edit") — each app
// embedding SynthLib gets its own settings file, since G2-Edit/Z1-Edit/EmuUtility share this code
// but not their preferences. A missing file (first run) just starts with no entries; get_* calls
// return the supplied default until something is written.

void prefs_init(const char * appName);

// Every set_* call rewrites the whole file immediately — these are small, infrequent writes
// (window resize/move, zoom, folder change), not a hot path, so there's no batching/dirty-flag.
void prefs_set_string(const char * key, const char * value);
void prefs_set_double(const char * key, double value);
void prefs_set_int(const char * key, long value);

bool prefs_has_key(const char * key);

// Returns defaultValue if the key isn't present or can't be parsed as the requested type. The
// string returned by prefs_get_string() is only valid until the next prefs_get_string()/
// prefs_set_string() call — copy it if the caller needs to keep it past that.
const char * prefs_get_string(const char * key, const char * defaultValue);
double       prefs_get_double(const char * key, double defaultValue);
long         prefs_get_int(const char * key, long defaultValue);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PREFS_H__
