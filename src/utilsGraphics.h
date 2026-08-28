/*
 * SynthLib - common library for synthesizer editor applications.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __UTILS_GRAPHICS_H__
#define __UTILS_GRAPHICS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "synthlibTypes.h"

// The handful of visual values that genuinely differ between apps (colours,
// top bar height) and that this file's own drawing code needs — everything
// else app-specific stays a compile-time macro in each app's own defs.h,
// resolved normally since those files define G2_EDIT (or don't) for
// themselves. This is how utilsGraphics.cpp itself gets told which app it's
// drawing for, without including that app's defs.h (see configure_synthlib_theme()).
typedef struct {
    double topBarHeight;
    tRgb   orange1;
    tRgb   orange2;
    tRgb   greenOn;
    tRgb   backgroundGrey;
} tSynthLibTheme;

// Call once, early at startup (before any rendering), with values built from
// the calling app's own macros — e.g. G2-Edit's init_graphics() passes
// TOP_BAR_HEIGHT/RGB_ORANGE_1/etc from its own defs.h.
void configure_synthlib_theme(tSynthLibTheme theme);

// Dims the whole canvas behind a modal panel.
void draw_dialog_background_overlay(void);

void set_rgb_colour(tRgb rgb);
void set_rgba_colour(tRgba rgba);

// ── Render backend seam ──────────────────────────────────────────────────────
//
// The eight calls below are what the APPLICATIONS use to drive the renderer. They are
// no longer where a port happens: since 2026-08-28 the graphics API itself lives behind
// renderBackend.h's nine gfx_* calls, implemented once per platform in a renderBackend*.c
// that renderBackendSelect.h chooses. READ THAT HEADER BEFORE WRITING A BACKEND — it is
// the contract, and it says what deliberately is NOT part of it.
//
// What these eight are, then, is the portable half: each applies whatever batching rule
// its operation needs and hands the rest to a gfx_* call. utilsGraphics.c has named no
// graphics API since the split, which the compiler enforces by there being no such
// declaration in scope.
//
// Four of them had three or four separate copies before the seam existed: the GLFW window
// layer, the scale/resize path, each app's frame loop, and the plug-in's own NSOpenGLView
// (vst3/g2GlDraw.c), which shares these renderers but has no GLFW underneath it.
// render_backend_flush() arrived with geometry batching, and the three texture calls with
// EmuUtility's LCD, which until then created and uploaded its texture with raw GL in its
// own emuGraphics.c — the last thing outside this file that named the API.

// Session-wide drawing state. Call once, after a context is current and before
// anything is drawn. BLENDING IS ON FOR THE WHOLE SESSION and no drawing code turns
// it off again: an opaque draw (alpha == 1.0) resolves to the source colour whether
// blending is enabled or not, so the invariant costs opaque drawing nothing and
// spares every translucent caller an enable/disable pair of its own. render_text()
// used to end by DISABLING blend, which silently revoked the session-wide enable
// after the first string was drawn and is why the two translucent callers each
// carried their own pair.
void render_backend_init(void);

// Viewport plus the 2D orthographic projection the whole UI is laid out in: origin
// top-left, y increasing downwards, one unit per physical pixel. Call on any
// framebuffer size change.
void render_backend_set_surface(int width, int height);

// Clears the colour buffer. No depth buffer is in play — render_backend_init()
// disables depth testing and nothing ever writes depth — so colour is the whole frame.
void render_backend_clear(tRgb colour);

// Submits everything the drawing primitives have queued since the last submission.
//
// The primitives no longer issue a draw call each: they append triangles to one vertex
// array, which goes out in a single call. That array must be submitted before the frame
// is presented, so CALL THIS IMMEDIATELY BEFORE THE BUFFER SWAP in every frame loop —
// the three applications' render_frame() and the plug-in's g2_gl_draw_frame(). Miss it
// and the frame shows only what a mid-frame flush happened to force out; on a frame
// whose last drawing was a plain filled rectangle, that is nothing at all.
//
// Everything else that needs it calls it already, and all inside utilsGraphics.c: the
// module-pane scissor, the projection change, the clear, the frame read-back behind
// SCREENSHOT, and any glyph-atlas texture the batch might still be referencing. The
// batch is also submitted whenever the texture changes, so an untextured run and a run
// of text are separate calls; consecutive draws sharing a texture and a clip are one.
//
// Draw ORDER is never reordered. A 2D UI is painted back to front, so the batch is a
// pure concatenation of what was already going to be drawn, flushed at every point that
// would change how subsequent vertices rasterize.
void render_backend_flush(void);

// ── Textures ────────────────────────────────────────────────────────────────
//
// A texture is an OPAQUE HANDLE, not a graphics-API object. Under OpenGL it happens to
// be the GLuint; under Metal it will be an index into a table the backend keeps, since
// an id<MTLTexture> does not fit in an integer. Callers see no difference — which is
// the point, because the two things that hold one (this file's glyph atlases and
// EmuUtility's gLcdTexture) are already plain integers and do not change when the
// backend does. 0 is "no texture" and is never a valid handle.
//
// Every texture is RGBA8, nearest-filtered and clamped to edge. There is no parameter
// for any of that because both callers blit one texel per pixel: filtering could only
// blur a sample that already lands dead centre on its texel, and a UV never leaves
// [0,1] so the wrap mode is unobservable. If a caller ever genuinely needs filtering,
// add it then, with the case in front of you.

// Allocates width*height RGBA8 texels. `rgba` fills them, or NULL leaves them
// undefined for a later _update(). Returns 0 on failure.
uint32_t render_backend_texture_create(int width, int height, const uint8_t * rgba);

// Replaces a sub-rectangle. Flushes first if the batch is still referencing this
// texture — queued vertices were appended to sample the OLD contents, and a mid-frame
// upload would silently give them the new ones.
void render_backend_texture_update(uint32_t texture, int x, int y, int width, int height, const uint8_t * rgba);

// Frees it. Flushes first for the same reason, so nothing queued outlives its texture.
void render_backend_texture_destroy(uint32_t texture);

// Reads the frame back as tightly-packed RGB triples, bottom row first, into a
// caller-supplied buffer of at least width*height*3 bytes. Backs the backdoor
// SCREENSHOT command. False if the arguments are unusable.
bool render_backend_read_pixels_rgb(int x, int y, int width, int height, uint8_t * out);

// Perceptual (Rec. 601) luminance of bg, thresholded at 0.5 — the usual
// black/white crossover point for this formula. Lets callers put a label on
// a caller-supplied background colour (module/category colours, which range
// from near-black to near-white) without needing to know in advance whether
// black or white text will read against it.
tRgb contrasting_text_colour(tRgb bg);

// ── Module panes ────────────────────────────────────────────────────────────────────────────────
//
// The module canvas is drawn as one or more PANES stacked vertically down the window. Today there
// is exactly one, occupying the whole canvas band, so this is behaviourally identical to the single
// canvas that came before it — the structure exists so the Patch Window Split Bar can show the
// Voice Area and the FX Area at once (see G2-Edit's todo.txt) without every drawing call having to
// learn which half it is drawing into.
//
// A pane owns its SCROLL POSITION and its slice of the canvas band. It does NOT own the zoom:
// gZoomFactor stays global, so both panes always draw at the same scale. That is deliberate —
// matched scale is what makes two areas comparable side by side, and it keeps scale() a plain
// global multiply for the many callers that have no idea panes exist. Promoting zoom into the pane
// later is a contained change if it turns out to be wanted.
//
// The rendering transform reads the CURRENT pane, in the same "mode rather than argument" style
// that set_param_render_area() already uses for the parameter renderers: draw one pane's worth of
// modules, switch, draw the next. Rendering is sequential, so a mode is sufficient and avoids
// threading a pane argument through every render call in the app.
#define MAX_MODULE_PANES    (2)

// Selects the pane that module-space drawing and hit-testing resolve against. Out-of-range values
// are ignored, so a caller can't leave the transform pointing at nothing.
void set_module_pane(uint32_t pane);
uint32_t module_pane(void);

// How many panes are currently shown. 1 until the split bar sets up the second.
uint32_t module_pane_count(void);
void set_module_pane_count(uint32_t count);

// Sets a pane's vertical slice of the canvas band, both as fractions of that band: 0.0/1.0 is the
// whole thing, which is what pane 0 is set to at startup.
void set_module_pane_extent(uint32_t pane, double topFraction, double heightFraction);

// The current pane's rectangle, in window coordinates. Everything drawn into moduleArea is offset
// into this.
tRectangle module_area(void);

// Any pane's rectangle, without disturbing the current selection — for hit-testing a click against
// each pane in turn to decide which one it landed in.
tRectangle module_area_for_pane(uint32_t pane);

// Clips drawing to the current pane's rectangle. REQUIRED around a pane's render pass once there
// is more than one pane, and it is what makes panes actually independent: a module taller than its
// pane, or scrolled so it straddles the divider, would otherwise keep drawing straight into its
// neighbour — nothing else in this library clips. It also hides the connectors that
// render_modules() deliberately still draws for culled modules (so cables keep their endpoints),
// which used to be safely off-screen and, with panes, land in the other half of the window.
void module_pane_clip_begin(void);
void module_pane_clip_end(void);

bool rectangle_visible_in_module_area(tRectangle rectangle);
tRectangle render_line(tArea area, tCoord start, tCoord end, double thickness);
tRectangle render_rectangle(tArea area, tRectangle rectangle);
tRectangle render_texture(tArea area, tRectangle rectangle, uint32_t texture);
tRectangle render_rectangle_with_border(tArea area, tRectangle rectangle);
tRectangle render_triangle(tArea area, tTriangle triangle);
tRectangle render_circle_line(tArea area, tCoord coord, double radius, int segments, double thickness);
tRectangle render_circle_part(tArea area, tCoord coord, double radius, int segments, int startSeg, int numSegs);
tRectangle render_circle_part_angle(tArea area, tCoord coord, double radius, double startAngle, double endAngle, int numSteps);
tRectangle render_radial_line(tArea area, tCoord coord, double radius, double angleDegrees, double thickness);
tRectangle draw_power_button(tArea area, tRectangle rectangle, bool active);
tRectangle draw_button(tArea area, tRectangle rectangle, const char * text, tRgb backgroundColour);

// A button whose face is painted in two colours, split across the middle — for a control carrying
// two independent states that one fill colour cannot express (G2-Edit's variation buttons: green
// for "selected", orange for "linked", and both at once when it is both). Identical to draw_button()
// in every other respect, and draw_button() is itself this with one colour passed twice. Pass the
// same colour for both to get a plain button.
tRectangle draw_button_split(tArea area, tRectangle rectangle, const char * text, tRgb topColour, tRgb bottomColour);
tRectangle draw_button_bounds(tRectangle rectangle);   // the true clickable rect draw_button() draws for a given input
tRectangle draw_slider(tArea area, tRectangle rectangle, uint32_t value, uint32_t range, uint32_t morphRange, tRgb colour);

// Shared chrome for every modal panel and popup: the bordered box, its darker title bar, and a
// close button in the TOP LEFT corner - macOS's corner for it, and where these all used to get it
// wrong by putting a "Close" text button top right instead.
//
// draw_panel_chrome() returns the title bar rect (some panels use it as a drag handle) and indents
// the title past the close button. panel_close_button_rect() is the geometry on its own, for
// hit-testing outside the render pass; draw_panel_close_button() draws it and returns the same rect.
tRectangle draw_panel_chrome(tArea area, tRectangle box, double titleH, const char * title);
tRectangle panel_close_button_rect(tRectangle box);
tRectangle draw_panel_close_button(tArea area, tRectangle box, bool closePressed);
tRectangle render_bezier_curve(tArea area, tCoord start, tCoord control, tCoord end, double thickness, int segments);
tRectangle render_text(tArea area, tRectangle rectangle, const char * text);
bool preload_glyph_textures(const char * fontPath, double fontSize);
double get_text_width(const char * text, double targetHeight, tCache useCache);
double largest_text_width(int numItems, const char ** text, double targetHeight, tCache useCache);
void free_textures(void);
double get_scroll_bar_percent(double scrollBar, double renderSize);
double set_scroll_bar_percent(double percent, double renderSize);
double clamp_scroll_bar(double value, double max_value);
void set_x_scroll_percent(double percent);
void set_y_scroll_percent(double percent);
double get_x_scroll_percent(void);
double get_y_scroll_percent(void);
void set_zoom_factor(double zoomFactor, tCoord mouseCoord);
//void set_x_end_max(double xEndMax);
//void set_y_end_max(double yEndMax);
//double get_x_end_max(void);
//double get_y_end_max(void);
double get_char_width(char ch, double targetHeight);
void set_render_width(int width);
void set_render_height(int height);
double get_zoom_factor(void);
int get_render_width(void);
int get_render_height(void);
double calc_scroll_x(void);
double calc_scroll_y(void);
tRectangle rectangle_scale_from_percent(tRectangle rectangle);
double scale_from_percent(double val);
tRectangle render_dial(tArea area, tRectangle rectangle, uint32_t value, uint32_t range, uint32_t morphRange, tRgb colour);
tRectangle render_dial_with_text(tArea area, tRectangle rectangle, const char * label, const char * buff, double labelH, uint32_t value, uint32_t range, uint32_t morphRange, tRgb colour);

// Shared vertical scrollbar for list-style popups (bankBrowser.cpp, fileBrowser.cpp, and similar) —
// listRect is the list's own content box; totalRows/visibleRows/scrollOffset are the same terms
// each caller already tracks for scroll-wheel handling (scrollOffset is the index of the first
// visible row, a double so it can be compared/clamped against fractional drag positions).
//
// Drag state lives here as file-static, not per-caller — safe because bankBrowser/fileBrowser
// are mutually exclusive modals, so only one list scrollbar can ever be mid-drag at a time.
// Usage: list_scrollbar_mouse_down() on mouse-down (returns true if the thumb was hit, and starts
// the drag); while list_scrollbar_dragging() is true, feed every mouse-move to
// list_scrollbar_mouse_drag() and store its returned (already-clamped) scrollOffset back; call
// list_scrollbar_mouse_up() on mouse-up regardless.
tRectangle list_scrollbar_thumb_rect(tRectangle listRect, int32_t totalRows, int32_t visibleRows, double scrollOffset);
void render_list_scrollbar(tRectangle listRect, int32_t totalRows, int32_t visibleRows, double scrollOffset);
bool list_scrollbar_mouse_down(tRectangle listRect, int32_t totalRows, int32_t visibleRows, double scrollOffset, tCoord coord);
bool list_scrollbar_dragging(void);
double list_scrollbar_mouse_drag(tCoord coord);
void list_scrollbar_mouse_up(void);

#ifdef __cplusplus
}
#endif

#endif // __UTILS_GRAPHICS_H__
