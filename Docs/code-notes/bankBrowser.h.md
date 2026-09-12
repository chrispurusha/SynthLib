# bankBrowser.h notes

The longer comments from `bankBrowser.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tBankBrowserItem`

In-window, cross-platform replacement for the Cocoa NSAlert+NSTableView bank/location picker
this panel replaced — a scrollable named list with a Bank/Loc, Category, A-Z sort switcher,
drawn with the same GLFW/OpenGL primitives as the rest of the app. Every call site rebuilds and
passes its own item list, so the picker has no notion
of where the data came from — a device-cached name-table sweep (G2-Edit), a live device query,
or anything else a future caller (Z1-Edit, EmuUtility) might dynamically populate the list from.

The embedding app must call synthlib_host_init() (synthlibHost.h) once at startup, same as
contextMenu.c/fileBrowser.c require, and must, once per frame, call render_bank_browser(); route
mouse-down through handle_bank_browser_mouse_down() (press-state only, no action); route
mouse-up through handle_bank_browser_click() ahead of other click handling (returns true if the
click landed inside the browser); route key events through handle_bank_browser_key(); and route
scroll-wheel deltas through handle_bank_browser_scroll(). All four are safe to call
unconditionally — they no-op when bank_browser_active() is false. It must also call
update_bank_browser_hover() once per frame — same contract as contextMenu.h's
update_context_menu_hover() — since the app only repaints on a requested redraw, and this is the
thing that requests one when the mouse moves onto a new row.

The list's scrollbar thumb (utilsGraphics.h's list_scrollbar_* family) is draggable: route every
mouse-move through handle_bank_browser_mouse_move() while the mouse button is held (same pattern
as G2-Edit's other drag state, e.g. gScrollState.yBarDragging in mouseHandle.c) — it no-ops and
returns false unless a drag is actually in progress, so it's safe to call unconditionally
alongside the app's other drag checks.

## 2. `tBankBrowserItem`

One row's worth of data. name is copied internally before open_bank_browser() returns, so the
caller's array/strings only need to survive the call itself. category indexes into the
categoryNames array passed to open_bank_browser() — pass 0xFF (or any value >= categoryNameCount)
for "no category", which sorts into an "Unknown" bucket under Category mode.

## 3. `bank_browser_set_priority_categories()`

Category sort mode lists its groups alphabetically. Any category named here is floated to the TOP
of that list instead, in the order given, ahead of the alphabetical run — for the categories a
device treats as the user's own, which are worth reaching first however they happen to be spelt
(G2-Edit's "User 1"/"User 2" land under U, at the very bottom, purely by accident of the alphabet).

Named, not indexed, because the grouping itself is keyed on the category NAME: sharing that key
means a pinned group can never sort apart from the header it is drawn under. Names must match the
categoryNames passed to open_bank_browser() exactly; any that match nothing are simply inert.

Persists until changed and applies to every browser opened afterwards (and re-groups one already
on screen), so an app with a single category vocabulary sets it once at startup. Pass count 0 to
clear it and go back to plain alphabetical.
