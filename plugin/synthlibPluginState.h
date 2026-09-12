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
// Notes: Docs/code-notes/synthlibPluginState.h.md - "// notes §k" refers there.

// notes §1

#ifndef __SYNTHLIB_PLUGIN_STATE_H__
#define __SYNTHLIB_PLUGIN_STATE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "synthlibPlugin.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------------------------------------
// The parameter store
// ------------------------------------------------------------------------------------------------

// notes §2
typedef struct {
    uint32_t id;
    uint32_t index;
} tSynthLibParamSlot;

typedef struct {
    uint32_t             count;
    uint32_t *           ids;           // [index]
    uint32_t *           flags;         // [index] SYNTHLIB_PARAM_*
    double *             values;        // [index] normalized - use the accessors
    tSynthLibParamSlot * byId;          // sorted by id, for the lookup
    bool                 identity;      // every id is its own index, so no lookup is needed
} tSynthLibParamStore;

// Describes every parameter and sets each to its default. False only for an allocation failure.
bool     synthlib_params_init(tSynthLibParamStore * store, const tSynthLibPluginDesc * desc, void * inst);
void     synthlib_params_free(tSynthLibParamStore * store);

// The index of a parameter id, or -1 for one this plug-in does not have - which is what a project
// saved against a build with more parameters, or an automation lane for one since removed, looks like.
int32_t  synthlib_params_index(const tSynthLibParamStore * store, uint32_t id);

// Stores a value, clamped to 0..1. False for an unknown id.
bool     synthlib_params_set(tSynthLibParamStore * store, uint32_t id, double normalized);
double   synthlib_params_get(const tSynthLibParamStore * store, uint32_t id);
double   synthlib_params_get_at(const tSynthLibParamStore * store, uint32_t index);

// Resolves a parameter by id in one lookup rather than a walk of paramInfo(), which for a dynamic list
// formats a title per call. False for an unknown id.
bool     synthlib_params_describe(const tSynthLibParamStore * store, const tSynthLibPluginDesc * desc,
                                  void * inst, uint32_t id, tSynthLibParamDesc * out);

double   synthlib_param_clamp(double normalized);

// ------------------------------------------------------------------------------------------------
// The saved state
// ------------------------------------------------------------------------------------------------

// How many bytes the blob needs, and - when `out` is not NULL and `len` is at least that - writes
// it. Call once with out == NULL to size a buffer, then again to fill it. `params` may be NULL for a
// blob carrying only the plug-in's own bytes.
size_t synthlib_state_write(const tSynthLibPluginDesc * desc, void * inst,
                            const tSynthLibParamStore * params, void * out, size_t len);

// notes §3
bool synthlib_state_read(const tSynthLibParamStore * layout, const void * data, size_t len,
                         tSynthLibParamValue * values, uint32_t capacity, uint32_t * countOut,
                         const void ** pluginData, size_t * pluginLen);

// ------------------------------------------------------------------------------------------------
// Describing a parameter
// ------------------------------------------------------------------------------------------------

// notes §4
bool synthlib_param_text(const tSynthLibPluginDesc * desc, void * inst,
                         const tSynthLibParamDesc * param, double normalized, char * out, size_t len);

// The units string VST3 puts after a number; an Audio Unit uses an enum instead and reads the unit
// field directly. Shared so the two cannot disagree about what a percentage is called.
const char * synthlib_param_units(tSynthLibParamUnit unit);

// HOW MANY PARAMETERS THIS PLUG-IN HAS, from whichever of the two sources it uses.
uint32_t synthlib_param_count(const tSynthLibPluginDesc * desc, void * inst);

// ONE PARAMETER BY INDEX, RESOLVED, whether it came from the static table or from the plug-in's own
// callback. Returns false for an index the plug-in does not have.
bool synthlib_param_describe(const tSynthLibPluginDesc * desc, void * inst, uint32_t index,
                             tSynthLibParamDesc * out);

// 0..1 against the parameter's own displayed range, and back.
double synthlib_param_to_plain(const tSynthLibParamDesc * param, double normalized);
double synthlib_param_to_normalized(const tSynthLibParamDesc * param, double plain);

// ------------------------------------------------------------------------------------------------
// Threads
// ------------------------------------------------------------------------------------------------

// notes §5
void synthlib_run_on_main(void (* fn)(void * ctx), void * ctx);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_STATE_H__
