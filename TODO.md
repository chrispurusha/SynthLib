# SynthLib TODO

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
