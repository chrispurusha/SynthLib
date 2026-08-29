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

// See synthlibWindow.h for what this is and why the six callbacks below stopped being each
// application's business.

#ifdef __cplusplus
extern "C" {
#endif

#define GL_SILENCE_DEPRECATION    1

#include <stdio.h>
#include <stdlib.h>

#include "synthlibWindow.h"
#include "synthlibDefs.h"
#include "synthlibPersistence.h"
#include "prefs.h"
#include "inputState.h"
#include "synthlibScale.h"
#include "synthlibGlobals.h"
#include "geometry.h"
#include "renderBackendSelect.h"
#include "renderBackend.h"
#include "utilsGraphics.h"

// THE ONE PLACE A BACKEND CHANGES WHAT THE WINDOW IS, and it is a window concern rather than a
// drawing one. OpenGL wants GLFW to create a context alongside the window and make it current.
// Metal wants GLFW to create NO context at all — GLFW_NO_API, the same hint a Vulkan application
// uses — and then takes the NSWindow and puts a CAMetalLayer on it.
//
// The tests below are RUNTIME, not #ifs, because both backends are in the binary and the choice
// comes from a saved setting. This function being the difference between them is also why the
// choice must be made before the window is created and cannot change while running.
#ifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA    1
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include <GLFW/glfw3native.h>
#pragma clang diagnostic pop
#endif

// The saved setting, read once at window creation. It lives in SynthLib rather than in each
// application so all three get it from one place; an application only supplies the menu item that
// writes it.
#define PREFS_KEY_RENDER_BACKEND    "renderBackend"

static bool backend_is_opengl(void) {
    return gfx_backend_current() == eRenderBackendOpenGL;
}

// The window minimum, as a divisor of the design size. 640x360 for a 2560x1440 target, and still
// exactly the locked 16:9. The old TARGET/8 allowed a 320pt window, which on a 1x display is a 320px
// framebuffer — gGlobalGuiScale 0.25, putting body text at ~3px and the small labels at ~2px,
// unreadable however well they are rendered. At 640pt the 1x case bottoms out at ~6px, which is not.
#define SYNTHLIB_WINDOW_MIN_DIVISOR    (4)

// Which optional callbacks were actually registered, so the close handler can unregister exactly
// those and nothing else. Kept rather than re-derived because "unregister everything" would call
// glfwSet*Callback on a window during teardown for events the app never asked about — harmless
// today, but the reason EmuUtility's hand-written copy of this had already gone stale was that the
// register and unregister lists were two separate hand-maintained things. Here they are one.
static tSynthLibWindowCallbacks gCallbacks = {0};

// ── The six that only ever talked to SynthLib ────────────────────────────────

static void error_callback(int error, const char * description) {
    LOG_ERROR("GLFW error [%d]: %s\n", error, description);
}

static void framebuffer_size_callback(GLFWwindow * window, int width, int height) {
    (void)window;
    synthlib_scale_update(width, height);

    synthlib_request_redraw();
}

// Fires when the window moves to a display with a different HiDPI scale (e.g. dragging from a Retina
// built-in display to a non-Retina external one, or vice versa) — see synthlibScale.h's own comment
// for the bug this fixes (gContentScale used to be hardcoded 2.0f, so anything deriving a screen
// position from gGlobalGuiScale landed mispositioned wherever the real scale was not 2.0).
static void content_scale_callback(GLFWwindow * window, float xscale, float yscale) {
    (void)yscale; // these apps only ever use a single uniform scale factor

    synthlib_scale_set_content_scale(window, xscale);

    synthlib_request_redraw();
}

static void window_size_callback(GLFWwindow * window, int width, int height) {
    (void)window;
    (void)height;   // the aspect ratio is locked, so the width alone restores the window
    synthlib_save_window_size(width);
}

static void window_pos_callback(GLFWwindow * window, int x, int y) {
    (void)window;
    synthlib_save_window_pos(x, y);
}

static void window_close_callback(GLFWwindow * window) {
    (void)window;
    synthlib_window_close();
}

// ── The normalised shims ─────────────────────────────────────────────────────
//
// The boilerplate that used to be repeated around every event in every app, once. See
// tSynthLibInputHandlers in the header for what this deliberately does NOT take over.

static tSynthLibInputHandlers gHandlers = {0};

static void shim_mouse_button(GLFWwindow * window, int button, int action, int mods) {
    double x = 0.0;
    double y = 0.0;

    // BEFORE the handler runs: handlers read the shift/ctrl/cmd predicates, so the state has to be
    // current by the time they do. EmuUtility's own shim never did this at all.
    set_modifier_state_from_glfw(mods);
    glfwGetCursorPos(window, &x, &y);

    if (gHandlers.mouseButton != NULL) {
        gHandlers.mouseButton(synthlib_window_to_logical(x, y), synthlib_mouse_button(button, action), mods);
    }
    synthlib_request_redraw();
}

static void shim_cursor_pos(GLFWwindow * window, double x, double y) {
    (void)window;

    if (gHandlers.cursorPos != NULL) {
        gHandlers.cursorPos(synthlib_window_to_logical(x, y));
    }
    // NO REDRAW REQUEST HERE, matching what all three apps already did: a mouse move that changes
    // nothing should not repaint, and a handler that DOES change something asks for itself.
}

static void shim_key(GLFWwindow * window, int key, int scancode, int action, int mods) {
    (void)window;
    set_modifier_state_from_glfw(mods);   // a modifier press or release is a key event like any other

    if (gHandlers.key != NULL) {
        gHandlers.key(key, scancode, action, mods);
    }
    synthlib_request_redraw();
}

static void shim_char(GLFWwindow * window, unsigned int codepoint) {
    (void)window;

    if (gHandlers.character != NULL) {
        gHandlers.character(codepoint);
    }
    synthlib_request_redraw();
}

static void shim_scroll(GLFWwindow * window, double dx, double dy) {
    (void)window;

    if (gHandlers.scroll != NULL) {
        gHandlers.scroll(dx, dy);
    }
    synthlib_request_redraw();
}

static void shim_window_focus(GLFWwindow * window, int focused) {
    (void)window;

    if (gHandlers.windowFocus != NULL) {
        gHandlers.windowFocus(focused != GLFW_FALSE);
    }
    synthlib_request_redraw();
}

static void shim_window_refresh(GLFWwindow * window) {
    (void)window;

    if (gHandlers.windowRefresh != NULL) {
        gHandlers.windowRefresh();
    }
    synthlib_request_redraw();
}

// ── Create ───────────────────────────────────────────────────────────────────

void * synthlib_window_create(const tSynthLibWindowConfig * config, const tSynthLibWindowCallbacks * callbacks) {
    GLFWwindow * window     = NULL;
    int          minDivisor = SYNTHLIB_WINDOW_MIN_DIVISOR;
    int          fbWidth    = 0;
    int          fbHeight   = 0;

    if (config == NULL) {
        LOG_ERROR("synthlib_window_create called with no configuration\n");
        exit(EXIT_FAILURE);
    }

    if (config->minDivisor > 0) {
        minDivisor = config->minDivisor;
    }
    gCallbacks = (callbacks != NULL) ? *callbacks : (tSynthLibWindowCallbacks){
        0
    };

    // Normalised handlers take precedence over raw callbacks for the events they cover: an app
    // supplies one form or the other, never both for the same event.
    if (config->handlers != NULL) {
        gHandlers = *config->handlers;

        if (gHandlers.mouseButton != NULL) {
            gCallbacks.mouseButton = shim_mouse_button;
        }

        if (gHandlers.cursorPos != NULL) {
            gCallbacks.cursorPos = shim_cursor_pos;
        }

        if (gHandlers.key != NULL) {
            gCallbacks.key = shim_key;
        }

        if (gHandlers.character != NULL) {
            gCallbacks.character = shim_char;
        }

        if (gHandlers.scroll != NULL) {
            gCallbacks.scroll = shim_scroll;
        }

        if (gHandlers.windowFocus != NULL) {
            gCallbacks.windowFocus = shim_window_focus;
        }

        if (gHandlers.windowRefresh != NULL) {
            gCallbacks.windowRefresh = shim_window_refresh;
        }
    }
    // Everything that must be true before the first frame, and none of it needs a window: the dial
    // mode is set here rather than left to each app's saved-settings load, so a value read back from
    // NVM overwrites a known default rather than whatever the global happened to hold.
    synthlib_set_dial_mode(config->dialMode);
    configure_synthlib_theme(config->theme);

    // Injection point for the mouse-coord query every SynthLib popup/panel file (contextMenu.c,
    // menuBar.c, alertDialog.c, bankBrowser.cpp, fileBrowser.cpp) needs — see synthlibHost.h.
    synthlib_host_init((tSynthLibHost){
        .mouseCoord = config->mouseCoord,
    });
    synthlib_scale_init(config->targetWidth);

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);  // Needed for Intel systems with discrete graphics

    // THE CHOICE, made before anything is created. An unavailable saved value leaves the default
    // standing rather than failing — a preference file is not a thing to refuse to start over.
    long savedBackend = prefs_get_int(PREFS_KEY_RENDER_BACKEND, (long)RENDER_BACKEND_DEFAULT);

    if (!gfx_backend_choose((tRenderBackendId)savedBackend)) {
        LOG_ERROR("Saved render backend %ld unavailable; using %s\n",
                  savedBackend, gfx_backend_name(gfx_backend_current()));
    }

    if (backend_is_opengl()) {
#if GFX_MSAA_SAMPLES > 1
        // Under OpenGL the multisample buffer belongs to the PIXEL FORMAT, so it has to be asked
        // for before the context exists — there is no enabling it later. Metal has no equivalent
        // here: its sample count is a property of the render pass and the pipeline.
        glfwWindowHint(GLFW_SAMPLES, GFX_MSAA_SAMPLES);
#endif
    } else {
        // No context, no drawable, no swap chain — GLFW is reduced to a window and an event
        // source, which is exactly what is wanted.
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    window = glfwCreateWindow(config->targetWidth / minDivisor, config->targetHeight / minDivisor,
                              config->title, NULL, NULL);
    synthlib_set_window((void *)window);

    if (window == NULL) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwSetWindowSizeLimits(window, config->targetWidth / minDivisor, config->targetHeight / minDivisor,
                            GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowAspectRatio(window, config->targetWidth, config->targetHeight);

    if (backend_is_opengl()) {
        glfwMakeContextCurrent(window);
    }

    // Real initial scale for whichever display the window opens on, not the 2.0 (Retina-only)
    // assumption this used to hardcode — see content_scale_callback() above.
    synthlib_scale_query_initial(window);

    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    framebuffer_size_callback(window, fbWidth, fbHeight);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetWindowContentScaleCallback(window, content_scale_callback);
    glfwSetWindowSizeCallback(window, window_size_callback);
    glfwSetWindowPosCallback(window, window_pos_callback);
    glfwSetWindowCloseCallback(window, window_close_callback);
    if (backend_is_opengl()) {
        // Vsync. There is no context to set it on under any other backend; a CAMetalLayer says the
        // same thing with displaySyncEnabled, which renderBackendMetal.m sets explicitly.
        glfwSwapInterval(1);
    }

    if (gCallbacks.key != NULL) {
        glfwSetKeyCallback(window, gCallbacks.key);
    }

    if (gCallbacks.character != NULL) {
        glfwSetCharCallback(window, gCallbacks.character);
    }

    if (gCallbacks.cursorPos != NULL) {
        glfwSetCursorPosCallback(window, gCallbacks.cursorPos);
    }

    if (gCallbacks.mouseButton != NULL) {
        glfwSetMouseButtonCallback(window, gCallbacks.mouseButton);
    }

    if (gCallbacks.scroll != NULL) {
        glfwSetScrollCallback(window, gCallbacks.scroll);
    }

    if (gCallbacks.windowFocus != NULL) {
        glfwSetWindowFocusCallback(window, gCallbacks.windowFocus);   // clears held modifiers — see inputState.h
    }

    if (gCallbacks.windowRefresh != NULL) {
        glfwSetWindowRefreshCallback(window, gCallbacks.windowRefresh);
    }
    // Session-wide drawing state, set once now the surface exists. Blending is on for the whole
    // session in all three apps; render_backend_init() owns that (and the invariant note), so this
    // layer no longer names a graphics API of its own.
    render_backend_init();

#ifdef __APPLE__

    if (!backend_is_opengl()) {
        // The backend gets the NSWindow to present into. AFTER render_backend_init(), so the
        // device the layer is given is the one the frames are rendered with, and after the
        // framebuffer size is known, so the drawable is sized on the first frame not the second.
        gfx_attach_window(glfwGetCocoaWindow(window));
    }

#endif

    return (void *)window;
}

// ── Close ────────────────────────────────────────────────────────────────────

void synthlib_window_close(void) {
    GLFWwindow * window = (GLFWwindow *)synthlib_window();

    if (window == NULL) {
        return;
    }
    synthlib_clear_redraw();

    // Unregistering matters: GLFW can still deliver events between the close request and the loop
    // noticing it, and a handler that runs while the app is tearing its state down is a crash
    // waiting for the right timing.
    glfwSetFramebufferSizeCallback(window, NULL);
    glfwSetWindowContentScaleCallback(window, NULL);
    glfwSetWindowSizeCallback(window, NULL);
    glfwSetWindowPosCallback(window, NULL);
    glfwSetWindowCloseCallback(window, NULL);

    if (gCallbacks.key != NULL) {
        glfwSetKeyCallback(window, NULL);
    }

    if (gCallbacks.character != NULL) {
        glfwSetCharCallback(window, NULL);
    }

    if (gCallbacks.cursorPos != NULL) {
        glfwSetCursorPosCallback(window, NULL);
    }

    if (gCallbacks.mouseButton != NULL) {
        glfwSetMouseButtonCallback(window, NULL);
    }

    if (gCallbacks.scroll != NULL) {
        glfwSetScrollCallback(window, NULL);
    }

    if (gCallbacks.windowFocus != NULL) {
        glfwSetWindowFocusCallback(window, NULL);
    }

    if (gCallbacks.windowRefresh != NULL) {
        glfwSetWindowRefreshCallback(window, NULL);
    }
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    glfwPostEmptyEvent();
}

#ifdef __cplusplus
}
#endif
