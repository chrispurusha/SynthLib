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

#ifndef __SYNTHLIB_WINDOW_H__
#define __SYNTHLIB_WINDOW_H__

// THE SHARED GLFW WINDOW BOOTSTRAP.
//
// G2-Edit, SynthEdit and EmuUtility each opened their window with the same ~60 lines: the same two
// window hints, the same TARGET/4 minimum and locked aspect ratio, the same scale query, the same
// twelve callback registrations. Six of the callbacks were character-identical in all three, and one
// of them had already drifted — EmuUtility registered a focus callback its close handler never
// unregistered, because that handler was a copy taken before the focus callback existed. Anything
// duplicated three times drifts eventually; this is what it drifts into.
//
// So SynthLib now owns the whole sequence, and an application supplies only the handlers that reach
// into its own domain: key, char, cursor, mouse button, scroll, focus, refresh. The six that only
// ever talked to SynthLib — error, framebuffer size, content scale, window size, window position and
// window close — are implemented here and are no longer the application's business.
//
// THIS HEADER INCLUDES GLFW, WHICH inputState.h DELIBERATELY DOES NOT. The difference is real rather
// than a lapse: inputState.c has to link into a VST3 plug-in, where there is no GLFW and the
// modifier bits arrive from an NSEvent, so its header must stay platform-free. A window bootstrap
// has no such second life — a plug-in is handed a window by its host and never creates one — so the
// callback types are spelled exactly rather than laundered through void pointers, and a mismatched
// handler is a compile error instead of a crash.

#include <GLFW/glfw3.h>

#include "synthlibTypes.h"
#include "synthlibGlobals.h"
#include "synthlibHost.h"
#include "utilsGraphics.h"

#ifdef __cplusplus
extern "C" {
#endif

// THE NORMALISED INPUT HANDLERS — the alternative to supplying raw GLFW callbacks below.
//
// Every app wrote the same shim around every event: update the modifier state from the GLFW mods,
// fetch and scale the cursor position, decode the button/action pair, call its own handler, ask for
// a redraw. Seven tiny functions per app, three apps, and they had already drifted — EmuUtility's
// shims never called set_modifier_state_from_glfw() at all, so its shift/ctrl predicates only ever
// held whatever a previous event happened to leave behind.
//
// Supply these instead of the raw callbacks and SynthLib installs its own shims: the boilerplate
// happens once, and the app receives events already in its own vocabulary — a logical tCoord and a
// tMouseButton — rather than GLFW's.
//
// WHAT THIS DOES NOT DO, deliberately: it does not dispatch popups for you. Each host still calls
// synthlib_popups_dispatch_*() from inside its own handler, because where that sits relative to the
// host's other early checks is a real decision — G2-Edit swallows canvas input during a device
// operation BEFORE consulting the popups, and moving that silently would change behaviour nobody
// asked to change. Normalising the shim is mechanical; reordering the pipeline is not.
typedef struct {
    void (*mouseButton)(tCoord coord, tMouseButton button, int mods);
    void (*cursorPos)(tCoord coord);
    void (*key)(int key, int scancode, int action, int mods);
    void (*character)(unsigned int codepoint);
    void (*scroll)(double dx, double dy);
    void (*windowFocus)(bool focused);
    void (*windowRefresh)(void);
} tSynthLibInputHandlers;

typedef struct {
    const char *               title;           // the whole title, composed by the app: it owns __DATE__/__TIME__
    int                        targetWidth;     // design-space framebuffer size, and the locked aspect ratio
    int                        targetHeight;
    int                        minDivisor;      // window minimum is target/this; 0 means the shared default of 4
    tDialMode                  dialMode;        // G2-Edit is rotary, the other two vertical
    tSynthLibTheme             theme;           // built from the app's own colour/metric macros
    tSynthLibMouseCoordFn      mouseCoord;      // synthlib_host_init's injection point, see synthlibHost.h
    tSynthLibPointerCapturedFn pointerCaptured; // ditto: true while a drag has the pointer hidden

    // Normalised handlers. When non-NULL, SynthLib installs its own GLFW shims for the events these
    // cover, and the matching raw callbacks are ignored — see tSynthLibInputHandlers above.
    const tSynthLibInputHandlers * handlers;
} tSynthLibWindowConfig;

// Every one of these is optional. NULL simply leaves that GLFW callback unregistered, which is what
// an app that has nothing to do with the event wants — EmuUtility takes no character input, and
// G2-Edit has no use for a refresh callback.
typedef struct {
    GLFWkeyfun           key;
    GLFWcharfun          character;
    GLFWcursorposfun     cursorPos;
    GLFWmousebuttonfun   mouseButton;
    GLFWscrollfun        scroll;
    GLFWwindowfocusfun   windowFocus;
    GLFWwindowrefreshfun windowRefresh;
} tSynthLibWindowCallbacks;

// Creates the window and leaves it current, scaled, and fully wired. Returns it, and stores it in
// synthlib_window() — so the return value is a convenience, not a thing the caller has to keep.
//
// EXITS THE PROCESS if GLFW will not start or the window cannot be created, which is what all three
// applications already did at these two points: there is no useful way to continue, and returning a
// failure only moves the same exit() to a less informative place.
//
// Does NOT initialise fonts or the app's own subsystems. Everything here is window and input; the
// caller still does its own FreeType/atlas setup afterwards, because the three apps genuinely differ
// there (one preloads a glyph atlas, two call their own init_font()).
void * synthlib_window_create(const tSynthLibWindowConfig * config, const tSynthLibWindowCallbacks * callbacks);

// The window-coordinate transform and the button decode used to be declared here. They moved to
// inputState.h — they take plain doubles and ints, no GLFW types, and this header drags in
// <GLFW/glfw3.h>, which some hosts deliberately keep out of their input files (SynthEdit's
// mouseHandle.c hand-declares the handful of GLFW functions it needs rather than include it).
// Putting them on the platform-free input seam is what lets those files use them at all.

// The teardown half, registered as the GLFW close callback by synthlib_window_create(). Exposed
// because an application may also want to close the window from a menu item rather than the window
// control. Unregisters exactly the callbacks that were registered — which is the property that had
// already been lost to copy-and-paste in one of the three apps — then asks GLFW to close and wakes
// the event loop so the request is acted on promptly.
void synthlib_window_close(void);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_WINDOW_H__
