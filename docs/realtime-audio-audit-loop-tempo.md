# Warp Sample Tempo And Playback Mode

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and `processBlockBypassed`.
- `PercussionSynthesiser` note dispatch and rendering; `PercussionVoice::startNote`,
  `stopNote`, and `renderNextBlock`.
- Audio-thread APVTS callbacks for `sampleLoopBpm` and `sampleLoopTapeMode`.

Reachability inspected:
- Parameter callback -> sample-specific registry -> bounded, lock-free per-note
  atomic stores.
- Note start -> warp metadata and per-note mode/BPM -> original sample renderer
  or existing offline/realtime warp path.
- Loop metadata enables tempo playback even when its legacy `warp` flag is false;
  loop note-off still follows the key-held release path.
- Sounding warp sample -> atomic mode/BPM reads -> source-position-preserving path
  switch -> direct sample playback or cache/live warp.
- Cache worker -> per-note target/mode reads -> off-thread Rubber Band render ->
  pinned cache publication and worker-side reclamation.
- Ratio uses source BPM / target BPM for both loop and non-loop warp samples.
  The source BPM comes from JSON when present and falls back to 153 otherwise.
- Non-loop warp samples use the direct renderer's finite end position in speed
  and pitch mode; only `loop=true` sources wrap while held.
- Direct renderer -> bounded render frames, region wrapping, interpolated reads,
  source-time fades, and existing voice DSP.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:78`, `:377`
Call path: a pitch-preserving warp sample starts before its cache is ready, changes mode
from direct playback, changes target BPM, or wraps on the live path ->
`RubberBandStretcher::reset` -> R2 reset/reconfigure.
Issue: The existing realtime Rubber Band fallback can allocate and release
memory on the audio thread.
Why it matters: Cache misses and live loop wraps can cause callback-time heap
work. The new direct speed/pitch mode avoids this path while selected, but
pitch-preserving mode still reaches it.
Minimal fix: Replace or prepare the live fallback so its reset/render operations
have proven bounded, allocation-free behavior.

[RISK] `Source/PercussionSynthesiser.cpp:12`, `:19`
Call path: processor render -> JUCE synthesiser note dispatch/render.
Issue: JUCE's synthesiser uses a `CriticalSection`; concurrent sound or voice
inventory mutation would contend with audio. The loaded inventory is immutable
after construction in this implementation.

Cross-thread ownership:
- Samples, waveform summaries and JSON metadata: constructed before audio starts;
  immutable thereafter; owned by the processor's synthesiser; read by audio,
  worker and UI; destroyed after audio/worker shutdown.
- Per-note warp BPM/mode: written through fixed scalar atomics by UI/host state
  and automation; read by voices and cache worker; no object publication.
- Warp caches: allocated by worker, published in fixed slots, pinned by voices;
  old buffers are reclaimed by the worker after readers leave.
- Editor labels and controls: message-thread only; no audio-thread UI access.

Verification:
- Static audit only. Build and tests were not requested by the project workflow.

Residual risks:
- The existing Rubber Band R2 live fallback remains a realtime blocker.
- Mode changes can enter that fallback until the matching offline cache is ready.

Conclusion:
- The new scalar controls and direct playback path are bounded and lock-free,
  but pitch-preserving playback still reaches the existing R2 allocation path.
