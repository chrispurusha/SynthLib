/*
 * SynthLib - the VST3 editor window, written once for every plug-in in these projects.
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

#ifndef __SYNTHLIB_PLUGIN_VST3_VIEW_H__
#define __SYNTHLIB_PLUGIN_VST3_VIEW_H__

#include "pluginterfaces/gui/iplugview.h"

#include "synthlibPlugin.h"

// Wraps the NSView the plug-in's own createView() builds in the IPlugView a VST3 host wants. Defined
// in synthlibPluginVst3View.mm because it touches Cocoa; declared here so the wrapper itself needs
// no Objective-C.
//
// The returned view is owned by the caller (refcount 1) and is handed straight back to the host.
// May be NULL, which the caller reports to the host as "no editor".
Steinberg::IPlugView * synthlib_vst3_create_view(const tSynthLibPluginDesc * desc, void * inst);

#endif // __SYNTHLIB_PLUGIN_VST3_VIEW_H__
