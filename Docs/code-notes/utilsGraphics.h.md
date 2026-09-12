# utilsGraphics.h notes

The longer comments from `utilsGraphics.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tSynthLibTheme`

The handful of visual values that genuinely differ between apps (colours,
top bar height) and that this file's own drawing code needs — everything
else app-specific stays a compile-time macro in each app's own defs.h,
resolved normally since those files define G2_EDIT (or don't) for
themselves. This is how utilsGraphics.cpp itself gets told which app it's
drawing for, without including that app's defs.h (see configure_synthlib_theme()).

## 2. in `set_rgba_colour()`

tTextureFilter is the backend contract's, because a filter is a property of how the graphics API
samples a texture — but it appears in this file's public texture calls, so the contract comes
with it. Including it here does NOT let a caller reach the gfx_* functions in any meaningful
sense: naming one still means linking the backend, which only SynthLib does.

## 3. `render_backend_init()`

── Render backend seam ──────────────────────────────────────────────────────

The eight calls below are what the APPLICATIONS use to drive the renderer. They are
no longer where a port happens: since 2026-08-28 the graphics API itself lives behind
renderBackend.h's nine gfx_* calls, implemented once per platform in a renderBackend*.c
that renderBackendSelect.h chooses. READ THAT HEADER BEFORE WRITING A BACKEND — it is
the contract, and it says what deliberately is NOT part of it.

What these eight are, then, is the portable half: each applies whatever batching rule
its operation needs and hands the rest to a gfx_* call. utilsGraphics.c has named no
graphics API since the split, which the compiler enforces by there being no such
declaration in scope.

Four of them had three or four separate copies before the seam existed: the GLFW window
layer, the scale/resize path, each app's frame loop, and the plug-in's own NSOpenGLView
(vst3/g2GlDraw.c), which shares these renderers but has no GLFW underneath it.
render_backend_flush() arrived with geometry batching, and the three texture calls with
EmuUtility's LCD, which until then created and uploaded its texture with raw GL in its
own emuGraphics.c — the last thing outside this file that named the API.

## 4. `render_backend_init()`

Session-wide drawing state. Call once, after a context is current and before
anything is drawn. BLENDING IS ON FOR THE WHOLE SESSION and no drawing code turns
it off again: an opaque draw (alpha == 1.0) resolves to the source colour whether
blending is enabled or not, so the invariant costs opaque drawing nothing and
spares every translucent caller an enable/disable pair of its own. render_text()
used to end by DISABLING blend, which silently revoked the session-wide enable
after the first string was drawn and is why the two translucent callers each
carried their own pair.

## 5. `render_backend_flush()`

Submits everything the drawing primitives have queued since the last submission.

The primitives no longer issue a draw call each: they append triangles to one vertex
array, which goes out in a single call. That array must be submitted before the frame
is presented, so CALL THIS IMMEDIATELY BEFORE THE BUFFER SWAP in every frame loop —
the three applications' render_frame() and the plug-in's g2_gl_draw_frame(). Miss it
and the frame shows only what a mid-frame flush happened to force out; on a frame
whose last drawing was a plain filled rectangle, that is nothing at all.

Everything else that needs it calls it already, and all inside utilsGraphics.c: the
module-pane scissor, the projection change, the clear, the frame read-back behind
SCREENSHOT, and any glyph-atlas texture the batch might still be referencing. The
batch is also submitted whenever the texture changes, so an untextured run and a run
of text are separate calls; consecutive draws sharing a texture and a clip are one.

Draw ORDER is never reordered. A 2D UI is painted back to front, so the batch is a
pure concatenation of what was already going to be drawn, flushed at every point that
would change how subsequent vertices rasterize.

## 6. `render_present()`

ENDS THE FRAME: submits the batch, then puts it on screen. This is the last call in every
application's render_frame(), and it replaced a render_backend_flush() followed by
glfwSwapBuffers() — which was the last GLFW call left in any frame loop, and could not survive
Metal, where the window is created with GLFW_CLIENT_API set to GLFW_NO_API and there is no
context to swap.

Not used by the VST3 plug-in, which is drawn into a view its host presents.

## 7. `render_backend_texture_create()`

── Textures ────────────────────────────────────────────────────────────────

A texture is an OPAQUE HANDLE, not a graphics-API object. Under OpenGL it happens to
be the GLuint; under Metal it will be an index into a table the backend keeps, since
an id<MTLTexture> does not fit in an integer. Callers see no difference — which is
the point, because the two things that hold one (this file's glyph atlases and
EmuUtility's gLcdTexture) are already plain integers and do not change when the
backend does. 0 is "no texture" and is never a valid handle.

Every texture is RGBA8, nearest-filtered and clamped to edge. There is no parameter
for any of that because both callers blit one texel per pixel: filtering could only
blur a sample that already lands dead centre on its texel, and a UV never leaves
[0,1] so the wrap mode is unobservable. If a caller ever genuinely needs filtering,
add it then, with the case in front of you.

## 8. `render_backend_texture_create()`

Allocates width*height RGBA8 texels. `rgba` fills them, or NULL leaves them
undefined for a later _update(). `filter` is eTextureNearest for anything blitted one texel per
pixel — which is nearly everything — and eTextureLinear only where a texture is deliberately
sampled at another scale, currently just the supersampled small-text atlas. Returns 0 on
failure.

## 9. `contrasting_text_colour()`

Perceptual (Rec. 601) luminance of bg, thresholded at 0.5 — the usual
black/white crossover point for this formula. Lets callers put a label on
a caller-supplied background colour (module/category colours, which range
from near-black to near-white) without needing to know in advance whether
black or white text will read against it.

## 10. `MAX_MODULE_PANES`

── Module panes ────────────────────────────────────────────────────────────────────────────────

The module canvas is drawn as one or more PANES stacked vertically down the window. Today there
is exactly one, occupying the whole canvas band, so this is behaviourally identical to the single
canvas that came before it — the structure exists so the Patch Window Split Bar can show the
Voice Area and the FX Area at once (see G2-Edit's todo.md) without every drawing call having to
learn which half it is drawing into.

A pane owns its SCROLL POSITION and its slice of the canvas band. It does NOT own the zoom:
gZoomFactor stays global, so both panes always draw at the same scale. That is deliberate —
matched scale is what makes two areas comparable side by side, and it keeps scale() a plain
global multiply for the many callers that have no idea panes exist. Promoting zoom into the pane
later is a contained change if it turns out to be wanted.

The rendering transform reads the CURRENT pane, in the same "mode rather than argument" style
that set_param_render_area() already uses for the parameter renderers: draw one pane's worth of
modules, switch, draw the next. Rendering is sequential, so a mode is sufficient and avoids
threading a pane argument through every render call in the app.

## 11. `module_pane_clip_begin()`

Clips drawing to the current pane's rectangle. REQUIRED around a pane's render pass once there
is more than one pane, and it is what makes panes actually independent: a module taller than its
pane, or scrolled so it straddles the divider, would otherwise keep drawing straight into its
neighbour — nothing else in this library clips. It also hides the connectors that
render_modules() deliberately still draws for culled modules (so cables keep their endpoints),
which used to be safely off-screen and, with panes, land in the other half of the window.

## 12. `draw_button_split()`

A button whose face is painted in two colours, split across the middle — for a control carrying
two independent states that one fill colour cannot express (G2-Edit's variation buttons: green
for "selected", orange for "linked", and both at once when it is both). Identical to draw_button()
in every other respect, and draw_button() is itself this with one colour passed twice. Pass the
same colour for both to get a plain button.

## 13. `draw_panel_chrome()`

Shared chrome for every modal panel and popup: the bordered box, its darker title bar, and a
close button in the TOP LEFT corner - macOS's corner for it, and where these all used to get it
wrong by putting a "Close" text button top right instead.

draw_panel_chrome() returns the title bar rect (some panels use it as a drag handle) and indents
the title past the close button. panel_close_button_rect() is the geometry on its own, for
hit-testing outside the render pass; draw_panel_close_button() draws it and returns the same rect.

## 14. `list_scrollbar_thumb_rect()`

Shared vertical scrollbar for list-style popups (bankBrowser.cpp, fileBrowser.cpp, and similar) —
listRect is the list's own content box; totalRows/visibleRows/scrollOffset are the same terms
each caller already tracks for scroll-wheel handling (scrollOffset is the index of the first
visible row, a double so it can be compared/clamped against fractional drag positions).

Drag state lives here as file-static, not per-caller — safe because bankBrowser/fileBrowser
are mutually exclusive modals, so only one list scrollbar can ever be mid-drag at a time.
Usage: list_scrollbar_mouse_down() on mouse-down (returns true if the thumb was hit, and starts
the drag); while list_scrollbar_dragging() is true, feed every mouse-move to
list_scrollbar_mouse_drag() and store its returned (already-clamped) scrollOffset back; call
list_scrollbar_mouse_up() on mouse-up regardless.
