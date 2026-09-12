# synthlibMidi.c notes

The longer comments from `synthlibMidi.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `gPacketBuf`

ONE SHARED PACKING BUFFER RATHER THAN A STACK ONE. It has to be big enough for the largest thing
any app sends — a whole-bank restore, ~18.7KB measured — and putting that on the stack would mean
a 64KB frame at every call site, including the ones that only ever send three bytes. It is safe to
share because the mutex below already serialises sends: the lock is taken before the buffer is
touched and released after MIDISend has copied out of it.
