# Sample-specific Formant with vocal character and saturation

The separate LPC option is now removed. See the current
[single PSOLA Formant audit](realtime-audio-audit-psola-only.md).

This report records the LPC Formant implementation before Formant3 was added.
Its four-entry registry, 512-frame total latency and 1024-frame voice drain are
historical; see [the Formant3 audit](realtime-audio-audit-formant3.md) for the
current five-entry registry, serial processing chain and combined latency/drain.
The later 50% EQ reduction and added post-PSOLA LPC/saturation instance are also
covered there; the full-depth +/-24 dB EQ described below is historical.

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and explicit float `processBlockBypassed`.
- JUCE synth MIDI dispatch, `PercussionVoice::startNote`, `stopNote`, and
  `renderNextBlock`, including one-frame sub-blocks and voice stealing.
- Processor/editor parameter callbacks reachable from host automation.
- Preparation, state restore, and teardown as ownership/lifecycle dependencies.

Reachability inspected:
- Parameter callback -> four-entry registry -> finite/clamped Formant cache
  write -> lock-free scalar ratio -> voice target update.
- Note start -> `beginPlayback` -> fixed ring reset and cached Formant ratio;
  existing one-shot pitch latch or warp cache/fallback selection.
- Render -> fixed 128-frame voice scratch -> `renderSourceBlock` -> original
  sample renderer or existing realtime warp renderer -> gain, ADSR, Punch,
  sustain, declicker -> `FormantShifter::process` -> `processFrame` -> JUCE
  complex FFT -> normalized stereo power -> fixed-order LPC analysis ->
  frequency envelope correction -> overlap-add -> asymmetric ADAA saturation
  -> output mix -> Rzhavchina.
- Source finish -> 1024-frame zero-input drain -> `clearActivePlayback` ->
  JUCE sound-reference release and decrement-only warp lease release.
- Preparation -> stop/reset voice -> LPC tables/smoothing reset; processor shutdown
  -> stop voices -> destroy voices/FFT plans -> join worker -> destroy sounds.
- Bypass -> clear existing output storage. This no-input instrument has no
  pass-through audio to delay; its silent bypass avoids the default JUCE latency
  assertion. Double-precision processing is not advertised or dispatched.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:156`
Call path: warp cache miss/transition -> `start`/`render` -> Rubber Band
prepare/reset/process and scratch growth.
Issue: Existing channel-dependent engine reconstruction, Rubber Band internal
allocation/reclamation, required-input buffer growth, and the no-output feed
loop remain reachable. The new 128-frame output chunks do not bound Rubber
Band's required input or internal work.
Why it matters: A warped voice can still allocate or run excessive work on audio.
Minimal fix: Prebuild engine configurations, bound input/output work and buffer
sizes, or move fallback rendering off audio. This existing subsystem was not
expanded to implement Formant; one-shots still do not enter Rubber Band.

[BLOCKER] `Source/PluginProcessor.cpp:316`
Call path: MIDI activity and JUCE synth MIDI dispatch -> `metadata.getMessage`.
Issue: Existing long MIDI/SysEx materialization can allocate and deallocate.
Why it matters: Unsupported long host messages can trigger audio-thread heap work.
Minimal fix: Filter unsupported long messages as non-owning metadata views before
both activity handling and synth dispatch.

[PERF] `Source/Effects/FormantShifter.cpp:88`, `:163`, `:258`
Call path: voice render -> every 128th frame -> spectral analysis/correction.
Issue: Non-neutral stereo Formant uses six 512-point complex FFTs per hop,
a 20th-order LPC solve, 257 log evaluations, and up to 257 gain exponentials.
Saturation adds one square root per channel per output frame when active.
Analysis tables, window, bandwidth expansion and FFT plans are prepared off
audio; steady neutral Formant skips spectral analysis and saturation. Unchanged
ratio targets skip saturation-mix conversion, and zero log gains skip exp.
Why it matters: Eight simultaneous effected voices cost more than interpolation,
especially at high sample rates and small host blocks. Work is fixed per hop
and independent of sample duration, but has not been measured on target CPUs.
Minimal fix: Measure full-polyphony CPU before deciding whether further DSP
optimization is needed; no runtime performance claim is made here.

[RISK] `Source/Effects/FormantShifter.h:38`
Call path: `processFrame` -> voice-owned `juce::dsp::FFT::perform`.
Issue: JUCE's Windows fallback complex FFT takes a per-instance SpinLock. Each
voice owns its FFT, used only by its audio render thread, so no other thread
can contend for this lock under the plugin lifecycle contract. This is not a
claim that the backend is lock-free. The macOS vDSP path uses prepared plans.
Why it matters: Sharing this FFT across workers/voices would invalidate the
non-contention argument. Inspected complex transforms use prepared arrays;
JUCE's real-only transform scratch-allocation branches are not called.
Minimal fix: Keep FFT instances private to their voices and preparation/teardown
outside active processing, as implemented.

Cross-thread ownership:
- Formant values: UI/host/state callbacks write two fixed arrays of scalar
  `std::atomic<float>` values. Lock-freedom is compile-time asserted by the
  existing cache. DSP reads only the ratio, avoiding a mixed semitone/ratio
  snapshot. Relaxed ordering suffices for independent scalar values. No buffer
  publication, queue, allocation, or reclamation is introduced by a write.
- Formant FFT, scratch, rings, windows, LPC analysis tables and smoothing:
  created with the voice, tables prepared outside audio, then mutated only by audio.
  Note reset fills fixed arrays; it does not reconstruct FFTs or destroy memory.
  LPC recursion uses three fixed 21-element double arrays on the stack. The
  model envelope is audio-owned; failed/silent analysis invalidates it and the
  next valid frame seeds it afresh. No model or coefficients are published to
  another thread. Destruction occurs when the processor clears voices after
  processing stops.
- Saturation: two previous-input/root pairs and a scalar ramp, owned solely by
  the voice audio path. Reset/neutral saturation invalidates the pairs; the next processed
  frame seeds both channels. No queue, delay buffer, or heap storage is needed.
  The first-order ADAA branch retains one previous sample, not recursive feedback.
- Original samples, metadata and layer maps: loader-created and immutable while
  rendering; synth inventory retains ownership while voices borrow data. Clearing
  a voice's JUCE sound reference cannot destroy a sound still in that inventory.
  Teardown joins the warp worker before clearing the owning sound inventory.
- Warp buffers: unchanged worker-owned fixed slots, release/acquire publication,
  at most two audio pin attempts, and decrement-only audio release. A finishing
  voice keeps its lease through the short drain; replacements cannot free that
  buffer until the lease releases. Retired buffers are reclaimed by the worker.
  Formant adds no lease or snapshot type and does not change cache keys.
- State trees/APVTS serialization remain on existing save/restore paths; the
  render/parameter callback does not copy/replace trees. Editor callbacks return
  immediately on the audio thread. UI components and listener vectors are
  created/removed on the message thread under the existing JUCE listener rules.
- JUCE synth/voice-stealing locks retain the existing lifecycle restrictions.
  Eight voices are fixed after construction; the steal candidate array is
  reserved by JUCE as voices are added. No new concurrent inventory mutation.

Verification:
- Static audit only, repeated after the final LPC and saturation changes.
- Inspected the checked-out JUCE complex FFT implementations: vDSP on macOS and
  the fallback on Windows, inverse normalization, fixed radix-2/radix-4 recursion,
  preallocated plans, and per-instance synchronization.
- Inspected zero/tiny/oversized block handling: zero frames return; positive
  blocks chunk to at most 128 without buffer resizing; hop/ring positions persist
  across MIDI sub-blocks. The drain starts at 1024, strictly decreases, and ends
  before another source render. Host layouts are mono/stereo; internal stereo
  analysis preserves separate channels and avoids antiphase cancellation.
- Inspected frame/ring indexing, DC/Nyquist handling, fractional envelope lookup,
  positive smoothed ratios in 0.5..2, log floors, bounded spectral gain, finite
  input/output handling, and the processor's denormal guard. Correction is
  feed-forward and every ring slot is consumed/cleared; invalid data cannot
  become persistent feedback. All spectral loops are bounded by 512 or 257.
- Inspected model-grid endpoints: analysis indices lie in 0..256, and both
  source/destination envelope lookups are gated by the band taper before any
  float-to-index conversion. At high sample rates only the model band is shifted.
  The model ceiling tracks Nyquist below 16 kHz; invalid/outlandish rates fall
  back to 44.1 kHz for these prepared tables.
- Inspected LPC conditioning: finite peak normalization precedes power squaring;
  non-finite/near-zero energy skips correction. Double-precision recursion runs
  at most 20 steps with 0.1% diagonal loading, reflection coefficients clamped
  to +/-0.98 and an error floor checked before another step. Coefficients are
  bandwidth-expanded and evaluated by FFT; there is no recursive audio filter
  whose poles can run away. Spectral denominator/log floors and +/-24 dB gain
  limits bound the correction. Envelope smoothing only combines finite values.
- Inspected saturation at silence, equal/opposite/large samples, voice reuse,
  and edits through zero. Its rationalized ADAA denominator is at least 2;
  double intermediates safely cover finite float inputs. Bias is subtracted and
  the small-signal gain normalized. Blend stays in 0..0.18 and is smoothed over
  20 ms. At steady neutral the branch returns without touching output; note
  reset invalidates all previous-sample state. The existing drain covers its
  one-sample memory as well as the spectral rings.
- Traced delayed dry identity at neutral, four-way sqrt-Hann overlap normalization,
  zero-input startup, note end/release drain, hard stop/voice reuse, and sample-rate
  preparation. Latency is a constant 512 samples, reported in `prepareToPlay`.
- Inspected JUCE's default bypass latency assertion and the final explicit
  no-input silent bypass override; it only clears existing output storage.
- Inspected registration through selection, editor-closed automation, saving,
  restore with a missing Formant value, and Apply to All. The new host parameter
  is appended, preserving old indices and the legacy parameter slot.
- No build, runtime tests, allocation instrumentation, plugin validation, CPU
  benchmark, or listening test was requested or run, per repository instructions.
  This refinement changes only FormantShifter and documentation; parameter IDs,
  UI layout, build configuration, dependencies, voice chunking and latency stay
  as in the initial Formant implementation.

Residual risks:
- macOS arm64/x86_64/universal and Windows runtime behavior remains untested.
- This is an original character-oriented LPC shifter, not a verified emulation
  of Little AlterBoy. Listening/A-B comparison with the actual samples and
  reference plugin is still needed. No sonic-match claim is made.
- Envelope shifting is approximate, especially for noisy percussion or extreme
  shifts, and can change level, introduce pre-ringing, or soften attacks. The
  model ceiling is fixed in Hz but the 512-frame observation window shortens at
  high sample rates, limiting low-frequency detail.
- Asymmetric saturation deliberately adds harmonics and may create some signal-
  dependent DC during a hit. Zero input returns to zero after its one-sample
  memory. First-order ADAA reduces aliasing but cannot eliminate it; its small
  parallel contribution includes half-sample averaging/high-frequency coloration.
- Every voice now has 512 samples of latency even at Formant 0. Host compensation
  cannot remove live-monitoring delay. Voices occupy a slot for up to 1024 extra
  drain frames; stealing/hard stops discard pending output as documented.
- Existing Rubber Band, long-MIDI, and per-sample transient-shaping costs remain.

Conclusion:
- No new blocking realtime hazard was found in Formant's fixed-storage path.
  The wider reachable plugin path remains FAIL because of the existing warp
  fallback and long-MIDI allocation blockers.
