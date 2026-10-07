# breakpointGraph.h - notes

## 1. `tGraphPoint`

A breakpoint graph is a line through points - an envelope, a key-tracking curve, a velocity curve. The
widget knows nothing about what the points stand for: the caller places each point in 0..1 coordinates
(y measured up from the bottom) from whatever values it shows, and says which way a drag may move it.
That keeps device knowledge out of SynthLib: SynthEdit builds the points from a layout file's dial ids,
G2-Edit could build them from a module's parameters.

A point that moves neither way is drawn as part of the line but has no handle and cannot be picked up -
an envelope's fixed sustain end, or a display-only graph's points.

## 2. `graph_hit_point()`

The movable point nearest `at`, within a small reach, or -1. Nearest rather than first, because an
envelope's points can sit on top of each other (an attack time of zero puts the attack point on the start
point) and the one under the pointer should win.
