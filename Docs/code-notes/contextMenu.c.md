# contextMenu.c notes

The longer comments from `contextMenu.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `menu_edge_zone()`

── Core mechanism ───────────────────────────────────────────────────────────

gContextMenu.frame[0..depth-1] is the stack of currently visible levels —
frame[0] is the original top-level menu, frame[depth-1] the deepest open
flyout. Every level stays visible and clickable while a deeper one is open.

## 2. `menu_total_rows()`

HOW MANY ROWS THIS FRAME HAS, and how many of them fit below where it opens.

A menu that will not fit used to be MOVED up until it did, and once it was taller than the window
that failed silently: it landed at the top and the surplus ran off the bottom, drawn nowhere and
clickable never. That is fine while every list is short and becomes a correctness bug the moment
one is not - a device list is as long as the machine says it is.

## 3. `push_menu_frame()`

Deliberately leaves gContextMenu.hoverFrame/hoverIndex/hoverStartTime alone:
the mouse is still physically sitting over whichever item just triggered
this push (that's true whether the trigger was a click or a hover-dwell), so
clearing them here would make the very next update_context_menu_hover() tick
see that item as "newly hovered" and immediately collapse the frame just
pushed. Callers that push from somewhere other than the current hover
target (none today) are responsible for updating hover state themselves.

## 4. in `handle_context_menu_click()`

A CLICK IN A SCROLL STRIP SCROLLS RATHER THAN CHOOSING, which is what a menu does
everywhere else and is also the only way the strips are usable with a trackpad: a tap
reports a position and no motion, so without this the tap would pick whatever item happens
to sit under the strip. A page at a time, less one row of overlap so nothing is stepped
over between pages.

## 5. `MENU_SCROLL_ROWS_PER_SEC`

Called once per frame while gContextMenu.active (see the embedding app's
main render loop) — tracks which item the mouse is over and, if it has a
subMenu and the mouse dwells on it for MENU_HOVER_DELAY_SECS, opens it
exactly as a click would. Hovering a different item at a still-visible
ancestor level collapses whatever flyout was open beneath it, same as real
menus.
HOW A MENU TOO LONG TO FIT IS SCROLLED.

Hovering the top or bottom edge of a scrolling frame scrolls it, continuously, while the pointer
stays there. That is what a macOS menu does when it outgrows the screen, and it is the right shape
for this code for a second reason: it needs nothing but the pointer position, which
update_context_menu_hover() is already given every frame. A dragged scrollbar - SynthLib's own
idiom for the file and bank browsers - would need mouse-up delivered to the menu, and no app
routes that here today.

The strip is one cell tall, so it is exactly as big as the thing it scrolls by, and it is only
live on a frame that actually scrolls: a menu that fits has no edge behaviour at all.

## 6. `render_menu_frame()`

── Rendering ────────────────────────────────────────────────────────────────

Renders every currently open level (gContextMenu.frame[0..depth-1]) —
ancestors are drawn first so the deepest, frontmost flyout paints on top.

## 7. in `render_menu_frame()`

ONE GEOMETRY FUNCTION. Both passes below used to recompute the cell rectangle inline,
which was a second and a third copy of menu_item_rect() - and the moment a frame could
scroll, three copies would have had to learn about it together or the menu would draw in
one place and be clickable in another.

## 8. `render_menu_scroll_strips()`

THE STANDARD AFFORDANCE FOR A MENU THAT DOES NOT FIT: a strip at the edge with a chevron in it,
exactly where a macOS menu puts its scroll arrow, drawn OVER the first or last visible row.

Overdrawing a row is not a loss, because a strip is only live while there is more in that
direction - reach the end of the list and the bottom strip goes inactive, the row beneath it stops
being swallowed, and the last item is clickable again. So every item is still reachable, which is
the whole reason the scrolling exists.
