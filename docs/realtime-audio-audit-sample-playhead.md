# Sample Playhead

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock`, `processBlockBypassed`, and `releaseResources`.
- `PercussionSynthesiser` MIDI dispatch/render and `PercussionVoice::startNote`,
  `stopNote`, `renderNextBlock`, `renderSourceBlock`, and source completion.

Reachability inspected:
- MIDI note -> voice selection -> note-on sequence increment -> voice playback
  setup -> initial playhead snapshot.
- Original, cached warp, and live warp rendering -> original source time ->
  note-latched trim region -> packed playhead snapshot.
- Source finish, voice clear, bypass, and release -> hidden UI playhead.
- Editor timer -> immutable voice inventory -> atomic snapshot reads -> selected
  MIDI note and newest active voice -> waveform line.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:78`, `:377`
Call path: voice start or live-warp loop wrap -> `RubberBandStretcher::reset`
-> R2 reset/reconfigure.
Issue: The existing live-warp fallback can allocate and free memory on audio.
Why it matters: Cache misses and loop wraps can cause callback-time heap work.
Minimal fix: Prepare or replace the live-warp fallback with a render path whose
reset and loop wrap are allocation-free.

[RISK] `Source/PercussionSynthesiser.cpp:12`, `:19`
Call path: processor render -> synthesiser dispatch/render under `CriticalSection`.
Issue: Mutating voice or sound inventory during playback could contend with audio.
Why it matters: A contended lock can miss the callback deadline.
Minimal fix: Keep inventory fixed while rendering, as the current constructor-only
loading and voice setup do.

Cross-thread ownership:
- Playhead counter and per-voice snapshots: processor constructs the counter
  before the synthesiser; audio writes the counter and snapshots using lock-free
  64-bit atomics, and the message thread reads snapshots. Neither thread owns
  heap storage through these values. The processor destroys the synthesiser
  before its counter. Snapshots are cleared when voices finish or are reused.
- Voice and group inventory: processor construction creates/finalizes the fixed
  collection before editor polling. The message thread only borrows voice
  pointers; no runtime replacement or reclamation is introduced by this feature.
- Source buffers, metadata and warp caches: audio reads the existing immutable
  sounds and pinned cache leases. The playhead publishes scalar position data
  only; it does not extend the lifetime of these objects or alter reclamation.

Verification:
- Static audit only; repository instructions reserve builds and validation for
  an explicit request.
- Inspected zero and one-frame trim regions, non-finite source time/rate,
  differing variation lengths, direct/cache/live-warp time conversion, voice
  reuse, bypass, and zero-length audio blocks. Snapshot math is bounded and
  performs no allocation, lock, UI call or file I/O. The eight-voice UI scan is
  bounded and runs on the message thread.
- Inspected the existing Rubber Band reset call sites and synthesiser lock.

Residual risks:
- The pre-existing live-warp reset blocker prevents an overall realtime PASS.
- The line updates at 30 Hz, so hits that start and finish between timer ticks
  may not be displayed. Build and host GUI behavior remain unverified.

Conclusion:
- The new playhead publication is lock-free and bounded, while the existing
  live-warp reset path leaves the inspected render path at FAIL.
