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

// WHICH BACKEND GETS COMPILED, and it is chosen here rather than by the build system on purpose.
//
// The obvious arrangement is to let the project list one backend file and omit the other. That
// does not survive this codebase: SynthLib/src is a PBXFileSystemSynchronizedRootGroup in all
// three Xcode projects, so every .c file in it is compiled automatically and there is no list to
// leave a file out of; and G2-Edit's do-vst3 keeps its source list BY HAND, so a file left out
// there breaks only the plug-in and only at link time.
//
// So each backend file wraps itself in one #if on RENDER_BACKEND, at file scope, and compiles to
// nothing when it is not the chosen one. That is the ONLY conditional compilation in the drawing
// code — there are no #ifdefs inside any function, and none at all in utilsGraphics.c, which is
// the point of the split. Adding a backend is: write the file, add a constant here.
//
// Define RENDER_BACKEND in the build (e.g. -DRENDER_BACKEND=RENDER_BACKEND_METAL) to override.

#define RENDER_BACKEND_GL       1
#define RENDER_BACKEND_METAL    2

#ifndef RENDER_BACKEND

// OpenGL everywhere, still. It is deprecated on macOS but present, and on Windows and Linux it is
// simply the native answer — every GL call the backend makes is OpenGL 1.1 or earlier, which no
// driver on any of the three platforms has ever not had. Metal takes over as the macOS default
// when renderBackendMetal.m exists and has been diffed against this one.
#define RENDER_BACKEND    RENDER_BACKEND_GL

#endif

#endif // RENDER_BACKEND_SELECT_H
