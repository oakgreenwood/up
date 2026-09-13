# Sample-specific Formant3 TD-PSOLA comparison

Historical comparison-stage report. The separate LPC option has since been
removed and PSOLA renamed Formant; see the current
[single-option audit](realtime-audio-audit-psola-only.md) for the four-entry
registry, shortened chain and reduced latency/drain.

Updated for 85%-strength LPC EQ and a 20% saturation-blend increase in both
Formant and post-PSOLA Formant3 coloration.

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and float `processBlockBypassed`.
- JUCE synth MIDI dispatch, `PercussionVoice::startNote`, `stopNote`,
  `renderNextBlock`, voice stealing and source/tail completion.
- Processor/editor parameter callbacks reachable from host automation.
- Construction, preparation, state restore and teardown as lifetime dependencies.

Reachability inspected:
- Parameter callback -> five-entry registry -> finite/clamped Formant3 cache
  write -> precomputed scalar ratio -> voice target update. One Formant3 ratio
  load drives both its PSOLA core and its following LPC coloration instance.
- Note start -> `beginPlayback` -> prepared LPC/PSOLA state reset; existing
  original playback or warp cache/fallback selection.
- Render -> fixed 128-frame stereo scratch -> source renderer -> voice
  gain/shaping -> LPC Formant -> `PsolaFormantShifter::process` -> detector
  filtering/decimation -> bounded YIN analysis -> pitch-mark correlation ->
  resampled grain correction -> delayed dry plus correction -> independent
  `formant3Colouration` LPC EQ/saturation -> mix/global effect.
- Source finish -> combined zero-input drain -> sound-reference and warp-lease
  release. Hard stop/stealing discards pending output; the next start resets it.
- Prepare -> stop/reset voices -> allocate sample-rate-sized PSOLA rings outside
  processing -> report combined fixed latency. Teardown stops/clears voices,
  joins the cache worker, then destroys original sounds.
- Bypass clears existing output storage for this no-input instrument.
- Existing LPC FFT/saturation and playback dependencies retain the inspection
  described in [the Formant audit](realtime-audio-audit-formant.md).

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:129`, `:157`
Call path: warp cache miss/transition -> `start`/`render` -> Rubber Band
prepare/reset/process and scratch growth.
Issue: Existing channel-dependent engine reconstruction, internal heap work,
required-input buffer resizing and the no-output feed loop remain reachable.
Why it matters: A warped voice can allocate/reclaim memory or spend practically
uncontrolled time feeding the stretcher on audio. Fixed output chunks do not
bound the required input or engine work.
Minimal fix: Prepare all engine configurations and scratch outside audio and
bound feed/retrieve work, or move fallback rendering off audio. Formant3 does
not expand this subsystem; one-shots continue to avoid Rubber Band.

[BLOCKER] `Source/PluginProcessor.cpp:316`
Call path: MIDI activity and JUCE synth dispatch -> `metadata.getMessage` ->
`MidiMessage::allocateSpace`/destructor.
Issue: Existing long MIDI/SysEx materialization allocates and frees storage.
Why it matters: Unsupported long host messages can cause audio-thread heap work.
Minimal fix: Filter unsupported long messages as non-owning metadata before both
activity handling and synth dispatch. This is independent of the new parameter.

[PERF] `Source/Effects/PsolaFormantShifter.cpp:54`, `:124`, `:209`, `:276`
Call path: note reset and voice render -> detector/mark search/grain scheduling.
Issue: The new work is bounded, but includes burst costs. Detector analysis uses
at most 136 lags * 192 differences about 100 times per second per effected voice.
A seed examines at most `Pmax + 1` samples. Tracking examines at most 27
candidates with 32 correlation pairs each. A grain writes at most
`4 * Pmax + 1` stereo frames (3201 at 48 kHz), at most once per input frame.
Attempts back off by at least half the current period, even after failure;
accepted marks advance at least half a period. There is no catch-up loop.
Why it matters: Eight effected voices, dense retriggers, high sample rates and
tiny blocks can concentrate this work. Clearing the two prepared rings costs
128 KiB per note at 48 kHz, 512 KiB at 192 kHz, up to 2 MiB at the supported
768 kHz preparation limit, plus small fixed detector arrays. LPC work still
applies when either knob is active. No target-CPU timings have been measured.
Minimal fix: Measure full-polyphony CPU, dense note starts and worst-case tiny
blocks before making a production realtime-performance claim. Steady neutral
Formant3 already skips YIN and grains while retaining its detector/delay history.

[PERF] `Source/PercussionVoice.cpp:108`, `Source/Effects/FormantShifter.cpp:94`
Call path: voice render -> PSOLA output -> `formant3Colouration.process` ->
512-point spectral analysis/EQ/OLA -> asymmetric ADAA saturation.
Issue: Nonzero Formant3 now also runs the shared LPC implementation: six complex
512-point FFTs per 128-frame hop, a fixed 20-order LPC solve, 257 model logarithms,
up to 257 gain exponentials and one square root per channel per saturated frame.
Both controls enabled means two separate LPC instances may do this work.
Why it matters: The additional bounded work increases CPU and note-reset costs;
its FFT plans and fixed arrays also increase per-voice memory. Changing EQ depth
does not change the processing cost. Steady neutral skips the spectral/saturation work.
Minimal fix: Measure both knobs at full polyphony and high sample rates before
claiming a production CPU budget. No benchmark was requested or performed.

[RISK] `Source/Effects/FormantShifter.h:38`
Call path: serial chain -> either LPC Formant instance -> JUCE complex FFT.
Issue: JUCE's Windows fallback FFT has a per-instance SpinLock. The voice-private
FFT is used only by its audio render thread after preparation, so no other
thread contends under the existing lifecycle contract. macOS uses prepared vDSP
plans. This does not establish a lock-free backend; the PSOLA core itself uses
no FFT, but Formant3's added coloration does.
Why it matters: Concurrent preparation or sharing FFT instances would invalidate
the non-contention argument.
Minimal fix: Keep preparation/destruction outside active processing and FFT
instances private to their voices, as implemented.

Cross-thread ownership:
- Formant3 values: UI/host/state callbacks write fixed 128-element arrays of
  `atomic<float>` semitones/ratios. Lock-freedom is compile-time asserted. Audio
  reads only the ratio for the sound's mapped MIDI note; it does not depend on
  an atomic pair snapshot. Relaxed ordering is sufficient for independent scalar
  values. Later writes replace earlier values with no allocation or reclamation.
- Registry/group mapping: definitions are immutable, and the loaded sample-group
  inventory stays immutable during playback. Selection uses an atomic index.
  Audio callbacks perform bounded registry lookup without tree/string creation.
- PSOLA rings, analysis arrays, windows, filters and smoothers: owned by each
  voice, allocated/initialized during construction/preparation, then mutated only
  by audio. Reset fills POD elements without resizing or destruction; render does
  not resize, publish or release them. Voice destruction frees vectors after
  processing stops. A second preparation stops voices before replacing storage.
- LPC FFT/tables/rings and stereo scratch: same private voice ownership as the
  earlier Formant implementation. The new `formant3Colouration` is a direct
  voice member, constructed with its own FFT plan before processing. Preparation
  stops voices before updating its tables; note start clears fixed rings and
  seeds smoothers without reconstructing or releasing a plan. Only audio mutates
  its model, rings and saturation history after preparation. Destruction happens
  when the stopped processor clears voices. No cross-thread publication is added.
- Original sounds, metadata and velocity maps: loader-created immutable data;
  synth inventory retains ownership while voices reference it. Releasing a voice
  reference cannot destroy a sound retained by the inventory. Teardown joins the
  warp worker before clearing sounds.
- Warp buffers: immutable worker-owned fixed slots, release/acquire publication,
  at most two audio pin attempts, and decrement-only audio release. The worker
  reclaims retired buffers after readers leave. Replacements cannot destroy an
  earlier buffer held through the now-longer effect drain. No new cache key,
  lease type, publication queue or audio cleanup fallback is introduced.
- Tempo/warp flags and MIDI UI activity retain their existing scalar atomic
  handoffs. The editor polls activity on the message thread; no DSP/UI shared
  buffer is added. APVTS attachments and edit listeners are message-thread-owned;
  editor edit-tracking callbacks return immediately off that thread. State save
  and restore use existing tree-copy/replace paths outside rendering, while
  processor parameter callbacks only update the realtime cache.
- JUCE synth locks and steal-candidate storage retain existing lifecycle limits:
  eight voices are installed before processing, steal storage is reserved when
  voices are added, and no new UI/worker inventory mutation is introduced.

Verification:
- Static audit only, after the final relevant implementation changes. Inspected
  the 85%-depth EQ formula, 0..21.6% saturation ramp, both LPC instances, note reset,
  preparation, combined drain/latency and teardown. Re-inspected JUCE complex FFT
  vDSP/fallback processing and existing cache ownership. Registry/cache/UI/state
  wiring retains the inspection above; this refinement adds no parameter or UI.
- This refinement changes only two shared DSP constants and comments; no new
  allocation, ownership, locks, loops, parameters, latency or drain changes.
  Saturation remains a convex blend below unity; the increased finite EQ bound
  remains below the original +/-24 dB limit. Both controls share these constants.
- No build, test execution, allocation instrumentation, plugin validation,
  benchmark or listening test was requested or run, per repository instructions.
- Bounds: validated preparation rate is 1000..768000 Hz with a 44100 fallback;
  positive periods stay within `minPeriod..Pmax`. Power-of-two rings have at least
  `8 * Pmax + 16` slots. Detector count is at most 329 within 512-element arrays;
  lag neighbours stay within the 137-element difference array. Window interpolation
  guards the upper endpoint of its 1025-entry table. Positive ratio stays in
  0.5..2. Source reads reject negative, future and expired indices, and correction
  writes reject destinations outside the prepared future ring. The only new
  `while` loop is the bounded power-of-two capacity calculation in preparation.
- Numerics: input/output and correction storage reject non-finite floats.
  Detector filtering, energy, differences, correlation and interpolation use
  double intermediates; peak normalization precedes pitch analysis, silence
  suppresses detection, and normalization/parabolic denominators have floors.
  The confidence ramp stays in 0..1, period/ratio bounds protect divisions and
  window indices, and correlation protects near-zero energy. Corrections are
  feed-forward and each consumed slot is cleared. The processor's denormal guard
  covers the filters. No recursive audio synthesis filter was introduced.
- LPC EQ now applies `exp(0.85 * clamp(oldLogGain, +/-2.763102112))`. For the same
  analysed frame this applies 85% of the original boost/cut in dB, including capped
  bins, bounding the spectral gain to +/-20.4 dB. Source/destination model lookups,
  band taper, finite guards, reflection/error limits and positive 0.5..2 ratios
  are unchanged. Both voice instances execute this exact implementation.
- The added saturation instance uses a 0..21.6% ramp (1.2 times the original blend) and the same rationalized ADAA
  formula. Double intermediates handle finite float inputs; the denominator is
  at least 2. It retains only one previous sample/root per channel. Reset and
  neutral invalidate this history; the first active frame seeds it. The input
  to saturation is finite-checked after OLA, and correction is feed-forward.
- Block/lifecycle inspection: zero frames return, positive voice blocks chunk
  to at most 128 frames without resizing, and all detector/ring counters survive
  one-frame and changing sub-blocks. Source completion starts a positive bounded
  drain that strictly decreases before clearing the active note. New notes reset
  queued grains, detector state and both LPC/saturation histories. The extra
  stage uses the same fixed stereo scratch, with no buffer growth or extra loop
  over samples beyond the existing bounded frame processing. Mono sources duplicate into the stereo
  scratch; shared pitch timing keeps separate stereo channels intact.
- Neutral analysis: after queued transitions finish, no grains are scheduled at
  ratio 1, and both LPC instances skip correction/saturation after transitions.
  Serial latency is `2 * 512 + 4 * ceil(rate / 60) + 1`, reported in
  `prepareToPlay` even at zero. At 48 kHz this is 4225 frames (88.02 ms); the
  combined zero-input drain is 7651 frames, including 1024 for each LPC instance.
  `getTailLengthSeconds` uses the same valid-rate fallback as effect preparation.
- State inspection: Formant3 is appended after Formant, registered independently,
  defaults to zero when absent from old per-sample state, and participates in
  selection, editor-closed automation, persistence and Apply to All. DSP reads
  the precomputed ratio without doing per-frame pitch exponentiation.

Residual risks:
- Runtime behavior and CPU remain untested on macOS arm64/x86_64/universal and
  Windows. Compile/runtime verification and listening comparison remain to do.
- TD-PSOLA here is an experimental monophonic formant treatment. Noisy percussion,
  chords, short hits or unreliable/out-of-range pitch can leave the PSOLA core
  dry or mistrack; its added EQ/saturation still applies at nonzero Formant3.
  Grain interpolation/mark jitter can alter level, smear attacks or add roughness;
  upward linear resampling can alias. No pitch/formant accuracy or Soundtoys
  sonic-match claim has been established.
- The matching post-PSOLA EQ is itself a reduced LPC envelope shift, so it adds
  further coloration to the grain-shifted output. Identical semitone labels on
  Formant and Formant3 do not imply identical measured envelope displacement.
  LPC-only compensation remains deliberately below the original full-depth EQ.
- All voices incur about 88 ms total latency at 48 kHz, even with both knobs zero.
  Host compensation cannot remove live-monitoring delay. Longer drains occupy
  voice slots/cache leases for longer; hard stops/stealing discard pending output.
- Existing Rubber Band, long-MIDI, LPC and transient-processing costs remain.

Conclusion:
- No new blocking realtime hazard was found in the prepared Formant3 path.
  The wider reachable plugin path remains FAIL because of the existing warp
  fallback and long-MIDI allocation blockers.
