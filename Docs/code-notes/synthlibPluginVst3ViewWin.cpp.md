# synthlibPluginVst3ViewWin.cpp - notes

## 1. What it is

The Windows IPlugView, the HWND counterpart to `synthlibPluginVst3View.mm` and linked in its place by a
Windows build (G2-Edit's `tools/do-windows-plugin`, 2026-10-04). The same size logic (default and saved
width, aspect, clamping, checkSizeConstraint) and the same `synthlib_plugin_request_resize()`. Sizes are
pixels on Windows - the VST3 contract says so for kPlatformTypeHWND - and content scaling is not
implemented yet.

## 2. `attached()`

The plug-in contract's `createView()` takes no parent, so the plug-in makes its window as a hidden popup
and the wrapper turns it into a WS_CHILD of the host's HWND (SetParent, then SetWindowPos with
SWP_FRAMECHANGED and SWP_SHOWWINDOW). `removed()` hides and unparents it, then `destroyView()`, which on
Windows destroys the window as well - the wrapper holds no reference of its own, unlike ARC's NSView.
