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

// WHAT A PLUG-IN IS, SAID ONCE, IN C.
//
// A plug-in project fills in one of these and links a WRAPPER: synthlibPluginVst3.cpp turns it into
// a VST3, synthlibPluginAu.c turns it into an Audio Unit. Neither format appears anywhere in this
// header, and neither wrapper knows anything about the plug-in beyond what is below.
//
// THIS EXISTS BECAUSE THE SECOND FORMAT WOULD OTHERWISE HAVE BEEN A SECOND COPY. G2-Edit's
// g2Vst3.cpp was 982 lines, of which the parts that were actually about the G2 - ten parameters,
// a patch path, note on and note off - came to well under two hundred. An Audio Unit written the
// same way would have duplicated the other eight hundred in a different dialect, and every fix
// after that would have had to be made twice. The three sibling plug-ins already show what that
// looks like: their *View.m files differ by 85 lines out of 360 and are the same file.
//
// PLAIN C, DELIBERATELY. VST3 needs C++ and an Audio Unit's view needs Objective-C, so the one
// language both can call without ceremony is the one the shared description is written in. Nothing
// here allocates, so a descriptor can be - and in practice is - a file-scope constant.
//
// THREADING. The wrapper calls render() on the host's audio thread and everything else on its UI
// thread, with one exception: setParam() arrives on BOTH, since a host may automate a parameter
// within a processing block. A plug-in must therefore make setParam() safe against render(); the
// usual answer, and G2-Edit's, is that every value it touches is stored through an atomic.

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

// The MIDI controls a HOST INTERCEPTS rather than delivering as events, and which therefore have to
// be named as parameters or they are lost.
//
// A VST3 host converts continuous controllers, channel pressure and pitch bend into parameter
// changes and asks IMidiMapping which parameter each becomes; an Audio Unit host hands the same
// bytes over raw and the wrapper does that mapping itself. Either way the answer comes from the
// table below, so the two formats cannot disagree about it.
//
// The numbers are MIDI controller numbers, extended above 127 the way VST3's ControllerNumbers does
// - which is where the two out-of-band entries get their values, so no translation is needed there.
#define SYNTHLIB_MIDI_NONE          (-1)
#define SYNTHLIB_MIDI_MOD_WHEEL     (1)
#define SYNTHLIB_MIDI_BREATH        (2)
#define SYNTHLIB_MIDI_FOOT          (4)
#define SYNTHLIB_MIDI_EXPRESSION    (11)
#define SYNTHLIB_MIDI_SUSTAIN       (64)
#define SYNTHLIB_MIDI_AFTERTOUCH    (128)      // channel pressure, 0xD0 - not a controller number
#define SYNTHLIB_MIDI_PITCH_BEND    (129)      // 0xE0

// ONE PARAMETER, RESOLVED. Both wrappers work from this and only this, whether it came from a static
// table or was computed at run time - see tSynthLibParamDesc below and synthlib_param_describe().
typedef struct {
    uint32_t           id;              // what the host quotes back; also the index into the table
    const char *       title;           // "Output Level"
    const char *       shortTitle;      // "Level" - VST3 shortTitle, AU has only the one name
    tSynthLibParamUnit unit;

    // The range in DISPLAYED units. A wrapper maps 0..1 onto this linearly and back; anything that
    // is not linear in its own units belongs in paramText() rather than in a curve here, because
    // the two formats disagree about which of the two numbers they hand around and a curve would
    // then have to be inverted in one of them.
    double             plainMin;
    double             plainMax;

    double             defaultNormalized;   // 0..1, NOT plain - see the note on plainMin
    int32_t            stepCount;           // 0 == continuous; N == N+1 discrete positions
    int16_t            midiControl;         // SYNTHLIB_MIDI_*, or SYNTHLIB_MIDI_NONE
} tSynthLibParam;

// THE SAME THING, BUT IT OWNS ITS STRINGS. A plug-in whose parameter LIST is not known until it is
// running - GenBridge's device and MIDI-destination pickers are named after whatever hardware is
// plugged in, and there are as many first-channel positions as the interface has inputs - cannot
// answer with a pointer into a static table, because there is no static table for it to point into.
//
// So the callback form fills one of these, the table form is copied into one, and the two wrappers
// only ever see this. Without that split each wrapper would have had to know about both sources.
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
} tSynthLibParamDesc;

// ------------------------------------------------------------------------------------------------
// What the wrapper asks of the plug-in
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// Buses
// ------------------------------------------------------------------------------------------------

// AN AUDIO BUS, described rather than counted. A count was enough while the only plug-in here was a
// stereo instrument with no input; it is not enough for an effect with a SIDE-CHAIN, which is a
// second input bus that must be declared, must be marked auxiliary, and must NOT be active by
// default - a host asked to fill an input the plug-in does not need will connect something to it and
// then wonder why nothing happens.
typedef struct {
    const char * name;              // "Input", "Side-chain" - a host shows this
    uint32_t     channels;
    bool         isAux;             // a side-chain rather than the main signal
    bool         defaultActive;     // an aux bus normally wants false
} tSynthLibBus;

// ------------------------------------------------------------------------------------------------
// What the host is doing, per block
// ------------------------------------------------------------------------------------------------

// THE HOST'S TRANSPORT, AS MUCH OF IT AS THE HOST WILL SAY. Every field has its own validity flag
// rather than a sentinel, because "tempo 0" and "the host did not tell us the tempo" are different
// facts and a plug-in that generates a clock has to be able to tell them apart.
//
// THE TWO FORMATS DO NOT OFFER THE SAME THING HERE, and the difference is not cosmetic. VST3 hands
// a ProcessContext to every process() call, filled by the host, on the audio thread. An Audio Unit
// offers HostCallbacks - function pointers the plug-in CALLS - and a host may install none of them,
// may install some, and answers from whatever the host feels like rather than from the block being
// rendered. So `valid` is false far more often on an Audio Unit, and a plug-in whose whole purpose
// is timing accuracy should be measured on both before either is trusted.
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

    bool     systemTimeValid;
    uint64_t systemTime;            // host nanoseconds

    int64_t  projectTimeSamples;
    double   sampleRate;
} tSynthLibTransport;

typedef struct {
    // ---- lifecycle -----------------------------------------------------------------------------

    // One instance per copy in the host. May return NULL, which the wrapper reports as a failure to
    // instantiate rather than handing the host a half-built plug-in.
    //
    // GIVEN ITS OWN DESCRIPTOR, because one binary may register several VARIANTS of itself and a
    // shared implementation has to know which one it is being made as - GenBridge registers the same
    // code twice, as an effect and as an instrument, and the two differ in their bus layout and in
    // how many parameters they own.
    void * (* create)(const tSynthLibPluginDesc * desc);
    void   (* destroy)(void * inst);

    // The host has finished connecting things up. Load whatever a fresh instance should play.
    void   (* initialize)(void * inst);
    void   (* terminate)(void * inst);

    void   (* setSampleRate)(void * inst, double sampleRate);

    // GOING ACTIVE IS NOT THE SAME AS BEING CREATED, and the difference has cost real time here:
    // G2-Edit's engine ignores a request to resolve its chain while it is inactive, so the patch
    // has to be pushed into it from here rather than from initialize(). See soundEngine.c.
    void   (* setActive)(void * inst, bool active);

    // Silence everything currently sounding. A host calls this on a transport jump.
    void   (* reset)(void * inst);

    // ---- audio ---------------------------------------------------------------------------------

    // Fill out[channel][0..frames-1]. Called on the audio thread; allocate nothing here.
    //
    // DE-INTERLEAVED, because that is what both formats hand over - a VST3 channelBuffers32 and an
    // AudioBufferList of one-channel buffers are the same shape, and an engine rendering interleaved
    // frames (G2-Edit's does) de-interleaves once, here, instead of once per wrapper.
    //
    // `in` is the first input bus, or NULL for a plug-in that declared none - and also NULL when the
    // host has not connected one, which is a thing a host may legitimately do to a bus the plug-in
    // said was auxiliary. `transport` is never NULL, but its `valid` is often false; see above.
    void   (* process)(void * inst,
                       const float * const * in, uint32_t numIn,
                       float ** out, uint32_t numOut,
                       uint32_t frames,
                       const tSynthLibTransport * transport);

    // ---- events --------------------------------------------------------------------------------
    //
    // BLOCK GRANULARITY. Neither wrapper passes a sample offset, because neither of the two engines
    // behind this can place an event inside a block: a note lands at the start of the buffer it
    // arrived in. At 44.1 kHz and 512 frames that is under 12 ms. Worth revisiting only when an
    // engine can actually use a finer answer, at which point the offset joins these signatures.

    void   (* noteOn)(void * inst, uint8_t note, float velocity);       // velocity 0..1
    void   (* noteOff)(void * inst, uint8_t note);
    void   (* polyPressure)(void * inst, uint8_t note, float pressure); // 0..1; may be NULL

    // ---- parameters ----------------------------------------------------------------------------
    //
    // NORMALIZED 0..1 THROUGHOUT. Plain units exist only where a host insists on them, and the
    // wrapper converts using plainMin/plainMax so the plug-in never has to hold both.

    // THE DYNAMIC ALTERNATIVE TO THE STATIC TABLE. A plug-in leaves the descriptor's `params` NULL
    // and fills these in instead; anything with a fixed parameter list should use the table, which
    // is checkable at compile time and needs no instance to exist before it can be read.
    //
    // A HOST WALKS 0..paramCount()-1 AND EXPECTS EVERY ONE OF THEM TO EXIST. Returning false for an
    // index below the count is not a way to hide a parameter - GenBridge shipped a variant that
    // advertised seven and had six, and what a host does with the refusal is its own business.
    uint32_t (* paramCount)(void * inst);
    bool     (* paramInfo)(void * inst, uint32_t index, tSynthLibParamDesc * out);

    void   (* setParam)(void * inst, uint32_t id, double normalized);
    double (* getParam)(void * inst, uint32_t id);

    // How the value reads. Return false and the wrapper prints the plain number with the unit,
    // which is right for most parameters - this is for the ones where it is not ("-inf").
    bool   (* paramText)(void * inst, uint32_t id, double normalized, char * out, size_t len);

    // ---- state ---------------------------------------------------------------------------------
    //
    // An opaque blob the host stores with its project. Called with out == NULL to ask how big it
    // is; the wrapper then calls again with a buffer of at least that size. Return 0 for a plug-in
    // with nothing to save beyond its parameters, which both wrappers persist by themselves.

    size_t (* getState)(void * inst, void * out, size_t len);
    void   (* setState)(void * inst, const void * data, size_t len);

    // ---- the other half of itself ----------------------------------------------------------------

    // A SMALL MESSAGE FROM THE PROCESSOR TO THE CONTROLLER, OR BACK. VST3 splits a plug-in in two
    // and IConnectionPoint is the only channel between them; an Audio Unit is one object and needs
    // no channel at all, so this is delivered straight through there.
    //
    // It carries an id and an integer and nothing else, on purpose: the one thing that has ever
    // needed to travel this way is "which status slot am I", and a channel that can carry a block of
    // bytes invites a per-frame stream that belongs in shared memory instead. May be NULL.
    void   (* message)(void * inst, const char * id, int64_t value);

    // ---- editor --------------------------------------------------------------------------------

    // Build the editor and hand back an NSView as a void *, or NULL for a plug-in with no editor of
    // its own (the host then draws a generic panel from the parameter table). Called on the main
    // thread. Typed as void * so this header stays free of Cocoa and can be included from C.
    //
    // RETURNED RETAINED (+1), and the wrapper releases it. Under ARC that means handing back
    // (__bridge_retained void *)view: an autoreleased object crossing a void * is invisible to ARC
    // on both sides, so the ownership has to be stated once, here, rather than guessed at each end.
    void * (* createView)(void * inst, double width, double height);

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

    // AN OVERRIDE, NOT A REQUIREMENT. NULL takes "Instrument|Synth" or "Fx" from isInstrument, which
    // is right for most things. GenBridge wants "Fx|NoOfflineProcess|Tools", and NoOfflineProcess is
    // not decoration there: a live capture has nothing to give a faster-than-realtime render, so a
    // host bouncing offline must not call it at all - without the flag it bounces silence and looks
    // like a plug-in bug.
    const char * vst3SubCategory;

    const tSynthLibBus * inputs;
    uint32_t     numInputs;
    const tSynthLibBus * outputs;
    uint32_t     numOutputs;

    // An effect can want MIDI IN too. There is deliberately no "MIDI out": both plug-ins here that
    // send MIDI open their own CoreMIDI port, because VST3 cannot express a system-realtime byte -
    // a MIDI clock generator has no way to emit a 0xF8 through its host at all.
    bool         wantsMidiIn;

    // Whether the host's transport is worth asking for. VST3 turns this into
    // IProcessContextRequirements, which is how a host knows it need not fill in a ProcessContext
    // nobody reads; an Audio Unit installs its HostCallbacks on the strength of it.
    bool         wantsTransport;

    // ---- identity, per format ------------------------------------------------------------------
    //
    // A HOST REMEMBERS A PLUG-IN BY THESE. Once a project has been saved against a build, none of
    // them may ever change, or that project reopens with an empty slot where the plug-in was.

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

    // LOCKED, if non-zero: width / height, and height is derived from width on every resize. The
    // canvas plug-in needs this - it scales from width alone, so a taller window would otherwise
    // uncover rows the application's own aspect lock prevents it from ever showing.
    double       editorAspect;

    // Remembering the size across sessions. Both wrappers call these, so an editor opened in Live
    // and one opened in Logic agree. Either may be NULL, and then the default width is used.
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

// ONE BINARY MAY REGISTER SEVERAL PLUG-INS. Usually it is one and this is a list of length 1, but
// GenBridge registers the same code twice - once as an effect and once as an instrument - because
// VST3 instruments live on instrument tracks and effects do not, and a user needs whichever their
// host will let them put where they want it.
typedef struct {
    const tSynthLibPluginDesc * variants;
    uint32_t                    count;
} tSynthLibPluginSet;

// THE ONE SYMBOL A PLUG-IN PROJECT HAS TO PROVIDE. Both wrappers call it, once, and hold on to what
// comes back for the life of the process - so it must return a pointer that outlives every call,
// which in practice means a file-scope constant.
const tSynthLibPluginSet * synthlib_plugin_variants(void);

// ------------------------------------------------------------------------------------------------
// What the plug-in may ask of the wrapper
// ------------------------------------------------------------------------------------------------

// TELL THE HOST A PARAMETER MOVED BECAUSE THE USER MOVED IT IN OUR OWN EDITOR.
//
// Without this a host sees a value appear from nowhere: it cannot record the move as automation and
// its own generic panel goes out of step with ours. VST3 spells it beginEdit/performEdit/endEdit on
// the component handler; an Audio Unit posts a property-changed notification. Both are behind this
// one call, and both are no-ops when no host has connected the channel yet.
//
// Main thread only - it reaches into host UI in both formats.
void synthlib_plugin_param_edited(uint32_t id, double normalized);

// Ask the host to resize the editor window, after the plug-in has changed its own idea of how big it
// should be. Returns false if the host offers no such channel, which is common; the caller should
// then leave the window alone rather than resizing its view inside a frame that did not move.
bool synthlib_plugin_request_resize(double width, double height);

// Send one small message to the other half of the plug-in - see the `message` callback. Returns
// false when there is nothing connected to send it to, which on VST3 is every moment before the
// host has joined the two halves up.
bool synthlib_plugin_send_message(void * inst, const char * id, int64_t value);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_H__
