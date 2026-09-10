# Parameters And State

Read for parameter definitions, automation, serialization, sample-specific values,
or audio-thread publication. See [UI](ui.md) for selector appearance/activity and
[playback, warp, and transients](playback-warp-and-transients.md) for pitch/punch
behavior or warp caches.

## Host Parameters

APVTS parameters are serialized with `parameters.copyState()` and restored with
`parameters.replaceState(...)`.

| Parameter ID | Display name | Type/range | Default | Main use |
| --- | --- | --- | ---: | --- |
| `rzhavchina` | `Rzhavchina` | float `0..1` | `0` | Bit depth and sample-rate reduction in `processBlock` |
| `sustainShorten` | `Pomyatost` | float `0..1` | `0` | Sustain/tail shortening in voices |
| `warpEnabled` | `Tempo sync` | bool | `true` | Enables tempo-sync warp behavior |
| `samplePunch` | `Punch` | float `0..1` | `0` | Sample-specific transient volume boost |
| `samplePitchSemitones` | `Pitch` | float `-6..6` semitones | `0` | Sample-specific pitch offset for the selected sample group |

## Program Metadata

The processor reports one program at index `0`, named `Default`. The nonempty
name satisfies the VST3 validator's program-name requirement. Program selection
and renaming are no-ops; selecting this program does not reset parameter values.

## Sample-Specific State

`AudioPluginAudioProcessor` stores the selected group index and serializes both
that index and its stable `(noteIndex, pitchIndex)` key. Restore by key, with
index fallback for older state.

`SampleSpecificParameterState` stores per-sample values in a `sampleSpecific`
ValueTree child, keyed by `(noteIndex, pitchIndex)` with legacy index fallback.
Its `ValueTree`, `CriticalSection`, string IDs, and linear scans belong only to
UI edits and state save/restore; never read it from audio.

For `samplePitchSemitones` and `samplePunch`:

- Processor helpers read/write the selected group's stored values.
- Normal APVTS slider attachments report gestures/changes for host automation
  and undo; custom value callbacks store the displayed value for that group.
- Selection changes refresh the knobs through their attachments, synchronizing
  host-facing parameters to the newly selected group's values.
- The processor mirrors audio values to `SampleSpecificRealtimeCache`, indexed
  by the loaded group's MIDI note. Precomputed pitch ratios and punch amounts
  avoid audio-thread `ValueTree` access and pitch `pow()` calls.

## Adding Sample-Specific Effects

- Define the APVTS parameter/range/default in `PluginParameters.cpp` and register
  its ID with `PluginParameters::isSampleSpecificParameterId`.
- Use processor helpers for UI access to the selected group's
  `SampleSpecificParameterState`.
- If audio needs the value, extend a dedicated realtime cache and mirror values
  before use. Prefer preallocated arrays/vectors sized to the loaded inventory,
  atomics, or another lock-free handoff. Allocate/resize only outside audio,
  e.g. construction, sample loading, or `prepareToPlay`.
- In `processBlock`, voices, and other realtime paths, never read
  `SampleSpecificParameterState`, `ValueTree`, strings, or locked structures.
  Use the resolved mapped note/group index, not filename parsing or UI-state lookup.
- For expensive DSP updates (e.g. RubberBand pitch/time), use audio-safe smoothing,
  debounce, crossfade, or offline caching suited to sound and CPU cost; do not
  apply raw UI-rate updates.
