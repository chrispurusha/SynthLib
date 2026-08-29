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

#ifndef RENDER_BACKEND_SELECT_H
#define RENDER_BACKEND_SELECT_H

// WHICH BACKEND THE APPLICATION STARTS WITH when nothing has been saved.
//
// This header used to CHOOSE the backend, at compile time, with each backend file wrapped in an #if
// that compiled it to nothing when it was not the one. Both are built now — the choice is made at
// start-up from a saved setting so a user can try the other without a rebuild — so all that is left
// here is the default, and renderBackendMetal.m is excluded only by being on a platform without
// Metal.
//
// OPENGL IS THE DEFAULT and should stay that way until Metal has had real use. It is the path that
// has been in a host, it is the one that runs unchanged on Windows and Linux, and every call it
// makes is OpenGL 1.1 or earlier.

#ifndef RENDER_BACKEND_DEFAULT
#define RENDER_BACKEND_DEFAULT    eRenderBackendOpenGL
#endif

#endif // RENDER_BACKEND_SELECT_H
