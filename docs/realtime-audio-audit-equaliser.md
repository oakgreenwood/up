# Sample-specific four-band EQ and spectrum

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and float `processBlockBypassed`.
- JUCE synthesis/MIDI dispatch, `PercussionVoice::startNote`, `stopNote`,
  `renderNextBlock`, source completion, tail drain and voice stealing.
- Sample-specific parameter callbacks, with preparation, UI gestures, selection,
  Apply to All, save/restore and teardown as publication/lifetime partners.

Reachability inspected:
- Parameter callback -> 14-entry registry -> four frequency/four gain per-note
  atomic arrays -> voice `updateEqualiser` -> `setBand` -> JUCE
  `IIR::ArrayCoefficients<double>` shelf/peak factories.
- Original/cached/realtime-warp source -> existing stereo/128-frame scratch ->
  PSOLA -> coloration -> Mono -> Panorama -> four stereo biquads -> output mix.
- Voice post-EQ output -> `SampleSpectrum::add` at host offsets -> `endBlock`
  -> fixed rolling stereo window -> bounded SPSC queue -> editor-only FFT.
- `beginBlock` snapshots selected root note, capture enable and sample rate;
  bypass invalidates partial capture. UI reads the same sample rate for graph
  limits/response, then polls parameters and consumes complete spectrum frames.
- Note start clears EQ histories and seeds current targets; preparation clears
  voices before updating rate-dependent state; source completion/live tail edit
  reserves at most one extra 0.5-second EQ drain.
- Existing source renderer, sample ownership, warp acquisition/release,
  Rubber Band start/render/reconfigure and long-MIDI dispatch. The unchanged
  formant/global DSP retain the dependencies documented in their prior audits;
  this report does not newly certify every unrelated DSP implementation.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:154`, `:328`, `:345`
Call path: voice fallback start/live ratio changes/loop reset -> Rubber Band R2
reset/reconfiguration; render -> repeated feeding until output is available.
Issue: Existing R2 internals can allocate and reclaim buffers/windows/resamplers;
reset invokes emergency scavenging, and the wrapper feeding loop still lacks a
practical per-callback work budget. Re-inspected the vendored implementations.
Why it matters: Prepared wrapper buffers do not bound internal heap activity,
possible scavenger contention or callback duration.
Minimal fix: Bound library capacities/reclamation and fallback work, or prepare
fallback audio off thread. These pre-existing issues are outside the EQ change;
see [the fixed-capacity warp audit](realtime-audio-audit-warp-fixed-capacity.md).

[BLOCKER] `Source/PluginProcessor.cpp:364`, `:370`
Call path: activity handling and JUCE synth dispatch -> MIDI metadata.getMessage
-> owning MidiMessage construction/destruction.
Issue: Existing long MIDI/SysEx messages can allocate and free storage on audio.
JUCE's synth independently performs the same owning conversion.
Why it matters: Unsupported host messages can invoke heap work before filtering.
Minimal fix: Filter unsupported long messages through non-owning metadata before
both activity handling and synth dispatch. This predates the EQ.

[RISK] JUCE synthesis, APVTS callbacks and existing voice FFTs
Call path: render/note dispatch -> Synthesiser CriticalSection; host parameter
changes -> framework listeners; existing formant processing -> private FFT.
Issue: Existing framework synchronization remains. Inventory/preparation must
stay outside active rendering. Platform FFT backend mechanisms in the existing
formant effect are unchanged; the newly added spectrum FFT runs only on UI.
Why it matters: This is not a plugin-wide lock-free guarantee.
Minimal fix: Preserve stopped preparation/teardown and private DSP ownership;
assess existing framework contention separately.

[PERF] `Source/PercussionVoice.cpp:94`, `Source/Effects/SampleEqualiser.cpp`
Call path: source completion -> EQ drain; voice render -> four stereo biquads.
Issue: Each non-neutral or decaying band adds fixed per-frame double-precision
filter work and up to 0.5 seconds of additional voice drain, retaining the
existing formant chain during that time. Settled neutral bands bypass exactly.
Why it matters: CPU and voice-stealing behavior under fast repeated hits need
runtime profiling; coefficient changes also do bounded transcendental work.
Minimal fix: Profile the authorized implementation before deciding whether any
further optimization is needed. No new worker or optimization layer was added.

[PERF] `Source/Effects/SampleSpectrum.h`, `Source/UI/SampleEqualiserEditor.cpp`
Call path: capture -> overlapping windows; independent 60 Hz UI timer -> FFT
and cached spectrum polygon.
Issue: Complete stereo windows now publish every 512 samples after the initial
2048, with at most two 2048-point FFTs per UI tick. Successful publication copies
about 1.54 MB/s at 48 kHz when the queue has space. Drawing uses two segments per
logical pixel, capped at 2048, and preserves FFT peaks between display points.
Why it matters: This bounds the added work but does not establish measured CPU
cost or guarantee 60 rendered frames per second on a busy message thread.
Minimal fix: Profile runtime CPU/frame pacing when requested. Response geometry
is cached until parameters or geometry change; unchanged silence skips repaint.

Cross-thread ownership:
- EQ values: processor-owned fixed 4 x 128 frequency and gain atomic arrays.
  UI, host automation and state/Apply to All write finite/clamped scalar values;
  audio reads by the sound's immutable MIDI root note. Float lock-freedom is
  compile-time asserted. Independent scalar publication needs no reclamation.
  The eight values are not an atomic transaction; smoothing follows each target.
- EQ coefficients, ramps and channel histories: direct voice members, modified
  only by audio or stopped preparation. Coefficient factories return fixed arrays
  by value; no reference-counted coefficient assignment/destruction occurs.
  Replacing them destroys only trivial scalar storage. Audio reads no ValueTree.
- Spectrum capture/partial frame: processor-owned fixed arrays, touched only by
  the single audio producer. Voices hold borrowed pointers; processor teardown
  explicitly clears voices while this storage is alive. The UI never reads
  these mutable audio-owned arrays. The rolling index stays in 0..2047. Each
  successful publication uses four bounded array copies to produce a complete
  chronological stereo window; no intermediate shared scratch is introduced.
- Spectrum packets: four processor-owned slots. Audio writes a free slot then
  release-stores its monotonically wrapping uint32 write index. The sole editor
  consumer acquire-loads that index, copies the complete packet, then release-
  stores its read index. Producer acquire-loads read before reuse. The unsigned
  distance remains at most four, including counter wrap. A full queue drops the
  new whole packet; audio never waits, retries, resizes or overwrites a reader.
  There are no dynamic resources inside packets and no audio cleanup fallback.
- Spectrum enable/rate: lock-free scalar atomics; UI writes enable, stopped
  preparation writes rate, audio and UI read. Changing selection/rate or
  disabling capture resets audio's partial frame on the next callback. Queue
  indices are never reset concurrently. Packets carry note/rate tags; selection
  drains stale queued frames and clears the displayed spectrum on the UI thread.
- Editor graph, paths, window and FFT: created/destroyed on the message thread.
  Frame copying and transforms occur there. Closing an editor disables capture;
  processor-owned storage remains alive during any in-flight audio callback.
  This uses the plugin's usual single active editor/consumer lifecycle.
- Original sounds, metadata and inventory: loaded before playback and immutable
  while rendered. The synth retains sounds while voices borrow them. Teardown
  clears voices, joins the warp worker, then releases originals/metadata.
- Warp snapshots: worker-owned fixed slots and immutable buffers, release/acquire
  publication and at most two reader-pin attempts. Audio release decrements a
  scalar reader count; the worker reclaims only after all readers leave. Longer
  EQ drain keeps the existing lease alive; it does not transfer ownership.
- Existing formant rings/plans and global DSP: remain voice/processor-owned;
  preparation/destruction occur off render. EQ adds no shared ownership there.

Verification:
- Re-audited after the dot/curve alignment, border-to-border plot, independent
  60 Hz timer, denser polygon and overlapping analysis changes. The producer
  retains fixed storage and release/acquire queue ownership; no new audio
  allocation, lock, cleanup or retry was found. Note/rate changes, bypass and
  disabled/oversized capture reset the rolling window and hop counters; a valid
  zero-length block preserves them. FFT, path allocation and geometry caching
  remain on the message thread. This follow-up used source inspection only.
- Subsequent UI-only revision: dots display each band's own frequency and gain,
  independently of the combined response path, without connector lines. Vertical
  dragging directly maps to that band's +/-15 dB gain range. This removes curve
  vertex insertion and response compensation from message-thread drawing and
  dragging; audio DSP, parameter publication and spectrum handoff are unchanged.
  The 2.5:1 panel retains opaque #8E8B8B spectrum and black dots/curve/border.
- Re-audited after the transparent/black graph restyle and fixed-Q change to
  1.0. The shared coefficient factory supplies Q 1.0 to both shelves and both
  bells in audio and to the UI response curve. Inspected JUCE's value-returning
  shelf/peak formulas, voice target/reset/render paths and unchanged MIDI/warp
  blockers. Q remains finite and positive; the existing frequency/gain bounds,
  coefficient checks, fixed storage, ramp length and finite-state guards still
  apply. No new audio ownership, synchronization or allocation was introduced.
  The graph changes execute only on the message thread, using opaque #8E8B8B
  spectrum fill and black paths/dots/border without background, grid or text.
  This follow-up was static inspection only, without build or runtime checks.
- Static audit only, after final EQ, spectrum, UI and tail changes. Inspected the
  final source/diff, JUCE array coefficient factories, parameter normalization,
  window/graphics APIs, synth MIDI conversion/locking, original playback bounds,
  warp leases and vendored R2 reconfiguration. No build, executable tests,
  pluginval, GUI run, listening test or runtime allocation/CPU profiling was run,
  following the repository instruction to do these only when requested.
- Fixed four bands, two histories per band and five normalized coefficients;
  note/reset operations use constant-size arrays. Target updates avoid rebuilding
  unchanged coefficients. Ramps take 10..7680 frames for validated 1..768 kHz
  rates and snap exactly to their targets. Invalid rates use 44100 Hz.
- Q is positive and fixed at 1.0; gains are finite within +/-15 dB; frequencies
  remain 20 Hz..min(20 kHz, 0.45*rate). Array factory denominators are positive
  in this domain. Generated values must also pass finite checks and the
  second-order Jury conditions. Accepted stable denominator endpoints are
  interpolated inside that convex stability region. Histories use double
  precision; finite/output-range checks reset a poisoned channel, and the
  processor's existing ScopedNoDenormals covers processing.
- All-zero gains and cleared history give an exact bypass: frequency-only edits
  skip coefficient generation, and bypassed bands skip all sample processing.
  A band returning to neutral processes until its recursive state falls below
  1e-15, clears it, then bypasses. The UI uses the same validated factories and
  cascades their magnitudes; handles separately display each band's own gain.
  These are static/algebraic observations, not measured response tests.
- Zero voice blocks return before DSP; one-frame sub-blocks advance ramps once;
  larger host blocks retain existing fixed 128-frame voice chunks and MIDI
  dispatch. Spectrum offsets use the actual host start offset across voices.
  A zero host block preserves partial analysis; blocks above 32768 frames skip
  display capture and reset the partial frame without restricting playback.
- Capture is at most 32768 stereo frames per block, with at most 64 publication
  opportunities for complete 2048-frame windows at a 512-frame hop. Publication
  and overflow have no retry loops. UI consumption is bounded to four frames per
  timer tick and two FFTs for the newest matching packet. Finite UI input/power
  checks prevent invalid plot coordinates. Spectrum geometry has at most 2048
  segments; peak scanning follows monotonically increasing FFT bin positions.
- Per-note state, selection, old-state defaults and editor-closed automation all
  use the existing registry. Apply to All snapshots the complete grouped EQ;
  existing scalar effects retain single-parameter behavior. Dot drags bracket
  both parameter gestures and stop before a manual/MIDI-driven group switch.
- CMake adds only the native sources to the existing cross-platform target;
  existing JUCE DSP is reused. No Windows/macOS-specific API was introduced.

Residual risks:
- Existing Rubber Band and long-MIDI blockers remain unresolved. No new blocking
  allocation, lock, resource reclamation or shared-buffer race was identified in
  the EQ or spectrum paths by static inspection.
- macOS arm64/x86_64/universal and Windows compilation/runtime remain untested.
  Listening under rapid automation, worst-case filter movement, tail decay,
  polyphony/CPU and actual UI interaction remain unverified.
- Combined boosts can exceed one band's +/-15 dB; the graph clips the combined
  curve to its gain display bounds. No limiter or automatic gain is added.
- Spectrum resolution is limited by its 2048-point FFT. Stalled UI/full queues
  drop frames; host blocks above 32768 frames skip spectrum capture. Neither
  condition changes playback. The fixed 0.5-second IIR drain is an approximation.

Conclusion:
- EQ and spectrum add bounded, allocation-free audio work under static inspection;
  the inspected wider path remains FAIL because of the existing blockers above.
