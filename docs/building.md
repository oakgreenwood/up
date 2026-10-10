# Building

Read for builds, CMake/dependencies/formats, or platform/architecture changes.
Check current scripts and target settings before running commands.

## Targets And Dependencies

- Project `ulichperc`; product `ulichpercs`; company `shroomnoise`.
- Synth with MIDI input and no MIDI output.
- Formats: Windows `VST3`; macOS `VST3`, `AU`, `Standalone`;
  other platforms `VST3`, `Standalone`.
- JUCE: CPM fetch at the tag pinned in `CMakeLists.txt`.
- RubberBand: `external/rubberband/single/RubberBandSingle.cpp`.
- The retained Formant EQ/saturation and PSOLA core compile from `Source/Effects/FormantShifter.*` and
  `Source/Effects/PsolaFormantShifter.*` in the existing target. The native C++
  PSOLA implementation needs no Python, Praat or additional dependency and uses
  the same sources on macOS arm64/x86_64/universal and Windows.
- macOS defaults to universal `arm64;x86_64` unless overridden.
- Global OTT compiles from `Source/Effects/OttProcessor.*` using the already
  linked JUCE DSP module; no additional dependency or platform-specific code is
  required for macOS or Windows.

The sample EQ compiles from `Source/Effects/SampleEqualiser.*`, with the fixed
spectrum handoff in `Source/Effects/SampleSpectrum.h` and graph in
`Source/UI/SampleEqualiserEditor.*`. These are added to the existing target and
use the already-linked JUCE DSP module. No dependencies or platform-specific
code are added for macOS arm64/x86_64/universal or Windows.

## Build Command

For a macOS Debug plugin build, run `./build.sh` from the repository root. It
configures Xcode and builds both VST3 and AU. CMake's enabled
`COPY_PLUGIN_AFTER_BUILD` also copies both formats into the user's
`~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components` directories.
The script then uses `sudo` to replace the system VST3 bundle in
`/Library/Audio/Plug-Ins/VST3`. Build outputs remain in
`build/ulichperc_artefacts/Debug/`. This helper is not a Windows build command.

## Standalone App (macOS)

Run `./bstand.sh` to configure Xcode, build the `ulichperc_Standalone` target in
Debug, and open the app, or `./bstand.sh Release` for Release. `RelWithDebInfo`
and `MinSizeRel` are also accepted; `--help` prints usage.
The script resolves paths relative to itself, so it can be invoked from any
working directory. It requires macOS, Xcode, and CMake 3.24 or newer; the initial
configuration also needs internet access to fetch CPM/JUCE if not already cached.

The app is written to
`build/ulichperc_artefacts/<configuration>/Standalone/ulichpercs.app`, and the
script prints its absolute path on success, then launches a new instance with
`open -n` so each run uses the newly built executable. It builds in the same `build/`
directory as `build.sh` and preserves its configured macOS architectures; a fresh
configuration defaults to universal `arm64;x86_64`. To launch an existing Debug
build without rebuilding, run
`open -n build/ulichperc_artefacts/Debug/Standalone/ulichpercs.app`.

This helper is macOS-only. Windows still has only the VST3 target enabled in
`CMakeLists.txt`.

## Plugin Validation

Run `./val.sh` from the repository root to validate the existing Release VST3
and, on macOS, AU bundles with pluginval at strictness 10. Use
`conf=Debug ./val.sh` for Debug builds. The script resolves its default paths
relative to itself and does not build or install plugins.

Default bundle paths:

- `build/ulichperc_artefacts/Release/VST3/ulichpercs.vst3`
- `build/ulichperc_artefacts/Release/AU/ulichpercs.component`

Both bundles must exist on macOS before validation starts. `build.sh` builds both
Debug formats; to build both Release formats in a configured macOS build, use:

```bash
cmake --build build --config Release --target ulichperc_VST3 ulichperc_AU --parallel
```

On macOS, the default [pluginval executable](https://github.com/Tracktion/pluginval#running-in-headless-mode)
is `~/pluginval/Builds/Release/pluginval_artefacts/Release/pluginval.app/Contents/MacOS/pluginval`.
Other platforms default to `pluginval` on `PATH`. Override either default with
`PLUGINVAL`, for example:

```bash
PLUGINVAL=/full/path/to/pluginval conf=Debug ./val.sh
```

`strlvl` (1–10, default 10), `VST3_PATH`, and `AU_PATH` can also be overridden
through environment variables. For example, `strlvl=5 conf=Debug ./val.sh`
validates Debug builds at strictness 5. Non-macOS runs skip AU validation; the
Bash script requires a Bash environment on Windows.

Each run writes pluginval output and separate `vst3-terminal.log` and
`au-terminal.log` captures to a unique directory under `validation-logs/`.
`validation-logs/latest` points to the most recently started run. The script
continues with AU after a VST3 validation failure, and exits with status 1 if
either validation or terminal-log capture fails. Validation logs are ignored by
Git.

## CMake Rules

`noise/*.wav` is embedded in a separate `ulichpercNoiseData` target with the
`NoiseBinaryData` namespace/header, keeping noise out of percussion discovery.
The current inventory is Brazil Farm and Lviv Bicycle. CMake's `CONFIGURE_DEPENDS`
glob updates the inventory on rebuild when files are added/removed; distributed
plugins do not scan the project directory at runtime. Both WAV files are bundled
in every format. Playback compiles from `Source/Playback/BackgroundNoisePlayer.*`
using existing JUCE modules on macOS and Windows. The folder must remain populated
(CMake requires resources for the binary-data target).

Noise pump/follow detection is header-only in `Source/Effects/NoiseSidechain.h`,
listed with the other effect sources. It reuses JUCE and standard C++ on macOS
and Windows, with no additional dependency or platform-specific implementation.

`Source/UI/NoiseWaveformEditor.*` is included in the same plugin target and uses
the existing JUCE GUI modules. Its peak summaries are prepared by the noise
loader; no file reader, thumbnail worker or additional library is needed in UI.
`Source/UI/SampleWaveformEditor.*` uses the same modules. Percussion waveform
summaries are prepared from embedded sample buffers during loading, so the UI
adds no decoder or platform-specific dependency.

Runtime noise import uses JUCE's existing audio-format module and a JUCE worker
thread. `JUCE_USE_MP3AUDIOFORMAT=1` enables JUCE's bundled MP3 decoder on all
targets, alongside default WAV/AIFF/FLAC/Ogg and platform formats. No external
codec package is required on macOS arm64/x86_64/universal or Windows. Imported
state uses float PCM WAV, so reopening does not depend on the original codec
being installed on the destination platform.

- Keep configuration cross-platform without hardcoded paths.
- Use target-based `target_sources`, `target_link_libraries`, and
  `target_compile_definitions`; separate platform-specific logic.
- Verify dependency architecture support. Explain build-change impact on macOS
  arm64/x86_64/universal and Windows.
