# synthlibPanelView.h notes

The longer comments from `synthlibPanelView.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

A PLUG-IN PANEL: the NSView a plug-in draws its own controls on, repainted continuously while it can
be seen. The window around it is SynthLib's - synthlibPluginVst3View.mm on VST3,
synthlibPluginAuView.m on an Audio Unit - and this is what both put inside.

GenBridge and MidiSyncTool each had their own copy (gbView.m, msView.m), 91% the same and already
drifting; since 2026-09-11 they share this. What differs between them is WHAT is drawn, so that is
all a plug-in supplies: the draw calls below. G2 Alike's editor is not one of these - it is the
application's own canvas, with input and redraw needs of its own (plugin/g2View.m in G2-Edit).

THE OBJECTIVE-C CLASS NAME COMES FROM THE BUILD: -DSYNTHLIB_PANEL_VIEW_CLASS=<a name of the plug-in's
own>. Class names are global to the host's process, so two plug-ins both calling theirs
"SynthLibPanelView" would collide - and the runtime would keep ONE, so one plug-in's editor would
run the other plug-in's copy of this file, drawing through the other's renderer. The Audio Unit
view's class name is chosen the same way, for the same reason.

PLAIN C INTERFACE: the plug-in describing itself (gbPlugin.c, msPlugin.c) is C and must not import
AppKit.
