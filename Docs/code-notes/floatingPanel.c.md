# floatingPanel.c notes

The longer comments from `floatingPanel.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `PANEL_CASCADE_STEP`

Each newly placed panel is offset from the last so a second one does not land exactly on top of
the first. Centring them all — which is what the modal versions did — is the one placement that
guarantees they hide each other, and the complaint that started this work was precisely that these
panels take the whole screen.

## 2. file scope

Monotonic, so "most recently raised" is simply the largest. Never reset: at one raise per click it
would take longer than any session to wrap a uint32_t.

A panel is raised when it is OPENED, not when it is first placed. Placement happens inside the
render pass, by which point that frame has already been sorted using the old order — so a newly
opened panel was drawn BEHIND the others for that frame, and since the app only redraws on demand,
that wrong stacking then stayed on screen until something else asked for a frame.

## 3. in `floating_panel_place()`

KEPT WHOLLY INSIDE THE BOUNDS, and re-clamped every frame rather than only on placement — a
window resize can otherwise strand a panel outside them, and a panel parked off-screen cannot
be dragged back. Because this runs after the drag has moved the panel, it is also what stops a
drag at the edge: the panel simply will not go further.

This used to clamp only enough to keep the TITLE BAR reachable, letting the rest hang off the
bottom and right. That was fine against a bare window edge and wrong against chrome — a panel
lying over the canvas scrollbars reads as a mistake rather than as a panel in front.
