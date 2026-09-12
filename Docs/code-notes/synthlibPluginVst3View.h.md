# synthlibPluginVst3View.h notes

The longer comments from `synthlibPluginVst3View.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

Wraps the NSView the plug-in's own createView() builds in the IPlugView a VST3 host wants. Defined
in synthlibPluginVst3View.mm because it touches Cocoa; declared here so the wrapper itself needs
no Objective-C.

The returned view is owned by the caller (refcount 1) and is handed straight back to the host.
May be NULL, which the caller reports to the host as "no editor".

`owner` - the controller - is kept alive for as long as the view is, because `widthSink` points into
it: every width the host settles on is written there, which is how a project remembers its editor's
size. `initialWidth` is that remembered width, or 0 for "none yet".
