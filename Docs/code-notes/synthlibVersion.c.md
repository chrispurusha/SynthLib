# synthlibVersion.c notes

The longer comments from `synthlibVersion.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. in `synthlib_about_text()`

__DATE__ and __TIME__ are the moment THIS FILE was compiled, not the moment the build
finished. That is exact for do-vst3 and do-release, which compile everything from scratch
every run; an incremental Xcode build can leave it behind if nothing here changed. Hence
"Compiled", which is true in both cases, rather than "Built", which would not be.
