# synthlibPersistence.h notes

The longer comments from `synthlibPersistence.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `synthlib_save_dial_mode()`

Window position/size + dial-mode persistence — identical prefs.h keys ("windowX", "windowY",
"windowWidth", "dialMode") and identical restore logic previously copy-pasted across G2-Edit's,
EmuUtility's, and SynthEdit's own persistence.c. Each app also has its own extra settings (zoom
factor, last-browsed folder, device config, etc.) that stay local — this only covers the shared
core, operating on synthlib_window()/synthlib_dial_mode() (synthlibGlobals.h).

## 2. `synthlib_load_window_and_dial_mode()`

Call once at startup, after prefs_init() (prefs.h) and after init_graphics() has created the
real window (synthlib_window() must already be non-NULL) — same "needs the window to already
exist" requirement each app's own resize_window()/reposition_window() always had.
targetFrameBuffWidth/targetFrameBuffHeight are the app's own TARGET_FRAME_BUFF_WIDTH/HEIGHT
constants, needed to restore the saved width at the correct aspect ratio.
