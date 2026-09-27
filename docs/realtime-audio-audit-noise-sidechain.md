# Noise pump/follow realtime audit

The later [waveform/trim audit](realtime-audio-audit-noise-waveform.md) covers the
additional start/end parameters, region fades and immutable waveform summaries.

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock`, `processBlockBypassed` and tail query.
- `PercussionSynthesiser::renderAudioRange` / `dispatchMidi`, existing note and
  controller callbacks, and `PercussionVoice::renderNextBlock`.
- APVTS scalar reads, with construction, preparation/release, state, editor
  attachment and teardown paths inspected as publication/lifetime partners.

Reachability inspected:
- Host block -> cached scalar loads -> streamed MIDI metadata -> bounded short
  MidiMessage -> activity atomics / synth dispatch -> existing voice callbacks.
- Host chunk -> sidechain begin -> synth render range -> each voice's existing
  source/Formant/Mono/Pan/EQ chain -> post-EQ magnitude capture at host offsets.
- Sidechain finish -> peak/activity attack-release followers -> smoothed bipolar
  amount -> fixed modulation array -> noise gain multiplication and output mix.
- Chunk view -> existing Rzhavchina and OTT processing -> existing looped noise.
- Bypass and prepare/release -> scalar envelope, hold and playhead resets.
- JUCE's local MIDI iterator, inline message storage, buffer-view channel storage,
  SmoothedValue, synthesis dispatch/locking and voice lifetime implementations.
- Existing realtime-warp start/reset/reconfigure/feeding and vendored R2 reset
  scavenging/window allocation. Unchanged percussion DSP remains covered by the
  [EQ audit](realtime-audio-audit-equaliser.md) and its linked audits, rather than
  being newly certified in full by this feature audit.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:154`, `:328`, `:345`
Call path: voice fallback start / live ratio change / loop reset -> Rubber Band
R2 reset/reconfiguration; render -> repeated feeding until output is available.
Issue: Existing reset scavenges resources, reconfiguration can allocate
windows/resamplers, and fallback feeding lacks a practical callback work budget.
Why it matters: Fixed wrapper buffers and the new 512-frame outer chunks do not
bound the library's internal allocation/reclamation or work required for output.
Minimal fix: Bound internal capacities/reclamation and feeding work, or prepare
fallback audio off thread. These pre-existing blockers remain unresolved and
outside the noise sidechain implementation.

[RISK] `Source/PercussionSynthesiser.cpp:6`, `:14`
Call path: chunk render / MIDI dispatch -> inherited JUCE CriticalSection.
Issue: Existing synth synchronization remains, now around explicit audio ranges
and individual dispatch calls; note callbacks can recursively enter the same
framework lock. Inventory/preparation changes must not occur during rendering.
Why it matters: This is not a whole-plugin lock-free guarantee. Extra chunk
boundaries add fixed framework call/lock overhead.
Minimal fix: Preserve stopped initialization/preparation/teardown and assess
existing framework contention separately; do not add UI/loader synth mutation.

[PERF] `Source/Effects/NoiseSidechain.h:59`
Call path: every host chunk -> two envelope followers and amount smoothing.
Issue: Detector work continues at neutral amount and muted base noise gain so
envelopes already reflect current audio when modulation/noise is enabled.
Why it matters: It adds bounded per-frame arithmetic plus per-voice magnitude
capture. Large blocks add one synth range call per 512 frames, plus MIDI splits.
Minimal fix: Profile the feature before deciding whether further optimization
is warranted; no unrelated optimization or worker was added.

Cross-thread ownership:
- Sidechain values, envelopes, hold, coefficients and amount smoother are direct
  processor-owned members. Audio alone clears/writes/reads the 512-float array:
  voices accumulate levels, then finish overwrites levels with modulation, then
  noise consumes them before the next clear. No other thread sees this storage.
  Preparation/release reset only outside active rendering; bypass resets on
  audio. Voice pointers are connected during stopped setup. Voices are explicitly
  destroyed before the detector at processor teardown. No queue or reclamation
  protocol is necessary and source/trigger changes replace no resource.
- APVTS owns the four new scalar atomics for the processor lifetime. UI/host/state
  publish targets; audio reads cached pointers once per host block. Float atomic
  lock-freedom is asserted by the pinned JUCE implementation. Independent scalar
  changes require no snapshot destruction. Tree copying/replacing and stable
  trigger-key remapping remain in state callbacks, outside the new render code.
- Trigger inventory is a const processor-owned vector prepared before rendering.
  Resource parsing, maps, labels and vector allocation happen during construction
  or UI setup. Audio reads one bounded index and MIDI root note. No inventory
  mutation or resource-name lookup occurs on audio; destruction is stopped.
- Noise audio retains the initial immutable-buffer lifecycle documented in the
  [noise audit](realtime-audio-audit-noise.md). Per-frame modulation borrows the
  detector array only during its current chunk. Muting modulation does not free,
  reset or exchange noise buffers, and the noise playhead continues normally.
- Percussion sources, warp snapshots and existing effects retain their previous
  ownership. The synth holds original sounds while voices borrow them; warp
  leases release scalar reader pins and the worker reclaims retired buffers.
- New controls/formatters/renderer execute on the message thread. Attachments
  are destroyed before controls, and custom look-and-feel pointers are cleared
  before its destruction. Closing the editor does not reset audio state.

Verification:
- Static audit only, after final relevant implementation changes. No build,
  executable test, pluginval, UI run, listening test or runtime allocation/CPU
  profile was run, following the repository's build/validation-only-on-request
  instruction. macOS and Windows compilation remain unverified.
- Zero blocks dispatch pending short MIDI without advancing audio/envelopes.
  One-frame, changing and oversized blocks use the same <=512-frame storage.
  Chunk views explicitly cap channel pointers at two, using JUCE's inline array;
  construction/destruction owns no sample storage. Original host offsets remain
  valid for spectrum capture, which retains its independent large-block limit.
- The MIDI iterator advances exactly once per event, without copying a MidiBuffer
  or walking its event count repeatedly. The inner loop advances the iterator;
  the outer loop advances by a positive bounded chunk. Default 32-frame MIDI
  coalescing is tracked independently of chunk boundaries. Boundary/end events
  are dispatched once; future events are not handed repeatedly to JUCE.
- The former long-MIDI allocation blocker is removed from the active processor
  path: metadata longer than three bytes never constructs an owning MidiMessage,
  and chunk rendering calls renderVoices without JUCE's MIDI-buffer conversion.
  Short messages fit JUCE's inline storage on supported macOS/Windows targets.
- Synth variation maps/vectors are prepared in finalizeLayerMappings and unchanged
  while playing; registered notes find existing entries/capacity. Noise capture
  introduces no map operation or additional voice/reference-count ownership.
- Capture validates offsets, finite samples and selected roots. Magnitudes are
  clamped before summation; detector targets stay in [0, 1]. Rates are finite
  in 1..768 kHz, times in 1..2000 ms, one-pole coefficients in (0, 1), and the
  hold count is bounded to 5..3840 frames. Exponentials run only on time/rate
  changes. Amount smoothing and output clamping keep modulation in [0, 1].
- Static endpoint evaluation gives exact unity at amount zero, `1 - activity`
  at full Pump, and the level envelope at full Follow. Snap-to-target within
  1e-5 gives exact settled zero/unity; the existing denormal guard covers render.
  Attack/release and 10 ms amount ramps preserve continuity across chunks.
- Re-inspected after seeding amount directly on the first parameter read after
  prepare/release/bypass. This prevents a brief neutral-noise blend when starting
  full Follow without a trigger. The initialization flag is audio-owned scalar
  state and adds no allocation, ownership change or cross-thread publication.
- Parameters are appended, old-state defaults restore neutral modulation,
  stable group keys remap saved triggers, and all four controls are excluded
  from the sample-specific registry. Centre-filled drawing is message-only.

Residual risks:
- Rubber Band realtime blockers remain. No new blocking allocation, destruction,
  buffer growth, shared mutation or unbounded loop was found in noise modulation.
- Listening/host checks remain needed for the fixed -80 dB gate floor, 5 ms hold,
  -12 dB follower sensitivity, rapid automation, long release tails and source
  switching. Displayed times cover 99.9% of an envelope step, not a hard gate.
- Chunking preserves DSP state but creates additional render-call boundaries;
  dense MIDI, warp transitions, polyphony and spectrum alignment need runtime
  regression/profiling. No additional latency or new thread is introduced.
- Existing independent APVTS publications are not atomic multi-parameter state
  transactions. Host engine suspension can still stop all plugin processing.

Conclusion:
- New sidechain/render plumbing has bounded storage and allocation-free work
  under static inspection, and long-MIDI heap conversion is removed. The wider
  inspected realtime path remains FAIL due to pre-existing Rubber Band blockers.
