# Parallel one-shot pitch preparation: realtime audio audit

Realtime audio audit: FAIL

Audited realtime entry points:
- `processBlock`, inherited bypass, synth note/controller dispatch, and
  `PercussionVoice::startNote`, `stopNote`, and `renderNextBlock`.
- APVTS `parameterChanged` and the scalar parameters consumed by preparation.
- Playback-rate publication in `prepareToPlay` and coordinated processor teardown.

Reachability inspected:
- Note start -> `beginPlayback -> OneShotPitchCache::recordPlayed / acquire`.
- Voice rendering -> `SamplePlaybackRenderer`, Punch, sustain, ADSR, declicker ->
  `clearActivePlayback -> Lease::reset`.
- Parameter callbacks -> per-note atomic cache. Message-thread Apply to All ->
  the same cache plus non-realtime state-tree writes; no pool submission on audio.
- Coordinator -> `refreshRequests / nextGroupToRender -> renderGroup -> RenderBatch`.
- Six JUCE pool workers -> atomic recording claim -> `renderSound` -> independent
  R3 study/process/retrieve -> buffer validation and engine destruction.
- Pool completion -> coordinator publication/reclamation; cancellation, exception
  cleanup, and shutdown -> join jobs before releasing their borrowed storage.
- Existing warp cache/fallback, JUCE synth/MIDI, and DSP dependencies, as in the
  pitch-mode and recent-warp-cache audits.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:156`, `:339`
Call path: warp cache miss -> realtime start/render -> prepare/reset/ensureBuffers.
Issue: Existing fallback may rebuild engines, resize scratch storage, reclaim
Rubber Band resources, or repeatedly feed/reset a very short loop without a
practical work cap.
Why it matters: Allocation, destruction, and excessive audio-thread work remain
reachable for warp-enabled playback. The one-shot pool does not use this fallback.
Minimal fix: Prepare engine configurations/storage off audio and bound feed/reset
work. This is an existing issue outside the parallel one-shot preparation change.

[BLOCKER] `Source/PluginProcessor.cpp:334`
Call path: MIDI activity and JUCE synth dispatch -> materialize long MIDI messages.
Issue: Long MIDI/SysEx can allocate and free heap-backed message storage.
Why it matters: Hosts can still trigger audio-thread heap work.
Minimal fix: Filter unsupported long messages as views before materialization.

[PERF] `Source/Playback/OneShotPitchCache.cpp:388`
Call path: coordinator -> six parallel R3 renders, alongside the separate warp worker.
Issue: Six one-shot engines can compete for CPU and memory bandwidth while a
seventh engine prepares warp caches. This is a per-plugin-instance bound, not a
global DAW limit. Groups with one recording only use one job.
Why it matters: Shorter multi-variation preparation is expected when CPU capacity
is available, but higher concurrent background load can affect a busy project.
Minimal fix: Measure actual Release-build preparation/audio callback performance
to determine whether six workers improve latency on the target machine; use a
lower worker count if background contention outweighs the benefit.

Cross-thread ownership:
- Source sounds, metadata, and group membership: initialized before the
  coordinator starts and immutable during playback. Pool jobs only read borrowed
  pointers. Teardown clears voices, joins the coordinator and all its jobs, stops
  the pool, then releases source sounds. Member order destroys the pool before
  groups if construction unwinds.
- Recent history: audio does a bounds-checked lookup, one lock-free uint64 fetch
  increment, and one release store per one-shot start. Coordinator reads stamps
  with acquire ordering and computes the top five in fixed arrays. Repeated
  velocity/variation hits promote one MIDI-note group. History is instance-local,
  includes plays with Keep length disabled, and is not serialized.
- Parameters and status: existing scalar atomics publish pitch/mode/rate and
  ready/failure state. All debounce/selection and publication mutation stays on
  the coordinator. Pool jobs read only parameters, their batch, and source data.
  Float/bool/int/double/uint64 audio atomics have compile-time lock-free checks.
- Render batch: coordinator allocates the snapshot and sizes its output vector
  before submitting up to six jobs. A worker-only atomic index gives each
  recording exactly one writer. The vector never changes size during rendering;
  each job has its own engine, scratch buffer, and output-buffer element.
- Completion: JUCE removes finished jobs under its pool lock after `runJob`
  returns. Coordinator observes an empty pool under the same lock before moving
  the completed snapshot, so all buffer writes happen before publication. Pool
  locks, job allocation, waiting, and destruction are never reached from audio.
- Cancellation/failure: jobs check current pitch/mode/rate and batch cancellation
  between 1024-frame chunks, during retrieval, and in chunked finite checks.
  Coordinator checks priority while waiting and observes all requests every
  20 ms. Lower-priority batches yield to pending recent groups; partial output is
  discarded after all jobs exit. Preemption/stale requests are deferred, while
  actual failures preserve the previous cache and suppress repeated failed work.
  Batch RAII cleanup cancels and joins even after partial submission exceptions.
- Published snapshots: existing ten stable slots and bounded two-attempt audio
  acquisition remain. Only the coordinator reserves unpublished zero-reader
  slots, publishes completed groups, and destroys retired snapshots. Audio
  release only decrements a reader count. All variations switch together for
  subsequent hits; already-playing voices retain their pinned snapshot.
- Rubber Band globals: inspected the selected single-file build (vDSP on Apple,
  built-in FFT elsewhere, built-in resampler, NO_TIMING/NO_THREADING). Engines own
  their processing buffers/plans. No project call changes the FFT implementation
  default during rendering. Internal threading stays disabled in each instance.
- JUCE synth/state: workers do not access live voices, the sampler lock, APVTS
  trees, or UI. Existing synth dispatch locking remains a framework mechanism;
  new code adds no competing holder.

Verification:
- Six-worker follow-up: statically rechecked the final `renderWorkerCount = 6`
  against pool construction, the batch job array, submission, cancellation,
  publication, and teardown. Submission remains `min(6, recordingCount)`;
  atomic claims assign each output buffer to one writer and the coordinator
  waits for all jobs before publishing. The ten voice-lease slots depend on
  polyphony, not worker count, and need no expansion. Recent-five ordering,
  Apply to All, R3 options, and audio-side work are unchanged. Concurrent
  engine/scratch memory can increase to six instances. No speedup was measured.
- Static audit after the final relevant implementation changes; inspected current
  source, pinned JUCE pool completion/removal semantics, and Rubber Band internals.
- Manually traced empty/under-five/over-five history, repeated promotion, multiple
  velocities, disabled modes, neutral pitch, failed requests, Apply to All, and
  recent requests arriving during non-recent rendering. All group requests are
  observed together so debounce can finish while another group renders.
- Traced one/two/many recordings, concurrent job completion, stale pitch/rate/mode,
  cancellation during study/process/retrieve/validation, partial job submission,
  publication with old voice leases, and shutdown with queued/running jobs.
- Existing zero/tiny/variable host-block handling remains in voice/render code;
  no host-block-sized storage or history traversal was added to audio. Output
  frame/rate bounds and original Punch timing are retained. Fixed input/output
  chunks avoid signed overflow at final partial blocks.
- `git diff --check` passed. No build, tests, sanitizer, host validation, listening
  comparison, or timing benchmark was run, following AGENTS.md's instruction to
  run builds/validation only when requested. macOS/Windows runtime verification
  remains outstanding.

Residual risks:
- The existing realtime warp fallback and long-MIDI path prevent a plugin-wide pass.
- Background CPU rises while multiple variations render; speedup is unmeasured.
- Repeated recent edits can postpone other groups. A cancelled lower-priority
  batch is restarted later; completed partial recordings are not retained.
- Six workers accelerate variations within one group; separate single-recording
  groups still prepare sequentially. The warp path retains its own R2 worker.
- A fast offline bounce can use the previous prepared pitch while work is pending,
  as with the existing accepted fallback policy.

Conclusion:
- No new blocker was found in recent-history publication, the six-worker batch,
  group publication/reclamation, or latched one-shot playback. The inspected
  render path remains FAIL because of the existing warp/MIDI blockers above.
