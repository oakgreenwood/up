# Playback, Warp, And Transients

Read for sample/voice playback, pitch/punch DSP, warp caches, transient metadata,
or `processBlock`. Also read [samples and velocity](samples-and-velocity.md) for
velocity selection/gain and [parameters and state](parameters-and-state.md) for
parameter storage or realtime-cache publication.

## ProcessBlock

Order: clear output; update BPM/transport via `HostTempoTracker`; publish the
transport state to `WarpCachePrewarmer`; render the sampler; process effects.
The prewarmer's persistent worker performs all cache rendering and reclamation.

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
- Warp-cache preparation uses one persistent worker. BPM changes debounce for
  `0.12 s`; pitch changes debounce for `0.08 s`.
- `HostTempoTracker` reads JUCE's optional position/BPM directly. A missing,
  non-finite, or sub-1 BPM retains the previous playback tempo and does not unlock
  startup preparation; it is not interpreted as a host-supplied default tempo.
- Switching to a prepared warp cache or original playback leaves the inactive
  realtime Rubber Band engine untouched. `RealtimeWarpPlayer::start()` resets
  it before reuse; cached note starts must not clear/reconfigure that engine.
- Note-off is honoured only when metadata has `warp=true`. Non-warp samples are
  one-shots that ignore note-off, even with transient JSON for punch/sustain.

## Sample Gain

`sampleGainDb` scales each voice independently of velocity, Punch, and sustain
makeup, before mixing voices and global effects. All velocity layers/variations
of a selector group share its gain. New notes start at the cached gain; existing
notes follow changes with a 10 ms linear-amplitude ramp. A voice-owned
`juce::SmoothedValue<float>` advances once per rendered frame, shared across
channels and retained across original/cached/realtime-warp playback switches.
The audio path reads a precomputed linear atomic once per voice sub-block; it
never reads the stored dB value or performs gain exponentiation per frame.

## Punch And Pitch

`samplePunch` rises from unity during the 7 ms before each transient, reaches
the punch amount at the transient, then decays over 20 ms. Apply it to every
metadata transient on every loop pass. Warp maps starts to
`transientTime * timeRatio`; rise/decay durations stay fixed in playback time.

One-shots (`warp=false`, `loop=false`) latch their sample-specific pitch at note
start. Pitch changes playback speed and duration: higher pitch shortens the hit,
lower pitch lengthens it. Changes affect new hits only. Punch follows the
speed-adjusted transient positions, with its rise/decay durations fixed in
playback time. Sustain follows the original source position as playback advances.
There is no one-shot duration mode, background pitch render, or preparation wait.
Legacy saved `samplePitchPreserveLength` values have no playback effect.

With metadata `warp=true` and tempo sync enabled, non-neutral pitch must preserve
duration. Neutral pitch uses the BPM-only offline cache without pitch-cache
allocation. All warp-enabled samples, including non-looping ones, may use the
lazy pitched cache below.

## One-Shot Playback

One-shots read the immutable original sample through `SamplePlaybackRenderer`.
The source step is `sourceRate / hostRate * pitchRatio`; they never enter Rubber
Band, acquire a prepared pitch buffer, or start a pitch-render worker. The former
one-shot coordinator, six-worker pool, and recent-group pitch cache are removed.
Apply to All updates per-sample pitch atomics for subsequent hits immediately.

The sample renderer handles zero blocks, renders the final source frame, and
finishes exhausted voices immediately. Loop wrapping uses `fmod`, and invalid
positions/rates end the voice before buffer access. Warp caches retain their
separate worker, recent-five scheduling, fixed-slot publication and off-audio
reclamation. See [the one-shot removal audit](realtime-audio-audit-oneshot-pitch-removal.md)
for remaining plugin-wide realtime blockers.

## Pitch-Aware Warp Caches

- Plugin construction schedules a startup pass over every `warp=true` sound,
  including all velocity layers, variations, and non-looping sounds. Successful
  plugin-state restore requests a fresh pass after publishing restored pitch and
  tempo-sync values. No rendered buffers are stored in plugin state or on disk.
- Startup preparation waits for a valid host BPM from an audio callback after
  construction/restore, then the normal 120 ms tempo debounce. With tempo sync
  enabled it prepares each sound's current `(BPM, pitch)` cache while transport
  is stopped as well as running. Neutral pitch prepares the BPM-only cache;
  non-neutral pitch prepares the pitched cache. Exact ready caches are reused.
- Recent-five and playback-demand work has priority, including during its
  debounce. Startup work comes next, followed by the existing BPM-only transport
  prewarm. Each pass runs sequentially on the warp worker and checks current
  tempo, pitch, restore generation, enable state, and urgent requests between
  chunks. A failed key is skipped for that pass rather than retried indefinitely.
- Hosts that do not process audio or supply BPM while stopped cannot start this
  pass until valid timing arrives. A plugin without host timing keeps its existing
  playback fallback. Immediate playback before caches finish can still reach
  realtime Rubber Band. Once the startup pass finishes, ordinary pitch/BPM edits
  retain the existing recent-five/demand policy; another state restore restarts
  the full pass.
- Key: `(quantizedBpm, quantizedPitchRatio)`; pitch is quantized to `0.01` semitone
  before lookup/build. Available whenever `warp=true` and tempo sync is enabled,
  independently of `loop`.
- The worker remembers the five most recently played distinct warp-enabled sound
  variants for the lifetime of the plugin instance. On a stable BPM or per-sample
  pitch change, it prepares their current caches sequentially, most recent first.
  The list is runtime history and is not serialized in plugin state.
- Each sound publishes one replaceable BPM-only cache and one replaceable pitched
  cache. This is a five-sound preparation history, not a history of five pitches;
  returning to an older pitch requires rendering it again.
- Sounds outside the recent five still make a demand request when played. During
  transport, BPM-only caches for the remaining warp inventory are prepared after
  recent and demand work.
- For an already-playing warp-enabled loop, `PercussionVoice` debounces pitch by
  `0.08 s` before updating `RealtimeWarpPlayer`. New notes immediately use the
  current pitch.
- Non-looping warped samples retain their existing immediate live pitch response;
  cache eligibility does not enable loop pitch debounce or change note-off/end
  behavior.
- New hits try the pitched cache. Playing voices also try it after live pitch
  updates (debounced for loops); if unavailable, request it lazily and use
  `RealtimeWarpPlayer` from the current source position. When ready and BPM is
  stable, switch to cached playback at matching source time and trigger the
  note-start declicker. Subsequent hits at the same BPM/pitch reuse that cache.
- The first hit immediately after a new BPM/pitch can still use realtime Rubber
  Band until its cache is ready. Existing ready caches remain valid for voices
  already using them; a new exact-key cache replaces the published version only
  after rendering completes.
- That fallback runs RubberBand with `OptionPitchHighConsistency` for dynamic
  pitch and may briefly cost more CPU. Once cached, `SamplePlaybackRenderer`
  avoids realtime RubberBand's steady playback cost for that `(BPM, pitch)`.

### Offline Duration And Transient Alignment

`PercussionSound::renderWarpedCache` sets RubberBand's expected input duration
from the source sample count and trims/pads output to `sourceSamples * timeRatio`.
Never derive loop length from pitch ratio or RubberBand's pitch-dependent
returned frame count.

Offline study/process runs in `1024`-frame chunks so superseded BPM/pitch work
and teardown can cancel between chunks.

RubberBand first stretches to `timeRatio * pitchRatio`, then resamples back.
Author transient key-frame targets in that internal domain:
`sourceFrame * timeRatio * pitchRatio`. Using final-output targets
(`sourceFrame * timeRatio`) makes pitched loops sound off-tempo even with the
correct buffer length.

Warp buffers live in fixed worker-owned slots. Audio acquisition makes at most
two atomic pin attempts and audio release only decrements a reader count. The
worker publishes completed immutable buffers and destroys retired ones after all
voice leases release. See [the recent warp-cache realtime audit](realtime-audio-audit-recent-warp-cache.md).
