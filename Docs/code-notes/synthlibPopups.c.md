# synthlibPopups.c notes

The longer comments from `synthlibPopups.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `file_browser_mouse()`

── Adapters for SynthLib's own popups ───────────────────────────────────────

Each of these is the quirk that used to be copy-pasted into every host, written once. They are
small on purpose: the value is not the code, it is that there is now exactly one copy of each.

## 2. `file_browser_mouse()`

MODAL MEANS SWALLOW THE PRESS TOO, not just the click. The browsers only need the mouse-UP to do
their work, and every host originally passed the mouse-DOWN straight through to whatever was
underneath — so the down-half of a click on a browser's Cancel button also pressed a module
control on the canvas behind it, started a rubber-band selection, or grabbed a scrollbar.

## 3. `alert_dialog_mouse()`

THE ALERT DIALOG HAS TO ROUTE AROUND ITS OWN DROPDOWN. show_bank_confirm()'s bank picker is opened
through the shared context-menu system, so once that flyout is up the clicks belong to it and not
to the dialog panel underneath — exactly as a menu bar defers to the menu it opened. Swallowing
them here as if they had landed on the dialog is what makes the picker unusable.

## 4. `context_menu_mouse()`

THE MENU IS DRAWN OVER EVERYTHING ON THE CANVAS, SO IT IS ASKED FIRST — which it was not, and that
was a real bug: a right-click menu overlapping the horizontal scrollbar, the vertical scrollbar or
the split drag bar passed its clicks THROUGH to them, so choosing "add module" also scrolled or
dragged the pane, and an item that happened to sit over a scrollbar could not be chosen at all.
The host tested those in a fixed sequence of ifs that ran before its context-menu call, so paint
order and hit-test order disagreed.

SWALLOWING THE PRESS IS THE HALF THAT FIXES IT. The menu itself only acts on the release, but it is
the PRESS that a scrollbar or a drag bar latches onto; letting that through is what made the thing
underneath start moving.

A click OUTSIDE the menu is deliberately not consumed: dismissing an open menu by clicking away
from it is the host's own business, and it needs to see that click to decide what else it means.

## 5. `menu_bar_mouse()`

The bar opens its menu on the PRESS, which is why only the press is offered to it: the matching
release is what the context menu (the dropdown IS a context menu) acts on, and consuming that here
would stop a menu item ever being chosen.

Returning false when the coordinate misses the bar is what lets the host see the click — the bar
is not modal and a press beside it means whatever the canvas says it means.

## 6. `gLibPopups`

SynthLib's own five, in one table so that the order is stated rather than implied by the sequence
of calls in somebody else's render function.
THE MENU BAR'S CLICKS ARRIVED HERE ON 2026-08-20, and what had to happen first is the point. The
BAR sits behind the floating panels in every host's input pipeline — a panel may overlap the top
bar, so a click where a panel covers the bar belongs to the panel — and while the panels were
dispatched by the host AFTER this coordinator ran, moving the bar in here would have silently put
it in front of them. The fix was not to special-case the bar but to give the panels a layer of
their own in this same ordering (the host registers them; see G2-Edit's floatingPanels entry).
Once the panels are ranked rather than sequenced, the bar's own layer — the lowest there is —
says exactly what the old call order said, and says it where it can be checked.

Its RENDER stays the host's: the bar is chrome the panels float above, so it must be drawn before
them, which is a different position in the sequence from the one its clicks want. That is not an
inconsistency to fix — it is the one popup that is genuinely behind what it is in front of.

THE CONTEXT MENU IS DIFFERENT, and was moved here on 2026-08-20 for a reason worth stating: it is
drawn AFTER the floating panels and after all the canvas chrome, so it genuinely is in front of
them, and being hit-tested last was a bug rather than a caution. See context_menu_mouse().

## 7. `warn_on_lib_layer_collisions_once()`

THE LIBRARY'S OWN FIVE, CHECKED AGAINST EACH OTHER. Deliberately not part of the function above,
and deliberately not called from registration: an application need not register any popups at all
— SynthEdit and EmuUtility both use the library's and register none of their own — so a check that
ran only from synthlib_popups_register() would never run for either of them, which is precisely
the half of the check they depend on.

Once per process. The table is static, so the answer cannot change between calls, and collect() is
on the render path where repeating it every frame would be pure waste.

## 8. `warn_on_layer_collisions()`

A SHARED LAYER IS NOT AN ERROR, BUT IT IS ALWAYS WORTH KNOWING ABOUT. The sort is stable, so two
popups on one layer keep their registration order — and that order runs the library's table first
and the application's second, which means an app popup that collides with a library one sorts
BEHIND it every time, whatever the host intended. Silent, and invisible until the day the two
overlap on screen.

The whole premise of this file is that the ordering is data and can therefore be checked, so it is
checked. Logged rather than refused: the collision may well be deliberate (the two browsers are
never up together, and G2-Edit's progress panels are mutually exclusive), and a library that
declines to show a panel because of a number would be a far worse failure than a line in the log.
