# synthlibPanelView.m notes

The longer comments from `synthlibPanelView.m`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

See synthlibPanelView.h. This was GenBridge's gbView.m, which MidiSyncTool's msView.m had been
copied from; the notes below are the ones both carried, and each records something measured or
something that went wrong in a real host.

METAL, so a PLAIN NSView. There is no context for the view to own - it is layer-hosting and the
CAMetalLayer is the surface - which is why there is no NSOpenGLView and no -prepareOpenGL.

## 2. file scope

LAYER-HOSTING, AND THE ORDER MATTERS: gfx_attach_window() assigns the layer and only then is
wantsLayer set, which is what tells AppKit the contents belong to the layer and that it must
not draw over them. It is handed the VIEW rather than a window - in a plug-in the window
belongs to the host, and we may never see it.

## 3. file scope

Whether a repaint would be seen by anyone. THE EDITOR BEING CLOSED IS NOT THE ONLY WAY TO STOP
SHOWING IT: the host calls removed() for that and the timer goes with the view, but a window that
is minimised, completely covered by another, or on an inactive Space is just as invisible and the
view is still in the hierarchy. So is one in a host that HIDES its plug-in view rather than
removing it, which some do when switching between panels in a rack. In every one of those cases
this used to go on drawing thirty full Metal frames a second - a frame has no dirty check - into a
surface nobody was looking at.

## 4. file scope

The timer exists exactly while it is worth having. Starting one is cheap, so this is driven from
the notifications rather than by letting a tick fire and return early: a tick that returns early
still wakes the process thirty times a second, which is most of what there was to save on a
machine that has gone to sleep with a project open.

## 5. file scope

A timer rather than a CVDisplayLink. The panels show meters and live telemetry, so they
repaint continuously rather than on demand, but nothing here is worth a display link's
complications - and a link fires on its own thread, which would mean marshalling every
frame back to the main one before touching AppKit.

## 6. file scope

IN STEP WITH THE RESIZE. The 30 Hz timer is fine for meters and hopeless for a drag: the host moves
the frame at display rate, so the content arrived up to a thirtieth of a second behind the window
edge and visibly lagged it. The backend reallocates its render targets from the new size on the
next frame, so there is nothing else to tell.

## 7. file scope

NOT WHILE THE USER IS DRAGGING THE EDGE. -setFrameSize: is already redrawing, at least as often
as the timer would and usually more, so a tick here draws a second frame nobody asked for - and
that is not merely wasted, it is actively what made a resize judder.

MEASURED: a redraw takes a drawable from the layer and presents it, and CAMetalLayer keeps a
small pool. Present more often than the display refreshes and -nextDrawable blocks until one
comes free - up to a full refresh, 15.4 ms of the 17 ms a resize step was costing. That block
happens INSIDE AppKit's drag loop, so the window itself stops moving while we wait for a frame
the user was never going to see. Reallocating the render targets measured 0.00 ms.

## 8. file scope

SELECT THIS VIEW'S CONTEXT FIRST. With two editors open, whichever drew last left the backend
pointing at its own layer; drawing without claiming ours would paint into the other one's
window. Attaching an already-known view is a pointer assignment, so this is cheap enough to do
every frame and removes any need to track whose turn it is.

## 9. file scope

ONE FRAME, ONE TRANSACTION. The frame moves the layer's geometry and gfx_present() hands over
the pixels for it; as two separate Core Animation transactions the layer had its NEW size and
its OLD contents, stretched to fit, for one commit - seen as a jump on every step of a resize.
The backend presents with presentsWithTransaction set for a hosted view, which is what makes
putting them in the same transaction meaningful.

## 10. file scope

SYNCED BEFORE THE HIT TEST, not just before the draw. The draw layer's notion of which editor
it is serving is file-scope, and the hit test consults it: GenBridge only offers its Measure
and Offset controls when it believes it is drawing the instrument, and with an effect and an
instrument both open the effect's repaint had already told it otherwise by the time a click
reached the instrument - so those two controls silently did nothing while every other one
worked. The values go with it, for the same reason: a stepper steps from what the layer holds.

## 11. file scope

THE POINTER POSITION, for an open drop-down to highlight under.

NSTrackingInVisibleRect means AppKit maintains the region itself as the view is resized, so this
does not have to be torn down and rebuilt on every geometry change - which matters here, where the
host owns the window and resizing is already the fiddliest part of this view.
