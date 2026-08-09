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


#ifndef __SYNTHLIB_DEFS_H__
#define __SYNTHLIB_DEFS_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_USB_LOG
void usb_log_text(const char * fmt, ...);
#define _USB_LOG(fmt, ...)    usb_log_text(fmt, ## __VA_ARGS__)
#else
#define _USB_LOG(fmt, ...)    ((void)0)
#endif

#define LOG_ERROR(fmt, ...)                                      \
   do {fprintf(stderr, "E %s() " fmt, __func__, ## __VA_ARGS__); \
       _USB_LOG("E %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#define LOG_WARNING(fmt, ...)                                    \
   do {fprintf(stderr, "W %s() " fmt, __func__, ## __VA_ARGS__); \
       _USB_LOG("W %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#define LOG_INFO(fmt, ...)                                       \
   do {fprintf(stdout, "I %s() " fmt, __func__, ## __VA_ARGS__); \
       _USB_LOG("I %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#ifdef ENABLE_LOG_DEBUG
#define LOG_DEBUG(fmt, ...)                                      \
   do {fprintf(stdout, "D %s() " fmt, __func__, ## __VA_ARGS__); \
       _USB_LOG("D %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#define LOG_DEBUG_DIRECT(fmt, ...)           \
   do {fprintf(stdout, fmt, ## __VA_ARGS__); \
       _USB_LOG(fmt, ## __VA_ARGS__);} while (0)
#else
// DISABLED, BUT THE ARGUMENTS STILL COUNT AS USED. `((void)0)` discarded them entirely, so any variable
// that existed only to be logged became an unused variable in Release while being perfectly used in
// Debug — two of those turned into errors the moment warnings became errors, and only in the
// configuration do-release builds. `if (0)` keeps every argument in an expression the compiler must
// still check, so the format string and its arguments stay type-checked in both configurations, then
// optimises away to nothing.
#define LOG_DEBUG(fmt, ...) \
   do {if (0) {fprintf(stdout, "D %s() " fmt, __func__, ## __VA_ARGS__);}} while (0)
#define LOG_DEBUG_DIRECT(fmt, ...) \
   do {if (0) {fprintf(stdout, fmt, ## __VA_ARGS__);}} while (0)
#endif
#ifdef ENABLE_LOG_MODULE_DATA
#define LOG_MODULE_DATA(fmt, ...)                                \
   do {fprintf(stdout, "D %s() " fmt, __func__, ## __VA_ARGS__); \
       _USB_LOG("D %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#define LOG_MODULE_DATA_DIRECT(fmt, ...)     \
   do {fprintf(stdout, fmt, ## __VA_ARGS__); \
       _USB_LOG(fmt, ## __VA_ARGS__);} while (0)
#else
// Same treatment as LOG_DEBUG above, and for the same reason — ENABLE_LOG_MODULE_DATA is defined by no
// configuration at all, so without this every variable that exists only to be logged here is unused in
// every build.
#define LOG_MODULE_DATA(fmt, ...) \
   do {if (0) {fprintf(stdout, "D %s() " fmt, __func__, ## __VA_ARGS__);}} while (0)
#define LOG_MODULE_DATA_DIRECT(fmt, ...) \
   do {if (0) {fprintf(stdout, fmt, ## __VA_ARGS__);}} while (0)
#endif

#define MAX_GLYPH_CHAR    (127)

// Mac-style nested context menu (see contextMenu.h) — top-level menu plus
// however many submenu flyouts can be open beneath it, and how long the mouse
// must dwell on a submenu-bearing item before it auto-opens.
#define MAX_MENU_DEPTH           (4)
#define MENU_HOVER_DELAY_SECS    (0.3)

// Clickable-rectangle registry (see clickRegion.h) — upper bound on how many
// regions a single frame can register across every render function combined.
#define MAX_CLICK_REGIONS            (4096)

#ifdef G2_EDIT
#define RGB_BLACK                    {0.0, 0.0, 0.0}
#define RGB_WHITE                    {1.0, 1.0, 1.0}
#define RGB_GREEN                    {0.0, 0.8, 0.0}
#define RGB_BACKGROUND_GREY          {0.8, 0.8, 0.8}
#define RGB_GREY_2                   {0.2, 0.2, 0.2}
#define RGB_GREY_3                   {0.3, 0.3, 0.3}
#define RGB_GREY_5                   {0.5, 0.5, 0.5}
#define RGB_GREY_7                   {0.7, 0.7, 0.7}
#define RGB_GREY_9                   {0.9, 0.9, 0.9}
#define RGB_GREEN_ON                 {0.3, 0.7, 0.3}
#define RGB_GREEN_3                  {0.0, 0.3, 0.0}
#define RGB_GREEN_7                  {0.0, 0.7, 0.0}
#define RGB_CONTEXT_MENU_GREEN       {0.2, 0.6, 0.2}
#define RGB_YELLOW_7                 {0.7, 0.7, 0.0}
#define RGB_RED_7                    {0.7, 0.0, 0.0}
#define RGB_RED_5                    {0.7, 0.2, 0.2}
#define RGB_ORANGE_0                 {0.8, 0.7, 0.5}
#define RGB_ORANGE_1                 {0.8, 0.3, 0.1}
#define RGB_ORANGE_2                 {0.8, 0.5, 0.2}
#define RGBA_BLACK_ON_TRANSPARENT    {0.0, 0.0, 0.0, 1.0}
#else
#define RGB_WHITE                    {1.0, 1.0, 1.0}
#define RGB_BLACK                    {0.0, 0.0, 0.0}
#define RGB_GREY                     {0.5, 0.5, 0.5}
#define RGB_BACKGROUND_GREY          {0.30, 0.30, 0.30}
#define RGB_GREY_2                   {0.20, 0.20, 0.20}
#define RGB_GREY_3                   {0.30, 0.30, 0.30}
#define RGB_GREY_5                   {0.50, 0.50, 0.50}
#define RGB_GREY_7                   {0.70, 0.70, 0.70}
#define RGB_GREEN_ON                 {0.00, 0.80, 0.00}
#define RGB_ORANGE_1                 {1.00, 0.50, 0.00}
#define RGB_ORANGE_2                 {1.00, 0.70, 0.00}
#endif

// The MODULE CANVAS's own scrollbar metrics — deliberately OUTSIDE the per-app #ifdef below.
//
// utilsGraphics.c compiles WITHOUT G2_EDIT defined (see the note in draw_panel_close_button), so
// anything inside that branch is invisible to it. That is exactly how the canvas came to reserve
// one width for the scrollbars while G2-Edit's split view drew them at another: module_area_for_pane()
// read the #else value and the app read the G2_EDIT one. Both sides read these instead.
//
// Matched to the Patch Window Split Bar's height (SPLIT_BAR_HEIGHT in G2-Edit's splitView.h) so the
// divider and the bars read as one family of furniture; if one changes, change the other.
#define MODULE_SCROLLBAR_WIDTH     (11.0)
#define MODULE_SCROLLBAR_MARGIN    MODULE_SCROLLBAR_WIDTH

// TODO - Might want to come up with another mechanism for switching these between projects
#ifdef G2_EDIT
#define TOP_BAR_HEIGHT    (80.0)
// Matched to the Patch Window Split Bar's own height (SPLIT_BAR_HEIGHT in splitView.h), so the
// divider and the scrollbars read as the same family of furniture rather than three thicknesses.
// Kept as a literal because this header cannot see the app's own headers; if one changes, change
// both.
#define SCROLLBAR_WIDTH          (11.0)
#define SCROLLBAR_LENGTH         (100.0)
#define SCROLLBAR_MARGIN         SCROLLBAR_WIDTH

#define MODULE_WIDTH             (350.0)
#define MODULE_X_GAP             (10.0)
#define MODULE_X_SPAN            (MODULE_WIDTH + MODULE_X_GAP)
#define MODULE_TITLE_X_OFFSET    (3.0)
#define MODULE_HEIGHT            (38.0)             // 1 row
#define MODULE_MARGIN            (5.0)
#define MODULE_Y_GAP             (5.0)
#define MODULE_Y_SPAN            (MODULE_HEIGHT + MODULE_Y_GAP)
#define MODULE_TITLE_Y_OFFSET    (20.0)
#define MODULE_AREA_X_MARGINS    ((MODULE_MARGIN * 2.0) + SCROLLBAR_WIDTH)
#define MODULE_AREA_Y_MARGINS    ((MODULE_MARGIN * 2.0) + TOP_BAR_HEIGHT + SCROLLBAR_WIDTH)
#define MODULE_AREA_X_WIDTH      ((double)renderWidth - (MODULE_AREA_X_MARGINS))
#define MODULE_AREA_Y_HEIGHT     ((double)renderHeight - (MODULE_AREA_Y_MARGINS))
#define NO_ZOOM                  (1.0)
#define ZOOM_DELTA               (0.1)
#else
#define TOP_BAR_HEIGHT           (0.0)
#define MODULE_MARGIN            (5.0)
#define MODULE_WIDTH             (350.0)
#define MODULE_HEIGHT            (38.0)
#define MAX_ROWS                 (127)
#define MAX_ROWS_MODULE          (12)
#define MODULE_WIDTH             (350.0)
#define MODULE_X_GAP             (10.0)
#define MODULE_X_SPAN            (MODULE_WIDTH + MODULE_X_GAP)
#define MODULE_Y_GAP             (5.0)
#define MODULE_Y_SPAN            (MODULE_HEIGHT + MODULE_Y_GAP)
#define BORDER_LINE_WIDTH        (2.0)
#define DRAW_BUTTON_MARGIN       (2.0)   // padding draw_button() adds around a button's text (see draw_button_bounds)
#define STANDARD_TEXT_HEIGHT     (12.0)
#define BLANK_SIZE               (0.0)
#define SCROLLBAR_WIDTH          (15.0)
#define SCROLLBAR_LENGTH         (100.0)
#define SCROLLBAR_MARGIN         SCROLLBAR_WIDTH
#define NO_ZOOM                  (1.0)
#define MAX_COLUMNS              (127)
#define NO_ZOOM                  (1.0)
#define ZOOM_DELTA               (0.1)
#endif

// Vertical scrollbar for list-style popups (bankBrowser.cpp, fileBrowser.cpp) — a proportional
// track+thumb, distinct from the main canvas's fixed-length pan scrollbar (SCROLLBAR_WIDTH above,
// which represents infinite-pan percent rather than a finite row count). Same width in both
// project variants, so it lives outside the G2_EDIT split above.
#define LIST_SCROLLBAR_WIDTH    (8.0)

// Floor on the thumb's height so it stays grabbable no matter how long the list is. Without it the
// proportional height collapses — a full device-wide patch sweep is ~1000 rows against 10 visible,
// which works out under 2pt on a 200pt track, leaving an 8x8 square to hit. The shortest track this
// is used on is 200pt (bankBrowser: 10 rows), so 24pt costs ~12% of the drag travel at worst.
#define LIST_SCROLLBAR_MIN_THUMB    (24.0)

// Double-click-to-confirm on list rows (bankBrowser.cpp, fileBrowser.cpp) — second click on the
// same row within this window counts as a double-click, same glfwGetTime()-based timing
// convention as MENU_HOVER_DELAY_SECS above (contextMenu.c).
#define DOUBLE_CLICK_DELAY_SECS    (0.4)

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_DEFS_H__
