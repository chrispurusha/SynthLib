# synthlibMidiPorts.h notes

The longer comments from `synthlibMidiPorts.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

SPLIT OUT OF synthlibMidi.c ON 2026-09-11, THE DAY IT WENT IN. The choice is saved in prefs, and the
send primitive is compiled by plug-ins that have no prefs to link against - GenBridge stopped
linking the moment this landed beside it. The send primitive stays dependency-free; this file is
for the applications, which get it automatically (SynthLib/src is a synchronized folder).

## 2. `SYNTHLIB_MIDI_PORT_NAME_MAX`

── WHICH PORTS THE USER CHOSE ──────────────────────────────────────────────────────────────────

Both applications used to find their device only by broadcasting an identity request to every
output and taking whichever input answered, then GUESSING the output from the input's entity. That
is right on a single-cable rig and wrong on the owner's, where a synth is driven through one
interface and answers through another (see find_dest_for_source() and the destination probe). A
port the user picks settles it; the scan stays as the automatic choice.

REMEMBERED BY NAME, because a MIDIEndpointRef does not survive a setup change or a relaunch and a
name is what the user picked. "" means automatic. A chosen port that is not present is WAITED FOR,
never swapped for another - the fall-through that sent GenBridge's notes to whatever came first.

A SCOPE keeps separate choices for separate devices. SynthEdit plays a Z1 on one interface and a
Voyager on another, so one choice per application would be wrong the moment it switched; it scopes
by device configuration. EmuUtility has one device and uses the unscoped choice.
