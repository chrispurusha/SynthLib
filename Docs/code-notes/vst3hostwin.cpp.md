# vst3hostwin.cpp - notes

## 1. What it is

A minimal VST3 host for Windows, for trying any SynthLib plug-in (G2Alike's, the AllAlike ones) without a DAW -
written 2026-11-04 on the Parallels VM, which had none. One file and one self-contained exe, built by
`tools/do-vst3hostwin.ps1` (llvm-mingw, the VST3 SDK's interfaces, G2Alike's `platform/windows/miniaudio.h`).
It takes from G2Alike's `tools/vst3host.mm` (the Mac harness: component first, the controller the component
names, connected, state across) and from GenBridge Monitor (state kept between runs, the editor closed before
the plug-in), and adds what a Windows test needs: audio, MIDI in, the computer keyboard and both DPI modes.

```
tools\vst3hostwin.exe <plugin.vst3> [--midi N] [--note 60] [--seconds N --shot out.bmp] [--dpi-unaware]
```

## 2. `Attributes` / `Message`

The wrapper's processor and controller talk through `IHostApplication::createInstance(IMessage)` - the
processor announcing its serial so the controller knows its instance. Ints, floats, strings and binaries are
all kept, so a plug-in sending more than an int is not silently dropped.

## 3. `ComponentHandler`

An edit made in the editor (`performEdit`) is queued as a parameter change for the next audio block, as a host
passes it to the processor; without a handler the controller has nowhere to send it.

## 4. The computer keyboard

A thread keyboard hook, active only while Caps Lock is on, so the plug-in's own typing (patch names, the file
browser) is untouched otherwise. A W S E D F T G Y H U J K O L P are C to D#, Z and X the octave.

## 5. DPI

Per-monitor aware by default, as the DAWs that scale their plug-ins are: the window's DPI goes to the view
through `IPlugViewContentScaleSupport` BEFORE its size is asked, and again on WM_DPICHANGED. `--dpi-unaware`
runs as a host Windows scales as a bitmap, which tests the other half - the plug-in must then answer 1.0 and
logical-sized pixels (SynthLib's `synthlibPluginVst3ViewWin.cpp` notes §3).

## 6. MIDI controllers

Through the controller's `IMidiMapping`, looked up once for every channel and controller at start, as a host
does: bend, aftertouch and the mapped CCs arrive as parameter changes, notes as events.

## 7. `binary_path()`

A single-file `.vst3` is loaded as it is; a bundle folder (AllAlike's layout) by its
`Contents\<arch>-win\<name>.vst3` for this exe's own architecture.

## 8. State between runs

Saved at exit to `%APPDATA%\vst3hostwin\<plugin>.state` and restored at start, so a patch loaded through the
plug-in's own File menu is there next time. `--fresh` starts without it, `--keep-state` leaves the file alone.
