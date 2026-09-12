# renderBackend.c notes

The longer comments from `renderBackend.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

── Which backend, and the one line each call takes to reach it ─────────────────────────────────

The gfx_* functions everything else calls, forwarded to whichever backend was chosen at start-up.
The choice exists so a user can try Metal against OpenGL without a rebuild — see renderBackend.h
for why it cannot change while the app is running.

## 2. file scope

EITHER BACKEND CAN BE LEFT OUT OF A BUILD, and the two exclusions are not the same kind.

Metal is excluded by PLATFORM: there is no Metal off Apple, so renderBackendMetal.m is not
compiled and its table is not declared.

OpenGL is excluded by CHOICE, and only where the target has something better. G2-Edit's PLUG-IN
defines SYNTHLIB_NO_GL_BACKEND: its view is a CAMetalLayer host, so the GL path could never be
selected in it, and carrying an unreachable renderer meant an OpenGL framework on the link line
and a second surface implementation to keep working. Nothing else defines it - the applications
still offer the runtime choice, and Windows and Linux will have OpenGL as their ONLY backend, so
renderBackendGL.c itself stays exactly where it is.
