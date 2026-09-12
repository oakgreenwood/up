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
| `samplePitchSemitones` | `Pitch` | float `-8..8` semitones | `0` | Sample-specific pitch offset for the selected sample group |
| `samplePitchPreserveLength` | `Pitch keep length` | bool | `false` | Inert legacy host parameter; no playback effect |
| `sampleGainDb` | `Gain` | float `-10..10` dB | `0` | Sample-specific volume, smoothed over 10 ms |

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

For `samplePitchSemitones`, `samplePunch`, and `sampleGainDb`:

- Processor helpers read/write the selected group's stored values.
- APVTS slider/button attachments report gestures/changes for host automation
  and undo. Processor parameter listeners update the atomic per-note cache,
  including when the editor is closed. They never access the state tree.
- Selection changes synchronize host-facing parameters to the newly selected
  group's cached values; state restore does the same after rebuilding the cache.
- `SampleSpecificRealtimeCache` is authoritative during use, indexed by the
  loaded group's MIDI note. It stores pitch semitones, the precomputed pitch
  ratio, Punch, and Gain in dB with a precomputed linear multiplier. Voices avoid state-tree access and
  pitch exponentiation; the ratio is computed only when the parameter changes.
- `getStateInformation` mirrors all cached groups to `SampleSpecificParameterState`
  before writing that child into `parameters.copyState()`. Restore uses
  `parameters.replaceState(...)` and rebuilds the cache from the saved child.
  Buffers and pending background work are not saved.
- After restoring per-sample values and tempo sync, request a full warp-cache
  startup pass through a scalar generation counter. The worker waits for a fresh
  valid host-BPM callback, then prepares all warp variants at their restored pitch
  even when transport is stopped. Matching caches are reused; superseded renders
  cancel. State restore does not render or wait for the worker. See
  [warp cache startup](playback-warp-and-transients.md#pitch-aware-warp-caches).
- One-shots latch pitch at note start and play the original sample at the
  corresponding speed. No one-shot pitch-preparation worker or cache remains.

The removed Keep length mode retains its original host parameter ID, position,
name and default as an inert compatibility entry, so subsequent parameter indices
(including Gain) stay stable in existing projects. APVTS still saves/restores its
value, but nothing reads it for playback, selection, or Apply to All. Old
sample-specific mode properties may round-trip in the state tree but are ignored.

Gain defaults to 0 dB for missing values in older state. Its cache rejects non-finite
input and clamps to -10..10 dB; conversion to linear gain occurs only on writes.
Voices read only the linear atomic and smooth live changes, including on existing
hits. See [the Gain realtime audit](realtime-audio-audit-gain.md).

## Apply To All

`APPLY TO ALL` copies the last UI-edited sample-specific parameter and its captured
value to every loaded sample group, including all velocity layers/variations that
share that group's MIDI note. Other parameters and the selected group stay as they
were. Gain, Punch, and Pitch are registered.

The editor observes APVTS change gestures for every registered parameter. Mere
selection, control refresh, ordinary host automation, and state restore do not
replace the captured edit. Switching groups retains that edit's value. Tracking
lasts for the current editor session and is not serialized. The button is disabled
until an actual edit occurs; beginning a gesture without changing a value does
not count. Knob drags, wheel/keyboard edits, double-click resets, and attached
toggles use the same tracking.

The processor's message-thread-only `applySampleSpecificParameterToAll` normalizes
the value through its APVTS range, writes the registered realtime cache and stored
state for every group, then updates the selected host parameter if needed. It also
marks non-parameter state as changed so hosts can save the batch even when the
selected parameter already matched. Playback reads the existing scalar atomics;
the batch never walks groups or touches `ValueTree` on audio. Updates publish per
group rather than as an indivisible snapshot. Existing warp-cache preparation, live
Punch response, and one-shot note-start pitch latching still apply.

One-shot pitch changes need no background preparation; subsequent hits use the
new value directly. Warp samples retain their existing startup/recent-five cache
policy. See [playback](playback-warp-and-transients.md).

See [the Apply to All realtime audit](realtime-audio-audit-apply-to-all.md) for the
inspection scope and existing render-path blockers.

## Adding Sample-Specific Effects

- Define the APVTS parameter/range/default in `PluginParameters.cpp` and add one
  entry to `PluginParameters::sampleSpecificParameters` (update its array size in
  the header and implementation). Supply allocation-free `noexcept` cache read
  and write callbacks using denormalized float values; convert bool/choice values
  in the callbacks and enforce the effect's valid range. This single registry
  drives recognition, processor listeners, selection synchronization, save/restore,
  UI edit tracking, and Apply to All. Do not add effect-specific button logic or
  separate parameter-ID lists.
- Add the per-note value to `SampleSpecificRealtimeCache`, including initialization
  and any derived DSP values. Prefer preallocated arrays sized to the MIDI-note
  inventory, scalar atomics, or another lock-free handoff. Allocate/resize only
  outside audio, e.g. construction, sample loading, or `prepareToPlay`.
- Use APVTS control attachments so UI edits emit change gestures. Sliders use
  `bindSliderToParameter`; attached buttons and choice controls are tracked via
  the same parameter listeners automatically. Custom controls must emit matching
  `beginChangeGesture`/`endChangeGesture` around edits. Use processor helpers for
  selected-group access; audio uses the cache.
- In `processBlock`, voices, and other realtime paths, never read
  `SampleSpecificParameterState`, `ValueTree`, strings, or locked structures.
  Use the resolved mapped note/group index, not filename parsing or UI-state lookup.
- For expensive DSP updates (e.g. RubberBand pitch/time), use audio-safe smoothing,
  debounce, crossfade, or offline caching suited to sound and CPU cost; do not
  apply raw UI-rate updates.
