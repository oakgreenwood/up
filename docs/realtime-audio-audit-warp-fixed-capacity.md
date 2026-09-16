# Preparation-only warp resources and fixed-capacity processing

Realtime audio audit: FAIL

## Audited realtime entry points

- `AudioPluginAudioProcessor::processBlock` -> JUCE synth MIDI dispatch and
  `PercussionVoice::startNote`, `stopNote`, and `renderNextBlock`, scoped to
  realtime warp resource ownership, cache transitions and buffer access.
- Float `processBlockBypassed` clears output and does not enter warp processing.
- Preparation and teardown were inspected as the non-render partners owning
  engines, scratch storage, original sounds and warp-cache slots.

## Reachability inspected

- `prepareToPlay -> prepareRealtimeWarpResources -> RealtimeWarpPlayer::prepare`.
- `startNote -> beginPlayback -> RealtimeWarpPlayer::start` after a cache miss.
- `renderNextBlock -> renderSourceBlock -> maybeSwitchWarpCacheToRealtime /
  maybeSwitchLengthPreservedPitchToRealtime -> switchToRealtimeWarpFromOriginal
  -> RealtimeWarpPlayer::start`.
- `renderSourceBlock -> RealtimeWarpPlayer::render -> setRubberBandRates /
  getSamplesRequired / process / available / retrieve / resetForLoop`.
- R2 `reset / setPitchScale / setTimeRatio -> reconfigure -> ChannelData`
  buffer sizing; R2 output-ring expansion and emergency scavenging.
- Cache/original transitions and voice completion -> warp lease release;
  worker publication/reclamation; processor teardown -> clear voices -> join
  worker -> clear original sounds.

## Findings

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:328`, `:345`
Call path: fallback start, live rate changes or loop restart -> Rubber Band R2
reset/reconfiguration; rendering -> R2 processing/output-ring expansion.
Issue: The vendored library still has internal allocation and destruction paths.
`R2Stretcher::reset` calls `m_emergencyScavenger.scavenge()` and `reconfigure()`;
ratio setters also reconfigure. `ChannelData::setOutbufSize` can allocate and
delete ring buffers, while processing can grow its output ring and retire the
old allocation. Emergency reclamation can also lock its excess-object list.
Why it matters: Prebuilding the engine objects and bounding wrapper buffers does
not make every call into those engines allocation-free or non-blocking.
Minimal fix: Prepare and enforce sufficient internal capacities and safe reset/
reclamation for the supported operating range. This is the separately deferred
Rubber Band internal-allocation work, outside the two requested changes.

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:154`
Call path: render -> feed input -> source end -> resetForLoop -> repeat.
Issue: The existing feed/reset loop has no practical per-callback work limit.
A very short held loop can repeatedly reset the engine before it emits audio.
Each individual copy is now bounded, but the number of feed/reset iterations is
not bounded by a processing budget.
Why it matters: A callback can overrun its deadline independently of allocation.
Minimal fix: Define a work budget and an explicit exhaustion policy. This is the
separately deferred processing-budget change.

## Cross-thread ownership

- Engines and scratch: voice-owned. With rendering stopped, preparation creates
  supported channel configurations, replaces engines after sample-rate changes,
  and sizes shared scratch storage. During playback, `start` only borrows a raw
  pointer from the stable `unique_ptr` array. It never transfers or releases
  ownership. The cache worker and message thread never access these engines.
  Preparation clears the borrowed pointer before any replacement. Processor
  teardown stops playback and destroys voices before releasing original sounds.
  Engine-owned internal reclamation remains the blocker above.
- Original sample buffers and metadata: created during loading, published before
  playback/worker startup, and immutable while either reads them. Voices borrow
  them through the sampler's retained sounds. They are freed after voices stop
  and the cache worker is joined; this change adds no publication or mutation.
- Warp caches: the worker creates immutable buffers in fixed slots and publishes
  indices with release ordering. Audio acquires with bounded atomic reader pins
  and releases only reader counts. A replacement leaves older pinned buffers
  alive; the worker reclaims them after readers leave. Engine selection does not
  alter this protocol or destroy cache buffers on audio.
- Playback controls: host BPM, tempo sync and sample-specific values retain
  their existing scalar atomic publication. Playback reads those values; no
  APVTS tree operations, UI calls, queues or new synchronization were introduced.

## Verification

- Static audit after the final relevant implementation changes. No build,
  automated tests, sanitizer, allocation instrumentation, plugin validation,
  listening comparison or runtime profiling was requested or run.
- Inspected the complete diff and preparation/start/render call sites. Wrapper
  `make_unique`, owning-pointer replacement/reset and `AudioBuffer::setSize`
  occur only in `prepare`; `ensureBuffers` and the call from `start` to `prepare`
  are removed. `isReady` is checked only for an active fallback after start.
- Traced first start, repeated starts, mono/stereo voice reuse, same-rate
  preparation, changed-rate preparation, unavailable configurations and invalid
  preparation rates. Start rejects a rate mismatch instead of rebuilding.
- Traced zero, one-frame, ordinary and larger-than-capacity output requests;
  input requests larger than capacity; final source tails; loop wrap and
  source/engine channel mismatches. Source copies are bounded by both source
  remainder and scratch capacity, before converting the frame count to `int`.
  Output retrieval is capped independently; mixing advances by its actual
  returned count, and a zero retrieval returns without spinning.
- Inspected the vendored R2 input-consumption and retrieval implementations.
  Partial input feeds accumulate in its input ring; the final flag is set only
  on the actual final source piece. The existing pull protocol follows
  `getSamplesRequired`; no new `setMaxProcessSize` configuration is required.
  The existing minimum one-frame request handling is retained.
- No platform-specific API, threading, state serialization, cache policy,
  pitch debounce, ratio formula or audio processing options were changed.

## Residual risks

- Both mono and stereo engines are now prepared per voice, increasing initial
  memory and preparation cost compared with a single stereo engine.
- Rubber Band's internal allocations and the feed/reset work bound remain
  unresolved. This is not a plugin-wide realtime-safety pass; previously
  documented unrelated render-path blockers also remain outside this scope.
- Smaller input feeds may affect processing scheduling and CPU overhead when
  engine demand exceeds capacity. Audio equivalence, performance and build
  behavior remain unverified on macOS arm64/x86_64 and Windows.

## Conclusion

The requested wrapper-owned preparation and fixed-capacity changes are complete.
The inspected fallback remains `FAIL` because of the explicitly deferred Rubber
Band internal allocation/reclamation and unbounded feed/reset work.
