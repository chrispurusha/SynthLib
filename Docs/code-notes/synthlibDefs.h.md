# synthlibDefs.h notes

The longer comments from `synthlibDefs.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `LOG_DEBUG`

DISABLED, BUT THE ARGUMENTS STILL COUNT AS USED. `((void)0)` discarded them entirely, so any variable
that existed only to be logged became an unused variable in Release while being perfectly used in
Debug — two of those turned into errors the moment warnings became errors, and only in the
configuration do-release builds. `if (0)` keeps every argument in an expression the compiler must
still check, so the format string and its arguments stay type-checked in both configurations, then
optimises away to nothing.

## 2. `MENU_SUBMENU_CLOSE_DELAY_SECS`

HOW LONG AN OPEN FLYOUT SURVIVES the pointer wandering onto one of its parent's other items.

Reaching a flyout means travelling diagonally, and the direct line from the item that opened it to
the flyout's contents passes straight over the items BELOW that item. Collapsing the moment one of
those is touched forces the user to trace an L — out along their own row first, then down — and
missing by a pixel shuts the menu. Real menus all forgive this: macOS tracks a triangle toward the
flyout, Windows simply waits. Waiting is what fits a per-frame hover tick.

The parent item still highlights immediately; only the collapse waits. Long enough to cross a menu
diagonally, short enough that deliberately moving to a sibling still feels like it responds.

## 3. `MODULE_SCROLLBAR_WIDTH`

The MODULE CANVAS's own scrollbar metrics — deliberately OUTSIDE the per-app #ifdef below.

utilsGraphics.c compiles WITHOUT G2_EDIT defined (see the note in draw_panel_close_button), so
anything inside that branch is invisible to it. That is exactly how the canvas came to reserve
one width for the scrollbars while G2-Edit's split view drew them at another: module_area_for_pane()
read the #else value and the app read the G2_EDIT one. Both sides read these instead.

Matched to the Patch Window Split Bar's height (SPLIT_BAR_HEIGHT in G2-Edit's splitView.h) so the
divider and the bars read as one family of furniture; if one changes, change the other.

## 4. `SCROLLBAR_WIDTH`

Matched to the Patch Window Split Bar's own height (SPLIT_BAR_HEIGHT in splitView.h), so the
divider and the scrollbars read as the same family of furniture rather than three thicknesses.
Kept as a literal because this header cannot see the app's own headers; if one changes, change
both.

## 5. `LIST_SCROLLBAR_WIDTH`

Vertical scrollbar for list-style popups (bankBrowser.cpp, fileBrowser.cpp) — a proportional
track+thumb, distinct from the main canvas's fixed-length pan scrollbar (SCROLLBAR_WIDTH above,
which represents infinite-pan percent rather than a finite row count). Same width in both
project variants, so it lives outside the G2_EDIT split above.

## 6. `LIST_SCROLLBAR_MIN_THUMB`

Floor on the thumb's height so it stays grabbable no matter how long the list is. Without it the
proportional height collapses — a full device-wide patch sweep is ~1000 rows against 10 visible,
which works out under 2pt on a 200pt track, leaving an 8x8 square to hit. The shortest track this
is used on is 200pt (bankBrowser: 10 rows), so 24pt costs ~12% of the drag travel at worst.
