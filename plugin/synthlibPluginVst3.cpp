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
// synthlib_plugin_variants(); this file names no engine, no parameter and no patch.
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
//
// ONE BINARY, SEVERAL PLUG-INS. synthlib_plugin_variants() may return more than one descriptor, and
// then every class below is instantiated once per variant: the factory registers two classes for
// each, and each object carries the descriptor it belongs to rather than reaching for a global.

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
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "synthlibPlugin.h"
#include "synthlibPluginState.h"
#include "synthlibPluginVst3View.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

// ------------------------------------------------------------------------------------------------
// Shared helpers
// ------------------------------------------------------------------------------------------------

static const tSynthLibPluginSet * variants(void) {
    static const tSynthLibPluginSet * v = synthlib_plugin_variants();

    return v;
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

// The subcategory a host reads to decide what KIND of plug-in this is - and, for an effect, whether
// it may be bounced offline. NULL in the descriptor means "the obvious one for isInstrument".
static const char * sub_category_of(const tSynthLibPluginDesc * d) {
    if (d->vst3SubCategory != nullptr) {
        return d->vst3SubCategory;
    }
    return d->isInstrument ? PlugType::kInstrumentSynth : PlugType::kFx;
}

static SpeakerArrangement arrangement_for(uint32_t channels) {
    switch (channels) {
        case 0:  return 0;
        case 1:  return SpeakerArr::kMono;
        case 2:  return SpeakerArr::kStereo;
        default: return SpeakerArr::kEmpty;
    }
}

// ------------------------------------------------------------------------------------------------
// The instance registry, and why it is here
// ------------------------------------------------------------------------------------------------

// VST3 SPLITS THE PLUG-IN IN TWO, AND ONLY ONE HALF HOLDS THE ENGINE.
//
// The processor owns the instance; the controller owns the parameters and the editor. In a correct
// host that is fine - a move on the controller goes out through performEdit(), comes back to the
// processor as an automation point, and is applied there. Not every host routes it that way, and a
// bare test host does not route it at all, so the controller ALSO applies directly to the instance
// when there is exactly one of its own VARIANT to apply to.
//
// PER VARIANT, because one binary may register several plug-ins and an effect's controller must not
// find an instrument's processor. Exactly one of each is still the honest limit: the engines behind
// these plug-ins keep state in process-wide globals, so a second instance of the same variant would
// fight the first whatever this recorded. With two loaded the entry goes null and the controller
// falls back to the host's own routing.
#define MAX_VARIANTS    (8)

struct tRegistryEntry {
    std::atomic<void *> instance{nullptr};
    std::atomic<int>    count{0};
};

static tRegistryEntry gRegistry[MAX_VARIANTS];

static int variant_index_of(const tSynthLibPluginDesc * d) {
    const tSynthLibPluginSet * set = variants();

    for (uint32_t i = 0; (i < set->count) && (i < MAX_VARIANTS); i++) {
        if (&set->variants[i] == d) {
            return (int)i;
        }
    }
    return -1;
}

static void register_instance(const tSynthLibPluginDesc * d, void * inst) {
    int slot = variant_index_of(d);

    if (slot < 0) {
        return;
    }

    if (gRegistry[slot].count.fetch_add(1) == 0) {
        gRegistry[slot].instance.store(inst);
    } else {
        gRegistry[slot].instance.store(nullptr);
    }
}

static void unregister_instance(const tSynthLibPluginDesc * d, void * inst) {
    int slot = variant_index_of(d);

    if (slot < 0) {
        return;
    }

    if (gRegistry[slot].count.fetch_sub(1) == 1) {
        gRegistry[slot].instance.store(nullptr);
    } else if (gRegistry[slot].instance.load() == inst) {
        gRegistry[slot].instance.store(nullptr);
    }
}

static void * instance_of(const tSynthLibPluginDesc * d) {
    int slot = variant_index_of(d);

    return (slot >= 0) ? gRegistry[slot].instance.load() : nullptr;
}

// The controller the editor reports its own moves through - see synthlib_plugin_param_edited().
static std::atomic<IComponentHandler *> gEditHandler{nullptr};
static std::atomic<IEditController *>   gEditController{nullptr};

// The processor that owns a given plug-in instance, for synthlib_plugin_send_message(). A plain
// pointer pair rather than a map: there are at most MAX_VARIANTS of these and the lookup happens
// when a message is sent, which is a handful of times in a session.
class SynthLibProcessor;
static std::atomic<SynthLibProcessor *> gProcessors[MAX_VARIANTS];

// ------------------------------------------------------------------------------------------------
// The processor
// ------------------------------------------------------------------------------------------------

class SynthLibProcessor : public IComponent,
                          public IAudioProcessor,
                          public IConnectionPoint,
                          public IProcessContextRequirements {
public:
    explicit SynthLibProcessor(const tSynthLibPluginDesc * descriptor)
        : refCount(1), desc(descriptor), sampleRate(44100.0), active(false) {
        inst = (desc->cb.create != nullptr) ? desc->cb.create(desc) : nullptr;

        if (inst != nullptr) {
            register_instance(desc, inst);
        }
        // The defaults, and they matter before anything has been played: a parameter left at zero
        // that means "full bend down" is a plug-in reporting a state it is not in. Nothing applies
        // them to the engine - the plug-in's own create() is responsible for starting in the state
        // it advertises - but a host asking is told the truth.
        uint32_t count = synthlib_param_count(desc, inst);

        params.resize(count, 0.0);

        for (uint32_t i = 0; i < count; i++) {
            tSynthLibParamDesc p;

            if (synthlib_param_describe(desc, inst, i, &p) == true) {
                params[i] = p.defaultNormalized;
            }
        }
        int slot = variant_index_of(desc);

        if (slot >= 0) {
            gProcessors[slot].store(this);
        }
    }

    virtual ~SynthLibProcessor(void) {
        int slot = variant_index_of(desc);

        if ((slot >= 0) && (gProcessors[slot].load() == this)) {
            gProcessors[slot].store(nullptr);
        }

        if (inst != nullptr) {
            unregister_instance(desc, inst);

            if (desc->cb.destroy != nullptr) {
                desc->cb.destroy(inst);
            }
            inst = nullptr;
        }

        if (hostApp != nullptr) {
            hostApp->release();
            hostApp = nullptr;
        }
    }

    void * plugin_instance(void) const {
        return inst;
    }

    // Called from synthlib_plugin_send_message(), on whatever thread the plug-in chose. VST3's
    // IConnectionPoint is not documented as thread safe, and neither is IMessage allocation, so this
    // is for the occasional announcement it was built for and not for a stream.
    bool post_message(const char * id, int64_t value) {
        if ((peer == nullptr) || (hostApp == nullptr) || (id == nullptr)) {
            return false;
        }
        IMessage * message = nullptr;
        TUID       messageIid;

        // createInstance takes TUIDs (raw 16-byte arrays) while the interface exposes an FUID, so it
        // has to be copied out rather than passed straight through.
        memcpy(messageIid, IMessage::iid.toTUID(), sizeof(TUID));

        // ONLY THE HOST CAN MAKE ONE. IMessage is allocated through IHostApplication, which is why
        // the host context is kept from initialize() - a plug-in cannot new one of its own.
        if ((hostApp->createInstance(messageIid, messageIid, (void **)&message) != kResultOk) ||
            (message == nullptr)) {
            return false;
        }
        message->setMessageID(id);
        message->getAttributes()->setInt("value", value);
        peer->notify(message);
        message->release();
        return true;
    }

    // ---- FUnknown ------------------------------------------------------------------------------
    tresult PLUGIN_API queryInterface(const TUID iid, void ** obj) SMTG_OVERRIDE {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponent)
        QUERY_INTERFACE(iid, obj, IPluginBase::iid, IComponent)
        QUERY_INTERFACE(iid, obj, IComponent::iid, IComponent)
        QUERY_INTERFACE(iid, obj, IAudioProcessor::iid, IAudioProcessor)
        QUERY_INTERFACE(iid, obj, IConnectionPoint::iid, IConnectionPoint)

        // Only claimed by a plug-in that asked for the transport. A host is entitled to skip filling
        // in a ProcessContext nobody reads, and this is how it knows.
        if ((desc->wantsTransport == true) &&
            FUnknownPrivate::iidEqual(iid, IProcessContextRequirements::iid)) {
            addRef();
            *obj = static_cast<IProcessContextRequirements *>(this);
            return kResultOk;
        }
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

    // ---- IProcessContextRequirements -----------------------------------------------------------
    uint32 PLUGIN_API getProcessContextRequirements(void) SMTG_OVERRIDE {
        // Everything tSynthLibTransport carries. Asking for more than is read costs a host a little
        // work per block; asking for less than is read gets a zero that looks like a real answer.
        return IProcessContextRequirements::kNeedTempo |
               IProcessContextRequirements::kNeedTransportState |
               IProcessContextRequirements::kNeedProjectTimeMusic |
               IProcessContextRequirements::kNeedBarPositionMusic |
               IProcessContextRequirements::kNeedSystemTime;
    }

    // ---- IConnectionPoint ----------------------------------------------------------------------
    tresult PLUGIN_API connect(IConnectionPoint * other) SMTG_OVERRIDE {
        peer = other;
        return kResultOk;
    }

    tresult PLUGIN_API disconnect(IConnectionPoint * other) SMTG_OVERRIDE {
        (void)other;
        peer = nullptr;
        return kResultOk;
    }

    tresult PLUGIN_API notify(IMessage * message) SMTG_OVERRIDE {
        return deliver_message(desc, inst, message);
    }

    // The shape both halves use, so a message from the controller reaches the plug-in the same way
    // one from the processor does.
    static tresult deliver_message(const tSynthLibPluginDesc * d, void * inst, IMessage * message) {
        if ((message == nullptr) || (d->cb.message == nullptr) || (inst == nullptr)) {
            return kResultOk;
        }
        int64 value = 0;

        if (message->getAttributes()->getInt("value", value) != kResultOk) {
            value = 0;
        }
        d->cb.message(inst, message->getMessageID(), (int64_t)value);
        return kResultOk;
    }

    // ---- IPluginBase / IComponent --------------------------------------------------------------
    tresult PLUGIN_API initialize(FUnknown * context) SMTG_OVERRIDE {
        if (inst == nullptr) {
            return kResultFalse;
        }

        // KEPT, because IMessage can only be allocated through it - see post_message().
        if (context != nullptr) {
            context->queryInterface(IHostApplication::iid, (void **)&hostApp);
        }

        if (desc->cb.initialize != nullptr) {
            desc->cb.initialize(inst);
        }
        return kResultOk;
    }

    tresult PLUGIN_API terminate(void) SMTG_OVERRIDE {
        if ((inst != nullptr) && (desc->cb.terminate != nullptr)) {
            desc->cb.terminate(inst);
        }

        if (hostApp != nullptr) {
            hostApp->release();
            hostApp = nullptr;
        }
        return kResultOk;
    }

    tresult PLUGIN_API getControllerClassId(TUID classId) SMTG_OVERRIDE {
        // This is how the host finds the controller. Returning kNotImplemented here - which is what
        // a single-object processor+controller does - leaves a host with no controller and therefore
        // no parameters and no panel.
        memcpy(classId, desc->vst3ControllerUid, sizeof(TUID));
        return kResultOk;
    }

    tresult PLUGIN_API setIoMode(IoMode mode) SMTG_OVERRIDE {
        (void)mode;
        return kNotImplemented;
    }

    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) SMTG_OVERRIDE {
        if (type == kAudio) {
            return (int32)((dir == kInput) ? desc->numInputs : desc->numOutputs);
        }

        if ((type == kEvent) && (dir == kInput) && (desc->wantsMidiIn == true)) {
            return 1;
        }
        return 0;
    }

    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo & bus) SMTG_OVERRIDE {
        memset(&bus, 0, sizeof(bus));

        if (type == kAudio) {
            const tSynthLibBus * list  = (dir == kInput) ? desc->inputs : desc->outputs;
            uint32_t             count = (dir == kInput) ? desc->numInputs : desc->numOutputs;

            if ((index < 0) || ((uint32_t)index >= count)) {
                return kInvalidArgument;
            }
            const tSynthLibBus * b = &list[index];

            bus.mediaType    = kAudio;
            bus.direction    = dir;
            bus.channelCount = (int32)b->channels;

            // AUX AND INACTIVE, for a side-chain. A host asked to fill a main input the plug-in does
            // not need will connect something to it and then wonder why nothing happens.
            bus.busType = b->isAux ? kAux : kMain;
            bus.flags   = b->defaultActive ? BusInfo::kDefaultActive : 0;
            copy_name(bus.name, b->name);
            return kResultOk;
        }

        if ((type == kEvent) && (dir == kInput) && (desc->wantsMidiIn == true) && (index == 0)) {
            bus.mediaType    = kEvent;
            bus.direction    = kInput;
            bus.channelCount = 1;
            bus.busType      = kMain;
            bus.flags        = BusInfo::kDefaultActive;
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

        if (desc->cb.setActive != nullptr) {
            desc->cb.setActive(inst, (state != 0));
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
        size_t               need = synthlib_state_write(desc, inst, params.data(), nullptr, 0);
        std::vector<uint8_t> blob(need);
        size_t               wrote = synthlib_state_write(desc, inst, params.data(),
                                                          blob.empty() ? nullptr : blob.data(), need);
        int32                written = 0;

        return state->write(blob.empty() ? (void *)"" : (void *)blob.data(), (int32)wrote, &written);
    }

    // ---- IAudioProcessor -----------------------------------------------------------------------
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement * inputs, int32 numIns,
                                          SpeakerArrangement * outputs, int32 numOuts) SMTG_OVERRIDE {
        // A HOST IS ENTITLED TO PROPOSE ZERO for a bus it has decided not to fill, which for an aux
        // side-chain is the normal case - so a count that is short is accepted rather than refused.
        if ((numIns > (int32)desc->numInputs) || (numOuts != (int32)desc->numOutputs)) {
            return kResultFalse;
        }

        for (int32 i = 0; i < numIns; i++) {
            if ((inputs[i] != 0) &&
                (SpeakerArr::getChannelCount(inputs[i]) != (int32)desc->inputs[i].channels)) {
                return kResultFalse;
            }
        }

        for (int32 i = 0; i < numOuts; i++) {
            if (SpeakerArr::getChannelCount(outputs[i]) != (int32)desc->outputs[i].channels) {
                return kResultFalse;
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement & arr) SMTG_OVERRIDE {
        const tSynthLibBus * list  = (dir == kInput) ? desc->inputs : desc->outputs;
        uint32_t             count = (dir == kInput) ? desc->numInputs : desc->numOutputs;

        if ((index < 0) || ((uint32_t)index >= count)) {
            return kInvalidArgument;
        }
        arr = arrangement_for(list[index].channels);
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

        if ((inst != nullptr) && (desc->cb.setSampleRate != nullptr)) {
            desc->cb.setSampleRate(inst, sampleRate);
        }
        return kResultOk;
    }

    tresult PLUGIN_API setProcessing(TBool state) SMTG_OVERRIDE {
        // Going from stopped to running is where a host expects held notes to have gone. Reset on
        // the way IN rather than on the way out, so a transport jump starts clean.
        if ((state != 0) && (inst != nullptr) && (desc->cb.reset != nullptr)) {
            desc->cb.reset(inst);
        }
        return kResultOk;
    }

    uint32 PLUGIN_API getTailSamples(void) SMTG_OVERRIDE {
        return kInfiniteTail;       // reverbs and delays live in these
    }

    tresult PLUGIN_API process(ProcessData & data) SMTG_OVERRIDE {
        if (inst == nullptr) {
            return kResultOk;
        }
        apply_automation(data);
        apply_events(data);

        if ((data.numSamples <= 0) || (desc->cb.process == nullptr)) {
            return kResultOk;
        }
        // THE INPUT MAY SIMPLY NOT BE THERE. A host that left an aux bus unconnected hands over a
        // bus whose channelBuffers32 is null, or no bus at all, and a plug-in dereferencing it
        // crashes the host rather than the other way round.
        const float * const * in    = nullptr;
        uint32_t              numIn = 0;

        if ((data.numInputs > 0) && (data.inputs[0].channelBuffers32 != nullptr)) {
            in    = (const float * const *)data.inputs[0].channelBuffers32;
            numIn = (uint32_t)data.inputs[0].numChannels;
        }
        float ** out    = nullptr;
        uint32_t numOut = 0;

        if ((data.numOutputs > 0) && (data.outputs[0].channelBuffers32 != nullptr)) {
            out    = data.outputs[0].channelBuffers32;
            numOut = (uint32_t)data.outputs[0].numChannels;

            for (uint32_t c = 0; c < numOut; c++) {
                if (out[c] == nullptr) {
                    return kResultOk;
                }
            }
        }
        tSynthLibTransport transport;

        fill_transport(data.processContext, transport);
        desc->cb.process(inst, in, numIn, out, numOut, (uint32_t)data.numSamples, &transport);

        if (data.numOutputs > 0) {
            data.outputs[0].silenceFlags = 0;
        }
        return kResultOk;
    }

    static FUnknown * createInstance(const tSynthLibPluginDesc * d) {
        return (IAudioProcessor *)new SynthLibProcessor(d);
    }

private:
    void fill_transport(ProcessContext * ctx, tSynthLibTransport & out) {
        memset(&out, 0, sizeof(out));
        out.sampleRate = sampleRate;

        if (ctx == nullptr) {
            return;         // valid stays false, which is the whole point of the flag
        }
        out.valid              = true;
        out.playing            = ((ctx->state & ProcessContext::kPlaying) != 0);
        out.recording          = ((ctx->state & ProcessContext::kRecording) != 0);
        out.cycleActive        = ((ctx->state & ProcessContext::kCycleActive) != 0);
        out.tempoValid         = ((ctx->state & ProcessContext::kTempoValid) != 0);
        out.tempo              = out.tempoValid ? ctx->tempo : 0.0;
        out.musicTimeValid     = ((ctx->state & ProcessContext::kProjectTimeMusicValid) != 0);
        out.projectTimeMusic   = out.musicTimeValid ? ctx->projectTimeMusic : 0.0;
        out.barPositionValid   = ((ctx->state & ProcessContext::kBarPositionValid) != 0);
        out.barPositionMusic   = out.barPositionValid ? ctx->barPositionMusic : 0.0;
        out.systemTimeValid    = ((ctx->state & ProcessContext::kSystemTimeValid) != 0);
        out.systemTime         = out.systemTimeValid ? (uint64_t)ctx->systemTime : 0u;
        out.projectTimeSamples = (int64_t)ctx->projectTimeSamples;

        if (ctx->sampleRate > 0.0) {
            out.sampleRate = ctx->sampleRate;
        }
    }

    void apply_automation(ProcessData & data) {
        // Only the LAST point in each queue is taken: the engines have no notion of a parameter
        // ramping within a block, so interpolating between points would be inventing a resolution
        // they cannot use. Same block-granularity trade as the events below.
        if (data.inputParameterChanges == nullptr) {
            return;
        }
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

    void apply_events(ProcessData & data) {
        if (data.inputEvents == nullptr) {
            return;
        }
        int32 count = data.inputEvents->getEventCount();

        for (int32 i = 0; i < count; i++) {
            Event e = {};

            if (data.inputEvents->getEvent(i, e) != kResultOk) {
                continue;
            }

            if (e.type == Event::kNoteOnEvent) {
                // A note-on at zero velocity is a note-off, as it is over MIDI.
                if ((e.noteOn.velocity > 0.0f) && (desc->cb.noteOn != nullptr)) {
                    desc->cb.noteOn(inst, (uint8_t)e.noteOn.pitch, e.noteOn.velocity);
                } else if (desc->cb.noteOff != nullptr) {
                    desc->cb.noteOff(inst, (uint8_t)e.noteOn.pitch);
                }
            } else if (e.type == Event::kNoteOffEvent) {
                if (desc->cb.noteOff != nullptr) {
                    desc->cb.noteOff(inst, (uint8_t)e.noteOff.pitch);
                }
            } else if (e.type == Event::kPolyPressureEvent) {
                // POLYPHONIC key pressure, which arrives as an EVENT and not as a parameter change.
                // IMidiMapping's kAfterTouch is CHANNEL pressure (0xD0) only, so a keyboard sending
                // poly pressure (0xA0) - and plenty do - reaches a plug-in by this path or not at all.
                if (desc->cb.polyPressure != nullptr) {
                    desc->cb.polyPressure(inst, (uint8_t)e.polyPressure.pitch, e.polyPressure.pressure);
                }
            }
        }
    }

    void apply_param(uint32_t id, ParamValue value) {
        if (id >= params.size()) {
            return;
        }

        if (value < 0.0) {
            value = 0.0;
        } else if (value > 1.0) {
            value = 1.0;
        }
        params[id] = value;

        if ((inst != nullptr) && (desc->cb.setParam != nullptr)) {
            desc->cb.setParam(inst, id, value);
        }
    }

    void apply_state(const std::vector<uint8_t> & blob) {
        std::vector<double> values(params.size(), 0.0);
        uint32_t            count      = 0;
        const void *        pluginData = nullptr;
        size_t              pluginLen  = 0;

        if (synthlib_state_read(desc, blob.empty() ? nullptr : blob.data(), blob.size(),
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
        if ((inst != nullptr) && (desc->cb.setState != nullptr)) {
            desc->cb.setState(inst, pluginData, pluginLen);
        }
    }

    // Whole-stream read. The blob carries whatever the plug-in wanted to keep - for G2 Alike that is
    // a patch path, which has no fixed length - and a single fixed-size read would silently truncate
    // a long one into a path that does not exist.
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

    std::atomic<int32>          refCount;
    const tSynthLibPluginDesc * desc;
    void *                      inst = nullptr;
    std::vector<double>         params;
    double                      sampleRate;
    bool                        active;
    IConnectionPoint *          peer    = nullptr;
    IHostApplication *          hostApp = nullptr;
};

// ------------------------------------------------------------------------------------------------
// The controller
// ------------------------------------------------------------------------------------------------

class SynthLibController : public IEditController, public IMidiMapping, public IConnectionPoint {
public:
    explicit SynthLibController(const tSynthLibPluginDesc * descriptor)
        : refCount(1), desc(descriptor) {
        uint32_t count = synthlib_param_count(desc, instance_for_edits());

        params.resize(count, 0.0);

        for (uint32_t i = 0; i < count; i++) {
            tSynthLibParamDesc p;

            if (synthlib_param_describe(desc, instance_for_edits(), i, &p) == true) {
                params[i] = p.defaultNormalized;
            }
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
        QUERY_INTERFACE(iid, obj, IConnectionPoint::iid, IConnectionPoint)
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

    // ---- IConnectionPoint ----------------------------------------------------------------------
    //
    // The processor announces things the editor has no other way to learn. Without this a panel has
    // no way to know WHICH processor it belongs to, which is how one showing a microphone came to
    // report that it was capturing a Kronos - it was reading the other instance's figures.
    tresult PLUGIN_API connect(IConnectionPoint * other) SMTG_OVERRIDE {
        peer = other;
        return kResultOk;
    }

    tresult PLUGIN_API disconnect(IConnectionPoint * other) SMTG_OVERRIDE {
        (void)other;
        peer = nullptr;
        return kResultOk;
    }

    tresult PLUGIN_API notify(IMessage * message) SMTG_OVERRIDE {
        return SynthLibProcessor::deliver_message(desc, instance_for_edits(), message);
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
        void *   inst  = instance_for_edits();
        uint32_t count = synthlib_param_count(desc, inst);

        for (uint32_t i = 0; i < count; i++) {
            tSynthLibParamDesc p;

            if ((synthlib_param_describe(desc, inst, i, &p) == true) &&
                (p.midiControl == (int16_t)midiControllerNumber)) {
                id = (ParamID)p.id;
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

        if (synthlib_state_read(desc, blob.empty() ? nullptr : blob.data(), blob.size(),
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
        return (int32)synthlib_param_count(desc, instance_for_edits());
    }

    tresult PLUGIN_API getParameterInfo(int32 paramIndex, ParameterInfo & info) SMTG_OVERRIDE {
        tSynthLibParamDesc p;

        if ((paramIndex < 0) ||
            (synthlib_param_describe(desc, instance_for_edits(), (uint32_t)paramIndex, &p) == false)) {
            return kResultFalse;
        }
        memset(&info, 0, sizeof(info));
        info.id                     = (ParamID)p.id;
        info.stepCount              = p.stepCount;
        info.unitId                 = 0;                    // kRootUnitId
        info.flags                  = ParameterInfo::kCanAutomate;
        info.defaultNormalizedValue = p.defaultNormalized;
        copy_name(info.title, p.title);
        copy_name(info.shortTitle, (p.shortTitle[0] != '\0') ? p.shortTitle : p.title);
        copy_name(info.units, synthlib_param_units(p.unit));
        return kResultOk;
    }

    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue valueNormalized,
                                             String128 string) SMTG_OVERRIDE {
        char text[64];

        if (synthlib_param_text(desc, instance_for_edits(), (uint32_t)id, valueNormalized,
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
        tSynthLibParamDesc p;

        if (synthlib_param_by_id(desc, instance_for_edits(), (uint32_t)id, &p) == false) {
            return valueNormalized;
        }
        return synthlib_param_to_plain(&p, valueNormalized);
    }

    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue plainValue) SMTG_OVERRIDE {
        tSynthLibParamDesc p;

        if (synthlib_param_by_id(desc, instance_for_edits(), (uint32_t)id, &p) == false) {
            return plainValue;
        }
        return synthlib_param_to_normalized(&p, plainValue);
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

        // AND STRAIGHT INTO THE ENGINE TOO, when there is exactly one instance of this variant to
        // put it into - see the registry note above. With a host that routes properly this is the
        // same value the processor is about to be given anyway; with one that does not, it is the
        // only way a move on the host's generic panel is heard at all.
        void * inst = instance_for_edits();

        if ((inst != nullptr) && (desc->cb.setParam != nullptr)) {
            desc->cb.setParam(inst, (uint32_t)id, value);
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

        if ((desc->cb.createView == nullptr) || (desc->editorDefaultWidth <= 0.0)) {
            return nullptr;         // no editor; the host draws its own panel from the parameters
        }
        return synthlib_vst3_create_view(desc, instance_for_edits());
    }

    static FUnknown * createInstance(const tSynthLibPluginDesc * d) {
        return (IEditController *)new SynthLibController(d);
    }

private:
    // The processor's instance for THIS variant, when there is exactly one to be sure about.
    void * instance_for_edits(void) const {
        return instance_of(desc);
    }

    std::atomic<int32>          refCount;
    const tSynthLibPluginDesc * desc;
    IComponentHandler *         componentHandler = nullptr;
    IConnectionPoint *          peer             = nullptr;
    std::vector<double>         params;
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

bool synthlib_plugin_send_message(void * inst, const char * id, int64_t value) {
    for (int i = 0; i < MAX_VARIANTS; i++) {
        SynthLibProcessor * p = gProcessors[i].load();

        if ((p != nullptr) && (p->plugin_instance() == inst)) {
            return p->post_message(id, value);
        }
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// Factory
// ------------------------------------------------------------------------------------------------

// TWO CLASSES PER VARIANT: index 2n is variant n's processor, 2n+1 its controller. The controllers
// go under kVstComponentControllerClass and NOT kVstAudioEffectClass, or a host enumerating plug-ins
// finds twice as many as there are.
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
        const tSynthLibPluginDesc * d = &variants()->variants[0];

        memset(info, 0, sizeof(PFactoryInfo));
        strncpy(info->vendor, d->vendor, PFactoryInfo::kNameSize - 1);
        strncpy(info->url, (d->url != nullptr) ? d->url : "", PFactoryInfo::kURLSize - 1);
        strncpy(info->email, (d->email != nullptr) ? d->email : "", PFactoryInfo::kEmailSize - 1);
        info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }

    int32 PLUGIN_API countClasses(void) SMTG_OVERRIDE {
        return (int32)(variants()->count * 2u);
    }

    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo * info) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d = nullptr;
        bool                        isController = false;

        if ((info == nullptr) || (resolve(index, &d, &isController) == false)) {
            return kInvalidArgument;
        }
        memset(info, 0, sizeof(PClassInfo));
        info->cardinality = PClassInfo::kManyInstances;
        memcpy(info->cid, isController ? d->vst3ControllerUid : d->vst3ProcessorUid, sizeof(TUID));
        strncpy(info->category, isController ? kVstComponentControllerClass : kVstAudioEffectClass,
                PClassInfo::kCategorySize - 1);
        class_name(info->name, PClassInfo::kNameSize, d, isController);
        return kResultOk;
    }

    // The one that actually matters - see the note at the top of the file.
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2 * info) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d = nullptr;
        bool                        isController = false;

        if ((info == nullptr) || (resolve(index, &d, &isController) == false)) {
            return kInvalidArgument;
        }
        memset(info, 0, sizeof(PClassInfo2));
        info->cardinality = PClassInfo::kManyInstances;
        info->classFlags  = 0;
        strncpy(info->vendor, d->vendor, PClassInfo2::kVendorSize - 1);
        strncpy(info->version, d->version, PClassInfo2::kVersionSize - 1);
        strncpy(info->sdkVersion, kVstVersionString, PClassInfo2::kVersionSize - 1);
        memcpy(info->cid, isController ? d->vst3ControllerUid : d->vst3ProcessorUid, sizeof(TUID));
        strncpy(info->category, isController ? kVstComponentControllerClass : kVstAudioEffectClass,
                PClassInfo::kCategorySize - 1);
        class_name(info->name, PClassInfo::kNameSize, d, isController);

        if (isController == false) {
            strncpy(info->subCategories, sub_category_of(d), PClassInfo2::kSubCategoriesSize - 1);
        }
        return kResultOk;
    }

    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW * info) SMTG_OVERRIDE {
        const tSynthLibPluginDesc * d = nullptr;
        bool                        isController = false;

        if ((info == nullptr) || (resolve(index, &d, &isController) == false)) {
            return kInvalidArgument;
        }
        char name[PClassInfo::kNameSize];

        memset(info, 0, sizeof(PClassInfoW));
        info->cardinality = PClassInfo::kManyInstances;
        info->classFlags  = 0;
        memcpy(info->cid, isController ? d->vst3ControllerUid : d->vst3ProcessorUid, sizeof(TUID));
        strncpy(info->category, isController ? kVstComponentControllerClass : kVstAudioEffectClass,
                PClassInfo::kCategorySize - 1);

        if (isController == false) {
            strncpy(info->subCategories, sub_category_of(d), PClassInfo2::kSubCategoriesSize - 1);
        }
        class_name(name, sizeof(name), d, isController);
        widen(info->name, name, PClassInfo::kNameSize);
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
        const tSynthLibPluginSet * set      = variants();
        FUnknown *                 instance = nullptr;

        for (uint32_t i = 0; (i < set->count) && (instance == nullptr); i++) {
            const tSynthLibPluginDesc * d = &set->variants[i];

            if (memcmp(cid, d->vst3ProcessorUid, sizeof(TUID)) == 0) {
                instance = SynthLibProcessor::createInstance(d);
            } else if (memcmp(cid, d->vst3ControllerUid, sizeof(TUID)) == 0) {
                instance = SynthLibController::createInstance(d);
            }
        }

        if (instance == nullptr) {
            return kResultFalse;
        }
        // _iid is a FIDString (const char *); queryInterface's TUID parameter decays to the same
        // thing, so it is passed straight through - a cast to TUID would be a cast to an array type.
        tresult result = instance->queryInterface(_iid, obj);

        instance->release();        // queryInterface took its own reference
        return result;
    }

private:
    static bool resolve(int32 index, const tSynthLibPluginDesc ** desc, bool * isController) {
        const tSynthLibPluginSet * set = variants();

        if ((index < 0) || (index >= (int32)(set->count * 2u))) {
            return false;
        }
        *desc         = &set->variants[index / 2];
        *isController = ((index % 2) == 1);
        return true;
    }

    static void class_name(char * out, size_t len, const tSynthLibPluginDesc * d, bool isController) {
        if (isController == true) {
            snprintf(out, len, "%s Controller", d->name);
        } else {
            snprintf(out, len, "%s", d->name);
        }
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
