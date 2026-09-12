# menuBar.h notes

The longer comments from `menuBar.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tMenuBarItem`

Persistent horizontal row of top-level labels (File, Settings, ...), each
opening its own dropdown through the existing context-menu engine
(contextMenu.h) rather than duplicating any menu-rendering here. An item's
open() callback is expected to build its own tMenuItem[] (same
static-array-per-call pattern already used throughout the app's own
menus.c dropdowns) and call open_context_menu(anchor, items, columns,
cellWidth) itself — anchor is the coord this file passes in, already
positioned below the clicked/hovered label.

items is a NULL-label-terminated array, mirroring tMenuItem.

The embedding app must call synthlib_host_init() (synthlibHost.h) once at startup, same as
contextMenu.c requires (see contextMenu.h), and must, once per frame:
```
  - call update_menu_bar_hover() so moving the mouse from one open
    top-level label to another switches the dropdown immediately, the way
    a native menu bar does, without requiring a second click;
  - call render_menu_bar() to draw the bar and whichever label is
    currently highlighted/open.
```
handle_menu_bar_click() should be called from the app's mouse-down
routing, ahead of any other click handling for the region the bar
occupies; it returns true if the click landed inside the bar (whether or
not it hit a specific label), so the caller knows not to also treat it as
a click on whatever the bar is drawn over.
