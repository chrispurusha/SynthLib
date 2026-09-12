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
// Notes: Docs/code-notes/utilsGraphics.h.md - "// notes §k" refers there.

#ifndef __UTILS_GRAPHICS_H__
#define __UTILS_GRAPHICS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "synthlibTypes.h"

// notes §1
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

// notes §2
#include "renderBackend.h"

// notes §3

// notes §4
void render_backend_init(void);

// Viewport plus the 2D orthographic projection the whole UI is laid out in: origin
// top-left, y increasing downwards, one unit per physical pixel. Call on any
// framebuffer size change.
void render_backend_set_surface(int width, int height);

// Clears the colour buffer. No depth buffer is in play — render_backend_init()
// disables depth testing and nothing ever writes depth — so colour is the whole frame.
void render_backend_clear(tRgb colour);

// notes §5
void render_backend_flush(void);

// notes §6
void render_present(void);

// notes §7

// notes §8
uint32_t render_backend_texture_create(int width, int height, const uint8_t * rgba, tTextureFilter filter);

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

// notes §9
tRgb contrasting_text_colour(tRgb bg);

// notes §10
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

// notes §11
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

// notes §12
tRectangle draw_button_split(tArea area, tRectangle rectangle, const char * text, tRgb topColour, tRgb bottomColour);
tRectangle draw_button_bounds(tRectangle rectangle);   // the true clickable rect draw_button() draws for a given input
tRectangle draw_slider(tArea area, tRectangle rectangle, uint32_t value, uint32_t range, uint32_t morphRange, tRgb colour);

// notes §13
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

// notes §14
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
