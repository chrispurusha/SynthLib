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
// Notes: Docs/code-notes/renderBackendSelect.h.md - "// notes §k" refers there.

#ifndef RENDER_BACKEND_SELECT_H
#define RENDER_BACKEND_SELECT_H

// notes §1

// notes §2
#if defined (__APPLE__) && !defined (SYNTHLIB_ALLOW_GL_ON_APPLE)
#ifndef SYNTHLIB_NO_GL_BACKEND
#define SYNTHLIB_NO_GL_BACKEND    1
#endif
#endif

#ifndef RENDER_BACKEND_DEFAULT
#ifdef SYNTHLIB_NO_GL_BACKEND
#define RENDER_BACKEND_DEFAULT    eRenderBackendMetal
#else
#define RENDER_BACKEND_DEFAULT    eRenderBackendOpenGL
#endif
#endif

#endif // RENDER_BACKEND_SELECT_H
