# One-shot pitch mode: realtime audio audit

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and inherited `processBlockBypassed`.
- `PercussionSynthesiser::noteOn`, JUCE note/controller dispatch, and voice start/stop.
- `PercussionVoice::renderNextBlock`, including reuse of a voice previously playing a loop.
- APVTS `AudioPluginAudioProcessor::parameterChanged` on a host/audio thread.

Reachability inspected:
- `processBlock -> sampler.renderNextBlock -> noteOn -> startNote -> beginPlayback
  -> OneShotPitchCache::acquire`.
- `renderNextBlock -> SamplePlaybackRenderer::render -> PunchEnvelope,
  SustainTailShaper, ADSR, NoteStartDeclicker -> clearActivePlayback -> Lease::reset`.
- `parameterChanged -> updateSample*CacheForGroup -> SampleSpecificRealtimeCache`.
- Cross-thread partner: `OneShotPitchCache::run -> renderGroup -> renderSound ->
  RubberBandStretcher` R3 study/process/retrieve, publication, and slot retirement.
- `processBlock -> WarpCachePrewarmer::update`, looping/non-looping warp voice
  cache leases, the background warp worker, and `RealtimeWarpPlayer`.
- Non-looping pitched warp follow-up: `startNote -> beginPlayback ->
  shouldUsePitchWarpCache -> tryUseWarpCache`; `renderNextBlock ->
  maybeSwitchToReadyWarpCache / maybeSwitchLengthPreservedPitchToRealtime`.
- Pinned JUCE synthesis, parameter attachments/APVTS listener dispatch, MIDI message
  construction, buffer access, ADSR, and default bypass implementation.
- Existing tempo tracking, MIDI activity publication, and output effect processing.

Findings:

[PERF, FIXED IN NON-LOOPING WARP FOLLOW-UP] `Source/PercussionVoice.cpp:319`, `:752`
Call path: warped non-looping `startNote / renderNextBlock ->` pitch cache
eligibility, ready-cache adoption, and realtime-fallback selection.
Issue: Cache eligibility depended on loop pitch debounce, so `warp=true` with
`loop=false` bypassed pitched caches at every trigger and could never adopt one.
Why it matters: Every non-neutral hit performed realtime Rubber Band work even
at a fixed BPM and pitch, although neutral BPM-only caching worked.
Minimal fix applied: Base cache eligibility/adoption on warp and enabled tempo
sync, independently of loop debounce. The existing matching-cache check now also
keeps non-looping voices on their prepared cache instead of switching back to
realtime processing. Looping, note-off, and pitch debounce semantics are unchanged.
The existing cache dispatch/publication/reclamation blockers below also apply
to this extended pitched-cache path; it is not fully realtime-safe.

[PERF, FIXED IN WARP-CPU FOLLOW-UP] `Source/PercussionVoice.cpp:530`, `:555`
Call path: `processBlock -> sampler -> startNote -> beginPlayback ->
tryUseWarpCache -> switchToWarpCache`; also render-time switches to cached/original
playback.
Issue: These transitions reset the inactive realtime Rubber Band engine. In the
vendored R2 implementation, reset clears channel buffers, resets resamplers,
scavenges retired allocations, and calls reconfigure, even for a fully cached hit.
Why it matters: Repeated warped triggers pay engine-management costs despite
using prepared audio, independently of the pitch amount.
Minimal fix applied: Removed both inactive-engine resets. All entries into
realtime playback call `RealtimeWarpPlayer::start`, which still resets the engine
and initializes source position, end state, output time, pitch, and time ratio.
This establishes removal of unnecessary work, not a measured CPU improvement or
proof of the cause of the reported regression. The resets also existed before
the one-shot pitch changes.

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:119`, `:156`, `:339`
Call path: loop `startNote / renderNextBlock -> RealtimeWarpPlayer::start / render
-> prepare / ensureBuffers / setSize`.
Issue: Existing realtime fallback can reconstruct the engine when channel count
changes and grow scratch buffers for larger host blocks or engine input requests.
Its feed loop also lacks a practical maximum work budget for very short looping
sources that repeatedly reset before producing output. R2 reset/reconfiguration
has additional scavenging/allocation paths in the vendored implementation.
Why it matters: Allocation, destruction, or excess work can interrupt audio.
Minimal fix: Preallocate the supported configurations and process bounded chunks;
bound feed/reset work and move engine preparation/reclamation off audio.
One-shot start/end now avoids redundant realtime Rubber Band resets entirely.

[BLOCKER] `Source/PluginProcessor.cpp:334`
Call path: MIDI activity loop -> `MidiMessageMetadata::getMessage`; JUCE's
`Synthesiser::processNextBlock` also materializes MIDI messages.
Issue: Existing long MIDI/SysEx messages allocate through `MidiMessage::allocateSpace`
and free when destroyed. Ordinary note/controller messages fit inline.
Why it matters: A host supplying long messages can still cause audio-thread heap work.
Minimal fix: Inspect/filter unsupported long messages as views, and make sampler
dispatch avoid materializing them. This existing MIDI behavior is outside the
one-shot pitch-mode implementation.

[PERF] `Source/Playback/SustainTailShaper.cpp:50`
Call path: sample rendering -> sustain gain calculation.
Issue: Existing per-sample exponent/power calculations and transient advancement
remain. The new cache does not increase this processing, and voice count is fixed
at eight, but no runtime CPU bound was measured.
Minimal fix: If profiling warrants it, prepare parameter-dependent coefficients
and use incremental segment state. This audit does not claim measured performance.

Cross-thread ownership:
- Original samples and metadata: constructed during embedded loading and retained
  by the sampler throughout playback. The pitch worker borrows immutable pointers
  gathered before it starts; it never accesses the live sampler. Voice sound refs
  cannot be the last owner during normal playback. Teardown clears voices, joins
  the pitch worker, then deletes sounds on the control thread.
- One-shot group snapshots: the coordinator creates the output vector; six
  pool workers fill independently claimed buffers before the coordinator publishes
  the whole group. See `realtime-audio-audit-parallel-pitch-cache.md` for batch
  ownership, cancellation, recent-group scheduling, and shutdown. Each
  group has ten stable slots for the eight-voice pool. A writer reserves a slot
  only by CAS from zero readers to `-1`; it never reserves the published slot.
  A release store ends writing; an acquire CAS pins audio access. Publication of
  the group index uses release/acquire ordering. All variations/layers publish
  together. Audio has at most two acquisition attempts and uses the original
  buffer if no lease is obtained.
- One-shot retirement: each voice releases only an atomic reader count. The worker
  can reserve and reclaim an unpublished slot only after its last reader releases
  it. Replacements use other slots while predecessors remain pinned. No audio
  branch destroys, resizes, or copies owning snapshot data. Remaining snapshots
  are destroyed during non-audio teardown after voices stop.
- Per-note pitch/mode/Punch: initialized before rendering, subsequently written by
  parameter callbacks or non-realtime restore. Readers access only fixed scalar
  atomics; pitch and mode latch for each one-shot. Float/bool/int/double lock-freedom
  is checked at compile time. The worker alone writes its observed-request state.
- Host sample rate: lifecycle code publishes a scalar to the worker. Each immutable
  snapshot carries its own output rate, so an old ready cache remains interpretable
  while a replacement is rendered. JUCE stops voices on a playback-rate change.
- Per-group ready/failure status: scalar atomics written by the worker and polled
  by the editor. UI code never reads a mutable snapshot or participates in its
  ownership. Status reads do not run on audio.
- APVTS/sample-specific state: parameter callbacks touch only scalar caches. The
  non-realtime save path mirrors them into the sample-specific tree and serializes
  `parameters.copyState()`. Restore uses `parameters.replaceState()`, restores the
  child tree, rebuilds caches, and synchronizes the selected controls. No new
  worker accesses the state tree. Existing state callbacks require the usual host
  lifecycle coordination.
- JUCE synth lock: retained as an existing framework mechanism. The new worker
  never modifies sounds/voices or takes this lock while playback is active.
  APVTS/host listener mechanics remain, with bounded scalar work in the new listener.
- Warp snapshots: now use persistent-worker-owned fixed slots and bounded audio
  leases. Their scheduling/publication/reclamation audit is in
  `realtime-audio-audit-recent-warp-cache.md`.

Verification:
- Non-looping pitched warp follow-up: static audit after the eligibility changes.
  Traced absent/pending/ready pitched caches at note start, ready-cache adoption
  at matching source time with declick, same-cache reuse, pitch changes, neutral
  pitch, tempo sync disabled, BPM motion, note-off, and source exhaustion.
  One-shot R3 eligibility and loop-only pitch debounce remain separate. Existing
  zero-block handling, interpolation bounds, and sample-rate/time-ratio math
  are unchanged. The later recent-cache worker replaces the former future/mutex
  lifecycle with immutable fixed-slot publication and off-audio reclamation.
  No build, tests, listening check, or CPU measurement was run.
- One-shot transient-timing follow-up: static audit after removing the metadata
  key-frame map from `OneShotPitchCache::renderSound`. R3 construction and all
  study/process/retrieve work remain on the worker. Output frame-count/rate
  checks, complete-group publication, bounded audio leases, and worker reclamation
  are unchanged. Cached playback still uses original timestamps for Punch and
  sustain. No new audio-thread work or platform-specific code was introduced.
  Click removal has not been verified by a build or listening test; local attack
  timing can differ now that the renderer has no explicit transient constraints.
- Warp-CPU follow-up: static audit after removing the two resets. Traced cached
  note starts and cached/original/realtime transitions. Rendering and source-time
  reads consult the retained engine only when `isRealtimeWarping` is true; both
  edited transitions set it false. The engine remains voice-owned and idle until
  a resetting start, with no new publication, ownership, buffer, or loop changes.
  No build, runtime validation, or CPU profile was collected. Process-list access
  for identifying a running instance was declined.
- Static audit only, performed after the final relevant code changes. Read the
  current sources, diffs, and pinned JUCE/Rubber Band implementations.
- Manually traced both modes, an active hit surviving pitch/mode changes, complete
  group publication, absent/pending/failed caches, neutral bypass, old-state defaults,
  per-group selection, automation with a closed editor, and voice/worker teardown.
- Inspected zero/tiny/variable-block behavior: the new renderer/voice returns on
  zero frames, has no host-block scratch allocation, clamps interpolation at the
  final source frame, uses bounded loop wrapping, and releases exhausted leases.
- Checked rate arithmetic: output duration uses source frames and the rate ratio,
  while pitch changes only the independent pitch scale. One-shot R3 rendering
  no longer supplies transient key frames; Punch explicitly uses original times.
- No build, tests, sanitizer, plugin validator, or listening comparison was run,
  following the repository instruction to do so only when requested. macOS/Windows
  compilation and behavior therefore remain unverified.

Residual risks:
- The realtime warp fallback and MIDI blockers prevent a plugin-wide realtime pass.
- Warped samples still use realtime fallback for a missing pitched cache; the
  first hit at a new BPM/pitch can have a CPU peak while preparation completes.
  The eligibility fix removes permanent cache bypass for non-looping warped
  sounds; it does not establish a measured runtime improvement.
- Preparation latency, CPU use, audio quality, and transient alignment need runtime
  measurement on the actual sample set at supported sample rates and pitch extremes.
- By the accepted fallback policy, new hits use the previous prepared pitch (or
  the original if none exists) until the requested group is ready. That also applies
  immediately after state restore or a rate change; a fast offline bounce started
  before preparation completes can capture the fallback. Wait for `Preparing...`
  to clear before rendering a final bounce.
- Compile, state round-trip, allocation/stress, and host UI checks remain to run.

Conclusion:
- No blocker was found in the new one-shot slot publication/reclamation or its
  note-latched playback/Punch path. The overall inspected render path still fails
  the audit because the existing tempo-warp and long-MIDI paths retain the blockers above.
