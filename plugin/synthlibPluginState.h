/*
 * SynthLib - the plug-in state blob and parameter formatting, shared by both wrappers.
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

// WHAT A HOST SAVES WITH ITS PROJECT, in one format for both plug-in formats.
//
// A VST3 stores it as an IBStream and an Audio Unit as CFData inside its ClassInfo dictionary, but
// the BYTES are the same either way - so a project saved in Live and one saved in Logic hold the
// identical thing, and the two wrappers cannot drift apart over what a saved plug-in means.
//
// Layout:
//
//     0   4   magic "SLP1"
//     4   4   parameter count, little-endian
//     8   8*N normalized parameter values, IEEE-754 doubles
//     ..  4   length of the plug-in's own blob
//     ..  n   the plug-in's own blob, exactly as getState() produced it
//
// LITTLE-ENDIAN AND NATIVE DOUBLES, deliberately: every machine these run on is little-endian
// (arm64 and x86_64 both), and a universal binary's two halves agree, so a project moves between
// them intact. Should a big-endian target ever appear this needs byte swapping and a bumped magic.
//
// A BLOB THAT DOES NOT START WITH THE MAGIC IS NOT AN ERROR. It is a project saved against an
// earlier build, when the state was nothing but the plug-in's own bytes - G2 Alike's was a bare
// patch path - so it is handed to the plug-in whole and the parameters stay at their defaults.
// That is what stops this change from emptying a slot in somebody's existing project.

#ifndef __SYNTHLIB_PLUGIN_STATE_H__
#define __SYNTHLIB_PLUGIN_STATE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "synthlibPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

// How many bytes the blob needs, and - when `out` is not NULL and `len` is at least that - writes
// it. Call once with out == NULL to size a buffer, then again to fill it.
size_t synthlib_state_write(const tSynthLibPluginDesc * desc, void * inst,
                            const double * params, void * out, size_t len);

// Parses a blob. `paramsOut` must have room for desc->numParams doubles and is left untouched for a
// legacy blob; `*paramCountOut` says how many were actually read. `*pluginData` points INTO `data`,
// so it is valid only as long as that buffer is.
//
// Returns false only for a blob that is malformed - one truncated mid-value, or claiming more
// parameters than could possibly fit in it. A short but consistent blob from a build with fewer
// parameters reads back what it has and returns true.
bool synthlib_state_read(const tSynthLibPluginDesc * desc, const void * data, size_t len,
                         double * paramsOut, uint32_t * paramCountOut,
                         const void ** pluginData, size_t * pluginLen);

// How a value reads, for a host that wants to print it. Asks the plug-in first; falls back to the
// plain value with the parameter's own unit after it, which is right for most parameters.
//
// Always writes something null-terminated when len > 0. Returns false only for an unknown id.
bool synthlib_param_text(const tSynthLibPluginDesc * desc, void * inst, uint32_t id,
                         double normalized, char * out, size_t len);

// The parameter table, looked up by id, or NULL. Shared so the two wrappers cannot disagree about
// what an out-of-range id means.
const tSynthLibParam * synthlib_param_at(const tSynthLibPluginDesc * desc, uint32_t id);

// 0..1 against the parameter's own displayed range, and back.
double synthlib_param_to_plain(const tSynthLibPluginDesc * desc, uint32_t id, double normalized);
double synthlib_param_to_normalized(const tSynthLibPluginDesc * desc, uint32_t id, double plain);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_STATE_H__
