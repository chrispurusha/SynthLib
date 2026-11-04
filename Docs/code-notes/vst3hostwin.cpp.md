# vst3hostwin.cpp - notes

## 1. What it is

A minimal VST3 host for Windows, for trying any SynthLib plug-in (G2Alike's, the AllAlike ones) without a DAW -
written 2026-11-04 on the Parallels VM, which had none. One file and one self-contained exe, built by
`tools/do-vst3hostwin.ps1` (llvm-mingw, the VST3 SDK's interfaces, G2Alike's `platform/windows/miniaudio.h`).
It takes from G2Alike's `tools/vst3host.mm` (the Mac harness: component first, the controller the component
names, connected, state across) and from GenBridge Monitor (state kept between runs, the editor closed before
the plug-in), and adds what a Windows test needs: audio, MIDI in, the computer keyboard and both DPI modes.

```
tools\vst3hostwin.exe [<plugin.vst3>] [--midi N] [--note 60] [--seconds N --shot out.bmp] [--dpi-unaware]
```

Without a plug-in it opens the browser (§10), which is what a double-click or a shortcut does.

## 2. `Attributes` / `Message`

The wrapper's processor and controller talk through `IHostApplication::createInstance(IMessage)` - the
processor announcing its serial so the controller knows its instance. Ints, floats, strings and binaries are
all kept, so a plug-in sending more than an int is not silently dropped.

## 3. `ComponentHandler`

An edit made in the editor (`performEdit`) is queued as a parameter change for the next audio block, as a host
passes it to the processor; without a handler the controller has nowhere to send it.

## 4. The computer keyboard

G2Alike's layout (its `virtualKeyboard.c`): A S D F G H J K L the white keys from C, W E T Y U O P the black,
Z and X the octave, starting at C4. A thread keyboard hook, so it reaches the keys before the plug-in's editor.

The letters play by default, as in G2Alike - but G2Alike knows when a name is being typed and the host cannot,
so CAPS LOCK ON hands them to the plug-in (a patch name, its file browser), and Keyboard > Letters play notes
turns it off for good (`keys.txt`). Never while Ctrl, Alt or Win is held (Alt+K opens the menu), and only while
the plug-in's window is the active one, so the browser, a file dialog or a message box types normally. Each held
key keeps the note it started, so its release ends that note even after an octave shift; all held notes are let
go when the window loses activation, since their key-ups would go to another window. The title bar says which
mode is in force and which C the A key plays.

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

## 9. Started from an icon

It stays a console program, so a terminal run (and a script capturing `--shot` output) behaves as before. When
the console holds no process but this one - Explorer, a shortcut or `Start-Process` gave it a console of its
own - the console is hidden and freed, and the errors a terminal would have printed (`fail()`) come up as
message boxes instead. The console may flash for a moment at start.

## 10. The plug-in browser

Lists, in this order: the recently opened (`%APPDATA%\vst3hostwin\recent.txt`, ten, full paths), this
checkout's own builds (`AllAlike\*\build\windows\*.vst3` and `G2Alike\build-win-*\G2Alike.vst3`, found from the
exe's own place in `SynthLib\tools`), then the installed ones (`%CommonProgramFiles%\VST3` and the per-user
`%LOCALAPPDATA%\Programs\Common\VST3`). Each binary's PE machine field is read, so one built for the other
architecture is listed with the reason and cannot be opened; `vst3hostwin-x64.exe` is the host for x64 builds.
Browse... opens any `.vst3` file (inside a bundle, `Contents\<arch>-win\<name>.vst3`).

## 11. The menu bar

Plug-in > Open another plug-in... shows the browser over the running plug-in; a choice closes this one (its
state saved as at any exit) and opens the new one in a FRESH PROCESS rather than unloading and reloading a DLL
whose statics were never meant to start twice. MIDI input lists the winmm inputs; the choice is remembered by
NAME in `midi.txt`, since the numbers move as devices come and go, and `--midi N` still overrides it. Help
explains the computer keyboard (§4), which the title bar also summarises with the current octave.
