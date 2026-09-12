# synthlibPopups.h notes

The longer comments from `synthlibPopups.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

THE POPUP LIFECYCLE COORDINATOR.

SynthLib has owned the popup WIDGETS for a long time — the file and bank browsers, the alert
dialog, the context menu, the menu bar — but not their ORCHESTRATION, so each application
re-implemented the same four things:

```
  * the render order, back to front, as a hand-kept sequence of calls;
  * the modal click cascade: if (file_browser_active()) {...return;} if (bank_browser_active())
    {...} if (alert_dialog_active()) {...} — 13 routing references in G2-Edit's mouseHandle.c and
    11 in SynthEdit's, character-identical between them;
  * the same cascade again for keys;
  * a per-frame hover tick the host has to remember to call for each popup.

```
That last one had already shipped real bugs rather than merely being untidy: SynthEdit's own
comments record hover handlers that were "never actually called anywhere until now", twice. An
order-dependent contract spread across three hosts is fragile by construction — nothing anywhere
states the order, so nothing can check it.

So the order lives HERE, as data, and it is a LAYER on each popup rather than a call sequence.
That is the part that makes it more than a tidy-up: an application registers its own panels into
the same ordering as SynthLib's, so a host's popups and the library's cannot disagree about who is
in front — and the layer that decides drawing is by construction the layer that decides which
popup gets the click.

FLOATING PANELS KEEP THEIR OWN REGISTRY (floatingPanel.h) and should still be registered there:
their order among THEMSELVES is dynamic — they raise on click, so it is a property of the panels
rather than a constant, and no fixed layer could describe it. What belongs here is where that
whole group sits relative to everything else, which IS a constant. A host registers one entry
whose mouse handler walks its own sorted panel list; see G2-Edit's "floatingPanels".

That is what let the menu bar's clicks move in here at all — see the note above gLibPopups in the
.c. Two orderings, each authoritative over a different thing, is the arrangement; two orderings
that both claim the same thing is the bug this file exists to remove.

## 2. `SYNTHLIB_POPUP_LAYER_MENU_BAR`

Layers for SynthLib's own popups, exposed so an application can place its panels BETWEEN them
rather than guessing a number. Higher is nearer the front.

The gaps are deliberate and are the whole point: G2-Edit's patch-notes editor and its progress
panels belong above the context menu and below the browsers, and its device-busy overlay belongs
above the browsers and below the alert. Before this, that was expressed only by the order of five
calls in one function — correct, unstated, and one careless insertion away from being wrong.
