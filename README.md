# SynthLib

A shared C/C++ library for synthesizer editor applications. Common code and third-party dependencies extracted from the editor projects to avoid duplication and keep behaviour consistent across them.

## Working with LLMs

Development started as a solo project. As the project has evolved, I've recently incorporated LLM-assisted development into the workflow, using it as a tool for research, coding, debugging, and experimentation.

## Projects using SynthLib

- [G2-Edit](https://github.com/chrispurusha/G2-Edit) — Nord G2 modular synthesizer editor
- [EmuUtility](https://github.com/chrispurusha/EmuUtility) — Emu sampler utility
- [Z1-Edit](https://github.com/chrispurusha/SynthEdit) — Synthesizer editor
- ...and more in my repo list

## Structure

```
SynthLib/
  src/           — shared C/C++ source files
  audio/         — CoreAudio device access and a ring buffer
  plugin/        — the VST3 and Audio Unit wrappers
  ThirdParty/    — submodules: glfw, freetype, libusb
  do-build       — the one build; everything lands in build/
```

## Using SynthLib in a project

Clone it beside the project, run `./do-build`, and link `build/libSynthLib.a`. One build serves every project: nothing in the library is specific to one, and what differs is passed in at run time. See [Docs/build.md](Docs/build.md).

## License

GNU General Public License v3. See [LICENSE](./LICENSE).

Donations welcome: https://buymeacoffee.com/chrispurusha
