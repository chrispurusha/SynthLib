/*
 * SynthLib - the Audio Unit wrapper, written once for every plug-in in these projects.
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
// Notes: Docs/code-notes/synthlibPluginAu.c.md - "// notes §k" refers there.

// notes §1

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/HostTime.h>
#include <CoreFoundation/CoreFoundation.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "synthlibPlugin.h"
#include "synthlibPluginAu.h"
#include "synthlibPluginState.h"

#define MAX_LISTENERS        (32)
#define MAX_RENDER_NOTIFY    (8)
#define MAX_INSTANCES        (64)

// A host must tell us its slice size before rendering, but auval and a few hosts render before
// setting it. This is what MaximumFramesPerSlice reports until then.
#define DEFAULT_MAX_FRAMES   (4096)

// Events that arrived between one render and the next, waiting for the render they belong to. Sized
// for a burst - a chord, a controller sweep, a host scheduling automation ahead - not for a backlog:
// every render empties it.
#define EVENT_QUEUE_SIZE     (1024)

// How many consecutive points for one parameter go to paramPoints() in one call - see the VST3
// wrapper's MAX_POINTS, which this matches.
#define MAX_POINTS           (64)

typedef struct {
    AudioUnitPropertyID          id;
    AudioUnitPropertyListenerProc proc;
    void *                       userData;
} tPropertyListener;

typedef struct {
    AURenderCallback proc;
    void *           userData;
} tRenderNotify;

// ONE THING THAT HAPPENED BEFORE A RENDER, to be delivered inside it: a MIDI message, or a scheduled
// parameter value.
typedef struct {
    bool     isParam;
    uint8_t  status;
    uint8_t  data1;
    uint8_t  data2;
    uint32_t id;
    double   value;             // normalized
    uint32_t offset;            // frames into the render it belongs to
} tQueuedEvent;

typedef struct {
    // FIRST, AND IT MUST STAY FIRST. The pointer the factory returns is the address of this member,
    // and every method the host calls is handed that same pointer back as `self`.
    AudioComponentPlugInInterface iface;

    AudioComponentInstance ci;
    const tSynthLibPluginDesc * desc;
    void *                 inst;

    // How a host feeds an EFFECT. Either it hands over a callback to pull from, or it connects
    // another unit's output to our input; both are stored and neither is assumed.
    AURenderCallbackStruct inputCallback;
    bool                   haveInputCallback;
    AudioUnit              inputSourceUnit;
    UInt32                 inputSourceBus;

    // The host's transport, such as it is. See tSynthLibTransport: an Audio Unit host installs
    // these or does not, and answers from whatever it feels like rather than from the block being
    // rendered - which is why `valid` is false far more often here than on the VST3 side.
    HostCallbackInfo       hostCallbacks;
    bool                   haveHostCallbacks;

    // The buffer an input is pulled into, and the AudioBufferList that describes it.
    float *                inputSamples;
    UInt32                 inputFrames;
    AudioBufferList *      inputList;
    const float **         inputChannels;

    double                 sampleRate;
    UInt32                 maxFrames;
    bool                   offline;
    bool                   bypassed;        // an effect's input handed straight through - see au_render()
    bool                   initialized;

    tSynthLibParamStore    params;

    tPropertyListener      listeners[MAX_LISTENERS];
    UInt32                 listenerCount;

    tRenderNotify          renderNotify[MAX_RENDER_NOTIFY];
    UInt32                 renderNotifyCount;

    OSStatus               lastRenderError;

    // Channel pointers handed to the plug-in's render(), and the buffer used when the host asks us
    // to supply our own - see au_render().
    float **               channels;
    float *                ownedSamples;
    UInt32                 ownedFrames;

    AUPreset               currentPreset;

    // THE EVENT QUEUE. One consumer, the render; producers are whoever the host calls MIDIEvent and
    // ScheduleParameters from, usually the render thread itself and occasionally a MIDI or UI thread
    // - so producers take a spin flag against EACH OTHER and the render never waits on anything.
    tQueuedEvent           events[EVENT_QUEUE_SIZE];
    _Atomic uint32_t       eventWrite;
    _Atomic uint32_t       eventRead;
    atomic_flag            eventProducer;
} tSynthLibAu;

// ------------------------------------------------------------------------------------------------
// The live instances
// ------------------------------------------------------------------------------------------------

// For the calls a plug-in makes back into the wrapper, which name an instance - see the foot of this
// file. Replaces a single "sole instance" pointer that went NULL the moment a second copy loaded, so
// that with two tracks neither editor's moves reached its host.
static pthread_mutex_t gInstanceLock = PTHREAD_MUTEX_INITIALIZER;
static tSynthLibAu *   gInstances[MAX_INSTANCES];

static void register_au(tSynthLibAu * au) {
    pthread_mutex_lock(&gInstanceLock);

    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (gInstances[i] == NULL) {
            gInstances[i] = au;
            break;
        }
    }
    pthread_mutex_unlock(&gInstanceLock);
}

static void unregister_au(tSynthLibAu * au) {
    pthread_mutex_lock(&gInstanceLock);

    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (gInstances[i] == au) {
            gInstances[i] = NULL;
        }
    }
    pthread_mutex_unlock(&gInstanceLock);
}

// The unit an instance belongs to. Used on the main thread, which is also where a host disposes of a
// unit, so what is found here cannot be closed while it is being used.
static tSynthLibAu * au_for(void * inst) {
    tSynthLibAu * found = NULL;

    if (inst == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&gInstanceLock);

    for (int i = 0; i < MAX_INSTANCES; i++) {
        if ((gInstances[i] != NULL) && (gInstances[i]->inst == inst)) {
            found = gInstances[i];
            break;
        }
    }
    pthread_mutex_unlock(&gInstanceLock);
    return found;
}

static const tSynthLibPluginSet * variants(void) {
    static const tSynthLibPluginSet * v = NULL;

    if (v == NULL) {
        v = synthlib_plugin_variants();
    }
    return v;
}

// notes §2
static const tSynthLibPluginDesc * variant_for(OSType subType) {
    const tSynthLibPluginSet * set = variants();

    for (uint32_t i = 0; i < set->count; i++) {
        if (set->variants[i].auSubType == (uint32_t)subType) {
            return &set->variants[i];
        }
    }
    return &set->variants[0];
}

static uint32_t output_channels(const tSynthLibPluginDesc * d) {
    return (d->numOutputs > 0u) ? d->outputs[0].channels : 0u;
}

static uint32_t input_channels(const tSynthLibPluginDesc * d) {
    return (d->numInputs > 0u) ? d->inputs[0].channels : 0u;
}

// ------------------------------------------------------------------------------------------------
// Small conversions
// ------------------------------------------------------------------------------------------------

static bool is_list(const tSynthLibParamDesc * p) {
    return ((p->flags & SYNTHLIB_PARAM_LIST) != 0u) && (p->stepCount > 0);
}

static AudioUnitParameterUnit au_unit_of(const tSynthLibParamDesc * p) {
    // A LIST IS INDEXED, whatever unit it was given - that is what makes a host draw a menu and ask
    // for the ParameterValueStrings rather than a knob.
    if (is_list(p) == true) {
        return kAudioUnitParameterUnit_Indexed;
    }

    switch (p->unit) {
        case eSynthLibUnitPercent:      return kAudioUnitParameterUnit_Percent;
        case eSynthLibUnitDecibels:     return kAudioUnitParameterUnit_Decibels;
        case eSynthLibUnitHertz:        return kAudioUnitParameterUnit_Hertz;
        case eSynthLibUnitSeconds:      return kAudioUnitParameterUnit_Seconds;
        case eSynthLibUnitMilliseconds: return kAudioUnitParameterUnit_Milliseconds;
        case eSynthLibUnitSemitones:    return kAudioUnitParameterUnit_RelativeSemiTones;
        case eSynthLibUnitIndexed:      return kAudioUnitParameterUnit_Indexed;
        case eSynthLibUnitBoolean:      return kAudioUnitParameterUnit_Boolean;
        case eSynthLibUnitGeneric:
        default:                        return kAudioUnitParameterUnit_Generic;
    }
}

// THE RANGE AN AUDIO UNIT HOST SEES. An Audio Unit's values are PLAIN, in the range declared in its
// ParameterInfo - where VST3's are always 0..1 - so the table's plainMin/plainMax apply, except for a
// list, which an Indexed parameter must count from 0 to its last entry.
static double au_min(const tSynthLibParamDesc * p) {
    return is_list(p) ? 0.0 : p->plainMin;
}

static double au_max(const tSynthLibParamDesc * p) {
    return is_list(p) ? (double)p->stepCount : p->plainMax;
}

static double au_to_plain(const tSynthLibParamDesc * p, double normalized) {
    return au_min(p) + (normalized * (au_max(p) - au_min(p)));
}

static double au_to_normalized(const tSynthLibParamDesc * p, double plain) {
    double span = au_max(p) - au_min(p);

    return (span == 0.0) ? 0.0 : synthlib_param_clamp((plain - au_min(p)) / span);
}

// THE FORMAT THIS WRAPPER RENDERS, AND THE ONLY ONE IT ACCEPTS. De-interleaved 32-bit float, one
// channel per buffer, which is both the Audio Unit canonical format and the shape the plug-in's
// render() already wants - so nothing is repacked between the host's buffers and the engine.
static void fill_stream_format(AudioStreamBasicDescription * asbd, double sampleRate, UInt32 channels) {
    memset(asbd, 0, sizeof(*asbd));
    asbd->mSampleRate       = sampleRate;
    asbd->mFormatID         = kAudioFormatLinearPCM;
    asbd->mFormatFlags      = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    asbd->mBytesPerPacket   = sizeof(float);
    asbd->mFramesPerPacket  = 1;
    asbd->mBytesPerFrame    = sizeof(float);
    asbd->mChannelsPerFrame = channels;
    asbd->mBitsPerChannel   = 32;
}

static void notify_listeners(tSynthLibAu * au, AudioUnitPropertyID id,
                             AudioUnitScope scope, AudioUnitElement element) {
    for (UInt32 i = 0; i < au->listenerCount; i++) {
        if ((au->listeners[i].id == id) && (au->listeners[i].proc != NULL)) {
            au->listeners[i].proc(au->listeners[i].userData, au->ci, id, scope, element);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Parameters
// ------------------------------------------------------------------------------------------------

static void apply_param(tSynthLibAu * au, AudioUnitParameterID id, double normalized) {
    const tSynthLibPluginDesc * d       = au->desc;
    double                      clamped = synthlib_param_clamp(normalized);

    if (synthlib_params_set(&au->params, (uint32_t)id, clamped) == false) {
        return;
    }

    if ((au->inst != NULL) && (d->cb.setParam != NULL)) {
        d->cb.setParam(au->inst, (uint32_t)id, clamped);
    }
}

static void restore_defaults(tSynthLibAu * au) {
    const tSynthLibPluginDesc * d = au->desc;

    for (uint32_t i = 0; i < au->params.count; i++) {
        tSynthLibParamDesc p;

        if (synthlib_param_describe(d, au->inst, i, &p) == true) {
            apply_param(au, (AudioUnitParameterID)p.id, p.defaultNormalized);
        }
    }
}

// THE PLUG-IN'S OWN VALUES, read back into the store - after a state restore that may have set
// parameters the wrapper never saved, which is every NO_SAVE one and all of them in an old project.
static void sync_from_plugin(tSynthLibAu * au) {
    const tSynthLibPluginDesc * d = au->desc;

    if ((au->inst == NULL) || (d->cb.getParam == NULL)) {
        return;
    }

    for (uint32_t i = 0; i < au->params.count; i++) {
        synthlib_params_set(&au->params, au->params.ids[i], d->cb.getParam(au->inst, au->params.ids[i]));
    }
}

// ------------------------------------------------------------------------------------------------
// State - the same bytes the VST3 wrapper writes, wrapped in the dictionary a host expects
// ------------------------------------------------------------------------------------------------

static CFDictionaryRef copy_class_info(tSynthLibAu * au) {
    const tSynthLibPluginDesc * d    = au->desc;
    CFMutableDictionaryRef      dict = CFDictionaryCreateMutable(NULL, 0,
                                                                 &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);

    if (dict == NULL) {
        return NULL;
    }
    SInt32 version      = (SInt32)d->auVersion;
    SInt32 type         = (SInt32)d->auType;
    SInt32 subtype      = (SInt32)d->auSubType;
    SInt32 manufacturer = (SInt32)d->auManufacturer;

#define PUT_INT(key, value)                                                          \
    do {                                                                             \
        CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &(value));          \
        if (n != NULL) {                                                             \
            CFDictionarySetValue(dict, CFSTR(key), n);                               \
            CFRelease(n);                                                            \
        }                                                                            \
    } while (0)

    PUT_INT(kAUPresetVersionKey, version);
    PUT_INT(kAUPresetTypeKey, type);
    PUT_INT(kAUPresetSubtypeKey, subtype);
    PUT_INT(kAUPresetManufacturerKey, manufacturer);
#undef PUT_INT

    if (au->currentPreset.presetName != NULL) {
        CFDictionarySetValue(dict, CFSTR(kAUPresetNameKey), au->currentPreset.presetName);
    }

    // OUR OWN BLOB, BYTE FOR BYTE THE ONE THE VST3 SIDE WRITES. That is what lets a patch saved in
    // one format be read back by the other, and stops the two from drifting over what a saved
    // plug-in means - see synthlibPluginState.h.
    size_t need = synthlib_state_write(d, au->inst, &au->params, NULL, 0);

    if (need > 0) {
        uint8_t * bytes = (uint8_t *)malloc(need);

        if (bytes != NULL) {
            size_t    wrote = synthlib_state_write(d, au->inst, &au->params, bytes, need);
            CFDataRef data  = CFDataCreate(NULL, bytes, (CFIndex)wrote);

            if (data != NULL) {
                CFDictionarySetValue(dict, CFSTR(kAUPresetDataKey), data);
                CFRelease(data);
            }
            free(bytes);
        }
    }
    return dict;
}

static OSStatus apply_class_info(tSynthLibAu * au, CFDictionaryRef dict) {
    const tSynthLibPluginDesc * d = au->desc;

    if (dict == NULL) {
        return kAudioUnitErr_InvalidPropertyValue;
    }
    CFDataRef data = (CFDataRef)CFDictionaryGetValue(dict, CFSTR(kAUPresetDataKey));

    if ((data == NULL) || (CFGetTypeID(data) != CFDataGetTypeID())) {
        return noErr;       // a dictionary with no data of ours in it is not an error, just nothing to do
    }
    uint32_t              capacity   = (au->params.count > 0u) ? au->params.count : 1u;
    tSynthLibParamValue * values     = (tSynthLibParamValue *)calloc(capacity, sizeof(tSynthLibParamValue));
    uint32_t              got        = 0;
    const void *          pluginData = NULL;
    size_t                pluginLen  = 0;

    if (values == NULL) {
        return kAudioUnitErr_FailedInitialization;
    }

    if (synthlib_state_read(&au->params, CFDataGetBytePtr(data), (size_t)CFDataGetLength(data),
                            values, au->params.count, &got, &pluginData, &pluginLen) == true) {
        for (uint32_t i = 0; i < got; i++) {
            apply_param(au, (AudioUnitParameterID)values[i].id, values[i].value);
        }

        // THE PLUG-IN'S OWN STATE LAST, for the reason the VST3 wrapper gives: loading it rebuilds
        // whatever the parameters feed into, so they have to already be where the project left them.
        if ((au->inst != NULL) && (d->cb.setState != NULL)) {
            d->cb.setState(au->inst, pluginData, pluginLen);
        }
        sync_from_plugin(au);
    }
    free(values);

    CFStringRef name = (CFStringRef)CFDictionaryGetValue(dict, CFSTR(kAUPresetNameKey));

    if ((name != NULL) && (CFGetTypeID(name) == CFStringGetTypeID())) {
        if (au->currentPreset.presetName != NULL) {
            CFRelease(au->currentPreset.presetName);
        }
        au->currentPreset.presetName = CFStringCreateCopy(NULL, name);
        au->currentPreset.presetNumber = -1;         // a restored state is a user preset, not a factory one
    }
    notify_listeners(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0);
    notify_listeners(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
    return noErr;
}

// ------------------------------------------------------------------------------------------------
// Lifecycle helpers
// ------------------------------------------------------------------------------------------------

static void send_prepare(tSynthLibAu * au) {
    const tSynthLibPluginDesc * d = au->desc;

    if ((au->inst == NULL) || (d->cb.prepare == NULL)) {
        return;
    }
    tSynthLibSetup setup;

    setup.sampleRate = au->sampleRate;
    setup.maxFrames  = au->maxFrames;
    setup.offline    = au->offline;
    d->cb.prepare(au->inst, &setup);
}

// ------------------------------------------------------------------------------------------------
// Properties
// ------------------------------------------------------------------------------------------------

static UInt32 element_count(const tSynthLibPluginDesc * d, AudioUnitScope scope) {
    switch (scope) {
        case kAudioUnitScope_Global: return 1;
        case kAudioUnitScope_Input:  return d->numInputs;
        case kAudioUnitScope_Output: return d->numOutputs;
        case kAudioUnitScope_Group:  return d->isInstrument ? 1 : 0;
        default:                     return 0;
    }
}

// notes §3
static bool is_global_only(AudioUnitPropertyID id) {
    switch (id) {
        case kAudioUnitProperty_ParameterInfo:
        case kAudioUnitProperty_ParameterValueStrings:
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
        case kAudioUnitProperty_SupportedNumChannels:
        case kAudioUnitProperty_MaximumFramesPerSlice:
        case kAudioUnitProperty_LastRenderError:
        case kAudioUnitProperty_FactoryPresets:
        case kAudioUnitProperty_PresentPreset:
        case kAudioUnitProperty_CocoaUI:
        case kAudioUnitProperty_ParameterStringFromValue:
        case kAudioUnitProperty_OfflineRender:
        case kAudioUnitProperty_BypassEffect:
        case kMusicDeviceProperty_InstrumentCount:
        case kSynthLibAuProperty_Instance:
            return true;

        default:
            return false;
    }
}

// The parameters a host is shown: all of them but the hidden ones, which exist only to be delivered
// to - a GenBridge pass-through arrives here as MIDI and is mapped by us, never set by a host.
static uint32_t visible_count(const tSynthLibAu * au) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < au->params.count; i++) {
        if ((au->params.flags[i] & SYNTHLIB_PARAM_HIDDEN) == 0u) {
            count++;
        }
    }
    return count;
}

static OSStatus au_get_property_info(void * self, AudioUnitPropertyID id, AudioUnitScope scope,
                                     AudioUnitElement element, UInt32 * outSize, Boolean * outWritable) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = au->desc;
    UInt32                      size     = 0;
    Boolean                     writable = false;

    if ((is_global_only(id) == true) && (scope != kAudioUnitScope_Global)) {
        return kAudioUnitErr_InvalidScope;
    }

    switch (id) {
        case kAudioUnitProperty_ClassInfo:
            size     = sizeof(CFPropertyListRef);
            writable = true;
            break;

        case kAudioUnitProperty_SampleRate:
            size     = sizeof(Float64);
            writable = true;
            break;

        case kAudioUnitProperty_ParameterList:
            // notes §4
            size = (scope == kAudioUnitScope_Global)
                   ? (UInt32)(visible_count(au) * sizeof(AudioUnitParameterID))
                   : 0;
            break;

        // THE ELEMENT IS THE PARAMETER ID, not its position - which only mattered once a plug-in's
        // ids stopped being their positions.
        case kAudioUnitProperty_ParameterInfo:
            if (synthlib_params_index(&au->params, (uint32_t)element) < 0) {
                return kAudioUnitErr_InvalidElement;
            }
            size = sizeof(AudioUnitParameterInfo);
            break;

        case kAudioUnitProperty_ParameterValueStrings: {
            tSynthLibParamDesc p;

            if ((synthlib_params_describe(&au->params, d, au->inst, (uint32_t)element, &p) == false) ||
                (is_list(&p) == false)) {
                return kAudioUnitErr_InvalidProperty;
            }
            size = sizeof(CFArrayRef);
            break;
        }

        case kAudioUnitProperty_StreamFormat:
            if (element_count(d, scope) == 0) {
                return kAudioUnitErr_InvalidScope;
            }
            size     = sizeof(AudioStreamBasicDescription);
            writable = true;
            break;

        case kAudioUnitProperty_ElementCount:
            size = sizeof(UInt32);
            break;

        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
            size = sizeof(Float64);
            break;

        case kAudioUnitProperty_SupportedNumChannels:
            size = sizeof(AUChannelInfo);
            break;

        case kAudioUnitProperty_MaximumFramesPerSlice:
            size     = sizeof(UInt32);
            writable = true;
            break;

        // A BOUNCE, which a host announces here. Accepted from any plug-in and passed on through
        // prepare(); what to DO about it is the plug-in's business.
        case kAudioUnitProperty_OfflineRender:
            size     = sizeof(UInt32);
            writable = true;
            break;

        // A HOST'S BYPASS BUTTON, which auval lists among the properties an effect should have. Only
        // an effect: an instrument has no input for a bypass to hand on.
        case kAudioUnitProperty_BypassEffect:
            if ((d->isInstrument == true) || (input_channels(d) == 0u)) {
                return kAudioUnitErr_InvalidProperty;
            }
            size     = sizeof(UInt32);
            writable = true;
            break;

        // WRITE ONLY, FROM THE HOST. These are how a host feeds an effect and how it offers its
        // transport; there is nothing to read back, so GetProperty refuses them and only the size
        // and the writable flag are meaningful here.
        case kAudioUnitProperty_HostCallbacks:
            if (d->wantsTransport == false) {
                return kAudioUnitErr_InvalidProperty;
            }
            size     = sizeof(HostCallbackInfo);
            writable = true;
            break;

        case kAudioUnitProperty_MakeConnection:
            if (element_count(d, kAudioUnitScope_Input) == 0u) {
                return kAudioUnitErr_InvalidProperty;
            }
            size     = sizeof(AudioUnitConnection);
            writable = true;
            break;

        case kAudioUnitProperty_SetRenderCallback:
            if (element_count(d, kAudioUnitScope_Input) == 0u) {
                return kAudioUnitErr_InvalidProperty;
            }
            size     = sizeof(AURenderCallbackStruct);
            writable = true;
            break;

        case kAudioUnitProperty_LastRenderError:
            size = sizeof(OSStatus);
            break;

        case kAudioUnitProperty_FactoryPresets:
            size = sizeof(CFArrayRef);
            break;

        case kAudioUnitProperty_PresentPreset:
            size     = sizeof(AUPreset);
            writable = true;
            break;

        case kAudioUnitProperty_CocoaUI:
            if ((d->cb.createView == NULL) || (d->editorDefaultWidth <= 0.0)) {
                return kAudioUnitErr_InvalidProperty;
            }
            size = sizeof(AudioUnitCocoaViewInfo);
            break;

        case kAudioUnitProperty_ParameterStringFromValue:
            size = sizeof(AudioUnitParameterStringFromValue);
            break;

        // notes §5

        case kMusicDeviceProperty_InstrumentCount:
            if (d->isInstrument == false) {
                return kAudioUnitErr_InvalidProperty;
            }
            size = sizeof(UInt32);
            break;

        case kSynthLibAuProperty_Instance:
            size = sizeof(tSynthLibAuHandle);
            break;

        default:
            return kAudioUnitErr_InvalidProperty;
    }

    if (outSize != NULL) {
        *outSize = size;
    }

    if (outWritable != NULL) {
        *outWritable = writable;
    }
    return noErr;
}

static OSStatus au_get_property(void * self, AudioUnitPropertyID id, AudioUnitScope scope,
                                AudioUnitElement element, void * outData, UInt32 * ioDataSize) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = au->desc;
    UInt32                      size     = 0;
    Boolean                     writable = false;
    OSStatus                    err      = au_get_property_info(self, id, scope, element, &size, &writable);

    if (err != noErr) {
        return err;
    }

    if ((ioDataSize == NULL) || (*ioDataSize < size)) {
        return kAudioUnitErr_InvalidPropertyValue;
    }
    *ioDataSize = size;

    if (outData == NULL) {
        return noErr;
    }

    switch (id) {
        case kAudioUnitProperty_ClassInfo: {
            CFDictionaryRef dict = copy_class_info(au);

            if (dict == NULL) {
                return kAudioUnitErr_FailedInitialization;
            }
            *(CFPropertyListRef *)outData = dict;       // the caller releases it
            break;
        }

        case kAudioUnitProperty_SampleRate:
            *(Float64 *)outData = au->sampleRate;
            break;

        case kAudioUnitProperty_ParameterList: {
            AudioUnitParameterID * ids = (AudioUnitParameterID *)outData;
            uint32_t               n   = 0;

            for (uint32_t i = 0; i < au->params.count; i++) {
                if ((au->params.flags[i] & SYNTHLIB_PARAM_HIDDEN) == 0u) {
                    ids[n++] = (AudioUnitParameterID)au->params.ids[i];
                }
            }
            break;
        }

        case kAudioUnitProperty_ParameterInfo: {
            tSynthLibParamDesc       p;
            AudioUnitParameterInfo * info = (AudioUnitParameterInfo *)outData;

            if (synthlib_params_describe(&au->params, d, au->inst, (uint32_t)element, &p) == false) {
                return kAudioUnitErr_InvalidElement;
            }
            memset(info, 0, sizeof(*info));
            info->unit         = au_unit_of(&p);
            info->minValue     = (AudioUnitParameterValue)au_min(&p);
            info->maxValue     = (AudioUnitParameterValue)au_max(&p);
            info->defaultValue = (AudioUnitParameterValue)au_to_plain(&p, p.defaultNormalized);
            info->cfNameString = CFStringCreateWithCString(NULL, p.title, kCFStringEncodingUTF8);

            // CFNameRelease says the caller owns the string we just made. Without it the host leaks
            // one CFString per parameter per query, and a host queries often.
            info->flags = kAudioUnitParameterFlag_IsReadable |
                          kAudioUnitParameterFlag_IsWritable |
                          kAudioUnitParameterFlag_HasCFNameString |
                          kAudioUnitParameterFlag_CFNameRelease |
                          kAudioUnitParameterFlag_IsHighResolution;

            if (is_list(&p) == true) {
                info->flags |= kAudioUnitParameterFlag_ValuesHaveStrings;
            }

            // The 52-byte name is the legacy field, filled as well as the CFString: some hosts and
            // every old one read only this.
            strncpy(info->name, p.title, sizeof(info->name) - 1);
            break;
        }

        case kAudioUnitProperty_ParameterValueStrings: {
            tSynthLibParamDesc     p;
            CFMutableArrayRef      names;

            if (synthlib_params_describe(&au->params, d, au->inst, (uint32_t)element, &p) == false) {
                return kAudioUnitErr_InvalidElement;
            }
            names = CFArrayCreateMutable(NULL, p.stepCount + 1, &kCFTypeArrayCallBacks);

            if (names == NULL) {
                return kAudioUnitErr_FailedInitialization;
            }

            // THE SAME NAMES THE VST3 HOST GETS, from paramText(), entry by entry.
            for (int32_t i = 0; i <= p.stepCount; i++) {
                char        text[128];
                CFStringRef s;

                synthlib_param_text(d, au->inst, &p, (double)i / (double)p.stepCount, text, sizeof(text));
                s = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);

                if (s != NULL) {
                    CFArrayAppendValue(names, s);
                    CFRelease(s);
                }
            }
            *(CFArrayRef *)outData = names;             // the caller releases it
            break;
        }

        case kAudioUnitProperty_StreamFormat: {
            UInt32 channels = (scope == kAudioUnitScope_Input) ? input_channels(d)
                                                               : output_channels(d);

            fill_stream_format((AudioStreamBasicDescription *)outData, au->sampleRate, channels);
            break;
        }

        case kAudioUnitProperty_ElementCount:
            *(UInt32 *)outData = element_count(d, scope);
            break;

        case kAudioUnitProperty_Latency: {
            uint32_t samples = ((au->inst != NULL) && (d->cb.latencySamples != NULL))
                               ? d->cb.latencySamples(au->inst) : 0u;

            // SECONDS, where VST3 counts samples.
            *(Float64 *)outData = (au->sampleRate > 0.0) ? ((Float64)samples / au->sampleRate) : 0.0;
            break;
        }

        case kAudioUnitProperty_TailTime:
            // THE NEAREST AN AUDIO UNIT CAN COME TO VST3'S kInfiniteTail. There are reverbs and delays
            // in these engines, so a host must not decide the plug-in has finished the moment the
            // notes stop.
            *(Float64 *)outData = 10.0;
            break;

        case kAudioUnitProperty_SupportedNumChannels: {
            AUChannelInfo * info = (AUChannelInfo *)outData;

            info->inChannels  = (SInt16)input_channels(d);
            info->outChannels = (SInt16)output_channels(d);
            break;
        }

        case kAudioUnitProperty_MaximumFramesPerSlice:
            *(UInt32 *)outData = au->maxFrames;
            break;

        case kAudioUnitProperty_OfflineRender:
            *(UInt32 *)outData = au->offline ? 1u : 0u;
            break;

        case kAudioUnitProperty_BypassEffect:
            *(UInt32 *)outData = au->bypassed ? 1u : 0u;
            break;

        case kAudioUnitProperty_HostCallbacks:
        case kAudioUnitProperty_MakeConnection:
        case kAudioUnitProperty_SetRenderCallback:
            return kAudioUnitErr_InvalidPropertyValue;      // the host writes these; there is no read

        case kAudioUnitProperty_LastRenderError:
            *(OSStatus *)outData  = au->lastRenderError;
            au->lastRenderError   = noErr;      // reading it clears it, as a host expects
            break;

        case kAudioUnitProperty_FactoryPresets: {
            // notes §6
            static const AUPreset kFactoryPresets[1] = { { 0, CFSTR("Default") } };
            const void *          items[1]           = { &kFactoryPresets[0] };
            CFArrayRef            array              = CFArrayCreate(NULL, items, 1, NULL);

            if (array == NULL) {
                return kAudioUnitErr_FailedInitialization;
            }
            *(CFArrayRef *)outData = array;             // the caller releases it
            break;
        }

        case kAudioUnitProperty_PresentPreset: {
            AUPreset * preset = (AUPreset *)outData;

            *preset = au->currentPreset;

            if (preset->presetName != NULL) {
                CFRetain(preset->presetName);           // the caller releases it
            }
            break;
        }

        case kAudioUnitProperty_CocoaUI: {
            AudioUnitCocoaViewInfo * info   = (AudioUnitCocoaViewInfo *)outData;
            CFStringRef              bundleId;
            CFBundleRef              bundle;

            memset(info, 0, sizeof(*info));

            if (d->auBundleId == NULL) {
                return kAudioUnitErr_InvalidProperty;
            }
            bundleId = CFStringCreateWithCString(NULL, d->auBundleId, kCFStringEncodingUTF8);

            if (bundleId == NULL) {
                return kAudioUnitErr_FailedInitialization;
            }
            // notes §7
            bundle = CFBundleGetBundleWithIdentifier(bundleId);
            CFRelease(bundleId);

            if (bundle == NULL) {
                return kAudioUnitErr_InvalidProperty;
            }
            info->mCocoaAUViewBundleLocation = CFBundleCopyBundleURL(bundle);
            info->mCocoaAUViewClass[0]       = CFStringCreateCopy(NULL, synthlib_au_view_class_name());
            break;
        }

        case kAudioUnitProperty_ParameterStringFromValue: {
            AudioUnitParameterStringFromValue * req = (AudioUnitParameterStringFromValue *)outData;
            char                                text[128];
            double                              normalized;

            tSynthLibParamDesc p;

            if (synthlib_params_describe(&au->params, d, au->inst, (uint32_t)req->inParamID, &p) == false) {
                return kAudioUnitErr_InvalidParameter;
            }
            normalized = (req->inValue != NULL) ? au_to_normalized(&p, (double)(*req->inValue))
                                                : synthlib_params_get(&au->params, (uint32_t)req->inParamID);

            if (synthlib_param_text(d, au->inst, &p, normalized, text, sizeof(text)) == false) {
                return kAudioUnitErr_InvalidParameter;
            }
            req->outString = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
            break;
        }

        case kMusicDeviceProperty_InstrumentCount:
            // Zero means "not multitimbral", which is the honest answer for a one-voice engine.
            *(UInt32 *)outData = 0;
            break;

        case kSynthLibAuProperty_Instance: {
            tSynthLibAuHandle * handle = (tSynthLibAuHandle *)outData;

            handle->desc = au->desc;
            handle->inst = au->inst;
            break;
        }

        default:
            return kAudioUnitErr_InvalidProperty;
    }
    return noErr;
}

static OSStatus au_set_property(void * self, AudioUnitPropertyID id, AudioUnitScope scope,
                                AudioUnitElement element, const void * inData, UInt32 inDataSize) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = au->desc;

    (void)element;

    if ((is_global_only(id) == true) && (scope != kAudioUnitScope_Global)) {
        return kAudioUnitErr_InvalidScope;
    }

    switch (id) {
        case kAudioUnitProperty_ClassInfo:
            if ((inData == NULL) || (inDataSize < sizeof(CFPropertyListRef))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            return apply_class_info(au, *(CFDictionaryRef *)inData);

        case kAudioUnitProperty_SampleRate: {
            if ((inData == NULL) || (inDataSize < sizeof(Float64))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            Float64 rate = *(const Float64 *)inData;

            if (rate <= 0.0) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            au->sampleRate = rate;

            if ((au->inst != NULL) && (d->cb.setSampleRate != NULL)) {
                d->cb.setSampleRate(au->inst, au->sampleRate);
            }
            notify_listeners(au, kAudioUnitProperty_StreamFormat, scope, 0);
            return noErr;
        }

        case kAudioUnitProperty_StreamFormat: {
            if ((inData == NULL) || (inDataSize < sizeof(AudioStreamBasicDescription))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            const AudioStreamBasicDescription * asbd = (const AudioStreamBasicDescription *)inData;
            UInt32 channels = (scope == kAudioUnitScope_Input) ? input_channels(d)
                                                               : output_channels(d);

            // ONLY THE ONE FORMAT, AND SAYING SO IS WHAT LETS au_render() TRUST ITS BUFFERS. A host
            // told "no" here picks another; a host allowed to set an interleaved or 16-bit format
            // would hand over buffers this wrapper would then have to repack at every block.
            if ((asbd->mFormatID != kAudioFormatLinearPCM) ||
                ((asbd->mFormatFlags & kAudioFormatFlagIsFloat) == 0) ||
                ((asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0) ||
                (asbd->mBitsPerChannel != 32) ||
                (asbd->mChannelsPerFrame != channels)) {
                return kAudioUnitErr_FormatNotSupported;
            }

            if (asbd->mSampleRate > 0.0) {
                au->sampleRate = asbd->mSampleRate;

                if ((au->inst != NULL) && (d->cb.setSampleRate != NULL)) {
                    d->cb.setSampleRate(au->inst, au->sampleRate);
                }
            }
            return noErr;
        }

        case kAudioUnitProperty_MaximumFramesPerSlice: {
            if ((inData == NULL) || (inDataSize < sizeof(UInt32))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            au->maxFrames = *(const UInt32 *)inData;

            // A HOST HAS TO BE TOLD. auval sets this and then waits for the notification - and so
            // does anything else that caches a buffer sized from it, which is the reason the test
            // exists at all.
            notify_listeners(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0);
            return noErr;
        }

        case kAudioUnitProperty_OfflineRender: {
            if ((inData == NULL) || (inDataSize < sizeof(UInt32))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            bool offline = (*(const UInt32 *)inData != 0u);

            if (offline != au->offline) {
                au->offline = offline;

                if (au->initialized == true) {
                    send_prepare(au);
                }
            }
            return noErr;
        }

        case kAudioUnitProperty_BypassEffect: {
            if ((inData == NULL) || (inDataSize < sizeof(UInt32))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            au->bypassed = (*(const UInt32 *)inData != 0u);
            notify_listeners(au, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0);
            return noErr;
        }

        case kAudioUnitProperty_PresentPreset: {
            if ((inData == NULL) || (inDataSize < sizeof(AUPreset))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            const AUPreset * preset = (const AUPreset *)inData;

            if (au->currentPreset.presetName != NULL) {
                CFRelease(au->currentPreset.presetName);
                au->currentPreset.presetName = NULL;
            }
            au->currentPreset.presetNumber = preset->presetNumber;
            au->currentPreset.presetName   = (preset->presetName != NULL)
                                             ? CFStringCreateCopy(NULL, preset->presetName)
                                             : CFStringCreateCopy(NULL, CFSTR("Untitled"));

            // The one factory preset is "everything back to its default".
            if (preset->presetNumber == 0) {
                restore_defaults(au);
                notify_listeners(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0);
            }
            notify_listeners(au, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
            return noErr;
        }

        // WHERE THE HOST'S TRANSPORT COMES FROM, and it is a set of function pointers we CALL rather
        // than a block of facts we are given - see fill_transport() for what that costs.
        case kAudioUnitProperty_HostCallbacks: {
            if (d->wantsTransport == false) {
                return kAudioUnitErr_InvalidProperty;
            }
            memset(&au->hostCallbacks, 0, sizeof(au->hostCallbacks));

            // A HOST MAY SEND A SHORTER STRUCT THAN THE ONE WE COMPILED AGAINST, which is the whole
            // reason this copies inDataSize bytes rather than sizeof(). Reading past what it sent
            // would call a function pointer it never wrote.
            UInt32 copy = (inDataSize < (UInt32)sizeof(au->hostCallbacks))
                          ? inDataSize : (UInt32)sizeof(au->hostCallbacks);

            if ((inData != NULL) && (copy > 0u)) {
                memcpy(&au->hostCallbacks, inData, copy);
                au->haveHostCallbacks = true;
            } else {
                au->haveHostCallbacks = false;      // a host clearing them, which is legal
            }
            return noErr;
        }

        // The two ways a host feeds an effect. An instrument declares no input bus, and then both
        // are refused above by the element count.
        case kAudioUnitProperty_MakeConnection: {
            if ((inData == NULL) || (inDataSize < sizeof(AudioUnitConnection))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            const AudioUnitConnection * conn = (const AudioUnitConnection *)inData;

            au->inputSourceUnit     = conn->sourceAudioUnit;
            au->inputSourceBus      = conn->sourceOutputNumber;
            au->haveInputCallback   = false;        // a connection replaces a callback
            return noErr;
        }

        case kAudioUnitProperty_SetRenderCallback: {
            if ((inData == NULL) || (inDataSize < sizeof(AURenderCallbackStruct))) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            memcpy(&au->inputCallback, inData, sizeof(au->inputCallback));
            au->haveInputCallback = (au->inputCallback.inputProc != NULL);
            au->inputSourceUnit   = NULL;           // and a callback replaces a connection
            return noErr;
        }

        default:
            return kAudioUnitErr_InvalidProperty;
    }
}

// ------------------------------------------------------------------------------------------------
// Listeners
// ------------------------------------------------------------------------------------------------

static OSStatus au_add_property_listener(void * self, AudioUnitPropertyID id,
                                         AudioUnitPropertyListenerProc proc, void * userData) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    if (au->listenerCount >= MAX_LISTENERS) {
        return kAudioUnitErr_FailedInitialization;
    }
    au->listeners[au->listenerCount].id       = id;
    au->listeners[au->listenerCount].proc     = proc;
    au->listeners[au->listenerCount].userData = userData;
    au->listenerCount++;
    return noErr;
}

static OSStatus au_remove_property_listener_with_user_data(void * self, AudioUnitPropertyID id,
                                                           AudioUnitPropertyListenerProc proc,
                                                           void * userData) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    for (UInt32 i = 0; i < au->listenerCount; ) {
        if ((au->listeners[i].id == id) && (au->listeners[i].proc == proc) &&
            (au->listeners[i].userData == userData)) {
            au->listeners[i] = au->listeners[au->listenerCount - 1];
            au->listenerCount--;
        } else {
            i++;
        }
    }
    return noErr;
}

static OSStatus au_remove_property_listener(void * self, AudioUnitPropertyID id,
                                            AudioUnitPropertyListenerProc proc) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    for (UInt32 i = 0; i < au->listenerCount; ) {
        if ((au->listeners[i].id == id) && (au->listeners[i].proc == proc)) {
            au->listeners[i] = au->listeners[au->listenerCount - 1];
            au->listenerCount--;
        } else {
            i++;
        }
    }
    return noErr;
}

static OSStatus au_add_render_notify(void * self, AURenderCallback proc, void * userData) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    if (au->renderNotifyCount >= MAX_RENDER_NOTIFY) {
        return kAudioUnitErr_FailedInitialization;
    }
    au->renderNotify[au->renderNotifyCount].proc     = proc;
    au->renderNotify[au->renderNotifyCount].userData = userData;
    au->renderNotifyCount++;
    return noErr;
}

static OSStatus au_remove_render_notify(void * self, AURenderCallback proc, void * userData) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    for (UInt32 i = 0; i < au->renderNotifyCount; ) {
        if ((au->renderNotify[i].proc == proc) && (au->renderNotify[i].userData == userData)) {
            au->renderNotify[i] = au->renderNotify[au->renderNotifyCount - 1];
            au->renderNotifyCount--;
        } else {
            i++;
        }
    }
    return noErr;
}

// ------------------------------------------------------------------------------------------------
// The event queue
// ------------------------------------------------------------------------------------------------

// FALSE WHEN FULL, and the event is dropped - which a render that empties the queue every time makes
// a matter of a host scheduling a thousand events between two renders.
static bool enqueue(tSynthLibAu * au, const tQueuedEvent * event) {
    bool queued = false;

    while (atomic_flag_test_and_set_explicit(&au->eventProducer, memory_order_acquire) == true) {
        // Another producer, for the handful of instructions below. Never the render.
    }
    uint32_t write = atomic_load_explicit(&au->eventWrite, memory_order_relaxed);
    uint32_t read  = atomic_load_explicit(&au->eventRead, memory_order_acquire);

    if ((write - read) < (uint32_t)EVENT_QUEUE_SIZE) {
        au->events[write % EVENT_QUEUE_SIZE] = *event;
        atomic_store_explicit(&au->eventWrite, write + 1u, memory_order_release);
        queued = true;
    }
    atomic_flag_clear_explicit(&au->eventProducer, memory_order_release);
    return queued;
}

static OSStatus queue_midi(tSynthLibAu * au, uint8_t status, uint8_t data1, uint8_t data2, UInt32 offset) {
    tQueuedEvent event;

    memset(&event, 0, sizeof(event));
    event.status = status;
    event.data1  = data1 & 0x7Fu;
    event.data2  = data2 & 0x7Fu;
    event.offset = offset;
    return (enqueue(au, &event) == true) ? noErr : kAudioUnitErr_TooManyFramesToProcess;
}

static void queue_param(tSynthLibAu * au, uint32_t id, double normalized, uint32_t offset) {
    tQueuedEvent event;

    memset(&event, 0, sizeof(event));
    event.isParam = true;
    event.id      = id;
    event.value   = synthlib_param_clamp(normalized);
    event.offset  = offset;
    (void)enqueue(au, &event);
}

// ------------------------------------------------------------------------------------------------
// Parameters, as the host sees them
// ------------------------------------------------------------------------------------------------

static OSStatus au_get_parameter(void * self, AudioUnitParameterID id, AudioUnitScope scope,
                                 AudioUnitElement element, AudioUnitParameterValue * outValue) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    (void)element;

    if (scope != kAudioUnitScope_Global) {
        return kAudioUnitErr_InvalidScope;
    }

    tSynthLibParamDesc p;

    if ((outValue == NULL) ||
        (synthlib_params_describe(&au->params, au->desc, au->inst, (uint32_t)id, &p) == false)) {
        return kAudioUnitErr_InvalidParameter;
    }
    // PLAIN, not normalized. An Audio Unit's parameter values are in the range it declared in
    // AudioUnitParameterInfo, where VST3's are always 0..1 - the one real difference between the two
    // formats' parameter models, and the reason the table carries plainMin and plainMax at all.
    *outValue = (AudioUnitParameterValue)au_to_plain(&p, synthlib_params_get(&au->params, (uint32_t)id));
    return noErr;
}

// FROM ANY THREAD, AND APPLIED AT ONCE - see the threading note in synthlibPlugin.h. A host moving a
// knob in its own generic panel calls this from its UI thread with no render in sight, so there is no
// block to queue it for; the offset only means something to ScheduleParameters, below.
static OSStatus au_set_parameter(void * self, AudioUnitParameterID id, AudioUnitScope scope,
                                 AudioUnitElement element, AudioUnitParameterValue value,
                                 UInt32 inBufferOffsetInFrames) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    (void)element;
    (void)inBufferOffsetInFrames;

    if (scope != kAudioUnitScope_Global) {
        return kAudioUnitErr_InvalidScope;
    }

    tSynthLibParamDesc p;

    if (synthlib_params_describe(&au->params, au->desc, au->inst, (uint32_t)id, &p) == false) {
        return kAudioUnitErr_InvalidParameter;
    }
    apply_param(au, id, au_to_normalized(&p, (double)value));
    return noErr;
}

// AUTOMATION FOR THE NEXT RENDER, from a host that schedules it - so it is queued and delivered inside
// that render, at its offset, exactly as a VST3 host's parameter queue is walked inside process().
static OSStatus au_schedule_parameters(void * self, const AudioUnitParameterEvent * events,
                                       UInt32 numEvents) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    if (events == NULL) {
        return kAudioUnitErr_InvalidParameterValue;
    }

    for (UInt32 i = 0; i < numEvents; i++) {
        const AudioUnitParameterEvent * e = &events[i];
        tSynthLibParamDesc              p;

        if ((e->scope != kAudioUnitScope_Global) ||
            (synthlib_params_describe(&au->params, au->desc, au->inst, (uint32_t)e->parameter, &p) == false)) {
            continue;
        }

        // A RAMP IS ITS TWO ENDS. The engines have no notion of a parameter moving within a block,
        // so interpolating would be inventing a resolution they cannot use - a plug-in with
        // paramPoints() sees both ends, one without sees the end it settles at.
        if (e->eventType == kParameterEvent_Ramped) {
            SInt32   start    = e->eventValues.ramp.startBufferOffset;
            uint32_t startAt  = (start > 0) ? (uint32_t)start : 0u;
            uint32_t duration = e->eventValues.ramp.durationInFrames;

            queue_param(au, e->parameter, au_to_normalized(&p, (double)e->eventValues.ramp.startValue),
                        startAt);
            queue_param(au, e->parameter, au_to_normalized(&p, (double)e->eventValues.ramp.endValue),
                        startAt + ((duration > 0u) ? (duration - 1u) : 0u));
        } else {
            queue_param(au, e->parameter, au_to_normalized(&p, (double)e->eventValues.immediate.value),
                        e->eventValues.immediate.bufferOffset);
        }
    }
    return noErr;
}

// ------------------------------------------------------------------------------------------------
// MIDI
// ------------------------------------------------------------------------------------------------

// ALL OF THESE QUEUE, and the render delivers. An Audio Unit host hands over its MIDI BEFORE the
// render it belongs to - that is what inOffsetSampleFrame is measured from - so delivering it on the
// spot would reach a plug-in that has not yet been told where the block it belongs to begins.

static OSStatus au_midi_event(void * self, UInt32 inStatus, UInt32 inData1, UInt32 inData2,
                              UInt32 inOffsetSampleFrame) {
    return queue_midi((tSynthLibAu *)self, (uint8_t)inStatus, (uint8_t)inData1, (uint8_t)inData2,
                      inOffsetSampleFrame);
}

// THE GROUP IS THE CHANNEL, by convention - which is all a MusicDevice that is not multitimbral
// makes of it.
static OSStatus au_start_note(void * self, MusicDeviceInstrumentID inInstrument,
                              MusicDeviceGroupID inGroupID, NoteInstanceID * outNoteInstanceID,
                              UInt32 inOffsetSampleFrame, const MusicDeviceNoteParams * inParams) {
    (void)inInstrument;

    if (inParams == NULL) {
        return kAudioUnitErr_InvalidParameter;
    }
    // mPitch is a FLOAT here, so a host may ask for a fractional note. The engines are note-number
    // driven, so it is rounded - and the note instance id is the rounded number, which is what
    // StopNote will hand back.
    uint8_t note     = (uint8_t)(inParams->mPitch + 0.5f);
    uint8_t velocity = (uint8_t)(inParams->mVelocity + 0.5f);

    if (outNoteInstanceID != NULL) {
        *outNoteInstanceID = (NoteInstanceID)note;
    }
    return queue_midi((tSynthLibAu *)self, (uint8_t)(0x90u | (inGroupID & 0x0Fu)), note,
                      (velocity > 0u) ? velocity : 1u, inOffsetSampleFrame);
}

static OSStatus au_stop_note(void * self, MusicDeviceGroupID inGroupID, NoteInstanceID inNoteInstanceID,
                             UInt32 inOffsetSampleFrame) {
    return queue_midi((tSynthLibAu *)self, (uint8_t)(0x80u | (inGroupID & 0x0Fu)),
                      (uint8_t)inNoteInstanceID, 0u, inOffsetSampleFrame);
}

// notes §8
static bool map_control(tSynthLibAu * au, uint8_t channel, int16_t control, uint32_t * idOut) {
    const tSynthLibPluginDesc * d = au->desc;

    if (d->cb.midiMapping != NULL) {
        return (d->cb.midiMapping(d, au->inst, channel, control, idOut) == true) &&
               (synthlib_params_index(&au->params, *idOut) >= 0);
    }

    for (uint32_t i = 0; i < au->params.count; i++) {
        tSynthLibParamDesc p;

        if ((synthlib_param_describe(d, au->inst, i, &p) == true) && (p.midiControl == control)) {
            *idOut = p.id;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// Render
// ------------------------------------------------------------------------------------------------

// Parameter points for one id, delivered as a run - the same shape paramPoints() gets on VST3.
static void deliver_points(tSynthLibAu * au, uint32_t id, const tSynthLibParamPoint * points, uint32_t count) {
    const tSynthLibPluginDesc * d = au->desc;

    if ((count == 0u) || (synthlib_params_set(&au->params, id, points[count - 1].value) == false)) {
        return;
    }

    if (d->cb.paramPoints != NULL) {
        d->cb.paramPoints(au->inst, id, points, count);
    } else if (d->cb.setParam != NULL) {
        d->cb.setParam(au->inst, id, points[count - 1].value);
    }
}

static void deliver_midi(tSynthLibAu * au, const tQueuedEvent * e, uint32_t offset) {
    const tSynthLibPluginDesc * d       = au->desc;
    uint8_t                     channel = e->status & 0x0Fu;
    uint32_t                    id      = 0;
    tSynthLibParamPoint         point;

    point.sampleOffset = offset;

    switch (e->status & 0xF0u) {
        case 0x90:      // note on - and a note on at zero velocity is a note off, as it is on the wire
            if ((e->data2 > 0u) && (d->cb.noteOn != NULL)) {
                d->cb.noteOn(au->inst, channel, e->data1, (float)e->data2 / 127.0f, offset);
            } else if (d->cb.noteOff != NULL) {
                d->cb.noteOff(au->inst, channel, e->data1, 0.0f, offset);
            }
            break;

        case 0x80:      // note off
            if (d->cb.noteOff != NULL) {
                d->cb.noteOff(au->inst, channel, e->data1, (float)e->data2 / 127.0f, offset);
            }
            break;

        case 0xA0:      // POLYPHONIC key pressure, which is not channel pressure and not a controller
            if (d->cb.polyPressure != NULL) {
                d->cb.polyPressure(au->inst, channel, e->data1, (float)e->data2 / 127.0f, offset);
            }
            break;

        case 0xB0:      // continuous controller
            if (map_control(au, channel, (int16_t)e->data1, &id) == true) {
                point.value = (double)e->data2 / 127.0;
                deliver_points(au, id, &point, 1u);
            }

            // ALL NOTES OFF and ALL SOUND OFF, which a host sends on a transport stop and which a
            // plug-in that ignores them leaves droning.
            if (((e->data1 == 120u) || (e->data1 == 123u)) && (d->cb.reset != NULL)) {
                d->cb.reset(au->inst);
            }
            break;

        case 0xD0:      // channel pressure
            if (map_control(au, channel, SYNTHLIB_MIDI_AFTERTOUCH, &id) == true) {
                point.value = (double)e->data1 / 127.0;
                deliver_points(au, id, &point, 1u);
            }
            break;

        case 0xE0:      // pitch bend, 14 bits across the two data bytes, 0x2000 at rest
            if (map_control(au, channel, SYNTHLIB_MIDI_PITCH_BEND, &id) == true) {
                point.value = (double)((uint32_t)e->data1 | ((uint32_t)e->data2 << 7)) / 16383.0;
                deliver_points(au, id, &point, 1u);
            }
            break;

        default:
            break;
    }
}

// EVERYTHING QUEUED SINCE THE LAST RENDER, in order, now that this one's position is known. Offsets
// past the end of the block are pulled back to its last frame: a host scheduling for the next render
// has already missed this one, and early is better than dropped.
static void drain_events(tSynthLibAu * au, uint32_t frames) {
    uint32_t read  = atomic_load_explicit(&au->eventRead, memory_order_relaxed);
    uint32_t write = atomic_load_explicit(&au->eventWrite, memory_order_acquire);
    uint32_t last  = (frames > 0u) ? (frames - 1u) : 0u;

    while (read != write) {
        const tQueuedEvent * e = &au->events[read % EVENT_QUEUE_SIZE];

        if (e->isParam == false) {
            deliver_midi(au, e, (e->offset < last) ? e->offset : last);
            read++;
            continue;
        }
        // A RUN OF ONE PARAMETER'S POINTS goes over as one call, so a plug-in that looks for a press
        // among a block's points - GenBridge's Measure - sees a press and its release together.
        tSynthLibParamPoint points[MAX_POINTS];
        uint32_t            count = 0;
        uint32_t            id    = e->id;

        while ((read != write) && au->events[read % EVENT_QUEUE_SIZE].isParam &&
               (au->events[read % EVENT_QUEUE_SIZE].id == id)) {
            const tQueuedEvent * p = &au->events[read % EVENT_QUEUE_SIZE];

            if (count == MAX_POINTS) {
                count = MAX_POINTS - 1u;
            }
            points[count].sampleOffset = (p->offset < last) ? p->offset : last;
            points[count].value        = p->value;
            count++;
            read++;
        }
        deliver_points(au, id, points, count);
    }
    atomic_store_explicit(&au->eventRead, read, memory_order_release);
}

static OSStatus ensure_owned_buffer(tSynthLibAu * au, UInt32 frames) {
    uint32_t channels = output_channels(au->desc);

    if (au->ownedFrames >= frames) {
        return noErr;
    }
    float * block = (float *)realloc(au->ownedSamples, (size_t)frames * channels * sizeof(float));

    if (block == NULL) {
        return kAudioUnitErr_FailedInitialization;
    }
    au->ownedSamples = block;
    au->ownedFrames  = frames;
    return noErr;
}

// PULLING THE INPUT, for an effect. A host either installs a render callback or connects another
// unit's output to ours, and either way the samples do not arrive - they are fetched, from here,
// during our own render.
static OSStatus pull_input(tSynthLibAu * au, AudioUnitRenderActionFlags * flags,
                           const AudioTimeStamp * ts, UInt32 frames) {
    uint32_t channels = input_channels(au->desc);

    if ((channels == 0u) || ((au->haveInputCallback == false) && (au->inputSourceUnit == NULL))) {
        return noErr;       // nothing connected, which is legal and common
    }

    if (au->inputFrames < frames) {
        float * block = (float *)realloc(au->inputSamples, (size_t)frames * channels * sizeof(float));

        if (block == NULL) {
            return kAudioUnitErr_FailedInitialization;
        }
        au->inputSamples = block;
        au->inputFrames  = frames;
    }

    for (UInt32 c = 0; c < channels; c++) {
        au->inputList->mBuffers[c].mNumberChannels = 1;
        au->inputList->mBuffers[c].mDataByteSize   = frames * (UInt32)sizeof(float);
        au->inputList->mBuffers[c].mData           = au->inputSamples + ((size_t)c * au->inputFrames);
    }
    au->inputList->mNumberBuffers = channels;

    AudioUnitRenderActionFlags inFlags = (flags != NULL) ? *flags : 0;
    OSStatus                   err;

    if (au->haveInputCallback == true) {
        err = au->inputCallback.inputProc(au->inputCallback.inputProcRefCon, &inFlags,
                                          ts, 0, frames, au->inputList);
    } else {
        err = AudioUnitRender(au->inputSourceUnit, &inFlags, ts,
                              au->inputSourceBus, frames, au->inputList);
    }

    if (err != noErr) {
        return err;
    }

    for (UInt32 c = 0; c < channels; c++) {
        au->inputChannels[c] = (const float *)au->inputList->mBuffers[c].mData;
    }
    return noErr;
}

// notes §9
static void fill_transport(tSynthLibAu * au, const AudioTimeStamp * ts, tSynthLibTransport * out) {
    memset(out, 0, sizeof(*out));
    out->sampleRate = au->sampleRate;

    // THE TIMESTAMP IS THE ONE THING EVERY HOST HANDS OVER, with the render itself - and its host
    // time is exactly the field VST3's Live never fills in.
    if (ts != NULL) {
        if ((ts->mFlags & kAudioTimeStampHostTimeValid) != 0u) {
            out->systemTimeValid = true;
            out->systemTime      = AudioConvertHostTimeToNanos(ts->mHostTime);
        }

        if ((ts->mFlags & kAudioTimeStampSampleTimeValid) != 0u) {
            out->continuousTimeValid   = true;
            out->continuousTimeSamples = (int64_t)ts->mSampleTime;
        }
    }

    if ((au->desc->wantsTransport == false) || (au->haveHostCallbacks == false)) {
        return;             // valid stays false, which is the whole point of the flag
    }
    HostCallbackInfo * cb = &au->hostCallbacks;

    if (cb->beatAndTempoProc != NULL) {
        Float64 beat  = 0.0;
        Float64 tempo = 0.0;

        if (cb->beatAndTempoProc(cb->hostUserData, &beat, &tempo) == noErr) {
            out->valid            = true;
            out->musicTimeValid   = true;
            out->projectTimeMusic = (double)beat;
            out->tempoValid       = (tempo > 0.0);
            out->tempo            = (double)tempo;
        }
    }

    if (cb->transportStateProc2 != NULL) {
        Boolean playing   = false;
        Boolean recording = false;
        Boolean changed   = false;
        Float64 sample    = 0.0;
        Boolean cycling   = false;
        Float64 cycleStart = 0.0;
        Float64 cycleEnd   = 0.0;

        if (cb->transportStateProc2(cb->hostUserData, &playing, &recording, &changed,
                                    &sample, &cycling, &cycleStart, &cycleEnd) == noErr) {
            out->valid              = true;
            out->playing            = (playing != false);
            out->recording          = (recording != false);
            out->cycleActive        = (cycling != false);
            out->cycleValid         = true;
            out->cycleStartMusic    = (double)cycleStart;
            out->cycleEndMusic      = (double)cycleEnd;
            out->projectTimeSamples = (int64_t)sample;
        }
    } else if (cb->transportStateProc != NULL) {
        Boolean playing = false;
        Boolean changed = false;
        Float64 sample  = 0.0;
        Boolean cycling = false;
        Float64 cycleStart = 0.0;
        Float64 cycleEnd   = 0.0;

        if (cb->transportStateProc(cb->hostUserData, &playing, &changed, &sample,
                                   &cycling, &cycleStart, &cycleEnd) == noErr) {
            out->valid              = true;
            out->playing            = (playing != false);
            out->cycleActive        = (cycling != false);
            out->cycleValid         = true;
            out->cycleStartMusic    = (double)cycleStart;
            out->cycleEndMusic      = (double)cycleEnd;
            out->projectTimeSamples = (int64_t)sample;
        }
    }

    if (cb->musicalTimeLocationProc != NULL) {
        UInt32  offset   = 0;
        Float32 numerator = 0.0f;      // a time signature's top half is a FLOAT here, its bottom an int
        UInt32  denominator = 0;
        Float64 downBeat = 0.0;

        if (cb->musicalTimeLocationProc(cb->hostUserData, &offset, &numerator,
                                        &denominator, &downBeat) == noErr) {
            out->valid              = true;
            out->barPositionValid   = true;
            out->barPositionMusic   = (double)downBeat;
            out->timeSigValid       = (numerator > 0.0f) && (denominator > 0u);
            out->timeSigNumerator   = (int32_t)(numerator + 0.5f);
            out->timeSigDenominator = (int32_t)denominator;
        }
    }
}

static OSStatus au_render(void * self, AudioUnitRenderActionFlags * ioActionFlags,
                          const AudioTimeStamp * inTimeStamp, UInt32 inBusNumber,
                          UInt32 inNumberFrames, AudioBufferList * ioData) {
    tSynthLibAu *               au       = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d        = au->desc;
    AudioUnitRenderActionFlags  flags    = (ioActionFlags != NULL) ? *ioActionFlags : 0;
    uint32_t                    channels = output_channels(d);
    OSStatus                    err      = noErr;

    if (au->initialized == false) {
        au->lastRenderError = kAudioUnitErr_Uninitialized;
        return kAudioUnitErr_Uninitialized;
    }

    if ((ioData == NULL) || (inBusNumber != 0)) {
        au->lastRenderError = kAudioUnitErr_InvalidElement;
        return kAudioUnitErr_InvalidElement;
    }

    if (inNumberFrames > au->maxFrames) {
        au->lastRenderError = kAudioUnitErr_TooManyFramesToProcess;
        return kAudioUnitErr_TooManyFramesToProcess;
    }

    if (ioData->mNumberBuffers < channels) {
        au->lastRenderError = kAudioUnitErr_FormatNotSupported;
        return kAudioUnitErr_FormatNotSupported;
    }

    // Pre-render notifications, which is how a host taps the signal ahead of us.
    for (UInt32 i = 0; i < au->renderNotifyCount; i++) {
        AudioUnitRenderActionFlags f = flags | kAudioUnitRenderAction_PreRender;

        au->renderNotify[i].proc(au->renderNotify[i].userData, &f, inTimeStamp,
                                 inBusNumber, inNumberFrames, ioData);
    }

    // A HOST MAY HAND OVER NULL BUFFERS and expect us to supply our own. That is legal, and a
    // plug-in that dereferences them crashes the host rather than the other way round - so an
    // internal block stands in and the pointers are written back for the host to read.
    for (UInt32 c = 0; c < channels; c++) {
        if (ioData->mBuffers[c].mData == NULL) {
            err = ensure_owned_buffer(au, inNumberFrames);

            if (err != noErr) {
                au->lastRenderError = err;
                return err;
            }
            ioData->mBuffers[c].mData           = au->ownedSamples + ((size_t)c * au->ownedFrames);
            ioData->mBuffers[c].mDataByteSize   = inNumberFrames * (UInt32)sizeof(float);
            ioData->mBuffers[c].mNumberChannels = 1;
        }
        au->channels[c] = (float *)ioData->mBuffers[c].mData;
    }
    uint32_t numIn = 0;

    if (input_channels(d) > 0u) {
        err = pull_input(au, ioActionFlags, inTimeStamp, inNumberFrames);

        if (err == noErr) {
            numIn = ((au->haveInputCallback == true) || (au->inputSourceUnit != NULL))
                    ? input_channels(d) : 0u;
        }
        // A FAILED PULL IS NOT A FAILED RENDER. The host is told nothing arrived - numIn stays 0 -
        // and the plug-in produces whatever it produces with no input, which for every plug-in here
        // is exactly what it produces anyway.
        err = noErr;
    }
    tSynthLibTransport transport;

    fill_transport(au, inTimeStamp, &transport);

    // THE SAME ORDER AS THE VST3 WRAPPER'S process(): where the block is, then what happened in it,
    // then the audio.
    if (d->cb.blockBegin != NULL) {
        d->cb.blockBegin(au->inst, inNumberFrames, &transport);
    }
    drain_events(au, inNumberFrames);

    if (au->bypassed == true) {
        // BYPASSED: the input straight through, which is what a host's bypass button promises. The
        // plug-in still hears where the block is and what changed in it above - its state stays
        // current - it simply does not get to touch the audio. No input connected is silence.
        for (UInt32 c = 0; c < channels; c++) {
            const float * source = (numIn > 0u) ? au->inputChannels[(c < numIn) ? c : (numIn - 1u)] : NULL;

            if (source == NULL) {
                memset(au->channels[c], 0, (size_t)inNumberFrames * sizeof(float));
            } else if (source != au->channels[c]) {
                memcpy(au->channels[c], source, (size_t)inNumberFrames * sizeof(float));
            }
        }
    } else if (d->cb.process != NULL) {
        d->cb.process(au->inst, (numIn > 0u) ? au->inputChannels : NULL, numIn,
                      au->channels, channels, inNumberFrames, &transport);
    }

    // NOT SILENT, and saying so matters: the flag arrives set on some hosts, and leaving it set
    // tells the host it may skip everything downstream of us.
    if (ioActionFlags != NULL) {
        *ioActionFlags &= (AudioUnitRenderActionFlags)~kAudioUnitRenderAction_OutputIsSilence;
    }

    for (UInt32 i = 0; i < au->renderNotifyCount; i++) {
        AudioUnitRenderActionFlags f = flags | kAudioUnitRenderAction_PostRender;

        au->renderNotify[i].proc(au->renderNotify[i].userData, &f, inTimeStamp,
                                 inBusNumber, inNumberFrames, ioData);
    }
    au->lastRenderError = noErr;
    return noErr;
}

static OSStatus au_reset(void * self, AudioUnitScope scope, AudioUnitElement element) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = au->desc;

    (void)scope;
    (void)element;

    if ((au->inst != NULL) && (d->cb.reset != NULL)) {
        d->cb.reset(au->inst);
    }
    return noErr;
}

// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------

static OSStatus au_initialize(void * self) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = au->desc;

    if (au->inst == NULL) {
        return kAudioUnitErr_FailedInitialization;
    }

    if (d->cb.setSampleRate != NULL) {
        d->cb.setSampleRate(au->inst, au->sampleRate);
    }
    send_prepare(au);

    // GOING ACTIVE IS WHERE THE ENGINE STARTS, not where the instance was made - see the note on
    // setActive in synthlibPlugin.h. Doing this at construction time is the mistake that had the
    // VST3 build render silence from a perfectly good patch.
    if (d->cb.setActive != NULL) {
        d->cb.setActive(au->inst, true);
    }

    // AN AUDIO UNIT HAS NO setProcessing(): a host keeps a unit initialised for as long as it may
    // render, so initialisation is the nearest thing to "blocks are about to start arriving".
    if (d->cb.setProcessing != NULL) {
        d->cb.setProcessing(au->inst, true);
    } else if (d->cb.reset != NULL) {
        d->cb.reset(au->inst);
    }
    au->initialized = true;
    return noErr;
}

static OSStatus au_uninitialize(void * self) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = au->desc;

    au->initialized = false;

    if ((au->inst != NULL) && (d->cb.setProcessing != NULL)) {
        d->cb.setProcessing(au->inst, false);
    }

    if ((au->inst != NULL) && (d->cb.setActive != NULL)) {
        d->cb.setActive(au->inst, false);
    }
    return noErr;
}

static AudioComponentMethod au_lookup_common(SInt16 selector) {
    switch (selector) {
        case kAudioUnitInitializeSelect:            return (AudioComponentMethod)au_initialize;
        case kAudioUnitUninitializeSelect:          return (AudioComponentMethod)au_uninitialize;
        case kAudioUnitGetPropertyInfoSelect:       return (AudioComponentMethod)au_get_property_info;
        case kAudioUnitGetPropertySelect:           return (AudioComponentMethod)au_get_property;
        case kAudioUnitSetPropertySelect:           return (AudioComponentMethod)au_set_property;
        case kAudioUnitAddPropertyListenerSelect:   return (AudioComponentMethod)au_add_property_listener;
        case kAudioUnitRemovePropertyListenerSelect: return (AudioComponentMethod)au_remove_property_listener;
        case kAudioUnitRemovePropertyListenerWithUserDataSelect:
            return (AudioComponentMethod)au_remove_property_listener_with_user_data;
        case kAudioUnitAddRenderNotifySelect:       return (AudioComponentMethod)au_add_render_notify;
        case kAudioUnitRemoveRenderNotifySelect:    return (AudioComponentMethod)au_remove_render_notify;
        case kAudioUnitGetParameterSelect:          return (AudioComponentMethod)au_get_parameter;
        case kAudioUnitSetParameterSelect:          return (AudioComponentMethod)au_set_parameter;
        case kAudioUnitScheduleParametersSelect:    return (AudioComponentMethod)au_schedule_parameters;
        case kAudioUnitRenderSelect:                return (AudioComponentMethod)au_render;
        case kAudioUnitResetSelect:                 return (AudioComponentMethod)au_reset;
        default:                                    return NULL;
    }
}

// notes §10
static AudioComponentMethod au_lookup_effect(SInt16 selector) {
    return au_lookup_common(selector);
}

static AudioComponentMethod au_lookup_music(SInt16 selector) {
    switch (selector) {
        case kMusicDeviceMIDIEventSelect: return (AudioComponentMethod)au_midi_event;
        case kMusicDeviceStartNoteSelect: return (AudioComponentMethod)au_start_note;
        case kMusicDeviceStopNoteSelect:  return (AudioComponentMethod)au_stop_note;
        default:                          return au_lookup_common(selector);
    }
}

static OSStatus au_open(void * self, AudioComponentInstance ci) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = au->desc;

    au->ci = ci;

    if (d->cb.create != NULL) {
        au->inst = d->cb.create(d);
    }

    if (au->inst == NULL) {
        return kAudioUnitErr_FailedInitialization;
    }

    if (d->cb.initialize != NULL) {
        d->cb.initialize(au->inst);
    }

    // THE PARAMETER STORE IS BUILT NOW, with an instance to ask, and every value starts at its
    // default - a parameter left at zero that means "full bend down" is a plug-in reporting a state
    // it is not in.
    if (synthlib_params_init(&au->params, d, au->inst) == false) {
        return kAudioUnitErr_FailedInitialization;
    }
    register_au(au);
    return noErr;
}

static OSStatus au_close(void * self) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = au->desc;

    // OUT OF THE TABLE FIRST, so nothing the plug-in posted to the main thread finds a unit that is
    // going.
    unregister_au(au);

    if (au->inst != NULL) {
        if (d->cb.terminate != NULL) {
            d->cb.terminate(au->inst);
        }

        if (d->cb.destroy != NULL) {
            d->cb.destroy(au->inst);
        }
        au->inst = NULL;
    }

    if (au->currentPreset.presetName != NULL) {
        CFRelease(au->currentPreset.presetName);
    }
    synthlib_params_free(&au->params);
    free(au->channels);
    free(au->ownedSamples);
    free(au->inputSamples);
    free(au->inputList);
    free(au->inputChannels);

    // THE FACTORY'S ALLOCATION IS FREED HERE AND NOWHERE ELSE. `self` IS that allocation - the
    // interface is its first member - so this is the last line that may touch it.
    free(au);
    return noErr;
}

// ------------------------------------------------------------------------------------------------
// What the plug-in may ask of us
// ------------------------------------------------------------------------------------------------

typedef struct {
    void *   inst;
    uint32_t id;
    double   value;
    bool     latency;
} tHostPost;

// ON THE MAIN THREAD, and possibly some time after the plug-in asked - so the unit is found again
// here, from scratch. If it has been closed in the meantime there is nobody left to tell.
static void deliver_to_host(void * ctx) {
    tHostPost *   post = (tHostPost *)ctx;
    tSynthLibAu * au   = au_for(post->inst);

    if (au != NULL) {
        if (post->latency == true) {
            notify_listeners(au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
        } else {
            apply_param(au, (AudioUnitParameterID)post->id, post->value);

            // BEGIN, CHANGE, END, as one gesture - the Audio Unit spelling of VST3's
            // beginEdit/performEdit/endEdit, and what lets a host record the move as automation
            // rather than watch a value appear from nowhere.
            AudioUnitEvent event;

            memset(&event, 0, sizeof(event));
            event.mArgument.mParameter.mAudioUnit   = au->ci;
            event.mArgument.mParameter.mParameterID = (AudioUnitParameterID)post->id;
            event.mArgument.mParameter.mScope       = kAudioUnitScope_Global;
            event.mArgument.mParameter.mElement     = 0;

            event.mEventType = kAudioUnitEvent_BeginParameterChangeGesture;
            AUEventListenerNotify(NULL, NULL, &event);

            event.mEventType = kAudioUnitEvent_ParameterValueChange;
            AUEventListenerNotify(NULL, NULL, &event);

            event.mEventType = kAudioUnitEvent_EndParameterChangeGesture;
            AUEventListenerNotify(NULL, NULL, &event);
        }
    }
    free(post);
}

static void post_to_host(void * inst, uint32_t id, double value, bool latency) {
    tHostPost * post;

    if (inst == NULL) {
        return;
    }
    post = (tHostPost *)malloc(sizeof(tHostPost));

    if (post == NULL) {
        return;
    }
    post->inst    = inst;
    post->id      = id;
    post->value   = synthlib_param_clamp(value);
    post->latency = latency;
    synthlib_run_on_main(deliver_to_host, post);
}

void synthlib_plugin_param_edited(void * inst, uint32_t id, double normalized) {
    post_to_host(inst, id, normalized, false);
}

void synthlib_plugin_latency_changed(void * inst) {
    post_to_host(inst, 0u, 0.0, true);
}

double synthlib_plugin_param_value(void * inst, uint32_t id) {
    tSynthLibAu * au = au_for(inst);

    return (au != NULL) ? synthlib_params_get(&au->params, id) : 0.0;
}

// AN AUDIO UNIT IS ONE OBJECT, so there is no channel to cross - the message goes straight to the
// callback. VST3 needs IConnectionPoint and an IMessage the host has to allocate; here the two
// halves the message was invented to join are the same pointer.
bool synthlib_plugin_send_message(void * inst, const char * id, int64_t value) {
    tSynthLibAu * au = au_for(inst);

    if ((au == NULL) || (au->desc->cb.message == NULL) || (id == NULL)) {
        return false;
    }
    au->desc->cb.message(inst, id, value);
    return true;
}

// notes §11
bool synthlib_plugin_request_resize(void * inst, double width, double height) {
    (void)inst;
    (void)width;
    (void)height;
    return false;
}

// ------------------------------------------------------------------------------------------------
// The factory - the one exported symbol, named in the .component's Info.plist
// ------------------------------------------------------------------------------------------------

__attribute__((visibility("default")))
AudioComponentPlugInInterface * SynthLibAUFactory(const AudioComponentDescription * inDesc);

AudioComponentPlugInInterface * SynthLibAUFactory(const AudioComponentDescription * inDesc) {
    // WHICH OF OUR PLUG-INS THIS IS. macOS names it by subtype, out of our own Info.plist.
    const tSynthLibPluginDesc * d = (inDesc != NULL) ? variant_for(inDesc->componentSubType)
                                                     : &variants()->variants[0];

    if (d == NULL) {
        return NULL;
    }
    uint32_t outChannels = output_channels(d);
    uint32_t inChannels  = input_channels(d);

    if (outChannels == 0u) {
        return NULL;
    }
    tSynthLibAu * au = (tSynthLibAu *)calloc(1, sizeof(tSynthLibAu));

    if (au == NULL) {
        return NULL;
    }
    au->iface.Open     = au_open;
    au->iface.Close    = au_close;
    au->iface.Lookup   = ((d->isInstrument == true) || (d->wantsMidiIn == true)) ? au_lookup_music
                                                                               : au_lookup_effect;
    au->iface.reserved = NULL;

    au->desc       = d;
    au->sampleRate = 44100.0;
    au->maxFrames  = DEFAULT_MAX_FRAMES;
    au->channels   = (float **)calloc(outChannels, sizeof(float *));
    atomic_init(&au->eventWrite, 0u);
    atomic_init(&au->eventRead, 0u);
    atomic_flag_clear(&au->eventProducer);

    if (au->channels == NULL) {
        free(au);
        return NULL;
    }

    if (inChannels > 0u) {
        // One AudioBuffer more than the struct declares, per extra channel.
        size_t listBytes = sizeof(AudioBufferList) + ((size_t)(inChannels - 1u) * sizeof(AudioBuffer));

        au->inputList     = (AudioBufferList *)calloc(1, listBytes);
        au->inputChannels = (const float **)calloc(inChannels, sizeof(const float *));

        if ((au->inputList == NULL) || (au->inputChannels == NULL)) {
            free(au->inputList);
            free(au->inputChannels);
            free(au->channels);
            free(au);
            return NULL;
        }
    }
    au->currentPreset.presetNumber = 0;
    au->currentPreset.presetName   = CFStringCreateCopy(NULL, CFSTR("Default"));

    // THE ADDRESS OF THE INTERFACE IS THE ADDRESS OF THE STRUCT, because it is the first member -
    // which is what lets every method cast its `self` straight back to a tSynthLibAu *.
    return &au->iface;
}
