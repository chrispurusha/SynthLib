# synthlibPluginState.h notes

The longer comments from `synthlibPluginState.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

WHAT BOTH WRAPPERS NEED AND NEITHER FORMAT DEFINES: the parameter store, the saved-state blob, and
the formatting of a value. Plain C, so the C++ VST3 wrapper and the C Audio Unit wrapper share one
copy - which is the only way a project saved from one format reads back identically in the other.

THE SAVED STATE

A VST3 stores it as an IBStream and an Audio Unit as CFData inside its ClassInfo dictionary, but
the BYTES are the same either way - so a project saved in Live and one saved in Logic hold the
identical thing, and the two wrappers cannot drift apart over what a saved plug-in means.

Layout, as written since 2026-09-11:

```
    0   4    magic "SLP2"
    4   4    record count, little-endian
    8   12*N records: parameter id (u32, little-endian) and normalized value (IEEE-754 double)
    ..  4    length of the plug-in's own blob
    ..  n    the plug-in's own blob, exactly as getState() produced it

```
BY ID, NOT BY POSITION, and only the parameters worth saving. "SLP1", its predecessor, stored every
parameter's value in index order. That was fine for ten morphs; it is not fine for a plug-in with
two thousand controller pass-throughs, none of which mean anything a moment later, and it tied a
saved value to a POSITION - so inserting a parameter anywhere but at the end would have handed
every later one its neighbour's value. SLP1 blobs are still read.

A PLUG-IN THAT SAVES NO PARAMETERS THROUGH THE WRAPPER GETS ITS OWN BYTES BACK, UNWRAPPED - no
magic, no count, nothing but what getState() made. GenBridge is the case: every setting is in its
own blob, keyed by device, and every parameter is NO_SAVE. Wrapping that would change nothing it
restores and would make a project saved by this build unreadable to an older build of the same
plug-in, which knows nothing of a header. It reads back through the same path a project from before
the header does. The one rule this imposes: a plug-in's own blob must never begin with "SLP".

LITTLE-ENDIAN AND NATIVE DOUBLES, deliberately: every machine these run on is little-endian
(arm64 and x86_64 both), and a universal binary's two halves agree, so a project moves between
them intact. Should a big-endian target ever appear this needs byte swapping and a bumped magic.

A BLOB THAT DOES NOT START WITH EITHER MAGIC IS NOT AN ERROR. It is a project saved against an
earlier build, when the state was nothing but the plug-in's own bytes - G2 Alike's was a bare
patch path - so it is handed to the plug-in whole and the parameters stay at their defaults, or
at whatever stateParams() says the bytes imply. That is what stops this from emptying a slot in
somebody's existing project.

## 2. `tSynthLibParamSlot`

EVERY VALUE A WRAPPER HOLDS FOR ONE HALF OF ONE INSTANCE, BY INDEX, FOUND BY ID.

A host walks parameters by index and names them by id, and the two are not the same number - see
tSynthLibParamDesc. Both wrappers used to keep a plain array indexed by id, which worked while every
id was its own index and would have written GenBridge's pass-through at id 1000 past the end of a
twelve-entry array.

Built ONCE, from paramCount()/paramInfo(), which the contract says never change their count or ids
for the life of a variant. The values are read and written through the accessors below, which are
atomic: a host sets parameters from its UI thread while the audio thread reads them.

## 3. `synthlib_state_read()`

Parses a blob. `values` receives up to `capacity` saved parameters, and `*countOut` says how many;
an id `layout` does not know is skipped rather than guessed at. `*pluginData` points INTO `data`, so
it is valid only as long as that buffer is.

`layout` is what an SLP1 blob's positions are resolved against - it stored values, not ids.

Returns false only for a blob that is malformed - one truncated mid-value, or claiming more
parameters than could possibly fit in it. A short but consistent blob reads back what it has.

## 4. `synthlib_param_text()`

How a value reads, for a host that wants to print it. Asks the plug-in first; falls back to the
plain value with the parameter's own unit after it, which is right for most parameters.

Always writes something null-terminated when len > 0. Returns false only for a NULL parameter.

## 5. `synthlib_run_on_main()`

RUN `fn` ON THE MAIN THREAD: now, when that is where the caller already is, and otherwise as soon
as the main thread is next free. Both formats want their host notifications made from there, and a
plug-in that works out a new latency on a worker thread should not have to know that.

A POSTED CALL OUTLIVES ITS CALLER, so `ctx` must be heap memory `fn` frees, and `fn` must look its
instance up again rather than trust a pointer that may since have been destroyed.
