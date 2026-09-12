# SynthLib TODO

## The plug-in contract, second version (2026-09-11), and the sibling ports

`plugin/synthlibPlugin.h` was reworked before GenBridge or MidiSyncTool could
move onto the shared wrappers, because the first version assumed ONE INSTANCE
PER VARIANT - true of G2 Alike's process-wide engine, false of both siblings,
which are routinely loaded once per synth or per track. With two copies loaded
the VST3 controller found no instance at all (a per-variant global that went
empty), editors opened blank, and every editor's edits went to whichever
controller was created last. The AU had the same with `gSoleAu`.

**What changed, all in `plugin/`:**

- **Instances.** Each VST3 processor announces a serial number to its own
  controller over the IConnectionPoint the host wires between them
  (`"synthlib.bind"`); the controller resolves it each time. A host that never
  connects the halves gets the old behaviour - the sole instance, or none when
  there are several. Every call a plug-in makes back into the wrapper now names
  its instance: `synthlib_plugin_param_edited(inst, ...)`,
  `_latency_changed(inst)`, `_param_value(inst, id)`, `_request_resize(inst, ...)`,
  `_send_message(inst, ...)`. The host-facing ones may be called from any
  thread and are posted to the main thread.
- **Parameters** have flags - `SYNTHLIB_PARAM_HIDDEN`, `_LIST`, `_NO_SAVE` -
  and ids that need not be indices. Both wrappers keep a `tSynthLibParamStore`
  per half (indexed, found by id); `paramCount()`/`paramInfo()` take the
  descriptor and must answer with no instance. AU list parameters get
  `ParameterValueStrings`; hidden ones are left out of its parameter list.
- **Events** carry the MIDI channel and the sample offset. The AU queues MIDI
  and scheduled parameters and delivers them inside the render they belong to.
- **New callbacks**: `prepare()` (max block, offline), `setProcessing()`,
  `latencySamples()`, `blockBegin()`, `paramPoints()` (every automation point in
  a block, for momentary buttons), `midiMapping()` (per channel), and
  `stateParams()` (parameter values out of the plug-in's own blob, for a VST3
  controller that has no instance and for projects saved before the wrapper).
- **Transport** gained the loop ends, time signature and continuous time; the
  AU fills `systemTime` from the render timestamp's host time.
- **Saved state is "SLP2"**: (id, value) records for saved parameters only,
  then the plug-in's blob. SLP1 and pre-header blobs are still read.
- `controllerAppliesParams` keeps G2 Alike's old behaviour of pushing the
  controller's values straight into the instance; leave it false for a
  plug-in whose parameters are events (GenBridge's controller pass-throughs).

`plugin/test/do-test` builds and runs the offline checks: the state format, and
a fake plug-in linked straight against the VST3 wrapper that records which
instance every call reaches. Run it after any change to the wrappers.

**GenBridge's port - DONE 2026-09-11**, now `vst3/gbPlugin.c` plus `./do-plugin`; `tools/vst3check`
passes 91/91 and `auval` passes both components clean. It needed five more things of the contract,
all now in: `createView()` is given the descriptor (the instance may be NULL and the two variants draw
different panels); `editorMaxWidth`; the VST3 controller keeps each project's editor WIDTH in its own
state (reading GenBridge's old `GENBRIDGEGUI1` too); `checkSizeConstraint()` averages the width a
rect implies with the width its height implies, so a drag of either edge converges; and a plug-in
that saves no parameters through the wrapper has its own bytes written UNWRAPPED, so GenBridge's saved
state is byte-identical to before. The Audio Unit gained `kAudioUnitProperty_BypassEffect` for
effects, and only a unit that takes MIDI answers the MIDI selectors. What it was planned as, against
the old `vst3/gbVst3.cpp`: two variants
(effect + instrument with an aux side-chain); `latencySamples()` =
`gb_bridge_latency()` and `synthlib_plugin_latency_changed()` in place of the
`gbLatency` message; `blockBegin()` = `gb_bridge_block_begin()`; `paramPoints()`
for `kParamMeasure`; `midiMapping()` for the 16 x 130 pass-throughs (HIDDEN |
NO_SAVE at ids 1000+); every other parameter NO_SAVE with `stateParams()` =
`gb_state_parse_active()`, since its own blob already holds them; and the
`gbDeviceSlot`/`gbMode`/`gbFirstChannel`/`gbOffset`/`gbSource` corrections
become `synthlib_plugin_param_edited()`. Its editor's per-project size
(`GENBRIDGEGUI1`) is read by the shared controller, which now keeps every plug-in's width per project.

**MidiSyncTool's port - DONE 2026-09-11**, now `vst3/msPlugin.c` plus
`./do-plugin`: an effect with a pass-through `process()`, `wantsTransport`,
`setProcessing()` for its suspend, and its existing `[4 doubles][name][double]
[name]` blob written and read unwrapped. `msVst3.cpp`'s processor logic moved
across unchanged. `auval` is clean, and `tools/mstDriver` through IAC delivers
the same 333 ticks as the pre-port build with the same interval RMS (0.020-0.023
ms against 0.019-0.022 over three runs each) and identical loop-wrap handling in
its log. One wrapper change came out of it: the VST3 transport now copies the
host's values RAW beside their flags, because `msVst3.cpp` read the loop ends
without checking `kCycleValid`.

**All three plug-ins are on the shared wrappers now**, so `renderBackendGL.c`
no longer accepts the old `G2_VST3_BUILD` spelling. The two panels' views
(`gbView.m`/`msView.m`, 91% alike) became one the same day:
`plugin/synthlibPanelView.m`, driven by a `tSynthLibPanel` of draw calls each
plug-in supplies, with its Objective-C class name taken from the build
(`-DSYNTHLIB_PANEL_VIEW_CLASS`) as the AU view's is.

## The renderer position after 2026-09-09, and what the ports need

**macOS is Metal; OpenGL is what Windows and Linux will run.** That is now the
shape of it, rather than "OpenGL with Metal available":

- `RENDER_BACKEND_DEFAULT` in `renderBackendSelect.h` follows the PLATFORM —
  Metal on `__APPLE__`, OpenGL everywhere else. It was OpenGL unconditionally,
  with a note saying that should hold "until Metal has had real use"; it has,
  and this machine's own `prefs.txt` had already selected Metal by hand.
- `SYNTHLIB_NO_GL_BACKEND` leaves the GL backend out of a build entirely:
  `renderBackend.c` does not declare its table, `gfx_backend_available()`
  answers false for it, and there is an `#error` for the combination that would
  leave no backend at all. **G2-Edit's plug-in defines it** — that is what makes
  it Metal *only* rather than Metal by default, and why `renderBackendGL.c` and
  `-framework OpenGL` are off its build.
- **`renderBackendGL.c` itself is untouched and must stay that way.** It is
  OpenGL 1.1 with no platform in it, it is still what a saved `renderBackend=0`
  selects, and it is the ONLY backend the ports will have. Nothing about the
  Metal work should make it harder to build somewhere else.

### The way back is a pref, not a menu

`synthlibWindow.c` reads `renderBackend` from `prefs.txt` before it makes the
window, so forcing OpenGL on a Mac that has trouble with Metal is one line in a
text file. **G2-Edit's Settings menu no longer offers the switch** — it keeps
only the "Renderer: <name>" readout — because a recovery route should look like
one rather than being a setting to browse.

**SynthEdit and EmuUtility still have their own menu item** (each has its own
`src/appMenuBar.c`). They are on older pins; when they advance, decide whether
to follow. Note SynthEdit's saved pref is `renderBackend=0`, so it stays on
OpenGL whatever the default becomes, until someone changes it.

### What the ports will actually need

Not the renderer — that part is ready. What is macOS-only and has no equivalent
yet:

- `renderBackendMetal.m`, `plugin/pluginStubs.c`'s callers, and every `.m`/`.mm`
  in the projects' `vst3/` folders: the plug-in view is an `NSView` and the
  editor is an `IPlugView` handing one over. On Windows that is an `HWND` and on
  Linux an X11 window, and none of the Cocoa code transfers.
- `audio/device.c` is CoreAudio throughout. WASAPI/ASIO and ALSA/JACK are whole
  implementations behind the same header, not ports of this one.
- `synthlibWindow.c` is GLFW, which does carry across.

## audio/ and plugin/ — shared code that must NOT go in src/ (2026-09-09)

Two new directories, and the reason they are not `src/` is concrete rather than
tidy: **`SynthLib/src` is a `PBXFileSystemSynchronizedRootGroup` in G2-Edit's
Xcode project and it recurses**, so anything added under it is compiled into
that *application* whether the application wants it or not. Plug-in-only code
put there would be compiled into G2-Edit and would collide with the
application's own definitions.

    audio/     device.c/.h, ring.c/.h   CoreAudio access and an SPSC ring.
                                        Not plug-in code at all — GenBridge's
                                        command line tool uses both.
    plugin/    pluginStubs.c            the SynthLib entry points an app would
                                        provide and a plug-in panel has to.

Every file here is listed BY HAND in the `do-vst3` / `do-poc` of whichever
project needs it, which is how those scripts already treat SynthLib's own
sources. GenBridge is wired up and building against all three.

### Why they moved: they had already drifted

`device.c` lived twice — GenBridge's `poc/device.c` and MidiSyncTool's
`msDevice.c` — and both files carried a comment saying they were verbatim
copies and to "fix a bug here and fix it there". It did not happen. Two fixes
went into GenBridge's copy and neither reached MidiSyncTool's:

- the **confirmed buffer-size setter** (set, then poll until the device really
  reports the new size — `noErr` means *accepted*, not *applied*), and
- `device_wait_until_idle()`.

MidiSyncTool's todo had the first as "dormant, nothing calls it", which was
false: its measurement harness calls it on every run and could therefore label
a run with a block size the device had not taken yet. Both were ported on
2026-09-09 and the two files made identical again — and then one of them
deleted. The drift was found by *diffing the files*, not by reading either one.

### What MidiSyncTool has to do, once this is pushed and its pin advanced

Its SynthLib pin is on a different commit from the other four projects, so it
needs `git submodule update --remote SynthLib` first, then:

1. Delete `src/msDevice.c`, `src/msDevice.h`, `src/msRing.c`, `src/msRing.h`
   and `vst3/msAppStubs.c`. They are now `audio/device.*`, `audio/ring.*` and
   `plugin/pluginStubs.c`, identical apart from the header comment.
2. In `tools/do-driver` and `do-vst3`, replace those paths and add
   `-I SynthLib/audio -I SynthLib/plugin`.
3. `msDevice.h`/`msRing.h` had `__MS_DEVICE_H__`-style guards and the shared
   ones use `DEVICE_H`/`RING_H`; the includes become `"device.h"` / `"ring.h"`.
   The function names (`device_*`, `ring_*`) were already identical, so no call
   site changes.

### G2-Edit, and what is actually blocking it

**Its plug-in already runs Metal** — `vst3/g2GlView.m` calls
`gfx_backend_choose(eRenderBackendMetal)` at line 260, exactly as GenBridge's
and MidiSyncTool's views do. The file name is historical. What is still on
OpenGL is the **application**, and only by default:
`RENDER_BACKEND_DEFAULT` in `renderBackendSelect.h` is `eRenderBackendOpenGL`,
G2-Edit's Renderer menu already writes a `renderBackend` pref, and
`synthlibWindow.c` applies it at start-up. So "move G2-Edit to Metal" is a
default flip plus a decision to retire the GL path — not a port. The header's
own note says OpenGL stays the default "until Metal has had real use", and that
is the thing to change when it has.

So the backend is NOT what stops G2-Edit sharing the plug-in view. Its
`g2GlView.m` is 652 lines against GenBridge's 302 because that plug-in shows
the whole editor CANVAS — keyboard, scroll, drag, cursors — where the other two
show a fixed panel. Sharing needs that scope difference resolved, not a
renderer change. Same for `g2AppStubs.c`, which is 322 lines because the G2
plug-in links the application's renderer and its undo, queue and module
database.

### The next candidates, measured 2026-09-09

    gbView.m    vs msView.m      91%   -> plugin/synthlibPanelView.m (DONE 2026-09-11)
    gbEditor.mm vs msEditor.mm   78%   -> plugin/pluginEditor.mm

Both are the same job with a small per-project interface: the view needs a
callback type and a set-values hook, the editor needs the project's parameter
mapping. Deliberately NOT done in the same pass as the move above, so that a
pass which only relocates identical files stays checkable. Do them when two
projects can be rebuilt and run together — the whole value is in the second
adopter.

## SynthEdit needs to adopt contextMenu.h/.c and the runtime theme

G2-Edit has been migrated onto SynthLib's generic mac-style nested context
menu (`contextMenu.h`/`contextMenu.c` — menu stack, hover-dwell submenu
opening, click dispatch, rendering) and onto the `tSynthLibTheme` runtime
mechanism in `utilsGraphics.h`/`.cpp` (replaces the old compile-time
`G2_EDIT` macro dependency for the handful of values — `topBarHeight`,
`orange1`, `orange2`, `greenOn`, `backgroundGrey` — that genuinely differ per
app). SynthEdit's own copy of SynthLib is still pinned to a commit from
before both of these existed, so SynthEdit hasn't picked them up yet.

Once SynthEdit's submodule pin is advanced, its app code needs the same
adaptation G2-Edit already went through:

- **types.h** — remove SynthEdit's local `tMenuItem`/`tContextMenu` (they'll
  come from `synthlibTypes.h` transitively via `geometry.h`). Note SynthEdit's
  current shape differs from the new shared one: `index`→`param`,
  `subItems`→`subMenu` (currently dead/unwired — never actually opened
  anywhere), explicit `count` field → NULL-terminated item arrays instead.
- **menus.h** — replace the local declarations with `#include "contextMenu.h"`.
- **menus.c** — delete entirely (open_context_menu/close_context_menu_if_outside/
  handle_context_menu_click/render_context_menu/the `gContextMenu` definition
  all move to SynthLib); there's no SynthEdit-specific menu content to keep
  today since no real menu tables are wired up yet.
- **mouseHandle.c/.h** — add `get_global_gui_scaled_mouse_coord(tCoord*)`
  under that exact name (SynthEdit currently has an equivalent-but-differently-
  named private helper, `window_to_logical`, that isn't itself a "get current
  mouse pos" query — wrap it: `glfwGetCursorPos` + `window_to_logical`). Drop
  the now-redundant `close_context_menu_if_outside` call in
  `handle_mouse_button` — `handle_context_menu_click` already closes on a
  total miss.
- **graphics.cpp** — call `update_context_menu_hover()` once per loop
  iteration, and make `do_graphics_loop`'s event wait poll at ~16ms
  (`glfwWaitEventsTimeout`) while `gContextMenu.active`, instead of always
  blocking on `glfwWaitEvents()` (SynthEdit currently has no "keep polling
  while X is active" branch at all — this is new, not a rename).
- **init (wherever SynthEdit sets up its window)** — call
  `configure_synthlib_theme(...)` early, built from SynthEdit's own
  `TOP_BAR_HEIGHT`/`RGB_ORANGE_1`/`RGB_ORANGE_2`/`RGB_GREEN_ON`/
  `RGB_BACKGROUND_GREY` macros (mirrors G2-Edit's `init_graphics()`).
- Confirm SynthEdit's `gReDraw` really is `_Atomic bool` (G2-Edit's is) before
  assuming it's a drop-in — `contextMenu.c`/`utilsGraphics.cpp` declare it
  `extern` under that exact name/type.

All of the above only becomes buildable once SynthEdit's `SynthLib` submodule
pin is advanced past this commit — do that first (`git submodule update
--remote SynthLib` in the SynthEdit repo, then commit the pin bump there).

## Integrity-check prefs.txt / cache.txt (idea, 2026-07-28)

Both stores are plain `key=value` text with no integrity check, so a truncated
or garbled line is silently accepted — `load_if_needed()` just skips any line
without an `=`, and a corrupted *value* is read back as if it were real.

Partly mitigated already: `save()` now writes to a `.tmp` sibling and
`rename()`s it into place (atomic within a directory), so an interrupted save
can no longer leave a half-written file. That covers the crash case, not
on-disk rot, a partial disk, or anything editing the file by hand.

Proposal: a trailing `#crc=<hex>` line over the preceding bytes, written by
`save()` and verified in `load_if_needed()`. On mismatch, treat the file as
absent rather than refusing to start — settings fall back to defaults, and the
name cache re-sweeps, which is exactly the recovery path both already have for
a first run. An absent `#crc=` line means a file written before this existed,
so accept it (same reasoning as `name_cache_is_complete()`'s absent-key
default in SynthEdit's `synthBackup.c`).

Worth doing per-store rather than only for the cache: the cache can always be
rebuilt from the synth, whereas `prefs.txt` is the one that actually hurts to
lose. Small, self-contained, and shared by all three apps.
