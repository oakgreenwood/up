# Playback, Warp, And Transients

Read for sample/voice playback, pitch/punch DSP, warp caches, transient metadata,
or `processBlock`. Also read [samples and velocity](samples-and-velocity.md) for
velocity selection/gain and [parameters and state](parameters-and-state.md) for
parameter storage or realtime-cache publication.

## ProcessBlock

Order: clear output; update BPM/transport via `HostTempoTracker`; prewarm caches
via `WarpCachePrewarmer` when tempo sync is enabled and transport is running;
render the sampler; process effects.

`RzhavProcessor` implements `Rzhavchina`: true bypass at `0`; above zero, bit depth
falls from `12` to `8` bits and target sample rate from `21 kHz` to `9 kHz`, clamped
to host sample rate.

No file I/O, logging, UI calls, allocations, or avoidable locks. Never access
`SampleGroupSelector` or `SampleSpecificParameterState` here; use prepared
realtime caches. Feed UI activity through fixed atomics (`MidiNoteActivityState`)
polled by the editor on the message thread.

## Metadata And Tempo

BinaryData JSON lookup uses the exact WAV stem: `1_v1_n1.wav` loads
`1_v1_n1.transients.json`. Each velocity/variation/pitch suffix has its own file;
older generated resource-name lookup is a fallback.

Fields: `sampleRate`, `warp`, `loop`, `ignoreTransientShaper`, `transients`.
Missing/invalid metadata falls back to one transient at `0.0`, `warp=false`,
`loop=false`.

- Sounds are constructed with original BPM `153.0`.
- Host tempo `<= 90 BPM` uses half-time warp base (`originalBpm * 0.5`).
- Warp BPM is quantized to `0.01 BPM`; offline caches are always enabled.
- BPM-cache prewarm: debounce `0.12 s`, retry `0.03 s`, maximum `2` concurrent builds.
- Note-off is honoured only when metadata has `warp=true`. Non-warp samples are
  one-shots that ignore note-off, even with transient JSON for punch/sustain.

## Punch And Pitch

`samplePunch` rises from unity during the 2 ms before each transient, reaches
the punch amount at the transient, then decays over 20 ms. Apply it to every
metadata transient on every loop pass. Warp maps starts to
`transientTime * timeRatio`; rise/decay durations stay fixed in playback time.

Normal, non-warp-protected playback updates
`SamplePlaybackRenderer::State::pitchRatio` from the realtime cache each render
block. Pitch changes affect already-playing samples/loops and intentionally
change playback speed and duration.

With metadata `warp=true` and tempo sync enabled, non-neutral pitch must preserve
duration. Neutral pitch uses the BPM-only offline cache without pitch-cache
allocation. Non-neutral loops may use the lazy cache below.

## Pitch-Aware Loop Caches

- Key: `(quantizedBpm, quantizedPitchRatio)`; pitch is quantized to `0.01` semitone
  before lookup/build. Loop-only; never prewarmed.
- `PercussionSound` stores one replaceable pitched cache. A different non-neutral
  `(BPM, pitch)` request drops the stored old cache before background rendering;
  voices holding it keep it alive via `shared_ptr` until stopping/switching.
  Allocate pitched audio buffers only on a non-neutral loop-pitch request.
- For an already-playing warp-enabled loop, `PercussionVoice` debounces pitch by
  `0.08 s` before updating `RealtimeWarpPlayer`. New notes immediately use the
  current pitch.
- After a debounced change, try the pitched cache; if unavailable, request it
  lazily and use `RealtimeWarpPlayer` from the current source position. When ready,
  switch to cached playback at matching source time and trigger the note-start
  declicker.
- That fallback runs RubberBand with `OptionPitchHighConsistency` for dynamic
  pitch and may briefly cost more CPU. Once cached, `SamplePlaybackRenderer`
  avoids realtime RubberBand's steady playback cost for that `(BPM, pitch)`.

### Offline Duration And Transient Alignment

`PercussionSound::renderWarpedCache` sets RubberBand's expected input duration
from the source sample count and trims/pads output to `sourceSamples * timeRatio`.
Never derive loop length from pitch ratio or RubberBand's pitch-dependent
returned frame count.

RubberBand first stretches to `timeRatio * pitchRatio`, then resamples back.
Author transient key-frame targets in that internal domain:
`sourceFrame * timeRatio * pitchRatio`. Using final-output targets
(`sourceFrame * timeRatio`) makes pitched loops sound off-tempo even with the
correct buffer length.
