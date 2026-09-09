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
#include <string.h>

#include "synthlibPluginState.h"

#define STATE_MAGIC_0    'S'
#define STATE_MAGIC_1    'L'
#define STATE_MAGIC_2    'P'
#define STATE_MAGIC_3    '1'

#define HEADER_BYTES     (8)        // magic + parameter count

uint32_t synthlib_param_count(const tSynthLibPluginDesc * desc, void * inst) {
    if (desc == NULL) {
        return 0u;
    }

    // The dynamic form wins when it is there. A descriptor should not fill in both, but if it does,
    // the callback is the one that can answer for THIS instance and the table cannot.
    if (desc->cb.paramCount != NULL) {
        return desc->cb.paramCount(inst);
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
        return desc->cb.paramInfo(inst, index, out);
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

    if (p->title != NULL) {
        strncpy(out->title, p->title, sizeof(out->title) - 1u);
    }
    strncpy(out->shortTitle, (p->shortTitle != NULL) ? p->shortTitle : out->title,
            sizeof(out->shortTitle) - 1u);
    return true;
}

bool synthlib_param_by_id(const tSynthLibPluginDesc * desc, void * inst, uint32_t id,
                          tSynthLibParamDesc * out) {
    uint32_t count = synthlib_param_count(desc, inst);

    // THE COMMON CASE FIRST, AND IT IS NOT AN ASSUMPTION. For a static table the id IS the index, so
    // this hits immediately; the walk below exists for a dynamic list whose ids are its own, and for
    // a project saved against a build with more parameters than this one has - which is where an id
    // that indexes nothing comes from.
    if ((id < count) && (synthlib_param_describe(desc, inst, id, out) == true) && (out->id == id)) {
        return true;
    }

    for (uint32_t i = 0; i < count; i++) {
        if ((synthlib_param_describe(desc, inst, i, out) == true) && (out->id == id)) {
            return true;
        }
    }
    return false;
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

static void write_u32(uint8_t * out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t read_u32(const uint8_t * in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

size_t synthlib_state_write(const tSynthLibPluginDesc * desc, void * inst,
                            const double * params, void * out, size_t len) {
    if (desc == NULL) {
        return 0;
    }
    uint32_t count = (params != NULL) ? synthlib_param_count(desc, inst) : 0u;

    // The plug-in's own blob is asked for its SIZE first and written straight into place after, so
    // it is never copied twice - it can be a whole patch.
    size_t pluginLen = 0;

    if (desc->cb.getState != NULL) {
        pluginLen = desc->cb.getState(inst, NULL, 0);
    }
    size_t total = HEADER_BYTES + ((size_t)count * sizeof(double)) + 4u + pluginLen;

    if ((out == NULL) || (len < total)) {
        return total;
    }
    uint8_t * p = (uint8_t *)out;

    p[0] = STATE_MAGIC_0;
    p[1] = STATE_MAGIC_1;
    p[2] = STATE_MAGIC_2;
    p[3] = STATE_MAGIC_3;
    write_u32(p + 4, count);

    if (count > 0u) {
        memcpy(p + HEADER_BYTES, params, (size_t)count * sizeof(double));
    }
    uint8_t * tail = p + HEADER_BYTES + ((size_t)count * sizeof(double));

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
        total = HEADER_BYTES + ((size_t)count * sizeof(double)) + 4u + actual;
    }
    return total;
}

bool synthlib_state_read(const tSynthLibPluginDesc * desc, const void * data, size_t len,
                         double * paramsOut, uint32_t * paramCountOut,
                         const void ** pluginData, size_t * pluginLen) {
    if (paramCountOut != NULL) {
        *paramCountOut = 0u;
    }

    if (pluginData != NULL) {
        *pluginData = NULL;
    }

    if (pluginLen != NULL) {
        *pluginLen = 0u;
    }

    if ((desc == NULL) || (data == NULL) || (len == 0u)) {
        return false;
    }
    const uint8_t * p = (const uint8_t *)data;

    // NOT OURS, SO IT IS THE PLUG-IN'S - see the note in the header. This is the compatibility path
    // for a project saved before the blob had a header at all, and it must stay.
    if ((len < HEADER_BYTES) ||
        (p[0] != STATE_MAGIC_0) || (p[1] != STATE_MAGIC_1) ||
        (p[2] != STATE_MAGIC_2) || (p[3] != STATE_MAGIC_3)) {
        if (pluginData != NULL) {
            *pluginData = data;
        }

        if (pluginLen != NULL) {
            *pluginLen = len;
        }
        return true;
    }
    uint32_t count = read_u32(p + 4);

    // A count that cannot fit in what is here is a corrupt blob, not a short one.
    if ((size_t)count > ((len - HEADER_BYTES) / sizeof(double))) {
        return false;
    }
    uint32_t have = synthlib_param_count(desc, NULL);
    uint32_t take = (count < have) ? count : have;

    if ((paramsOut != NULL) && (take > 0u)) {
        memcpy(paramsOut, p + HEADER_BYTES, (size_t)take * sizeof(double));
    }

    if (paramCountOut != NULL) {
        *paramCountOut = take;
    }
    size_t offset = HEADER_BYTES + ((size_t)count * sizeof(double));

    // The plug-in blob's length field is optional: a blob written by a build whose plug-in had no
    // state of its own stops here.
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

bool synthlib_param_text(const tSynthLibPluginDesc * desc, void * inst, uint32_t id,
                         double normalized, char * out, size_t len) {
    tSynthLibParamDesc param;

    if ((out == NULL) || (len == 0u)) {
        return false;
    }
    out[0] = '\0';

    if (synthlib_param_by_id(desc, inst, id, &param) == false) {
        return false;
    }

    if ((desc->cb.paramText != NULL) && (desc->cb.paramText(inst, id, normalized, out, len) == true)) {
        return true;
    }
    double plain = synthlib_param_to_plain(&param, normalized);

    switch (param.unit) {
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
