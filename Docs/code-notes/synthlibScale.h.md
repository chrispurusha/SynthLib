# synthlibScale.h notes

The longer comments from `synthlibScale.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `synthlib_scale_init()`

Owns the HiDPI content-scale recompute that feeds gGlobalGuiScale (geometry.h/.c) — replacing
the near-identical gContentScale/update_framebuffer_state()/framebuffer_size_callback()/
content_scale_callback() quartet that G2-Edit, EmuUtility, and SynthEdit each hand-rolled in
their own graphics.cpp. G2-Edit fixed a real bug here first (issue #9: right-click menus and
anything else deriving screen position from gGlobalGuiScale landed mispositioned after dragging
the window to a display with a different HiDPI scale, because gContentScale was hardcoded
2.0f) — EmuUtility and SynthEdit never got the same fix, so this pushes it down instead of
copy-pasting it twice more.

Usage from the app's own init_graphics(), in this order:
```
  synthlib_scale_init(TARGET_FRAME_BUFF_WIDTH);            // once, before the window exists
  ... glfwCreateWindow() / glfwMakeContextCurrent() ...
  synthlib_scale_query_initial(window);                    // real initial scale, not a guess
  { int fbW, fbH; glfwGetFramebufferSize(window, &fbW, &fbH); synthlib_scale_update(fbW, fbH); }
  glfwSetFramebufferSizeCallback(window, app_framebuffer_size_callback);
  glfwSetWindowContentScaleCallback(window, app_content_scale_callback);

```
The app keeps registering its own real GLFW callbacks (same "SynthLib never touches GLFW's
registration API directly" convention as clickRegion.h/bankBrowser.h) — they just forward into
the two functions below instead of duplicating the recompute logic themselves:
```
  void app_framebuffer_size_callback(GLFWwindow * win, int width, int height) {
      synthlib_scale_update(width, height);
      gReDraw = true; // or synthlib_request_redraw(), see synthlibHost.h
  }
  void app_content_scale_callback(GLFWwindow * win, float xscale, float yscale) {
      (void)yscale; // every embedding app so far only ever uses a single uniform scale factor
      synthlib_scale_set_content_scale(win, xscale);
      gReDraw = true;
  }
```
At window close, unregister the content-scale callback the same way every other GLFW callback
already is: glfwSetWindowContentScaleCallback(window, NULL).

glfwWindow is accepted as void* (not GLFWwindow*) so this header stays GLFW-free, same
reasoning as gWindow being declared void* in every app's own globalVars.h.
