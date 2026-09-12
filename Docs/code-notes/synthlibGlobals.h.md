# synthlibGlobals.h notes

The longer comments from `synthlibGlobals.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `synthlib_request_quit()`

Lifecycle/window state identical across G2-Edit, EmuUtility, and SynthEdit — owned here as
accessor functions rather than raw externs, so nothing outside this file ever touches the
underlying storage directly (unlike gGlobalGuiScale/gScrollState in geometry.h, which predate
this and stayed plain externs — this is the stricter pattern going forward).

## 2. `synthlib_request_redraw()`

── Redraw ───────────────────────────────────────────────────────────────────
Sets the redraw flag and wakes a possibly-blocked glfwWaitEvents()/glfwWaitEventsTimeout() —
every SynthLib popup/panel mechanism (contextMenu.c, menuBar.c, alertDialog.cpp,
bankBrowser.cpp, fileBrowser.cpp) calls this, and so does every app, in place of what used to
be a raw `gReDraw = true;`.
