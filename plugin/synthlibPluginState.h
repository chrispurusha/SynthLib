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

// WHAT BOTH WRAPPERS NEED AND NEITHER FORMAT DEFINES: the parameter store, the saved-state blob, and
// the formatting of a value. Plain C, so the C++ VST3 wrapper and the C Audio Unit wrapper share one
// copy - which is the only way a project saved from one format reads back identically in the other.
//
// THE SAVED STATE
//
// A VST3 stores it as an IBStream and an Audio Unit as CFData inside its ClassInfo dictionary, but
// the BYTES are the same either way - so a project saved in Live and one saved in Logic hold the
// identical thing, and the two wrappers cannot drift apart over what a saved plug-in means.
//
// Layout, as written since 2026-09-11:
//
//     0   4    magic "SLP2"
//     4   4    record count, little-endian
//     8   12*N records: parameter id (u32, little-endian) and normalized value (IEEE-754 double)
//     ..  4    length of the plug-in's own blob
//     ..  n    the plug-in's own blob, exactly as getState() produced it
//
// BY ID, NOT BY POSITION, and only the parameters worth saving. "SLP1", its predecessor, stored every
// parameter's value in index order. That was fine for ten morphs; it is not fine for a plug-in with
// two thousand controller pass-throughs, none of which mean anything a moment later, and it tied a
// saved value to a POSITION - so inserting a parameter anywhere but at the end would have handed
// every later one its neighbour's value. SLP1 blobs are still read.
//
// A PLUG-IN THAT SAVES NO PARAMETERS THROUGH THE WRAPPER GETS ITS OWN BYTES BACK, UNWRAPPED - no
// magic, no count, nothing but what getState() made. GenBridge is the case: every setting is in its
// own blob, keyed by device, and every parameter is NO_SAVE. Wrapping that would change nothing it
// restores and would make a project saved by this build unreadable to an older build of the same
// plug-in, which knows nothing of a header. It reads back through the same path a project from before
// the header does. The one rule this imposes: a plug-in's own blob must never begin with "SLP".
//
// LITTLE-ENDIAN AND NATIVE DOUBLES, deliberately: every machine these run on is little-endian
// (arm64 and x86_64 both), and a universal binary's two halves agree, so a project moves between
// them intact. Should a big-endian target ever appear this needs byte swapping and a bumped magic.
//
// A BLOB THAT DOES NOT START WITH EITHER MAGIC IS NOT AN ERROR. It is a project saved against an
// earlier build, when the state was nothing but the plug-in's own bytes - G2 Alike's was a bare
// patch path - so it is handed to the plug-in whole and the parameters stay at their defaults, or
// at whatever stateParams() says the bytes imply. That is what stops this from emptying a slot in
// somebody's existing project.

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

// EVERY VALUE A WRAPPER HOLDS FOR ONE HALF OF ONE INSTANCE, BY INDEX, FOUND BY ID.
//
// A host walks parameters by index and names them by id, and the two are not the same number - see
// tSynthLibParamDesc. Both wrappers used to keep a plain array indexed by id, which worked while every
// id was its own index and would have written GenBridge's pass-through at id 1000 past the end of a
// twelve-entry array.
//
// Built ONCE, from paramCount()/paramInfo(), which the contract says never change their count or ids
// for the life of a variant. The values are read and written through the accessors below, which are
// atomic: a host sets parameters from its UI thread while the audio thread reads them.
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

// Parses a blob. `values` receives up to `capacity` saved parameters, and `*countOut` says how many;
// an id `layout` does not know is skipped rather than guessed at. `*pluginData` points INTO `data`, so
// it is valid only as long as that buffer is.
//
// `layout` is what an SLP1 blob's positions are resolved against - it stored values, not ids.
//
// Returns false only for a blob that is malformed - one truncated mid-value, or claiming more
// parameters than could possibly fit in it. A short but consistent blob reads back what it has.
bool synthlib_state_read(const tSynthLibParamStore * layout, const void * data, size_t len,
                         tSynthLibParamValue * values, uint32_t capacity, uint32_t * countOut,
                         const void ** pluginData, size_t * pluginLen);

// ------------------------------------------------------------------------------------------------
// Describing a parameter
// ------------------------------------------------------------------------------------------------

// How a value reads, for a host that wants to print it. Asks the plug-in first; falls back to the
// plain value with the parameter's own unit after it, which is right for most parameters.
//
// Always writes something null-terminated when len > 0. Returns false only for a NULL parameter.
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

// RUN `fn` ON THE MAIN THREAD: now, when that is where the caller already is, and otherwise as soon
// as the main thread is next free. Both formats want their host notifications made from there, and a
// plug-in that works out a new latency on a worker thread should not have to know that.
//
// A POSTED CALL OUTLIVES ITS CALLER, so `ctx` must be heap memory `fn` frees, and `fn` must look its
// instance up again rather than trust a pointer that may since have been destroyed.
void synthlib_run_on_main(void (* fn)(void * ctx), void * ctx);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_STATE_H__
