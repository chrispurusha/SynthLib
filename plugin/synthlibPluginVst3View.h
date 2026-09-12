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
// Notes: Docs/code-notes/synthlibPluginVst3View.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_PLUGIN_VST3_VIEW_H__
#define __SYNTHLIB_PLUGIN_VST3_VIEW_H__

#include "pluginterfaces/gui/iplugview.h"

#include "synthlibPlugin.h"

// notes §1
Steinberg::IPlugView * synthlib_vst3_create_view(const tSynthLibPluginDesc * desc, void * inst,
                                                 Steinberg::FUnknown * owner, double initialWidth,
                                                 double * widthSink);

#endif // __SYNTHLIB_PLUGIN_VST3_VIEW_H__
