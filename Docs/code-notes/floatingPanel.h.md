# floatingPanel.h notes

The longer comments from `floatingPanel.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tFloatingPanel`

A panel that sits ON the canvas rather than over it: movable, non-modal, and able to share the
screen with other panels and with the patch underneath.

The Virtual Keyboard and the Patch Adjuster were both written as MODAL dialogues — each computed
(renderW - boxW) / 2 every frame, so it re-centred itself continuously and could not be moved, and
each drew draw_dialog_background_overlay() over the whole window and returned true from its mouse
handler for every click anywhere. Two of those cannot be open at once in any useful sense. This
holds the small amount of state that turns such a panel into a floating one, so the behaviour is
written once instead of once per panel.

The Patch Mutator already floats, by hand, with its own copy of these four fields. It is left
alone deliberately — it works, and porting it buys nothing but risk — but it is the model this
follows, and it could be migrated later.

## 2. file scope

Stacking order: higher is nearer the front. Panels overlap, so the one drawn on top must also
be the one that gets the click — without this the hit-test order is whatever order the
handlers happen to be called in, and a panel underneath silently swallows presses aimed at the
panel above it.

## 3. `floating_panel_set_bounds()`

The area panels are kept inside. Defaults to the whole render area; an app with chrome down an
edge sets the inner rect instead, and G2-Edit does: its canvas scrollbars sit along the bottom and
the right, and a panel dragged over them looked wrong — overlapping the TOP bar is fine, since a
panel has to start somewhere and the bar is not something you scroll.

Set it per frame if it moves with the window; it is one assignment, and a stale bound is worse
than a recomputed one — the clamp runs every frame precisely so a window resize cannot strand a
panel outside it.

A panel LARGER than the bounds is pinned to the top-left and allowed to overflow, rather than
being refused a position it cannot have: at G2-Edit's 640x360 minimum window, Synth Settings is
taller than the whole canvas.

## 4. `tFloatingPanelEntry`

One floating panel, paired with how to draw it and how to offer it a click. Registering them in an
array and sorting means the draw order and the hit-test order come from ONE place. Hand-written
two-way branches worked while there were two panels and become combinatorial at three, which is
exactly the sort of duplication that lets the two orders drift apart — and if they drift, a panel
is drawn on top of one that is taking its clicks.

## 5. in `floating_panel_in_front_of()`

IS THE PANEL UP? Points at the application's own visibility flag; NULL means "always shown".

This is the column the table was missing, and its absence had already cost a bug. Every
OTHER user of a panel could ask privately — each render function and each mouse handler opens
by testing its own flag and doing nothing if it is clear — so the table never needed to know.
The HOVER path cannot: its question is "is the pointer over any panel at all", asked before
any particular panel is in hand, and rect alone cannot answer it because a panel keeps its
rectangle after it is closed. G2-Edit answered it by naming ONE panel in an if, so hovering
over any of the other six ran the canvas hover underneath and hid cables that the panel was
covering anyway.

## 6. `eFloatingPanelHit`

What a press or release on a floating panel turned out to be.

Every panel handler opens with the same three-way question — is this a move, is it mine at all, or
is it a click on my content? — and every one of them used to answer it by hand, in slightly
different words. Getting it wrong has two failure modes, both of which have been seen here: claim
too much and the panel is modal again (the canvas and every other panel go dead while it is open);
claim too little and a drag started on the title bar is dropped the moment the pointer leaves it.

## 7. `floating_panel_mouse()`

The shared preamble. `stillOurs` is for a panel with something outstanding that a release must
reach even when the pointer has left the panel — the Virtual Keyboard's sounding note is the case
that proves it: press a key, slide off the panel, release, and without this the note hangs. Pass
false when the panel has no such state.
