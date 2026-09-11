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
// EVERY INSTANCE IS ITS OWN (2026-09-11). The first version of this contract assumed one instance of
// each plug-in per process, because G2 Alike's engine is process-wide and nothing else had been
// wrapped. GenBridge and MidiSyncTool are routinely loaded several times at once - one per synth,
// one per track - and a plug-in that asks the wrapper for something ("tell the host this moved")
// therefore always says WHICH instance it is asking for. See "What the plug-in may ask of the
// wrapper" at the foot of this file.
//
// THREADING. The wrapper calls process(), blockBegin(), paramPoints() and the note callbacks on the
// host's audio thread, and everything else on its UI thread, with one exception: setParam() may
// arrive on EITHER, since an Audio Unit host sets parameters from wherever it likes. A plug-in must
// therefore make setParam() safe against process(); the usual answer, and G2-Edit's, is that every
// value it touches is stored through an atomic.
//
// SOME CALLBACKS GET NO INSTANCE. paramCount(), paramInfo(), paramText(), midiMapping() and
// stateParams() may be called with inst == NULL, because a VST3 controller is built - and asked about
// its parameters - before it knows which processor it belongs to, and may never be told at all by a
// host that does not connect the two halves. They are handed the descriptor instead, which is enough
// to tell one variant from another.

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

// WHAT A HOST MAY DO WITH A PARAMETER, beyond setting it.
//
// HIDDEN is somewhere for a host to DELIVER a value, not something to draw or automate. GenBridge has
// two thousand of them: a VST3 host turns every MIDI controller into a parameter change, so passing a
// controller through to the hardware needs a parameter per controller per channel, none of which
// belongs in a host's automation lane.
//
// LIST says the stepped values are names rather than numbers - a host draws a drop-down and reads
// the entries through paramText().
//
// NO_SAVE keeps a parameter out of the block the WRAPPER saves. Two kinds want it: a pass-through
// whose value means nothing a moment later, and a value the plug-in saves better itself - a device
// picker's index is only meaningful against the device list it was chosen from, so GenBridge keeps
// the device's NAME in its own state and derives the index again on load.
#define SYNTHLIB_PARAM_HIDDEN     (1u << 0)
#define SYNTHLIB_PARAM_LIST       (1u << 1)
#define SYNTHLIB_PARAM_NO_SAVE    (1u << 2)

// The MIDI controls a HOST INTERCEPTS rather than delivering as events, and which therefore have to
// be named as parameters or they are lost.
//
// A VST3 host converts continuous controllers, channel pressure and pitch bend into parameter
// changes and asks IMidiMapping which parameter each becomes; an Audio Unit host hands the same
// bytes over raw and the wrapper does that mapping itself. Either way the answer comes from the
// table below - or from midiMapping(), for a plug-in that needs the channel too - so the two formats
// cannot disagree about it.
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
#define SYNTHLIB_MIDI_CONTROLS      (130)      // how many of the above there can be, per channel

// ONE PARAMETER, IN A STATIC TABLE. See tSynthLibParamDesc below for the resolved form both wrappers
// actually work from.
typedef struct {
    uint32_t           id;              // what the host quotes back
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

    // SYNTHLIB_PARAM_*. LAST, so a table written positionally before the field existed still reads
    // as zero here - which is "shown, automatable, saved", what every parameter was then.
    uint32_t           flags;
} tSynthLibParam;

// THE SAME THING, BUT IT OWNS ITS STRINGS. A plug-in whose parameter NAMES are not known until it is
// running - GenBridge's device and MIDI-destination pickers are named after whatever hardware is
// plugged in - cannot answer with a pointer into a static table, because there is no static table
// for it to point into.
//
// So the callback form fills one of these, the table form is copied into one, and the two wrappers
// only ever see this. Without that split each wrapper would have had to know about both sources.
//
// THE ID IS NOT THE INDEX. A host walks parameters by index and quotes them back by id, and a plug-in
// is free to leave gaps - GenBridge's pass-throughs start at 1000 so that its own parameters can grow
// without renumbering anything a saved project refers to.
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
// What the host is doing
// ------------------------------------------------------------------------------------------------

// HOW THE HOST INTENDS TO RUN US. Given once the host has decided, before the first block.
//
// `offline` is a render FASTER THAN REALTIME - a bounce. A plug-in whose audio comes off a wire at
// one second per second has nothing to give it and should know, rather than silently producing a
// bounce full of whatever the ring happened to hold.
typedef struct {
    double   sampleRate;
    uint32_t maxFrames;             // the largest block the host has promised to hand over
    bool     offline;
} tSynthLibSetup;

// THE HOST'S TRANSPORT, AS MUCH OF IT AS THE HOST WILL SAY. Every field has its own validity flag
// rather than a sentinel, because "tempo 0" and "the host did not tell us the tempo" are different
// facts and a plug-in that generates a clock has to be able to tell them apart. On VST3 each value is
// whatever the host wrote, flagged or not - check the flag before trusting one.
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

    // Everything the host has decided about how it will run us - see tSynthLibSetup. Called after
    // setSampleRate(), whenever any of it changes. May be NULL.
    void   (* prepare)(void * inst, const tSynthLibSetup * setup);

    // GOING ACTIVE IS NOT THE SAME AS BEING CREATED, and the difference has cost real time here:
    // G2-Edit's engine ignores a request to resolve its chain while it is inactive, so the patch
    // has to be pushed into it from here rather than from initialize(). See soundEngine.c.
    void   (* setActive)(void * inst, bool active);

    // THE HOST HAS STARTED, OR STOPPED, FEEDING US BLOCKS - distinct from setActive(), and the moment
    // anything that measures the gap between one block and the next must stop assuming the next one
    // follows the last. May be NULL, and then the wrapper calls reset() on the way in, which is what
    // an instrument wants.
    void   (* setProcessing)(void * inst, bool running);

    // Silence everything currently sounding. A host calls this on a transport jump.
    void   (* reset)(void * inst);

    // HOW LATE OUR OUTPUT IS, in samples, so the host can line it up against the rest of the session.
    // A host reads this when it activates us and caches it - see synthlib_plugin_latency_changed()
    // for how to make it read again. May be NULL, meaning none.
    uint32_t (* latencySamples)(void * inst);

    // ---- audio ---------------------------------------------------------------------------------

    // THE START OF A BLOCK, before any of its parameter changes or notes are delivered. For a plug-in
    // that places those in wall-clock time - GenBridge stamps every MIDI byte it sends from the moment
    // the block began - so it has to be told where that is BEFORE the first event rather than
    // discovering it in process(), after them. May be NULL.
    void   (* blockBegin)(void * inst, uint32_t frames, const tSynthLibTransport * transport);

    // Fill out[channel][0..frames-1]. Called on the audio thread; allocate nothing here.
    //
    // DE-INTERLEAVED, because that is what both formats hand over - a VST3 channelBuffers32 and an
    // AudioBufferList of one-channel buffers are the same shape, and an engine rendering interleaved
    // frames (G2-Edit's does) de-interleaves once, here, instead of once per wrapper.
    //
    // `in` is the first input bus, or NULL for a plug-in that declared none - and also NULL when the
    // host has not connected one, which is a thing a host may legitimately do to a bus the plug-in
    // said was auxiliary. `in` and `out` may be the SAME buffers: a host is entitled to process in
    // place. `transport` is never NULL, but its `valid` is often false; see above.
    void   (* process)(void * inst,
                       const float * const * in, uint32_t numIn,
                       float ** out, uint32_t numOut,
                       uint32_t frames,
                       const tSynthLibTransport * transport);

    // ---- events --------------------------------------------------------------------------------
    //
    // ON THE AUDIO THREAD, after blockBegin() and before process(), with the MIDI channel (0..15) and
    // the frame within this block the event lands on. An engine that cannot place an event inside a
    // block - G2 Alike's cannot - ignores both; one that can, uses them. An Audio Unit hands its MIDI
    // over before the render it belongs to, so that wrapper queues it and delivers it here, in the
    // same place a VST3 host's event list is walked.

    void   (* noteOn)(void * inst, uint8_t channel, uint8_t note, float velocity,
                      uint32_t sampleOffset);                                     // velocity 0..1
    void   (* noteOff)(void * inst, uint8_t channel, uint8_t note, float velocity,
                       uint32_t sampleOffset);
    void   (* polyPressure)(void * inst, uint8_t channel, uint8_t note, float pressure,
                            uint32_t sampleOffset);                               // may be NULL

    // ---- parameters ----------------------------------------------------------------------------
    //
    // NORMALIZED 0..1 THROUGHOUT. Plain units exist only where a host insists on them, and the
    // wrapper converts using plainMin/plainMax so the plug-in never has to hold both.

    // THE DYNAMIC ALTERNATIVE TO THE STATIC TABLE. A plug-in leaves the descriptor's `params` NULL
    // and fills these in instead; anything with a fixed parameter list should use the table, which
    // is checkable at compile time.
    //
    // THE COUNT AND THE IDS MUST NOT CHANGE for the life of a variant - both wrappers size their own
    // storage from them once - though the titles may. And both must answer with inst == NULL; see
    // the note at the top of this file.
    //
    // A HOST WALKS 0..paramCount()-1 AND EXPECTS EVERY ONE OF THEM TO EXIST. Returning false for an
    // index below the count is not a way to hide a parameter - GenBridge shipped a variant that
    // advertised seven and had six, and what a host does with the refusal is its own business. Use
    // SYNTHLIB_PARAM_HIDDEN.
    uint32_t (* paramCount)(const tSynthLibPluginDesc * desc, void * inst);
    bool     (* paramInfo)(const tSynthLibPluginDesc * desc, void * inst, uint32_t index,
                           tSynthLibParamDesc * out);

    // WHICH PARAMETER A MIDI CONTROL BECOMES, when the table's midiControl column cannot say - it has
    // no channel, and a plug-in passing controllers through to hardware needs a different parameter
    // for each one on each channel. `control` is a controller number or SYNTHLIB_MIDI_AFTERTOUCH /
    // _PITCH_BEND. Return false for "none". May be NULL, and then the table decides, on every channel.
    bool     (* midiMapping)(const tSynthLibPluginDesc * desc, void * inst, uint8_t channel,
                             int16_t control, uint32_t * idOut);

    void   (* setParam)(void * inst, uint32_t id, double normalized);
    double (* getParam)(void * inst, uint32_t id);

    // EVERY POINT A HOST DELIVERED FOR ONE PARAMETER IN THIS BLOCK, in order, on the audio thread.
    // May be NULL, and then the wrapper hands only the LAST point to setParam() - which is right for
    // anything that is a level rather than an event.
    //
    // It is not right for a momentary button. An editor presses one and releases it straight away, a
    // host may deliver both in the same block, and reading only the last point sees nothing but the
    // release: GenBridge's Measure did nothing, every time, until it looked at all of them.
    void   (* paramPoints)(void * inst, uint32_t id, const tSynthLibParamPoint * points,
                           uint32_t count);

    // How the value reads. Return false and the wrapper prints the plain number with the unit,
    // which is right for most parameters - this is for the ones where it is not ("-inf"), and for
    // the entries of a SYNTHLIB_PARAM_LIST.
    bool   (* paramText)(const tSynthLibPluginDesc * desc, void * inst, uint32_t id,
                         double normalized, char * out, size_t len);

    // ---- state ---------------------------------------------------------------------------------
    //
    // An opaque blob the host stores with its project. Called with out == NULL to ask how big it
    // is; the wrapper then calls again with a buffer of at least that size. Return 0 for a plug-in
    // with nothing to save beyond its parameters, which both wrappers persist by themselves - all
    // except the SYNTHLIB_PARAM_NO_SAVE ones.
    //
    // A PROJECT SAVED BEFORE THE WRAPPER EXISTED arrives at setState() whole, exactly as the plug-in
    // wrote it then - see synthlibPluginState.h. That is what lets a plug-in move onto this wrapper
    // without emptying the slot in somebody's existing project.

    size_t (* getState)(void * inst, void * out, size_t len);
    void   (* setState)(void * inst, const void * data, size_t len);

    // THE PARAMETER VALUES A BLOB OF YOURS IMPLIES, WITHOUT AN INSTANCE. Fill `out` (room for
    // `capacity`) and return how many.
    //
    // A VST3 host hands the processor's state to the CONTROLLER as well, precisely so the panel comes
    // up showing what was loaded - and the controller has no instance to load it into. For anything
    // the wrapper saved itself that is no problem; for a NO_SAVE parameter, or for a project saved
    // before the wrapper did any saving, only the plug-in knows what its own bytes mean. Without this
    // two tracks saved with a Kronos and a Helix both reopened showing the first device in the list.
    // May be NULL.
    uint32_t (* stateParams)(const tSynthLibPluginDesc * desc, const void * data, size_t len,
                             tSynthLibParamValue * out, uint32_t capacity);

    // ---- the other half of itself ----------------------------------------------------------------

    // A SMALL MESSAGE FROM THE PROCESSOR TO THE CONTROLLER, OR BACK. VST3 splits a plug-in in two
    // and IConnectionPoint is the only channel between them; an Audio Unit is one object and needs
    // no channel at all, so this is delivered straight through there. Either way it arrives
    // synchronously, on the thread that sent it.
    //
    // It carries an id and an integer and nothing else, on purpose: a channel that can carry a block
    // of bytes invites a per-frame stream that belongs in shared memory instead. Ids beginning
    // "synthlib." are the wrapper's own and never reach this. May be NULL.
    void   (* message)(void * inst, const char * id, int64_t value);

    // ---- editor --------------------------------------------------------------------------------

    // Build the editor and hand back an NSView as a void *, or NULL for a plug-in with no editor of
    // its own (the host then draws a generic panel from the parameter table). Called on the main
    // thread. Typed as void * so this header stays free of Cocoa and can be included from C.
    //
    // `inst` IS THE INSTANCE THIS EDITOR BELONGS TO - or NULL, in the one case where a VST3 host has
    // not connected the controller to its processor and there is more than one to choose from. The
    // DESCRIPTOR comes too, because that NULL still has to draw the right variant's panel.
    //
    // RETURNED RETAINED (+1), and the wrapper releases it. Under ARC that means handing back
    // (__bridge_retained void *)view: an autoreleased object crossing a void * is invisible to ARC
    // on both sides, so the ownership has to be stated once, here, rather than guessed at each end.
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

    // THE VST3 CONTROLLER ALSO HANDS EVERY VALUE IT IS GIVEN STRAIGHT TO ITS INSTANCE, on the UI
    // thread, as well as leaving the host to deliver it to the processor.
    //
    // VST3 keeps the two apart: a value set on the controller reaches the processor only because the
    // host routes it there, in time order, inside a block. A bare test host does not route it at all,
    // and G2 Alike - whose parameters are levels, where an early duplicate is harmless - sets this so
    // a move on such a host's generic panel is still heard. A plug-in whose parameters are EVENTS must
    // leave it false: GenBridge would otherwise send each controller to the hardware twice, the second
    // copy out of order and unstamped.
    bool         controllerAppliesParams;

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
    double       editorMaxWidth;        // 0 == no maximum

    // LOCKED, if non-zero: width / height, kept on every resize. Both canvases need this - they
    // scale from width alone, so a taller window would otherwise uncover rows, or lose them, rather
    // than drawing larger. Which edge the user is dragging does not matter: see checkSizeConstraint()
    // in synthlibPluginVst3View.mm.
    double       editorAspect;

    // Remembering the size across sessions. Both wrappers call these, so an editor opened in Live
    // and one opened in Logic agree. Either may be NULL, and then the default width is used.
    //
    // A VST3 PROJECT ALSO REMEMBERS ITS OWN. The controller keeps the width in its state, so a set
    // reopens each instance's editor at the size it was left and these are only the starting point
    // for one that has never been opened.
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
//
// EACH OF THESE NAMES AN INSTANCE - the pointer create() returned - because each is a question about
// ONE copy of the plug-in: this copy's host channel, this copy's editor window. With one instance
// loaded that looked like an unnecessary argument; with two, a global "the controller" reported one
// track's edits on the other.

// TELL THE HOST A PARAMETER MOVED BECAUSE THE PLUG-IN MOVED IT - the user turned something in our own
// editor, or the plug-in corrected a value itself (a device that could not give the channel asked for).
//
// Without this a host sees a value appear from nowhere: it cannot record the move as automation, its
// own generic panel goes out of step with ours, and a VST3 host will overwrite the value from its own
// copy at the next opportunity. VST3 spells it beginEdit/performEdit/endEdit on the component
// handler; an Audio Unit posts parameter events. Both are behind this one call.
//
// ANY THREAD. The host side of both formats wants the main thread, so a call from anywhere else is
// posted there and delivered shortly after; from the main thread it happens before this returns.
void synthlib_plugin_param_edited(void * inst, uint32_t id, double normalized);

// WHAT THE HOST THINKS A PARAMETER IS SET TO, for an editor to draw. On VST3 that is the controller's
// value, which is the one a host's own panel shows and which can differ from the processor's for a
// block or two; on an Audio Unit there is only one. Main thread.
double synthlib_plugin_param_value(void * inst, uint32_t id);

// OUR LATENCY CHANGED, and the host should read latencySamples() again. A host caches the figure from
// activation, so without this a plug-in whose latency depends on a device it opened later reports the
// wrong one for the rest of the session. Any thread, as synthlib_plugin_param_edited().
void synthlib_plugin_latency_changed(void * inst);

// Ask the host to resize this instance's editor window, after the plug-in has changed its own idea of
// how big it should be. Returns false if the host offers no such channel, which is common; the caller
// should then leave the window alone rather than resizing its view inside a frame that did not move.
// Main thread.
bool synthlib_plugin_request_resize(void * inst, double width, double height);

// Send one small message to the other half of the plug-in - see the `message` callback. Returns
// false when there is nothing connected to send it to, which on VST3 is every moment before the
// host has joined the two halves up.
bool synthlib_plugin_send_message(void * inst, const char * id, int64_t value);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_H__
