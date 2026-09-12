# synthlibMidiPorts.c notes

The longer comments from `synthlibMidiPorts.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `gChoiceMutex`

── The chosen ports ─────────────────────────────────────────────────────────────────────────────

Written on the UI thread and read by the MIDI thread at every scan, so the three strings are only
ever touched under this lock. The prefs file is the UI thread's alone: it is read and written here
only from the two UI-thread entry points, never from synthlib_midi_ports_chosen().
