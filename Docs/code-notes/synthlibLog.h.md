# synthlibLog.h notes

The longer comments from `synthlibLog.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `gSynthLibLogName`

DIAGNOSTICS, GATED ON A FILE rather than an environment variable. The obvious gate would be getenv,
and it does not work: a host launched from the Dock inherits no shell environment, so the variable
is never seen in the one situation that matters. A file the user can touch is visible from anywhere.

```
    touch /tmp/<name>-log        # on within a second, no reload
    cat /tmp/<name>.log
    rm /tmp/<name>-log           # off again

```
The GATE is the hyphen and the OUTPUT is the dot - deliberately similar, and it has caught both
sibling projects out once.

THE PROJECT NAMES ITSELF by defining this, once, in its own plug-in source:

```
    const char gSynthLibLogName[] = "genbridge";

```
A constant rather than a set-the-name call, so there is no first call for two threads to race over
and no line logged before the name was set. A build that compiles this file and forgets the
definition fails to link rather than logging somewhere unexpected.

EVERY LINE SAYS WHO WROTE IT - "[Live 1234] ..." - because one log file is shared by every instance
in every process on the machine: a DAW with two plug-ins in it and a command line harness running
alongside all append here. Reading it without attribution means diagnosing one process's symptom
from another's output, which is exactly what happened in GenBridge.

CALLABLE FROM ANY THREAD. With the gate absent - almost always - a call costs a clock read and two
atomic loads; the gate itself, a syscall, is re-checked at most once a second. With it present each
line is an fopen/fclose in append mode, so a line is one write and lines from concurrent callers do
not interleave (plugin/test/logTest.c). Not for the audio thread with the gate present.

Moved here on 2026-09-12 from GenBridge's gbLog.c, which MidiSyncTool had copied as msLog.c and
which had since drifted: MidiSyncTool's checked the gate once per process and never again.
