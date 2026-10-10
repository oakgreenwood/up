# Sample Attack and Release

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and `processBlockBypassed`.
- `PercussionSynthesiser` note dispatch/render; `PercussionVoice::startNote`,
  `stopNote`, and `renderNextBlock`.
- Host audio-thread APVTS callbacks for the two new sample parameters.

Reachability inspected:
- APVTS parameter callback -> fixed sample-specific registry lookup ->
  finite/range check -> lock-free per-note float atomic store.
- MIDI note -> selected sound -> two atomic envelope reads -> voice-owned
  source-time durations and key-release ADSR setup.
- Original/cached/live-warp rendering -> `SampleEnvelope::resolve` -> bounded
  `gain` at the original source position -> per-frame output and noise-hit gain.
- Key-held sample note-off -> `juce::ADSR::noteOff` -> bounded release stage.
- Existing original, offline-cache and live-warp voice render branches.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:78`, `:377`
Call path: warp voice start or long loop wrap -> `RubberBandStretcher::reset`
-> R2 reset/reconfigure.
Issue: The existing live-warp fallback can allocate and scavenge/deallocate
memory on the audio thread.
Why it matters: Cache misses and loop wraps can cause callback-time heap work.
Minimal fix: Prepare or replace the live-warp fallback with an allocation-free
render path.

[RISK] `Source/PercussionSynthesiser.cpp:12`, `:19`
Call path: processor render -> JUCE synthesiser note dispatch/render.
Issue: JUCE's synthesiser uses a `CriticalSection`; concurrent mutation of its
sound or voice inventory would contend with audio.
Why it matters: A contended lock can miss the callback deadline.
Minimal fix: Keep inventory changes outside active rendering.

Cross-thread ownership:
- Envelope values: editor/host listeners publish finite, clamped scalar values
  through existing per-note lock-free atomics. Each audio voice reads both once
  at note start and owns its fade durations and ADSR state until the next note.
  `SampleEnvelope` contains only three doubles; stack copies have trivial
  destruction. No state tree or UI object is accessed from voice rendering.
- Sound data, metadata and warp caches: loading/worker threads prepare immutable
  data; voices use the existing sound lifetime and pinned warp-cache leases.
  Worker reclamation of retired caches remains off audio.
- Stereo waveform summaries: two fixed min/max arrays are built during sound
  loading, published with the immutable sound, borrowed by the UI, and destroyed
  with the sound after stopped teardown. Rendering never accesses these arrays.
  Triangle drawing, labels and host gestures run on the message thread.
- Live Rubber Band engines: prepared per voice outside render; the existing
  R2 reset/reconfigure path remains realtime-reachable.

Verification:
- Static audit only; the repository instructions reserve builds and validation
  for an explicit request.
- Inspected JUCE's local `ADSR` implementation: `reset`, `setSampleRate`,
  `setParameters`, `noteOn`, `noteOff`, and `getNextSample` use scalar state and
  do not allocate. Reset before changing parameters satisfies its documented
  retrigger requirement.
- Reviewed 0 and 2000 ms bounds, zero attack/release, one-frame and overlapping
  fades, different variation lengths, source/cached time mapping, voice reuse,
  zero/single-frame callbacks and larger host blocks. Duration resolution is
  constant work per render sub-block and gain is constant work per sample.
- Fade divisions only use positive durations; invalid durations resolve to zero
  and invalid positions produce silence. No new memory operations or locks are
  reachable from the fade helper. Existing buffer bounds and feed limits remain.
- Live warp no longer starts ADSR release when feeding final input. It retrieves
  available output, applies the source-end fade, and completes when drained.

Residual risks:
- The pre-existing live-warp reset blocker prevents a realtime PASS.
- No build, host GUI, automation or listening run verified the new controls.
- Source-time fades follow pitch and time stretching. One-shot note-offs remain
  ignored, while Release now applies at the trimmed end and at loop boundaries.

Conclusion:
- The new Attack/Release path is bounded and allocation-free, while the
  existing live-warp fallback leaves the overall audit at FAIL.
