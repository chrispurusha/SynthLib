# ring.h notes

The longer comments from `ring.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tRing`

A single-producer / single-consumer ring, written by one audio callback and read by another.

THE CURSORS ARE ABSOLUTE FRAME COUNTS, NOT BUFFER INDICES. That is taken from JUCE's
AudioIODeviceCombiner (juce_CoreAudio_mac.cpp), and it is the single most useful idea in that
file. Two consequences follow that head/tail indices do not give:

```
  - Overflow and underrun become plain arithmetic on two monotonic numbers, rather than the
    usual "is the gap wrapped or not" case analysis that is so easy to get subtly wrong.
  - The fill depth is meaningful before either side has ever run. There is no ambiguous
    "empty or full?" state at start-up, which is exactly when the latency is not yet known.

```
A uint64 frame count at 96 kHz wraps after about six million years, so wrap-around is not
handled and does not need to be.

JUCE's own comment (line 1870 there) explains why its AbstractFifo could not be used for this:
a generic SPSC ring cannot recover from under/overflow lock-free without either overwriting or
reading stale data. The same reasoning applies here, so recovery is explicit - see ring_read().

## 2. `ring_resync()`

Force the fill depth to exactly 'targetFrames' by moving the READ cursor, discarding whatever
is surplus. Consumer side only - it writes readPos, so calling it from the producer would put
two threads on one cursor.

This is how the bridge starts, and how it recovers. Waiting for the ring to fill to the
setpoint sounds like the natural way to prime it, but it races the two devices: a device that
is slow to deliver its first callback - a DisplayPort output takes about 160 ms - lets the
other one run far past the setpoint first, handing the drift loop an opening error it then
needs minutes to walk off at a few hundred ppm. Snapping the cursor makes the opening error
zero by construction.
