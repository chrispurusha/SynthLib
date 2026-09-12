# clickRegion.c notes

The longer comments from `clickRegion.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

In-flight press capture — see eClickPhase's comment in clickRegion.h. Deliberately a COPY of the
region rather than an index into sRegions: regions are rebuilt from scratch every frame
(clear_click_regions), so an index would dangle or silently point at a different widget by the
time the release arrives. The copy also keeps the capture valid if the widget stops registering
mid-gesture (scrolled away, page switched), which is exactly when the release still has to be
delivered so the handler can unwind whatever the press started.

## 2. in `dispatch_click_region()`

A press that hit nothing leaves no capture, so the matching release falls through to the
coordinate lookup above exactly as it always did — apps that route releases here for widgets
whose press was consumed elsewhere (before dispatch_click_region was ever reached) keep their
existing behaviour.
