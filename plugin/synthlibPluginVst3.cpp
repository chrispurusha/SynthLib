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
// Notes: Docs/code-notes/synthlibPluginVst3.cpp.md - "// notes §k" refers there.

// notes §1

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
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

// The one message the wrapper sends for itself - see "Which processor is mine". Ids with this prefix
// are never handed to the plug-in's own message callback.
#define BIND_MESSAGE         "synthlib.bind"
#define WRAPPER_MESSAGE      "synthlib."

// How many automation points of one parameter in one block are handed to paramPoints() at most. A
// queue longer than this keeps its first points and its LAST, which is the one a level settles at.
#define MAX_POINTS           (64)

// The midiControl numbering in synthlibPlugin.h is VST3's own, written out because a C header cannot
// include a C++ one. Asserted here, where both are visible, so the two cannot drift apart in silence.
static_assert(SYNTHLIB_MIDI_AFTERTOUCH == kAfterTouch, "the SDK renumbered aftertouch");
static_assert(SYNTHLIB_MIDI_PITCH_BEND == kPitchBend, "the SDK renumbered pitch bend");
static_assert(SYNTHLIB_MIDI_CONTROLS == kCountCtrlNumber, "the SDK changed how many controls there are");

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

// A whole IBStream, read into memory. A saved blob has no fixed length - G2 Alike's carries a patch
// path, GenBridge's a table of devices - and a single fixed-size read would silently truncate a long
// one into something that does not parse.
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

// ------------------------------------------------------------------------------------------------
// Which processor is mine
// ------------------------------------------------------------------------------------------------

// notes §2

class SynthLibProcessor;
class SynthLibController;

struct tLiveInstance {
    int64                       serial;
    const tSynthLibPluginDesc * desc;
    void *                      inst;
    SynthLibProcessor *         processor;
};

static std::mutex                        gRegistryLock;
static std::vector<tLiveInstance>        gInstances;
static std::vector<SynthLibController *> gControllers;
static int64                             gNextSerial = 1;

// THE BLOCK'S MIDI OUTPUT, for synthlib_plugin_midi_out(): set for the length of each process() call
// on the thread making it, so a plug-in's call finds the host's list without the registry lock.
struct tMidiOut {
    void *       inst;
    IEventList * list;
};

static thread_local tMidiOut gMidiOut = { nullptr, nullptr };

struct tMidiOutScope {
    tMidiOutScope(void * inst, IEventList * list) {
        gMidiOut = { inst, list };
    }

    ~tMidiOutScope() {
        gMidiOut = { nullptr, nullptr };
    }
};

static int64 register_instance(const tSynthLibPluginDesc * d, void * inst, SynthLibProcessor * p) {
    std::lock_guard<std::mutex> lock(gRegistryLock);
    int64                       serial = gNextSerial++;

    gInstances.push_back({ serial, d, inst, p });
    return serial;
}

static void unregister_instance(int64 serial) {
    std::lock_guard<std::mutex> lock(gRegistryLock);

    for (size_t i = 0; i < gInstances.size(); i++) {
        if (gInstances[i].serial == serial) {
            gInstances.erase(gInstances.begin() + (long)i);
            return;
        }
    }
}

// The two lookups a controller makes, with the lock already held by the caller.
static void * instance_for_serial_locked(int64 serial) {
    for (const tLiveInstance & live : gInstances) {
        if (live.serial == serial) {
            return live.inst;
        }
    }
    return nullptr;
}

static void * sole_instance_locked(const tSynthLibPluginDesc * d) {
    void * found = nullptr;
    int    count = 0;

    for (const tLiveInstance & live : gInstances) {
        if (live.desc == d) {
            found = live.inst;
            count++;
        }
    }
    return (count == 1) ? found : nullptr;
}

static SynthLibProcessor * processor_for_locked(void * inst) {
    for (const tLiveInstance & live : gInstances) {
        if (live.inst == inst) {
            return live.processor;
        }
    }
    return nullptr;
}

// ------------------------------------------------------------------------------------------------
// The processor
// ------------------------------------------------------------------------------------------------

class SynthLibProcessor : public IComponent,
                          public IAudioProcessor,
                          public IConnectionPoint,
                          public IProcessContextRequirements {
public:
    explicit SynthLibProcessor(const tSynthLibPluginDesc * descriptor)
        : refCount(1), desc(descriptor), sampleRate(44100.0) {
        inst = (desc->cb.create != nullptr) ? desc->cb.create(desc) : nullptr;

        // notes §3
        synthlib_params_init(&params, desc, inst);

        if (inst != nullptr) {
            serial = register_instance(desc, inst, this);
        }
    }

    virtual ~SynthLibProcessor(void) {
        // OUT OF THE TABLE BEFORE IT IS DESTROYED, so nothing resolves an instance that is going.
        if (serial != 0) {
            unregister_instance(serial);
        }

        if ((inst != nullptr) && (desc->cb.destroy != nullptr)) {
            desc->cb.destroy(inst);
        }
        inst = nullptr;
        synthlib_params_free(&params);

        if (hostApp != nullptr) {
            hostApp->release();
            hostApp = nullptr;
        }
    }

    double param_value(uint32_t id) const {
        return synthlib_params_get(&params, id);
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
        return IProcessContextRequirements::kNeedSystemTime |
               IProcessContextRequirements::kNeedContinousTimeSamples |
               IProcessContextRequirements::kNeedProjectTimeMusic |
               IProcessContextRequirements::kNeedBarPositionMusic |
               IProcessContextRequirements::kNeedCycleMusic |
               IProcessContextRequirements::kNeedTempo |
               IProcessContextRequirements::kNeedTimeSignature |
               IProcessContextRequirements::kNeedTransportState;
    }

    // notes §4
    tresult PLUGIN_API connect(IConnectionPoint * other) SMTG_OVERRIDE {
        peer = other;
        announce();
        return kResultOk;
    }

    tresult PLUGIN_API disconnect(IConnectionPoint * other) SMTG_OVERRIDE {
        (void)other;
        peer = nullptr;
        return kResultOk;
    }

    tresult PLUGIN_API notify(IMessage * message) SMTG_OVERRIDE {
        if ((message == nullptr) || (inst == nullptr) || (desc->cb.message == nullptr)) {
            return kResultOk;
        }
        const char * id = message->getMessageID();

        if ((id == nullptr) || (strncmp(id, WRAPPER_MESSAGE, strlen(WRAPPER_MESSAGE)) == 0)) {
            return kResultOk;
        }
        int64 value = 0;

        if (message->getAttributes()->getInt("value", value) != kResultOk) {
            value = 0;
        }
        desc->cb.message(inst, id, (int64_t)value);
        return kResultOk;
    }

    // ---- IPluginBase / IComponent --------------------------------------------------------------
    tresult PLUGIN_API initialize(FUnknown * context) SMTG_OVERRIDE {
        if (inst == nullptr) {
            return kResultFalse;
        }

        // KEPT, because IMessage can only be allocated through it - see post_message().
        if ((context != nullptr) && (hostApp == nullptr)) {
            context->queryInterface(IHostApplication::iid, (void **)&hostApp);
        }

        if (desc->cb.initialize != nullptr) {
            desc->cb.initialize(inst);
        }
        announce();
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

        if ((type == kEvent) && (dir == kOutput) && (desc->wantsMidiOut == true)) {
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
            // SIXTEEN CHANNELS, because a plug-in passing MIDI through to hardware plays it on the
            // channel it arrived on - and a host that was told one routes everything to channel 1.
            bus.mediaType    = kEvent;
            bus.direction    = kInput;
            bus.channelCount = 16;
            bus.busType      = kMain;
            bus.flags        = BusInfo::kDefaultActive;
            copy_name(bus.name, "MIDI In");
            return kResultOk;
        }

        if ((type == kEvent) && (dir == kOutput) && (desc->wantsMidiOut == true) && (index == 0)) {
            bus.mediaType    = kEvent;
            bus.direction    = kOutput;
            bus.channelCount = 16;
            bus.busType      = kMain;
            bus.flags        = BusInfo::kDefaultActive;
            copy_name(bus.name, "MIDI Out");
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
        size_t               need = synthlib_state_write(desc, inst, &params, nullptr, 0);
        std::vector<uint8_t> blob(need);
        size_t               wrote = synthlib_state_write(desc, inst, &params,
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

    // Asked when we activate, and cached until the controller tells the host to ask again - see
    // synthlib_plugin_latency_changed().
    uint32 PLUGIN_API getLatencySamples(void) SMTG_OVERRIDE {
        if ((inst == nullptr) || (desc->cb.latencySamples == nullptr)) {
            return 0;
        }
        return desc->cb.latencySamples(inst);
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup & setup) SMTG_OVERRIDE {
        sampleRate = setup.sampleRate;

        if (inst == nullptr) {
            return kResultOk;
        }

        if (desc->cb.setSampleRate != nullptr) {
            desc->cb.setSampleRate(inst, sampleRate);
        }

        if (desc->cb.prepare != nullptr) {
            tSynthLibSetup s;

            s.sampleRate = setup.sampleRate;
            s.maxFrames  = (setup.maxSamplesPerBlock > 0) ? (uint32_t)setup.maxSamplesPerBlock : 0u;
            s.offline    = (setup.processMode == kOffline);
            desc->cb.prepare(inst, &s);
        }
        return kResultOk;
    }

    tresult PLUGIN_API setProcessing(TBool state) SMTG_OVERRIDE {
        if (inst == nullptr) {
            return kResultOk;
        }

        if (desc->cb.setProcessing != nullptr) {
            desc->cb.setProcessing(inst, (state != 0));
        } else if ((state != 0) && (desc->cb.reset != nullptr)) {
            // Going from stopped to running is where a host expects held notes to have gone. Reset
            // on the way IN rather than on the way out, so a transport jump starts clean.
            desc->cb.reset(inst);
        }
        return kResultOk;
    }

    uint32 PLUGIN_API getTailSamples(void) SMTG_OVERRIDE {
        // notes §5
        return kInfiniteTail;
    }

    tresult PLUGIN_API process(ProcessData & data) SMTG_OVERRIDE {
        if (inst == nullptr) {
            return kResultOk;
        }
        tMidiOutScope      midiOut(inst, (desc->wantsMidiOut == true) ? data.outputEvents : nullptr);
        tSynthLibTransport transport;

        fill_transport(data.processContext, transport);

        // FIRST, so every parameter change and note below lands in a block the plug-in already knows
        // the position of. numSamples may be 0 here: a host flushing parameter changes while stopped
        // calls process() with no audio at all.
        if (desc->cb.blockBegin != nullptr) {
            desc->cb.blockBegin(inst, (data.numSamples > 0) ? (uint32_t)data.numSamples : 0u, &transport);
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

        if ((data.numInputs > 0) && (data.inputs != nullptr) &&
            (data.inputs[0].channelBuffers32 != nullptr) && (data.inputs[0].numChannels > 0)) {
            in    = (const float * const *)data.inputs[0].channelBuffers32;
            numIn = (uint32_t)data.inputs[0].numChannels;

            for (uint32_t c = 0; c < numIn; c++) {
                if (in[c] == nullptr) {
                    in    = nullptr;
                    numIn = 0;
                    break;
                }
            }
        }
        float ** out    = nullptr;
        uint32_t numOut = 0;

        if ((data.numOutputs > 0) && (data.outputs != nullptr) &&
            (data.outputs[0].channelBuffers32 != nullptr)) {
            out    = data.outputs[0].channelBuffers32;
            numOut = (uint32_t)data.outputs[0].numChannels;

            for (uint32_t c = 0; c < numOut; c++) {
                if (out[c] == nullptr) {
                    return kResultOk;
                }
            }
        }
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
    void announce(void) {
        if ((serial != 0) && (peer != nullptr)) {
            post_message(BIND_MESSAGE, serial);
        }
    }

    void fill_transport(ProcessContext * ctx, tSynthLibTransport & out) {
        memset(&out, 0, sizeof(out));
        out.sampleRate = sampleRate;

        if (ctx == nullptr) {
            return;         // valid stays false, which is the whole point of the flag
        }
        // notes §6
        out.valid                 = true;
        out.playing               = ((ctx->state & ProcessContext::kPlaying) != 0);
        out.recording             = ((ctx->state & ProcessContext::kRecording) != 0);
        out.cycleActive           = ((ctx->state & ProcessContext::kCycleActive) != 0);
        out.tempoValid            = ((ctx->state & ProcessContext::kTempoValid) != 0);
        out.tempo                 = ctx->tempo;
        out.musicTimeValid        = ((ctx->state & ProcessContext::kProjectTimeMusicValid) != 0);
        out.projectTimeMusic      = ctx->projectTimeMusic;
        out.barPositionValid      = ((ctx->state & ProcessContext::kBarPositionValid) != 0);
        out.barPositionMusic      = ctx->barPositionMusic;
        out.cycleValid            = ((ctx->state & ProcessContext::kCycleValid) != 0);
        out.cycleStartMusic       = ctx->cycleStartMusic;
        out.cycleEndMusic         = ctx->cycleEndMusic;
        out.timeSigValid          = ((ctx->state & ProcessContext::kTimeSigValid) != 0);
        out.timeSigNumerator      = ctx->timeSigNumerator;
        out.timeSigDenominator    = ctx->timeSigDenominator;
        out.systemTimeValid       = ((ctx->state & ProcessContext::kSystemTimeValid) != 0);
        out.systemTime            = (uint64_t)ctx->systemTime;
        out.projectTimeSamples    = (int64_t)ctx->projectTimeSamples;
        out.continuousTimeValid   = ((ctx->state & ProcessContext::kContTimeValid) != 0);
        out.continuousTimeSamples = (int64_t)ctx->continousTimeSamples;

        if (ctx->sampleRate > 0.0) {
            out.sampleRate = ctx->sampleRate;
        }
    }

    void apply_automation(ProcessData & data) {
        if (data.inputParameterChanges == nullptr) {
            return;
        }
        int32 queues = data.inputParameterChanges->getParameterCount();

        for (int32 q = 0; q < queues; q++) {
            IParamValueQueue * queue = data.inputParameterChanges->getParameterData(q);

            if (queue == nullptr) {
                continue;
            }
            uint32_t id     = (uint32_t)queue->getParameterId();
            int32    points = queue->getPointCount();

            if ((points <= 0) || (synthlib_params_index(&params, id) < 0)) {
                continue;
            }

            // WITHOUT paramPoints() ONLY THE LAST POINT IS TAKEN: an engine with no notion of a
            // parameter moving within a block would only be handed a resolution it cannot use.
            if (desc->cb.paramPoints == nullptr) {
                int32      offset = 0;
                ParamValue value  = 0.0;

                if (queue->getPoint(points - 1, offset, value) == kResultOk) {
                    apply_param(id, value);
                }
                continue;
            }
            tSynthLibParamPoint list[MAX_POINTS];
            uint32_t            count = 0;

            for (int32 k = 0; k < points; k++) {
                int32      offset = 0;
                ParamValue value  = 0.0;

                if (queue->getPoint(k, offset, value) != kResultOk) {
                    continue;
                }

                // FULL: the last slot is overwritten by every later point, so it ends up holding the
                // final one - which is the one a level settles at.
                if (count == MAX_POINTS) {
                    count = MAX_POINTS - 1;
                }
                list[count].sampleOffset = (offset > 0) ? (uint32_t)offset : 0u;
                list[count].value        = synthlib_param_clamp(value);
                count++;
            }

            if (count == 0u) {
                continue;
            }
            synthlib_params_set(&params, id, list[count - 1].value);
            desc->cb.paramPoints(inst, id, list, count);
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
            uint32_t offset = (e.sampleOffset > 0) ? (uint32_t)e.sampleOffset : 0u;

            if (e.type == Event::kNoteOnEvent) {
                uint8_t channel = (uint8_t)(e.noteOn.channel & 0x0F);
                uint8_t note    = (uint8_t)(e.noteOn.pitch & 0x7F);

                // A note-on at zero velocity is a note-off, as it is over MIDI.
                if ((e.noteOn.velocity > 0.0f) && (desc->cb.noteOn != nullptr)) {
                    desc->cb.noteOn(inst, channel, note, e.noteOn.velocity, offset);
                } else if (desc->cb.noteOff != nullptr) {
                    desc->cb.noteOff(inst, channel, note, 0.0f, offset);
                }
            } else if (e.type == Event::kNoteOffEvent) {
                if (desc->cb.noteOff != nullptr) {
                    desc->cb.noteOff(inst, (uint8_t)(e.noteOff.channel & 0x0F),
                                     (uint8_t)(e.noteOff.pitch & 0x7F), e.noteOff.velocity, offset);
                }
            } else if (e.type == Event::kPolyPressureEvent) {
                // POLYPHONIC key pressure, which arrives as an EVENT and not as a parameter change.
                // IMidiMapping's kAfterTouch is CHANNEL pressure (0xD0) only, so a keyboard sending
                // poly pressure (0xA0) - and plenty do - reaches a plug-in by this path or not at all.
                if (desc->cb.polyPressure != nullptr) {
                    desc->cb.polyPressure(inst, (uint8_t)(e.polyPressure.channel & 0x0F),
                                          (uint8_t)(e.polyPressure.pitch & 0x7F),
                                          e.polyPressure.pressure, offset);
                }
            }
        }
    }

    void apply_param(uint32_t id, ParamValue value) {
        double clamped = synthlib_param_clamp(value);

        if (synthlib_params_set(&params, id, clamped) == false) {
            return;
        }

        if ((inst != nullptr) && (desc->cb.setParam != nullptr)) {
            desc->cb.setParam(inst, id, clamped);
        }
    }

    void apply_state(const std::vector<uint8_t> & blob) {
        std::vector<tSynthLibParamValue> values((params.count > 0u) ? params.count : 1u);
        uint32_t                         count      = 0;
        const void *                     pluginData = nullptr;
        size_t                           pluginLen  = 0;

        if (synthlib_state_read(&params, blob.empty() ? nullptr : blob.data(), blob.size(),
                                values.data(), params.count, &count, &pluginData, &pluginLen) == false) {
            return;
        }

        for (uint32_t i = 0; i < count; i++) {
            apply_param(values[i].id, values[i].value);
        }

        // THE PLUG-IN'S OWN STATE LAST. For G2 Alike that is the patch path, and loading a patch
        // rebuilds the parameter snapshot - so the parameters have to already be where the project
        // left them when it happens, or the first block plays the patch at its defaults.
        if ((inst != nullptr) && (desc->cb.setState != nullptr)) {
            desc->cb.setState(inst, pluginData, pluginLen);
        }

        // AND THEN READ BACK, because the plug-in's own bytes may have set parameters the wrapper
        // never saved - every NO_SAVE one, and all of them in a project from before the wrapper
        // saved any. Without this the stored copy goes on reporting defaults the plug-in is not at.
        if ((inst != nullptr) && (desc->cb.getParam != nullptr)) {
            for (uint32_t i = 0; i < params.count; i++) {
                synthlib_params_set(&params, params.ids[i], desc->cb.getParam(inst, params.ids[i]));
            }
        }
    }

    std::atomic<int32>          refCount;
    const tSynthLibPluginDesc * desc;
    void *                      inst   = nullptr;
    int64                       serial = 0;
    tSynthLibParamStore         params;
    double                      sampleRate;
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
        // WITH NO INSTANCE, deliberately: which processor this controller belongs to is not known
        // until the host connects the two, and the parameter list cannot depend on it - see the
        // note on paramCount() in synthlibPlugin.h.
        synthlib_params_init(&params, desc, nullptr);

        std::lock_guard<std::mutex> lock(gRegistryLock);

        gControllers.push_back(this);
    }

    virtual ~SynthLibController(void) {
        {
            std::lock_guard<std::mutex> lock(gRegistryLock);

            for (size_t i = 0; i < gControllers.size(); i++) {
                if (gControllers[i] == this) {
                    gControllers.erase(gControllers.begin() + (long)i);
                    break;
                }
            }
        }
        synthlib_params_free(&params);
    }

    // THE PROCESSOR'S INSTANCE, if there is one this controller can be sure is its own. The caller
    // holds gRegistryLock.
    void * instance_locked(void) const {
        int64 bound = boundSerial.load();

        if (bound != 0) {
            return instance_for_serial_locked(bound);
        }
        return sole_instance_locked(desc);
    }

    void * instance(void) const {
        std::lock_guard<std::mutex> lock(gRegistryLock);

        return instance_locked();
    }

    double param_value(uint32_t id) const {
        return synthlib_params_get(&params, id);
    }

    // BEGIN, PERFORM, END, as one gesture. That is what lets a host record the move as automation
    // and show the parameter as touched, rather than seeing a value appear from nowhere. Main thread.
    void report_edit(uint32_t id, double normalized) {
        if (componentHandler != nullptr) {
            componentHandler->beginEdit((ParamID)id);
            componentHandler->performEdit((ParamID)id, normalized);
            componentHandler->endEdit((ParamID)id);
        }
        setParamNormalized((ParamID)id, normalized);
    }

    // ONLY THE CONTROLLER CAN SAY THIS. restartComponent() lives on IComponentHandler, which a
    // processor never sees - which is why a latency worked out inside the processor has to come
    // round to here before any host will hear of it. Main thread.
    void report_latency_changed(void) {
        if (componentHandler != nullptr) {
            componentHandler->restartComponent(kLatencyChanged);
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
    tresult PLUGIN_API connect(IConnectionPoint * other) SMTG_OVERRIDE {
        peer = other;
        return kResultOk;
    }

    tresult PLUGIN_API disconnect(IConnectionPoint * other) SMTG_OVERRIDE {
        (void)other;
        peer = nullptr;
        boundSerial.store(0);
        return kResultOk;
    }

    // THE PROCESSOR TELLING US WHICH ONE IT IS, and otherwise the plug-in talking to itself across the
    // split - both halves share one instance, so a message the plug-in sent from its processor side
    // arrives here and is handed back to that same instance, now on this side of the connection.
    tresult PLUGIN_API notify(IMessage * message) SMTG_OVERRIDE {
        if (message == nullptr) {
            return kInvalidArgument;
        }
        const char * id    = message->getMessageID();
        int64        value = 0;

        if (id == nullptr) {
            return kResultOk;
        }

        if (message->getAttributes()->getInt("value", value) != kResultOk) {
            value = 0;
        }

        if (strcmp(id, BIND_MESSAGE) == 0) {
            boundSerial.store(value);
            return kResultOk;
        }

        if ((strncmp(id, WRAPPER_MESSAGE, strlen(WRAPPER_MESSAGE)) == 0) ||
            (desc->cb.message == nullptr)) {
            return kResultOk;
        }
        void * inst = instance();

        if (inst != nullptr) {
            desc->cb.message(inst, id, (int64_t)value);
        }
        return kResultOk;
    }

    // notes §7
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 channel,
                                                   CtrlNumber midiControllerNumber,
                                                   ParamID & id) SMTG_OVERRIDE {
        if ((busIndex != 0) || (midiControllerNumber < 0)) {
            return kResultFalse;
        }

        if (desc->cb.midiMapping != nullptr) {
            uint32_t mapped = 0;

            if ((desc->cb.midiMapping(desc, instance(), (uint8_t)(channel & 0x0F),
                                      (int16_t)midiControllerNumber, &mapped) == true) &&
                (synthlib_params_index(&params, mapped) >= 0)) {
                id = (ParamID)mapped;
                return kResultTrue;
            }
            return kResultFalse;
        }
        void * inst = instance();

        for (uint32_t i = 0; i < params.count; i++) {
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

    // THE HOST HANDS THE CONTROLLER THE PROCESSOR'S SAVED STATE so the panel comes up showing what
    // was loaded. The wrapper's own parameters are read straight out of it; anything the plug-in
    // keeps in its own bytes, only the plug-in can read - see stateParams().
    tresult PLUGIN_API setComponentState(IBStream * state) SMTG_OVERRIDE {
        if (state == nullptr) {
            return kResultOk;
        }
        std::vector<uint8_t>             blob = read_stream(state);
        std::vector<tSynthLibParamValue> values((params.count > 0u) ? params.count : 1u);
        uint32_t                         count      = 0;
        const void *                     pluginData = nullptr;
        size_t                           pluginLen  = 0;

        if (synthlib_state_read(&params, blob.empty() ? nullptr : blob.data(), blob.size(),
                                values.data(), params.count, &count, &pluginData, &pluginLen) == false) {
            return kResultOk;
        }

        for (uint32_t i = 0; i < count; i++) {
            synthlib_params_set(&params, values[i].id, values[i].value);
        }

        if ((desc->cb.stateParams != nullptr) && (pluginData != nullptr) && (pluginLen > 0u)) {
            count = desc->cb.stateParams(desc, pluginData, pluginLen, values.data(), params.count);

            for (uint32_t i = 0; (i < count) && (i < params.count); i++) {
                synthlib_params_set(&params, values[i].id, values[i].value);
            }
        }
        return kResultOk;
    }

    // notes §8
    tresult PLUGIN_API setState(IBStream * state) SMTG_OVERRIDE {
        if (state == nullptr) {
            return kResultFalse;
        }
        std::vector<uint8_t> blob = read_stream(state);
        std::string          text(blob.begin(), blob.end());
        size_t               at = text.find("editor=");
        double               width = 0.0;

        // SANITY-CHECKED, not trusted. This comes out of a project file, and a size the user cannot
        // see or cannot fit on a screen would have no way back short of editing the project.
        if ((at != std::string::npos) && (sscanf(text.c_str() + at, "editor=%lf", &width) == 1) &&
            (width >= desc->editorMinWidth) &&
            (width <= ((desc->editorMaxWidth > 0.0) ? desc->editorMaxWidth : 16384.0))) {
            editorWidth = width;
        }
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream * state) SMTG_OVERRIDE {
        if ((state == nullptr) || (desc->editorDefaultWidth <= 0.0)) {
            return (state == nullptr) ? kResultFalse : kResultOk;
        }
        double width  = current_editor_width();
        double height = (desc->editorAspect > 0.0) ? (width / desc->editorAspect) : width;
        char   text[96];
        int    len     = snprintf(text, sizeof(text), "SYNTHLIBGUI1\neditor=%.0f,%.0f\n", width, height);
        int32  written = 0;

        return (state->write(text, (int32)len, &written) == kResultOk) ? kResultOk : kResultFalse;
    }

    int32 PLUGIN_API getParameterCount(void) SMTG_OVERRIDE {
        return (int32)params.count;
    }

    tresult PLUGIN_API getParameterInfo(int32 paramIndex, ParameterInfo & info) SMTG_OVERRIDE {
        tSynthLibParamDesc p;

        // ZEROED FIRST, EVEN ON THE FAILING PATH. A caller has no business reading info after a
        // refusal, and one that does should see nothing rather than the last parameter's details,
        // which is what an untouched struct reused round a loop would show it.
        memset(&info, 0, sizeof(info));

        if ((paramIndex < 0) || ((uint32_t)paramIndex >= params.count) ||
            (synthlib_param_describe(desc, instance(), (uint32_t)paramIndex, &p) == false)) {
            return kResultFalse;
        }
        info.id                     = (ParamID)p.id;
        info.stepCount              = p.stepCount;
        info.unitId                 = 0;                    // kRootUnitId
        info.defaultNormalizedValue = p.defaultNormalized;

        if ((p.flags & SYNTHLIB_PARAM_HIDDEN) != 0u) {
            info.flags = ParameterInfo::kIsHidden;
        } else {
            info.flags = ParameterInfo::kCanAutomate |
                         (((p.flags & SYNTHLIB_PARAM_LIST) != 0u) ? ParameterInfo::kIsList : 0);
        }
        copy_name(info.title, p.title);
        copy_name(info.shortTitle, (p.shortTitle[0] != '\0') ? p.shortTitle : p.title);
        copy_name(info.units, synthlib_param_units(p.unit));
        return kResultOk;
    }

    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue valueNormalized,
                                             String128 string) SMTG_OVERRIDE {
        tSynthLibParamDesc p;
        void *             inst = instance();
        char               text[128];

        if ((synthlib_params_describe(&params, desc, inst, (uint32_t)id, &p) == false) ||
            (synthlib_param_text(desc, inst, &p, valueNormalized, text, sizeof(text)) == false)) {
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

        if (synthlib_params_describe(&params, desc, instance(), (uint32_t)id, &p) == false) {
            return valueNormalized;
        }
        return synthlib_param_to_plain(&p, valueNormalized);
    }

    ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue plainValue) SMTG_OVERRIDE {
        tSynthLibParamDesc p;

        if (synthlib_params_describe(&params, desc, instance(), (uint32_t)id, &p) == false) {
            return plainValue;
        }
        return synthlib_param_to_normalized(&p, plainValue);
    }

    ParamValue PLUGIN_API getParamNormalized(ParamID id) SMTG_OVERRIDE {
        return synthlib_params_get(&params, (uint32_t)id);
    }

    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) SMTG_OVERRIDE {
        double clamped = synthlib_param_clamp(value);

        if (synthlib_params_set(&params, (uint32_t)id, clamped) == false) {
            return kResultFalse;
        }

        // AND STRAIGHT INTO THE INSTANCE TOO, for a plug-in that asked - see controllerAppliesParams.
        // With a host that routes properly this is the same value the processor is about to be given
        // anyway; with one that does not, it is the only way a move on the host's own panel is heard.
        if ((desc->controllerAppliesParams == true) && (desc->cb.setParam != nullptr)) {
            void * inst = instance();

            if (inst != nullptr) {
                desc->cb.setParam(inst, (uint32_t)id, clamped);
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API setComponentHandler(IComponentHandler * handler) SMTG_OVERRIDE {
        componentHandler = handler;
        return kResultOk;
    }

    IPlugView * PLUGIN_API createView(FIDString name) SMTG_OVERRIDE {
        if ((name == nullptr) || (strcmp(name, ViewType::kEditor) != 0)) {
            return nullptr;
        }

        if ((desc->cb.createView == nullptr) || (desc->editorDefaultWidth <= 0.0)) {
            return nullptr;         // no editor; the host draws its own panel from the parameters
        }
        return synthlib_vst3_create_view(desc, instance(), (IEditController *)this, editorWidth,
                                         &editorWidth);
    }

    static FUnknown * createInstance(const tSynthLibPluginDesc * d) {
        return (IEditController *)new SynthLibController(d);
    }

private:
    // What this project's editor is, or would open at: its own restored width, else the machine's
    // remembered one, else the default.
    double current_editor_width(void) const {
        if (editorWidth > 0.0) {
            return editorWidth;
        }

        if (desc->editorWidthLoad != nullptr) {
            double saved = (double)desc->editorWidthLoad();

            if ((saved >= desc->editorMinWidth) &&
                ((desc->editorMaxWidth <= 0.0) || (saved <= desc->editorMaxWidth))) {
                return saved;
            }
        }
        return desc->editorDefaultWidth;
    }

    std::atomic<int32>          refCount;
    const tSynthLibPluginDesc * desc;
    tSynthLibParamStore         params;
    IComponentHandler *         componentHandler = nullptr;
    IConnectionPoint *          peer             = nullptr;

    // The serial the processor announced, or 0 before it has - see "Which processor is mine".
    std::atomic<int64>          boundSerial{0};

    // This project's editor width, restored by setState() and kept current by the open view; 0 until
    // either has said anything. Main thread only, like both of them.
    double                      editorWidth = 0.0;
};

// ------------------------------------------------------------------------------------------------
// What the plug-in may ask of us
// ------------------------------------------------------------------------------------------------

// notes §9
static std::vector<SynthLibController *> controllers_of(void * inst) {
    std::vector<SynthLibController *> found;
    std::lock_guard<std::mutex>       lock(gRegistryLock);

    for (SynthLibController * c : gControllers) {
        if ((inst != nullptr) && (c->instance_locked() == inst)) {
            found.push_back(c);
        }
    }
    return found;
}

typedef struct {
    void *   inst;
    uint32_t id;
    double   value;
    bool     latency;
} tHostPost;

// ON THE MAIN THREAD, and possibly some time after the plug-in asked - so the instance is found
// again here, from scratch. If it has gone in the meantime there is simply nobody left to tell.
static void deliver_to_host(void * ctx) {
    tHostPost * post = (tHostPost *)ctx;

    for (SynthLibController * c : controllers_of(post->inst)) {
        if (post->latency == true) {
            c->report_latency_changed();
        } else {
            c->report_edit(post->id, post->value);
        }
    }
    delete post;
}

void synthlib_plugin_param_edited(void * inst, uint32_t id, double normalized) {
    if (inst == nullptr) {
        return;
    }
    synthlib_run_on_main(deliver_to_host,
                         new tHostPost{ inst, id, synthlib_param_clamp(normalized), false });
}

void synthlib_plugin_latency_changed(void * inst) {
    if (inst == nullptr) {
        return;
    }
    synthlib_run_on_main(deliver_to_host, new tHostPost{ inst, 0u, 0.0, true });
}

double synthlib_plugin_param_value(void * inst, uint32_t id) {
    // THE CONTROLLER'S COPY when there is one, because that is the value the host's own panel shows;
    // the processor's otherwise, which is what a host that never connected the halves still updates.
    std::vector<SynthLibController *> owners = controllers_of(inst);

    if (owners.empty() == false) {
        return owners[0]->param_value(id);
    }
    std::lock_guard<std::mutex> lock(gRegistryLock);
    SynthLibProcessor *         p = processor_for_locked(inst);

    return (p != nullptr) ? p->param_value(id) : 0.0;
}

// NOT AFTER terminate(). The processor is looked up under the lock and used outside it, which is safe
// because a plug-in stops everything that sends before the host is allowed to destroy it.
bool synthlib_plugin_midi_out(void * inst, uint8_t status, uint8_t data1, uint8_t data2, uint32_t sampleOffset) {
    if ((inst == nullptr) || (gMidiOut.inst != inst) || (gMidiOut.list == nullptr)) {
        return false;
    }
    Event   e       = {};
    uint8_t kind    = (uint8_t)(status & 0xF0);
    int16   channel = (int16)(status & 0x0F);

    e.busIndex     = 0;
    e.sampleOffset = (int32)sampleOffset;
    e.flags        = Event::kIsLive;

    if ((kind == 0x90) && (data2 > 0)) {
        e.type             = Event::kNoteOnEvent;
        e.noteOn.channel   = channel;
        e.noteOn.pitch     = (int16)(data1 & 0x7F);
        e.noteOn.velocity  = (float)data2 / 127.0f;
        e.noteOn.noteId    = -1;
    } else if ((kind == 0x80) || (kind == 0x90)) {      // a note-on at velocity 0 is a note-off
        e.type             = Event::kNoteOffEvent;
        e.noteOff.channel  = channel;
        e.noteOff.pitch    = (int16)(data1 & 0x7F);
        e.noteOff.velocity = (kind == 0x80) ? ((float)data2 / 127.0f) : 0.0f;
        e.noteOff.noteId   = -1;
    } else if (kind == 0xA0) {
        e.type                  = Event::kPolyPressureEvent;
        e.polyPressure.channel  = channel;
        e.polyPressure.pitch    = (int16)(data1 & 0x7F);
        e.polyPressure.pressure = (float)data2 / 127.0f;
        e.polyPressure.noteId   = -1;
    } else if ((kind == 0xB0) || (kind == 0xD0) || (kind == 0xE0)) {
        e.type                    = Event::kLegacyMIDICCOutEvent;
        e.midiCCOut.channel       = (int8)channel;
        e.midiCCOut.controlNumber = (kind == 0xB0) ? (uint8)(data1 & 0x7F) : ((kind == 0xD0) ? (uint8)kAfterTouch : (uint8)kPitchBend);
        e.midiCCOut.value         = (int8)((kind == 0xB0) ? data2 : data1);
        e.midiCCOut.value2        = (int8)((kind == 0xE0) ? data2 : 0);
    } else {
        return false;
    }
    return gMidiOut.list->addEvent(e) == kResultOk;
}

bool synthlib_plugin_send_message(void * inst, const char * id, int64_t value) {
    SynthLibProcessor * p = nullptr;

    {
        std::lock_guard<std::mutex> lock(gRegistryLock);

        p = processor_for_locked(inst);
    }
    return (p != nullptr) ? p->post_message(id, value) : false;
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
