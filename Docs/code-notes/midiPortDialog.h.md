# midiPortDialog.h notes

The longer comments from `midiPortDialog.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

THE MIDI PORTS DIALOGUE: which input the synth is heard on and which output it is played through,
each either a named port or "Automatic", with a Scan button and a line saying what is connected.

It only CHOOSES. The choice itself lives in synthlibMidi.h (synthlib_midi_ports_*), saved by name
under whatever scope the application set; connecting is the application's, because each one finds
its device differently. So the application supplies three things: what to do when the choice
changes (reconnect), what Scan means (the identity scan it has always had), and one line of status.

NOT BUILT IN TO THE COORDINATOR, unlike the alert and the browsers: it needs CoreMIDI, and
synthlibPopups.c is linked into G2 Alike, which has no business linking CoreMIDI. An application
that wants the dialogue registers midi_port_dialog_popup() with synthlib_popups_register().
