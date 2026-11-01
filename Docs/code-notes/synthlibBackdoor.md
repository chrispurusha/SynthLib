# synthlibBackdoor.c - a plain-text command channel

Lets a script or an assistant read and edit a running plug-in's sound. Each plug-in names itself and fills in a `tSynthLibBackdoor`; `synthlib_backdoor_poll()` runs from its panel's sync, so on the main thread and only while the editor is open, and a change reaches the host as an edit on the panel does.

## 1. Off by default
It does nothing until `/tmp/<name>-backdoor` exists and belongs to the same user, as `synthlibLog` is switched by a file, and a release needs no separate build. Commands are the lines of `/tmp/<name>_cmd.txt`, which must also be that user's. The answers appear whole in `/tmp/<name>_result.txt`, and the command file is removed. With two instances open, whichever polls first answers.

## 2. Commands
LIST [word], CHANGED, GET title, SET title=value, NAME [text], PRESET n, SAVE path, INIT, HELP. A value is plain, as on the panel, or a choice's text. Titles are the parameters' full titles and are not case sensitive.
