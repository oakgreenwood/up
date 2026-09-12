# Startup and recent warp-cache preparation: realtime audio audit

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and inherited `processBlockBypassed`.
- JUCE synth MIDI dispatch and `PercussionVoice::startNote`, `stopNote`, and
  `renderNextBlock`.
- APVTS pitch/tempo-sync publication consumed by playback and the worker.
- Host timing in `HostTempoTracker::update`; state restore as a producer of
  startup requests consumed by playback's cache worker.

Reachability inspected:
- `processBlock -> HostTempoTracker::update -> AudioPlayHead::getPosition` ->
  optional finite BPM -> `WarpCachePrewarmer::update`.
- State restore -> per-note cache/tempo-sync publication ->
  `requestStartupPreparation`; worker generation/readiness reads -> startup pass.
- `startNote -> beginPlayback -> recordPlayed -> acquire -> tryUseWarpCache ->
  switchToWarpCache`.
- `renderNextBlock -> maybeSwitchToReadyWarpCache -> acquire`; cache/original/
  realtime transitions and `clearActivePlayback -> WarpCachePrewarmer::Lease::reset`.
- Worker partner: `WarpCachePrewarmer::run -> render ->
  PercussionSound::renderWarpedCache -> RubberBandStretcher` R2 offline
  study/process/retrieve, publication, cancellation, and reclamation.
- Realtime fallback: `PercussionVoice -> RealtimeWarpPlayer::start/render`.
- MIDI activity, JUCE synth dispatch, sample/Punch/sustain rendering, Rzhav, and
  the one-shot pitch worker were checked as reachable dependencies.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:119`, `:156`, `:339`
Call path: missing exact warp cache -> `RealtimeWarpPlayer::start / render ->
prepare / reset / ensureBuffers`.
Issue: The fallback may rebuild its engine for a channel-count change, reset R2
with internal scavenging/reconfiguration, and resize scratch buffers for a large
host block or engine input request. Its feed loop has no practical work cap for
very short looping sources that repeatedly reset before producing output.
Why it matters: The first hit before a newly requested cache is ready can still
allocate, reclaim, or perform excessive work on the audio thread.
Minimal fix: Prebuild supported engine configurations and fixed chunk storage,
and bound feed/reset work. This fallback is retained so a new pitch/BPM is heard
immediately while background preparation completes.

[BLOCKER] `Source/PluginProcessor.cpp:334`
Call path: `processBlock` MIDI activity loop and JUCE synth dispatch -> long
`MidiMessageMetadata::getMessage` construction/destruction.
Issue: Long MIDI/SysEx messages can use heap storage. Ordinary note/controller
messages fit inline.
Why it matters: A host can cause audio-thread allocation and reclamation.
Minimal fix: Filter unsupported long messages as views and avoid materializing
them in synth dispatch.

[PERF] `Source/Playback/SustainTailShaper.cpp:50`
Call path: cached or realtime voice rendering -> sustain-gain calculation.
Issue: Existing per-sample exponent/power calculations and transient traversal
remain. The recent-cache change does not add this work, but it was not profiled.
Why it matters: It may contribute to total multi-voice CPU independently of cache
preparation.
Minimal fix: Profile first; if material, prepare coefficients and advance an
incremental segment state.

Cross-thread ownership:
- Original sounds/metadata: created during embedded loading and immutable during
  playback. The worker records borrowed pointers in an inventory that never
  resizes after construction. Processor teardown stops/clears voices, joins both
  workers, then clears sounds. Member order also destroys the warp worker before
  the sample cache and sampler if construction exits through an exception.
- Warp-cache slots: allocated before the worker starts and owned only by it.
  Each sound has `voiceCount + 4` stable slots. The worker reserves an unpublished
  slot by changing its reader count from `0` to `-1`, fills it, releases the
  reservation, then publishes its index with release ordering. Audio loads that
  index with acquire ordering and makes at most two pin attempts. It only reads
  immutable cache data and releases by decrementing the counter. Replaced and
  disabled publications are reclaimed only by the worker after all readers leave.
- Requests/recency: audio publishes a packed `(BPM, pitch)` key and monotonically
  increasing play stamp through lock-free scalar atomics. Last request wins;
  superseded work checks current BPM/pitch/enable state between 1024-frame chunks.
  The worker alone mutates debounce, failure, recent-selection, and render state.
- Parameters/transport: sample pitch, host BPM, tempo-sync enabled, and transport
  running are scalar atomics. The worker reads them; it never accesses APVTS
  trees, UI objects, the live synth inventory, or mutable voice state.
- Startup requests: construction initializes a pending generation. State restore
  clears a scalar timing-ready flag and release-increments the generation after
  restoring pitch/tempo-sync values. Worker acquire-loads that generation and
  schedules a fresh inventory pass. Audio release-stores timing-ready only after
  receiving and storing a finite host BPM >= 1; the worker acquire-loads it before
  reading BPM. Render cancellation checks generation, pitch, BPM, enabled state,
  and urgent work between chunks. A restore needs another valid timing callback;
  no thread walks the inventory or waits as part of publishing this request.
- Host timing: JUCE's deprecated `getCurrentPosition` already called `getPosition`
  internally. Using that same host call directly exposes whether BPM is actually
  present instead of using the legacy default. Optional scalar/PositionInfo values
  are stack/value objects; no new heap storage or additional host call is added.
  Missing/non-finite BPM retains the previous playback tempo. Position queries
  stay in processBlock, not on the worker or message thread.
- Realtime fallback: remains voice-owned and is used only when no exact cache is
  pinned. Moving or resetting a warp lease performs atomic reader updates only.

Verification:
- Startup follow-up: static audit after the final changes to tempo tracking,
  processor restore/update, and warp scheduling. Traced construction with no
  history, stopped transport with valid BPM, missing playhead/position/BPM,
  NaN/Inf/sub-1 BPM, fresh timing equal to the initial 153 BPM, state restore
  before/after initial processing, repeated restore during a render, disabled
  tempo sync, restored neutral/non-neutral pitch, and complete inventory reuse.
  Checked recent/demand priority during debounce, same-key hits during a startup
  render, stale/satisfied/failed demand requests, partial-pass BPM/pitch changes,
  render failure, empty inventory, completion, and teardown with pinned caches.
  Startup work uses the existing single R2 worker/slots; no voice/container/DSP
  ownership changes, new render-thread loops, or new block-size assumptions.
- Inspected pinned JUCE getPosition/getCurrentPosition/getBpm implementations.
  `git diff --check` passed. No build or runtime validation was requested or run.
- Static audit only after the final relevant code changes.
- Traced zero/unplayed/fewer-than-five/exactly-five/more-than-five histories,
  repeated promotion, non-looping and looping warp sounds, neutral/non-neutral
  pitch, stable and moving BPM, tempo-sync disable/re-enable, demand requests
  outside the recent set, failed/cancelled renders, slot exhaustion, overlapping
  voice leases, cache replacement, voice reuse, and processor teardown.
- Confirmed the new audio-side operations use fixed inventory lookup and bounded
  atomic work; render allocation, transient-map construction, Rubber Band offline
  work, and buffer destruction occur on the persistent worker.
- Checked finite key/rate/output-length guards, 0.01 BPM/0.01-semitone keys,
  source/output frame bounds, and cancellation between fixed input chunks.
- No build, tests, sanitizer, plugin validation, listening comparison, or CPU
  measurement was run.

Residual risks:
- Full startup preparation increases background CPU and cache memory after load.
  It can overlap the six one-shot workers. Timing/performance remain unmeasured.
- If a host supplies no processing callbacks or no BPM while stopped, the startup
  pass waits for valid timing. Playback before preparation finishes can still
  reach the existing realtime fallback. Rendered buffers are rebuilt in memory
  each plugin instance, not persisted. Failed startup keys are skipped until a
  later eligible retry/change rather than repeatedly consuming worker time.
- The retained realtime fallback and long-MIDI path prevent a plugin-wide pass.
- Recent caches render sequentially. An immediate hit after a new BPM/pitch can
  reach realtime fallback before its entry is ready, especially for the least
  recent of the five or long samples.
- The recent list tracks concrete loaded sound variants and lasts only for the
  plugin instance. It is not saved in plugin state. Each sound retains one current
  base and pitched publication; pinned retired versions remain until voices end.
- Compile/runtime behavior and audio quality on macOS arm64/x86_64 and Windows
  remain unverified.

Conclusion:
- Startup signalling, the five-sound scheduler, and cache publication add no allocation, locks,
  waiting, or audio-thread buffer destruction, but the inspected render path
  remains `FAIL` because its cache-miss fallback and long-MIDI path are unsafe.
