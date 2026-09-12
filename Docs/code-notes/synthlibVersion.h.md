# synthlibVersion.h notes

The longer comments from `synthlibVersion.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

── Which build is this? ────────────────────────────────────────────────────────────────────────

The question the plug-in made urgent: a .vst3 in a host's plug-in folder carries nothing that
says where it came from, and "G2 Alike" looks the same whether it was built this morning or three
weeks ago. An About box is the only place that can say.

THE VERSION COMES FROM GIT, and only when a build system that knows about git supplies it.
do-vst3 and do-release pass -DSYNTHLIB_VERSION_STRING from `git describe --tags`, which gives
something like "V0.6.4-beta.5-9-g4fc085a": the last release tag, how many commits have landed
since, and the commit itself. That is exactly enough to find the source a binary came from.

A PLAIN XCODE BUILD SAYS SO INSTEAD. Xcode cannot run git without a Run Script phase, and the
project's MARKETING_VERSION is stale and documented as not being the source of truth — reporting
it would be worse than useless, because a wrong version number is believed. So an Xcode build
says "development build" and leaves the timestamp to identify it.

## 2. `synthlib_about_text()`

A block of text naming the application, its version, when it was compiled, and which render
backend is running — that last is worth having now the backend is a preference: "it looks wrong"
and "it looks wrong on Metal" are different reports.

Returns a pointer to a static buffer, rebuilt on each call. Not thread-safe, which is no
constraint on something a menu item shows.
