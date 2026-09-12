# synthlibHost.h notes

The longer comments from `synthlibHost.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `void()`

Injection point for the one thing every SynthLib popup/panel mechanism (contextMenu.c,
menuBar.c, alertDialog.cpp, bankBrowser.cpp, fileBrowser.cpp) still needs from the embedding
app that SynthLib itself can't provide: the current mouse position, in the app's own
logical/scaled coordinate space (depends on the app's own window and GLFW cursor query).
Previously each of those 5 files also declared its own
`extern "C" _Atomic bool gReDraw;` for requesting a redraw — gReDraw itself now lives in
SynthLib (synthlibGlobals.h's synthlib_request_redraw()), so that half of this mechanism was
retired; only the mouse-coord half remains app-specific.

## 2. `bool()`

True while the host has the pointer CAPTURED for a drag - hidden, and reporting a relative-delta
accumulator rather than a real on-screen point. During such a drag the reported coordinate drifts
wherever the accumulated deltas take it, so ANY hover highlight computed from it lights the wrong
thing: drag a dial far enough and the menu bar lights up under a cursor that is not there.
Optional - a host that never hides the pointer leaves it NULL and nothing changes.
