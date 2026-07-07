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
#define LOG_DEBUG(fmt, ...)           ((void)0)
#define LOG_DEBUG_DIRECT(fmt, ...)    ((void)0)
#endif
#ifdef ENABLE_LOG_MODULE_DATA
#define LOG_MODULE_DATA(fmt, ...)                                \
   do {fprintf(stdout, "D %s() " fmt, __func__, ## __VA_ARGS__); \
       _USB_LOG("D %s() " fmt, __func__, ## __VA_ARGS__);} while (0)
#define LOG_MODULE_DATA_DIRECT(fmt, ...)     \
   do {fprintf(stdout, fmt, ## __VA_ARGS__); \
       _USB_LOG(fmt, ## __VA_ARGS__);} while (0)
#else
#define LOG_MODULE_DATA(fmt, ...)           ((void)0)
#define LOG_MODULE_DATA_DIRECT(fmt, ...)    ((void)0)
#endif

#define MAX_GLYPH_CHAR    (127)

// Mac-style nested context menu (see contextMenu.h) — top-level menu plus
// however many submenu flyouts can be open beneath it, and how long the mouse
// must dwell on a submenu-bearing item before it auto-opens.
#define MAX_MENU_DEPTH               (4)
#define MENU_HOVER_DELAY_SECS        (0.3)

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

// TODO - Might want to come up with another mechanism for switching these between projects
#ifdef G2_EDIT
#define TOP_BAR_HEIGHT           (80.0)
#define SCROLLBAR_WIDTH          (15.0)
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

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_DEFS_H__
