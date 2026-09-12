# synthlibMidi.h notes

The longer comments from `synthlibMidi.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

THE COREMIDI SEND PRIMITIVE, ONCE.

EmuUtility and SynthEdit each carried their own midi_send_to(): the same MIDIPacketListInit /
MIDIPacketListAdd / MIDISend over the same out port under the same mutex, character-identical
apart from a log string. They differed in exactly two ways, and BOTH of those differences were
SynthEdit having already been bitten:

```
  * THE BUFFER. EmuUtility packs into 512 bytes of stack. SynthEdit used to as well, which was
    ample for everything it sends EXCEPT a whole-bank restore — 18734 bytes in a real capture.
    Past 512, MIDIPacketListAdd simply fails.
  * THE RETURN VALUE. EmuUtility's returns void, as SynthEdit's did. That is what turned the
    failure above into a lie: the send logged an error, but the caller had nothing to test, so
    Restore Bank reported success while nothing had reached the wire. Found 2026-07-11 from the
    owner's own MIDI Monitor capture showing no traffic at all.

```
So this is not a tidy-up that happens to remove duplication — EmuUtility today has the 512-byte
limit AND no way to notice it, which is the identical bug waiting for a large enough message.
Sharing the one that learned fixes it there for free.

WHAT IS DELIBERATELY NOT HERE: scan, connect and dispatch. Those have diverged between the two
apps for real reasons — SynthEdit's is a multi-device framework with deferred identity replies,
three destination-matching fallbacks and per-source running-status parsing; EmuUtility's is a
single-device E-mu client — and merging them mechanically would be a bad trade. This is the safe
piece: the bytes-to-the-wire primitive, which is the same everywhere.

This header includes CoreMIDI, as synthlibWindow.h includes GLFW and for the same reason: the
thing it describes IS CoreMIDI. Nothing platform-free links it.

## 2. `synthlib_midi_set_out_port()`

The port everything is sent through. Call once, straight after MIDIOutputPortCreate() succeeds;
pass 0 when tearing the client down, and sends become no-ops rather than touching a stale port.

The port stays the application's to CREATE — naming it is the app's business ("EmuUtility Out",
"SynthEdit Out") and creation sits inside the connect logic that is staying put.

## 3. `SYNTHLIB_MIDI_MAX_MESSAGE`

Sends one message. Returns false and logs if it could not be packed or MIDISend failed — CHECK IT
for anything whose success is reported to the user; see the note above about Restore Bank.

Serialised internally, so callers on different threads need no lock of their own. Messages up to
SYNTHLIB_MIDI_MAX_MESSAGE bytes are packed from a shared buffer rather than the stack, so a bank
restore does not need a 64KB stack frame at every call site that never sends one.
