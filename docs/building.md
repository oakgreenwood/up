# Building

Read for builds, CMake/dependencies/formats, or platform/architecture changes.
Check current scripts and target settings before running commands.

## Targets And Dependencies

- Project `ulichperc`; product `ulichpercs`; company `shroomnoise`.
- Synth with MIDI input and generated shake MIDI output (`NEEDS_MIDI_OUTPUT=TRUE`).
- Separate `ULICHPERCMidi` background helper publishes the `ULICHPERC MIDI` input for
  recording on the instrument's own track. See
  [virtual MIDI installation/build requirements](../integrations/virtual-midi/README.md).
  `ULICHPERC_BUILD_MIDI_COMPONENT` defaults ON; Windows configuration requires
  the compatible JUCE MIDI Services/CppWinRT SDK packages. Disable this option
  explicitly for plugin-only builds without those dependencies.
- Formats: Windows `VST3`; macOS `VST3`, `AU`, `Standalone`;
  other platforms `VST3`, `Standalone`.
- JUCE: CPM fetch at the tag pinned in `CMakeLists.txt`.
- On macOS, `cmake/EnableAuRecording.cmake` generates a copy of the pinned AUv2
  wrapper in the build directory and replaces only its transport query. It uses
  `HostCallbackInfo::transportStateProc2` when supplied by the host, publishing
  recording as well as playing/looping; the legacy callback remains the fallback.
  The downloaded dependency is unchanged. Configuration fails if the expected
  source fragment disappears, so JUCE updates require reviewing this overlay.
  The generated wrapper includes `Source/Tempo/HostRecordingCallbackReceiver.h`
  via an AU-only include path and reports callback presence to the processor's
  lock-free runtime enum. It does not depend on the concrete processor header.
  This applies to arm64/x86_64/universal AU builds; VST3 and Windows are unchanged.
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

`Source/UI/AbletonMidiBridgeClient.*` is the separate legacy ADD MIDI insertion
client. SHAKE does not use it or require an Ableton Control Surface: TRACK MIDI mode
routes through the ordinary system MIDI input while enabled. The prototype
`ULICHPERC_Transport` companion/installers and transport-state polling were
removed. Only the virtual MIDI helper needs installation for SHAKE.

## Virtual MIDI Source And Distribution Files

`integrations/virtual-midi/` is the maintained helper source, installation scripts
and setup guide. The `VirtualMidi` CMake install component writes generated
distribution files to `<prefix>/virtual-midi/`; the documented `--prefix stage`
creates the Git-ignored `stage/virtual-midi/` folder. Its installer/README are
intentional copies packaged beside the compiled helper, not additional sources.
Edit only the source files and regenerate the distribution with CMake. Nothing
in `stage/` is required as build input; it can be removed and regenerated.

## Build Command

For a macOS Debug plugin build, run `./build.sh` from the repository root. It
configures Xcode and builds both VST3 and AU. CMake's enabled
`COPY_PLUGIN_AFTER_BUILD` also copies both formats into the user's
`~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components` directories.
The script then uses `sudo` to replace the system VST3 bundle in
`/Library/Audio/Plug-Ins/VST3`. Build outputs remain in
`build/ulichperc_artefacts/Debug/`. This helper is not a Windows build command.

## Shake MIDI Recording

To capture without routing plugin MIDI output, enable SHAKE or play incoming
MIDI notes mapped to samples. Capture works during playback, recording and
stopped audition. Drag `DRAG MIDI` from the sample editor
onto the plugin's own MIDI track or another MIDI track. It creates a type-0,
single-track standard MIDI file at 960 ticks per quarter note, with note/channel/
velocity data and the current take's relative beat timing. SHAKE uses channel 1;
incoming notes retain their channels. Drop it at the original playback/recording
start to retain leading silence. Live imports standard MIDI
files as clips; the DAW controls the final drop position.

In Ableton Live, the legacy ADD MIDI client can ask a separate insertion companion
to insert the snapshot at its retained original Arrangement start, on the
track containing the requesting plugin. This bypasses the clipboard and plugin
MIDI-output routing. It does not select a destination from the currently selected
track and does not overwrite existing clips. A temporary Session clip (and, only
if needed, temporary scene) stages notes for `duplicate_clip_to_arrangement`,
using APIs shared by Live 11/12. It is cleaned up in the same undo group and
never launched. The ordinary Live clip-note API has no per-note channel field;
the exported MIDI file used by DRAG MIDI keeps channels. AU/Live can use virtual
SHAKE routing; direct AU MIDI output remains unsupported there.

The host's observed playback-start and recording-start edges replace the old
take. While neither playing nor recording, the first captured note-on after a
four-second pause replaces it too. The pause never splits a playing/recording
take. Missing host position retains the last known mode; without any known
mode, capture behaves as stopped audition. An empty replacement leaves nothing
to export. DRAG MIDI can snapshot a take while it grows. Loop passes are appended
in elapsed beat order. Host tempo is sampled per callback, so fast tempo ramps and
drop/import alignment need DAW testing. The cache is not saved in plugin state.
Export is message-thread-only and keeps its temporary MIDI file alive for a
host that reads it after a native drop. Returned virtual MIDI is ordinary incoming
MIDI and populates the same cache in AU/VST3.
See [the UI workflow](ui.md#captured-midi).

TRACK MIDI below SHAKE chooses the route: off (new-state default) auditions
internally using the existing local queue; on uses `ULICHPERC MIDI`, disables
new local SHAKE hits and waits for returned DAW input. The choice uses the existing
saved route property. The helper is claimed only with TRACK MIDI + SHAKE enabled;
input routing/monitoring is required in that mode. A stroke with no matching input
note-on after one second shows the settings alert once per mode/target session,
independent of recording state. TRACK MIDI off never shows that warning. Native
transport continues to supply tempo/take boundaries where available. No Control
Surface or recording-state companion is needed. See the
[component guide](../integrations/virtual-midi/README.md).

The retained shake/cache components are header-only: `Source/UI/ShakeGesture.h`,
`Source/Midi/ShakeMidiState.h` and `Source/Midi/ShakeMidiTake.h`. There are no new
libraries; file drag uses JUCE's native macOS/Windows drag-and-drop support.
The pinned JUCE VST3 wrapper and both AU input/output MIDI buffers reserve at
least 2048 bytes before processing. Generated output is capped at 224 short
events (2016 serialized bytes), preserving that capacity without audio-thread
resizing/swapping. Other wrapper types do not write generated output. Re-audit
this guarantee if updating JUCE or enabling additional plugin formats.

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
