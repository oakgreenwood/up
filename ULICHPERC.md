# ULICHPERC

Implementation reading guide for this C++/JUCE/CMake sample-based audio plugin.
[AGENTS.md](AGENTS.md) contains the project-wide working rules.
Read only the topic documents relevant to the task; follow their cross-links
when the change crosses those boundaries.

## Read By Task

| Task | Required reference |
| --- | --- |
| Sample loading/naming, MIDI mapping, velocity groups/gain, variations | [Samples and velocity](docs/samples-and-velocity.md) |
| Playback, pitch/punch DSP, warp caches, transient JSON, `processBlock` | [Playback, warp, and transients](docs/playback-warp-and-transients.md) |
| Parameters, automation, serialization, sample-specific values and realtime caches | [Parameters and state](docs/parameters-and-state.md) |
| Selector appearance/layout and MIDI activity indicators | [UI](docs/ui.md) |
| Build commands, CMake, dependencies, plugin formats and architectures | [Building](docs/building.md) |

Realtime-reachable changes also require the
[realtime audio audit](.agents/skills/realtime-audio-audit/SKILL.md) after the
final relevant implementation changes. Topic documents describe the existing
implementation; inspect the affected code and update the corresponding reference
when behavior changes.

## Main Source Files

- `Source/PluginProcessor.cpp`: plugin lifecycle, sampler ownership,
  parameter/state serialization, and `processBlock` orchestration.
- `Source/Parameters/PluginParameters.cpp`: APVTS parameter IDs and layout.
- `Source/Parameters/SampleSpecificParameterState.cpp`: message-thread
  storage, lookup, and serialization for future sample-specific parameter
  values. This is not realtime-safe audio state.
- `Source/Parameters/SampleSpecificRealtimeCache.cpp`: fixed-size, lock-free
  realtime cache for per-MIDI-note sample-specific playback values.
- `Source/Midi/MidiNoteActivityState.cpp`: fixed-size, lock-free handoff for
  per-MIDI-note UI activity velocities and latest-note editor selection.
- `Source/UI/SampleGroupSelector.cpp`: bottom UI selector for choosing the
  currently edited sample group.
- `Source/SampleLibrary/PercussionSampleLibrary.cpp`: embedded BinaryData
  sample loading, sample-group inventory, and registration with the percussion
  synthesiser.
- `Source/SampleLibrary/SampleNameParser.cpp`: sample-name parsing.
- `Source/PercussionSynthesiser.cpp`: velocity-group range calculation, MIDI
  velocity to group selection, and variation selection.
- `Source/PercussionSound.cpp`: sample storage, transient metadata ownership,
  velocity-layer metadata, and offline warp-cache rendering.
- `Source/PercussionVoice.cpp`: note-start playback setup, velocity gain,
  warp playback paths, transient/sustain shaping, and per-sample rendering.
- `Source/Tempo/HostTempoTracker.cpp`: host BPM/transport and BPM-motion
  tracking.
- `Source/Warp/WarpCachePrewarmer.cpp`: startup/state-restore preparation and recent
  warp-sample tracking, background BPM/pitch caches, atomic publication, and
  off-audio reclamation.
- `Source/Effects/FormantShifter.cpp`: retained LPC EQ and saturation coloration
  after PSOLA, with fixed latency and preallocated FFT/ring storage. There is
  no independent LPC-only control or leading LPC stage.
- `Source/Effects/PsolaFormantShifter.cpp`: per-voice Formant processing using
  pitch detection, pitch-mark tracking and resampled TD-PSOLA grains, with
  prepared delay/correction rings and a dry fallback for unpitched material.
- `Source/Effects/RzhavProcessor.cpp`: `Rzhavchina` bit-depth and sample-rate
  reduction effect.
- `Source/Effects/OttProcessor.cpp`: global three-band upward/downward
  compression after Rzhavchina, with a single OTT amount control.
- `Source/SampleMetadata.cpp`: transient JSON lookup and parsing.
- `CMakeLists.txt`: plugin target, formats, BinaryData resources, JUCE, and
  RubberBand integration.
