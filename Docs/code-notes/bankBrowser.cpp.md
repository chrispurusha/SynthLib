# bankBrowser.cpp notes

The longer comments from `bankBrowser.cpp`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

One row of the flattened, rebuilt-per-sort-mode display list — mirrors the design of the Cocoa
NSTableView data source this panel replaced (rowLabels/rowKinds/rowBanks/rowLocs arrays),
collapsed into one struct.
itemIndex is -1 for separator/header rows (nothing to select).
Normal rows are stored as separate column fields rather than one concatenated string, so each
column can be drawn at a fixed x and line up vertically down the list. label is used only by
header rows (which span the full width and have no columns).

## 2. `kColBankX`

Column x offsets within a row, measured from the row's left edge, so Bank/Loc/Name/Category line
up vertically down the list. Sized for the widest real content: "Bank 8" and "Loc 128" at
STANDARD_TEXT_HEIGHT. Category is right-anchored (kColCategoryW back from the row's right edge)
so it stays put regardless of how wide the list is, and Name takes whatever is left between them.

## 3. `rebuild_rows()`

Rebuilds the flattened display list from sState.items for the current sState.sortMode — mirrors
the Cocoa data source's rebuildForSortMode: this panel replaced. mode 0 keeps the caller's raw
order (assumed Bank/Loc already) with a separator between banks; mode 1 groups by category with a
header row per group (skipped entirely if the caller supplied no category names) — pinned
categories first, the rest alphabetically; mode 2 is fully alphabetical with no grouping.

## 4. `handle_bank_browser_mouse_down()`

Called on mouse-down while the browser is active so Close/Cancel/Confirm can show a pressed
state while held — matches the rest of the app's convention (gTopbarControls[i].isPressed,
fileBrowser.cpp's sState.closePressed, ...) of darkening a button's fill from mouse-down to
mouse-up rather than only reacting on click.

## 5. `update_bank_browser_hover()`

Called once per frame while bank_browser_active() (see the embedding app's main render loop) —
same "redraw only happens when gReDraw fires, so pure mouse-move never repaints" gap that
update_context_menu_hover() (contextMenu.c) was built to close. Without this, the row highlight
only catches up with the mouse on whatever redraw next happens to fire for an unrelated reason,
which is exactly the "only highlights sometimes" symptom reported against this picker.

## 6. in `render_bank_browser()`

Highlight fills are inset from the list box's own left/right border by BORDER_LINE_WIDTH
— rowRect itself runs edge-to-edge with listRect (matching render_rectangle_with_border()'s
border, which is drawn as a ring just inside listRect's bounds), so painting a highlight at
the full rowRect width would overwrite that border on every highlighted row.

## 7. in `render_bank_browser()`

Drawn as four separate calls rather than one concatenated string, so the columns align
down the list. Name gets whatever space is left between the Loc and Category columns;
Category is dropped from the layout entirely when the device has no categories, letting
Name run the full remaining width instead of leaving a permanent empty gutter.
