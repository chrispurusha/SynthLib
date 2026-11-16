/*
 * SynthLib - shared code for the synth editors and their plug-ins.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If
 * not, see <https://www.gnu.org/licenses/>.
 */
// Notes: Docs/code-notes/synthlibResizeGrip.m.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_RESIZE_GRIP_H__
#define __SYNTHLIB_RESIZE_GRIP_H__

#import <Cocoa/Cocoa.h>

#include "synthlibPlugin.h"

// notes §1: a grip in the bottom-right corner of parent, above everything in it, that resizes the editor by
// asking the wrapper (synthlib_plugin_request_resize) as it is dragged - for hosts whose window has no corner to drag
#ifdef __cplusplus
extern "C" {
#endif

NSView * synthlib_resize_grip_attach(NSView * parent, const tSynthLibPluginDesc * desc, void * inst);   // the grip, or nil

#ifdef __cplusplus
}
#endif

#endif
