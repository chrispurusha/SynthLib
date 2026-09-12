# geometry.h notes

The longer comments from `geometry.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `dial_drag_pixels_for_full_range()`

HOW MANY PIXELS OF TRAVEL AN INCREMENTAL DIAL DRAG SPREADS THE FULL RANGE OVER: 200 normally, and
with Shift held the larger of the dial's own range and a fine-drag floor. Divide the drag delta by
this — so a bigger number is a SLOWER, finer drag.

Shared rather than written out per application because it is a POLICY, and one the two editors must
not disagree about: "Shift = finer" has to feel the same in G2-Edit as in SynthEdit, and the floor
has to stay above the unmodified 200 or Shift would speed the drag UP instead of slowing it. That
last part is not hypothetical — the first version of this floored at the range itself, which for any
dial narrower than 200 units evaluated to exactly 200 and made Shift do nothing whatsoever. Nearly
every dial in both editors is narrower than 200 (G2 parameters are 0-127), so "nothing whatsoever"
was the behaviour almost everywhere.

Reads the pushed modifier state (inputState.h), so it needs no window and works in a plug-in.
