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
// start-up from a saved setting — so all that is left here is the default.
//
// THE DEFAULT FOLLOWS THE PLATFORM, since 2026-09-09: Metal on macOS, OpenGL everywhere else. It
// said OpenGL unconditionally, with a note that this should hold "until Metal has had real use".
// It has: both sibling plug-ins have only ever had Metal, G2-Edit's plug-in has had it since it
// gained an editor and now has nothing else linked, and the applications have been able to select
// it from a menu for as long.
//
// That is also the shape the ports need. OpenGL is not a fallback here, it is what Windows and
// Linux will run: renderBackendGL.c is OpenGL 1.1 with no platform in it, and #else below is where
// those builds land without anyone choosing anything.
//
// THE SAVED SETTING STILL WINS on either platform - synthlibWindow.c reads "renderBackend" from
// prefs.txt before it makes the window - so forcing OpenGL on a Mac that has trouble with Metal is
// still one line in a text file. G2-Edit's menu no longer offers the switch, deliberately: that is
// a recovery route rather than a setting to browse.

// A BUILD WITH NO OpenGL IN IT HAS ONLY ONE ANSWER. G2-Edit's plug-in defines
// SYNTHLIB_NO_GL_BACKEND, and a default of OpenGL there would name a backend that is not linked -
// see the note at the top of renderBackend.c.
// macOS IS METAL ONLY, and this is where that is decided for every target - the three applications
// and the three plug-ins alike. SYNTHLIB_NO_GL_BACKEND leaves the OpenGL backend out of the build
// entirely: renderBackendGL.c compiles to nothing, renderBackend.c does not declare its table, and
// gfx_backend_available() answers false for it.
//
// THE A/B IS STILL AVAILABLE, and that matters because renderBackendGL.c's own header argues for
// keeping it alive on macOS: running the two backends against each other on one machine is the cheap
// way to prove the Metal port moved no pixel. Build with SYNTHLIB_ALLOW_GL_ON_APPLE to get it back -
// and the same switch is the way back if Metal ever misbehaves on a particular Mac, since the
// prefs.txt route cannot help once the backend is not linked.
#if defined(__APPLE__) && !defined(SYNTHLIB_ALLOW_GL_ON_APPLE)
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
