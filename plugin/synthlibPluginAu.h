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

#ifndef __SYNTHLIB_PLUGIN_AU_H__
#define __SYNTHLIB_PLUGIN_AU_H__

#include <CoreFoundation/CoreFoundation.h>

#include "synthlibPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

// HOW THE HOST FINDS OUR EDITOR. kAudioUnitProperty_CocoaUI answers with a bundle and the NAME of an
// Objective-C class inside it, so the name has to travel from the Objective-C file that defines the
// class to the C file that reports the property.
//
// IT IS PER PLUG-IN, NOT FIXED, and that is the whole reason it is a function rather than a
// constant. Objective-C class names are global to the PROCESS: two SynthLib-based Audio Units
// loaded in the same host, each defining a class called "SynthLibAUView", would collide, one would
// silently win, and a host would open one plug-in's editor from inside the other. Each build sets
// SYNTHLIB_AU_VIEW_CLASS to its own name - see do-plugin.
//
// Returned +0; the caller does not release it.
CFStringRef synthlib_au_view_class_name(void);

// THE PRIVATE PROPERTY THE EDITOR FETCHES ITS PLUG-IN THROUGH. A Cocoa view factory is handed an
// AudioUnit and nothing else, so this is how it gets from that back to what the wrapper created.
// Apple reserves property ids below 64000; this sits well above.
//
// THE DESCRIPTOR TRAVELS WITH THE INSTANCE, because one binary may register several plug-ins and
// they do not share editor geometry - an effect and an instrument variant are different sizes and
// remember their widths separately. Reaching for variant 0 instead would open the wrong one's editor
// for every variant after the first.
typedef struct {
    const tSynthLibPluginDesc * desc;
    void *                      inst;
} tSynthLibAuHandle;

#define kSynthLibAuProperty_Instance    (64100)

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_AU_H__
