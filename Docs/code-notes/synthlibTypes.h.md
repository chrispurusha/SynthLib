# synthlibTypes.h notes

The longer comments from `synthlibTypes.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tMenuItem`

── Context menu (see contextMenu.h) ────────────────────────────────────────

Deliberately app-agnostic: an item knows only its label, colour, an
action(index) callback, an opaque param the callback can read back out, and
optionally a subMenu it opens instead of running that action. Nothing here
knows what a "module" or "param" is — an app that needs to recall what a
menu was raised against (e.g. G2-Edit's moduleKey/paramIndex) keeps that in
its own app-local struct, set before opening the menu and read back from
inside its own action callbacks.

## 2. file scope

CONST, because a menu item's label is only ever READ — rendered, measured, matched. It was
`char *`, which made every caller passing a `const char *` table (a string map, a static const
array of names) discard a qualifier to get it in, and two of those were the last warnings in
G2-Edit's build. Nothing anywhere writes through this field; a label built at runtime is built in
the caller's own buffer and that buffer's address stored here.

## 3. file scope

OPTIONAL CUSTOM FACE. When set, the item's cell is painted by this instead of by its label —
the engine still measures and lays out from the label, so the cell comes out the size the text
would have needed and the item stays keyboard- and search-friendly. Added for G2-Edit's
waveform pickers, which the original hardware editor draws as little pictures of the wave
rather than as words. Deliberately takes only the cell and the item's own `param`: SynthLib
knows nothing about modules, so an app that needs more context stores it app-side before
opening the menu, exactly as the note above this struct describes.

## 4. file scope

SCROLLING, for a list too long to fit the window. visibleRows is the number of rows this
frame may actually show - equal to its total rows whenever everything fits, in which case
nothing about the frame scrolls and scrollRow stays 0. Both are worked out by contextMenu.c
when the frame is pushed and re-checked as the window resizes; an app never sets them.

## 5. `tMouseButton`

Mouse press/release, normalised away from GLFW's button/action pair.

MOVED HERE FROM G2-Edit's types.h (2026-08-20) because the shared floating-panel registry takes it
in its handler signature. It was G2-Edit's alone; EmuUtility and SynthEdit still pass GLFW's raw
ints around, which is exactly why neither of them could adopt a panel until now.
