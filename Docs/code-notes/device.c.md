# device.c notes

The longer comments from `device.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

Enumerating devices, reading and setting their rate and buffer size, opening an IOProc, and
working out what they cost in latency. CoreAudio and nothing else - no project's types appear in
it, which is what makes it shareable.

IT LIVED IN TWO PLACES UNTIL 2026-09-09, as GenBridge's poc/device.c and MidiSyncTool's
msDevice.c, both saying "verbatim copy, fix a bug here and fix it there". It did not happen: two
fixes went into one copy and neither reached the other, and nothing would have said so - the
drift was found by diffing the files, months later. That is the argument for this directory.

NOT IN SynthLib/src, DELIBERATELY. That folder is a synchronized group in G2-Edit's Xcode project
and it recurses, so anything put there is compiled into that APPLICATION whether it wants it or
not. Everything here is listed by hand in the do-vst3 / do-poc script of whichever project needs
it, which is also how those scripts already treat SynthLib's own sources.

## 2. in `device_set_sample_rate_and_wait()`

Bounded, because this is called from setActive() - the HOST'S MAIN THREAD - where a plug-in
is expected to do its expensive set-up but not to stall the application. Two seconds per
instance made Ableton visibly slow to load a set with several of them. A device that has not
taken the rate in under a second is not going to.

## 3. `device_set_buffer_frames()`

CONFIRMED, NOT ASSUMED - and noErr is not confirmation.

AudioObjectSetPropertyData() returning noErr means the request was ACCEPTED, not that the device
has reconfigured. CoreAudio applies a buffer size change asynchronously and posts a property
notification when it lands, so a read-back taken immediately afterwards can still report the old
size. The caller then sizes its ring from that old size and tells the host a latency to match.

Symptom: opening a project with two instances announced "attempting to set 64, getting 512" on
both, and setting 64 BY HAND afterwards worked every time - the second attempt succeeding because
the first had by then taken effect. A race that looks exactly like a device refusing a request.

Polled rather than waiting on the notification, because the caller is a worker thread inside a
device open that is already slower than this, and a listener here would need a run loop and an
unsubscribe path for a wait that is normally over in a millisecond or two.

## 4. `stream_latency_frames()`

THE STREAM'S OWN LATENCY, which is a fourth term and not the device's.

kAudioDevicePropertyLatency is what the DEVICE reports; a stream within it can declare more on top
- format conversion, DSP in the path - and CoreAudio reports that separately, on the stream object
rather than the device. Measured on this rig: zero on every USB and Thunderbolt interface, and
2399 frames (50 ms at 48 kHz) on the built-in microphone, 690 on the built-in speakers.

That distribution is exactly why it went unnoticed - it is zero on the devices a bridge is
actually pointed at, and only the built-in hardware pays it. Left out, the host is told a figure
50 ms short and its delay compensation is wrong by that much.

## 5. in `stream_latency_frames()`

The FIRST stream in scope, as JUCE does. A device with several streams in one direction can in
principle declare a different latency on each, but the channels this bridge takes all come
from one of them, and there is no meaningful way to report two numbers to a host that wants
one.

## 6. `device_is_running_somewhere()`

IS SOMEONE ELSE ALREADY DRIVING THIS DEVICE?

Rate and buffer size are GLOBAL properties: setting either one changes it for every client of the
device at once, the host included. That is fine on a device nobody else has open and actively
harmful on one the host is running its own audio through - and the two cases are indistinguishable
without asking, which is what this asks.

The case that motivates it: a mixer used as the host's own output AND as the bridge's capture
source. There, the device's buffer frame size IS the host's block size, so imposing one means the
plug-in setting its own process() call rate on hardware it does not own.

## 7. `DEVICE_WATCH_MAX`

---- Hot-plug ----------------------------------------------------------------------------------

ONE CoreAudio LISTENER FOR THE WHOLE PROCESS, fanned out to however many plug-in instances are
loaded. A listener per instance would work too, but a host with a dozen GenBridges in a set would
then hold a dozen registrations for one property, and CoreAudio would call all of them anyway.

## 8. `gather()`

CoreAudio hands over an AudioBufferList whose layout varies by device: one buffer holding N
interleaved channels, or N buffers of one channel each, or something in between. Rather than
assume, walk the buffers and track a running channel index - which covers every layout with one
piece of code, and is the reason this loop looks more general than it first appears it needs to.
