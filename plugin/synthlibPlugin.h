/*
 * SynthLib - the format-free description of a plug-in.
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
// Notes: Docs/code-notes/synthlibPlugin.h.md - "// notes §k" refers there.

// notes §1

#ifndef __SYNTHLIB_PLUGIN_H__
#define __SYNTHLIB_PLUGIN_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Named ahead of the callbacks, which take one - see create().
typedef struct tSynthLibPluginDesc tSynthLibPluginDesc;

// ------------------------------------------------------------------------------------------------
// Parameters
// ------------------------------------------------------------------------------------------------

// What a value MEANS, which is the one thing a host cannot work out from a number. VST3 wants a
// units string, an Audio Unit wants an AudioUnitParameterUnit; both come from this.
typedef enum {
    eSynthLibUnitGeneric = 0,
    eSynthLibUnitPercent,
    eSynthLibUnitDecibels,
    eSynthLibUnitHertz,
    eSynthLibUnitSeconds,
    eSynthLibUnitMilliseconds,
    eSynthLibUnitSemitones,
    eSynthLibUnitIndexed,        // a list; stepCount says how many entries
    eSynthLibUnitBoolean
} tSynthLibParamUnit;

// notes §2
#define SYNTHLIB_PARAM_HIDDEN     (1u << 0)
#define SYNTHLIB_PARAM_LIST       (1u << 1)
#define SYNTHLIB_PARAM_NO_SAVE    (1u << 2)

// notes §3
#define SYNTHLIB_MIDI_NONE          (-1)
#define SYNTHLIB_MIDI_MOD_WHEEL     (1)
#define SYNTHLIB_MIDI_BREATH        (2)
#define SYNTHLIB_MIDI_FOOT          (4)
#define SYNTHLIB_MIDI_EXPRESSION    (11)
#define SYNTHLIB_MIDI_SUSTAIN       (64)
#define SYNTHLIB_MIDI_AFTERTOUCH    (128)      // channel pressure, 0xD0 - not a controller number
#define SYNTHLIB_MIDI_PITCH_BEND    (129)      // 0xE0
#define SYNTHLIB_MIDI_CONTROLS      (130)      // how many of the above there can be, per channel

// ONE PARAMETER, IN A STATIC TABLE. See tSynthLibParamDesc below for the resolved form both wrappers
// actually work from.
typedef struct {
    uint32_t           id;              // what the host quotes back
    const char *       title;           // "Output Level"
    const char *       shortTitle;      // "Level" - VST3 shortTitle, AU has only the one name
    tSynthLibParamUnit unit;

    // notes §4
    double             plainMin;
    double             plainMax;

    double             defaultNormalized;   // 0..1, NOT plain - see the note on plainMin
    int32_t            stepCount;           // 0 == continuous; N == N+1 discrete positions
    int16_t            midiControl;         // SYNTHLIB_MIDI_*, or SYNTHLIB_MIDI_NONE

    // SYNTHLIB_PARAM_*. LAST, so a table written positionally before the field existed still reads
    // as zero here - which is "shown, automatable, saved", what every parameter was then.
    uint32_t           flags;
} tSynthLibParam;

// notes §5
#define SYNTHLIB_PARAM_TITLE_MAX    (64)

typedef struct {
    uint32_t           id;
    char               title[SYNTHLIB_PARAM_TITLE_MAX];
    char               shortTitle[SYNTHLIB_PARAM_TITLE_MAX];
    tSynthLibParamUnit unit;
    double             plainMin;
    double             plainMax;
    double             defaultNormalized;
    int32_t            stepCount;
    int16_t            midiControl;
    uint32_t           flags;
} tSynthLibParamDesc;

// A value, with the parameter it belongs to. What a saved state reads back as, and what
// stateParams() fills.
typedef struct {
    uint32_t id;
    double   value;             // normalized
} tSynthLibParamValue;

// ONE AUTOMATION POINT INSIDE A BLOCK. See paramPoints().
typedef struct {
    uint32_t sampleOffset;      // frames into the current block
    double   value;             // normalized
} tSynthLibParamPoint;

// ------------------------------------------------------------------------------------------------
// Buses
// ------------------------------------------------------------------------------------------------

// notes §6
typedef struct {
    const char * name;              // "Input", "Side-chain" - a host shows this
    uint32_t     channels;
    bool         isAux;             // a side-chain rather than the main signal
    bool         defaultActive;     // an aux bus normally wants false
} tSynthLibBus;

// ------------------------------------------------------------------------------------------------
// What the host is doing
// ------------------------------------------------------------------------------------------------

// notes §7
typedef struct {
    double   sampleRate;
    uint32_t maxFrames;             // the largest block the host has promised to hand over
    bool     offline;
} tSynthLibSetup;

// notes §8
typedef struct {
    bool     valid;                 // false: the host said nothing at all this block

    bool     playing;
    bool     recording;
    bool     cycleActive;

    bool     tempoValid;
    double   tempo;                 // BPM

    bool     musicTimeValid;
    double   projectTimeMusic;      // in quarter notes from the project start

    bool     barPositionValid;
    double   barPositionMusic;      // quarter notes, of the last bar line

    // THE LOOP, in quarter notes. A clock generator needs both ends: Live does not split a block at a
    // loop boundary, so the wrap has to be recognised from where the loop is rather than from the
    // position jumping.
    bool     cycleValid;
    double   cycleStartMusic;
    double   cycleEndMusic;

    bool     timeSigValid;
    int32_t  timeSigNumerator;
    int32_t  timeSigDenominator;

    // NANOSECONDS, from the host's clock. VST3's ProcessContext.systemTime - which Live never fills
    // in - or, on an Audio Unit, the render timestamp's host time converted. The Audio Unit's is the
    // more useful of the two, and it is the one field here an Audio Unit is MORE likely to supply.
    bool     systemTimeValid;
    uint64_t systemTime;

    int64_t  projectTimeSamples;

    // Samples since the host's audio engine started, regardless of the transport - which does not
    // jump when the playhead does, so it is what a plug-in times itself against.
    bool     continuousTimeValid;
    int64_t  continuousTimeSamples;

    double   sampleRate;
} tSynthLibTransport;

// ------------------------------------------------------------------------------------------------
// What the wrapper asks of the plug-in
// ------------------------------------------------------------------------------------------------

typedef struct {
    // ---- lifecycle -----------------------------------------------------------------------------

    // notes §9
    void * (* create)(const tSynthLibPluginDesc * desc);
    void   (* destroy)(void * inst);

    // The host has finished connecting things up. Load whatever a fresh instance should play.
    void   (* initialize)(void * inst);
    void   (* terminate)(void * inst);

    void   (* setSampleRate)(void * inst, double sampleRate);

    // Everything the host has decided about how it will run us - see tSynthLibSetup. Called after
    // setSampleRate(), whenever any of it changes. May be NULL.
    void   (* prepare)(void * inst, const tSynthLibSetup * setup);

    // GOING ACTIVE IS NOT THE SAME AS BEING CREATED, and the difference has cost real time here:
    // G2-Edit's engine ignores a request to resolve its chain while it is inactive, so the patch
    // has to be pushed into it from here rather than from initialize(). See soundEngine.c.
    void   (* setActive)(void * inst, bool active);

    // notes §10
    void   (* setProcessing)(void * inst, bool running);

    // Silence everything currently sounding. A host calls this on a transport jump.
    void   (* reset)(void * inst);

    // HOW LATE OUR OUTPUT IS, in samples, so the host can line it up against the rest of the session.
    // A host reads this when it activates us and caches it - see synthlib_plugin_latency_changed()
    // for how to make it read again. May be NULL, meaning none.
    uint32_t (* latencySamples)(void * inst);

    // ---- audio ---------------------------------------------------------------------------------

    // notes §11
    void   (* blockBegin)(void * inst, uint32_t frames, const tSynthLibTransport * transport);

    // notes §12
    void   (* process)(void * inst,
                       const float * const * in, uint32_t numIn,
                       float ** out, uint32_t numOut,
                       uint32_t frames,
                       const tSynthLibTransport * transport);

    // notes §13

    void   (* noteOn)(void * inst, uint8_t channel, uint8_t note, float velocity,
                      uint32_t sampleOffset);                                     // velocity 0..1
    void   (* noteOff)(void * inst, uint8_t channel, uint8_t note, float velocity,
                       uint32_t sampleOffset);
    void   (* polyPressure)(void * inst, uint8_t channel, uint8_t note, float pressure,
                            uint32_t sampleOffset);                               // may be NULL

    // notes §14

    // notes §15
    uint32_t (* paramCount)(const tSynthLibPluginDesc * desc, void * inst);
    bool     (* paramInfo)(const tSynthLibPluginDesc * desc, void * inst, uint32_t index,
                           tSynthLibParamDesc * out);

    // notes §16
    bool     (* midiMapping)(const tSynthLibPluginDesc * desc, void * inst, uint8_t channel,
                             int16_t control, uint32_t * idOut);

    void   (* setParam)(void * inst, uint32_t id, double normalized);
    double (* getParam)(void * inst, uint32_t id);

    // notes §17
    void   (* paramPoints)(void * inst, uint32_t id, const tSynthLibParamPoint * points,
                           uint32_t count);

    // How the value reads. Return false and the wrapper prints the plain number with the unit,
    // which is right for most parameters - this is for the ones where it is not ("-inf"), and for
    // the entries of a SYNTHLIB_PARAM_LIST.
    bool   (* paramText)(const tSynthLibPluginDesc * desc, void * inst, uint32_t id,
                         double normalized, char * out, size_t len);

    // notes §18

    size_t (* getState)(void * inst, void * out, size_t len);
    void   (* setState)(void * inst, const void * data, size_t len);

    // notes §19
    uint32_t (* stateParams)(const tSynthLibPluginDesc * desc, const void * data, size_t len,
                             tSynthLibParamValue * out, uint32_t capacity);

    // ---- the other half of itself ----------------------------------------------------------------

    // notes §20
    void   (* message)(void * inst, const char * id, int64_t value);

    // ---- editor --------------------------------------------------------------------------------

    // notes §21
    void * (* createView)(const tSynthLibPluginDesc * desc, void * inst, double width, double height);

    // The host has resized the window it gave us. Sizes are in POINTS, not pixels.
    void   (* viewResized)(void * inst, void * view, double width, double height);

    // The host is taking the window away. The view itself is released by the wrapper; this is for
    // whatever the plug-in hung off it.
    void   (* destroyView)(void * inst, void * view);
} tSynthLibPluginCallbacks;

// ------------------------------------------------------------------------------------------------
// The plug-in itself
// ------------------------------------------------------------------------------------------------

struct tSynthLibPluginDesc {
    const char * name;                  // "G2 Alike" - what a host lists
    const char * vendor;
    const char * url;
    const char * email;
    const char * version;               // "0.1.0", for display

    bool         isInstrument;          // false == an effect

    // notes §22
    const char * vst3SubCategory;

    const tSynthLibBus * inputs;
    uint32_t     numInputs;
    const tSynthLibBus * outputs;
    uint32_t     numOutputs;

    // An effect can want MIDI IN too. There is deliberately no "MIDI out": both plug-ins here that
    // send MIDI open their own CoreMIDI port, because VST3 cannot express a system-realtime byte -
    // a MIDI clock generator has no way to emit a 0xF8 through its host at all.
    bool         wantsMidiIn;
    bool         wantsMidiOut;         // an event output bus, fed by synthlib_plugin_midi_out()

    // Whether the host's transport is worth asking for. VST3 turns this into
    // IProcessContextRequirements, which is how a host knows it need not fill in a ProcessContext
    // nobody reads; an Audio Unit installs its HostCallbacks on the strength of it.
    bool         wantsTransport;

    // notes §23
    bool         controllerAppliesParams;

    // notes §24

    const uint8_t * vst3ProcessorUid;   // 16 bytes
    const uint8_t * vst3ControllerUid;  // 16 bytes, and NOT the same as the processor's

    uint32_t     auType;                // 'aumu' for an instrument, 'aufx' for an effect
    uint32_t     auSubType;
    uint32_t     auManufacturer;
    uint32_t     auVersion;             // 0xMMMMmmbb, as an Audio Unit expects

    // The bundle identifier the AU wrapper looks itself up by, to tell a host where its Cocoa view
    // class lives. Must match CFBundleIdentifier in the .component's Info.plist exactly.
    const char * auBundleId;

    // ---- parameters ----------------------------------------------------------------------------

    const tSynthLibParam * params;
    uint32_t     numParams;

    // ---- editor geometry -----------------------------------------------------------------------

    double       editorDefaultWidth;    // points; 0 == no editor
    double       editorMinWidth;
    double       editorMaxWidth;        // 0 == no maximum

    // notes §25
    double       editorAspect;

    // notes §26
    long         (* editorWidthLoad)(void);
    void         (* editorWidthSave)(long width);

    // Free for the plug-in's own use, and the reason a shared implementation can tell its variants
    // apart without comparing names - see create().
    const void * userData;

    tSynthLibPluginCallbacks cb;
};

// ------------------------------------------------------------------------------------------------
// What a plug-in project provides
// ------------------------------------------------------------------------------------------------

// notes §27
typedef struct {
    const tSynthLibPluginDesc * variants;
    uint32_t                    count;
} tSynthLibPluginSet;

// THE ONE SYMBOL A PLUG-IN PROJECT HAS TO PROVIDE. Both wrappers call it, once, and hold on to what
// comes back for the life of the process - so it must return a pointer that outlives every call,
// which in practice means a file-scope constant.
const tSynthLibPluginSet * synthlib_plugin_variants(void);

// notes §28

// notes §29
void synthlib_plugin_param_edited(void * inst, uint32_t id, double normalized);

// WHAT THE HOST THINKS A PARAMETER IS SET TO, for an editor to draw. On VST3 that is the controller's
// value, which is the one a host's own panel shows and which can differ from the processor's for a
// block or two; on an Audio Unit there is only one. Main thread.
double synthlib_plugin_param_value(void * inst, uint32_t id);

// OUR LATENCY CHANGED, and the host should read latencySamples() again. A host caches the figure from
// activation, so without this a plug-in whose latency depends on a device it opened later reports the
// wrong one for the rest of the session. Any thread, as synthlib_plugin_param_edited().
void synthlib_plugin_latency_changed(void * inst);

// notes §30
bool synthlib_plugin_request_resize(void * inst, double width, double height);

// Send one small message to the other half of the plug-in - see the `message` callback. Returns
// false when there is nothing connected to send it to, which on VST3 is every moment before the
// host has joined the two halves up.
bool synthlib_plugin_send_message(void * inst, const char * id, int64_t value);

// Audio thread, from inside the plug-in's own blockBegin(), note callbacks or process(): one MIDI
// message out, sampleOffset frames into the current block. Needs wantsMidiOut. Notes, poly and
// channel pressure, controllers and pitch bend. False if the host gave no output list this block,
// or the format has no MIDI output yet (the Audio Unit).
bool synthlib_plugin_midi_out(void * inst, uint8_t status, uint8_t data1, uint8_t data2, uint32_t sampleOffset);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_H__
