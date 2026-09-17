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
// Notes: Docs/code-notes/prefs.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_PREFS_H__
#define __SYNTHLIB_PREFS_H__

#include "synthlibTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §1

void prefs_init(const char * appName);

// Every set_* call rewrites the whole file immediately — these are small, infrequent writes
// (window resize/move, zoom, folder change), not a hot path, so there's no batching/dirty-flag.
void prefs_set_string(const char * key, const char * value);
void prefs_set_double(const char * key, double value);
void prefs_set_int(const char * key, long value);

bool prefs_has_key(const char * key);

// One key in ANOTHER app's prefs.txt (or this one's), read from disk and written straight back - for
// a setting two programs share, such as a plug-in and the application it came from.
void prefs_set_string_in(const char * appName, const char * key, const char * value);
const char * prefs_get_string_from(const char * appName, const char * key, const char * defaultValue);

// Returns defaultValue if the key isn't present or can't be parsed as the requested type. The
// string returned by prefs_get_string() is only valid until the next prefs_get_string()/
// prefs_set_string() call — copy it if the caller needs to keep it past that.
const char * prefs_get_string(const char * key, const char * defaultValue);

// Patch-name cache — same store mechanism, but a separate file (cache.txt) alongside prefs.txt.
// Kept apart because the cache is bulky and rewritten far more often than settings are, so its
// churn must never put prefs.txt at risk. Initialised by prefs_init(), no separate init call.
void cache_set_string(const char * key, const char * value);
const char * cache_get_string(const char * key, const char * defaultValue);
double prefs_get_double(const char * key, double defaultValue);
long prefs_get_int(const char * key, long defaultValue);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PREFS_H__
