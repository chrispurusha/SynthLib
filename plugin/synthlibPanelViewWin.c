/*
 * SynthLib - a plug-in's own drawn panel on Windows, the HWND counterpart to synthlibPanelView.m.
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
// Notes: Docs/code-notes/synthlibPanelViewWin.c.md - "// notes §k" refers there.

#if defined (_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#include "renderBackend.h"
#include "inputState.h"
#include "synthlibPanelView.h"

#define TIMER_TICK       (1u)
#define TICK_MS          (33u)    // 30 Hz, as the Mac view
#define KEY_UNKNOWN      (-1)     // GLFW's codes, written out as synthlibPanelView.m does

typedef struct {
    const tSynthLibPanel * panel;
    void *                 user;
    HWND                   hwnd;
    HDC                    dc;
    HGLRC                  rc;
    bool                   ticking;
    bool                   tracking;    // TrackMouseEvent armed for the WM_MOUSELEAVE
    bool                   captured;    // press() grabbed a control; the pointer is hidden and held
    bool                   following;   // press() grabbed one that keeps the pointer visible
    POINT                  anchor;      // where a captured drag holds the pointer, client coordinates
} tPanelWin;

static wchar_t   gClassName[64] = {0};
static HINSTANCE gModule        = NULL;
static HWND      gRootWindow    = NULL;
static HDC       gRootDc        = NULL;
static HGLRC     gRootRc        = NULL;
static bool      gCursorHidden  = false;

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// notes §1: one class per loaded DLL, named from its address, so two plug-ins' views never meet
static bool register_class(void) {
    if (gClassName[0] != 0) {
        return true;
    }
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void *)&register_class, &gModule);
    swprintf(gClassName, sizeof(gClassName) / sizeof(gClassName[0]), L"SynthLibPanel%p", (void *)gModule);

    WNDCLASSEXW wc = {0};

    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc   = panel_proc;
    wc.hInstance     = gModule;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = gClassName;

    if (RegisterClassExW(&wc) == 0) {
        gClassName[0] = 0;
        return false;
    }
    return true;
}

static bool set_pixel_format(HDC dc) {
    PIXELFORMATDESCRIPTOR pfd = {0};

    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int format = ChoosePixelFormat(dc, &pfd);

    return (format != 0) && (SetPixelFormat(dc, format, &pfd) == TRUE);
}

// notes §2: a hidden context every view shares its textures with, so the glyph atlas outlives any one editor
static bool ensure_root_context(void) {
    if (gRootRc != NULL) {
        return true;
    }
    gRootWindow = CreateWindowExW(0, gClassName, L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, gModule, NULL);

    if (gRootWindow == NULL) {
        return false;
    }
    gRootDc = GetDC(gRootWindow);

    if (!set_pixel_format(gRootDc)) {
        return false;
    }
    gRootRc = wglCreateContext(gRootDc);
    return gRootRc != NULL;
}

static void make_current(tPanelWin * w) {
    if (wglGetCurrentContext() != w->rc) {
        wglMakeCurrent(w->dc, w->rc);
    }
}

static void cursor_capture(void) {
    if (gCursorHidden) {
        return;
    }
    gCursorHidden = true;

    while (ShowCursor(FALSE) >= 0) {
    }
}

static void cursor_release(void) {
    if (!gCursorHidden) {
        return;
    }
    gCursorHidden = false;

    while (ShowCursor(TRUE) < 0) {
    }
}

static double logical_per_pixel(tPanelWin * w) {
    RECT   r     = {0};
    double width = (w->panel->canvasWidth > 0.0) ? w->panel->canvasWidth : 1.0;

    GetClientRect(w->hwnd, &r);
    return (r.right > 0) ? (width / (double)r.right) : 1.0;
}

static void sync(tPanelWin * w) {
    if (w->panel->sync != NULL) {
        w->panel->sync(w->user);
    }
}

static void draw_now(tPanelWin * w) {
    RECT r = {0};

    GetClientRect(w->hwnd, &r);

    if ((r.right < 1) || (r.bottom < 1) || (w->panel->frame == NULL)) {
        return;
    }
    make_current(w);
    sync(w);
    w->panel->frame(w->user, (int)r.right, (int)r.bottom);
    SwapBuffers(w->dc);
}

static void redraw(tPanelWin * w) {
    InvalidateRect(w->hwnd, NULL, FALSE);
}

static bool paused(tPanelWin * w) {
    return (w->panel->paused != NULL) && w->panel->paused(w->user);
}

// The Mac view's 30 Hz timer, stood down while the panel asks for no repainting
static void update_timer(tPanelWin * w) {
    bool wanted = !paused(w);

    if (wanted && !w->ticking) {
        w->ticking = true;
        SetTimer(w->hwnd, TIMER_TICK, TICK_MS, NULL);
        redraw(w);
    } else if (!wanted && w->ticking) {
        w->ticking = false;
        KillTimer(w->hwnd, TIMER_TICK);
    }
}

static void push_modifiers(void) {
    uint32_t bits = (uint32_t)eModifierNone;

    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        bits |= (uint32_t)eModifierShift;
    }

    if (((GetKeyState(VK_LWIN) & 0x8000) != 0) || ((GetKeyState(VK_RWIN) & 0x8000) != 0)) {
        bits |= (uint32_t)eModifierCmd;
    }

    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        bits |= (uint32_t)eModifierAlt;
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        bits |= (uint32_t)eModifierCtrl;
    }
    set_modifier_state(bits);
}

static int glfw_key_for_vk(WPARAM vk) {
    switch (vk) {
        case VK_ESCAPE: return 256;
        case VK_RETURN: return 257;
        case VK_TAB:    return 258;
        case VK_BACK:   return 259;
        case VK_DELETE: return 261;
        case VK_RIGHT:  return 262;
        case VK_LEFT:   return 263;
        case VK_DOWN:   return 264;
        case VK_UP:     return 265;
        case VK_PRIOR:  return 266;
        case VK_NEXT:   return 267;
        case VK_HOME:   return 268;
        case VK_END:    return 269;
        default:        return KEY_UNKNOWN;
    }
}

static void track_leave(tPanelWin * w) {
    if (w->tracking) {
        return;
    }
    TRACKMOUSEEVENT tme = {0};

    tme.cbSize    = sizeof(tme);
    tme.dwFlags   = TME_LEAVE;
    tme.hwndTrack = w->hwnd;
    w->tracking   = (TrackMouseEvent(&tme) == TRUE);
}

static void end_drag(tPanelWin * w) {
    w->following = false;

    if (w->captured) {
        POINT p = w->anchor;

        w->captured = false;
        ClientToScreen(w->hwnd, &p);
        SetCursorPos(p.x, p.y);
        cursor_release();
    }
}

static void press(tPanelWin * w, int px, int py) {
    double k = logical_per_pixel(w);
    double x = px * k;
    double y = py * k;

    sync(w);

    if (w->panel->pointer != NULL) {
        w->panel->pointer(x, y);
    }

    if (w->panel->press != NULL) {
        bool grabbed = w->panel->press(w->user, x, y);
        bool keeps   = grabbed && (w->panel->keepsPointer != NULL) && (w->panel->dragTo != NULL) && w->panel->keepsPointer(w->user);

        w->following = keeps;
        w->captured  = grabbed && !keeps;

        if (w->captured) {
            w->anchor.x = px;
            w->anchor.y = py;
            cursor_capture();
        }
    } else if (w->panel->click != NULL) {
        (void)w->panel->click(w->user, x, y);
    }
}

static void move(tPanelWin * w, int px, int py, bool buttonDown) {
    double k = logical_per_pixel(w);

    if (!buttonDown) {
        if (w->panel->pointer != NULL) {
            w->panel->pointer(px * k, py * k);

            if ((w->panel->menuActive != NULL) && w->panel->menuActive()) {
                redraw(w);
            }
        }
        return;
    }

    if (w->panel->press == NULL) {
        press(w, px, py);    // a drag is a stream of clicks, as on the Mac
        redraw(w);
        return;
    }

    if (w->following) {
        sync(w);
        w->panel->dragTo(w->user, px * k, py * k);
        redraw(w);
    } else if (w->captured && (w->panel->dragBy != NULL)) {
        int dx = px - w->anchor.x;
        int dy = py - w->anchor.y;

        if ((dx == 0) && (dy == 0)) {
            return;    // the warp back below arrives as a move of its own
        }
        sync(w);
        w->panel->dragBy(w->user, dx * k, dy * k);

        POINT p = w->anchor;

        ClientToScreen(w->hwnd, &p);
        SetCursorPos(p.x, p.y);
        redraw(w);
    }
}

static void release(tPanelWin * w, int px, int py) {
    double k = logical_per_pixel(w);

    if (w->panel->press == NULL) {
        return;
    }
    bool wasCaptured = w->captured;

    end_drag(w);

    if (w->panel->release != NULL) {
        sync(w);
        w->panel->release(w->user, wasCaptured ? w->anchor.x * k : px * k, wasCaptured ? w->anchor.y * k : py * k);
    }
}

static void destroy(tPanelWin * w) {
    KillTimer(w->hwnd, TIMER_TICK);

    if (GetCapture() == w->hwnd) {
        ReleaseCapture();
    }
    end_drag(w);    // a host closing the editor mid-drag must not leave it without a pointer
    SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, 0);
    gfx_detach_window((void *)w->hwnd);

    if (wglGetCurrentContext() == w->rc) {
        wglMakeCurrent(NULL, NULL);
    }
    wglDeleteContext(w->rc);    // its textures live on in the root context it shares with
    ReleaseDC(w->hwnd, w->dc);
    free(w);
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    tPanelWin * w = (tPanelWin *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (w == NULL) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;

            BeginPaint(hwnd, &ps);
            draw_now(w);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
            redraw(w);
            return 0;

        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;

        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;

        case WM_TIMER:
            if (IsWindowVisible(hwnd)) {
                redraw(w);
            }
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (w->panel->key != NULL) {
                SetFocus(hwnd);    // the keyboard is claimed by a click, so a host keeps its keys until then
            }
            SetCapture(hwnd);
            push_modifiers();
            press(w, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            redraw(w);
            update_timer(w);    // a click may have paused or resumed the panel
            return 0;

        case WM_MOUSEMOVE:
            track_leave(w);
            push_modifiers();
            move(w, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (wParam & MK_LBUTTON) != 0);
            return 0;

        case WM_LBUTTONUP:
            ReleaseCapture();
            push_modifiers();
            release(w, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            redraw(w);
            return 0;

        case WM_CAPTURECHANGED:
            if (w->captured || w->following) {
                end_drag(w);    // notes §3: a release that never came
                redraw(w);
            }
            return 0;

        case WM_MOUSELEAVE:
            w->tracking = false;

            if (!w->captured && !w->following && (w->panel->pointer != NULL)) {
                w->panel->pointer(-1.0e6, -1.0e6);
                redraw(w);
            }
            return 0;

        case WM_MOUSEWHEEL: {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};    // screen coordinates

            if (w->panel->scroll == NULL) {
                break;
            }
            ScreenToClient(hwnd, &p);

            double k = logical_per_pixel(w);

            sync(w);
            // one notch is one line, downward positive, as a Mac wheel's
            w->panel->scroll(w->user, p.x * k, p.y * k, -(double)GET_WHEEL_DELTA_WPARAM(wParam) / (double)WHEEL_DELTA);
            redraw(w);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int key = glfw_key_for_vk(wParam);

            push_modifiers();

            if ((w->panel->key != NULL) && (key != KEY_UNKNOWN)) {
                sync(w);

                if (w->panel->key(w->user, key, 0u, (lParam & (1L << 30)) != 0)) {
                    redraw(w);
                    return 0;
                }
            }
            break;    // a character, if it is one, follows as WM_CHAR
        }

        case WM_CHAR: {
            unsigned int c = (unsigned int)wParam;

            if ((c < 0x20u) || (c == 0x7Fu) || (w->panel->key == NULL)) {
                break;
            }
            sync(w);

            if (w->panel->key(w->user, KEY_UNKNOWN, c, (lParam & (1L << 30)) != 0)) {
                redraw(w);
                return 0;
            }
            break;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP:
            push_modifiers();
            break;

        case WM_DESTROY:
            destroy(w);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// An HWND as a void *. The VST3 wrapper parents it into the host's window and destroys it on removed()
// (notes §4); WM_DESTROY gives back the context and the backend's slot.
void * synthlib_panel_view_create(const tSynthLibPanel * panel, void * user, double width, double height) {
    if ((panel == NULL) || !register_class() || !ensure_root_context()) {
        return NULL;
    }
    tPanelWin * w = (tPanelWin *)calloc(1, sizeof(tPanelWin));

    if (w == NULL) {
        return NULL;
    }
    w->panel = panel;
    w->user  = user;
    w->hwnd  = CreateWindowExW(0, gClassName, L"", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                               0, 0, (int)width, (int)height, NULL, NULL, gModule, NULL);

    if (w->hwnd == NULL) {
        free(w);
        return NULL;
    }
    w->dc = GetDC(w->hwnd);

    if (!set_pixel_format(w->dc) || ((w->rc = wglCreateContext(w->dc)) == NULL) || (wglShareLists(gRootRc, w->rc) == FALSE)) {
        if (w->rc != NULL) {
            wglDeleteContext(w->rc);
        }
        ReleaseDC(w->hwnd, w->dc);
        DestroyWindow(w->hwnd);
        free(w);
        return NULL;
    }
    make_current(w);
    gfx_backend_choose(eRenderBackendOpenGL);
    gfx_attach_window((void *)w->hwnd);

    if (panel->init != NULL) {
        panel->init();
    }
    SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, (LONG_PTR)w);
    update_timer(w);
    return (void *)w->hwnd;
}

#endif // _WIN32
