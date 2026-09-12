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

One-shots (`warp=false`, `loop=false`) latch pitch and the sample-specific
`samplePitchPreserveLength` switch at note start. Changes affect new hits only;
existing hits keep their starting buffer, pitch, and mode. The switch defaults
to false, including when restoring older state without it:

- Off: pitch changes playback speed and length. Punch follows the speed-adjusted
  transient positions, with its rise/decay durations fixed in playback time.
- On: pitch preserves the original duration, independently of tempo sync.
  `SamplePlaybackRenderer::State::pitchPreservesLength` explicitly keeps Punch's
  transient time ratio at `1.0`; the original transient timestamps and sustain
  timeline remain unchanged. Neutral pitch plays the original buffer.

The existing live pitch/debounce behavior for warp/loop samples is unchanged.
The editor disables the new switch for those samples.

With metadata `warp=true` and tempo sync enabled, non-neutral pitch must preserve
duration. Neutral pitch uses the BPM-only offline cache without pitch-cache
allocation. All warp-enabled samples, including non-looping ones, may use the
lazy pitched cache below.

## One-Shot Pitch Preparation

`Playback/OneShotPitchCache` runs a persistent coordinator and a fixed pool of
six render workers, polling atomic per-note pitch/mode requests. It debounces
pitch, mode, and host-rate changes for 80 ms and renders all velocity
layers/variations for a MIDI note before publishing them together.
Playback keeps using the last prepared group while a replacement is pending; if
none exists, it plays the original at natural pitch and length. A playing voice
never adopts a newer cache. Zero pitch bypasses preparation immediately.

- The five most recently played distinct one-shot sample groups take priority,
  newest first among settled requests. A group is one selector item/MIDI note,
  including all its velocity layers and variations; repeated variations of the
  same group only promote that group. Plays count even before Keep length is
  enabled. History lasts for this plugin instance and is not serialized.
- Prepare one group at a time. Up to six pool jobs claim independent recordings
  from that group's fixed output vector, each with a separate Rubber Band engine.
  Jobs may cross velocity-group boundaries. All jobs must finish successfully
  before the coordinator publishes the complete group. A one-recording group
  uses one render job; no more than six one-shot renders run simultaneously.
- Apply to All, individual edits, automation, and state restore use the same
  priority policy. After pending recent groups finish (or fail/become ineligible),
  prepare remaining eligible groups in MIDI-note order. Pending recent requests
  also take priority while debouncing. The coordinator observes edits every
  20 ms during renders, so requests debounce concurrently.
- If a recent group needs preparation while a group outside the recent five is
  rendering, cancel that lower-priority batch and revisit it later. Its partial
  output is discarded, not published or resumed. Already-started recent groups
  finish before the priority order is reconsidered. Continuous changes can defer
  preparation of less recent groups.
- Rubber Band R3, offline two-pass processing, standard multi-resolution window,
  channels together, internal threading disabled. No additional dependencies.
- Cache buffers use the host sample rate. Rubber Band receives time ratio
  `hostRate / sourceRate` and pitch scale `pitchRatio * sourceRate / hostRate`;
  their product leaves the internal stretch at the musical pitch ratio.
  Output length is `round(sourceFrames * hostRate / sourceRate)`, independent of
  pitch. Excess output is drained; a short output is zero-padded.
- No transient key-frame map is supplied to R3 for one-shot pitch preparation.
  R3 chooses local timing while total duration stays fixed. Punch continues to
  use the original metadata transient times; the rendered transients are not
  explicitly pinned to those times.
- `study`/`process` use 1024-frame chunks. Superseded requests, recent-group
  preemption, failures, and shutdown cancel between chunks and variations; output
  retrieval and finite-sample checks also observe cancellation. Failed renders
  leave the last prepared group intact and show `Pitch unavailable` until pitch,
  mode, or sample rate changes. Priority preemption is retried, not marked failed.
- Each group has `maximumVoices + 2` fixed slots. An atomic reader count pins a
  slot for each voice; `-1` reserves it for the worker. Audio acquisition makes
  at most two attempts, falling back to the original on a publication race.
  Audio release only decrements the count. The worker reclaims retired buffers
  after the final reader releases them; successive replacements never mutate
  a pinned buffer. One current group, active old groups, and one new render are
  retained, bounded by the eight-voice pool and fixed slots.
- Original sounds/metadata remain immutable while the worker runs. Teardown
  stops/clears voices, joins the coordinator and its outstanding pool jobs, stops
  the pool, then clears sounds. Host-rate changes trigger a replacement; the previous cache retains its own sample rate until
  replacement is ready. No worker access to the sampler after construction.

The sample renderer handles zero blocks, renders the final source frame, and
finishes exhausted voices immediately so leases retire. Loop wrapping uses
`fmod`, and invalid positions/rates end the voice before buffer access.

The one-shot coordinator/pool and the separate warp worker use fixed-slot
publication and off-audio reclamation. The six-worker limit applies to one-shot
R3 rendering; the warp worker can render concurrently. See [the pitch-mode realtime audit](realtime-audio-audit-pitch-mode.md)
and [the parallel-preparation audit](realtime-audio-audit-parallel-pitch-cache.md),
plus the warp-cache audit below for remaining fallback/MIDI blockers; this is not
a plugin-wide realtime-safety claim.

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
