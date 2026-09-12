# renderBackendMetal.m notes

The longer comments from `renderBackendMetal.m`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

── The Metal backend ───────────────────────────────────────────────────────────────────────────

The same nine gfx_* calls renderBackendGL.c implements, on Metal. Nothing above this file knows
which one it is talking to.

IT RENDERS OFFSCREEN, and that is deliberate rather than a stepping stone left half-built. The
frame is drawn into a texture this file owns; a window layer will later blit that texture to a
CAMetalLayer drawable and present it. Two reasons it is built this way round:
```
  - mtl_read_pixels_rgb() — the backdoor SCREENSHOT, which is how every rendering change in this
    project gets proved — cannot read a drawable back reliably once it has been presented. An
    offscreen target makes the read trivial and keeps the verification method intact.
  - it means the DRAWING can be proved correct against the OpenGL backend, on the same machine,
    with the same patch, before any windowing code exists to argue with.
```
So a Metal build today renders correct frames and PRESENTS NOTHING: the window stays blank while
SCREENSHOT returns exactly what the GL build returns. That is the intended intermediate state.

THE THREE COORDINATE FACTS, which is where this normally goes wrong:
```
  - Metal's clip space has y going UP, tVertex has y going DOWN. The vertex shader flips it —
    the job glOrtho(0, w, h, 0, ...) was doing in the GL backend.
  - Metal's SCISSOR and viewport origin is the TOP LEFT, which is already the tVertex origin, so
    mtl_scissor() needs no flip here at all. The GL backend needs one. Getting both right at the
    same time is the trap: they are opposite, in the same file position, in the two backends.
  - a scissor rect that leaves the render target is a Metal VALIDATION FAILURE, where GL quietly
    clamped it. So it is clamped here, explicitly.
```

## 2. `kShaderSource`

── Shaders ─────────────────────────────────────────────────────────────────

Compiled at RUNTIME from this string rather than built into a .metallib. A .metallib would mean
a build step in do-vst3 (which is a shell script, not an Xcode target) and a bundle-resource
lookup that differs between the application and a plug-in a host may have sandboxed. A string
costs a few tens of milliseconds once, at start-up, and behaves identically in both.

The fragment shader is GL_MODULATE: vertex colour times texture sample. Untextured geometry is
drawn with a 1x1 opaque white texture bound, so one pipeline state covers both cases and there
is no branch — white times anything is anything.

## 3. file scope

── Per GPU, shared by every window ─────────────────────────────────────────

These are properties of the Metal device, not of any surface, so several windows share one set.
The texture table is deliberately among them: the glyph atlas is the largest thing in it and
there is no sense in every open editor building its own.
THIS FILE ASSUMES ARC, and without it every frame leaks. -newBufferWithBytes: in mtl_submit()
returns an object the caller owns, and nothing here releases it by hand - ARC does, at the end of
the function. G2-Edit's do-plugin compiled this file with its plain C flags, which have no
-fobjc-arc, so G2 Alike kept every vertex buffer it ever drew with: its editor grew from 1.7 GB to
12.7 GB in thirty seconds of pointer movement, and a frame went from 4 ms to 44 ms because creating
a Metal buffer slows as the pile of live ones grows. The applications build it in Xcode with ARC,
and GenBridge's and MidiSyncTool's scripts list it with their Objective-C sources, so none of them
showed it. A build without ARC is refused rather than trusted.

## 4. `MAX_METAL_WINDOWS`

── Per window ──────────────────────────────────────────────────────────────

EVERY ONE OF THESE USED TO BE A GLOBAL, which quietly limited the process to a single window.
That was true of an application and false of a plug-in: a host may open two editors, and the
second attach simply overwrote the first's layer, leaving the first drawing nowhere. It did not
crash, which made it harder to notice, not easier.

The surface, its render targets, the in-flight command buffer and the scissor all belong to one
window. The body of this file is unchanged: the names below are macros onto whichever context is
current, so the several hundred lines of drawing code neither know nor care that there is more
than one.

## 5. in `metal_apply_scissor()`

NO Y FLIP: Metal's scissor origin is the top left, which is already the caller's origin, and
the rectangle arrives in whole pixels. So this is the caller's rectangle, clamped and nothing
else — where the GL backend has to flip it.

CLAMPED, and that part is not optional: a scissor rect that leaves the render target is a
VALIDATION FAILURE in Metal, where GL clamps it silently.

## 6. in `metal_begin_pass()`

STORE AND RESOLVE ON EVERY PASS, not just the last one. A frame here is not one pass: the
batch is submitted, and the pass ended, at every scissor change and every texture change, so
there is no way to know which pass is final. Storing the samples keeps loadAction Load
working for the next pass, and resolving each time keeps gTarget correct whenever a
read-back or a present happens to come next. The cost is bandwidth on a UI that redraws only
when something changes.

## 7. in `mtl_set_surface()`

NO IMPLICIT ANIMATION ON LAYER GEOMETRY. frame and drawableSize are animatable CALayer
properties, so a bare assignment enrols them in the default 0.25s action - and during a
live window resize that is a new animation every frame, each one starting from where the
last had got to. The window edge moves at once and the contents crawl after it, arriving
only once the drag stops, which is exactly what it looked like: "the components only seem
to be resized after the window resize completes".

A layer inside a plug-in is a SUBLAYER of the host view's layer - see mtl_attach_window -
so nothing lays it out for us and there is no free ride from the view's own resizing.

## 8. in `mtl_set_surface()`

THE LAYER'S SIZE IS DERIVED FROM THE DRAWABLE, not from the view, and that is the whole
of a bug worth remembering. A host asked for a 1367x768 editor and the view ended up
768.9 POINTS tall; convertRectToBacking then truncates 1537.8 pixels to 1537, which is
what gets rendered. Leave the layer at the view's 768.9pt and Core Animation scales a
1537-pixel frame into a 1537.8-pixel box — a 0.9995 resample that never looks broken,
just faintly soft, and which showed up as 6.5% of pixels differing from the OpenGL build
with the text quietly blurred.

Sizing the layer at drawable/contentsScale makes the mapping exactly 1:1 again. It can
leave the layer a fraction of a point short of the view, which is invisible; a resampled
frame is not.

## 9. in `mtl_clear()`

A clear starts a frame, so the PREVIOUS frame is finished and can go to the GPU. Committing
here rather than waiting for a read-back is what keeps one command buffer per frame: without
it every frame since the last SCREENSHOT accumulates into one buffer, holding on to a vertex
buffer per submission for as long as the app runs between captures.

Committed, not waited on. Only the read-back needs the GPU to have finished.

## 10. in `mtl_submit()`

SMALL SUBMISSIONS GO IN THE COMMAND BUFFER ITSELF. A fresh MTLBuffer per submission was the
plan here "until a profile says otherwise"; G2 Alike submits ~470 times a frame, averaging ~50
vertices, which is an allocation and a free per draw call for data a few hundred bytes long.
setVertexBytes: copies up to 4 KB straight into the command buffer and allocates nothing -
Apple's own advice for data that small - and only the occasional large batch still gets a
buffer of its own. MEASURED in tools/vst3host: idle CPU of the continuously repainting panels
fell from 3.5% to 2.9% (GenBridge) and 4.8% to 3.6% (MidiSyncTool). It did NOT change their
memory: the ~400 MB of GPU memory a plug-in editor shows there is not this file's (see todo).

## 11. in `mtl_read_pixels_rgb()`

TWO CONVERSIONS, and both are the contract's doing rather than Metal's. renderBackend.h
promises tightly-packed RGB triples with the BOTTOM row first, because that is what
glReadPixels gave and what the backdoor's PNG writer expects — so the rows are walked in
reverse. And the target is BGRA, so the two ends of each pixel swap.

## 12. in `mtl_attach_window()`

A MATCH ON THE POINTER IS NOT PROOF IT IS THE SAME WINDOW. The slot is keyed on the raw
address, and an address is reused: a host that closes an editor and opens another gets a
fresh NSView from the allocator at the same place often enough to rely on it. If the
caller failed to detach, this lookup then hands the new view the DEAD one's layer, and the
first present crashes in -nextDrawable on a layer that is in no live layer tree.

So check the layer is still where this slot put it. For a view the layer is a sublayer of
the view's own; for a window it IS the content view's layer. Either way a stale slot fails
the test and is rebuilt from scratch rather than trusted.

## 13. in `mtl_attach_window()`

NO SLOT MEANS DRAW NOWHERE, NOT DRAW INTO SOMEONE ELSE'S WINDOW. Returning with gW left
pointing at whichever window was last current means this view's frames are presented to
that one's layer. Slot 0 is the empty sentinel and every guard in this file already
handles it, so parking there degrades to drawing nothing.

## 14. in `mtl_attach_window()`

EITHER AN NSWindow OR AN NSView, and it has to be both because the two callers differ. The
application hands over the NSWindow it got from glfwGetCocoaWindow() — GLFW made that window
with GLFW_CLIENT_API = GLFW_NO_API, so it has no context and no drawable of its own, which is
the whole point. The VST3 plug-in hands over its OWN view, because in a plug-in the window
belongs to the host and is emphatically not ours to put a layer on.

## 15. in `mtl_attach_window()`

OPAQUE, and this is not a hint — it is a correctness fix. Blending writes the alpha channel
as well as the colour, so wherever a glyph's antialiased edge is drawn with alpha below one
the framebuffer's own alpha ends up below one too. Core Animation then composites the frame
over whatever is behind the layer, which for a plug-in view is the host's dark background,
and every piece of text acquires a dark fringe. Saying the layer is opaque tells Core
Animation to ignore the alpha channel it has been handed, which is what OpenGL's drawable
did implicitly all along.

## 16. in `mtl_attach_window()`

NO COLOUR MATCHING. A CAMetalLayer is colour-managed by default: Core Animation treats the
pixels as being in some source space and converts them for the display. The OpenGL path is
not — an NSOpenGLView's values go to the screen as written — so the two disagreed on solid
colours by a few counts per channel, RGB_GREEN_ON arriving as (75,179,87) where OpenGL wrote
(77,178,77). Not visible, but it is a real difference in what the user sees and it would
have made every future screen-level comparison useless.

A nil colorspace means "these pixels are already in the display's space, pass them through",
which is exactly what OpenGL was doing implicitly.

## 17. in `mtl_attach_window()`

VSYNC, STATED RATHER THAN ASSUMED. displaySyncEnabled defaults to YES, so presentation is
already paced to the display's refresh — but the OpenGL path asks for the same thing out
loud (glfwSwapInterval(1) in synthlibWindow.c, and the swap interval on the plug-in's
context in g2GlView.m), and an invariant that is explicit in one backend and inherited in
the other is one nobody can answer a question about. Now both say it.

## 18. in `mtl_attach_window()`

PRESENTING INSIDE THE TRANSACTION, for a view whose drawing AppKit drives.

presentDrawable: hands the frame over asynchronously, OUTSIDE the Core Animation transaction
AppKit is in the middle of. When we own the frame loop that is right and cheapest: nothing
else is laying the window out around us. But the plug-in draws from -drawRect:, inside a
view hierarchy the HOST is laying out, and an asynchronous present means our pixels and
AppKit's idea of the layout land at different moments — which is seen as tearing or a jitter
at the edges, most obviously while something is being dragged or resized.

The documented answer is to present as part of the transaction instead: commit, wait until
the work is SCHEDULED (not completed — that would stall a frame), then present the drawable
by hand. It costs a synchronisation point per frame, which is why it is not done for the
application.

## 19. in `mtl_attach_window()`

HOSTING OR SUBLAYER, AND THE CHOICE IS NOT COSMETIC — it decides whether the view is ever
asked to draw at all.

A LAYER-HOSTING view (assign .layer, then set wantsLayer) tells AppKit the layer's contents
ARE the view's contents, so AppKit stops calling -drawRect: entirely. That is right for the
application, where the render loop drives every frame and nothing waits to be asked.

It is WRONG for the plug-in, whose every redraw is a -setNeedsDisplay: that AppKit turns into
a -drawRect:. Made layer-hosting, the plug-in editor drew nothing at all: a blank window, no
error, no warning, because the mechanism that would have drawn it had been switched off by
the very call meant to enable it. So a view is given the layer as a SUBLAYER and stays
layer-BACKED, which keeps -drawRect: coming.

## 20. in `mtl_attach_window()`

Points to pixels. Without this the drawable is sized in points and every frame is presented
at half resolution on a Retina display. A plug-in view may not be in a window yet when the
host attaches it, so fall back to the main screen rather than to 1.0 — being wrong by a
factor of two is far more visible than being wrong about which display.

## 21. in `mtl_present()`

A BLIT, not a second render pass. The frame already exists in gTarget — rendering it again
into the drawable would mean a full-screen quad, another pipeline and a sampler, to copy
pixels that are already correct. The offscreen target is what makes mtl_read_pixels_rgb()
work, so it earns its keep twice.
