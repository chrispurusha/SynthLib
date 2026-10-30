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
// Notes: Docs/code-notes/synthlibDefs.h.md - "// notes §k" refers there.


#ifndef __SYNTHLIB_DEFS_H__
#define __SYNTHLIB_DEFS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// The log level is set at run time, so the one build of SynthLib serves Debug and Release alike. An
// application sets it at start-up with SYNTHLIB_LOG_INIT() (synthlibVersion.h). A level includes those
// below it.
typedef enum {
    eSynthLibLogNormal = 0,     // errors, warnings and information
    eSynthLibLogDebug,          // and LOG_DEBUG
    eSynthLibLogModuleData      // and LOG_MODULE_DATA
} tSynthLibLogLevel;

typedef void (*tSynthLibLogSink)(const char * fmt, ...);

// Defined here rather than in a source file because every target that logs needs them, including
// the small tools that compile a single SynthLib file. Weak, so a link merges them into one; hidden,
// so two plug-ins in one host keep their own.
#define SYNTHLIB_LOG_STATE    __attribute__((weak, visibility("hidden")))
extern int                          gSynthLibLogLevel;
extern tSynthLibLogSink             gSynthLibLogSink;  // an extra destination for every message, or NULL
SYNTHLIB_LOG_STATE int              gSynthLibLogLevel = eSynthLibLogNormal;
SYNTHLIB_LOG_STATE tSynthLibLogSink gSynthLibLogSink  = NULL;

#define _LOG_SINK(fmt, ...) \
   do {if (gSynthLibLogSink != NULL) {gSynthLibLogSink(fmt, ## __VA_ARGS__);}} while (0)

#define LOG_ERROR(fmt, ...)                                      \
   do {fprintf(stderr, "E %s() " fmt, __func__, ## __VA_ARGS__); \
       _LOG_SINK("E %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#define LOG_WARNING(fmt, ...)                                    \
   do {fprintf(stderr, "W %s() " fmt, __func__, ## __VA_ARGS__); \
       _LOG_SINK("W %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#define LOG_INFO(fmt, ...)                                       \
   do {fprintf(stdout, "I %s() " fmt, __func__, ## __VA_ARGS__); \
       _LOG_SINK("I %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#define LOG_DEBUG(fmt, ...)                                          \
   do {if (gSynthLibLogLevel >= eSynthLibLogDebug) {                 \
           fprintf(stdout, "D %s() " fmt, __func__, ## __VA_ARGS__); \
           _LOG_SINK("D %s() " fmt, __func__, ## __VA_ARGS__);}      \
   } while (0)
#define LOG_DEBUG_DIRECT(fmt, ...)                   \
   do {if (gSynthLibLogLevel >= eSynthLibLogDebug) { \
           fprintf(stdout, fmt, ## __VA_ARGS__);     \
           _LOG_SINK(fmt, ## __VA_ARGS__);}          \
   } while (0)
#define LOG_MODULE_DATA(fmt, ...)                                    \
   do {if (gSynthLibLogLevel >= eSynthLibLogModuleData) {            \
           fprintf(stdout, "D %s() " fmt, __func__, ## __VA_ARGS__); \
           _LOG_SINK("D %s() " fmt, __func__, ## __VA_ARGS__);}      \
   } while (0)
#define LOG_MODULE_DATA_DIRECT(fmt, ...)                  \
   do {if (gSynthLibLogLevel >= eSynthLibLogModuleData) { \
           fprintf(stdout, fmt, ## __VA_ARGS__);          \
           _LOG_SINK(fmt, ## __VA_ARGS__);}               \
   } while (0)

#define MAX_GLYPH_CHAR    (127)

// Mac-style nested context menu (see contextMenu.h) — top-level menu plus
// however many submenu flyouts can be open beneath it, and how long the mouse
// must dwell on a submenu-bearing item before it auto-opens.
#define MAX_MENU_DEPTH                   (4)
#define MENU_HOVER_DELAY_SECS            (0.3)

// notes §2
#define MENU_SUBMENU_CLOSE_DELAY_SECS    (0.35)

// Clickable-rectangle registry (see clickRegion.h) — upper bound on how many
// regions a single frame can register across every render function combined.
#define MAX_CLICK_REGIONS           (4096)

#define RGB_WHITE                   {1.0, 1.0, 1.0}
#define RGB_BLACK                   {0.0, 0.0, 0.0}
#define RGB_GREY                    {0.5, 0.5, 0.5}
#define RGB_BACKGROUND_GREY         {0.30, 0.30, 0.30}
#define RGB_GREY_2                  {0.20, 0.20, 0.20}
#define RGB_GREY_3                  {0.30, 0.30, 0.30}
#define RGB_GREY_5                  {0.50, 0.50, 0.50}
#define RGB_GREY_7                  {0.70, 0.70, 0.70}
#define RGB_GREEN_ON                {0.00, 0.80, 0.00}
#define RGB_ORANGE_1                {1.00, 0.50, 0.00}
#define RGB_ORANGE_2                {1.00, 0.70, 0.00}

// notes §3
#define MODULE_SCROLLBAR_WIDTH      (11.0)
#define MODULE_SCROLLBAR_MARGIN     MODULE_SCROLLBAR_WIDTH

#define TOP_BAR_HEIGHT              (0.0)
#define MODULE_MARGIN               (5.0)
#define MODULE_WIDTH                (350.0)
#define MODULE_HEIGHT               (38.0)
#define MAX_ROWS                    (127)
#define MAX_ROWS_MODULE             (12)
#define MODULE_WIDTH                (350.0)
#define MODULE_X_GAP                (10.0)
#define MODULE_X_SPAN               (MODULE_WIDTH + MODULE_X_GAP)
#define MODULE_Y_GAP                (5.0)
#define MODULE_Y_SPAN               (MODULE_HEIGHT + MODULE_Y_GAP)
#define BORDER_LINE_WIDTH           (2.0)
#define DRAW_BUTTON_MARGIN          (2.0) // padding draw_button() adds around a button's text (see draw_button_bounds)
#define STANDARD_TEXT_HEIGHT        (12.0)
#define BLANK_SIZE                  (0.0)
#define SCROLLBAR_WIDTH             (15.0)
#define SCROLLBAR_LENGTH            (100.0)
#define SCROLLBAR_MARGIN            SCROLLBAR_WIDTH
#define NO_ZOOM                     (1.0)
#define MAX_COLUMNS                 (127)
#define NO_ZOOM                     (1.0)
#define ZOOM_DELTA                  (0.1)

// notes §5
#define LIST_SCROLLBAR_WIDTH        (8.0)

// notes §6
#define LIST_SCROLLBAR_MIN_THUMB    (24.0)

// Double-click-to-confirm on list rows (bankBrowser.cpp, fileBrowser.cpp) — second click on the
// same row within this window counts as a double-click, same glfwGetTime()-based timing
// convention as MENU_HOVER_DELAY_SECS above (contextMenu.c).
#define DOUBLE_CLICK_DELAY_SECS    (0.4)

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_DEFS_H__
