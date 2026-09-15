# Sample-specific Panorama

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and float `processBlockBypassed`.
- Synth MIDI dispatch, `PercussionVoice::startNote`, `stopNote`,
  `renderNextBlock`, source completion, voice stealing and effect-tail drain.
- Processor/editor parameter callbacks, with preparation, selection, Apply to
  All, state save/restore and teardown as dependencies.

Reachability inspected:
- APVTS Panorama change -> six-entry registry -> finite/clamped/integer-snapped
  per-note atomic -> voice target read -> linear smoother -> stereo pan matrix.
- Original/cached/realtime-warp source -> fixed stereo/128-frame scratch ->
  PSOLA -> coloration -> Mono -> Panorama -> voice mix -> global Rzhavchina.
- Note start -> bounded sample rate -> reset/seed 10 ms pan ramp and scalar
  normalization cache. Mono output buses advance the ramp but bypass the matrix.
- State restore -> rebuild per-note cache with zero for missing Panorama ->
  synchronize selected APVTS value. Save mirrors the cache before `copyState()`.
- Value editor -> bounded text parser -> ordinary slider gesture; host/audio
  callbacks do not parse text. Selection discards unfinished input.
- Existing transitive source, formant, ownership and framework dependencies
  retain the behavior inspected in [the Mono audit](realtime-audio-audit-mono.md).

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:129`, `:157`, `:340`
Call path: voice start/cache transition -> realtime warp start/render -> prepare,
buffer resizing and repeated input feeding.
Issue: The existing fallback can rebuild its engine, grow audio buffers and
repeat feeding without a small fixed per-callback work limit.
Why it matters: Heap work and uncontrolled callback duration can cause dropouts.
Minimal fix: Prepare storage/configurations off audio and bound fallback work,
or move fallback rendering off audio. This predates Panorama and is outside its
scope; the relevant start/render/ensureBuffers paths were re-inspected.

[BLOCKER] `Source/PluginProcessor.cpp:316`, `:318`
Call path: activity handling and JUCE synth MIDI dispatch -> owning
`juce::MidiMessage` construction/destruction from MIDI metadata.
Issue: Long host MIDI/SysEx messages can allocate and free storage on audio.
Why it matters: Unsupported messages can trigger heap work before filtering.
Minimal fix: Filter unsupported long messages using non-owning metadata before
both activity handling and synth dispatch. This is an existing blocker.

[RISK] JUCE synthesis/APVTS/attachment/FFT dependencies
Call path: automation -> parameter listeners/attachment notification; synth
render -> framework locks and private voice FFT backend.
Issue: Existing JUCE mechanisms use locks and asynchronous attachment updates.
Windows' fallback complex FFT uses a private instance SpinLock; macOS uses
prepared vDSP plans. Panorama follows the existing attachment mechanism.
Why it matters: Concurrent inventory mutation or DSP preparation would break
the current non-contention assumptions.
Minimal fix: Preserve stopped preparation/teardown and private voice ownership;
assess framework notification contention separately.

Cross-thread ownership:
- Panorama values: processor-owned fixed array of 128 scalar float atomics.
  UI/host/state/Apply to All writes publish independent values with relaxed
  stores; audio reads only the resolved root note. Float lock-freedom is
  compile-time asserted. Replacements transfer no resources and need no cleanup.
- Registry and sample groups: constructed before playback and then immutable;
  selection is atomic. Registry callbacks are allocation-free and `noexcept`.
  Render/parameter callbacks do not access the saved ValueTree.
- Pan smoother and normalization: direct voice-owned scalar state, mutated by
  audio. Scratch retains its preallocated two-channel/128-frame capacity.
  Panorama introduces no buffers, jobs, queues or reference-counted ownership.
- Original sounds/metadata/layer maps: loader-created and retained by the
  immutable synth inventory while voices run. A voice release cannot destroy
  the inventory's sound. Teardown stops/clears voices, joins the warp worker,
  then releases original sounds and metadata.
- Warp snapshots: worker-owned fixed slots publish immutable buffers through
  release/acquire ordering. Audio pinning has at most two attempts; release only
  decrements readers. Worker reclamation waits for zero readers, so replacement
  cannot destroy buffers read by active or draining voices.
- Existing PSOLA rings, detectors, FFT plans and coloration arrays remain
  voice-owned, prepared and destroyed off render. Panorama changes no history.
- Editor components/attachments: constructed and destroyed on the message
  thread. Formatting/parsing stays there; edit-tracking callbacks return
  immediately when invoked on another thread.

Verification:
- Static audit only, after final relevant source changes. Reviewed Panorama
  changes against the pre-edit snapshot to preserve other working-tree edits.
- Inspected JUCE's scalar SmoothedValue reset/target/advance and slider interval
  handling. Unchanged targets do not restart ramps; completion reaches the exact
  target. Each pan position uses the same smoother value for both channels.
- Note-start rate validation bounds the 10 ms ramp to at most 7680 samples;
  host-rate changes stop existing voices before subsequent note-start resets.
  Render handles zero blocks, single-frame sub-blocks and larger host blocks
  through the existing fixed scratch chunks. Tail drain decreases each chunk.
- The matrix adds constant scalar work per frame. At steady pan, normalization
  is cached; during movement its square-root argument is bounded to 1..2.
  Double intermediates avoid float addition overflow, and final non-finite
  results are replaced with zero. There is no feedback or added latency.
- Hand-derived matrix checks: centre is identity; full right is
  `(0, (L+R)/sqrt(2))`, full left is its mirror. With `L=R=M`, output powers
  sum to `2*M*M` at every position. Both source channels contribute at either
  endpoint. These are algebraic checks, not executed audio tests.
- Parameter/registry integration covers editor-closed automation, sample
  selection, Apply to All and persistence. Panorama is appended after Mono,
  retaining prior host indices; older state defaults to centre.
- The input parser accepts signed integers, case-insensitive `C` and unsigned
  `L`/`R` forms. Length and per-digit bounds prevent integer overflow; incomplete
  forms cannot commit. Formatting follows attachment setup. APVTS and slider
  intervals are 1, including numeric-field/editor/knob arrow handling.
- No builds, executable tests, plugin validation, GUI or listening checks were
  run, following the repository instruction to run these only when asked.

Residual risks:
- Existing warp/long-MIDI blockers prevent a plugin-wide realtime pass. No new
  blocking hazard was found in Panorama's DSP or scalar cache.
- macOS arm64/x86_64 and Windows compilation/runtime behavior remains untested.
- Constant channel power is defined for dual-mono input. Arbitrary stereo can
  change level when narrowed, and opposing channel content can cancel when
  summed. Hard-panned dual-mono gains approximately 3 dB on its remaining
  channel. Listening and automation-under-load checks remain unperformed.

Conclusion:
- Panorama adds bounded, allocation-free render work. The inspected wider path
  remains FAIL because of the existing warp and long-MIDI blockers.
