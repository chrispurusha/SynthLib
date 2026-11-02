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

## §5 Multisampling

The Mac's Metal view and the GLFW applications draw with `GFX_MSAA_SAMPLES` (4) samples; without them the
panels' dials and lines are visibly jagged, which is how the first Windows build looked (2026-11-02). A classic
`ChoosePixelFormat()` cannot ask for samples, so the hidden root context is used to fetch
`wglChoosePixelFormatARB`, which can. A window's pixel format can be set only once, so when a multisampled format
exists the root window and context are made again with it, and every editor uses the same format and shares
lists with that root. A driver without the extension, or one that refuses the format, keeps the plain format:
jagged but drawn. `gl_init()` (through `gfx_init()`) enables `GL_MULTISAMPLE` in each editor's context.
