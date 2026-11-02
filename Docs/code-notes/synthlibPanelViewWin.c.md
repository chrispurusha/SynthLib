# synthlibPanelViewWin.c

The Windows counterpart to `synthlibPanelView.m`: the same `tSynthLibPanel` contract, drawn into an HWND with
OpenGL through `renderBackendGL.c`. Adapted from G2Alike's `plugin/g2ViewWin.c` (2026-11-02), for the AllAlike
plug-ins' Windows builds (`AllAlike/tools/do-windows-plugin`). Built from G2Alike's working code; not yet run in a
Windows host.

## §1 One window class per DLL

The class name carries the loading module's address, as G2Alike's does. Two SynthLib plug-ins in one host are two
DLLs with two copies of this file; a shared name would let the second register nothing and route its windows to
the first DLL's window procedure.

## §2 The root context

A hidden 1x1 window owns an OpenGL context that every editor's context shares lists with
(`wglShareLists`). The textures the renderer creates once - the glyph atlas - then outlive any one editor, which
a host opening and closing editors otherwise breaks.

## §3 A release that never came

`WM_CAPTURECHANGED` during a drag means the host or Windows took the mouse capture away, so `WM_LBUTTONUP` will
not arrive. The drag is ended there, the pointer shown and put back, rather than left hidden for good.

## §4 Who destroys the window

The plug-ins built on this panel give no `destroyView` callback, as on the Mac, where the wrapper releases the
NSView. `synthlibPluginVst3ViewWin.cpp` therefore calls `DestroyWindow()` itself when there is no `destroyView`,
and `WM_DESTROY` here hands back the GL context and the backend's slot. G2Alike, which has its own
`destroyView`, is unaffected.
