# synthlibPluginAuView.m notes

The longer comments from `synthlibPluginAuView.m`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

HOW AN AUDIO UNIT HANDS A HOST ITS WINDOW. kAudioUnitProperty_CocoaUI names a bundle and a class
inside it; the host loads the class, makes one, and asks it for a view. This is that class, and
the view it hands back is the SAME one the VST3 editor shows - the plug-in's own createView().

THE CLASS NAME COMES FROM THE BUILD, and that is not decoration. Objective-C class names are
global to the PROCESS: two SynthLib-based Audio Units loaded into one host, each with a class
called "SynthLibAUView", would collide - the runtime would keep one, say so in a log line nobody
reads, and a host would then open one plug-in's editor from inside the other. do-plugin passes
-DSYNTHLIB_AU_VIEW_CLASS with a name of the plug-in's own.

## 2. in `synthlib_au_view_class_name()`

WHY THERE IS A CONTAINER AT ALL. An Audio Unit host has no equivalent of VST3's
checkSizeConstraint(): it resizes the view it was given and expects the view to cope. The canvas
plug-in scales from WIDTH alone, so a taller-but-not-wider window would uncover rows rather than
drawing larger - the very thing the application's own aspect lock prevents. Enforcing the ratio
here gives the Audio Unit editor the same behaviour the VST3 one gets from the host.
