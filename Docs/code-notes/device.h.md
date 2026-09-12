# device.h notes

The longer comments from `device.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `DEVICE_NAME_LEN`

The HAL directly - AudioDeviceCreateIOProcID - rather than a HAL output AudioUnit as
G2-Edit's audioOutput.c uses. An AudioUnit is the right choice when something must be rendered
INTO a device and the unit's own pull model is convenient. Here two devices are being run
against each other and the interesting quantity is when each one's callback fires relative to
the other, so the extra layer only gets in the way. It is also what AudioMovers' feeder does.

## 2. `device_wait_until_idle()`

Waits, up to timeoutMs, for a device to stop reporting that it is running. True if it went idle.

CoreAudio tears an IOProc down asynchronously, so kAudioDevicePropertyDeviceIsRunningSomewhere can
still say "running" for a while after the client that owned it has closed - including when that
client was US. Anything that probes for OTHER clients right after closing its own stream needs
this first, or it sees its own ghost.

## 3. `device_latency_frames()`

deviceLatency + safetyOffset + bufferFrames + streamLatency. The first three are what AudioMovers'
feeder logs separately; the fourth is declared on the STREAM rather than the device and is zero on
every USB and Thunderbolt interface here - but 2399 frames on the built-in microphone, which is
how it stayed missing. Together they are what a host must be told about.

## 4. `void()`

Told when a device is plugged in, unplugged, or otherwise appears or vanishes.

NOTHING NOTICED HOT-PLUG BEFORE THIS. The device list is enumerated when something asks for it and
the plug-in only asks when a parameter changes, so a USB interface switched on after a project was
opened stayed invisible until the user touched a control - which is precisely the case the
"waiting for a saved device" state exists to serve, and it would have waited for ever.

The callback comes from a CoreAudio thread, so it must do no more than set a flag and wake
somebody. Registering the same `user` twice replaces the first entry rather than adding a second.
