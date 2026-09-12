# fileBrowser.h notes

The longer comments from `fileBrowser.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tFileBrowserMode`

In-window, cross-platform replacement for native NSOpenPanel/NSSavePanel-style dialogs — a
real directory browser (not just a typed-path field), drawn with the same GLFW/OpenGL
primitives as everything else, so it behaves identically on macOS/Windows/Linux. Directory
listing is backed by std::filesystem internally (fileBrowser.cpp); this header stays a plain C
API like every other SynthLib component.

Interaction model, deliberately simpler than a native panel to avoid needing double-click
timing or drag-selection: clicking a folder row always navigates into it; clicking a file row
selects it (Open mode) or copies its name into the filename field (Save mode); a single
Confirm button commits — in Choose Folder mode it always targets the currently-open directory,
not a clicked row, matching how NSOpenPanel's own folder mode works.

The embedding app must call synthlib_host_init() (synthlibHost.h) once at startup, same as
contextMenu.c/menuBar.c require, and must, once per frame, call render_file_browser(); route
mouse-down clicks through handle_file_browser_click() ahead
of other click handling (it returns true if the click landed inside the browser, whether or
not it hit something specific); route key/char events through handle_file_browser_key()/
handle_file_browser_char(); and route scroll-wheel deltas through handle_file_browser_scroll().
All four are safe to call unconditionally — they no-op when file_browser_active() is false.

The directory listing's scrollbar thumb (utilsGraphics.h's list_scrollbar_* family) is
draggable: route every mouse-move through handle_file_browser_mouse_move() while the mouse
button is held (same pattern as bankBrowser.h's handle_bank_browser_mouse_move()) — it no-ops
and returns false unless a drag is actually in progress.

## 2. `set_file_browser_start_directory()`

fileBrowser.c has no cross-launch persistence of its own (SynthLib doesn't know how a given
app prefers to store preferences — NSUserDefaults, a config file, ...). Instead: the embedding
app calls set_file_browser_start_directory() once at startup with whatever it last saved, to
seed where the browser opens; and registers a callback via
set_file_browser_directory_changed_callback(), invoked with the new directory every time a
browse session completes successfully, so the app can persist it for next launch.
