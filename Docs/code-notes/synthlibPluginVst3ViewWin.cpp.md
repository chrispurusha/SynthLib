# synthlibPluginVst3ViewWin.cpp - notes

## 1. What it is

The Windows IPlugView, the HWND counterpart to `synthlibPluginVst3View.mm` and linked in its place by a
Windows build (G2-Edit's `tools/do-windows-plugin`, 2026-10-04). The same size logic (default and saved
width, aspect, clamping, checkSizeConstraint) and the same `synthlib_plugin_request_resize()`. Sizes are
pixels on Windows - the VST3 contract says so for kPlatformTypeHWND - and are converted from the
descriptor's logical units by the content scale (§3).

## 2. `attached()`

The plug-in contract's `createView()` takes no parent, so the plug-in makes its window as a hidden popup
and the wrapper turns it into a WS_CHILD of the host's HWND (SetParent, then SetWindowPos with
SWP_FRAMECHANGED and SWP_SHOWWINDOW). `removed()` hides and unparents it, then `destroyView()`, which on
Windows destroys the window as well - the wrapper holds no reference of its own, unlike ARC's NSView.

## 3. Content scale (2026-11-04)

The descriptor's editor sizes (default, minimum, maximum, the saved width) are logical units, as on the
Mac, where AppKit turns points into pixels. Windows has no such layer for a child HWND, so until this the
logical size was handed over as pixels: on a 200% display (the Parallels VM, 192 dpi) the editor was half
size in a DPI-aware host, and in one Windows scales as a bitmap it was stretched - blocky, on top of the
missing multisampling G2Alike also had (g2ViewWin notes §8).

The view now keeps its size logical (`currentWidth/Height`) and a `scale`, pixels per logical unit, and
converts at every boundary with the host: `getSize()`, `onSize()`, `checkSizeConstraint()`, the window it
positions in `attached()`, and `synthlib_vst3_request_resize()`. The scale is what the host gives through
`IPlugViewContentScaleSupport::setContentScaleFactor()` - after which the window is resized and the host
asked, through `IPlugFrame::resizeView()`, for the new pixel size - and until then the system DPI over 96.
`GetDpiForSystem()` answers as the calling process sees it, which is 96 in a DPI-unaware host, so that
default is right in both kinds of host. Neither panel needs to know: `g2ViewWin.c` and `synthlibPanelViewWin.c`
both draw and hit-test from the window's pixel width.
