# synthlibPluginVst2.c

The VST 2.4 binary interface around a `tSynthLibPluginDesc`, for the Akai MPC and Force: their standalone
firmware hosts nothing else. No editor, no Mac or Windows code; a product builds it with its own script
(TiAlike's `do-mpc`).

## 1. Compiled only with the interface's description
The interface is described by the MPC plug-in SDK's `framework/mpc_vst2_abi.h` (MIT, written without
Steinberg's SDK). The whole file is inside `__has_include("mpc_vst2_abi.h")`, so the library's own build and
the Windows build, which take every file in `plugin/`, compile it to nothing.

## 2. String lengths
The host's buffers for parameter names, units and values are nominally 8 characters; the MPC's are larger in
practice, and the SDK's own framework writes up to 31 plus the NUL. We do the same.

## 3. Parameters set from any thread
The MPC delivers a knob turned on its screen on the audio thread, but project and preset loads and automation
from the UI thread while audio runs. `setParameter()` therefore stores the value (so `getParameter()` reads it
back at once) and marks it; the next block applies everything marked, on the audio thread. A state save first
applies what is waiting, or the plug-in's own bytes would miss it; a state load drops what is waiting, or a
value set before the load would land on top of it.

## 4. In-place processing
The MPC passes the same buffers as input and output. Inputs are copied out at the top of the block, so an effect
may write its output before it has read all of its input.

## 5. Denormals
Flush-to-zero is set for each block on ARM (FPSCR/FPCR bit 24) and restored after. The MPC sets it itself on its
32-bit products only.

## 6. Finding the wrapper from the instance
The plug-in calls back with its own instance pointer (`synthlib_plugin_param_edited()` and the rest), so live
wrappers are kept in a small table and found by it.

## 7. One variant
A VST2 library holds one plug-in; the first variant is the one built. The Audio Unit subtype is reported as the
VST2 id and must match `uid` in the MPC's `plugin.json`; the AU version number is the VST2 version.

## 8. Hidden parameters are not shown
The host sees only parameters without `SYNTHLIB_PARAM_HIDDEN`, and its indices are positions in that list.
The hidden ones stay in the store, so a MIDI program change, controller, pressure or bend still reaches its
parameter. TiAlike's Program and Bank Select are never saved, by design, and the SDK's host simulator failed
its state round-trip on them while they were shown.

## 9. Programs
A plug-in with a `SYNTHLIB_PARAM_PROGRAM` parameter has as many VST2 programs as that parameter has positions
(stepCount + 1). A program's name is the parameter's text at that position, which is how the plug-in names the
stored sound a program change would load; selecting one sets the parameter as a MIDI program change would, through
the same queue as any other value (§3). The MPC lists these names in its preset list, and saves and restores the
program number with the project - before the project's state, which then wins. Without one there is a single
program, "Default".
