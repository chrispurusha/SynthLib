# SynthLib

A shared C/C++ library for synthesizer editor applications. Common code extracted from the editor projects to avoid duplication and keep behaviour consistent across them.

## Projects using SynthLib

- [G2-Edit](https://github.com/chrispurusha/G2-Edit) — Nord G2 modular synthesizer editor
- [EmuUtility](https://github.com/chrispurusha/EmuUtility) — Emu sampler utility
- [Z1-Edit](https://github.com/chrispurusha/Z1-Edit) — Korg Z1 synthesizer editor

## Using SynthLib in a project

Add as a git submodule:

```
git submodule add https://github.com/chrispurusha/SynthLib.git SynthLib
git submodule update --init
```

In Xcode, add `SynthLib/` as a file system synchronized root group in the target, and add `$(PROJECT_DIR)/SynthLib` to the target's Header Search Paths. SynthLib headers include `sysIncludes.h` and `defs.h`, which must be provided by each consuming project.

## Dependencies

SynthLib code uses GLFW and FreeType 2, which must be present in the consuming project's build. See [THIRD_PARTY.md](./THIRD_PARTY.md) for details.

## License

GNU General Public License v3. See [LICENSE](./LICENSE).

Donations welcome: https://buymeacoffee.com/chrispurusha
