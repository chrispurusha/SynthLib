# synthlibScale.c notes

The longer comments from `synthlibScale.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. in `synthlib_scale_update()`

The framebuffer `width` already reflects the display's backing/content scale
(glfwGetFramebufferSize returns physical pixels). The UI is laid out in a fixed
logical canvas gTargetFrameBuffWidth/2 units wide (the framebuffer width at the
default window size on a 2x display), so the mapping is simply
physical_pixels / logical_units = 2 * width / gTargetFrameBuffWidth.
Do NOT multiply by gContentScale here: the framebuffer size already carries the
scale, so doing so DOUBLE-COUNTS it and made the whole UI collapse to a quarter-
size corner on non-Retina / "Low Resolution" displays (where content scale is
1.0). On a 2x display gContentScale==2.0, so old and new are identical there —
only the 1x case is fixed. gContentScale is retained only as the change-trigger
signal (content_scale_callback -> synthlib_scale_set_content_scale recomputes).
