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

#include <stdio.h>

#include "renderBackend.h"
#include "synthlibVersion.h"

const char * synthlib_about_text(const char * appName) {
    static char text[320];

    // __DATE__ and __TIME__ are the moment THIS FILE was compiled, not the moment the build
    // finished. That is exact for do-vst3 and do-release, which compile everything from scratch
    // every run; an incremental Xcode build can leave it behind if nothing here changed. Hence
    // "Compiled", which is true in both cases, rather than "Built", which would not be.
    snprintf(text, sizeof(text),
             "%s\n\nVersion:  %s\nCompiled: %s %s\nRenderer: %s",
             (appName != NULL) ? appName : "SynthLib",
             SYNTHLIB_VERSION_STRING,
             __DATE__, __TIME__,
             gfx_backend_name(gfx_backend_current()));
    return text;
}
