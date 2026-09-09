# SynthLib TODO

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

    gbView.m    vs msView.m      91%   -> plugin/pluginView.m
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
