/*
 * SynthLib - the Audio Unit wrapper's few internal seams.
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
// Notes: Docs/code-notes/synthlibPluginAu.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_PLUGIN_AU_H__
#define __SYNTHLIB_PLUGIN_AU_H__

#include <CoreFoundation/CoreFoundation.h>

#include "synthlibPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

// notes §1
CFStringRef synthlib_au_view_class_name(void);

// notes §2
typedef struct {
    const tSynthLibPluginDesc * desc;
    void *                      inst;
} tSynthLibAuHandle;

#define kSynthLibAuProperty_Instance    (64100)

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_AU_H__
