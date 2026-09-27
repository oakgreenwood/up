# Playback, Warp, And Transients

Read for sample/voice playback, pitch/punch DSP, warp caches, transient metadata,
or `processBlock`. Also read [samples and velocity](samples-and-velocity.md) for
velocity selection/gain and [parameters and state](parameters-and-state.md) for
parameter storage or realtime-cache publication.

## ProcessBlock

Order: clear output; update BPM/transport via `HostTempoTracker`; publish the
transport state to `WarpCachePrewarmer`; render the sampler (including each
voice's PSOLA Formant followed by its LPC EQ/saturation, Mono, Panorama and sample EQ);
process global Rzhavchina, then OTT, then mix independent background noise.
The prewarmer's persistent worker performs all cache rendering and reclamation.

Rendering now uses fixed chunks of at most 512 frames to capture a noise trigger
without block-size-dependent allocation. Each chunk renders percussion and
captures latency-aligned voice hits, computes noise modulation, processes Rzhavchina
and OTT, then adds noise. The spectrum still receives original host offsets.
MIDI is traversed once: events are dispatched between render ranges through
the synth, preserving JUCE's default 32-frame coalescing and exact first rendered
interval independently of chunk boundaries. Future events are not passed to
JUCE repeatedly; end/zero-block events are dispatched once. Unsupported messages
longer than three bytes are filtered before constructing owning MIDI messages.

`RzhavProcessor` implements `Rzhavchina`: true bypass at `0`; above zero, bit depth
falls from `12` to `8` bits and target sample rate from `21 kHz` to `9 kHz`, clamped
to host sample rate.

No file I/O, logging, UI calls, allocations, or avoidable locks. Never access
`SampleGroupSelector` or `SampleSpecificParameterState` here; use prepared
realtime caches. Feed UI activity and the last played mapped note through fixed atomics
(`MidiNoteActivityState`). The editor polls activity on the message thread and
reads the remembered note only when `EDIT SAMPLES` is clicked.

## Background Noise

`BackgroundNoisePlayer` decodes the embedded noise WAVs once during processor
construction. Original sample rates are retained; linear interpolation follows
the host rate, with mono duplication or stereo-to-mono averaging as needed.
Invalid/empty sources stay silent. Decoding sanitizes non-finite samples to zero
and clamps samples to [-1, 1]. The immutable buffers remain owned by the processor
until stopped teardown; choosing a source never loads or frees audio on render.

Noise is mixed after Rzhavchina/OTT and ignores MIDI, tempo and transport. A new
instance is silent. Raising gain from the far-left sentinel starts at the selected region's start
with a 10 ms linear-amplitude ramp. Returning to silence ramps down over 10 ms,
then resets playback and skips all source reading. Source changes fade out the
old source before starting the new one; changes while silent do not start it.
The loop overlaps up to the final/initial 10 ms of the region (at most half its length), then
continues after the consumed head overlap. This smooths endpoint discontinuities
and shortens the repeated period by the overlap duration.

The selected region defaults to the whole file and is driven by normalized
`noiseStart`/`noiseEnd` values. Resolution clamps it to a nonempty frame range;
interpolation never reads beyond the selected end. During a waveform handle
gesture, noise completes one 10 ms gain fade-out and resets playback. It stays
stopped while the handle is held, without repeatedly adopting intermediate
regions. On release it restarts at the final selected start using the existing
gain/start fades. A quick release still completes the stop before restarting;
a completed gesture between callbacks is detected too. Gain at the silence
sentinel continues to prevent playback.

The UI publishes a scalar gesture counter (odd while dragging, even otherwise).
Audio acquires it once per host block before reading start/end targets and
passes that snapshot to every render chunk. Only audio owns the pending-stop
flag, observed counter and playhead. UI cleanup ends the temporary stop on
release, source/replacement changes, hiding or destruction. This runtime gesture
state is not saved and does not write the gain parameter.

Start/end automation outside a UI gesture retains the existing 2 ms region
fade-out, adoption of the newest range and restart with a 2 ms fade-in.
These scalar fades are separate from the base-gain
10 ms ramp and sidechain envelopes. No buffers are copied/replaced. Edits while
base gain is off update only parameter targets; the next start uses those bounds.
Repeated rapid edits adopt the most recent range after the short fade completes.
Modulo wrapping remains bounded for one-frame regions and rate conversion that
skips multiple loop periods; a floating-point endpoint guard prevents a rounded
position from reaching the exclusive end.

Construction also prepares 1024 min/max waveform bins from the sanitized mono/
stereo source. These small summaries, their peak magnitude and source length
are immutable with their source. The editor copies only these summaries,
not the audio buffers; drawing and handle interaction never decode or scan WAVs.

At the end of each noise render chunk, audio publishes one lock-free uint64
playhead snapshot containing source/slot identity and normalized full-source
position. Zero represents stopped playback; reset, mute completion, bypass,
release and zero-channel rendering clear it. No per-sample atomic write is used.
The UI reads only this scalar, rejects a different source/import slot and draws
the line from its existing timer. Imported-slot identity prevents displaying the
outgoing source's position on a replacement's waveform during its fade. The
snapshot is runtime-only and never serialized. Sidechain ducking/following leaves
the running playhead intact, including at zero modulation gain.

Preparation/sample-rate changes reset playback and configure the ramp; release
and bypass reset it without freeing buffers. The next active callback follows
the current saved gain. Zero blocks do not advance playback; one-frame and larger
blocks use bounded per-frame work with no scratch allocation. Non-finite targets
are made silent or select the first source; sample rates and indices are bounded.
Tail reporting is infinite while noise gain is enabled, finite otherwise.

See [noise state](parameters-and-state.md#background-noise-parameters),
[UI](ui.md#background-noise), and the current
[import audit](realtime-audio-audit-noise-import.md).

### Imported noise loading

`BackgroundNoisePlayer` has one persistent JUCE loader thread. Dropping a file
queues a latest-request-wins job; state restoration uses the same worker with
embedded WAV data. JUCE basic formats decode WAV, AIFF, FLAC, Ogg/Vorbis and MP3,
plus available platform codecs. Decoding reads at most 16384 frames per step and
checks cancellation between steps. Empty/invalid files, rates outside 1..768 kHz,
and imports exceeding 128 MiB of decoded mono/stereo float audio are rejected.
Files with more channels use the first two. Peak building, sanitization, portable
float-WAV encoding, Base64 work and allocations stay on the loader.

Three fixed imported-source slots provide nonblocking publication. Audio makes
at most two attempts to pin a published slot, and keeps its active slot pinned
across callbacks and throughout a source-switch fade. A per-call temporary pin
protects the new candidate. Adoption retains its pin and releases the old one;
reset/bypass/mute release only scalar pins. The worker reserves an unpublished,
unpinned slot with CAS before replacing its owning pointer, then publishes the
fully prepared source. Audio never destroys a source. Consecutive imports and
restores invalidate older jobs by generation. Only the worker can wait for a
free slot; rendering never waits or takes the import mutex.

UI waveform/name reads and state serialization share a non-audio mutex with
publication. The UI owns a summary copy and never retains a replaceable source
pointer. A revision counter refreshes same-slot replacements, even with identical
filenames, and ends any trim drag on the replaced source. Teardown cancels and
joins the worker before destroying slots, with host rendering already stopped.
The three slots bound retained source history; retired audio is destroyed on
worker reuse or stopped teardown. Imported playback uses the same gain ramps,
region fades, loop crossfade and sidechain as bundled noise.

Failed drops keep the previously published imported audio, if any, and report an
error on the message thread. Since a drop selects the imported slot immediately,
a first failed import leaves that slot silent; its gain setting is preserved.

### Noise Pump / Follow

`NoiseSidechain` receives hit events from the selected sample group (or All
samples). One-shots trigger once at source start. Loop/warp sounds use their
transient JSON onsets on every playback pass, independently of Punch and
`ignoreTransientShaper`. A quiet continuous loop bed cannot keep the envelope
open: only the markers retrigger it. Missing/empty valid markers fall back to
one trigger at source start, repeated if the source loops; no onset detection
is performed on the audio thread.

`PercussionSound` prepares an immutable noise-trigger table during loading.
Finite markers within the measured audio duration are sorted and deduplicated.
Each marker stores a stereo peak measured over the next 50 ms or up to the next
marker, whichever comes first. One-shots use their whole-source peak. At the hit,
this peak is multiplied by the current sample Gain, velocity gain and voice ADSR.
Levels at/below -80 dB produce no trigger. Audible hits have strength:

```
strength = 0.8 + 0.2 * min(1, 4 * peak * voiceGain)
```

Thus quiet hits open almost as far as loud hits, retaining up to 20% amplitude
dynamics. The existing -12 dB reference for full dynamic strength is unchanged.
Strength estimates use original source audio and gain, not post-EQ/Formant peaks;
Punch, sustain and global effects do not alter the 20% estimate. Simultaneous
voices use the strongest hit, without summing their strengths.

`NoiseTriggerTracker` advances at each actual rendered frame. Original playback
uses source position/rate; cached warp additionally divides by its time ratio;
realtime warp uses its output source-time cursor, not the input feed cursor.
Explicit loop wraps reset the marker cursor. Playback-path switches preserve
it, preventing repeat hits from small rounding rewinds. Multiple markers crossed
within one output frame coalesce to the latest; a binary search avoids scanning
all skipped markers. Per-voice event rings delay triggers by the existing PSOLA
plus coloration latency, including through the effect drain. They allocate only
in preparation and reset via scalar indices on note reuse. No extra audio
latency is introduced. Realtime warp follows the existing output timeline;
Rubber Band's transient displacement can still differ from the offline cache.

A hit starts a fixed 5 ms attack stage, followed by
release. New hits restart attack from the current values without a zero reset;
a quieter overlapping hit cannot pull an existing attack down. Long releases
can merge successive pulses. Release remains 1..2000 ms, default 150 ms.

Follow retains its exponential strength envelope `L`: its one-pole coefficients
cover 99.9% of a step within the configured time, snapping within 1e-5 of the target.
Full Follow opens during attack and fades to exact silence on release. The
maximum triggered gain above the base noise volume is fixed at +12 dB. Follow
amount has separate background and boost behavior:
background gain decreases using square-root depth across the Follow half, while the
triggered boost reaches its ceiling on a square-root curve by 30% of that half
(+15 on a conceptual -50..+50 scale). Above that point, the maximum triggered
level stays fixed while the untriggered background continues falling.

The 80/20 hit-strength envelope remains unchanged, so quieter hits can still
land below the ceiling. For a strength less than unity, some background remains
in the interpolation and the actual peak can decrease slightly as depth rises;
the boost ceiling itself does not change. The render path supplies fixed Attack
and Rise defaults, so legacy saved values or automation cannot override them.
The existing cached conversion/smoothing initializes Rise directly at +12 dB.
The square root is recomputed only while the existing smoothed Follow depth changes.

Pump now has separate strength and unit ducking envelopes `Lp` and `Ap` that
continue tracking even when Follow is selected. Attack uses the square of the
existing exponential coefficient, making the remaining distance to the ducked
target decay twice as fast (99.9% reached in 2.5 ms). The attack
stage still lasts 5 ms; values snap within 1e-5 of their targets,
allowing exact silence at full Pump before release. Retriggers preserve current
values and can only maintain or deepen the ducking target.

Pump release returns noise gain using quadratic ease-in over the Release time.
For normalized release progress `t` from 0 to 1:

```
Lp = releaseStartLp * (1 - t*t)
Ap = releaseStartAp * (1 - t*t)
```

Thus 25%, 50% and 75% through release restore 6.25%, 25% and 56.25% of the ducked
volume respectively; at the endpoint noise returns fully. Release-time edits
change the phase increment without resetting phase or envelope value. Pump
release begins from the actual final attack values.

Both Pump ducking depth and Follow background attenuation use `d = sqrt(abs(a))`
where `a` is the existing smoothed bipolar amount. This increases sensitivity
near centre and eases toward full depth: conceptual slider positions +/-10,
+/-25 and +/-35 on a -50..+50 scale give about 44.7%, 70.7% and 83.7% effective
depth. Full depth remains at +/-50; there is no plateau at +/-35. Each square
root is cached until the input changes. Envelope timing/curves are unchanged.

With raw depth `r = abs(a)`, curved depth `d = sqrt(r)` and fixed Rise gain
`B = 10^(12/20)`, the mapping is:

```
a < 0: 1 - d * ((1 - d) * Lp + d * Ap)
a >= 0:
    base = 1 - d
    boostShape = sqrt(min(1, r / 0.3))
    triggeredPeak = 1 + (B - 1) * boostShape
    multiplier = base + (triggeredPeak - base) * L
```

At an envelope of unity, all raw depths from 0.3 to 1 reach the same `B` maximum.
The boost curve keeps using raw depth, so its +15 saturation point is unchanged.
At zero envelope, only `base` remains. The gain ceiling is bounded to approximately
3.9811 (+12 dB); the noise renderer consumes it without a unity clamp.
Centre is exact unity. Full Pump uses `Ap` for level-independent ducking; full
Follow has no unmodulated floor. Amount changes still ramp over 10 ms. Trigger
selection changes preserve envelope continuity and filter future arriving hits.
After preparation/bypass, the first amount is seeded directly from the parameter,
so restored full Follow starts silent. Detection continues while base noise gain
is off. Modulation never resets the noise loop/playhead.

Capture and modulation reuse a fixed 512-float array; voices use fixed 128-frame
hit scratch plus the prepared latency ring. Parameters are snapshotted once per
host block; coefficient exponentials run only when times/rate change. No new
DSP-owned buffers are introduced. See the current
[depth curve audit](realtime-audio-audit-noise-depth-curve.md).

## Global OTT

`OttProcessor` processes the combined mono/stereo instrument output after
Rzhavchina. Its thresholds, ratios and crossovers follow the user's Ableton
reference. Its RMS detector recovery and fixed band makeup are calibrated from
the supplied `abletonNoOTT.wav` / `abletonOTT.wav` renders; see the
[measured comparison](ott-reference-comparison.md). The single amount
control scales the total input/compression/output gain in dB, preserving the
neutral default at zero; it is compression depth, rather than a dry/wet control.
The reference behavior for compression depth is described in
[Ableton's Multiband Dynamics manual](https://www.ableton.com/en/live-manual/11/live-audio-effect-reference/#multiband-dynamics).

Two fourth-order JUCE Linkwitz-Riley crossovers split at 88 Hz and 2.5 kHz.
The low branch receives the high crossover's allpass phase response so the three
unity-gain bands sum with flat magnitude. Cutoffs stay below Nyquist at low
sample rates. A shared RMS power envelope per band smooths the greater squared
channel magnitude, applying identical gains to left and right without cancelling
opposing channel signals. Converting power directly with `10*log10` avoids a
square root. Each band has a fixed +5.2 dB input gain included before
threshold detection and once in the applied audio gain. Full depth uses an
upward slope of `1/4.17` (the supplied Below ratio `1:4.17`), downward slopes
of `1/66` for low/mid and zero for high (infinite downward compression):

| Band | Upward threshold | Downward threshold | Downward ratio | Base attack | Base release |
| --- | ---: | ---: | ---: | ---: | ---: |
| Low | -41 dBFS | -33.8 dBFS | 66:1 | 47.8 ms | 282 ms |
| Mid | -41 dBFS | -30.2 dBFS | 66:1 | 22.4 ms | 282 ms |
| High | -41 dBFS | -37.5 dBFS | infinity:1 | 13.5 ms | 132 ms |

The 65% Time scale remains on the base times. The RMS power detector also uses
a fixed 0.15 release calibration factor: smoothing squared level is different
from Ableton's detector/gain timing, and the former long amplitude decay kept
quiet tails attenuated. These are this implementation's one-pole time constants,
not a claim to reproduce Ableton's attack/release algorithm:

| Band | RMS attack | RMS release | Calibrated band makeup |
| --- | ---: | ---: | ---: |
| Low | 31.07 ms | 27.495 ms | +12.5 dB |
| Mid | 14.56 ms | 27.495 ms | +7.75 dB |
| High | 8.775 ms | 12.87 ms | +11 dB |

The compression ratios and Amount range retain their supplied values; there
is no extra depth multiplier. The band makeup values are measured calibration
choices, not values claimed to come from Ableton's preset. No additional UI
control or parameter is introduced.

Master output gain is -7 dB
at full Amount, scaled in dB with the existing depth control: -3.5 dB at 50%
and no cut at zero. It is included after the compression curve, so it does not
alter detector thresholds or compression ratios. The existing depth smoothing
also smooths the output trim; no additional control or saved parameter is added.
The initial custom +3/+3/+4 dB makeup is removed. The previous
24 dB upward cap and -72..-90 dB fade-out are removed so quiet background detail
receives the full upward curve. A -120 dBFS detector floor bounds gain at silence
(approximately 60.055 dB upward, plus input, band makeup and master trim at full
depth) without adding noise or gating tails. Zero input from reset stays silent.
Ableton's detector, crossover and Soft Knee behavior can still differ.

Depth changes ramp over 20 ms. The detector and resulting gain now update every
frame; the former 16-frame target/interpolation stage allowed stale upward gain
to amplify new transients. The power envelope and depth ramp smooth gain without
that extra delay. Entering/leaving zero also crossfades
the crossover path over 20 ms to avoid an abrupt phase switch. Once at zero,
finite dry samples pass through exactly; filters/detectors continue tracking so
re-enabling does not restart from stale state. Active crossovers rotate phase;
the brief bypass transition can therefore affect magnitude.

Filters allocate their two-channel state only during preparation. Rendering
uses fixed stack arrays, scalar state and a cached parameter atomic, with no
block-sized scratch storage or allocation. Double filter/detector arithmetic,
finite input/output checks and denormal suppression protect numerical state.
Zero, one-frame and arbitrarily larger host blocks use the same bounded per-frame
work. Preparation resets sample-rate-dependent state, and release resets history.
No lookahead or additional latency is introduced; tail reporting adds a
conservative 0.5 seconds for crossover ringing. This is a character compressor,
not a peak limiter or loudness-matched effect; attacks can still exceed 0 dBFS.

See [OTT state](parameters-and-state.md), [UI](ui.md#global-ott), and the
[OTT realtime audit](realtime-audio-audit-ott.md). The isolated C++ DSP was compiled
and tested on macOS arm64 against the reference and signal edge cases. Complete
plugin builds, host validation, other platforms and listening comparisons on
additional material remain unperformed.

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
never reads the stored dB value or performs gain exponentiation per frame. The
parameter range is -20..+20 dB in 0.1 dB steps.

## Sample Mono

`sampleMonoAmount` narrows each voice after Formant, coloration and its makeup
gain, before voice mixing/global effects. It applies to original samples,
prepared warp caches, realtime warp output and formant tails in the same place.
All layers/variations of a selector group share the cached amount.

At 0 the blend is bypassed exactly. Above zero it computes `mid = (L + R) / 2`
and `side = (L - R) / 2`, scaling Side by `1 - amount` and reconstructing
`L = mid + side`, `R = mid - side`. Half-scaled terms are added/subtracted to
avoid overflowing the intermediate unscaled sum/difference. At 100% both outputs
contain the same mid signal; already-mono audio stays mono. This is conventional
summing, with no additional latency, filtering or level compensation; opposing
channel content can cancel.

New notes seed a voice-owned linear `juce::SmoothedValue<float>` from the cache.
Existing notes follow edits over 10 ms, with one atomic target read per voice
render call and one smoother advance per stereo frame, including the effect
drain. Smoothing persists across source/cache switches and is reset using the
current host sample rate at each note start. Panorama processes the result after
this blend.

See [parameters and state](parameters-and-state.md) for the 0..1 parameter,
percentage display, validation and persistence, and
[the Mono realtime audit](realtime-audio-audit-mono.md) for remaining blockers.

## Sample Equaliser

Each voice owns a four-band stereo `SampleEqualiser`, after Panorama and before
voice mixing/global effects. Its low shelf, two bell filters and high shelf all
use Q 1.0, -15..+15 dB gain, and 20..20000 Hz frequencies limited to 45% of the
host sample rate. The response graph calls the same coefficient factory as DSP.
JUCE's value-returning `IIR::ArrayCoefficients<double>` factories avoid allocating
reference-counted coefficient objects on audio.

Voices read eight cached scalar targets per render call. Coefficients are only
recomputed when targets change; normalized biquad coefficients interpolate over
10 ms, with independent double-precision channel histories and shared stereo
coefficients. Generated coefficients must pass finite checks and the second-order
Jury stability conditions before use. The valid denominator region is convex, so
the linear ramp between two accepted endpoints remains stable. New notes start at
their current settings with cleared histories.
Preparation clears histories and updates the rate; denormal protection is
provided by the processor. Finite checks prevent invalid input/history or an
out-of-float-range result from poisoning recursive state. A settled flat EQ
with cleared history is unity and becomes an exact bypass. Frequency-only edits
at 0 dB neither calculate coefficients nor run the band. A band returning to
0 dB continues until its prior IIR state is below a fixed negligible threshold,
then clears that state and bypasses. No processing latency is added.

An active EQ reserves up to 0.5 seconds of additional zero-input drain after the
existing formant drain. Enabling EQ during an existing drain reserves this once.
Hard stops/stealing discard tails as before; the processor's reported tail
includes the extra allowance. This is a bounded decay window, not an infinite
IIR tail. Rapid automation and the extra live-voice cost need listening/profiling.

`SampleSpectrum` collects and sums only the selected group's post-EQ voices at
their actual host-buffer offsets, including tails. It has fixed stereo capture
storage for 32768 frames and a rolling 2048-frame stereo window. After filling
the first window, it publishes an independent complete packet every 512 samples
(75% overlap), tagged with note and sample rate. A free queue slot receives the
two portions of the ring in chronological order, with four bounded scalar-array
copies. An atomic SPSC queue of four packets transfers complete frames
to the editor using release/acquire ownership; a full queue drops the new packet.
Audio never waits, overwrites a consumer-owned slot, allocates FFT storage or
performs an FFT. The editor computes stereo-power spectra and applies visual
decay with its own 60 Hz timer, performing at most two FFTs per tick on the newest
matching packet. The spectrum polygon has two segments per logical pixel, capped
at 2048; response and frequency/bin mappings are cached on UI. Closing the editor
disables capture. Oversized host blocks skip
analysis without changing playback or MIDI dispatch; zero-length blocks preserve
partial analysis frames, and bypass/selection/rate changes discard partial data.

See [EQ state](parameters-and-state.md#sample-equaliser-parameters),
[EQ UI](ui.md#sample-equaliser), and [the EQ audit](realtime-audio-audit-equaliser.md).

## Sample Panorama

`samplePan` operates after Mono and before voice mixing/global effects, including
formant tails. It uses stereo crossfeed so content from either source channel
can move to the chosen output. Centre is an exact bypass. On a mono output bus,
the matrix is bypassed while its smoother still advances, preserving the existing
single-channel output instead of silencing it when panned right.

The cached -50..50 integer position maps to a voice-owned -1..1 linear smoother.
New notes start at their group's position; changes on active notes ramp over
10 ms. Note start resets the ramp with the current host rate (valid range
1000..768000 Hz, otherwise 44100 Hz). The smoother advances once per stereo
frame across original, cached and realtime-warp playback, including tails.

For normalized position `p` and `g = 1 / sqrt(1 + p*p)`, the matrix is:

```
p < 0: Lout = g * (L - p*R); Rout = g * (1+p)*R
p > 0: Lout = g * (1-p)*L;   Rout = g * (R + p*L)
p = 0: Lout = L;             Rout = R
```

Both hard-pan endpoints sum `(L+R)/sqrt(2)` into the chosen output and zero the
other. For dual-mono input `L=R=M`, the output powers sum to `2*M*M` at every
position: centre remains unity on both channels, and a hard-panned mono signal
has approximately +3 dB in its remaining channel. This compensation is a defined
mono pan law, not a guarantee of constant power for arbitrary stereo material;
channel correlation and cancellation still affect a stereo downmix.

The normalization is recalculated only when the smoothed position changes.
Steady pan uses scalar multiplies/adds; during movement the square-root argument
is bounded to 1..2. Double intermediates and finite-output checks prevent extreme
input sums from introducing non-finite output. No allocation, delay, filter,
phase rotation, grain processing or extra tail is added.

### DAW References

The routing choice follows the independent-channel positioning available in
[Ableton Split Stereo Pan](https://help.ableton.com/hc/en-us/articles/360000103324-Split-Stereo-Pan-Mode),
[Logic Stereo Pan](https://support.apple.com/guide/logicpro/set-channel-strip-pan-or-balance-positions-lgcpbc21a438/10.7/mac/11.0),
and [Cubase Stereo Combined Panner](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/mixconsole/mixconsole_stereo_combined_panner_c.html).
Cubase explicitly notes that summing channels can increase volume and separately
offers [pan-law choices](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/project_handling/project_handling_project_setup_dialog_r.html).
These public descriptions do not specify a common sample-level matrix. The
formula above is this plugin's own normalized crossfeed implementation, not a
claim to reproduce any DAW's exact curve or its additional stereo-width UI.

See [the Panorama realtime audit](realtime-audio-audit-panorama.md).

## Punch And Pitch

`samplePunch` rises from unity during the 7 ms before each transient, reaches
the punch amount at the transient, then decays over 20 ms. Apply it to every
metadata transient on every loop pass. Warp maps starts to
`transientTime * timeRatio`; rise/decay durations stay fixed in playback time.
The parameter remains `0.0..1.0` in 0.01 steps; the editor represents those
values as `0%..100%`.

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
EQ/saturation -> negative-formant makeup gain -> Mono -> Panorama -> sample EQ -> mix/global Rzhavchina -> OTT. The knob spans -12..+12 semitones and
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
reports the combined drain plus the sample EQ allowance. Silent float bypass clears existing output storage
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

### Realtime Fallback Resources

`prepareToPlay -> PercussionVoice::prepareRealtimeWarpResources` prepares both
mono and stereo Rubber Band engines for each voice. `RealtimeWarpPlayer::start`
selects the matching prepared engine without constructing, replacing, or
destroying an engine. A missing configuration or playback-rate mismatch returns
failure instead of allocating during playback. Engine replacement happens only
during preparation with rendering stopped; voice teardown releases both engines.

The shared input and output scratch buffers are sized during preparation to
`max(4096, samplesPerBlock)` frames and two channels. Their dimensions remain
fixed until the next preparation. Rendering feeds at most the smaller of Rubber
Band's requested input, remaining source frames, and input capacity. Retrieval
is capped by output capacity and the remaining requested output. Only frames
actually retrieved are mixed. A non-looping source is marked final only when
the last source piece is fed, including when one engine request spans several
pieces. Zero-frame renders return without touching the engine. Larger host
blocks still pass through the voice's existing 128-frame chunks.

Preparing both channel configurations increases per-voice preparation memory.
This removes wrapper-owned allocation and resizing from note starts and rendering;
Rubber Band's internal allocation/reclamation during reset, ratio changes and
processing remains a separate realtime blocker. The existing feed/reset loop
also retains its lack of a work budget. Pitch debounce, live tempo/pitch updates,
cache scheduling and cache reclamation are unchanged. See the
[fixed-capacity fallback audit](realtime-audio-audit-warp-fixed-capacity.md).

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
