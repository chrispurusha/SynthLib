# synthlibGlobals.c notes

The longer comments from `synthlibGlobals.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

eDialModeVertical matches EmuUtility's and SynthEdit's own previous default — G2-Edit's default
was eDialModeRotary instead, so it calls synthlib_set_dial_mode() explicitly at the top of its
own init_graphics(), before load_saved_settings() can overwrite it from a real saved value
anyway (see that call site's own comment).
