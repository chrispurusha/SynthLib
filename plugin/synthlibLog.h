/*
 * SynthLib - a plug-in's diagnostic log, switched on by a file.
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
// Notes: Docs/code-notes/synthlibLog.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_LOG_H__
#define __SYNTHLIB_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

// notes §1
extern const char gSynthLibLogName[];

void synthlib_log_line(const char * format, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_LOG_H__
