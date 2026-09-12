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
// Notes: Docs/code-notes/synthlibScale.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_SCALE_H__
#define __SYNTHLIB_SCALE_H__

#ifdef __cplusplus
extern "C" {
#endif

// notes §1
void synthlib_scale_init(int targetFrameBuffWidth);
void synthlib_scale_query_initial(void * glfwWindow);
void synthlib_scale_update(int width, int height);
void synthlib_scale_set_content_scale(void * glfwWindow, float xscale);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_SCALE_H__
