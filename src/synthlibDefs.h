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

#if ENABLE_DEBUG
#define LOG_DEBUG(fmt, ...)    fprintf(stderr, "[DBG] " fmt, ## __VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)    do {} while (0)
#endif
#define LOG_ERROR(fmt, ...)    fprintf(stderr, "[ERR] " fmt, ## __VA_ARGS__)

#define MAX_GLYPH_CHAR          (127)

#define RGB_WHITE              {1.0, 1.0, 1.0}
#define RGB_BLACK              {0.0, 0.0, 0.0}
#define RGB_GREY               {0.5, 0.5, 0.5}
#define RGB_BACKGROUND_GREY    {0.30, 0.30, 0.30}
#define RGB_GREY_2             {0.20, 0.20, 0.20}
#define RGB_GREY_3             {0.30, 0.30, 0.30}
#define RGB_GREY_5             {0.50, 0.50, 0.50}
#define RGB_GREY_7             {0.70, 0.70, 0.70}
#define RGB_GREEN_ON           {0.00, 0.80, 0.00}
#define RGB_ORANGE_1            {1.00, 0.50, 0.00}
#define RGB_ORANGE_2            {1.00, 0.70, 0.00}

// TODO - Might want to either conditionally not define these if not G2 editor, or come up with another mechanism for them
#define TOP_BAR_HEIGHT          (0.0)
#define MODULE_MARGIN           (5.0)
#define MODULE_WIDTH            (350.0)
#define MODULE_HEIGHT           (38.0)
#define MAX_ROWS                (127)
#define MAX_ROWS_MODULE         (12)
#define MODULE_WIDTH            (350.0)
#define MODULE_X_GAP            (10.0)
#define MODULE_X_SPAN           (MODULE_WIDTH + MODULE_X_GAP)
#define MODULE_Y_GAP            (5.0)
#define MODULE_Y_SPAN           (MODULE_HEIGHT + MODULE_Y_GAP)
#define BORDER_LINE_WIDTH       (2.0)
#define STANDARD_TEXT_HEIGHT    (12.0)
#define BLANK_SIZE              (0.0)
#define SCROLLBAR_WIDTH         (15.0)
#define SCROLLBAR_LENGTH        (100.0)
#define SCROLLBAR_MARGIN        SCROLLBAR_WIDTH

#define NO_ZOOM                 (1.0)
#define MAX_COLUMNS             (127)

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_DEFS_H__
