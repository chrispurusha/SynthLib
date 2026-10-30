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
// Notes: Docs/code-notes/synthlibVersion.h.md - "// notes §k" refers there.

#ifndef SYNTHLIB_VERSION_H
#define SYNTHLIB_VERSION_H

#include "synthlibDefs.h"

// Set by the application, not at SynthLib's compile time - see SYNTHLIB_APP_INIT() below.
void synthlib_set_app_info(const char * version, const char * built);

// notes §2
const char * synthlib_about_text(const char * appName);

// Both macros are expanded in the APPLICATION'S source, so these #ifdefs read the application's own
// settings. Call one early at start-up. The version comes from SYNTHLIB_VERSION_STRING where the
// application's build defines it; do-release does.
#ifndef SYNTHLIB_VERSION_STRING
#define SYNTHLIB_VERSION_STRING    "development build"
#endif

#if defined (ENABLE_LOG_MODULE_DATA)
#define SYNTHLIB_APP_LOG_LEVEL    eSynthLibLogModuleData
#elif defined (ENABLE_LOG_DEBUG)
#define SYNTHLIB_APP_LOG_LEVEL    eSynthLibLogDebug
#else
#define SYNTHLIB_APP_LOG_LEVEL    eSynthLibLogNormal
#endif

#define SYNTHLIB_LOG_INIT()    do {gSynthLibLogLevel = SYNTHLIB_APP_LOG_LEVEL;} while (0)

#define SYNTHLIB_APP_INIT()                                                   \
   do {synthlib_set_app_info(SYNTHLIB_VERSION_STRING, __DATE__ " " __TIME__); \
       SYNTHLIB_LOG_INIT();} while (0)

#endif // SYNTHLIB_VERSION_H
