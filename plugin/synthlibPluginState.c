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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <pthread.h>
#endif

#include "synthlibPluginState.h"

#define HEADER_BYTES     (8)        // magic + count
#define RECORD_BYTES     (12)       // SLP2: u32 id + f64 value

// ------------------------------------------------------------------------------------------------
// Describing a parameter
// ------------------------------------------------------------------------------------------------

uint32_t synthlib_param_count(const tSynthLibPluginDesc * desc, void * inst) {
    if (desc == NULL) {
        return 0u;
    }

    // The dynamic form wins when it is there. A descriptor should not fill in both, but if it does,
    // the callback is the one that can answer for THIS instance and the table cannot.
    if (desc->cb.paramCount != NULL) {
        return desc->cb.paramCount(desc, inst);
    }
    return desc->numParams;
}

bool synthlib_param_describe(const tSynthLibPluginDesc * desc, void * inst, uint32_t index,
                             tSynthLibParamDesc * out) {
    if ((desc == NULL) || (out == NULL)) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (desc->cb.paramInfo != NULL) {
        return desc->cb.paramInfo(desc, inst, index, out);
    }

    if ((desc->params == NULL) || (index >= desc->numParams)) {
        return false;
    }
    const tSynthLibParam * p = &desc->params[index];

    out->id                = p->id;
    out->unit              = p->unit;
    out->plainMin          = p->plainMin;
    out->plainMax          = p->plainMax;
    out->defaultNormalized = p->defaultNormalized;
    out->stepCount         = p->stepCount;
    out->midiControl       = p->midiControl;
    out->flags             = p->flags;

    if (p->title != NULL) {
        strncpy(out->title, p->title, sizeof(out->title) - 1u);
    }
    strncpy(out->shortTitle, (p->shortTitle != NULL) ? p->shortTitle : out->title,
            sizeof(out->shortTitle) - 1u);
    return true;
}

double synthlib_param_clamp(double normalized) {
    // NOT `normalized < 0.0 ? ...`: a NaN fails every comparison, and would sail through to an engine
    // that turns it into a stuck voice or a device index nobody can explain.
    if (!(normalized >= 0.0)) {
        return 0.0;
    }
    return (normalized > 1.0) ? 1.0 : normalized;
}

double synthlib_param_to_plain(const tSynthLibParamDesc * param, double normalized) {
    if (param == NULL) {
        return normalized;
    }
    return param->plainMin + (normalized * (param->plainMax - param->plainMin));
}

double synthlib_param_to_normalized(const tSynthLibParamDesc * param, double plain) {
    if ((param == NULL) || (param->plainMax == param->plainMin)) {
        return plain;
    }
    return (plain - param->plainMin) / (param->plainMax - param->plainMin);
}

const char * synthlib_param_units(tSynthLibParamUnit unit) {
    switch (unit) {
        case eSynthLibUnitPercent:      return "%";
        case eSynthLibUnitDecibels:     return "dB";
        case eSynthLibUnitHertz:        return "Hz";
        case eSynthLibUnitSeconds:      return "s";
        case eSynthLibUnitMilliseconds: return "ms";
        case eSynthLibUnitSemitones:    return "semi";
        default:                        return "";
    }
}

bool synthlib_param_text(const tSynthLibPluginDesc * desc, void * inst,
                         const tSynthLibParamDesc * param, double normalized, char * out, size_t len) {
    if ((out == NULL) || (len == 0u)) {
        return false;
    }
    out[0] = '\0';

    if ((desc == NULL) || (param == NULL)) {
        return false;
    }

    if ((desc->cb.paramText != NULL) &&
        (desc->cb.paramText(desc, inst, param->id, normalized, out, len) == true)) {
        return true;
    }
    double plain = synthlib_param_to_plain(param, normalized);

    // A LIST THE PLUG-IN DID NOT NAME reads as its position, which is at least a number a user can
    // match against the drop-down rather than the fraction it happens to be stored as.
    if (((param->flags & SYNTHLIB_PARAM_LIST) != 0u) && (param->stepCount > 0)) {
        snprintf(out, len, "%d", (int)((normalized * (double)param->stepCount) + 0.5));
        return true;
    }

    switch (param->unit) {
        case eSynthLibUnitBoolean:
            snprintf(out, len, "%s", (plain >= 0.5) ? "On" : "Off");
            break;

        case eSynthLibUnitIndexed:
            snprintf(out, len, "%d", (int)(plain + 0.5));
            break;

        case eSynthLibUnitPercent:
            snprintf(out, len, "%.1f %%", plain);
            break;

        case eSynthLibUnitDecibels:
            snprintf(out, len, "%.1f dB", plain);
            break;

        case eSynthLibUnitHertz:
            snprintf(out, len, "%.2f Hz", plain);
            break;

        case eSynthLibUnitSeconds:
            snprintf(out, len, "%.3f s", plain);
            break;

        case eSynthLibUnitMilliseconds:
            snprintf(out, len, "%.1f ms", plain);
            break;

        case eSynthLibUnitSemitones:
            snprintf(out, len, "%+.2f semi", plain);
            break;

        case eSynthLibUnitGeneric:
        default:
            snprintf(out, len, "%.3f", plain);
            break;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
// The parameter store
// ------------------------------------------------------------------------------------------------

static int compare_slots(const void * a, const void * b) {
    uint32_t ia = ((const tSynthLibParamSlot *)a)->id;
    uint32_t ib = ((const tSynthLibParamSlot *)b)->id;

    return (ia < ib) ? -1 : ((ia > ib) ? 1 : 0);
}

// ATOMIC, AND WITHOUT _Atomic. The store is shared with a C++ file, which cannot parse C11's
// qualifier, and a double is lock-free on both architectures these build for - so the builtins,
// which both languages accept, do the same job on a plain double.
static double load_value(const double * where) {
    double value;

    __atomic_load(where, &value, __ATOMIC_RELAXED);
    return value;
}

static void store_value(double * where, double value) {
    __atomic_store(where, &value, __ATOMIC_RELAXED);
}

void synthlib_params_free(tSynthLibParamStore * store) {
    if (store == NULL) {
        return;
    }
    free(store->ids);
    free(store->flags);
    free(store->values);
    free(store->byId);
    memset(store, 0, sizeof(*store));
}

bool synthlib_params_init(tSynthLibParamStore * store, const tSynthLibPluginDesc * desc, void * inst) {
    if (store == NULL) {
        return false;
    }
    memset(store, 0, sizeof(*store));
    store->identity = true;

    uint32_t count = synthlib_param_count(desc, inst);

    if (count == 0u) {
        return true;
    }
    store->ids    = (uint32_t *)calloc(count, sizeof(uint32_t));
    store->flags  = (uint32_t *)calloc(count, sizeof(uint32_t));
    store->values = (double *)calloc(count, sizeof(double));
    store->byId   = (tSynthLibParamSlot *)calloc(count, sizeof(tSynthLibParamSlot));

    if ((store->ids == NULL) || (store->flags == NULL) || (store->values == NULL) ||
        (store->byId == NULL)) {
        synthlib_params_free(store);
        return false;
    }
    store->count = count;

    for (uint32_t i = 0; i < count; i++) {
        tSynthLibParamDesc p;

        if (synthlib_param_describe(desc, inst, i, &p) == true) {
            store->ids[i]    = p.id;
            store->flags[i]  = p.flags;
            store->values[i] = synthlib_param_clamp(p.defaultNormalized);
        } else {
            // AN INDEX THE PLUG-IN WOULD NOT DESCRIBE, which the contract says must not happen. Given
            // an id nothing will ever ask for, and kept out of everything a host sees or saves, so a
            // plug-in that breaks the rule loses that one parameter and nothing else.
            store->ids[i]   = UINT32_MAX - i;
            store->flags[i] = SYNTHLIB_PARAM_HIDDEN | SYNTHLIB_PARAM_NO_SAVE;
        }
        store->byId[i].id    = store->ids[i];
        store->byId[i].index = i;

        if (store->ids[i] != i) {
            store->identity = false;
        }
    }
    qsort(store->byId, count, sizeof(tSynthLibParamSlot), compare_slots);
    return true;
}

int32_t synthlib_params_index(const tSynthLibParamStore * store, uint32_t id) {
    if ((store == NULL) || (store->count == 0u)) {
        return -1;
    }

    // A STATIC TABLE'S IDS ARE ITS INDICES, so for G2 Alike this is all there is.
    if (store->identity == true) {
        return (id < store->count) ? (int32_t)id : -1;
    }
    uint32_t lo = 0;
    uint32_t hi = store->count;

    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) / 2u);

        if (store->byId[mid].id < id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return ((lo < store->count) && (store->byId[lo].id == id)) ? (int32_t)store->byId[lo].index : -1;
}

bool synthlib_params_set(tSynthLibParamStore * store, uint32_t id, double normalized) {
    int32_t index = synthlib_params_index(store, id);

    if (index < 0) {
        return false;
    }
    store_value(&store->values[index], synthlib_param_clamp(normalized));
    return true;
}

double synthlib_params_get(const tSynthLibParamStore * store, uint32_t id) {
    int32_t index = synthlib_params_index(store, id);

    return (index < 0) ? 0.0 : load_value(&store->values[index]);
}

double synthlib_params_get_at(const tSynthLibParamStore * store, uint32_t index) {
    if ((store == NULL) || (index >= store->count)) {
        return 0.0;
    }
    return load_value(&store->values[index]);
}

bool synthlib_params_describe(const tSynthLibParamStore * store, const tSynthLibPluginDesc * desc,
                              void * inst, uint32_t id, tSynthLibParamDesc * out) {
    int32_t index = synthlib_params_index(store, id);

    if (index < 0) {
        return false;
    }
    return synthlib_param_describe(desc, inst, (uint32_t)index, out);
}

// ------------------------------------------------------------------------------------------------
// The saved state
// ------------------------------------------------------------------------------------------------

static void write_u32(uint8_t * out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t read_u32(const uint8_t * in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

// Byte-copied rather than cast: a record is 12 bytes, so every other double in the blob sits on a
// 4-byte boundary and reading one through a double * would be an unaligned access.
static double read_f64(const uint8_t * in) {
    double value;

    memcpy(&value, in, sizeof(value));
    return value;
}

static bool is_magic(const uint8_t * p, char version) {
    return (p[0] == 'S') && (p[1] == 'L') && (p[2] == 'P') && (p[3] == (uint8_t)version);
}

static uint32_t saved_count(const tSynthLibParamStore * params) {
    uint32_t count = 0;

    if (params == NULL) {
        return 0u;
    }

    for (uint32_t i = 0; i < params->count; i++) {
        if ((params->flags[i] & SYNTHLIB_PARAM_NO_SAVE) == 0u) {
            count++;
        }
    }
    return count;
}

size_t synthlib_state_write(const tSynthLibPluginDesc * desc, void * inst,
                            const tSynthLibParamStore * params, void * out, size_t len) {
    if (desc == NULL) {
        return 0;
    }
    uint32_t records = saved_count(params);

    // The plug-in's own blob is asked for its SIZE first and written straight into place after, so
    // it is never copied twice - it can be a whole patch.
    size_t pluginLen = 0;

    if (desc->cb.getState != NULL) {
        pluginLen = desc->cb.getState(inst, NULL, 0);
    }

    // NOTHING OF THE WRAPPER'S TO SAVE, SO NOTHING OF THE WRAPPER'S IS WRITTEN - see the header. The
    // plug-in's own bytes go out exactly as it made them.
    if (records == 0u) {
        if ((out == NULL) || (len < pluginLen)) {
            return pluginLen;
        }
        size_t actual = ((pluginLen > 0u) && (desc->cb.getState != NULL))
                        ? desc->cb.getState(inst, out, pluginLen) : 0u;

        return (actual > pluginLen) ? pluginLen : actual;
    }
    size_t head  = HEADER_BYTES + ((size_t)records * RECORD_BYTES);
    size_t total = head + 4u + pluginLen;

    if ((out == NULL) || (len < total)) {
        return total;
    }
    uint8_t * p = (uint8_t *)out;

    p[0] = 'S';
    p[1] = 'L';
    p[2] = 'P';
    p[3] = '2';
    write_u32(p + 4, records);

    uint8_t * record = p + HEADER_BYTES;

    for (uint32_t i = 0; (params != NULL) && (i < params->count); i++) {
        if ((params->flags[i] & SYNTHLIB_PARAM_NO_SAVE) != 0u) {
            continue;
        }
        double value = load_value(&params->values[i]);

        write_u32(record, params->ids[i]);
        memcpy(record + 4, &value, sizeof(value));
        record += RECORD_BYTES;
    }
    uint8_t * tail = p + head;

    write_u32(tail, (uint32_t)pluginLen);

    if ((pluginLen > 0u) && (desc->cb.getState != NULL)) {
        // ASKED AGAIN RATHER THAN TRUSTED. A plug-in whose state changed between the sizing call
        // and this one would otherwise overrun the buffer; taking the second answer's own length
        // keeps the blob consistent even when it shrank.
        size_t actual = desc->cb.getState(inst, tail + 4, pluginLen);

        if (actual > pluginLen) {
            actual = pluginLen;
        }
        write_u32(tail, (uint32_t)actual);
        total = head + 4u + actual;
    }
    return total;
}

// The plug-in blob's length and bytes, after the parameters. The field is optional: a blob written by
// a build whose plug-in had no state of its own may stop before it.
static bool read_plugin_tail(const uint8_t * p, size_t len, size_t offset,
                             const void ** pluginData, size_t * pluginLen) {
    if ((len - offset) < 4u) {
        return true;
    }
    uint32_t blobLen = read_u32(p + offset);

    offset += 4u;

    if ((size_t)blobLen > (len - offset)) {
        return false;
    }

    if (blobLen > 0u) {
        if (pluginData != NULL) {
            *pluginData = p + offset;
        }

        if (pluginLen != NULL) {
            *pluginLen = blobLen;
        }
    }
    return true;
}

bool synthlib_state_read(const tSynthLibParamStore * layout, const void * data, size_t len,
                         tSynthLibParamValue * values, uint32_t capacity, uint32_t * countOut,
                         const void ** pluginData, size_t * pluginLen) {
    if (countOut != NULL) {
        *countOut = 0u;
    }

    if (pluginData != NULL) {
        *pluginData = NULL;
    }

    if (pluginLen != NULL) {
        *pluginLen = 0u;
    }

    if ((data == NULL) || (len == 0u)) {
        return false;
    }
    const uint8_t * p     = (const uint8_t *)data;
    uint32_t        taken = 0;

    // NOT OURS, SO IT IS THE PLUG-IN'S - see the note in the header. This is the compatibility path
    // for a project saved before the blob had a header at all, and it must stay.
    if ((len < HEADER_BYTES) || ((is_magic(p, '1') == false) && (is_magic(p, '2') == false))) {
        if (pluginData != NULL) {
            *pluginData = data;
        }

        if (pluginLen != NULL) {
            *pluginLen = len;
        }
        return true;
    }
    uint32_t count  = read_u32(p + 4);
    size_t   stride = is_magic(p, '1') ? sizeof(double) : (size_t)RECORD_BYTES;

    // A count that cannot fit in what is here is a corrupt blob, not a short one.
    if ((size_t)count > ((len - HEADER_BYTES) / stride)) {
        return false;
    }
    const uint8_t * at = p + HEADER_BYTES;

    for (uint32_t i = 0; i < count; i++, at += stride) {
        uint32_t id;
        double   value;

        if (is_magic(p, '1') == true) {
            // SLP1 STORED POSITIONS, so the position is resolved against the layout this build has.
            // For every plug-in that ever wrote SLP1 - G2 Alike alone - position and id are the same.
            if ((layout == NULL) || (i >= layout->count)) {
                break;
            }
            id    = layout->ids[i];
            value = read_f64(at);
        } else {
            id    = read_u32(at);
            value = read_f64(at + 4);

            // AN ID THIS BUILD DOES NOT HAVE is a parameter since removed, and is skipped rather than
            // handed to a plug-in that would have to wonder what it was.
            if ((layout != NULL) && (synthlib_params_index(layout, id) < 0)) {
                continue;
            }
        }

        if ((values != NULL) && (taken < capacity)) {
            values[taken].id    = id;
            values[taken].value = synthlib_param_clamp(value);
            taken++;
        }
    }

    if (countOut != NULL) {
        *countOut = taken;
    }
    return read_plugin_tail(p, len, HEADER_BYTES + ((size_t)count * stride), pluginData, pluginLen);
}

// ------------------------------------------------------------------------------------------------
// Threads
// ------------------------------------------------------------------------------------------------

void synthlib_run_on_main(void (* fn)(void * ctx), void * ctx) {
    if (fn == NULL) {
        return;
    }
#if defined(__APPLE__)
    if (pthread_main_np() != 0) {
        fn(ctx);
        return;
    }
    dispatch_async_f(dispatch_get_main_queue(), ctx, fn);
#else
    // NO MAIN-THREAD QUEUE YET on the ports to come. Called in place, which is what every caller did
    // before this existed; a Windows or Linux build will need its own answer here.
    fn(ctx);
#endif
}
