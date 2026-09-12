# inputState.h notes

The longer comments from `inputState.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tModifierBits`

WHICH MODIFIER KEYS ARE HELD, AS PUSHED STATE RATHER THAN SOMETHING TO POLL.

glfwGetKey() is a PULL api: it asks the window system, right now, on this thread. That works for an
application built around GLFW and not at all for a plug-in, which is only ever HANDED events by its
host and has no window to interrogate. Every widget that wanted to know about Shift therefore had
to be given a platform seam of its own, and there were three of those with several copies each.

The seam is now ONE WRITER AND MANY READERS. Each shell translates whatever its own toolkit gives
it — GLFW's `mods` argument, an NSEvent's modifierFlags — into the bits below and pushes them here
once; everything else reads the predicates. No reader needs to know a window exists, so the same
code answers correctly in an application, in a plug-in, and in a headless test that simply sets
the state it wants to exercise.

This file deliberately has no GLFW, no Cocoa and no platform header of any kind in it. That is the
point of it: the translation belongs to the shell, which is the only part that knows what it is
translating from.

## 2. `set_modifier_state()`

Called by the SHELL, from whichever events carry modifier state. Pass the complete set each time —
this replaces the stored value rather than merging into it, so a released key needs no separate
call. Clear it (eModifierNone) when the window loses focus: a key released while another
application has the keyboard is a release the shell will never be told about, and a modifier stuck
on is worse than one missed.

## 3. `set_modifier_state_from_glfw()`

THE GLFW SHELL'S ONE CALL. Pass the `mods` argument GLFW already gives a key or mouse-button
callback and it translates and stores it. Declared here but implemented in inputStateGlfw.c, so
this header stays free of platform headers and a plug-in links inputState.c alone — see that file
for why the mapping is shared rather than repeated in each application.

## 4. `synthlib_window_to_logical()`


The transform from window pixels to the logical, GUI-scaled space everything above the GLFW layer
works in. It was written out three times: character-identical in EmuUtility and SynthEdit as a
static window_to_logical(), and inlined into G2-Edit's get_global_gui_scaled_mouse_coord() — where
it had lost the divide-by-zero guard the other two kept, so a window reporting a zero dimension (it
happens while minimising) would have divided by it.

No window parameter: SynthLib owns the window (synthlib_window()), and every call site was passing
that same window back in.
