# inputStateGlfw.c notes

The longer comments from `inputStateGlfw.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

THE GLFW HALF OF THE MODIFIER SEAM, AND THE ONLY PART OF IT THAT KNOWS GLFW EXISTS.

Separate from inputState.c on purpose. That file is the state and the predicates, with no platform
header of any kind in it, which is what lets a VST3 plug-in link it and push from an NSEvent
instead. This file is what a GLFW-hosted application adds on top, and a plug-in simply does not
compile it.

It is shared rather than written out in each application because the mapping is a DECISION, not a
formality: which physical key counts as "Cmd" differs by platform, and two editors that each
decided for themselves would eventually disagree. G2-Edit and SynthEdit now cannot.

## 2. `set_modifier_state_from_glfw()`

NOT A CAST, EVEN THOUGH BOTH SIDES ARE BIT FLAGS. GLFW orders them Shift, Control, Alt, Super
(1, 2, 4, 8) while tModifierBits orders them Shift, Cmd, Alt, Ctrl — Cmd and Ctrl are swapped, so
a cast would silently read Control as Command on every platform. It also whitelists: GLFW reports
Caps Lock and Num Lock in the same word, and neither is a modifier this UI has any use for.
