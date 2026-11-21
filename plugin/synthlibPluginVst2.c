/*
 * SynthLib - the VST 2.4 binary interface, for the Akai MPC and Force (their only plug-in format).
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
// Notes: Docs/code-notes/synthlibPluginVst2.c.md - "// notes §k" refers there.

// notes §1: compiles to nothing without the MPC SDK's description of the interface on the include path
#if defined(__has_include)
#if __has_include("mpc_vst2_abi.h")
#define SYNTHLIB_HAVE_VST2_ABI    1
#endif
#endif

#ifdef SYNTHLIB_HAVE_VST2_ABI

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "mpc_vst2_abi.h"
#include "synthlibPlugin.h"
#include "synthlibPluginState.h"

#define MAX_EVENTS     (512u)
#define MAX_CHANNELS   (16u)
#define STRING_MAX     (32u)    // notes §2: what we write into a host's name and value buffers, with the NUL

typedef struct {
    uint8_t  status, data1, data2;
    uint32_t offset;
} tEvent;

typedef struct {
    struct AEffect              effect;     // first, so an AEffect * is one of these
    const tSynthLibPluginDesc * desc;
    void *                      inst;
    audioMasterCallback         master;
    tSynthLibParamStore         params;

    // notes §3: values set from any thread, applied on the audio thread at the next block
    uint32_t *                  shown;      // notes §8: the host's index -> the store's, hidden parameters left out
    uint32_t                    shownCount;
    int32_t                     programIndex;   // notes §9: the store index of the SYNTHLIB_PARAM_PROGRAM parameter, or -1
    uint32_t                    programs;
    _Atomic uint64_t *          pending;
    atomic_bool *               dirty;
    atomic_bool                 anyDirty;

    tEvent                      events[MAX_EVENTS];
    uint32_t                    eventCount;

    double                      sampleRate;
    uint32_t                    maxFrames;
    bool                        active;
    uint32_t                    numIn, numOut;
    float *                     inCopy;     // notes §4: the host processes in place
    uint8_t *                   chunk;
    size_t                      chunkLen;
} tVst2;

static tVst2 * of(struct AEffect * e) {
    return (e != NULL) ? (tVst2 *)e->object : NULL;
}

static void copy_string(char * dst, size_t size, const char * src) {
    size_t n = 0;

    if ((dst == NULL) || (size == 0)) {
        return;
    }

    while ((src != NULL) && (src[n] != '\0') && ((n + 1) < size)) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

static uint32_t bus_channels(const tSynthLibBus * buses, uint32_t count) {
    uint32_t n = 0;

    for (uint32_t i = 0; i < count; i++) {
        n += buses[i].isAux ? 0u : buses[i].channels;
    }
    return (n > MAX_CHANNELS) ? MAX_CHANNELS : n;
}

// ---- parameters ------------------------------------------------------------------------------------------

static uint64_t bits_of(double v) {
    uint64_t b;

    memcpy(&b, &v, sizeof(b));
    return b;
}

static double double_of(uint64_t b) {
    double v;

    memcpy(&v, &b, sizeof(v));
    return v;
}

static void apply_now(tVst2 * v, uint32_t id, double normalized) {
    if (synthlib_params_set(&v->params, id, normalized) && (v->desc->cb.setParam != NULL)) {
        v->desc->cb.setParam(v->inst, id, synthlib_param_clamp(normalized));
    }
}

static void queue_param(tVst2 * v, uint32_t hostIndex, double normalized) {
    if (hostIndex >= v->shownCount) {
        return;
    }
    uint32_t index = v->shown[hostIndex];

    normalized = synthlib_param_clamp(normalized);
    (void)synthlib_params_set(&v->params, v->params.ids[index], normalized);   // what the host reads back at once
    atomic_store(&v->pending[index], bits_of(normalized));
    atomic_store(&v->dirty[index], true);
    atomic_store(&v->anyDirty, true);
}

static void apply_pending(tVst2 * v) {
    if (!atomic_exchange(&v->anyDirty, false)) {
        return;
    }

    for (uint32_t i = 0; i < v->params.count; i++) {
        if (atomic_exchange(&v->dirty[i], false) && (v->desc->cb.setParam != NULL)) {
            v->desc->cb.setParam(v->inst, v->params.ids[i], double_of(atomic_load(&v->pending[i])));
        }
    }
}

static void set_parameter(struct AEffect * e, VstInt32 index, float value) {
    tVst2 * v = of(e);

    if ((v != NULL) && (index >= 0)) {
        queue_param(v, (uint32_t)index, value);
    }
}

static float get_parameter(struct AEffect * e, VstInt32 index) {
    tVst2 * v = of(e);

    return ((v != NULL) && (index >= 0) && ((uint32_t)index < v->shownCount)) ? (float)synthlib_params_get_at(&v->params, v->shown[index]) : 0.0f;
}

static bool describe(tVst2 * v, VstInt32 index, tSynthLibParamDesc * p) {
    return (index >= 0) && ((uint32_t)index < v->shownCount)
           && synthlib_params_describe(&v->params, v->desc, v->inst, v->params.ids[v->shown[index]], p);
}

// ---- programs - notes §9 ---------------------------------------------------------------------------------

static void program_name(tVst2 * v, int32_t program, char * out, size_t len) {
    tSynthLibParamDesc p;
    char               text[128] = "";

    if ((v->programIndex < 0) || (program < 0) || ((uint32_t)program >= v->programs)
        || !synthlib_params_describe(&v->params, v->desc, v->inst, v->params.ids[v->programIndex], &p)) {
        copy_string(out, len, (program == 0) ? "Default" : "");
        return;
    }
    (void)synthlib_param_text(v->desc, v->inst, &p, (double)program / (double)(v->programs - 1u), text, sizeof(text));
    copy_string(out, len, text);
}

static int32_t current_program(tVst2 * v) {
    if (v->programIndex < 0) {
        return 0;
    }
    return (int32_t)((synthlib_params_get_at(&v->params, (uint32_t)v->programIndex) * (double)(v->programs - 1u)) + 0.5);
}

static void select_program(tVst2 * v, int32_t program) {
    if ((v->programIndex < 0) || (program < 0) || ((uint32_t)program >= v->programs)) {
        return;
    }
    double normalized = (double)program / (double)(v->programs - 1u);

    (void)synthlib_params_set(&v->params, v->params.ids[v->programIndex], normalized);
    atomic_store(&v->pending[v->programIndex], bits_of(normalized));
    atomic_store(&v->dirty[v->programIndex], true);
    atomic_store(&v->anyDirty, true);
}

// ---- MIDI ------------------------------------------------------------------------------------------------

static bool map_control(tVst2 * v, uint8_t channel, int16_t control, uint32_t * id) {
    const tSynthLibPluginDesc * d = v->desc;

    if (d->cb.midiMapping != NULL) {
        return d->cb.midiMapping(d, v->inst, channel, control, id) && (synthlib_params_index(&v->params, *id) >= 0);
    }

    for (uint32_t i = 0; i < v->params.count; i++) {
        tSynthLibParamDesc p;

        if (synthlib_param_describe(d, v->inst, i, &p) && (p.midiControl == control)) {
            *id = p.id;
            return true;
        }
    }
    return false;
}

static void control_to(tVst2 * v, uint8_t channel, int16_t control, double value, uint32_t offset) {
    uint32_t            id;
    tSynthLibParamPoint point = {offset, value};

    if (!map_control(v, channel, control, &id) || !synthlib_params_set(&v->params, id, value)) {
        return;
    }

    if (v->desc->cb.paramPoints != NULL) {
        v->desc->cb.paramPoints(v->inst, id, &point, 1u);
    } else if (v->desc->cb.setParam != NULL) {
        v->desc->cb.setParam(v->inst, id, value);
    }
}

// As synthlibPluginAu.c's deliver_midi
static void deliver(tVst2 * v, const tEvent * ev) {
    const tSynthLibPluginDesc * d       = v->desc;
    uint8_t                     channel = ev->status & 0x0Fu;

    switch (ev->status & 0xF0u) {
        case 0x90:
            if ((ev->data2 > 0u) && (d->cb.noteOn != NULL)) {
                d->cb.noteOn(v->inst, channel, ev->data1, (float)ev->data2 / 127.0f, ev->offset);
            } else if (d->cb.noteOff != NULL) {
                d->cb.noteOff(v->inst, channel, ev->data1, 0.0f, ev->offset);
            }
            break;

        case 0x80:
            if (d->cb.noteOff != NULL) {
                d->cb.noteOff(v->inst, channel, ev->data1, (float)ev->data2 / 127.0f, ev->offset);
            }
            break;

        case 0xA0:
            if (d->cb.polyPressure != NULL) {
                d->cb.polyPressure(v->inst, channel, ev->data1, (float)ev->data2 / 127.0f, ev->offset);
            }
            break;

        case 0xB0:
            control_to(v, channel, (int16_t)ev->data1, (double)ev->data2 / 127.0, ev->offset);

            if (((ev->data1 == 120u) || (ev->data1 == 123u)) && (d->cb.reset != NULL)) {
                d->cb.reset(v->inst);
            }
            break;

        case 0xC0:
            for (uint32_t i = 0; i < v->params.count; i++) {
                tSynthLibParamDesc p;

                if (synthlib_param_describe(d, v->inst, i, &p) && ((p.flags & SYNTHLIB_PARAM_PROGRAM) != 0u)) {
                    apply_now(v, p.id, (double)ev->data1 / 127.0);
                    break;
                }
            }
            break;

        case 0xD0:
            control_to(v, channel, SYNTHLIB_MIDI_AFTERTOUCH, (double)ev->data1 / 127.0, ev->offset);
            break;

        case 0xE0:
            control_to(v, channel, SYNTHLIB_MIDI_PITCH_BEND, (double)((uint32_t)ev->data1 | ((uint32_t)ev->data2 << 7)) / 16383.0, ev->offset);
            break;

        default:
            break;
    }
}

static void take_events(tVst2 * v, const struct VstEvents * list) {
    if (list == NULL) {
        return;
    }

    for (VstInt32 i = 0; (i < list->numEvents) && (v->eventCount < MAX_EVENTS); i++) {
        const struct VstEvent * ev = list->events[i];

        if ((ev == NULL) || (ev->type != kVstMidiType)) {
            continue;
        }
        const struct VstMidiEvent * m = (const struct VstMidiEvent *)ev;
        tEvent *                    t = &v->events[v->eventCount++];

        t->status = (uint8_t)m->midiData[0];
        t->data1  = (uint8_t)m->midiData[1] & 0x7Fu;
        t->data2  = (uint8_t)m->midiData[2] & 0x7Fu;
        t->offset = (m->deltaFrames > 0) ? (uint32_t)m->deltaFrames : 0u;
    }
}

// ---- transport -------------------------------------------------------------------------------------------

static void transport_of(tVst2 * v, tSynthLibTransport * t) {
    memset(t, 0, sizeof(*t));
    t->sampleRate = v->sampleRate;

    if ((v->master == NULL) || !v->desc->wantsTransport) {
        return;
    }
    const struct VstTimeInfo * ti = (const struct VstTimeInfo *)v->master(&v->effect, audioMasterGetTime, 0,
                                                                           kVstPpqPosValid | kVstTempoValid | kVstBarsValid | kVstCyclePosValid | kVstTimeSigValid, NULL, 0.0f);

    if (ti == NULL) {
        return;
    }
    t->valid              = true;
    t->playing            = (ti->flags & kVstTransportPlaying) != 0;
    t->recording          = (ti->flags & kVstTransportRecording) != 0;
    t->cycleActive        = (ti->flags & kVstTransportCycleActive) != 0;
    t->tempoValid         = (ti->flags & kVstTempoValid) != 0;
    t->tempo              = ti->tempo;
    t->musicTimeValid     = (ti->flags & kVstPpqPosValid) != 0;
    t->projectTimeMusic   = ti->ppqPos;
    t->barPositionValid   = (ti->flags & kVstBarsValid) != 0;
    t->barPositionMusic   = ti->barStartPos;
    t->cycleValid         = (ti->flags & kVstCyclePosValid) != 0;
    t->cycleStartMusic    = ti->cycleStartPos;
    t->cycleEndMusic      = ti->cycleEndPos;
    t->timeSigValid       = (ti->flags & kVstTimeSigValid) != 0;
    t->timeSigNumerator   = ti->timeSigNumerator;
    t->timeSigDenominator = ti->timeSigDenominator;
    t->projectTimeSamples = (int64_t)ti->samplePos;
}

// ---- audio -----------------------------------------------------------------------------------------------

// notes §5: denormals flushed for the block, as the MPC's own build does on its 32-bit products only
static uintptr_t flush_denormals(void) {
    uintptr_t old = 0;
#if defined(__aarch64__)
    __asm__ volatile ("mrs %0, fpcr" : "=r" (old));
    __asm__ volatile ("msr fpcr, %0" : : "r" (old | (1u << 24)));
#elif defined(__arm__) && defined(__ARM_FP)
    __asm__ volatile ("vmrs %0, fpscr" : "=r" (old));
    __asm__ volatile ("vmsr fpscr, %0" : : "r" (old | (1u << 24)));
#endif
    return old;
}

static void restore_fp(uintptr_t old) {
#if defined(__aarch64__)
    __asm__ volatile ("msr fpcr, %0" : : "r" (old));
#elif defined(__arm__) && defined(__ARM_FP)
    __asm__ volatile ("vmsr fpscr, %0" : : "r" (old));
#else
    (void)old;
#endif
}

static void process_replacing(struct AEffect * e, float ** inputs, float ** outputs, VstInt32 frames) {
    tVst2 * v = of(e);

    if ((v == NULL) || (frames <= 0)) {
        return;
    }
    uintptr_t                   fp   = flush_denormals();
    const tSynthLibPluginDesc * d    = v->desc;
    uint32_t                    n    = (uint32_t)frames;
    const float *               in[MAX_CHANNELS];
    tSynthLibTransport          transport;

    apply_pending(v);

    for (uint32_t i = 0; i < v->numIn; i++) {   // notes §4
        float * copy = v->inCopy + ((size_t)i * v->maxFrames);

        if ((inputs != NULL) && (inputs[i] != NULL) && (n <= v->maxFrames)) {
            memcpy(copy, inputs[i], n * sizeof(float));
        } else {
            memset(copy, 0, (size_t)v->maxFrames * sizeof(float));
        }
        in[i] = copy;
    }
    transport_of(v, &transport);

    if (d->cb.blockBegin != NULL) {
        d->cb.blockBegin(v->inst, n, &transport);
    }

    for (uint32_t i = 0; i < v->eventCount; i++) {
        if (v->events[i].offset >= n) {
            v->events[i].offset = n - 1u;
        }
        deliver(v, &v->events[i]);
    }
    v->eventCount = 0;

    if (d->cb.process != NULL) {
        d->cb.process(v->inst, in, v->numIn, outputs, v->numOut, n, &transport);
    } else {
        for (uint32_t c = 0; c < v->numOut; c++) {
            memset(outputs[c], 0, n * sizeof(float));
        }
    }
    restore_fp(fp);
}

// ---- lifecycle -------------------------------------------------------------------------------------------

static void resume(tVst2 * v) {
    const tSynthLibPluginDesc * d = v->desc;
    tSynthLibSetup              setup = {v->sampleRate, v->maxFrames, false};

    free(v->inCopy);
    v->inCopy = (v->numIn > 0u) ? (float *)calloc((size_t)v->numIn * v->maxFrames, sizeof(float)) : NULL;

    if (d->cb.setSampleRate != NULL) {
        d->cb.setSampleRate(v->inst, v->sampleRate);
    }

    if (d->cb.prepare != NULL) {
        d->cb.prepare(v->inst, &setup);
    }

    if (d->cb.setActive != NULL) {
        d->cb.setActive(v->inst, true);
    }

    if (d->cb.setProcessing != NULL) {
        d->cb.setProcessing(v->inst, true);
    } else if (d->cb.reset != NULL) {
        d->cb.reset(v->inst);
    }
    v->effect.initialDelay = (d->cb.latencySamples != NULL) ? (VstInt32)d->cb.latencySamples(v->inst) : 0;
    v->active              = true;
}

static void suspend(tVst2 * v) {
    const tSynthLibPluginDesc * d = v->desc;

    if (!v->active) {
        return;
    }
    v->active = false;

    if (d->cb.setProcessing != NULL) {
        d->cb.setProcessing(v->inst, false);
    }

    if (d->cb.setActive != NULL) {
        d->cb.setActive(v->inst, false);
    }
}

static void forget(tVst2 * v);

static void destroy(tVst2 * v) {
    const tSynthLibPluginDesc * d = v->desc;

    forget(v);
    suspend(v);

    if (v->inst != NULL) {
        if (d->cb.terminate != NULL) {
            d->cb.terminate(v->inst);
        }

        if (d->cb.destroy != NULL) {
            d->cb.destroy(v->inst);
        }
    }
    synthlib_params_free(&v->params);
    free(v->pending);
    free(v->dirty);
    free(v->shown);
    free(v->inCopy);
    free(v->chunk);
    free(v);
}

// ---- state -----------------------------------------------------------------------------------------------

// The same bytes the VST3 and Audio Unit wrappers write
static VstIntPtr get_chunk(tVst2 * v, void ** out) {
    apply_pending(v);   // notes §3: the plug-in's own bytes must include what is still waiting for a block

    size_t need = synthlib_state_write(v->desc, v->inst, &v->params, NULL, 0);

    if ((out == NULL) || (need == 0)) {
        return 0;
    }

    if (need > v->chunkLen) {
        uint8_t * grown = (uint8_t *)realloc(v->chunk, need);

        if (grown == NULL) {
            return 0;
        }
        v->chunk    = grown;
        v->chunkLen = need;
    }
    *out = v->chunk;
    return (VstIntPtr)synthlib_state_write(v->desc, v->inst, &v->params, v->chunk, need);
}

// As synthlibPluginAu.c's apply_class_info
static VstIntPtr set_chunk(tVst2 * v, const void * data, size_t len) {
    const tSynthLibPluginDesc * d          = v->desc;
    uint32_t                    capacity   = (v->params.count > 0u) ? v->params.count : 1u;
    tSynthLibParamValue *       values     = (tSynthLibParamValue *)calloc(capacity, sizeof(tSynthLibParamValue));
    uint32_t                    got        = 0;
    const void *                pluginData = NULL;
    size_t                      pluginLen  = 0;

    if ((values == NULL) || (data == NULL)) {
        free(values);
        return 0;
    }

    if (synthlib_state_read(&v->params, data, len, values, v->params.count, &got, &pluginData, &pluginLen)) {
        atomic_store(&v->anyDirty, false);   // notes §3: nothing set before the load may land on top of it

        for (uint32_t i = 0; i < v->params.count; i++) {
            atomic_store(&v->dirty[i], false);
        }

        for (uint32_t i = 0; i < got; i++) {
            apply_now(v, values[i].id, values[i].value);
        }

        if (d->cb.setState != NULL) {
            d->cb.setState(v->inst, pluginData, pluginLen);
        }

        for (uint32_t i = 0, n = synthlib_param_count(d, v->inst); i < n; i++) {
            tSynthLibParamDesc p;

            if (synthlib_param_describe(d, v->inst, i, &p) && synthlib_param_resets_on_load(&p)) {
                apply_now(v, p.id, p.defaultNormalized);
            }
        }

        if (d->cb.getParam != NULL) {
            for (uint32_t i = 0; i < v->params.count; i++) {
                (void)synthlib_params_set(&v->params, v->params.ids[i], d->cb.getParam(v->inst, v->params.ids[i]));
            }
        }
    }
    free(values);
    return 1;
}

// ---- the dispatcher --------------------------------------------------------------------------------------

static VstIntPtr dispatcher(struct AEffect * e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void * ptr, float opt) {
    tVst2 *                     v   = of(e);

    if (v == NULL) {
        return 0;
    }
    const tSynthLibPluginDesc * d   = v->desc;
    char *                      str = (char *)ptr;
    tSynthLibParamDesc          p;

    switch (opcode) {
        case effOpen:
            return 0;

        case effClose:
            destroy(v);
            return 0;

        case effSetProgram:
            select_program(v, (int32_t)value);
            return 0;

        case effGetProgram:
            return current_program(v);

        case effGetProgramName:
            program_name(v, current_program(v), str, kVstMaxProgNameLen + 1);
            return 0;

        case effGetProgramNameIndexed:
            program_name(v, index, str, kVstMaxProgNameLen + 1);
            return ((index >= 0) && ((uint32_t)index < v->programs)) ? 1 : 0;

        case effGetParamLabel:
            copy_string(str, STRING_MAX, describe(v, index, &p) ? synthlib_param_units(p.unit) : "");
            return 0;

        case effGetParamDisplay:
            if (describe(v, index, &p)) {
                char text[128] = "";

                (void)synthlib_param_text(d, v->inst, &p, synthlib_params_get(&v->params, p.id), text, sizeof(text));
                copy_string(str, STRING_MAX, text);
            }
            return 0;

        case effGetParamName:
            copy_string(str, STRING_MAX, describe(v, index, &p) ? p.title : "");
            return 0;

        case effCanBeAutomated:
            return describe(v, index, &p) ? 1 : 0;

        case effSetSampleRate:
            v->sampleRate = (opt > 0.0f) ? (double)opt : 44100.0;
            return 0;

        case effSetBlockSize:
            v->maxFrames = (value > 0) ? (uint32_t)value : 512u;
            return 0;

        case effMainsChanged:
            if (value != 0) {
                resume(v);
            } else {
                suspend(v);
            }
            return 0;

        case effGetChunk:
            return get_chunk(v, (void **)ptr);

        case effSetChunk:
            return set_chunk(v, ptr, (size_t)value);

        case effProcessEvents:
            take_events(v, (const struct VstEvents *)ptr);
            return 1;

        case effGetInputProperties:
        case effGetOutputProperties: {
            uint32_t                 count = (opcode == effGetInputProperties) ? v->numIn : v->numOut;
            struct VstPinProperties * pin  = (struct VstPinProperties *)ptr;

            if ((pin == NULL) || (index < 0) || ((uint32_t)index >= count)) {
                return 0;
            }
            memset(pin, 0, sizeof(*pin));
            copy_string(pin->label, sizeof(pin->label), (opcode == effGetInputProperties) ? "In" : "Out");
            pin->flags = kVstPinIsActive | ((((index & 1) == 0) && ((uint32_t)(index + 1) < count)) ? kVstPinIsStereo : 0);
            return 1;
        }

        case effGetPlugCategory:
            return d->isInstrument ? kPlugCategSynth : kPlugCategEffect;

        case effGetEffectName:
        case effGetProductString:
            copy_string(str, kVstMaxEffectNameLen + 1, d->name);
            return 1;

        case effGetVendorString:
            copy_string(str, kVstMaxVendorStrLen + 1, d->vendor);
            return 1;

        case effGetVendorVersion:
            return (VstIntPtr)d->auVersion;

        case effCanDo:
            if (str == NULL) {
                return 0;
            }

            if ((strcmp(str, "receiveVstEvents") == 0) || (strcmp(str, "receiveVstMidiEvent") == 0)) {
                return (d->isInstrument || d->wantsMidiIn) ? 1 : -1;
            }

            if ((strcmp(str, "sendVstEvents") == 0) || (strcmp(str, "sendVstMidiEvent") == 0) || (strcmp(str, "bypass") == 0)
                || (strcmp(str, "offline") == 0)) {
                return -1;
            }
            return (strcmp(str, "receiveVstTimeInfo") == 0) ? 1 : 0;

        case effGetTailSize:
            return 0;

        case effGetVstVersion:
            return kVstVersion;

        case effSetProcessPrecision:
            return (value == kVstProcessPrecision32) ? 1 : 0;

        case effGetNumMidiInputChannels:
            return (d->isInstrument || d->wantsMidiIn) ? 16 : 0;

        default:
            return 0;
    }
}

// ---- what the plug-in may ask of us ----------------------------------------------------------------------

static void vst2_param_edited(void * inst, uint32_t id, double normalized);
static double vst2_param_value(void * inst, uint32_t id);
static void vst2_latency_changed(void * inst);
static bool vst2_request_resize(void * inst, double width, double height);
static bool vst2_send_message(void * inst, const char * id, int64_t value);
static bool vst2_midi_out(void * inst, uint8_t status, uint8_t data1, uint8_t data2, uint32_t sampleOffset);

static const tSynthLibPluginFormat gVst2Format = {
    .paramEdited    = vst2_param_edited,
    .paramValue     = vst2_param_value,
    .latencyChanged = vst2_latency_changed,
    .requestResize  = vst2_request_resize,
    .sendMessage    = vst2_send_message,
    .midiOut        = vst2_midi_out
};

// notes §6: the plug-in names itself by its instance, so the live wrappers are kept in a table
#define MAX_INSTANCES  (64u)
static tVst2 * _Atomic gInstances[MAX_INSTANCES];

static tVst2 * vst2_for(void * inst) {
    for (uint32_t i = 0; i < MAX_INSTANCES; i++) {
        tVst2 * v = atomic_load(&gInstances[i]);

        if ((v != NULL) && (v->inst == inst)) {
            return v;
        }
    }
    return NULL;
}

static void forget(tVst2 * v) {
    for (uint32_t i = 0; i < MAX_INSTANCES; i++) {
        tVst2 * mine = v;

        (void)atomic_compare_exchange_strong(&gInstances[i], &mine, NULL);
    }
}

static void vst2_param_edited(void * inst, uint32_t id, double normalized) {
    tVst2 * v     = vst2_for(inst);
    int32_t index = (v != NULL) ? synthlib_params_index(&v->params, id) : -1;

    if (index < 0) {
        return;
    }
    (void)synthlib_params_set(&v->params, id, normalized);

    for (uint32_t i = 0; (i < v->shownCount) && (v->master != NULL); i++) {
        if (v->shown[i] == (uint32_t)index) {
            v->master(&v->effect, audioMasterAutomate, (VstInt32)i, 0, NULL, (float)normalized);
            break;
        }
    }
}

static double vst2_param_value(void * inst, uint32_t id) {
    tVst2 * v = vst2_for(inst);

    return (v != NULL) ? synthlib_params_get(&v->params, id) : 0.0;
}

static void vst2_latency_changed(void * inst) {
    tVst2 * v = vst2_for(inst);

    if ((v != NULL) && (v->desc->cb.latencySamples != NULL)) {
        v->effect.initialDelay = (VstInt32)v->desc->cb.latencySamples(v->inst);

        if (v->master != NULL) {
            v->master(&v->effect, audioMasterIOChanged, 0, 0, NULL, 0.0f);
        }
    }
}

static bool vst2_request_resize(void * inst, double width, double height) {
    (void)inst;
    (void)width;
    (void)height;
    return false;   // no editor
}

static bool vst2_send_message(void * inst, const char * id, int64_t value) {
    tVst2 * v = vst2_for(inst);

    if ((v == NULL) || (v->desc->cb.message == NULL)) {
        return false;
    }
    v->desc->cb.message(v->inst, id, value);   // one object: the other half is ourselves
    return true;
}

static bool vst2_midi_out(void * inst, uint8_t status, uint8_t data1, uint8_t data2, uint32_t sampleOffset) {
    (void)inst;
    (void)status;
    (void)data1;
    (void)data2;
    (void)sampleOffset;
    return false;   // the MPC discards a plug-in's MIDI out
}

// ---- the entry point -------------------------------------------------------------------------------------

__attribute__((visibility("default"))) struct AEffect * VSTPluginMain(audioMasterCallback master);

struct AEffect * VSTPluginMain(audioMasterCallback master) {
    if ((master != NULL) && (master(NULL, audioMasterVersion, 0, 0, NULL, 0.0f) == 0)) {
        return NULL;
    }
    synthlib_plugin_set_format(&gVst2Format);
    const tSynthLibPluginSet *  set = synthlib_plugin_variants();

    if ((set == NULL) || (set->count == 0)) {
        return NULL;
    }
    const tSynthLibPluginDesc * d   = &set->variants[0];   // notes §7
    tVst2 *                     v   = (tVst2 *)calloc(1, sizeof(tVst2));

    if (v == NULL) {
        return NULL;
    }
    v->desc       = d;
    v->master     = master;
    v->sampleRate = 44100.0;
    v->maxFrames  = 512u;
    v->numIn      = bus_channels(d->inputs, d->numInputs);
    v->numOut     = bus_channels(d->outputs, d->numOutputs);
    v->inst       = (d->cb.create != NULL) ? d->cb.create(d) : NULL;

    if (v->inst == NULL) {
        free(v);
        return NULL;
    }

    if (d->cb.initialize != NULL) {
        d->cb.initialize(v->inst);
    }

    if (!synthlib_params_init(&v->params, d, v->inst)) {
        destroy(v);
        return NULL;
    }
    v->pending = (_Atomic uint64_t *)calloc(v->params.count + 1u, sizeof(*v->pending));
    v->dirty   = (atomic_bool *)calloc(v->params.count + 1u, sizeof(*v->dirty));

    v->shown   = (uint32_t *)calloc(v->params.count + 1u, sizeof(uint32_t));

    if ((v->pending == NULL) || (v->dirty == NULL) || (v->shown == NULL)) {
        destroy(v);
        return NULL;
    }

    v->programIndex = -1;
    v->programs     = 1;

    for (uint32_t i = 0; i < v->params.count; i++) {
        tSynthLibParamDesc p;

        if ((v->params.flags[i] & SYNTHLIB_PARAM_HIDDEN) == 0u) {
            v->shown[v->shownCount++] = i;
        }

        if (((v->params.flags[i] & SYNTHLIB_PARAM_PROGRAM) != 0u) && synthlib_param_describe(d, v->inst, i, &p) && (p.stepCount > 0)) {
            v->programIndex = (int32_t)i;
            v->programs     = (uint32_t)p.stepCount + 1u;
        }
    }

    for (uint32_t i = 0; i < MAX_INSTANCES; i++) {
        tVst2 * none = NULL;

        if (atomic_compare_exchange_strong(&gInstances[i], &none, v)) {
            break;
        }
    }
    struct AEffect * e = &v->effect;

    e->magic            = kEffectMagic;
    e->dispatcher       = dispatcher;
    e->setParameter     = set_parameter;
    e->getParameter     = get_parameter;
    e->numPrograms      = (VstInt32)v->programs;
    e->numParams        = (VstInt32)v->shownCount;
    e->numInputs        = (VstInt32)v->numIn;
    e->numOutputs       = (VstInt32)v->numOut;
    e->flags            = effFlagsCanReplacing | effFlagsProgramChunks | (d->isInstrument ? effFlagsIsSynth : 0);
    e->ioRatio          = 1.0f;
    e->object           = v;
    e->uniqueID         = (VstInt32)d->auSubType;
    e->version          = (VstInt32)d->auVersion;
    e->processReplacing = process_replacing;
    return e;
}

#endif // SYNTHLIB_HAVE_VST2_ABI
