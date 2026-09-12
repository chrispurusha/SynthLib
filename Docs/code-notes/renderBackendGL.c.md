# renderBackendGL.c notes

The longer comments from `renderBackendGL.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

COMPILED TO NOTHING WHERE OpenGL IS NOT WANTED. On macOS that is now every target - see
renderBackendSelect.h - and the guard is here rather than in a build script because SynthLib/src
is a synchronized folder in the Xcode projects: a file in it is compiled whether the target wants
it or not, so the only place to say "not this one" is inside the file.

NOTHING BELOW HAS BEEN DELETED, and it must not be. This is the whole renderer for Windows and
Linux, where Metal does not exist, and it is the A/B reference on macOS when built with
SYNTHLIB_ALLOW_GL_ON_APPLE.

## 2. file scope

── The OpenGL backend ──────────────────────────────────────────────────────────────────────────

The nine functions of renderBackend.h, and THE ONLY FILE IN SYNTHLIB OR IN ANY OF THE THREE
APPLICATIONS THAT NAMES OPENGL. Everything above it draws by appending triangles.

EVERY CALL BELOW IS OPENGL 1.1 OR EARLIER. That is not nostalgia, it is the reason this file
covers Windows and Linux as well as macOS without a second thought: glDrawArrays, the
client-side vertex array pointers, glTexImage2D, glScissor, glOrtho and glBlendFunc were all
there in 1997, so there is no driver on any of the three platforms that lacks them. macOS is
the only one of the three where OpenGL is deprecated, and it is deprecated rather than gone.

So the eventual arrangement is not four backends. It is this file on Windows and Linux, and
this file OR renderBackendMetal.m on macOS — with this one kept alive there precisely so the
two can be run against each other on one machine and diffed, which is the only cheap way to
prove the Metal port moved no pixel.

## 3. `gSurfaceHeight`

The surface height, remembered because gl_scissor() needs it: GL's scissor origin is the
BOTTOM left where every coordinate handed to this file is top-left. gl_set_surface() is always
called with the same height as set_render_height() — both from synthlibScale.c in the
applications and from g2_gl_draw_frame() in the plug-in — but this file keeps its own rather
than reaching for that global, so the flip cannot silently disagree with the projection.

## 4. in `gl_init()`

The multisample BUFFER is requested by the window layer, because under OpenGL it is a
property of the pixel format the context was created with and cannot be asked for after the
fact — see the GLFW_SAMPLES hint in synthlibWindow.c and the pixel format in
vst3/g2GlView.m. All that is left here is to switch it on. Harmless if no such buffer
exists: GL simply has nothing to multisample.

## 5. in `gl_scissor()`

THE FLIP LIVES HERE, because a bottom-left scissor origin is a property of OpenGL and not of
the UI — Metal's is top-left and needs none. It is exact: the rectangle arrives in whole
pixels, so nothing is truncated and both backends cover the same rows. It did not used to
be, and that was the one thing the Metal port actually got wrong — see renderBackend.h.

GL clamps an out-of-bounds rectangle silently, so there is nothing to do about that here.

## 6. in `gl_read_pixels_rgb()`

Tightly-packed rows (width*3 bytes). Without this, glReadPixels' default GL_PACK_ALIGNMENT
of 4 pads each row up to a 4-byte multiple whenever width*3 isn't already one (i.e. any
width not a multiple of 4) — which both shears the saved PNG (row stride mismatch vs stbi's
width*3) AND overruns the width*height*3 buffer. Only bit us at odd window sizes; Retina
captures were multiples of 4.

## 7. in `gl_present()`

THE PLUG-IN HAS NO GLFW — not just no window, no library. It includes this file for the
drawing and gets its GL headers through glfw3.h, but nothing links libglfw, so naming
glfwSwapBuffers here is an undefined symbol at link time rather than a runtime no-op. It
was, for one build.

Nothing is lost: a plug-in draws into a view the HOST presents, so g2GlDraw.c ends its frame
at render_backend_flush() and g2GlView.m calls [context flushBuffer]. render_present(), and
therefore this, is never reached there.
