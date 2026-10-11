# synthlibLog.c notes

The longer comments from `synthlibLog.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. in `synthlib_log_line()`

THE GATE IS CACHED, because it is a syscall and this is called from threads that must not
spend them. access() on every call is cheap next to the fopen below when logging is ON, and
it is the entire cost when logging is OFF - which is almost always, and is exactly when it
must be free. Re-polled once a second so touching the file enables logging mid-session rather
than needing a reload. Two threads may both re-poll at the turn of a second; both then store
the same answer.

## 2. Windows (2026-10-11)

There is no /tmp on Windows and no getprogname(): the switch and the log live in %TEMP% (`%TEMP%\<name>-log`,
`%TEMP%\<name>.log`), and a line names its process by pid alone. Both under `_WIN32`, so the Mac build is
unchanged. G2 Alike's Windows plug-in is the first Windows build to link the log.

