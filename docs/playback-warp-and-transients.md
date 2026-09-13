# Playback, Warp, And Transients

Read for sample/voice playback, pitch/punch DSP, warp caches, transient metadata,
or `processBlock`. Also read [samples and velocity](samples-and-velocity.md) for
velocity selection/gain and [parameters and state](parameters-and-state.md) for
parameter storage or realtime-cache publication.

## ProcessBlock

Order: clear output; update BPM/transport via `HostTempoTracker`; publish the
transport state to `WarpCachePrewarmer`; render the sampler (including each
voice's PSOLA Formant followed by its LPC EQ/saturation);
process global effects.
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
stops reading exhausted sources immediately, then drains the voice's formant
effects. Loop wrapping uses `fmod`, and invalid positions/rates end source
playback before buffer access. Warp caches retain their
separate worker, recent-five scheduling, fixed-slot publication and off-audio
reclamation. See [the one-shot removal audit](realtime-audio-audit-oneshot-pitch-removal.md)
for remaining plugin-wide realtime blockers.

## Formant

Formant is the surviving PSOLA option, previously called Formant3. The separate
LPC-only effect and its leading DSP stage are removed. Signal order is original,
cached or realtime-warp source -> voice gain/shaping -> PSOLA -> retained LPC
EQ/saturation -> negative-formant makeup gain -> mix/global Rzhavchina. The knob spans -12..+12 semitones and
keeps the former PSOLA parameter ID for state/automation compatibility; see
[parameters and state](parameters-and-state.md).

`PsolaFormantShifter` resamples pitch-mark-centred Hann grains while retaining
their output centres at the input mark times. Grain width changes the envelope
within a period; retaining mark spacing aims to preserve the fundamental. The
core uses neither LPC nor FFT. It observes already varispeed/warp-pitched audio,
so zero preserves natural varispeed formants and nonzero adds a further offset.
There is no automatic compensation for Pitch. One ratio load per voice sub-block
and note start drives both PSOLA and its following coloration.

The stereo-linked YIN-style cumulative mean normalized difference detector uses
a filtered analysis stream of at most 8 kHz, refreshed about every 10 ms, targeting
roughly 60-1000 Hz monophonic material. The stronger channel supplies timing,
with hysteresis; the same marks/windows process both original channels without
summing opposite-polarity audio. Initial detection needs roughly 41 ms at 48 kHz.
Missing pitch, silence or poor confidence fades new grain corrections towards
delayed dry in the core. Its following EQ/saturation still applies at nonzero
Formant. Ratio/confidence ramps take 20 ms; already scheduled grains finish.
Neutral skips pitch analysis/grains while keeping detector history and dry delay.

The first mark searches an amplitude extremum within one period; subsequent marks
use bounded waveform correlation around the next predicted period (+/-20%).
Hann grains span two input periods and resample by `2^(Formant/12)`. Overlapping
wet-minus-dry corrections, with square-root ratio gain compensation, are added
to delayed dry. Attempts back off by half a period, with at most one grain per
frame. This grain algorithm is unchanged from Formant3.

The supplied [PSOLA repository](https://github.com/maxrmorrison/psola) wraps Praat
through Python/parselmouth and serves only as a reference; no Python/Praat code
or dependency is embedded. The native core follows standard
[pitch-mark tracking](https://fon.hum.uva.nl/praat/manual/Sound___Pitch__To_PointProcess__cc_.html),
[pitch-synchronous overlap-add](https://www.fon.hum.uva.nl/praat/manual/overlap-add.html)
and [YIN](https://pubmed.ncbi.nlm.nih.gov/12002874/) principles.

### Retained EQ And Saturation

The voice-owned `FormantShifter formantColouration` processes PSOLA output using
the previous coloration settings. Its 512-frame FFT, 128-frame hop and periodic
sqrt-Hann windows support a stereo-power-linked 20-order LPC envelope model below
8 kHz (or Nyquist). Normalization, 50 Hz pre-emphasis, regularized Levinson-Durbin,
reflection limits, 50 Hz bandwidth expansion and 4 ms envelope smoothing condition
the model. It generates spectral gains, never a recursive audio synthesis filter.

EQ uses the shifted/current log-envelope difference with 1.15x contrast, an initial
+/-24 dB bound, then 0.95 scaling before exponentiation: 95% of the original boost/
cut in dB, capped at +/-22.8 dB. Correction fades to unity across the upper quarter
of the model band. The EQ follows the already grain-shifted output and adds further
coloration in the same direction; equal-and-opposite Pitch/Formant settings are
not calibrated cancellation.

After OLA, asymmetric gain-normalized `x / sqrt(1 + x*x)` saturation uses a biased,
rationalized first-order ADAA implementation. The parallel blend is
`0.2484 * abs(formantSemitones) / 12`, smoothed over 20 ms: zero at neutral and up
to 24.84% at either extreme, 15% more blend than the previous 21.6%. ADAA
reduces aliasing but does not eliminate it; its one-sample memory adds slight
high-frequency coloration. Neutral skips spectral correction and saturation.

Negative Formant also adds post-saturation makeup gain, independent of the
sample Gain knob: `boostDb = 3 * clamp(-formantSemitones / 12, 0, 1)`.
Thus -6 semitones adds +1.5 dB, -12 adds +3 dB, and zero/positive offsets add
nothing. The derived amplitude target is computed only on a ratio change or
note reset, and a voice-owned multiplicative ramp smooths live changes over
20 ms, shared by both channels. New notes start at their target gain. The boost
also applies when PSOLA pitch tracking falls back to dry, and returns to unity
when Formant returns to zero or above. It does not write the Gain parameter or
add any saved state, latency or tail memory. Finite output checks follow boosting.

### Latency And Voice Lifetime

With `Pmax = ceil(sampleRate / 60)`, the PSOLA core delays by `4 * Pmax + 1`
frames and its following coloration by 512 frames, including at zero. Total
host-reported latency is `512 + 4 * Pmax + 1`: 3713 frames (77.35 ms) at 48 kHz,
or 3453 frames (78.30 ms) at 44.1 kHz. Removing the separate LPC option removes
512 frames from the previous total. Live monitoring still incurs this delay.

The core drains its latency plus `3 * Pmax + 2` frames; coloration adds 1024 drain
frames, for 6627 total at 48 kHz. Source exhaustion/ADSR completion starts this
zero-input drain before releasing the voice/cache lease. Hard stops and stealing
discard pending output; new notes reset both stages. Preparation stops voices
before preparing sample-rate-dependent storage/tables. `getTailLengthSeconds`
reports the combined drain. Silent float bypass clears existing output storage
for this no-input instrument, avoiding JUCE's default nonzero-latency assertion.

Voice scratch remains fixed stereo/128 frames and handles variable/tiny/zero
blocks. PSOLA rings allocate only at construction/preparation; coloration uses
prepared FFT plans and fixed arrays. The later coloration refinement sets EQ to 95%, raises saturation blend by 15%,
and adds negative-formant makeup gain. No build or listening test was run for
that refinement.

This remains an experimental character effect: noise, chords, short hits and
out-of-range fundamentals can mistrack or receive mostly EQ/saturation. Grain
interpolation and irregular marks may alias, smear attacks, or change level.
See [the current realtime audit](realtime-audio-audit-psola-only.md) for ownership,
performance limits and existing plugin-wide blockers.

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
