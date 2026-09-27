# Follow depth curve and Rise knob audit

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` / `processBlockBypassed`.
- Synth note dispatch and voice render callbacks as unchanged hit producers.
- Preparation/release, APVTS save/restore and editor attachment lifetimes as
  publication/reset partners for the new parameter.

Reachability inspected:
- Cached APVTS `noiseRiseDb` atomic -> block snapshot -> `NoiseSidechain::setParameters`
  -> finite clamp -> cached JUCE dB conversion -> prepared 10 ms linear smoother.
- Existing hit capture -> <=512-frame `finishBlock` -> smoothed depth -> cached
  square-root boost shape -> base/triggered-peak interpolation -> bounded noise
  modulation -> `BackgroundNoisePlayer` gain multiplication.
- Existing Pump attack/release, Follow hit envelope, trigger tables and latency
  rings remain as covered by the [transient audit](realtime-audio-audit-noise-transients.md)
  and [Pump curve audit](realtime-audio-audit-noise-pump-curve.md).
- JUCE's local SmoothedValue and Decibels implementations, processor APVTS
  copy/replace/default restoration, UI knob setup/visibility/teardown, and the
  existing Rubber Band fallback reset/reconfiguration/feeding were inspected.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:156`, `:326`, `:345`
Call path: fallback voice start/live rate change/loop restart -> Rubber Band R2
reset/reconfigure; rendering -> repeated feeding until output is available.
Issue: Pre-existing reset scavenges resources, reconfiguration can allocate,
and feeding lacks a practical callback work budget. Vendored R2 code retains
scavenging at line 213, window allocation at 761/769 and resampler creation at 800.
Why it matters: The inspected broader path is not realtime-safe even though
this change adds no allocation or resource reclamation.
Minimal fix: Bound the fallback's internal memory/reclamation and work, or
prepare fallback output off thread. This remains outside the Follow/Rise task.

[RISK] `Source/PercussionSynthesiser.cpp:6`, `:14`
Call path: audio render / MIDI dispatch -> existing JUCE CriticalSection.
Issue: Framework locking remains; this feature adds no lock or live inventory edit.
Why it matters: No whole-plugin lock-free guarantee is made.
Minimal fix: Preserve stopped source/voice construction, preparation and teardown.

Cross-thread ownership:
- APVTS owns `noiseRiseDb` for the processor lifetime. UI/host/state publish the
  scalar; audio reads a constructor-cached pointer once per host block. No audio
  tree lookup/copy/replace, string operation or parameter object ownership is
  introduced. The parameter is appended after existing slots and old states
  insert +3 dB before replacement. Normal copyState/replaceState persists it.
- Rise smoother, cached gain, last depth and square-root result are plain
  audio-owned detector members. Preparation configures the smoother while
  stopped. Reset initializes scalars, and the first parameter read seeds saved
  values directly. No new dynamic storage, atomic pointer or reclamation exists.
- The existing 512-float detector array remains audio-only: voices write hit
  strengths, finish overwrites with modulation, then noise consumes it. There is
  no new buffer handoff or mutation by the UI.
- Immutable percussion audio/trigger tables retain synth ownership through voice
  borrows, with stopped teardown after voices. Warp leases and imported-noise
  slots retain their scalar pin release and worker reclamation protocols.
- The editor owns the Rise image knob, label, editable value and attachment.
  They execute on the message thread; attachments are destroyed before controls,
  values before their sliders, and L&F pointers are cleared before its destruction.
  Numeric formatters are installed after binding, following existing controls.
  No editor objects are read on audio or retained by the new detector state.

Verification:
- Static audit only after final relevant code changes. No build, runtime tests,
  UI launch, listening or allocation profiling was performed, per AGENTS.md.
- JUCE dB conversion is a scalar `pow`, executed only when the clamped block
  snapshot changes. Rise bounds 0..12 dB give a finite gain in [1,3.981072].
  Non-finite input uses +3 dB. JUCE linear smoothing stores scalar state and
  advances without allocation or locks. Sample-rate bounds keep ramp counts
  finite and bounded; identical targets do not restart ramps each block.
- Square-root input is in [0,1], avoiding domain errors. It is recomputed only
  when positive smoothed depth changes, at most once per output frame during
  automation/ramping. No square root is needed at steady depth. There is no new
  loop or traversal dependent on file/metadata size.
- Follow computes base=1-d, peak=1+(B-1)*sqrt(min(1,d/0.3)), then
  base+(peak-base)*L. At L=0 only the background remains. At L=1 the ceiling is
  B for every d>=0.3. At d=0 output is exactly unity for every L/B; at d=1 it
  reaches exact silence between hits. Rise=0 dB sets peak=1. The clamp is [0,B].
- Pump retains its old expression and [0,1] clamp. Its envelopes, timing and
  trigger strength are unchanged; the new Rise smoother advances independently.
- Depth still ramps over 10 ms. Computing the square root from the actual
  smoothed depth (rather than separately ramping that shape) preserves exact
  neutral and continuity when crossing between Pump and Follow.
- No new array indices, owning pointer operations, deallocation, file I/O or
  logging occur in the change. Zero/tiny/large host blocks retain existing
  <=512-frame processing and <=128-frame voice scratch. Reset/preparation/bypass
  maintain silent initial full Follow and valid scalar caches.
- State insertion is outside rendering; stable prior parameter indices and
  sample-specific Apply to All membership remain unchanged. Rise visibility,
  double-click default, dB formatting and 3-knob bottom layout were inspected
  statically. At the fixed 900x600 editor size, knob labels/value fields fit
  within the existing noise panel without overlap or new panel sizing.

Residual risks:
- Existing Rubber Band blockers remain unresolved. No new realtime blocker was
  found in this feature under static inspection; macOS/Windows compilation,
  host automation/state roundtrip, UI appearance and listening remain unverified.
- The ceiling reaches the same value at/above depth 0.3, but 80/20 dynamics and
  finite envelope attack mean actual peaks can be lower. With L<1, some base
  contribution remains, so reducing the background also slightly lowers peaks.
- Rise up to +12 dB reduces headroom; the plugin adds no limiter. This is the
  requested gain range, not a guarantee that mixed output stays below 0 dBFS.

Conclusion:
- New Rise/state/UI plumbing and Follow arithmetic introduce bounded scalar
  render work without new allocation, locking or unsafe ownership. The overall
  audited path remains FAIL because of pre-existing Rubber Band fallback issues.
