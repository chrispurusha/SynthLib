# synthlibPluginVst3.cpp notes

The longer comments from `synthlibPluginVst3.cpp`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

THE VST3 SIDE, AND NOTHING ELSE. Everything specific to a particular plug-in arrives through
synthlib_plugin_variants(); this file names no engine, no parameter and no patch.

Built against pluginterfaces/ ONLY: the SDK's public.sdk helper classes are not used, so there is
no CMake and nothing to reconcile with an Xcode-only application build. The cost is that the COM
plumbing below - reference counting, queryInterface, the factory - is written out by hand rather
than inherited. It is dull but it is all here, which is the point.

THREE THINGS IN HERE WERE EACH LEARNED FROM A HOST REFUSING TO LOAD, and none of them are
optional:

```
  1. THE PROCESSOR AND THE CONTROLLER ARE SEPARATE REGISTERED CLASSES. VST3 permits one object to
     implement IComponent + IAudioProcessor + IEditController, which is simpler and which a
     hand-written test host accepts happily. Ableton does not: it obtains the controller by
     instantiating the class named by IComponent::getControllerClassId() and does NOT fall back to
     asking the component for IEditController. With a single object it logged "parameter count is
     0" and its wrench icon opened nothing.

  2. THE FACTORY IS IPluginFactory2 OR BETTER. The base interface reports only a class CATEGORY
     ("Audio Module Class"), which says a plug-in makes audio but not whether it is an instrument
     or an effect. A host that cannot tell assumes effect, looks for the audio input an effect
     must have, finds none, and rejects it:

         error: Vst3: plugin has an effect category, but no valid audio input bus

     The subcategory that settles it - PlugType::kInstrumentSynth - exists only on PClassInfo2,
     which arrived with IPluginFactory2.

  3. IMidiMapping IS WHY PITCH BEND AND THE WHEELS WORK AT ALL. A VST3 host does not deliver them
     as MIDI events the way note on and note off arrive: continuous controllers, channel pressure
     and pitch bend are converted into PARAMETER CHANGES, and this interface is the only place a
     plug-in says which parameter each one becomes. Without it, notes play and every expressive
     control does nothing - silently, since nothing is technically wrong.

```
ONE BINARY, SEVERAL PLUG-INS. synthlib_plugin_variants() may return more than one descriptor, and
then every class below is instantiated once per variant: the factory registers two classes for
each, and each object carries the descriptor it belongs to rather than reaching for a global.

AND SEVERAL COPIES OF EACH. See "Which processor is mine" below: a controller finds its own
processor through the connection the host makes between the two, not through a global that could
only ever describe one of them.

## 2. in `arrangement_for()`

VST3 SPLITS THE PLUG-IN IN TWO, AND ONLY ONE HALF HOLDS THE INSTANCE.

The processor owns what create() returned; the controller owns the parameters the host shows and
the editor. In a correct host the two only ever talk through the IConnectionPoint the host wires
between them - and that is exactly how a controller finds out WHICH processor it belongs to: the
processor announces its serial number over the connection the moment it is made, and the
controller resolves the serial against the table below each time it needs the instance.

A SERIAL, NOT A POINTER. The processor can be destroyed while its controller lives on - the host
decides the order - and a pointer handed across would then name freed memory, or, worse, a NEW
instance that happened to be allocated at the same address. A serial that is no longer in the
table simply resolves to nothing.

THE FALLBACK IS "THE ONLY ONE". A bare test host never connects the two halves at all, and there,
with exactly one instance of a variant loaded, the controller takes that one. With two it takes
neither: guessing would have one track's panel drive the other's engine, which is the bug this
replaced - a single global per variant that went empty as soon as a second copy loaded and left
every editor blank.

## 3. in `processor_for_locked()`

The defaults, and they matter before anything has been played: a parameter left at zero
that means "full bend down" is a plug-in reporting a state it is not in. Nothing applies
them to the engine - the plug-in's own create() is responsible for starting in the state
it advertises - but a host asking is told the truth.

## 4. in `processor_for_locked()`

---- IConnectionPoint ----------------------------------------------------------------------

THE MOMENT THE HOST JOINS THE TWO HALVES is the moment the controller can learn which
processor it has been joined to - so this is where the serial goes across. If the host
connects before initialize() there is no host application yet to make the message with, and
initialize() sends it instead.

## 5. in `processor_for_locked()`

INFINITE, for every plug-in here. kNoTail - which is what a plain 0 means - promises that
nothing comes out once the input goes silent, and a host that believes it may stop
processing a chain with nothing feeding it. The instruments have reverbs and delays in
them; the two effects produce audio that has nothing to do with their input at all.

## 6. in `processor_for_locked()`

THE HOST'S VALUES AS IT GAVE THEM, WITH ITS FLAGS BESIDE THEM - not zeroed where a flag is
clear. A host is not above filling a field it has not flagged, and MidiSyncTool's clock was
built against Live's ProcessContext read exactly that way: blanking the loop ends when
kCycleValid was clear would have handed it a loop of zero length at every wrap. A plug-in
that wants to trust only flagged fields checks the flag, which is what the flags are for.

## 7. in `processor_for_locked()`

---- IMidiMapping --------------------------------------------------------------------------

The plug-in's own mapping when it has one, which can tell the channels apart; otherwise
straight out of the parameter table, the same on every channel, as the applications' Omni is.

## 8. in `processor_for_locked()`

THE CONTROLLER'S OWN STATE, which is a different thing from the processor's and exactly where
an editor's size belongs: the processor has no business knowing how big a window is. Line based
and keyed, so an older build skips a line it does not know rather than rejecting the lot - and
so GenBridge's own "GENBRIDGEGUI1 / editor=w,h", written before it moved onto this wrapper,
reads back as it is.

THE HEIGHT IS WRITTEN BUT NEVER READ. With an aspect lock it is derived from the width, and a
saved height is the aspect of the canvas AS IT WAS: GenBridge grew a row on 2026-09-09 and every
project saved before then reopened too short until a resize put it right.

## 9. in `processor_for_locked()`

Every controller that belongs to this instance - normally one, and none at all when the host has
not connected the two halves and there is more than one instance to choose between. Collected
under the lock and used outside it: everything that destroys a controller runs on the main thread,
which is also where every caller of this is.
