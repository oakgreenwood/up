# Held Loop Trim Updates

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and `processBlockBypassed`.
- `PercussionSynthesiser::renderAudioRange`, MIDI dispatch, and
  `PercussionVoice::startNote`, `stopNote`, and `renderNextBlock`.
- Host parameter callbacks for `sampleStart` and `sampleEnd`.

Reachability inspected:
- Parameter callback -> sample-specific registry -> bounded atomic per-note trim
  values; end duration is derived outside the voice render path.
- Processor render -> voice -> `updateHeldLoopTrim` -> direct/cached renderer or
  `RealtimeWarpPlayer`; all pending bounds are fixed scalar fields in a voice.
- Direct/cached wrap applies the pending start/end within the sample loop;
  source indices and envelope durations are clamped to at least one frame.
- Live warp source-feed wrap adopts the pending range; a new end changes the
  current feed only when it is ahead of both output and fed source positions.
- Notes that are not held loops retain their note-start trim behavior.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:78`, `:404`
Call path: held live-warp loop reaches a source-feed wrap ->
`RubberBandStretcher::reset`.
Issue: The existing Rubber Band R2 fallback may allocate or release memory on
the audio thread during reset.
Why it matters: A loop with no ready offline cache can perform heap work in its
audio callback. This edit does not introduce the reset, but the edited path
continues to reach it.
Minimal fix: Use a proven allocation-free realtime stretch path or avoid
audio-thread Rubber Band reset through prepared, safely published loop buffers.

[RISK] `Source/Playback/RealtimeWarpPlayer.cpp:206`
Call path: held live-warp loop -> buffered stretcher output -> trim update.
Issue: Rubber Band can have already consumed source frames beyond the audible
point. An end behind that feed position is deferred to the next source-feed
wrap, even if it remains ahead of audible playback.
Why it matters: On a live cache miss, the audible end edit can lag the requested
current pass. Direct and offline-cached playback do not have this limitation.
Minimal fix: Track audible loop boundaries independently and restart or replace
the buffered stretch stream without callback-time allocation or discontinuity.

Cross-thread ownership:
- Immutable samples, waveform summaries and metadata are constructed before
  rendering, read by voices and the cache worker, and destroyed after worker
  shutdown and voice cleanup.
- Per-note trim values are scalar atomics written by UI/host callbacks and read
  by voices. Pending and current loop bounds are voice-owned scalar fields.
- Warp caches are worker-created and published through pinned fixed slots; the
  worker reclaims a slot only after voice readers leave.

Verification:
- Static audit only. The project workflow requires builds and validation only
  when requested.

Residual risks:
- Existing live Rubber Band reset remains a realtime blocker.
- Live warp's buffered source feed can delay an end edit until the next repeat.

Conclusion:
- The added trim state is bounded and allocation-free; the reachable live
  Rubber Band reset keeps the full path at FAIL.
