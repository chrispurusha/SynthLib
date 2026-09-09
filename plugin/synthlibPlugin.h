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

// ------------------------------------------------------------------------------------------------
// What the wrapper asks of the plug-in
// ------------------------------------------------------------------------------------------------

typedef struct {
    // ---- lifecycle -----------------------------------------------------------------------------

    // One instance per copy in the host. May return NULL, which the wrapper reports as a failure to
    // instantiate rather than handing the host a half-built plug-in.
    void * (* create)(void);
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
    void   (* render)(void * inst, float ** out, uint32_t numChannels, uint32_t frames);

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

typedef struct {
    const char * name;                  // "G2 Alike" - what a host lists
    const char * vendor;
    const char * url;
    const char * email;
    const char * version;               // "0.1.0", for display

    bool         isInstrument;          // false == an effect with audio in
    uint32_t     numInputChannels;      // 0 for an instrument
    uint32_t     numOutputChannels;
    bool         wantsMidi;             // an effect can want MIDI too

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

    tSynthLibPluginCallbacks cb;
} tSynthLibPluginDesc;

// THE ONE SYMBOL A PLUG-IN PROJECT HAS TO PROVIDE. Both wrappers call it, once, and hold on to what
// comes back for the life of the process - so it must return a pointer that outlives every call,
// which in practice means a file-scope constant.
const tSynthLibPluginDesc * synthlib_plugin_descriptor(void);

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

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_PLUGIN_H__
