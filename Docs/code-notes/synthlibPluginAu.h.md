# synthlibPluginAu.h notes

The longer comments from `synthlibPluginAu.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `synthlib_au_view_class_name()`

HOW THE HOST FINDS OUR EDITOR. kAudioUnitProperty_CocoaUI answers with a bundle and the NAME of an
Objective-C class inside it, so the name has to travel from the Objective-C file that defines the
class to the C file that reports the property.

IT IS PER PLUG-IN, NOT FIXED, and that is the whole reason it is a function rather than a
constant. Objective-C class names are global to the PROCESS: two SynthLib-based Audio Units
loaded in the same host, each defining a class called "SynthLibAUView", would collide, one would
silently win, and a host would open one plug-in's editor from inside the other. Each build sets
SYNTHLIB_AU_VIEW_CLASS to its own name - see do-plugin.

Returned +0; the caller does not release it.

## 2. `tSynthLibAuHandle`

THE PRIVATE PROPERTY THE EDITOR FETCHES ITS PLUG-IN THROUGH. A Cocoa view factory is handed an
AudioUnit and nothing else, so this is how it gets from that back to what the wrapper created.
Apple reserves property ids below 64000; this sits well above.

THE DESCRIPTOR TRAVELS WITH THE INSTANCE, because one binary may register several plug-ins and
they do not share editor geometry - an effect and an instrument variant are different sizes and
remember their widths separately. Reaching for variant 0 instead would open the wrong one's editor
for every variant after the first.
