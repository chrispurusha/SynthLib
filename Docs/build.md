# Building SynthLib

SynthLib is one checkout, beside the projects that use it, with one build.

```
GitHub/
  SynthLib/      this repo
  G2Alike/       and every other project
  AllAlike/      holding Pulse2Alike/ and TiAlike/, one level further down
```

## Getting it

```
git clone --recurse-submodules https://github.com/chrispurusha/SynthLib.git
```

Or, in an existing clone, `git submodule update --init --recursive`. glfw, freetype and libusb are submodules in
`ThirdParty/`.

Needs Xcode's command line tools, and for the third-party libraries:

```
brew install cmake autoconf automake libtool
```

The VST3 half needs the [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) at `~/Documents/vst3sdk`, or wherever
`$VST3_SDK` points. It is not built first: its headers and six source files go straight into the library. Without
it the library is built with a warning and no VST3 wrapper, so Audio Unit and application builds still work.

## Building it

```
./do-build              whatever is out of date
./do-build check        then link every object, to prove nothing is unresolved or defined twice
./do-build thirdparty   rebuild freetype, glfw and libusb
./do-build clean        remove build/
```

Nothing needs running by hand: every product runs `do-build` before it links, and it takes a twentieth of a second
when nothing has changed. A first build takes about 20 seconds on an M4, third-party libraries included.

Everything lands in `build/`, universal (arm64 + x86_64) at macOS 12.0, and nothing is written into the submodules
except libusb's generated `configure`:

| File | |
|---|---|
| `build/libSynthLib.a` | Every source in `src/`, `audio/` and `plugin/`, the VST3 wrapper, and the SDK files it needs |
| `build/libfreetype.a` | Every product that draws |
| `build/libglfw3.a` | Applications with a GLFW window |
| `build/libusb-1.0.a` | G2Alike |

A new source file in one of those three folders is picked up by itself. `plugin/synthlibPluginVst3ViewWin.cpp` is
Windows only and is left out.

The flags are the same for everything: `-O2 -g`, C17, C++17, ARC for Objective-C. A product's Debug configuration
does not change them, so SynthLib stays `-O2` in a Debug build.

## Using it

Includes, with `SL=../SynthLib`:

```
-I$SL/src -I$SL/audio -I$SL/plugin -I$SL/ThirdParty/glfw/include -I$SL/ThirdParty/freetype/include
```

A script sets `SL="${SYNTHLIB_DIR:-$HERE/../SynthLib}"` (`../../SynthLib` inside AllAlike) and runs `"$SL/do-build"` first.

**A plug-in links:**

```
-Wl,-dead_strip -Wl,-S -Wl,-u,_GetPluginFactory  $SL/build/libSynthLib.a $SL/build/libfreetype.a     (VST3)
-Wl,-dead_strip -Wl,-S -Wl,-u,_SynthLibAUFactory $SL/build/libSynthLib.a $SL/build/libfreetype.a     (Audio Unit)
```

**`-u` is essential.** A linker takes an object from an archive only if something refers to it, and nothing in a
plug-in refers to its entry point: only the host does. Without `-u` the wrapper is silently left out and the bundle
links, loads and has no factory. `-S` drops the library's debug map from the bundle.

**An application (Xcode)** has:

- a first build phase running `"$PROJECT_DIR/../SynthLib/do-build"`, which declares `build/libSynthLib.a` as its
  output, with `ENABLE_USER_SCRIPT_SANDBOXING = NO` because it writes outside the project;
- `libSynthLib.a`, the libraries it uses, and `-framework Metal -framework QuartzCore` in `OTHER_LDFLAGS`. Metal
  has to be named: the library is built without modules, so nothing links it automatically;
- the `SynthLib/src` group in the navigator for browsing, but not in the target's synchronized folders.

## Rules

- **SynthLib expects nothing from a product.** No application-named settings, no `#ifdef` for one application, and
  no symbol a product must define. What differs is passed in at run time: `SYNTHLIB_APP_INIT()`,
  `SYNTHLIB_LOG_INIT()`, `configure_synthlib_theme()`, the `synthlib_set_*_hook()` calls and
  `SYNTHLIB_PLUGIN_REGISTER()`. `./do-build check` proves the link half of this.
- **Run time, not compile time, for a product's own macros.** A product may give `synthlibDefs.h`'s names its own
  values in its own sources. SynthLib's objects were compiled with the defaults and do not see them.
- **Nothing registers itself from inside the library.** A constructor in an object nothing refers to never runs.
  Registration macros expand in the product's source for this reason.
- **The one thing a product compiles from SynthLib** is `SYNTHLIB_AU_VIEW_FACTORY`, because a host looks that class
  up by name. See `plugin/synthlibAuViewFactory.h`.

## Things worth knowing

- **libusb's `pipe2`.** The macOS 27 SDK declares it and `configure` finds it, but it exists only from macOS 27, so a
  library built that way would crash on anything older. `do-build` passes `ac_cv_func_pipe2=no`.
- **`make` compares whole seconds here.** A source saved in the same second its object was compiled is not seen as
  changed. Only a script can manage that; a test that edits and rebuilds in a loop should wait two seconds first.
- **The library is locked while it builds**, so two products building at once take turns.
- **A release records the commit.** Each `do-release` adds SynthLib's hash to the `.dmg`'s `Read Me First.txt`, as
  there is no longer a pin in the product's own history.

## Windows

Windows is a separate platform build and does not use this library. Its scripts compile SynthLib's sources directly,
with `SYNTHLIB_PLUGIN_BUILD` and `SYNTHLIB_NO_GL_BACKEND` still meaningful there, and build third-party code from
`ThirdParty/` for each architecture. See `Docs/windows-port-plan.md` in G2Alike.
