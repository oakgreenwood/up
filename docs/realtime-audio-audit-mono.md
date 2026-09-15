# Sample-specific Mono

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and float `processBlockBypassed`.
- Synth MIDI dispatch, `PercussionVoice::startNote`, `stopNote`,
  `renderNextBlock`, stealing, source completion and effect-tail drain.
- Processor/editor parameter callbacks, with construction, preparation,
  selection, Apply to All, state save/restore and teardown as dependencies.

Reachability inspected:
- APVTS Mono change -> five-entry parameter registry -> finite/clamped/snapped
  per-note atomic -> voice target read -> linear smoother -> stereo matrix.
- Original/cached/realtime-warp source -> fixed stereo/128-frame scratch ->
  PSOLA -> LPC coloration/saturation/makeup -> Mono -> output accumulation ->
  global Rzhavchina. Mono also processes the zero-input formant drain.
- Note start -> sanitize sample rate for the 10 ms ramp -> seed the current
  group's Mono amount; zero voice blocks return without advancing the smoother.
- State restore -> rebuild cache using zero for missing Mono values -> synchronize
  selected APVTS parameter. Save mirrors atomics before `parameters.copyState()`.
- Percentage edits -> shared numeric validator -> ordinary slider gesture;
  selection discards unfinished edits. Host callbacks never parse editor text.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:129`, `:157`, `:340`
Call path: voice start/cache transition -> realtime warp start/render -> prepare,
reset, buffer resizing and repeated input feeding.
Issue: The existing fallback can allocate/rebuild an engine, grow audio buffers
and feed repeatedly without a small fixed per-callback work limit.
Why it matters: Heap work and uncontrolled callback duration can cause dropouts.
Minimal fix: Prepare all required configurations/storage off audio and bound
fallback work, or move fallback rendering off audio. These pre-existing changes
are outside the Mono controller's scope.

[BLOCKER] `Source/PluginProcessor.cpp:316`, `:318`
Call path: activity handling and JUCE synth MIDI dispatch -> `metadata.getMessage()`
-> `juce::MidiMessage` allocation/destruction for long messages.
Issue: Long host MIDI/SysEx messages can allocate and free storage on audio.
Why it matters: Unsupported messages still trigger heap work before filtering.
Minimal fix: Filter unsupported long messages through non-owning metadata before
both activity handling and synth dispatch. This is an existing blocker.

[RISK] JUCE synthesis/APVTS/attachment/FFT dependencies
Call path: host automation -> parameter listeners/attachment notification;
render -> JUCE Synthesiser locks and private voice FFT backend.
Issue: Existing JUCE mechanisms use locks and attachment async notifications.
Windows' fallback complex FFT uses a private instance SpinLock; macOS uses
prepared vDSP plans. Adding an ordinary Mono attachment follows the current
parameter system and does not make the whole plugin lock-free.
Why it matters: Concurrent voice/sound inventory mutation or DSP preparation
would invalidate the existing non-contention assumptions.
Minimal fix: Preserve stopped preparation/teardown and private voice ownership;
assess framework notification contention in a separate automation audit.

Cross-thread ownership:
- Mono values: processor-owned fixed array of 128 scalar float atomics,
  initialized during construction. UI/host/state/Apply to All writers publish
  independent values with relaxed stores. Audio reads only the resolved root
  note's scalar. `is_always_lock_free` is asserted for float atomics. Later
  values replace earlier values without any resource transfer or reclamation.
- Registry and sample groups: immutable after loading; the selected group index
  is atomic. The registry's callbacks are allocation-free and `noexcept`; Mono
  adds one bounded entry. The state tree remains outside render/callback reads.
- Mono smoother and scratch: direct voice ownership. The smoother contains only
  scalar state and is mutated by audio. Scratch was allocated at construction
  and remains two channels by 128 frames. No delay line, queue, sample copy,
  background job, new cache buffer or render-time destruction is introduced.
- Original sounds/metadata/layer maps: loader-created data retained by the
  synth's immutable inventory while voices run. Releasing a voice's sound
  reference cannot destroy the inventory's sound. Teardown stops/clears voices,
  joins the warp worker, then releases original sounds and metadata.
- Warp snapshots: worker-owned fixed slots publish immutable buffers with
  release/acquire ordering. Audio acquisition attempts at most two pins and
  release only decrements readers. Worker reclamation waits for zero readers;
  replacement cannot destroy buffers used by active or draining voices.
- Existing PSOLA rings, detector state, FFT plans and coloration arrays retain
  voice ownership and off-render allocation/preparation/destruction. Mono is
  downstream and adds no changes to their history or reset behavior.
- UI labels, image renderer, slider and attachment are editor-owned. Formatting,
  parsing and selection-refresh work stays on the message thread; editor edit
  tracking callbacks return immediately on other threads.

Verification:
- Static audit only, after final implementation changes. Reviewed the change
  against a pre-edit snapshot, retaining unrelated working-tree changes.
- Inspected local JUCE SmoothedValue scalar reset/target/advance implementation,
  synth locking/steal storage, MIDI allocation, parameter notifications and FFT
  backends, plus the source/cache/formant paths and publication/reclamation.
- Algebraic review: zero bypass leaves samples untouched; one produces identical
  `(L + R)/2` outputs; intermediate values scale the stereo difference by
  `1 - amount`. For `(L,R)=(1,0)`, half Mono gives `(0.75,0.25)` and full Mono
  gives `(0.5,0.5)`. Equal inputs remain equal; opposite inputs cancel at full
  Mono. These are hand-derived checks, not executed audio tests.
- The matrix has no feedback, divisions or transcendental operations. Its
  half-scaled sum/difference avoids intermediate overflow from adding unscaled
  float channels. Input reaches Mono through Formant's finite-output guards.
  The cache rejects NaN/Inf and clamps/snaps before publishing. One smoother
  advance drives both channels; a repeated unchanged target does not restart
  JUCE's ramp. New notes seed immediately; live edits ramp across block and
  playback-mode boundaries, including tails. Host sample-rate changes stop
  voices; subsequent note starts reset ramp length using the current rate.
- The added per-frame work is constant; positive voice blocks use at most 128
  scratch frames at a time, including single-frame sub-blocks and blocks larger
  than preparation's hint. Zero blocks return; source-end drain strictly
  decreases. Eight preallocated voices retain their existing lifecycle.
- Mono is appended to the APVTS layout, preserving all previous parameter
  indices. The registry covers editor-closed automation, per-group persistence,
  selection, Apply to All and keyboard edit tracking. Missing old-state values
  resolve to zero through the parameter default.
- Shared percentage validation bounds text length and checks each digit while
  the accumulator is at most 100. Values above 100 cannot wrap an integer into
  the accepted range. Signs, decimals, spaces, letters and misplaced/repeated
  percent signs reject the whole insertion; incomplete input cannot commit a
  value. Formatting/parsing is installed after the slider attachment.
- No builds, executable tests, plugin validation, GUI or listening checks were
  run, following the repository instruction to run these only when asked.

Residual risks:
- The existing warp/long-MIDI blockers prevent a plugin-wide realtime pass.
  The new Mono DSP and scalar cache introduce no blocking hazard found in this
  inspection; existing framework notification mechanisms remain as described.
- macOS arm64/x86_64 and Windows compilation/runtime behavior is untested.
- Conventional mono summing can reduce level or cancel opposing channel content.
  Mono adds no latency or temporal smearing; existing Formant latency and sonic
  behavior remain. Perceptual results and automation under load need listening
  and runtime verification when requested.

Conclusion:
- Mono's added render work is bounded and allocation-free. The inspected wider
  path remains FAIL because of the pre-existing warp and long-MIDI blockers.
