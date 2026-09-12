# synthlibPlugin.h notes

The longer comments from `synthlibPlugin.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

WHAT A PLUG-IN IS, SAID ONCE, IN C.

A plug-in project fills in one of these and links a WRAPPER: synthlibPluginVst3.cpp turns it into
a VST3, synthlibPluginAu.c turns it into an Audio Unit. Neither format appears anywhere in this
header, and neither wrapper knows anything about the plug-in beyond what is below.

THIS EXISTS BECAUSE THE SECOND FORMAT WOULD OTHERWISE HAVE BEEN A SECOND COPY. G2-Edit's
g2Vst3.cpp was 982 lines, of which the parts that were actually about the G2 - ten parameters,
a patch path, note on and note off - came to well under two hundred. An Audio Unit written the
same way would have duplicated the other eight hundred in a different dialect, and every fix
after that would have had to be made twice. The three sibling plug-ins already show what that
looks like: their *View.m files differ by 85 lines out of 360 and are the same file.

PLAIN C, DELIBERATELY. VST3 needs C++ and an Audio Unit's view needs Objective-C, so the one
language both can call without ceremony is the one the shared description is written in. Nothing
here allocates, so a descriptor can be - and in practice is - a file-scope constant.

EVERY INSTANCE IS ITS OWN (2026-09-11). The first version of this contract assumed one instance of
each plug-in per process, because G2 Alike's engine is process-wide and nothing else had been
wrapped. GenBridge and MidiSyncTool are routinely loaded several times at once - one per synth,
one per track - and a plug-in that asks the wrapper for something ("tell the host this moved")
therefore always says WHICH instance it is asking for. See "What the plug-in may ask of the
wrapper" at the foot of this file.

THREADING. The wrapper calls process(), blockBegin(), paramPoints() and the note callbacks on the
host's audio thread, and everything else on its UI thread, with one exception: setParam() may
arrive on EITHER, since an Audio Unit host sets parameters from wherever it likes. A plug-in must
therefore make setParam() safe against process(); the usual answer, and G2-Edit's, is that every
value it touches is stored through an atomic.

SOME CALLBACKS GET NO INSTANCE. paramCount(), paramInfo(), paramText(), midiMapping() and
stateParams() may be called with inst == NULL, because a VST3 controller is built - and asked about
its parameters - before it knows which processor it belongs to, and may never be told at all by a
host that does not connect the two halves. They are handed the descriptor instead, which is enough
to tell one variant from another.

## 2. `SYNTHLIB_PARAM_HIDDEN`

WHAT A HOST MAY DO WITH A PARAMETER, beyond setting it.

HIDDEN is somewhere for a host to DELIVER a value, not something to draw or automate. GenBridge has
two thousand of them: a VST3 host turns every MIDI controller into a parameter change, so passing a
controller through to the hardware needs a parameter per controller per channel, none of which
belongs in a host's automation lane.

LIST says the stepped values are names rather than numbers - a host draws a drop-down and reads
the entries through paramText().

NO_SAVE keeps a parameter out of the block the WRAPPER saves. Two kinds want it: a pass-through
whose value means nothing a moment later, and a value the plug-in saves better itself - a device
picker's index is only meaningful against the device list it was chosen from, so GenBridge keeps
the device's NAME in its own state and derives the index again on load.

## 3. `SYNTHLIB_MIDI_NONE`

The MIDI controls a HOST INTERCEPTS rather than delivering as events, and which therefore have to
be named as parameters or they are lost.

A VST3 host converts continuous controllers, channel pressure and pitch bend into parameter
changes and asks IMidiMapping which parameter each becomes; an Audio Unit host hands the same
bytes over raw and the wrapper does that mapping itself. Either way the answer comes from the
table below - or from midiMapping(), for a plug-in that needs the channel too - so the two formats
cannot disagree about it.

The numbers are MIDI controller numbers, extended above 127 the way VST3's ControllerNumbers does
- which is where the two out-of-band entries get their values, so no translation is needed there.

## 4. file scope

The range in DISPLAYED units. A wrapper maps 0..1 onto this linearly and back; anything that
is not linear in its own units belongs in paramText() rather than in a curve here, because
the two formats disagree about which of the two numbers they hand around and a curve would
then have to be inverted in one of them.

## 5. `SYNTHLIB_PARAM_TITLE_MAX`

THE SAME THING, BUT IT OWNS ITS STRINGS. A plug-in whose parameter NAMES are not known until it is
running - GenBridge's device and MIDI-destination pickers are named after whatever hardware is
plugged in - cannot answer with a pointer into a static table, because there is no static table
for it to point into.

So the callback form fills one of these, the table form is copied into one, and the two wrappers
only ever see this. Without that split each wrapper would have had to know about both sources.

THE ID IS NOT THE INDEX. A host walks parameters by index and quotes them back by id, and a plug-in
is free to leave gaps - GenBridge's pass-throughs start at 1000 so that its own parameters can grow
without renumbering anything a saved project refers to.

## 6. `tSynthLibBus`

AN AUDIO BUS, described rather than counted. A count was enough while the only plug-in here was a
stereo instrument with no input; it is not enough for an effect with a SIDE-CHAIN, which is a
second input bus that must be declared, must be marked auxiliary, and must NOT be active by
default - a host asked to fill an input the plug-in does not need will connect something to it and
then wonder why nothing happens.

## 7. `tSynthLibSetup`

HOW THE HOST INTENDS TO RUN US. Given once the host has decided, before the first block.

`offline` is a render FASTER THAN REALTIME - a bounce. A plug-in whose audio comes off a wire at
one second per second has nothing to give it and should know, rather than silently producing a
bounce full of whatever the ring happened to hold.

## 8. `tSynthLibTransport`

THE HOST'S TRANSPORT, AS MUCH OF IT AS THE HOST WILL SAY. Every field has its own validity flag
rather than a sentinel, because "tempo 0" and "the host did not tell us the tempo" are different
facts and a plug-in that generates a clock has to be able to tell them apart. On VST3 each value is
whatever the host wrote, flagged or not - check the flag before trusting one.

THE TWO FORMATS DO NOT OFFER THE SAME THING HERE, and the difference is not cosmetic. VST3 hands
a ProcessContext to every process() call, filled by the host, on the audio thread. An Audio Unit
offers HostCallbacks - function pointers the plug-in CALLS - and a host may install none of them,
may install some, and answers from whatever the host feels like rather than from the block being
rendered. So `valid` is false far more often on an Audio Unit, and a plug-in whose whole purpose
is timing accuracy should be measured on both before either is trusted.

## 9. file scope

One instance per copy in the host. May return NULL, which the wrapper reports as a failure to
instantiate rather than handing the host a half-built plug-in.

GIVEN ITS OWN DESCRIPTOR, because one binary may register several VARIANTS of itself and a
shared implementation has to know which one it is being made as - GenBridge registers the same
code twice, as an effect and as an instrument, and the two differ in their bus layout and in
how many parameters they own.

## 10. file scope

THE HOST HAS STARTED, OR STOPPED, FEEDING US BLOCKS - distinct from setActive(), and the moment
anything that measures the gap between one block and the next must stop assuming the next one
follows the last. May be NULL, and then the wrapper calls reset() on the way in, which is what
an instrument wants.

## 11. file scope

THE START OF A BLOCK, before any of its parameter changes or notes are delivered. For a plug-in
that places those in wall-clock time - GenBridge stamps every MIDI byte it sends from the moment
the block began - so it has to be told where that is BEFORE the first event rather than
discovering it in process(), after them. May be NULL.

## 12. file scope

Fill out[channel][0..frames-1]. Called on the audio thread; allocate nothing here.

DE-INTERLEAVED, because that is what both formats hand over - a VST3 channelBuffers32 and an
AudioBufferList of one-channel buffers are the same shape, and an engine rendering interleaved
frames (G2-Edit's does) de-interleaves once, here, instead of once per wrapper.

`in` is the first input bus, or NULL for a plug-in that declared none - and also NULL when the
host has not connected one, which is a thing a host may legitimately do to a bus the plug-in
said was auxiliary. `in` and `out` may be the SAME buffers: a host is entitled to process in
place. `transport` is never NULL, but its `valid` is often false; see above.

## 13. file scope

---- events --------------------------------------------------------------------------------

ON THE AUDIO THREAD, after blockBegin() and before process(), with the MIDI channel (0..15) and
the frame within this block the event lands on. An engine that cannot place an event inside a
block - G2 Alike's cannot - ignores both; one that can, uses them. An Audio Unit hands its MIDI
over before the render it belongs to, so that wrapper queues it and delivers it here, in the
same place a VST3 host's event list is walked.

## 14. file scope

---- parameters ----------------------------------------------------------------------------

NORMALIZED 0..1 THROUGHOUT. Plain units exist only where a host insists on them, and the
wrapper converts using plainMin/plainMax so the plug-in never has to hold both.

## 15. file scope

THE DYNAMIC ALTERNATIVE TO THE STATIC TABLE. A plug-in leaves the descriptor's `params` NULL
and fills these in instead; anything with a fixed parameter list should use the table, which
is checkable at compile time.

THE COUNT AND THE IDS MUST NOT CHANGE for the life of a variant - both wrappers size their own
storage from them once - though the titles may. And both must answer with inst == NULL; see
the note at the top of this file.

A HOST WALKS 0..paramCount()-1 AND EXPECTS EVERY ONE OF THEM TO EXIST. Returning false for an
index below the count is not a way to hide a parameter - GenBridge shipped a variant that
advertised seven and had six, and what a host does with the refusal is its own business. Use
SYNTHLIB_PARAM_HIDDEN.

## 16. file scope

WHICH PARAMETER A MIDI CONTROL BECOMES, when the table's midiControl column cannot say - it has
no channel, and a plug-in passing controllers through to hardware needs a different parameter
for each one on each channel. `control` is a controller number or SYNTHLIB_MIDI_AFTERTOUCH /
_PITCH_BEND. Return false for "none". May be NULL, and then the table decides, on every channel.

## 17. file scope

EVERY POINT A HOST DELIVERED FOR ONE PARAMETER IN THIS BLOCK, in order, on the audio thread.
May be NULL, and then the wrapper hands only the LAST point to setParam() - which is right for
anything that is a level rather than an event.

It is not right for a momentary button. An editor presses one and releases it straight away, a
host may deliver both in the same block, and reading only the last point sees nothing but the
release: GenBridge's Measure did nothing, every time, until it looked at all of them.

## 18. file scope

---- state ---------------------------------------------------------------------------------

An opaque blob the host stores with its project. Called with out == NULL to ask how big it
is; the wrapper then calls again with a buffer of at least that size. Return 0 for a plug-in
with nothing to save beyond its parameters, which both wrappers persist by themselves - all
except the SYNTHLIB_PARAM_NO_SAVE ones.

A PROJECT SAVED BEFORE THE WRAPPER EXISTED arrives at setState() whole, exactly as the plug-in
wrote it then - see synthlibPluginState.h. That is what lets a plug-in move onto this wrapper
without emptying the slot in somebody's existing project.

## 19. file scope

THE PARAMETER VALUES A BLOB OF YOURS IMPLIES, WITHOUT AN INSTANCE. Fill `out` (room for
`capacity`) and return how many.

A VST3 host hands the processor's state to the CONTROLLER as well, precisely so the panel comes
up showing what was loaded - and the controller has no instance to load it into. For anything
the wrapper saved itself that is no problem; for a NO_SAVE parameter, or for a project saved
before the wrapper did any saving, only the plug-in knows what its own bytes mean. Without this
two tracks saved with a Kronos and a Helix both reopened showing the first device in the list.
May be NULL.

## 20. file scope

A SMALL MESSAGE FROM THE PROCESSOR TO THE CONTROLLER, OR BACK. VST3 splits a plug-in in two
and IConnectionPoint is the only channel between them; an Audio Unit is one object and needs
no channel at all, so this is delivered straight through there. Either way it arrives
synchronously, on the thread that sent it.

It carries an id and an integer and nothing else, on purpose: a channel that can carry a block
of bytes invites a per-frame stream that belongs in shared memory instead. Ids beginning
"synthlib." are the wrapper's own and never reach this. May be NULL.

## 21. file scope

Build the editor and hand back an NSView as a void *, or NULL for a plug-in with no editor of
its own (the host then draws a generic panel from the parameter table). Called on the main
thread. Typed as void * so this header stays free of Cocoa and can be included from C.

`inst` IS THE INSTANCE THIS EDITOR BELONGS TO - or NULL, in the one case where a VST3 host has
not connected the controller to its processor and there is more than one to choose from. The
DESCRIPTOR comes too, because that NULL still has to draw the right variant's panel.

RETURNED RETAINED (+1), and the wrapper releases it. Under ARC that means handing back
(__bridge_retained void *)view: an autoreleased object crossing a void * is invisible to ARC
on both sides, so the ownership has to be stated once, here, rather than guessed at each end.

## 22. file scope

AN OVERRIDE, NOT A REQUIREMENT. NULL takes "Instrument|Synth" or "Fx" from isInstrument, which
is right for most things. GenBridge wants "Fx|NoOfflineProcess|Tools", and NoOfflineProcess is
not decoration there: a live capture has nothing to give a faster-than-realtime render, so a
host bouncing offline must not call it at all - without the flag it bounces silence and looks
like a plug-in bug.

## 23. file scope

THE VST3 CONTROLLER ALSO HANDS EVERY VALUE IT IS GIVEN STRAIGHT TO ITS INSTANCE, on the UI
thread, as well as leaving the host to deliver it to the processor.

VST3 keeps the two apart: a value set on the controller reaches the processor only because the
host routes it there, in time order, inside a block. A bare test host does not route it at all,
and G2 Alike - whose parameters are levels, where an early duplicate is harmless - sets this so
a move on such a host's generic panel is still heard. A plug-in whose parameters are EVENTS must
leave it false: GenBridge would otherwise send each controller to the hardware twice, the second
copy out of order and unstamped.

## 24. file scope

---- identity, per format ------------------------------------------------------------------

A HOST REMEMBERS A PLUG-IN BY THESE. Once a project has been saved against a build, none of
them may ever change, or that project reopens with an empty slot where the plug-in was.

## 25. file scope

LOCKED, if non-zero: width / height, kept on every resize. Both canvases need this - they
scale from width alone, so a taller window would otherwise uncover rows, or lose them, rather
than drawing larger. Which edge the user is dragging does not matter: see checkSizeConstraint()
in synthlibPluginVst3View.mm.

## 26. file scope

Remembering the size across sessions. Both wrappers call these, so an editor opened in Live
and one opened in Logic agree. Either may be NULL, and then the default width is used.

A VST3 PROJECT ALSO REMEMBERS ITS OWN. The controller keeps the width in its state, so a set
reopens each instance's editor at the size it was left and these are only the starting point
for one that has never been opened.

## 27. `tSynthLibPluginSet`

ONE BINARY MAY REGISTER SEVERAL PLUG-INS. Usually it is one and this is a list of length 1, but
GenBridge registers the same code twice - once as an effect and once as an instrument - because
VST3 instruments live on instrument tracks and effects do not, and a user needs whichever their
host will let them put where they want it.

## 28. `synthlib_plugin_param_edited()`

------------------------------------------------------------------------------------------------
What the plug-in may ask of the wrapper
------------------------------------------------------------------------------------------------

EACH OF THESE NAMES AN INSTANCE - the pointer create() returned - because each is a question about
ONE copy of the plug-in: this copy's host channel, this copy's editor window. With one instance
loaded that looked like an unnecessary argument; with two, a global "the controller" reported one
track's edits on the other.

## 29. `synthlib_plugin_param_edited()`

TELL THE HOST A PARAMETER MOVED BECAUSE THE PLUG-IN MOVED IT - the user turned something in our own
editor, or the plug-in corrected a value itself (a device that could not give the channel asked for).

Without this a host sees a value appear from nowhere: it cannot record the move as automation, its
own generic panel goes out of step with ours, and a VST3 host will overwrite the value from its own
copy at the next opportunity. VST3 spells it beginEdit/performEdit/endEdit on the component
handler; an Audio Unit posts parameter events. Both are behind this one call.

ANY THREAD. The host side of both formats wants the main thread, so a call from anywhere else is
posted there and delivered shortly after; from the main thread it happens before this returns.

## 30. `synthlib_plugin_request_resize()`

Ask the host to resize this instance's editor window, after the plug-in has changed its own idea of
how big it should be. Returns false if the host offers no such channel, which is common; the caller
should then leave the window alone rather than resizing its view inside a frame that did not move.
Main thread.
