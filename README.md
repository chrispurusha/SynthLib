# SynthLib

A shared C/C++ library for synthesizer editor applications. Common code and third-party dependencies extracted from the editor projects to avoid duplication and keep behaviour consistent across them.

## Projects using SynthLib

- [G2-Edit](https://github.com/chrispurusha/G2-Edit) — Nord G2 modular synthesizer editor
- [EmuUtility](https://github.com/chrispurusha/EmuUtility) — Emu sampler utility
- [Z1-Edit](https://github.com/chrispurusha/Z1-Edit) — Korg Z1 synthesizer editor

## Structure

```
SynthLib/
  src/           — shared C/C++ source files
  ThirdParty/
    glfw/        — submodule: window/input/OpenGL context
    freetype/    — submodule: font rendering
```

## Using SynthLib in a project

Add as a git submodule:

```
git submodule add https://github.com/chrispurusha/SynthLib.git SynthLib
git submodule update --init --recursive
```

Build the third-party libraries before building the consuming project:

```
cd SynthLib/ThirdParty/glfw
cmake -S . -B build
cmake --build build

cd ../freetype
cmake -S . -B build
cmake --build build
```

In Xcode, add `SynthLib/src` as a file system synchronized root group in the target. Add the following to Header Search Paths:

```
$(PROJECT_DIR)/SynthLib/src
$(PROJECT_DIR)/SynthLib/ThirdParty/glfw/include
$(PROJECT_DIR)/SynthLib/ThirdParty/freetype/include
```

SynthLib headers include `sysIncludes.h` and `defs.h`, which must be provided by each consuming project's own `src/`.

## License

GNU General Public License v3. See [LICENSE](./LICENSE).

Donations welcome: https://buymeacoffee.com/chrispurusha
