# prefs.h notes

The longer comments from `prefs.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `prefs_init()`

Small flat-file key/value settings persistence — a cross-platform stand-in for
NSUserDefaults/the Windows Registry/etc, backed by one plain "key=value" text file per app
under a per-OS standard config directory (see prefs.cpp's config_dir() for exact paths, and a
TODO there on the Windows/Linux branches — written to the documented convention for each, but
not build/run-verified since no SynthLib-based app has a Windows or Linux target yet).

prefs_init() must be called once at startup with the app's own name (e.g. "G2-Edit") — each app
embedding SynthLib gets its own settings file, since G2-Edit/Z1-Edit/EmuUtility share this code
but not their preferences. A missing file (first run) just starts with no entries; get_* calls
return the supplied default until something is written.
