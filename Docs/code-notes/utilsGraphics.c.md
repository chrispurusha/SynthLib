# utilsGraphics.c notes

The longer comments from `utilsGraphics.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

NO GRAPHICS HEADER, and that is the point of the split (2026-08-28). Everything this file used
to do with OpenGL directly now goes through renderBackend.h's nine gfx_* calls, which exactly
one renderBackend*.c implements. FreeType stays: rasterizing a glyph produces a buffer in RAM
and is not a graphics-API operation.

The compiler enforces it. A drawing primitive here cannot reach for a GL call, because there is
no declaration for one — which is the same guarantee moduleGraphics.c and paramOverlay.c got
when the seam was first drawn, now extended to the file that drew it.

## 2. `GLYPH_ATLAS_PADDING`

Glyph atlases.

Text is rendered from a glyph bitmap rasterized at the integer pixel height it is actually
drawn at, and blitted 1:1 to integer framebuffer pixels. Anything else resamples the glyph:
a fixed rasterization size has to be scaled to fit, and that final bilinear resample at an
arbitrary sub-pixel phase smears away exactly the stem alignment FreeType's hinter just
produced. On a 2x display there are enough pixels to hide it; at 1x, where the UI can scale
text down to ~6px, it turns small text to mush.

So atlases are keyed by integer drawn height and cached (in practice a running app needs only
two or three: G2-Edit at a 700px window on a 1x display draws text at just 6.6px and 4.6px).
The height is the drawn height rather than anything derived from gGlobalGuiScale, because
module text is additionally scaled by gZoomFactor and only the drawn height sees both.

Layout is deliberately NOT driven by these atlases. get_text_width() and everything built on
it stay on one canonical set of metrics (gCanonInfo, taken once at a reference size), so text
widths remain continuous and proportional and boxes fit exactly as before. Only the glyph
images and their final pixel positions come from the sized atlas.

## 3. `GLYPH_SUPERSAMPLE_BELOW_PX`

SMALL TEXT IS RASTERIZED BIG AND SCALED DOWN. Below this drawn height the 1:1 blit costs more
than it buys: the atlas em is an INTEGER, and the glyph bitmaps that come out of it are integers
too, so at an em of 6 one pixel of rounding is 17% of the text's height. Measured on a 1280x720
display, the same button's text filled 0.385 of its box where the Retina build gives 0.526 — the
14% CT could see. Rasterizing at twice the size and drawing at the true fractional height puts
the quantisation back under a few percent, at the cost of softer stems, which is the better
trade this small. Above the threshold nothing changes and the crisp 1:1 path is untouched.

THE TRIGGER IS THE DRAWN HEIGHT, NOT THE DISPLAY. "Retina or not" is a proxy and a leaky one:
gGlobalGuiScale runs continuously with the window size (0.69, 0.88 and 2.37 were all measured on
one machine in one afternoon), so a zoomed-out canvas on a Retina display draws 6px text too and
wants exactly the same treatment.

## 4. in `module_area_for_pane()`

Sliced by fraction rather than by pixels so a window resize redistributes the split
proportionally instead of stranding one pane at a fixed height. With pane 0 at {0.0, 1.0}
this is arithmetically the whole band, i.e. exactly the rectangle this function used to
return before panes existed.

## 5. `gBatchVerts`

── Geometry batching ────────────────────────────────────────────────────────

Every primitive below used to be a glBegin/glVertex/glEnd burst — immediate mode, one GL call
per vertex. Nothing after OpenGL has an immediate mode: Metal, D3D and Vulkan all want a buffer
of vertices and one submission. So the primitives no longer talk to GL at all; they append
triangles here, and the batch is submitted as a single array.

WHAT THIS IS NOT: a reordering. A 2D UI is painted back to front and every overlap depends on
draw order, so the batch is flushed the moment anything that would change how subsequent
vertices rasterize changes — the bound texture, the scissor rect, a clear, a read-back, or the
end of the frame. Consecutive draws that share all of those merge; nothing else does. That keeps
the pixels bit-identical while still collapsing the common runs (a module face's rectangles, a
string's glyphs) into one call.

The four topologies the old code used (GL_QUADS, GL_POLYGON, GL_TRIANGLE_FAN, GL_TRIANGLE_STRIP)
all become plain triangle lists, which is the one topology every backend agrees on. Winding is
not normalised because nothing in any of the three apps enables face culling.

## 6. `render_backend_flush()`

The batch's public face, and the whole of its dealing with the backend: one array of finished
triangles and the texture they sample. WHICH triangles ended up in it — the merging rules, the
sticky texture, the forced submissions — is decided above and is identical on every platform,
which is why none of it is in renderBackend.h.

## 7. `batch_untextured()`

UNTEXTURED geometry has to say so, and this is the one thing about the batch that is not
obvious: the texture is STICKY. In immediate mode every draw stated its own texturing —
internal_render_text() and internal_render_texture() each ended by unbinding and disabling
GL_TEXTURE_2D, so a rectangle drawn afterwards was untextured because the last textured draw
had cleaned up after itself. Nothing cleans up here: the batch carries whatever texture was
last selected until something selects another. Without this call every shape drawn after the
first string samples texel (0,0) of the glyph atlas — which is empty, so alpha 0, so the
entire UI except its text renders INVISIBLE. Called from the two places that append plain
triangles, which is every primitive that is not text or render_texture().

## 8. in `module_pane_clip_begin()`

ROUNDED HERE, ONCE, so that every backend clips the same whole pixels. Truncating both
EDGES — rather than the origin and the size separately — makes the rectangle cover exactly
the pixels whose top-left corner lies inside it, which is the obvious reading of a
fractional rect and, unlike leaving it to each backend, one they cannot disagree about.
They did disagree: it was the single defect the Metal port turned up, two rows out of 1704
at the pane boundary. See gfx_scissor() in renderBackend.h.

## 9. in `module_pane_clip_begin()`

The CLICK regions get the same clip as the pixels, set here so the two cannot drift apart. A
module scrolled past the bottom of its pane is not drawn there — and must not be clickable
there either, which it was: in the split view a Voice Area module scrolled under the FX pane
could still be selected through it. See set_click_region_clip().

## 10. `rectangle_visible_in_module_area()`

Returns true if any part of `rectangle` (moduleArea-local coordinates, i.e. the same
space passed to render_rectangle(moduleArea, ...) / render_module()'s own moduleRectangle)
would land within the currently visible, scrolled/zoomed module canvas. Callers can use
this to skip rendering work for things that are entirely off-screen — it applies the exact
same scale/scroll transform real rendering does, so it can't drift out of sync with what's
actually drawn. A rectangle straddling the edge of the viewport still counts as visible.

## 11. in `internal_render_texture()`

White, so the texture blits untinted. The old code set the GL colour here and never
put it back, leaving white current for whatever drew next; that is kept rather than
tidied — this is EmuUtility's LCD, its only caller, and what follows it on screen was
drawn against that colour.

## 12. `tGlyphStep`

── UTF-8, reduced to the glyph table ────────────────────────────────────────

THE ATLAS HOLDS ASCII ONLY (see MAX_GLYPH_CHAR), and every byte above it used to be replaced by
'?' one byte at a time. A single em dash is three bytes of UTF-8, so a perfectly ordinary status
line — "Sent — the connected device's live edit buffer..." — reached the screen as "Sent ??? the
connected...". Owner-reported on SynthEdit's restore alert, but nothing about it was specific to
that string: the source of all three apps carries around ninety em dashes inside string literals,
and any of them that reaches a drawn string does the same thing.

So a whole UTF-8 sequence is consumed at once and turned into ONE glyph. The handful of
codepoints that actually occur in these apps' strings get a sensible ASCII stand-in; anything
else still becomes a single '?', which is a fair report of "this font has no such character"
rather than a count of how many bytes it took to encode.

Not a general Unicode layer, and deliberately not: the fix needed is that non-ASCII punctuation
stops multiplying, and a transliteration table does that in one place. Real non-Latin text would
need a real font atlas, which is a different job.

## 13. in `internal_render_text()`

No blend enable/disable here: render_backend_init() turns blending on for the
whole session. This function used to enable it and then DISABLE it on the way
out, which revoked the session-wide enable the moment the first string was
drawn — see the invariant in utilsGraphics.h.

The atlas is SELECTED, not bound: batch_set_texture() flushes whatever is queued against
the previous texture and records this one, and render_backend_flush() does the binding.
Consecutive strings drawn at the same size therefore leave as a single call.

## 14. in `internal_render_text()`

The baseline comes from the CANONICAL ascent, exactly where it sat before glyphs were
rasterized per size, and is rounded once. Deriving it from the atlas's own ascent instead
would round a second time against a rasterization whose em height only approximates the
requested one — which drops the text up to a pixel, and by differing amounts for different
text sizes, so some labels sit low and others don't.

## 15. in `internal_render_text()`

The pen advances by the CANONICAL advance, scaled to this size, so a string occupies
exactly the width get_text_width() predicted for it. Only the position each glyph is
finally drawn at is rounded, which costs sub-pixel letter spacing and buys pixel
alignment — the right trade at these sizes.

## 16. in `internal_render_text()`

How much to shrink this atlas's bitmaps by when drawing them. 1.0 for a 1:1 atlas. For a
supersampled one it is 1/factor, CORRECTED by the ratio between the height actually asked
for and the integer the atlas was built for — which is what removes the last of the
quantisation rather than merely reducing it.

## 17. in `internal_render_text()`

Placement. In the 1:1 case the pen is snapped to a whole pixel so the glyph lands on
exact texels — the crisp path, unchanged. In the supersampled case the bitmap is being
scaled anyway, so snapping buys nothing and costs the even letter spacing that made the
text look wrong in the first place: the position stays fractional.

## 18. `render_backend_texture_create()`

glColor3f/4f, as a plain variable. Nothing is submitted here — the colour is written into
each vertex as it is appended, so a colour change between two draws no longer splits them
into separate submissions the way a GL state change would have.
Creating one has no bearing on queued geometry — nothing can be sampling a texture that does
not exist yet — so this is a straight pass-through.

## 19. in `render_backend_texture_update()`

A HAZARD THAT DID NOT EXIST BEFORE BATCHING: in immediate mode every draw was submitted
before the next statement ran, so an upload could never overtake one. Queued vertices now
outlive the call that appended them, and they were appended to sample what this texture
held THEN. Uploading underneath them would redraw already-issued geometry with new pixels.

The rule lives HERE rather than in the backend, so that a new backend cannot forget it:
gfx_texture_write() is handed a texture nothing is waiting on.

## 20. in `contrasting_text_colour()`

These three sit just under the 0.5 luminance line but read fine with
the black text draw_button() always used pre-luminance — pinned here
rather than following the general rule below, since flipping already-
fine buttons to white wasn't asked for (2026-07-13 user call):
```
  - SynthEdit's RGB_GREEN_ON (0.0, 0.8, 0.0) — the on/off toggle
    "on" state, explicitly meant to be left alone by that same call.
  - G2-Edit's RGB_GREEN_7 (0.0, 0.7, 0.0) — comms Online / Tx / Rx.
  - G2-Edit's RGB_RED_5 (0.7, 0.2, 0.2) — voice-count conflict.
```
Compared by literal value, not the macros, since not all of these
names are defined outside their own app's synthlibDefs.h branch.

## 21. in `contrasting_text_colour()`

>= not > : RGB_GREY_5 (0.5 exactly, e.g. render_page_tabs()'s pressed
state) sits right on the boundary and every caller of draw_button()
used to get fixed black text, so the midpoint keeps resolving to black
rather than flipping existing UI to white on a change nobody asked for.

## 22. in `render_bezier_curve()`

The base colour the lighting is derived from. This used to be READ BACK OUT OF GL with
glGetFloatv(GL_CURRENT_COLOR) — the one place in the file that queried the graphics API
rather than driving it, and a pipeline stall to recover a value the caller had just set.
The colour is ours now, so it is simply read.

## 23. in `render_bezier_curve()`

Normal perpendicular to tangent: (-ty, tx)
Vertex A: normal pointing in (-ty, tx) direction
Vertex B: normal pointing in (+ty, -tx) direction
In screen space, negative y = upward = towards light source

## 24. in `render_bezier_curve()`

The vertex whose normal has a more negative y component faces the light
ny for vertex A = tx * thickness * 0.5
ny for vertex B = -tx * thickness * 0.5
So if tx > 0, vertex A faces up (highlight); if tx < 0, vertex B faces up

## 25. `draw_button_bounds()`

draw_button() draws a box DRAW_BUTTON_MARGIN pixels larger than the rect it is
handed (padding around the text), anchored at the same top-left — so the button
visually extends DRAW_BUTTON_MARGIN*2 further right and down than the input rect.
draw_button() RETURNS that true drawn rect, and callers must hit-test against it,
not the pre-expansion rect, or the bottom/right padding strip is visible-but-dead.
draw_button_bounds() reports the same rect without drawing, for the many hit-test
sites that recompute a button's rect separately from where it is drawn (the popup
dialogs, panel buttons) rather than storing draw_button()'s return value.

## 26. `build_glyph_atlas()`

Rasterizes the font at fontSize pixels into a self-contained atlas. Nothing global is touched
until it has fully succeeded, so a failure here leaves existing text rendering untouched.
When outAtlas is NULL only the metrics are produced (used for the canonical layout metrics,
which never need a texture).

## 27. `atlas_for_height()`

Returns the atlas rasterized for text of this drawn pixel height, building it on first use and
evicting the least recently used entry when the cache is full. In practice an app settles on
two or three sizes, so this builds a handful of times at startup and then never again until the
window is resized or zoomed.

## 28. `preload_glyph_textures()`

Establishes the canonical layout metrics. fontSize is only the size those metrics are measured
at — it no longer fixes how text is rasterized, since widths are normalised by the em height
and the drawn glyphs come from an atlas built per drawn size. Validating the font here keeps
the existing "try each path until one loads" behaviour in the embedding apps working.

## 29. in `get_y_scroll_percent()`

── List scrollbar (bankBrowser.cpp, fileBrowser.cpp, and similar) ──────────────────────────────

Drag state is file-static rather than passed in/out by each caller — safe only because the
callers are mutually exclusive modals, so at most one list scrollbar can ever be mid-drag.

## 30. `PANEL_CLOSE_BOX_LINE`

BORDER_LINE_WIDTH is sized for a whole panel and reads as a slab around a button this small.
The cross is drawn slightly heavier than its frame: the frame's lines are axis-aligned and stay
crisp, while the diagonals get antialiased and would otherwise look the lighter of the two, so
matching the numbers makes the box dominate the mark it exists to present.

## 31. in `draw_panel_close_button()`

Deliberately NOT RGB_BACKGROUND_GREY. This file compiles without G2_EDIT defined, so that
macro resolves to the dark 0.30 of synthlibDefs.h's other branch - the very same value as
the RGB_GREY_3 title bar drawn below, which made the button vanish into the banner and left
a black cross on dark grey. RGB_GREY_3 and RGB_GREY_7 are 0.30 and 0.70 in BOTH branches, so
deriving from those and letting contrasting_text_colour() choose the stroke is stable
whichever app is compiling.

## 32. `render_dial_with_text()`

The rectangle IS THE DIAL: its coord is the top-left of the circle's bounding square and its
width the diameter. The value string is drawn one row directly above the dial and the label one
row above that, growing upwards, so the dial sits exactly where the caller put it whatever text
it does or doesn't carry.

That anchoring is the point. This used to take the top-left of the whole label+value+dial block
and work downwards, which made the dial's position depend on how many of the two strings were
non-NULL - a dial with no label rode a row higher than its neighbours, and the only way to line
a row of them up was to pass "" instead of NULL so the row was reserved but blank. NULL and ""
now do the same thing, because neither can move the dial.
