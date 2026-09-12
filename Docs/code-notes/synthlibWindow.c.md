# synthlibWindow.c notes

The longer comments from `synthlibWindow.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

THE ONE PLACE A BACKEND CHANGES WHAT THE WINDOW IS, and it is a window concern rather than a
drawing one. OpenGL wants GLFW to create a context alongside the window and make it current.
Metal wants GLFW to create NO context at all — GLFW_NO_API, the same hint a Vulkan application
uses — and then takes the NSWindow and puts a CAMetalLayer on it.

The tests below are RUNTIME, not #ifs, because both backends are in the binary and the choice
comes from a saved setting. This function being the difference between them is also why the
choice must be made before the window is created and cannot change while running.

## 2. `SYNTHLIB_WINDOW_MIN_DIVISOR`

The window minimum, as a divisor of the design size. 640x360 for a 2560x1440 target, and still
exactly the locked 16:9. The old TARGET/8 allowed a 320pt window, which on a 1x display is a 320px
framebuffer — gGlobalGuiScale 0.25, putting body text at ~3px and the small labels at ~2px,
unreadable however well they are rendered. At 640pt the 1x case bottoms out at ~6px, which is not.

## 3. `gCallbacks`

Which optional callbacks were actually registered, so the close handler can unregister exactly
those and nothing else. Kept rather than re-derived because "unregister everything" would call
glfwSet*Callback on a window during teardown for events the app never asked about — harmless
today, but the reason EmuUtility's hand-written copy of this had already gone stale was that the
register and unregister lists were two separate hand-maintained things. Here they are one.

## 4. `content_scale_callback()`

Fires when the window moves to a display with a different HiDPI scale (e.g. dragging from a Retina
built-in display to a non-Retina external one, or vice versa) — see synthlibScale.h's own comment
for the bug this fixes (gContentScale used to be hardcoded 2.0f, so anything deriving a screen
position from gGlobalGuiScale landed mispositioned wherever the real scale was not 2.0).

## 5. `gHandlers`

── The normalised shims ─────────────────────────────────────────────────────

The boilerplate that used to be repeated around every event in every app, once. See
tSynthLibInputHandlers in the header for what this deliberately does NOT take over.

## 6. in `synthlib_window_create()`

GLFW_SCALE_FRAMEBUFFER, not GLFW_COCOA_RETINA_FRAMEBUFFER. The old name is a LEGACY ALIAS,
not a deprecated behaviour: glfwWindowHint() falls both through to the same
_glfw.hints.window.scaleFramebuffer (glfw/src/window.c), so this is a rename and nothing more.
The new name is the honest one — the hint stopped being macOS-specific in GLFW 3.4, and a
Windows or Linux build wants it too, which is the point at which the Cocoa name would have
started to mislead. Needs GLFW >= 3.4; the bundled copy is 3.5.1.
