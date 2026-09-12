# pluginStubs.c notes

The longer comments from `pluginStubs.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

The handful of functions SynthLib calls that an APPLICATION would provide and a plug-in has to.

SHARED, because two projects had this file and it was the same file - GenBridge's gbAppStubs.c and
MidiSyncTool's msAppStubs.c differed by their first comment line and nothing else. G2-Edit's
plug-in has its own four-hundred-line version and cannot use this one yet: it links that
application's renderer, which reaches for undo, a message queue and a module database. If that
plug-in is ever narrowed to the panel alone, this is what it would narrow to.

NOT IN SynthLib/src, for the reason in audio/device.c: that folder is synchronized into G2-Edit's
application target, and these definitions would collide with the application's own.

synthlibGlobals.c is NOT linked, for the same reason G2-Edit does not link it: its
synthlib_request_redraw() calls glfwPostEmptyEvent(), which would drag GLFW into a plug-in that
deliberately has none. The two functions actually reached from the renderer are provided here.

## 2. `synthlib_request_redraw()`

NOT a stub - the same job, done differently. An application posts an empty event to wake a
blocked GLFW loop; here the panel repaints on a timer regardless, so a request needs no action.
It is defined rather than omitted because the renderer calls it from several places, and a
missing symbol at link time is a poorer answer than a deliberate no-op.
