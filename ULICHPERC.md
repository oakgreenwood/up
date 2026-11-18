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
  per-MIDI-note UI activity velocities and last-played sample memory.
- `Source/PluginProcessor.*` incoming note-on generations and `PluginEditor.*`
  pending note checks: SHAKE return-input timeout and settings alert.
- `Source/Midi/ShakeMidiState.h`: bounded cursor-trigger queue, generated note
  scheduling and MIDI output for VST3/AU.
- `Source/Midi/ShakeMidiTake.h`: fixed-slot live sample-MIDI take cache,
  transport/idle boundaries and message-thread MIDI-file export.
- `Source/Midi/VirtualMidiClient.*` and `VirtualMidiProtocol.h`: editor-only
  virtual MIDI routing, local discovery/lease/stroke messages and shared protocol.
- `integrations/virtual-midi/`: separate system MIDI helper, per-user installation
  and cross-DAW setup; macOS CoreMIDI and Windows 11 MIDI Services.
- `Source/UI/ShakeGesture.h`: message-thread cursor speed and reversal detection.
- `Source/UI/AbletonMidiBridgeClient.*`: separate legacy message-thread ADD MIDI
  request client; SHAKE does not use a DAW-specific transport companion.
- `Source/UI/SampleGroupSelector.cpp`: paged 3 x 3 sample grid, combined
  pitch/direction note labels, playback activity, and LP/ED/RP hover actions.
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
  tracking, plus optional PPQ for a take's original Arrangement position.
- `Source/Tempo/HostRecordingCallbackReceiver.h`: narrow AU wrapper-to-processor
  interface for retaining recording callback availability metadata.
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
- `Source/Effects/SampleEqualiser.cpp`: sample-specific four-band stereo EQ,
  fixed Q, smoothed coefficients and the shared response-curve calculation.
- `Source/Effects/SampleSpectrum.h`: bounded selected-sample audio capture and
  an allocation-free SPSC handoff to the editor.
- `Source/UI/SampleEqualiserEditor.cpp`: four draggable EQ dots, response curve
  and message-thread stereo spectrum FFT.
- `Source/UI/SampleWaveformEditor.cpp`: selected-group waveform and sample trim
  and fade gestures, using stereo summaries prepared by `PercussionSound` during loading.
- `Source/Playback/SampleEnvelope.h`: bounded source-time attack/release fades
  shared by original, cached and live-warp playback and waveform handles.
- `Source/Effects/OttProcessor.cpp`: global three-band upward/downward
  compression after Rzhavchina, with a single OTT amount control.
- `Source/Playback/BackgroundNoisePlayer.cpp`: embedded noise loading and
  queued custom-noise library imports, portable saved audio, pinned table publication,
  immutable waveform summaries, trimmed looping playback and declick fades,
  mixed after global effects.
- `Source/UI/NoiseWaveformEditor.cpp`: waveform display and draggable noise
  start/end handles with host parameter gestures. `Source/PluginEditor.cpp`
  handles editor-wide noise file drops and the noise-panel drag overlay.
- `Source/Effects/NoiseSidechain.h`: fixed-capacity hit capture, attack/release
  envelopes and bipolar noise pump/follow modulation.
- `Source/Playback/NoiseTrigger.h`: immutable onset/peak records, original-time
  transient tracking with 80/20 hit strength, and prepared voice-latency delay.
- `Source/SampleMetadata.cpp`: transient JSON lookup and parsing.
- `CMakeLists.txt`: plugin target, formats, BinaryData resources, JUCE, and
  RubberBand integration.
- `cmake/EnableAuRecording.cmake`: generated AUv2 wrapper transport overlay for
  the host recording callback missing from the pinned JUCE wrapper.
