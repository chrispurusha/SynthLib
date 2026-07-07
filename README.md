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
    libusb/      — submodule: USB device access (used by G2-Edit)
```

## Using SynthLib in a project

Add as a git submodule:

```
git submodule add https://github.com/chrispurusha/SynthLib.git SynthLib
git submodule update --init --recursive
```

The `--recursive` flag is required to also initialise glfw, freetype, and libusb inside SynthLib.

Build the third-party libraries from the consuming project's root directory:

**glfw:**

```
cmake -S SynthLib/ThirdParty/glfw -B SynthLib/ThirdParty/glfw/build \
  -DGLFW_BUILD_DOCS=OFF \
  -DGLFW_BUILD_EXAMPLES=OFF \
  -DGLFW_BUILD_TESTS=OFF
cmake --build SynthLib/ThirdParty/glfw/build
```

**freetype:**

```
cmake -S SynthLib/ThirdParty/freetype -B SynthLib/ThirdParty/freetype/build \
  -DFT_DISABLE_BROTLI=ON \
  -DFT_DISABLE_BZIP2=ON \
  -DFT_DISABLE_PNG=ON \
  -DFT_DISABLE_ZLIB=ON
cmake --build SynthLib/ThirdParty/freetype/build
```

**libusb** (G2-Edit only):

```
cd SynthLib/ThirdParty/libusb
./bootstrap.sh
./configure
make
cd ../../..
```

libusb requires autoconf, automake, and libtool: `brew install autoconf automake libtool`

In Xcode, add `SynthLib/src` as a file system synchronized root group in the target. Add to Header Search Paths:

```
$(PROJECT_DIR)/SynthLib/src
$(PROJECT_DIR)/SynthLib/ThirdParty/glfw/include
$(PROJECT_DIR)/SynthLib/ThirdParty/freetype/include
$(PROJECT_DIR)/SynthLib/ThirdParty/libusb/libusb
```

SynthLib source doesn't include any header from outside SynthLib. The one exception was `utilsGraphics.cpp` reaching into the consuming project's `defs.h` to pick up the `G2_EDIT` identity macro and the `ENABLE_LOG_DEBUG`/`ENABLE_USB_LOG` logging toggles — that's now resolved two different ways: app identity is passed in at runtime via `configure_synthlib_theme()` (see `utilsGraphics.h`), and the logging toggles are supplied as Preprocessor Macros in each consuming project's own Xcode build settings (`ENABLE_LOG_DEBUG` on the Debug configuration only) rather than via a header include.

## License

GNU General Public License v3. See [LICENSE](./LICENSE).

Donations welcome: https://buymeacoffee.com/chrispurusha
