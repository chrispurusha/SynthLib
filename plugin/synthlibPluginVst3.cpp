/*
 * SynthLib - the VST3 wrapper, written once for every plug-in in these projects.
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

// THE VST3 SIDE, AND NOTHING ELSE. Everything specific to a particular plug-in arrives through
// synthlib_plugin_descriptor(); this file names no engine, no parameter and no patch.
//
// Built against pluginterfaces/ ONLY: the SDK's public.sdk helper classes are not used, so there is
// no CMake and nothing to reconcile with an Xcode-only application build. The cost is that the COM
// plumbing below - reference counting, queryInterface, the factory - is written out by hand rather
// than inherited. It is dull but it is all here, which is the point.
//
// THREE THINGS IN HERE WERE EACH LEARNED FROM A HOST REFUSING TO LOAD, and none of them are
// optional:
//
//   1. THE PROCESSOR AND THE CONTROLLER ARE SEPARATE REGISTERED CLASSES. VST3 permits one object to
//      implement IComponent + IAudioProcessor + IEditController, which is simpler and which a
//      hand-written test host accepts happily. Ableton does not: it obtains the controller by
//      instantiating the class named by IComponent::getControllerClassId() and does NOT fall back to
//      asking the component for IEditController. With a single object it logged "parameter count is
//      0" and its wrench icon opened nothing.
//
//   2. THE FACTORY IS IPluginFactory2 OR BETTER. The base interface reports only a class CATEGORY
//      ("Audio Module Class"), which says a plug-in makes audio but not whether it is an instrument
//      or an effect. A host that cannot tell assumes effect, looks for the audio input an effect
//      must have, finds none, and rejects it:
//
//          error: Vst3: plugin has an effect category, but no valid audio input bus
//
//      The subcategory that settles it - PlugType::kInstrumentSynth - exists only on PClassInfo2,
//      which arrived with IPluginFactory2.
//
//   3. IMidiMapping IS WHY PITCH BEND AND THE WHEELS WORK AT ALL. A VST3 host does not deliver them
//      as MIDI events the way note on and note off arrive: continuous controllers, channel pressure
//      and pitch bend are converted into PARAMETER CHANGES, and this interface is the only place a
//      plug-in says which parameter each one becomes. Without it, notes play and every expressive
//      control does nothing - silently, since nothing is technically wrong.

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "synthlibPlugin.h"
#include "synthlibPluginState.h"
#include "synthlibPluginVst3View.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

// ------------------------------------------------------------------------------------------------
// Shared helpers
// ------------------------------------------------------------------------------------------------

static const tSynthLibPluginDesc * desc(void) {
    static const tSynthLibPluginDesc * d = synthlib_plugin_descriptor();

    return d;
}

static void copy_name(String128 dst, const char * src) {
    int i = 0;

    for (; (src != nullptr) && (src[i] != '\0') && (i < 127); i++) {
        dst[i] = (TChar)src[i];
    }
    dst[i] = 0;
}

// ASCII into the char16 fields PClassInfoW uses. Everything here is ASCII, so a widening copy is the
// whole conversion.
static void widen(char16 * dst, const char * src, int32 capacity) {
    int32 i = 0;

    for (; (src != nullptr) && (src[i] != '\0') && (i < capacity - 1); i++) {
        dst[i] = (char16)src[i];
    }
    dst[i] = 0;
}

// The table lookup and the 0..1 mapping both live in synthlibPluginState.c, so this wrapper and the
// Audio Unit one cannot disagree about what an out-of-range id means or where a value sits in its
// own range.
static const tSynthLibParam * param_at(uint32_t id) {
    return synthlib_param_at(desc(), id);
}

// The units string a host puts after the number. VST3 has no unit ENUM the way an Audio Unit does,
// so this is where tSynthLibParamUnit lands on this side.
static const char * units_of(tSynthLibParamUnit unit) {
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

// ------------------------------------------------------------------------------------------------
// The one-instance registry, and why it is here
// ------------------------------------------------------------------------------------------------

// VST3 SPLITS THE PLUG-IN IN TWO, AND ONLY ONE HALF HOLDS THE ENGINE.
//
// The processor owns the instance; the controller owns the parameters and the editor. In a correct
// host that is fine - a move on the controller goes out through performEdit(), comes back to the
// processor as an automation point, and is applied there. Not every host routes it that way, and a
// bare test host does not route it at all, so the controller ALSO applies directly to the instance
// when there is exactly one to apply to.
//
// EXACTLY ONE IS THE HONEST LIMIT, not a simplification. The engines behind these plug-ins keep
// their state in process-wide globals reached through atomics, so a second instance in the same
// project would fight the first over the same engine whatever this pointer did. Recording it here
// at least means the controller cannot pick the wrong one: with two loaded, this goes null and the
// controller falls back to the host's own routing.
static std::atomic<void *> gSoleInstance{nullptr};
static std::atomic<int>    gInstanceCount{0};

static void register_instance(void * inst) {
    if (gInstanceCount.fetch_add(1) == 0) {
        gSoleInstance.store(inst);
    } else {
        gSoleInstance.store(nullptr);
    }
}

static void unregister_instance(void * inst) {
    if (gInstanceCount.fetch_sub(1) == 1) {
        gSoleInstance.store(nullptr);
    } else if (gSoleInstance.load() == inst) {
        gSoleInstance.store(nullptr);
    }
}

// The controller the editor reports its own moves through - see synthlib_plugin_param_edited().
static std::atomic<IComponentHandler *> gEditHandler{nullptr};
static std::atomic<IEditController *>   gEditController{nullptr};

// ------------------------------------------------------------------------------------------------
// The processor
// ------------------------------------------------------------------------------------------------

class SynthLibProcessor : public IComponent, public IAudioProcessor {
public:
    SynthLibProcessor(void) : refCount(1), sampleRate(44100.0), active(false) {
        const tSynthLibPluginDesc * d = desc();

        params.resize(d->numParams, 0.0);

        // The defaults, and they matter before anything has been played: a parameter left at zero
        // that means "full bend down" is a plug-in reporting a state it is not in. Nothing applies
        // them to the engine yet - the plug-in's own create() is responsible for starting in the
        // state it advertises - but a host asking is told the truth.
        for (uint32_t i = 0; i < d->numParams; i++) {
            params[i] = d->params[i].defaultNormalized;
        }
        inst = (d->cb.create != nullptr) ? d->cb.create() : nullptr;

        if (inst != nullptr) {
            register_instance(inst);
        }
    }

    virtual ~SynthLibProcessor(void) {
        const tSynthLibPluginDesc * d = desc();

        if (inst != nullptr) {
            unregister_instance(inst);

            if (d->cb.destroy != nullptr) {
                d->cb.destroy(inst);
            }
            inst = nullptr;
        }
    }

    // ---- FUnknown ------------------------------------------------------------------------------
    tresult PLUGIN_API queryInterface(const TUID iid, void ** obj) SMTG_OVERRIDE {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponent)
        QUERY_INTERFACE(iid, obj, IPluginBase::iid, IComponent)
        QUERY_INTERFACE(iid, obj, IComponent::iid, IComponent)
        QUERY_INTERFACE(iid, obj, IAudioProcessor::iid, IAudioProcessor)
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef(void) SMTG_OVERRIDE {
        return (uint32)++refCount;
    }

    uint32 PLUGIN_API release(void) SMTG_OVERRIDE {
        int32 c = --refCount;

        if (c == 0) {
            delete this;
            return 0;
        }
        return (uint32)c;
    }

    // ---- IPluginBase / IComponent --------------------------------------------------------------
    tresult PLUGIN_API initialize(FUnknown * context) SMTG_OVERRIDE {
        (void)context;

        if (inst == nullptr) {
            return kResultFalse;
        }

        if (desc()->cb.initialize != nullptr) {
            desc()->cb.initialize(inst);
        }
        return kResultOk;
    }

    tresult PLUGIN_API terminate(void) SMTG_OVERRIDE {
        if ((inst != nullptr) && (desc()->cb.terminate != nullptr)) {
            desc()->cb.terminate(inst);
        }
        return kResultOk;
    }

    tresult PLUGIN_API getControllerClassId(TUID classId) SMTG_OVERRIDE {
        // This is how the host finds the controller. Returning kNotImplemented here - which is what
        // a single-object processor+controller does - leaves a host with no controller and therefore
        // no parameters and no panel.
        memcpy(classId, desc()->vst3ControllerUid, sizeof(TUID));
        return kResultOk;
    }

    tresult PLUGIN_API setIoMode(IoMode mode) SMTG_OVERRIDE {
        (void)mode;
        return kNotImplemented;
    }

    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d = desc();

        if ((type == kAudio) && (dir == kOutput) && (d->numOutputChannels > 0)) {
            return 1;
        }

        if ((type == kAudio) && (dir == kInput) && (d->numInputChannels > 0)) {
            return 1;
        }

        if ((type == kEvent) && (dir == kInput) && (d->wantsMidi == true)) {
            return 1;
        }
        return 0;
    }

    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo & bus) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d = desc();

        if (index != 0) {
            return kInvalidArgument;
        }
        memset(&bus, 0, sizeof(bus));
        bus.busType = kMain;
        bus.flags   = BusInfo::kDefaultActive;

        if ((type == kAudio) && (dir == kOutput) && (d->numOutputChannels > 0)) {
            bus.mediaType    = kAudio;
            bus.direction    = kOutput;
            bus.channelCount = (int32)d->numOutputChannels;
            copy_name(bus.name, "Output");
            return kResultOk;
        }

        if ((type == kAudio) && (dir == kInput) && (d->numInputChannels > 0)) {
            bus.mediaType    = kAudio;
            bus.direction    = kInput;
            bus.channelCount = (int32)d->numInputChannels;
            copy_name(bus.name, "Input");
            return kResultOk;
        }

        if ((type == kEvent) && (dir == kInput) && (d->wantsMidi == true)) {
            bus.mediaType    = kEvent;
            bus.direction    = kInput;
            bus.channelCount = 1;
            copy_name(bus.name, "MIDI In");
            return kResultOk;
        }
        return kInvalidArgument;
    }

    tresult PLUGIN_API getRoutingInfo(RoutingInfo & inInfo, RoutingInfo & outInfo) SMTG_OVERRIDE {
        (void)inInfo;
        (void)outInfo;
        return kNotImplemented;
    }

    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) SMTG_OVERRIDE {
        (void)type;
        (void)dir;
        (void)index;
        (void)state;
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE {
        if (inst == nullptr) {
            return kResultFalse;
        }

        if (desc()->cb.setActive != nullptr) {
            desc()->cb.setActive(inst, (state != 0));
        }
        active = (state != 0);
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream * state) SMTG_OVERRIDE {
        if (state == nullptr) {
            return kResultFalse;
        }
        std::vector<uint8_t> blob = read_stream(state);

        apply_state(blob);
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream * state) SMTG_OVERRIDE {
        if (state == nullptr) {
            return kResultFalse;
        }
        size_t               need = synthlib_state_write(desc(), inst, params.data(), nullptr, 0);
        std::vector<uint8_t> blob(need);
        size_t               wrote = synthlib_state_write(desc(), inst, params.data(),
                                                          blob.empty() ? nullptr : blob.data(), need);
        int32                written = 0;

        return state->write(blob.empty() ? (void *)"" : (void *)blob.data(),
                            (int32)wrote, &written);
    }

    // ---- IAudioProcessor -----------------------------------------------------------------------
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement * inputs, int32 numIns,
                                          SpeakerArrangement * outputs, int32 numOuts) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d = desc();
        int32 wantIn  = (d->numInputChannels > 0) ? 1 : 0;
        int32 wantOut = (d->numOutputChannels > 0) ? 1 : 0;

        if ((numIns != wantIn) || (numOuts != wantOut)) {
            return kResultFalse;
        }

        if ((wantIn == 1) && (SpeakerArr::getChannelCount(inputs[0]) != (int32)d->numInputChannels)) {
            return kResultFalse;
        }

        if ((wantOut == 1) && (SpeakerArr::getChannelCount(outputs[0]) != (int32)d->numOutputChannels)) {
            return kResultFalse;
        }
        return kResultOk;
    }

    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement & arr) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d = desc();

        if (index != 0) {
            return kInvalidArgument;
        }
        uint32_t channels = (dir == kOutput) ? d->numOutputChannels : d->numInputChannels;

        if (channels == 0) {
            return kInvalidArgument;
        }
        arr = (channels == 1) ? SpeakerArr::kMono : SpeakerArr::kStereo;
        return kResultOk;
    }

    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE {
        // The engines render float. A host asking for double is told no and will hand us float.
        return (symbolicSampleSize == kSample32) ? kResultTrue : kResultFalse;
    }

    uint32 PLUGIN_API getLatencySamples(void) SMTG_OVERRIDE {
        return 0;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup & setup) SMTG_OVERRIDE {
        sampleRate = setup.sampleRate;

        if ((inst != nullptr) && (desc()->cb.setSampleRate != nullptr)) {
            desc()->cb.setSampleRate(inst, sampleRate);
        }
        return kResultOk;
    }

    tresult PLUGIN_API setProcessing(TBool state) SMTG_OVERRIDE {
        // Going from stopped to running is where a host expects held notes to have gone. Reset on
        // the way IN rather than on the way out, so a transport jump starts clean.
        if ((state != 0) && (inst != nullptr) && (desc()->cb.reset != nullptr)) {
            desc()->cb.reset(inst);
        }
        return kResultOk;
    }

    uint32 PLUGIN_API getTailSamples(void) SMTG_OVERRIDE {
        return kInfiniteTail;       // reverbs and delays live in these
    }

    tresult PLUGIN_API process(ProcessData & data) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d = desc();

        if (inst == nullptr) {
            return kResultOk;
        }

        // Automation, before anything is rendered. Only the LAST point in each queue is taken: the
        // engines have no notion of a parameter ramping within a block, so interpolating between
        // points would be inventing a resolution they cannot use. Same block-granularity trade as
        // the events below.
        if (data.inputParameterChanges != nullptr) {
            int32 queues = data.inputParameterChanges->getParameterCount();

            for (int32 q = 0; q < queues; q++) {
                IParamValueQueue * queue = data.inputParameterChanges->getParameterData(q);

                if (queue == nullptr) {
                    continue;
                }
                int32 points = queue->getPointCount();

                if (points <= 0) {
                    continue;
                }
                int32      offset = 0;
                ParamValue value  = 0.0;

                if (queue->getPoint(points - 1, offset, value) == kResultOk) {
                    apply_param((uint32_t)queue->getParameterId(), value);
                }
            }
        }

        if (data.inputEvents != nullptr) {
            int32 count = data.inputEvents->getEventCount();

            for (int32 i = 0; i < count; i++) {
                Event e = {};

                if (data.inputEvents->getEvent(i, e) != kResultOk) {
                    continue;
                }

                if (e.type == Event::kNoteOnEvent) {
                    // A note-on at zero velocity is a note-off, as it is over MIDI.
                    if ((e.noteOn.velocity > 0.0f) && (d->cb.noteOn != nullptr)) {
                        d->cb.noteOn(inst, (uint8_t)e.noteOn.pitch, e.noteOn.velocity);
                    } else if (d->cb.noteOff != nullptr) {
                        d->cb.noteOff(inst, (uint8_t)e.noteOn.pitch);
                    }
                } else if (e.type == Event::kNoteOffEvent) {
                    if (d->cb.noteOff != nullptr) {
                        d->cb.noteOff(inst, (uint8_t)e.noteOff.pitch);
                    }
                } else if (e.type == Event::kPolyPressureEvent) {
                    // POLYPHONIC key pressure, which arrives as an EVENT and not as a parameter
                    // change. IMidiMapping's kAfterTouch is CHANNEL pressure (0xD0) only, so a
                    // keyboard sending poly pressure (0xA0) - and plenty do - reaches a plug-in by
                    // this path or not at all.
                    if (d->cb.polyPressure != nullptr) {
                        d->cb.polyPressure(inst, (uint8_t)e.polyPressure.pitch,
                                           e.polyPressure.pressure);
                    }
                }
            }
        }

        if ((data.numOutputs < 1) || (data.numSamples <= 0) || (d->cb.render == nullptr)) {
            return kResultOk;
        }
        int32 channels = data.outputs[0].numChannels;

        if ((channels < (int32)d->numOutputChannels) || (data.outputs[0].channelBuffers32 == nullptr)) {
            return kResultOk;
        }
        float ** out = data.outputs[0].channelBuffers32;

        for (int32 c = 0; c < (int32)d->numOutputChannels; c++) {
            if (out[c] == nullptr) {
                return kResultOk;
            }
        }
        d->cb.render(inst, out, d->numOutputChannels, (uint32_t)data.numSamples);
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }

    static FUnknown * createInstance(void * /*context*/) {
        return (IAudioProcessor *)new SynthLibProcessor();
    }

private:
    void apply_param(uint32_t id, ParamValue value) {
        const tSynthLibParam * p = param_at(id);

        if (p == nullptr) {
            return;
        }

        if (value < 0.0) {
            value = 0.0;
        } else if (value > 1.0) {
            value = 1.0;
        }
        params[id] = value;

        if ((inst != nullptr) && (desc()->cb.setParam != nullptr)) {
            desc()->cb.setParam(inst, id, value);
        }
    }

    void apply_state(const std::vector<uint8_t> & blob) {
        std::vector<double> values(params.size(), 0.0);
        uint32_t            count      = 0;
        const void *        pluginData = nullptr;
        size_t              pluginLen  = 0;

        if (synthlib_state_read(desc(), blob.empty() ? nullptr : blob.data(), blob.size(),
                                values.empty() ? nullptr : values.data(),
                                &count, &pluginData, &pluginLen) == false) {
            return;
        }

        for (uint32_t i = 0; i < count; i++) {
            apply_param(i, values[i]);
        }

        // THE PLUG-IN'S OWN STATE LAST. For G2 Alike that is the patch path, and loading a patch
        // rebuilds the parameter snapshot - so the parameters have to already be where the project
        // left them when it happens, or the first block plays the patch at its defaults.
        if ((inst != nullptr) && (desc()->cb.setState != nullptr)) {
            desc()->cb.setState(inst, pluginData, pluginLen);
        }
    }

    // Whole-stream read. The blob carries whatever the plug-in wanted to keep - for G2 Alike that
    // is a patch path, which has no fixed length - and a single fixed-size read would silently
    // truncate a long one into a path that does not exist.
    static std::vector<uint8_t> read_stream(IBStream * state) {
        std::vector<uint8_t> blob;
        uint8_t              chunk[4096];
        int32                got = 0;

        while ((state->read(chunk, (int32)sizeof(chunk), &got) == kResultOk) && (got > 0)) {
            blob.insert(blob.end(), chunk, chunk + got);

            if (got < (int32)sizeof(chunk)) {
                break;
            }
        }
        return blob;
    }

    std::atomic<int32>  refCount;
    void *              inst = nullptr;
    std::vector<double> params;
    double              sampleRate;
    bool                active;
};

// ------------------------------------------------------------------------------------------------
// The controller
// ------------------------------------------------------------------------------------------------

class SynthLibController : public IEditController, public IMidiMapping {
public:
    SynthLibController(void) : refCount(1) {
        const tSynthLibPluginDesc * d = desc();

        params.resize(d->numParams, 0.0);

        for (uint32_t i = 0; i < d->numParams; i++) {
            params[i] = d->params[i].defaultNormalized;
        }
        gEditController.store(this);
    }

    virtual ~SynthLibController(void) {
        if (gEditController.load() == this) {
            gEditController.store(nullptr);
        }

        if (gEditHandler.load() == componentHandler) {
            gEditHandler.store(nullptr);
        }
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void ** obj) SMTG_OVERRIDE {
        // The casts matter: each branch must hand back a pointer to the RIGHT base. Returning the
        // IEditController pointer for an IMidiMapping query would have the host call through the
        // wrong vtable.
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IEditController)
        QUERY_INTERFACE(iid, obj, IPluginBase::iid, IEditController)
        QUERY_INTERFACE(iid, obj, IEditController::iid, IEditController)
        QUERY_INTERFACE(iid, obj, IMidiMapping::iid, IMidiMapping)
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef(void) SMTG_OVERRIDE {
        return (uint32)++refCount;
    }

    uint32 PLUGIN_API release(void) SMTG_OVERRIDE {
        int32 c = --refCount;

        if (c == 0) {
            delete this;
            return 0;
        }
        return (uint32)c;
    }

    tresult PLUGIN_API initialize(FUnknown * context) SMTG_OVERRIDE {
        (void)context;
        return kResultOk;
    }

    tresult PLUGIN_API terminate(void) SMTG_OVERRIDE {
        return kResultOk;
    }

    // ---- IMidiMapping --------------------------------------------------------------------------
    //
    // Straight out of the parameter table - see the note on midiControl in synthlibPlugin.h. Most of
    // this maps a MIDI control onto a parameter that already exists rather than inventing one.
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel,
                                                   CtrlNumber midiControllerNumber,
                                                   ParamID & id) SMTG_OVERRIDE {
        (void)channel;      // one engine voice; all channels drive it, as the applications' Omni does

        if (busIndex != 0) {
            return kResultFalse;
        }
        const tSynthLibPluginDesc * d = desc();

        for (uint32_t i = 0; i < d->numParams; i++) {
            if (d->params[i].midiControl == (int16_t)midiControllerNumber) {
                id = (ParamID)d->params[i].id;
                return kResultTrue;
            }
        }
        return kResultFalse;
    }

    // ---- IEditController -----------------------------------------------------------------------

    // The host hands the controller the processor's saved state so the two agree. Parameters are
    // read back out of it; anything else in there is the processor's business.
    tresult PLUGIN_API setComponentState(IBStream * state) SMTG_OVERRIDE {
        if (state == nullptr) {
            return kResultOk;
        }
        std::vector<uint8_t> blob;
        uint8_t              chunk[4096];
        int32                got = 0;

        while ((state->read(chunk, (int32)sizeof(chunk), &got) == kResultOk) && (got > 0)) {
            blob.insert(blob.end(), chunk, chunk + got);

            if (got < (int32)sizeof(chunk)) {
                break;
            }
        }
        std::vector<double> values(params.size(), 0.0);
        uint32_t            count = 0;

        if (synthlib_state_read(desc(), blob.empty() ? nullptr : blob.data(), blob.size(),
                                values.empty() ? nullptr : values.data(),
                                &count, nullptr, nullptr) == true) {
            for (uint32_t i = 0; i < count; i++) {
                params[i] = values[i];
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream * state) SMTG_OVERRIDE {
        (void)state;
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream * state) SMTG_OVERRIDE {
        (void)state;
        return kResultOk;
    }

    int32 PLUGIN_API getParameterCount(void) SMTG_OVERRIDE {
        return (int32)desc()->numParams;
    }

    tresult PLUGIN_API getParameterInfo(int32 paramIndex, ParameterInfo & info) SMTG_OVERRIDE {
        const tSynthLibParam * p = param_at((uint32_t)paramIndex);

        if ((paramIndex < 0) || (p == nullptr)) {
            return kResultFalse;
        }
        memset(&info, 0, sizeof(info));
        info.id                     = (ParamID)p->id;
        info.stepCount              = p->stepCount;
        info.unitId                 = 0;                    // kRootUnitId
        info.flags                  = ParameterInfo::kCanAutomate;
        info.defaultNormalizedValue = p->defaultNormalized;
        copy_name(info.title, p->title);
        copy_name(info.shortTitle, (p->shortTitle != nullptr) ? p->shortTitle : p->title);
        copy_name(info.units, units_of(p->unit));
        return kResultOk;
    }

    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue valueNormalized,
                                             String128 string) SMTG_OVERRIDE {
        const tSynthLibParam * p = param_at((uint32_t)id);

        if (p == nullptr) {
            return kResultFalse;
        }
        char text[64];

        if (synthlib_param_text(desc(), instance_for_edits(), (uint32_t)id, valueNormalized,
                                text, sizeof(text)) == false) {
            return kResultFalse;
        }
        copy_name(string, text);
        return kResultOk;
    }

    tresult PLUGIN_API getParamValueByString(ParamID id, TChar * string,
                                             ParamValue & valueNormalized) SMTG_OVERRIDE {
        (void)id;
        (void)string;
        (void)valueNormalized;
        return kResultFalse;        // typed entry not supported; the host falls back to its knob
    }

    ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue valueNormalized) SMTG_OVERRIDE {
        return synthlib_param_to_plain(desc(), (uint32_t)id, valueNormalized);
    }

    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue plainValue) SMTG_OVERRIDE {
        return synthlib_param_to_normalized(desc(), (uint32_t)id, plainValue);
    }

    ParamValue PLUGIN_API getParamNormalized(ParamID id) SMTG_OVERRIDE {
        return (id < (ParamID)params.size()) ? params[id] : 0.0;
    }

    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) SMTG_OVERRIDE {
        if (id >= (ParamID)params.size()) {
            return kResultFalse;
        }

        if (value < 0.0) {
            value = 0.0;
        } else if (value > 1.0) {
            value = 1.0;
        }
        params[id] = value;

        // AND STRAIGHT INTO THE ENGINE TOO, when there is exactly one instance to put it into - see
        // the registry note above. With a host that routes properly this is the same value the
        // processor is about to be given anyway; with one that does not, it is the only way a move
        // on the host's generic panel is heard at all.
        void * inst = instance_for_edits();

        if ((inst != nullptr) && (desc()->cb.setParam != nullptr)) {
            desc()->cb.setParam(inst, (uint32_t)id, value);
        }
        return kResultOk;
    }

    tresult PLUGIN_API setComponentHandler(IComponentHandler * handler) SMTG_OVERRIDE {
        componentHandler = handler;
        gEditHandler.store(handler);
        return kResultOk;
    }

    IPlugView * PLUGIN_API createView(FIDString name) SMTG_OVERRIDE {
        if ((name == nullptr) || (strcmp(name, ViewType::kEditor) != 0)) {
            return nullptr;
        }
        const tSynthLibPluginDesc * d = desc();

        if ((d->cb.createView == nullptr) || (d->editorDefaultWidth <= 0.0)) {
            return nullptr;         // no editor; the host draws its own panel from the parameters
        }
        return synthlib_vst3_create_view(instance_for_edits());
    }

    static FUnknown * createInstance(void * /*context*/) {
        return (IEditController *)new SynthLibController();
    }

private:
    // The processor's instance, when the split has left exactly one to be sure about.
    static void * instance_for_edits(void) {
        return gSoleInstance.load();
    }

    std::atomic<int32>  refCount;
    IComponentHandler * componentHandler = nullptr;
    std::vector<double> params;
};

// ------------------------------------------------------------------------------------------------
// What the plug-in may ask of us
// ------------------------------------------------------------------------------------------------

void synthlib_plugin_param_edited(uint32_t id, double normalized) {
    IComponentHandler * handler = gEditHandler.load();
    IEditController *   ctrl    = gEditController.load();

    // BEGIN, PERFORM, END, as one gesture. That is what lets a host record the move as automation
    // and show the parameter as touched, rather than seeing a value appear from nowhere.
    if (handler != nullptr) {
        handler->beginEdit((ParamID)id);
        handler->performEdit((ParamID)id, normalized);
        handler->endEdit((ParamID)id);
    }

    if (ctrl != nullptr) {
        ctrl->setParamNormalized((ParamID)id, normalized);
    }
}

// ------------------------------------------------------------------------------------------------
// Factory
// ------------------------------------------------------------------------------------------------

class SynthLibFactory : public IPluginFactory3 {
public:
    SynthLibFactory(void) : refCount(1) {}
    virtual ~SynthLibFactory(void) {}

    tresult PLUGIN_API queryInterface(const TUID iid, void ** obj) SMTG_OVERRIDE {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IPluginFactory)
        QUERY_INTERFACE(iid, obj, IPluginFactory::iid, IPluginFactory)
        QUERY_INTERFACE(iid, obj, IPluginFactory2::iid, IPluginFactory2)
        QUERY_INTERFACE(iid, obj, IPluginFactory3::iid, IPluginFactory3)
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef(void) SMTG_OVERRIDE {
        return (uint32)++refCount;
    }

    uint32 PLUGIN_API release(void) SMTG_OVERRIDE {
        int32 c = --refCount;

        if (c == 0) {
            delete this;
            return 0;
        }
        return (uint32)c;
    }

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo * info) SMTG_OVERRIDE {
        if (info == nullptr) {
            return kInvalidArgument;
        }
        const tSynthLibPluginDesc * d = desc();

        memset(info, 0, sizeof(PFactoryInfo));
        strncpy(info->vendor, d->vendor, PFactoryInfo::kNameSize - 1);
        strncpy(info->url, (d->url != nullptr) ? d->url : "", PFactoryInfo::kURLSize - 1);
        strncpy(info->email, (d->email != nullptr) ? d->email : "", PFactoryInfo::kEmailSize - 1);
        info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }

    int32 PLUGIN_API countClasses(void) SMTG_OVERRIDE {
        return 2;                                        // processor + controller
    }

    // Class 0 is the processor, class 1 the controller. The controller is registered under
    // kVstComponentControllerClass, NOT kVstAudioEffectClass - a host enumerating instruments must
    // not find two.
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo * info) SMTG_OVERRIDE {
        if ((info == nullptr) || (index < 0) || (index > 1)) {
            return kInvalidArgument;
        }
        const tSynthLibPluginDesc * d = desc();

        memset(info, 0, sizeof(PClassInfo));
        info->cardinality = PClassInfo::kManyInstances;

        if (index == 0) {
            memcpy(info->cid, d->vst3ProcessorUid, sizeof(TUID));
            strncpy(info->category, kVstAudioEffectClass, PClassInfo::kCategorySize - 1);
            strncpy(info->name, d->name, PClassInfo::kNameSize - 1);
        } else {
            memcpy(info->cid, d->vst3ControllerUid, sizeof(TUID));
            strncpy(info->category, kVstComponentControllerClass, PClassInfo::kCategorySize - 1);
            controller_name(info->name, PClassInfo::kNameSize);
        }
        return kResultOk;
    }

    // The one that actually matters - see the note at the top of the file.
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2 * info) SMTG_OVERRIDE {
        if ((info == nullptr) || (index < 0) || (index > 1)) {
            return kInvalidArgument;
        }
        const tSynthLibPluginDesc * d = desc();

        memset(info, 0, sizeof(PClassInfo2));
        info->cardinality = PClassInfo::kManyInstances;
        info->classFlags  = 0;
        strncpy(info->vendor, d->vendor, PClassInfo2::kVendorSize - 1);
        strncpy(info->version, d->version, PClassInfo2::kVersionSize - 1);
        strncpy(info->sdkVersion, kVstVersionString, PClassInfo2::kVersionSize - 1);

        if (index == 0) {
            memcpy(info->cid, d->vst3ProcessorUid, sizeof(TUID));
            strncpy(info->category, kVstAudioEffectClass, PClassInfo::kCategorySize - 1);
            strncpy(info->name, d->name, PClassInfo::kNameSize - 1);
            strncpy(info->subCategories, sub_category(d), PClassInfo2::kSubCategoriesSize - 1);
        } else {
            memcpy(info->cid, d->vst3ControllerUid, sizeof(TUID));
            strncpy(info->category, kVstComponentControllerClass, PClassInfo::kCategorySize - 1);
            controller_name(info->name, PClassInfo::kNameSize);
        }
        return kResultOk;
    }

    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW * info) SMTG_OVERRIDE {
        if ((info == nullptr) || (index < 0) || (index > 1)) {
            return kInvalidArgument;
        }
        const tSynthLibPluginDesc * d = desc();

        memset(info, 0, sizeof(PClassInfoW));
        info->cardinality = PClassInfo::kManyInstances;
        info->classFlags  = 0;

        if (index == 0) {
            memcpy(info->cid, d->vst3ProcessorUid, sizeof(TUID));
            strncpy(info->category, kVstAudioEffectClass, PClassInfo::kCategorySize - 1);
            strncpy(info->subCategories, sub_category(d), PClassInfo2::kSubCategoriesSize - 1);
            widen(info->name, d->name, PClassInfo::kNameSize);
        } else {
            char name[PClassInfo::kNameSize];

            memcpy(info->cid, d->vst3ControllerUid, sizeof(TUID));
            strncpy(info->category, kVstComponentControllerClass, PClassInfo::kCategorySize - 1);
            controller_name(name, sizeof(name));
            widen(info->name, name, PClassInfo::kNameSize);
        }
        widen(info->vendor, d->vendor, PClassInfo2::kVendorSize);
        widen(info->version, d->version, PClassInfo2::kVersionSize);
        widen(info->sdkVersion, kVstVersionString, PClassInfo2::kVersionSize);
        return kResultOk;
    }

    tresult PLUGIN_API setHostContext(FUnknown * context) SMTG_OVERRIDE {
        (void)context;
        return kResultOk;
    }

    tresult PLUGIN_API createInstance(FIDString cid, FIDString _iid, void ** obj) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d        = desc();
        FUnknown *                  instance = nullptr;

        if (memcmp(cid, d->vst3ProcessorUid, sizeof(TUID)) == 0) {
            instance = SynthLibProcessor::createInstance(nullptr);
        } else if (memcmp(cid, d->vst3ControllerUid, sizeof(TUID)) == 0) {
            instance = SynthLibController::createInstance(nullptr);
        } else {
            return kResultFalse;
        }
        // _iid is a FIDString (const char *); queryInterface's TUID parameter decays to the same
        // thing, so it is passed straight through - a cast to TUID would be a cast to an array type.
        tresult result = instance->queryInterface(_iid, obj);

        instance->release();        // queryInterface took its own reference
        return result;
    }

private:
    static const char * sub_category(const tSynthLibPluginDesc * d) {
        return d->isInstrument ? PlugType::kInstrumentSynth : PlugType::kFx;
    }

    static void controller_name(char * out, size_t len) {
        snprintf(out, len, "%s Controller", desc()->name);
    }

    std::atomic<int32> refCount;
};

extern "C" {
SMTG_EXPORT_SYMBOL IPluginFactory * PLUGIN_API GetPluginFactory(void) {
    return new SynthLibFactory();
}

// macOS loads a .vst3 as a bundle, so these are the entry points rather than a plain dylib's.
SMTG_EXPORT_SYMBOL bool bundleEntry(void * ref) {
    (void)ref;
    return true;
}

SMTG_EXPORT_SYMBOL bool bundleExit(void) {
    return true;
}
}
