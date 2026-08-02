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

void set_rgb_colour(tRgb rgb);
void set_rgba_colour(tRgba rgba);

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
