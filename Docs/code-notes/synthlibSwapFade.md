# synthlibSwapFade

## 1. Why
Changing a whole sound changes everything at once, one parameter at a time, while notes and effect tails carry on: a step in the output. The fade takes the output to zero first, so every jump happens in silence, and voices and tails keep running behind it. The plug-in wraps its load in `begin` and `end` and passes each finished block to `apply`. It is not for a single dial.

## 2. The hold
A host may hand parameters back a block or two later, so the output stays at zero for 20 ms after `end` before it rises.

## 3. The timeout
`begin` blocks the main thread until the audio thread has reached zero, normally a few milliseconds and at most one host block. A host that is not calling process never gets there, so the wait gives up after 100 ms and the load goes ahead; nothing is sounding then. Only the audio thread sets `silent`, and only while `holding`, so `begin` can never read an answer from an earlier fade.

## 4. From the audio thread
A host may deliver a program change on the audio thread, which cannot sleep. There the plug-in calls `start`, keeps the program number, and at the top of each block makes the change once `silent` is true, then calls `end`. The fade itself runs on the finished blocks in between.
