# Noise 80/20 transient-trigger audit

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` / `processBlockBypassed`.
- Synth MIDI dispatch, `PercussionVoice::startNote` / `stopNote` / rendering.
- Original/cached sample rendering and realtime Rubber Band fallback rendering.
- Preparation, source construction and teardown inspected for ownership partners.

Reachability inspected:
- Processor <=512-frame chunk -> synth voice <=128-frame scratch -> sample or
  realtime-warp renderer -> original-source-time marker tracker -> hit scratch.
- Voice Formant/Mono/Pan/EQ render loop -> prepared event delay -> group-filtered
  `NoiseSidechain::addHit` -> attack/release pulse -> existing bipolar mapping ->
  `BackgroundNoisePlayer` modulation multiply. Spectrum capture is unchanged.
- Note start/reuse/stop -> marker pointer/index and delay index resets; source
  exhaustion -> effect drain also drains delayed events; preparation allocates
  event rings with the same latency calculation as the existing Formant chain.
- Loader -> sound constructor -> validated/sorted/deduplicated marker table and
  source-peak scan. Original sound ownership, cache leases and their reclamation,
  existing JUCE synth locks, and Rubber Band reset/reconfiguration were inspected.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:156`, `:326`, `:345`
Call path: voice realtime fallback start/loop reset/live rate changes -> Rubber
Band R2 reset/reconfigure; render -> repeated input feeding until output arrives.
Issue: Pre-existing resource scavenging and possible allocation remain in R2
reset/reconfiguration (`external/rubberband/src/faster/R2Stretcher.cpp:213`,
`:761`, `:769`, `:800`); the feed loop has no practical per-callback work budget.
Why it matters: Prepared wrapper scratch and bounded new event work do not make
this existing fallback realtime-safe.
Minimal fix: Bound the fallback's internal allocation/reclamation and work, or
move fallback preparation off thread. This engine change is outside this task;
these blockers remain unresolved, as in the previous noise audits.

[RISK] `Source/PercussionSynthesiser.cpp:6`, `:14`
Call path: processor audio range / MIDI dispatch -> JUCE synth CriticalSection.
Issue: Existing framework locking remains. This feature adds no locks or live
sound/voice inventory mutation.
Why it matters: No whole-plugin lock-free guarantee is made.
Minimal fix: Keep inventory creation, preparation and teardown outside rendering.

Cross-thread ownership:
- Noise-trigger points are private sound-owned vectors built completely during
  processor construction, before the sound is registered with the synth. Only
  const references/pointers escape. Audio reads but never modifies, copies,
  resizes or releases them. Synth sound ownership outlives all voice borrows;
  voices are destroyed before sounds during stopped processor teardown. There
  is no live replacement or additional reference-count operation.
- Per-voice marker cursor, 128-float hit scratch and delay ring are audio-owned.
  Ring allocation/replacement occurs only in `prepareRealtimeWarpResources`,
  with rendering stopped. Note reuse resets scalar indices; stale ring entries
  cannot escape because output is suppressed until every slot is overwritten.
  Render and note callbacks perform no container growth/destruction. Rings are
  reclaimed with voices in stopped teardown.
- The processor-owned detector array is cleared before voice rendering, receives
  hits at host offsets, then is overwritten with modulation and consumed by noise
  before reuse. Audio owns the envelope, target, counter and smoothing state.
  Voice pointers are attached during stopped setup and destroyed first.
- Existing parameter atomics and stable trigger inventory are unchanged. No new
  parameters, APVTS tree operations, shared queues or publication are introduced.
- Original audio/metadata retain stopped construction and teardown. Warp cache
  leases retain their scalar reader-pin release and worker-side reclamation.
  Imported noise slots retain their separate bounded pin/publication protocol;
  this change neither owns nor replaces noise audio.

Verification:
- Static audit only, after final relevant implementation changes. No build,
  automated test, host run, listening check or runtime allocation profile was
  run, following AGENTS.md's validation-only-on-request instruction.
- All renderer call sites pass the voice's fixed hit array; render counts are
  <=128. Zero blocks do not advance audio/events. Large/changing host blocks
  remain chunked at <=512; each voice chunk clears its hit array even during
  effect drain, preventing stale hits when a renderer exits early.
- Sorted finite in-range marker times give a valid `upper_bound` range and a
  nonempty consumed range before dereferencing its last element. Normal frames
  do one pending-marker comparison. Crossed markers use logarithmic search,
  never an unbounded marker-advance loop. Explicit loop wraps reset the cursor;
  cache/original/realtime switches preserve it to avoid duplicate triggers.
- Peak preparation scans only off audio, ignores non-finite source values and
  clamps frame positions before integer conversion. Render peak/gain arithmetic
  uses double, rejects non-finite/nonpositive/floor-level results and clamps hit
  strengths to [0.8, 1]. Invalid times emit no event.
- Event delay capacity uses the existing safe 1..768 kHz Formant latency helper;
  ring indexing/wrapping is bounded and each slot advances once per voice frame.
  No source ownership is stored in ring entries; they contain only floats.
- Attack counters are bounded to 1..1,536,000 frames (2 s at 768 kHz). Existing
  finite parameter clamps, coefficient caching, envelope snap and denormal guard
  remain. Targets/envelopes are bounded [0,1], as is the final multiplier.
  Retriggers preserve values; no hard reset or audio-rate transcendental is added.
- Pump/Follow formulas and 10 ms amount smoothing are unchanged. Amount zero
  yields unity, full Follow uses the strength envelope, full Pump uses the unit
  envelope. Initial restored full Follow stays silent until an accepted hit.
- Sample-rate changes prepare new ring capacity while stopped and reset voices;
  normal note reuse only resets indices. Voice termination drops pending hits
  with its audio. Bypass freezes voices/events with existing voice playback and
  resets the global noise envelope; playback resumes together on unbypass.

Residual risks:
- The existing Rubber Band fallback blockers above remain. No new allocation,
  destruction, lock, unsafe shared mutation or unbounded loop was found in the
  new trigger preparation/publication/render logic under static inspection.
- macOS/Windows builds and listening remain unverified. Host checks should cover
  quiet/loud layers, selected/all groups, loop boundaries, changing tempo/pitch,
  short and maximum envelopes, rapid retriggers and voice stealing.
- Realtime warp uses the existing output-time estimate; Rubber Band can move
  transients relative to that estimate. Cached warp anchors JSON markers via its
  existing keyframe map. PSOLA can also reshape onsets around its fixed latency.
- Strength is a prepared source-peak estimate scaled by voice ADSR/velocity/Gain,
  not a post-EQ peak follower. JSON errors can create missing/spurious pulses;
  absent markers provide only a start/per-loop trigger, not onset detection.
- The unchanged exponential curve reaches 99.9%, not mathematical unity, at the
  end of a single attack; full Pump is approximately -60 dB there from rest.
  Long release or dense hits intentionally merge pulses.

Conclusion:
- The new 80/20 hit-trigger path has bounded, allocation-free render work under
  static inspection. The wider audited path remains FAIL due to the existing
  Rubber Band fallback blockers; no unrelated engine repair was attempted.
