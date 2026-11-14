# buttonPress.h notes

The code points at each as `// notes §k`.

## 1. A press arms, the release acts

A panel button (Prev, Next, Load..., a tab, a toggle) acts on the RELEASE, not the press, and only when the release
lands on the button the press began on, so a press can be cancelled by moving off before letting go - the way a
desktop button behaves. Asked for across every AllAlike panel by the owner, 2026-11-14.

A panel's press handler calls `synthlib_button_press(id)` for the button under the pointer and does nothing else;
its draw code asks `synthlib_button_held(id)` to show the button in its held colour; its release handler takes
`synthlib_button_release()` (the button that was held, now let go) and acts on each button for which
`synthlib_button_clicked(held, id, bounds, at)` is true. The id is any address that names the button - its
rectangle's, in practice (`&gPrevRect`, `&gTabs[p]`).

There is one held button per process, not per instance: there is one pointer, so a press and its release always
belong to the same editor. Everything runs on the thread that draws.

`synthlib_button_cancel()` lets go without acting - for a popup opened over a held button.

Not for what a press must start at once: a dial grab, a menu that opens on the press and picks on the release, an
audition or a pad that sounds while held.

## 2. `synthlib_button_held_colour()`

Halfway from the button's own colour to white, so every button - the grey ones and the coloured toggles alike -
lights while held, and still reads as itself.
