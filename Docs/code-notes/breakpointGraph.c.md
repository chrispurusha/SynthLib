# breakpointGraph.c - notes

## 1. Points drawn and picked in screen space

Hit testing measures in screen units, not in 0..1 graph units, so the reach is the same on a wide graph
as on a narrow one and in both directions.
