# synthlibPluginAu.c notes

The longer comments from `synthlibPluginAu.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

THE AUDIO UNIT SIDE, AND NOTHING ELSE. Everything specific to a particular plug-in arrives through
synthlib_plugin_variants(), exactly as it does for the VST3 wrapper beside this.

AUv2, NOT AUv3, and the reason is distribution. An AUv3 is an app EXTENSION: it has to be embedded
in a containing .app, installed under /Applications and launched once so that pluginkit registers
it, and an ad-hoc-signed extension - which is all an unpaid Apple membership can produce - does
not survive that reliably on somebody else's machine. An AUv2 is a plain bundle wrapped around one
dylib, which is exactly what the .vst3 already is, so it ships in the same .dmg with the same
"clear the quarantine flag" instructions and no new machinery at all.

WRITTEN AGAINST THE C API DIRECTLY. Apple's AUBase and the CoreAudio Utility Classes are not used,
for the same reason the VST3 wrapper does not use public.sdk: they bring a build system with them,
and everything they do is one dispatch table and some property plumbing. A component's factory
hands back an AudioComponentPlugInInterface with three function pointers; Lookup() turns a
selector into a method, and every method's first argument is the pointer the factory returned. All
of that is below, in the open.

AN EFFECT'S INPUT IS PULLED, not handed over: kAudioUnitProperty_SetRenderCallback or
kAudioUnitProperty_MakeConnection says where from, and au_render() fetches it. That path is written
and auval exercises the properties, but the SAMPLES are proven only once a plug-in that reads its
input has been run in a real host - MidiSyncTool and GenBridge will be the first.

EVERY INSTANCE IS ITS OWN. An Audio Unit is one object, so unlike VST3 there is no question of
which half belongs to which - but the plug-in's calls back into the wrapper still name their
instance, and are looked up in the table below rather than assumed to mean the only one loaded.

## 2. `variant_for()`

WHICH VARIANT THIS COMPONENT IS. One binary may register several plug-ins, and macOS tells us
which one it is instantiating through the AudioComponentDescription in the factory call - so the
subtype is matched against the descriptors rather than assumed. A subtype that matches nothing
falls back to the first variant: the component manager only ever asks for one it read out of our
own Info.plist, so a miss means those two have drifted apart, and the first variant is a better
answer than a null pointer.

## 3. `is_global_only()`

PROPERTIES THAT EXIST ON THE GLOBAL SCOPE AND NOWHERE ELSE. Answering them on the output, group
and part scopes as well is not harmless: auval reports each one as "returning valid information
for scope/element ... which should be invalid", and a host is entitled to conclude that a latency
or a tail time it read off the output scope means something.

## 4. in `au_get_property_info()`

GLOBAL SCOPE ONLY, AND AN EMPTY LIST EVERYWHERE ELSE. Returning the same list for
every scope told auval there were parameters on the input and output scopes too; it
then read one of them back with AudioUnitGetParameter, which answers kInvalidScope,
and the parameter test failed on a plug-in whose parameters are all fine.

## 5. in `au_get_property_info()`

NO kAudioUnitProperty_SupportedChannelLayoutTags AND NO AudioChannelLayout, deliberately.
A plug-in that advertises supported layout tags is then required to answer
kAudioUnitProperty_AudioChannelLayout for every scope it claimed, and auval checks the
INPUT scope even on an instrument that has no input - which failed with "Cannot verify
Audio Channel Layouts as Format handling has problems". Plain stereo needs neither: the
channel count in the stream format says everything there is to say about it.

## 6. in `au_get_property()`

ONE PRESET, "Default", and selecting it restores the parameter defaults. A host with
an empty preset menu looks broken in the same way a host with no parameters does.

THE ARRAY HOLDS POINTERS TO AUPreset STRUCTS, not CFTypes. That is what the property
is documented to be, so the callbacks are NULL and the structs have to outlive the
array - hence a file-scope constant rather than a local.

## 7. in `au_get_property()`

OUR OWN BUNDLE, LOOKED UP BY IDENTIFIER. The host is told where to load the view class
from, and the only bundle that has it is this one - so the identifier here and
CFBundleIdentifier in the .component's Info.plist have to match exactly, and do-plugin
writes both from the same place.

## 8. `map_control()`

THE MAPPING THE VST3 SIDE GETS FROM THE HOST, DONE HERE OURSELVES. A VST3 host converts a
controller into a parameter change and asks IMidiMapping which parameter; an Audio Unit host hands
the raw bytes over and leaves it to us. Both ask the same midiMapping() or read the same midiControl
column of the same table, so the two formats cannot end up wired differently.

## 9. `fill_transport()`

THE HOST'S TRANSPORT, SUCH AS IT IS. Everything here but the timestamp is a question we ASK the
host, through function pointers it installed if it felt like it - where a VST3 host fills in a
ProcessContext for the block it is asking us to render. So the answers are not tied to this block,
several hosts install none of these at all, and a plug-in whose whole purpose is timing accuracy
should be measured on both formats before either is believed.

## 10. `au_lookup_effect()`

ONLY A UNIT THAT TAKES MIDI ANSWERS THE MIDI SELECTORS. auval flags an 'aufx' that does - "it should
be 'aumf'" - and a host is entitled to route MIDI to anything that says it takes it. The selector
table is a plain function with no instance to ask, so it is two tables and the factory hands each
unit the right one.

## 11. `synthlib_plugin_request_resize()`

AN AUDIO UNIT'S HOST OWNS THE WINDOW AND IS NOT ASKED. There is no equivalent of VST3's
IPlugFrame::resizeView here: a Cocoa view resizes itself and the host follows, or it does not.
Answering false is what tells the caller to leave the window alone rather than resize its own view
inside a frame that did not move.
