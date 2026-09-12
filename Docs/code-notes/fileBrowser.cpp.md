# fileBrowser.cpp notes

The longer comments from `fileBrowser.cpp`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

Left-hand quick-access column, built fresh each time the browser opens: a fixed Favorites
group (Home/Desktop/Documents/Downloads/iCloud Drive, whichever exist) plus a Locations group
with the boot volume and every currently-mounted volume under /Volumes — a real equivalent of
NSOpenPanel's sidebar rather than a hardcoded guess at what's mounted.

## 2. `kButtonH`

draw_button() sizes its label text directly off the height of the rectangle passed in (see
utilsGraphics.cpp — there's no separate font-size parameter), so every button here uses
STANDARD_TEXT_HEIGHT for its height, matching the text size every other button in the app
renders at, rather than a larger "easier to click" rectangle. (G2-Edit's own defs.h has a
STANDARD_BUTTON_TEXT_HEIGHT alias for the same 12.0 value, but that header is app-specific and
this file can only see synthlibDefs.h.)

## 3. `button_rect()`

fromRight counts button-widths in from the panel's right edge — 0 is rightmost. x is computed
from the button's right edge inward so it can never extend past the panel (the original version
of this function computed x from the left instead, which put the rightmost button outside the
panel's own border).

## 4. `handle_file_browser_mouse_down()`

Called on mouse-down while the browser is active so Close/Cancel/Confirm can show a pressed
state while held — matches the rest of the app's convention (gTopbarControls[i].isPressed,
tSettingsPanelRects.closePressed, ...) of darkening a button's fill from mouse-down to
mouse-up rather than only reacting on click.

## 5. in `render_file_browser()`

render_rectangle_with_border() fills the whole rectangle with whatever colour is
current when it's called (see utilsGraphics.cpp) — a separate render_rectangle() fill
beforehand would just be overwritten by it, so the focused/unfocused colour has to be
set immediately before this one call, not before a preceding plain fill.
