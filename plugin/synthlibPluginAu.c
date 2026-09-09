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

// THE AUDIO UNIT SIDE, AND NOTHING ELSE. Everything specific to a particular plug-in arrives through
// synthlib_plugin_descriptor(), exactly as it does for the VST3 wrapper beside this.
//
// AUv2, NOT AUv3, and the reason is distribution. An AUv3 is an app EXTENSION: it has to be embedded
// in a containing .app, installed under /Applications and launched once so that pluginkit registers
// it, and an ad-hoc-signed extension - which is all an unpaid Apple membership can produce - does
// not survive that reliably on somebody else's machine. An AUv2 is a plain bundle wrapped around one
// dylib, which is exactly what the .vst3 already is, so it ships in the same .dmg with the same
// "clear the quarantine flag" instructions and no new machinery at all.
//
// WRITTEN AGAINST THE C API DIRECTLY. Apple's AUBase and the CoreAudio Utility Classes are not used,
// for the same reason the VST3 wrapper does not use public.sdk: they bring a build system with them,
// and everything they do is one dispatch table and some property plumbing. A component's factory
// hands back an AudioComponentPlugInInterface with three function pointers; Lookup() turns a
// selector into a method, and every method's first argument is the pointer the factory returned. All
// of that is below, in the open.
//
// INSTRUMENTS ONLY, SO FAR. A plug-in with an audio INPUT needs two more things a host uses to feed
// it - kAudioUnitProperty_SetRenderCallback and kAudioUnitProperty_MakeConnection, and an
// AudioUnitRender() pull in au_render() - and they are deliberately absent rather than written
// blind: nothing in these projects has an Audio Unit with an input yet, and untested plumbing that
// only looks right is worse than a property that honestly answers "not supported". Add them when the
// first effect arrives, against a real host.

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "synthlibPlugin.h"
#include "synthlibPluginAu.h"
#include "synthlibPluginState.h"

#define MAX_LISTENERS        (32)
#define MAX_RENDER_NOTIFY    (8)

// A host must tell us its slice size before rendering, but auval and a few hosts render before
// setting it. This is what MaximumFramesPerSlice reports until then.
#define DEFAULT_MAX_FRAMES   (4096)

typedef struct {
    AudioUnitPropertyID          id;
    AudioUnitPropertyListenerProc proc;
    void *                       userData;
} tPropertyListener;

typedef struct {
    AURenderCallback proc;
    void *           userData;
} tRenderNotify;

typedef struct {
    // FIRST, AND IT MUST STAY FIRST. The pointer the factory returns is the address of this member,
    // and every method the host calls is handed that same pointer back as `self`.
    AudioComponentPlugInInterface iface;

    AudioComponentInstance ci;
    void *                 inst;

    double                 sampleRate;
    UInt32                 maxFrames;
    bool                   initialized;

    double *               params;          // normalized, one per descriptor parameter

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
} tSynthLibAu;

// The sole instance, for synthlib_plugin_param_edited(). Same limit, and the same reason, as the
// VST3 wrapper's registry: the engines behind these plug-ins keep their state in process-wide
// globals, so a second instance in one host would fight the first over the same engine whatever
// this pointer said. With two loaded this goes NULL and an editor's moves simply do not reach the
// host's automation - which is the honest outcome, rather than reaching the wrong instance.
static tSynthLibAu * gSoleAu       = NULL;
static int           gInstanceCount = 0;

static const tSynthLibPluginDesc * desc(void) {
    static const tSynthLibPluginDesc * d = NULL;

    if (d == NULL) {
        d = synthlib_plugin_descriptor();
    }
    return d;
}

// ------------------------------------------------------------------------------------------------
// Small conversions
// ------------------------------------------------------------------------------------------------

static AudioUnitParameterUnit au_unit_of(tSynthLibParamUnit unit) {
    switch (unit) {
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
    const tSynthLibPluginDesc * d = desc();

    if (id >= d->numParams) {
        return;
    }

    if (normalized < 0.0) {
        normalized = 0.0;
    } else if (normalized > 1.0) {
        normalized = 1.0;
    }
    au->params[id] = normalized;

    if ((au->inst != NULL) && (d->cb.setParam != NULL)) {
        d->cb.setParam(au->inst, (uint32_t)id, normalized);
    }
}

static void restore_defaults(tSynthLibAu * au) {
    const tSynthLibPluginDesc * d = desc();

    for (uint32_t i = 0; i < d->numParams; i++) {
        apply_param(au, (AudioUnitParameterID)i, d->params[i].defaultNormalized);
    }
}

// ------------------------------------------------------------------------------------------------
// State - the same bytes the VST3 wrapper writes, wrapped in the dictionary a host expects
// ------------------------------------------------------------------------------------------------

static CFDictionaryRef copy_class_info(tSynthLibAu * au) {
    const tSynthLibPluginDesc * d    = desc();
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
    size_t need = synthlib_state_write(desc(), au->inst, au->params, NULL, 0);

    if (need > 0) {
        uint8_t * bytes = (uint8_t *)malloc(need);

        if (bytes != NULL) {
            size_t     wrote = synthlib_state_write(desc(), au->inst, au->params, bytes, need);
            CFDataRef  data  = CFDataCreate(NULL, bytes, (CFIndex)wrote);

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
    const tSynthLibPluginDesc * d = desc();

    if (dict == NULL) {
        return kAudioUnitErr_InvalidPropertyValue;
    }
    CFDataRef data = (CFDataRef)CFDictionaryGetValue(dict, CFSTR(kAUPresetDataKey));

    if ((data == NULL) || (CFGetTypeID(data) != CFDataGetTypeID())) {
        return noErr;       // a dictionary with no data of ours in it is not an error, just nothing to do
    }
    double *     values     = (double *)calloc(d->numParams ? d->numParams : 1, sizeof(double));
    uint32_t     count      = 0;
    const void * pluginData = NULL;
    size_t       pluginLen  = 0;

    if (values == NULL) {
        return kAudioUnitErr_FailedInitialization;
    }

    if (synthlib_state_read(d, CFDataGetBytePtr(data), (size_t)CFDataGetLength(data),
                            values, &count, &pluginData, &pluginLen) == true) {
        for (uint32_t i = 0; i < count; i++) {
            apply_param(au, (AudioUnitParameterID)i, values[i]);
        }

        // THE PLUG-IN'S OWN STATE LAST, for the reason the VST3 wrapper gives: loading it rebuilds
        // whatever the parameters feed into, so they have to already be where the project left them.
        if ((au->inst != NULL) && (d->cb.setState != NULL)) {
            d->cb.setState(au->inst, pluginData, pluginLen);
        }
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
// Properties
// ------------------------------------------------------------------------------------------------

static UInt32 element_count(AudioUnitScope scope) {
    const tSynthLibPluginDesc * d = desc();

    switch (scope) {
        case kAudioUnitScope_Global: return 1;
        case kAudioUnitScope_Input:  return (d->numInputChannels > 0) ? 1 : 0;
        case kAudioUnitScope_Output: return (d->numOutputChannels > 0) ? 1 : 0;
        case kAudioUnitScope_Group:  return d->isInstrument ? 1 : 0;
        default:                     return 0;
    }
}

// PROPERTIES THAT EXIST ON THE GLOBAL SCOPE AND NOWHERE ELSE. Answering them on the output, group
// and part scopes as well is not harmless: auval reports each one as "returning valid information
// for scope/element ... which should be invalid", and a host is entitled to conclude that a latency
// or a tail time it read off the output scope means something.
static bool is_global_only(AudioUnitPropertyID id) {
    switch (id) {
        case kAudioUnitProperty_ParameterInfo:
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
        case kAudioUnitProperty_SupportedNumChannels:
        case kAudioUnitProperty_MaximumFramesPerSlice:
        case kAudioUnitProperty_LastRenderError:
        case kAudioUnitProperty_FactoryPresets:
        case kAudioUnitProperty_PresentPreset:
        case kAudioUnitProperty_CocoaUI:
        case kAudioUnitProperty_ParameterStringFromValue:
        case kMusicDeviceProperty_InstrumentCount:
        case kSynthLibAuProperty_Instance:
            return true;

        default:
            return false;
    }
}

static OSStatus au_get_property_info(void * self, AudioUnitPropertyID id, AudioUnitScope scope,
                                     AudioUnitElement element, UInt32 * outSize, Boolean * outWritable) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = desc();
    UInt32                      size     = 0;
    Boolean                     writable = false;

    (void)au;

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
            // GLOBAL SCOPE ONLY, AND AN EMPTY LIST EVERYWHERE ELSE. Returning the same list for
            // every scope told auval there were parameters on the input and output scopes too; it
            // then read one of them back with AudioUnitGetParameter, which answers kInvalidScope,
            // and the parameter test failed on a plug-in whose parameters are all fine.
            size = (scope == kAudioUnitScope_Global)
                   ? (UInt32)(d->numParams * sizeof(AudioUnitParameterID))
                   : 0;
            break;

        case kAudioUnitProperty_ParameterInfo:
            if (element >= d->numParams) {
                return kAudioUnitErr_InvalidElement;
            }
            size = sizeof(AudioUnitParameterInfo);
            break;

        case kAudioUnitProperty_StreamFormat:
            if (element_count(scope) == 0) {
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

        // NO kAudioUnitProperty_SupportedChannelLayoutTags AND NO AudioChannelLayout, deliberately.
        // A plug-in that advertises supported layout tags is then required to answer
        // kAudioUnitProperty_AudioChannelLayout for every scope it claimed, and auval checks the
        // INPUT scope even on an instrument that has no input - which failed with "Cannot verify
        // Audio Channel Layouts as Format handling has problems". Plain stereo needs neither: the
        // channel count in the stream format says everything there is to say about it.

        case kMusicDeviceProperty_InstrumentCount:
            if (d->isInstrument == false) {
                return kAudioUnitErr_InvalidProperty;
            }
            size = sizeof(UInt32);
            break;

        case kSynthLibAuProperty_Instance:
            size = sizeof(void *);
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
    const tSynthLibPluginDesc * d  = desc();
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

            for (uint32_t i = 0; i < d->numParams; i++) {
                ids[i] = (AudioUnitParameterID)d->params[i].id;
            }
            break;
        }

        case kAudioUnitProperty_ParameterInfo: {
            const tSynthLibParam *  p    = synthlib_param_at(d, (uint32_t)element);
            AudioUnitParameterInfo * info = (AudioUnitParameterInfo *)outData;

            if (p == NULL) {
                return kAudioUnitErr_InvalidElement;
            }
            memset(info, 0, sizeof(*info));
            info->unit         = au_unit_of(p->unit);
            info->minValue     = (AudioUnitParameterValue)p->plainMin;
            info->maxValue     = (AudioUnitParameterValue)p->plainMax;
            info->defaultValue = (AudioUnitParameterValue)synthlib_param_to_plain(d, p->id,
                                                                                  p->defaultNormalized);
            info->cfNameString = CFStringCreateWithCString(NULL, p->title, kCFStringEncodingUTF8);

            // CFNameRelease says the caller owns the string we just made. Without it the host leaks
            // one CFString per parameter per query, and a host queries often.
            info->flags = kAudioUnitParameterFlag_IsReadable |
                          kAudioUnitParameterFlag_IsWritable |
                          kAudioUnitParameterFlag_HasCFNameString |
                          kAudioUnitParameterFlag_CFNameRelease |
                          kAudioUnitParameterFlag_IsHighResolution;

            // The 52-byte name is the legacy field, filled as well as the CFString: some hosts and
            // every old one read only this.
            strncpy(info->name, p->title, sizeof(info->name) - 1);
            break;
        }

        case kAudioUnitProperty_StreamFormat: {
            UInt32 channels = (scope == kAudioUnitScope_Input) ? d->numInputChannels
                                                               : d->numOutputChannels;

            fill_stream_format((AudioStreamBasicDescription *)outData, au->sampleRate, channels);
            break;
        }

        case kAudioUnitProperty_ElementCount:
            *(UInt32 *)outData = element_count(scope);
            break;

        case kAudioUnitProperty_Latency:
            *(Float64 *)outData = 0.0;
            break;

        case kAudioUnitProperty_TailTime:
            // THE SAME ANSWER AS THE VST3 SIDE'S kInfiniteTail, said the way an Audio Unit says it.
            // There are reverbs and delays in these engines, so a host must not decide the plug-in
            // has finished the moment the notes stop.
            *(Float64 *)outData = 10.0;
            break;

        case kAudioUnitProperty_SupportedNumChannels: {
            AUChannelInfo * info = (AUChannelInfo *)outData;

            info->inChannels  = (SInt16)d->numInputChannels;
            info->outChannels = (SInt16)d->numOutputChannels;
            break;
        }

        case kAudioUnitProperty_MaximumFramesPerSlice:
            *(UInt32 *)outData = au->maxFrames;
            break;

        case kAudioUnitProperty_LastRenderError:
            *(OSStatus *)outData  = au->lastRenderError;
            au->lastRenderError   = noErr;      // reading it clears it, as a host expects
            break;

        case kAudioUnitProperty_FactoryPresets: {
            // ONE PRESET, "Default", and selecting it restores the parameter defaults. A host with
            // an empty preset menu looks broken in the same way a host with no parameters does.
            //
            // THE ARRAY HOLDS POINTERS TO AUPreset STRUCTS, not CFTypes. That is what the property
            // is documented to be, so the callbacks are NULL and the structs have to outlive the
            // array - hence a file-scope constant rather than a local.
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
            // OUR OWN BUNDLE, LOOKED UP BY IDENTIFIER. The host is told where to load the view class
            // from, and the only bundle that has it is this one - so the identifier here and
            // CFBundleIdentifier in the .component's Info.plist have to match exactly, and do-plugin
            // writes both from the same place.
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
            char                                text[64];
            double                              plain;

            if (synthlib_param_at(d, (uint32_t)req->inParamID) == NULL) {
                return kAudioUnitErr_InvalidParameter;
            }
            plain = (req->inValue != NULL) ? (double)(*req->inValue)
                                           : synthlib_param_to_plain(d, req->inParamID,
                                                                     au->params[req->inParamID]);

            if (synthlib_param_text(d, au->inst, (uint32_t)req->inParamID,
                                    synthlib_param_to_normalized(d, req->inParamID, plain),
                                    text, sizeof(text)) == false) {
                return kAudioUnitErr_InvalidParameter;
            }
            req->outString = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
            break;
        }

        case kMusicDeviceProperty_InstrumentCount:
            // Zero means "not multitimbral", which is the honest answer for a one-voice engine.
            *(UInt32 *)outData = 0;
            break;

        case kSynthLibAuProperty_Instance:
            *(void **)outData = au->inst;
            break;

        default:
            return kAudioUnitErr_InvalidProperty;
    }
    return noErr;
}

static OSStatus au_set_property(void * self, AudioUnitPropertyID id, AudioUnitScope scope,
                                AudioUnitElement element, const void * inData, UInt32 inDataSize) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = desc();

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
            UInt32 channels = (scope == kAudioUnitScope_Input) ? d->numInputChannels
                                                               : d->numOutputChannels;

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

        // An instrument has no audio input, so there is nothing for a host to connect to it. Said
        // explicitly rather than falling through to "unknown property", because the two mean
        // different things to a host and only one of them is true.
        case kAudioUnitProperty_MakeConnection:
        case kAudioUnitProperty_SetRenderCallback:
            return kAudioUnitErr_InvalidProperty;

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
// Parameters, as the host sees them
// ------------------------------------------------------------------------------------------------

static OSStatus au_get_parameter(void * self, AudioUnitParameterID id, AudioUnitScope scope,
                                 AudioUnitElement element, AudioUnitParameterValue * outValue) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    (void)element;

    if (scope != kAudioUnitScope_Global) {
        return kAudioUnitErr_InvalidScope;
    }

    if ((outValue == NULL) || (synthlib_param_at(desc(), (uint32_t)id) == NULL)) {
        return kAudioUnitErr_InvalidParameter;
    }
    // PLAIN, not normalized. An Audio Unit's parameter values are in the range it declared in
    // AudioUnitParameterInfo, where VST3's are always 0..1 - the one real difference between the two
    // formats' parameter models, and the reason the table carries plainMin and plainMax at all.
    *outValue = (AudioUnitParameterValue)synthlib_param_to_plain(desc(), (uint32_t)id, au->params[id]);
    return noErr;
}

static OSStatus au_set_parameter(void * self, AudioUnitParameterID id, AudioUnitScope scope,
                                 AudioUnitElement element, AudioUnitParameterValue value,
                                 UInt32 inBufferOffsetInFrames) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    (void)element;
    // BLOCK GRANULARITY, as on the VST3 side: the offset is accepted and ignored because the engines
    // behind this cannot place a change inside a block. See the note in synthlibPlugin.h.
    (void)inBufferOffsetInFrames;

    if (scope != kAudioUnitScope_Global) {
        return kAudioUnitErr_InvalidScope;
    }

    if (synthlib_param_at(desc(), (uint32_t)id) == NULL) {
        return kAudioUnitErr_InvalidParameter;
    }
    apply_param(au, id, synthlib_param_to_normalized(desc(), (uint32_t)id, (double)value));
    return noErr;
}

static OSStatus au_schedule_parameters(void * self, const AudioUnitParameterEvent * events,
                                       UInt32 numEvents) {
    tSynthLibAu * au = (tSynthLibAu *)self;

    if (events == NULL) {
        return kAudioUnitErr_InvalidParameterValue;
    }

    for (UInt32 i = 0; i < numEvents; i++) {
        const AudioUnitParameterEvent * e = &events[i];
        double                          value;

        // A RAMP IS TAKEN AT ITS END POINT. The engines have no notion of a parameter moving within
        // a block, so interpolating would be inventing a resolution they cannot use - the same
        // trade the VST3 wrapper makes by reading only the last point in each automation queue.
        if (e->eventType == kParameterEvent_Ramped) {
            value = (double)e->eventValues.ramp.endValue;
        } else {
            value = (double)e->eventValues.immediate.value;
        }

        if (synthlib_param_at(desc(), (uint32_t)e->parameter) != NULL) {
            apply_param(au, e->parameter,
                        synthlib_param_to_normalized(desc(), (uint32_t)e->parameter, value));
        }
    }
    return noErr;
}

// ------------------------------------------------------------------------------------------------
// MIDI
// ------------------------------------------------------------------------------------------------

// THE MAPPING THE VST3 SIDE GETS FROM THE HOST, DONE HERE OURSELVES. A VST3 host converts a
// controller into a parameter change and asks IMidiMapping which parameter; an Audio Unit host hands
// the raw bytes over and leaves it to us. Both read the same midiControl column of the same table,
// so the two formats cannot end up wired differently.
static void midi_control_to_param(tSynthLibAu * au, int16_t control, double normalized) {
    const tSynthLibPluginDesc * d = desc();

    for (uint32_t i = 0; i < d->numParams; i++) {
        if (d->params[i].midiControl == control) {
            apply_param(au, (AudioUnitParameterID)d->params[i].id, normalized);
            notify_listeners(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0);
            return;
        }
    }
}

static OSStatus au_midi_event(void * self, UInt32 inStatus, UInt32 inData1, UInt32 inData2,
                              UInt32 inOffsetSampleFrame) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = desc();
    UInt32                      command = inStatus & 0xF0u;

    // Block granularity, as everywhere else here.
    (void)inOffsetSampleFrame;

    if (au->inst == NULL) {
        return noErr;
    }

    switch (command) {
        case 0x90:      // note on - and a note on at zero velocity is a note off, as it is on the wire
            if ((inData2 > 0u) && (d->cb.noteOn != NULL)) {
                d->cb.noteOn(au->inst, (uint8_t)inData1, (float)inData2 / 127.0f);
            } else if (d->cb.noteOff != NULL) {
                d->cb.noteOff(au->inst, (uint8_t)inData1);
            }
            break;

        case 0x80:      // note off
            if (d->cb.noteOff != NULL) {
                d->cb.noteOff(au->inst, (uint8_t)inData1);
            }
            break;

        case 0xA0:      // POLYPHONIC key pressure, which is not channel pressure and not a controller
            if (d->cb.polyPressure != NULL) {
                d->cb.polyPressure(au->inst, (uint8_t)inData1, (float)inData2 / 127.0f);
            }
            break;

        case 0xB0:      // continuous controller
            midi_control_to_param(au, (int16_t)inData1, (double)inData2 / 127.0);

            // ALL NOTES OFF and ALL SOUND OFF, which a host sends on a transport stop and which a
            // plug-in that ignores them leaves droning.
            if (((inData1 == 120u) || (inData1 == 123u)) && (d->cb.reset != NULL)) {
                d->cb.reset(au->inst);
            }
            break;

        case 0xD0:      // channel pressure
            midi_control_to_param(au, SYNTHLIB_MIDI_AFTERTOUCH, (double)inData1 / 127.0);
            break;

        case 0xE0: {    // pitch bend, 14 bits across the two data bytes, 0x2000 at rest
            uint32_t value = (inData1 & 0x7Fu) | ((inData2 & 0x7Fu) << 7);

            midi_control_to_param(au, SYNTHLIB_MIDI_PITCH_BEND, (double)value / 16383.0);
            break;
        }

        default:
            break;
    }
    return noErr;
}

static OSStatus au_start_note(void * self, MusicDeviceInstrumentID inInstrument,
                              MusicDeviceGroupID inGroupID, NoteInstanceID * outNoteInstanceID,
                              UInt32 inOffsetSampleFrame, const MusicDeviceNoteParams * inParams) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = desc();

    (void)inInstrument;
    (void)inGroupID;
    (void)inOffsetSampleFrame;

    if ((inParams == NULL) || (au->inst == NULL)) {
        return kAudioUnitErr_InvalidParameter;
    }
    // mPitch is a FLOAT here, so a host may ask for a fractional note. The engines are note-number
    // driven, so it is rounded - and the note instance id is the rounded number, which is what
    // StopNote will hand back.
    UInt8 note = (UInt8)(inParams->mPitch + 0.5f);

    if (outNoteInstanceID != NULL) {
        *outNoteInstanceID = (NoteInstanceID)note;
    }

    if (d->cb.noteOn != NULL) {
        d->cb.noteOn(au->inst, note, inParams->mVelocity / 127.0f);
    }
    return noErr;
}

static OSStatus au_stop_note(void * self, MusicDeviceGroupID inGroupID, NoteInstanceID inNoteInstanceID,
                             UInt32 inOffsetSampleFrame) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = desc();

    (void)inGroupID;
    (void)inOffsetSampleFrame;

    if ((au->inst != NULL) && (d->cb.noteOff != NULL)) {
        d->cb.noteOff(au->inst, (uint8_t)inNoteInstanceID);
    }
    return noErr;
}

// ------------------------------------------------------------------------------------------------
// Render
// ------------------------------------------------------------------------------------------------

static OSStatus ensure_owned_buffer(tSynthLibAu * au, UInt32 frames) {
    const tSynthLibPluginDesc * d = desc();

    if (au->ownedFrames >= frames) {
        return noErr;
    }
    float * block = (float *)realloc(au->ownedSamples,
                                     (size_t)frames * d->numOutputChannels * sizeof(float));

    if (block == NULL) {
        return kAudioUnitErr_FailedInitialization;
    }
    au->ownedSamples = block;
    au->ownedFrames  = frames;
    return noErr;
}

static OSStatus au_render(void * self, AudioUnitRenderActionFlags * ioActionFlags,
                          const AudioTimeStamp * inTimeStamp, UInt32 inBusNumber,
                          UInt32 inNumberFrames, AudioBufferList * ioData) {
    tSynthLibAu *               au    = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d     = desc();
    AudioUnitRenderActionFlags  flags = (ioActionFlags != NULL) ? *ioActionFlags : 0;
    OSStatus                    err   = noErr;

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

    if (ioData->mNumberBuffers < d->numOutputChannels) {
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
    for (UInt32 c = 0; c < d->numOutputChannels; c++) {
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

    if (d->cb.render != NULL) {
        d->cb.render(au->inst, au->channels, d->numOutputChannels, inNumberFrames);
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
    const tSynthLibPluginDesc * d  = desc();

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
    const tSynthLibPluginDesc * d  = desc();

    if (au->inst == NULL) {
        return kAudioUnitErr_FailedInitialization;
    }

    if (d->cb.setSampleRate != NULL) {
        d->cb.setSampleRate(au->inst, au->sampleRate);
    }

    // GOING ACTIVE IS WHERE THE ENGINE STARTS, not where the instance was made - see the note on
    // setActive in synthlibPlugin.h. Doing this at construction time is the mistake that had the
    // VST3 build render silence from a perfectly good patch.
    if (d->cb.setActive != NULL) {
        d->cb.setActive(au->inst, true);
    }
    au->initialized = true;
    return noErr;
}

static OSStatus au_uninitialize(void * self) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = desc();

    au->initialized = false;

    if ((au->inst != NULL) && (d->cb.setActive != NULL)) {
        d->cb.setActive(au->inst, false);
    }
    return noErr;
}

static AudioComponentMethod au_lookup(SInt16 selector) {
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
        case kMusicDeviceMIDIEventSelect:           return (AudioComponentMethod)au_midi_event;
        case kMusicDeviceStartNoteSelect:           return (AudioComponentMethod)au_start_note;
        case kMusicDeviceStopNoteSelect:            return (AudioComponentMethod)au_stop_note;
        default:                                    return NULL;
    }
}

static OSStatus au_open(void * self, AudioComponentInstance ci) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = desc();

    au->ci = ci;

    if (d->cb.create != NULL) {
        au->inst = d->cb.create();
    }

    if (au->inst == NULL) {
        return kAudioUnitErr_FailedInitialization;
    }

    if (d->cb.initialize != NULL) {
        d->cb.initialize(au->inst);
    }

    if (gInstanceCount == 0) {
        gSoleAu = au;
    } else {
        gSoleAu = NULL;
    }
    gInstanceCount++;
    return noErr;
}

static OSStatus au_close(void * self) {
    tSynthLibAu *               au = (tSynthLibAu *)self;
    const tSynthLibPluginDesc * d  = desc();

    if (au->inst != NULL) {
        if (d->cb.terminate != NULL) {
            d->cb.terminate(au->inst);
        }

        if (d->cb.destroy != NULL) {
            d->cb.destroy(au->inst);
        }
        au->inst = NULL;
    }
    gInstanceCount--;

    if (gSoleAu == au) {
        gSoleAu = NULL;
    }

    if (au->currentPreset.presetName != NULL) {
        CFRelease(au->currentPreset.presetName);
    }
    free(au->params);
    free(au->channels);
    free(au->ownedSamples);

    // THE FACTORY'S ALLOCATION IS FREED HERE AND NOWHERE ELSE. `self` IS that allocation - the
    // interface is its first member - so this is the last line that may touch it.
    free(au);
    return noErr;
}

// ------------------------------------------------------------------------------------------------
// What the plug-in may ask of us
// ------------------------------------------------------------------------------------------------

void synthlib_plugin_param_edited(uint32_t id, double normalized) {
    tSynthLibAu * au = gSoleAu;

    if ((au == NULL) || (synthlib_param_at(desc(), id) == NULL)) {
        return;
    }
    apply_param(au, (AudioUnitParameterID)id, normalized);

    // BEGIN, CHANGE, END, as one gesture - the Audio Unit spelling of VST3's
    // beginEdit/performEdit/endEdit, and what lets a host record the move as automation rather than
    // watch a value appear from nowhere.
    AudioUnitEvent event;

    memset(&event, 0, sizeof(event));
    event.mArgument.mParameter.mAudioUnit   = au->ci;
    event.mArgument.mParameter.mParameterID = (AudioUnitParameterID)id;
    event.mArgument.mParameter.mScope       = kAudioUnitScope_Global;
    event.mArgument.mParameter.mElement     = 0;

    event.mEventType = kAudioUnitEvent_BeginParameterChangeGesture;
    AUEventListenerNotify(NULL, NULL, &event);

    event.mEventType = kAudioUnitEvent_ParameterValueChange;
    AUEventListenerNotify(NULL, NULL, &event);

    event.mEventType = kAudioUnitEvent_EndParameterChangeGesture;
    AUEventListenerNotify(NULL, NULL, &event);
}

// AN AUDIO UNIT'S HOST OWNS THE WINDOW AND IS NOT ASKED. There is no equivalent of VST3's
// IPlugFrame::resizeView here: a Cocoa view resizes itself and the host follows, or it does not.
// Answering false is what tells the caller to leave the window alone rather than resize its own view
// inside a frame that did not move.
bool synthlib_plugin_request_resize(double width, double height) {
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
    const tSynthLibPluginDesc * d = desc();

    (void)inDesc;

    if ((d == NULL) || (d->numOutputChannels == 0)) {
        return NULL;
    }
    tSynthLibAu * au = (tSynthLibAu *)calloc(1, sizeof(tSynthLibAu));

    if (au == NULL) {
        return NULL;
    }
    au->iface.Open     = au_open;
    au->iface.Close    = au_close;
    au->iface.Lookup   = au_lookup;
    au->iface.reserved = NULL;

    au->sampleRate = 44100.0;
    au->maxFrames  = DEFAULT_MAX_FRAMES;

    au->params   = (double *)calloc(d->numParams ? d->numParams : 1, sizeof(double));
    au->channels = (float **)calloc(d->numOutputChannels, sizeof(float *));

    if ((au->params == NULL) || (au->channels == NULL)) {
        free(au->params);
        free(au->channels);
        free(au);
        return NULL;
    }

    // The defaults, and they matter before anything has been played: a parameter left at zero that
    // means "full bend down" is a plug-in reporting a state it is not in.
    for (uint32_t i = 0; i < d->numParams; i++) {
        au->params[i] = d->params[i].defaultNormalized;
    }
    au->currentPreset.presetNumber = 0;
    au->currentPreset.presetName   = CFStringCreateCopy(NULL, CFSTR("Default"));

    // THE ADDRESS OF THE INTERFACE IS THE ADDRESS OF THE STRUCT, because it is the first member -
    // which is what lets every method cast its `self` straight back to a tSynthLibAu *.
    return &au->iface;
}
