# Simplified noise controls and extended volume range

Realtime audio audit: FAIL

Audited realtime entry points:
- Processor processBlock, bypass, preparation/release and tail query.
- Existing synth note callbacks and sample/warp rendering as hit producers.
- Save/restore and editor lifetime as non-realtime configuration partners.

Reachability inspected:
- processBlock -> fixed 5 ms Attack / +12 dB Rise constants plus cached Release
  atomic -> existing NoiseSidechain envelopes -> noise modulation multiplication.
- Noise gain snapshot -> -60.1 silence comparison -> [-60,+10] dB clamp and
  existing gain smoothing/source playback; tail query uses the same sentinel.
- copyState -> normal serialization; state restore -> normal replaceState. Removed controls, attachments, L&F entries and visibility/layout
  references were inspected. Host parameter IDs/indices remain stable.
- Existing Rubber Band allocation/scavenging and synth locking retain the scope
  established in the [Follow rise audit](realtime-audio-audit-noise-follow-rise.md).

Findings:

[BLOCKER] Source/Playback/RealtimeWarpPlayer.cpp:74, :156, :326, :345
Call path: realtime warp start/rate update/loop reset -> R2 reset/reconfigure;
render -> repeated feeding until output is available.
Issue: Existing resource scavenging, possible allocation and no practical feed
budget remain. Vendored R2 reset/reconfiguration retains scavenging at line 213,
window allocation at 761/769 and resampler construction at 800.
Why it matters: The wider render path remains unsafe for strict realtime use.
Minimal fix: Bound fallback memory/reclamation/work or prepare fallback output
outside rendering. No unrelated fallback repair is included in this task.

[RISK] Source/PercussionSynthesiser.cpp:6, :14
Call path: render/MIDI dispatch -> existing JUCE CriticalSection.
Issue: Framework locking persists; the change adds no contention or inventory edit.
Why it matters: No whole-plugin lock-free guarantee is made.
Minimal fix: Preserve stopped preparation, source construction and teardown.

Cross-thread ownership:
- Attack/Rise are constexpr values, not shared mutable state. Their old APVTS
  slots remain serialized for compatibility but no longer have audio pointers
  or editor attachments. Saved/automated legacy values cannot change DSP.
- Gain/Release remain APVTS-owned scalar atomics read through cached pointers.
  Existing detector arrays/envelopes and noise gain smoothers are audio-owned.
  The change adds no buffer, publication queue, ownership transfer or reclamation.
- Save/restore use ValueTree operations only in state callbacks, never in render.
  No gain migration or range-version marker is added; audio sees scalar targets.
- Immutable percussion sounds, onset tables, warp cache leases and imported-noise
  slots retain their established stopped-construction or worker-pinned ownership
  and off-audio reclamation. No replacement path is changed.
- Removed knobs/value fields/attachments no longer exist. Remaining Release
  controls use the existing message-thread lifetime and cleanup patterns.

Verification:
- Static audit only after final code changes. No build, runtime validation,
  host/UI run, listening check or allocation profiling was performed per AGENTS.md.
- Fixed constants remain inside all existing finite clamps. 5 ms attack gives
  bounded counters and finite one-pole coefficients; +12 dB gives bounded gain
  about 3.9811. Pump keeps its sharper response and 5 ms attack stage. The
  Follow depth curve and Release behavior are otherwise unchanged.
- New gain minimum -60 dB converts to 0.001, safely above denormals. The far-left
  -60.1 sentinel still maps to exact zero; source stop and playhead behavior use
  the existing fades/reset path. There is no new allocation, lock, loop or I/O.
- Follow-up at user request: removed gain migration and range-version stamping.
  Saved gain restores directly; -40.1 dB is audible in the expanded range.
  Missing gain retains the existing current-silence default. Static inspection
  after removal found no render-path or publication changes; no build was run.
- Host parameter slots stay ordered. Legacy defaults are 5 ms/+12 dB, while
  runtime values are fixed regardless of old-state or host automation values.
- Zero/tiny/oversized blocks retain the same fixed chunk processing and scratch
  bounds. Preparation/bypass/reset still initialize the current fixed constants
  on the next parameter snapshot; full Follow starts silent without a trigger.

Residual risks:
- Existing Rubber Band blockers remain. No new realtime blocker was found.
- macOS/Windows builds, state roundtrip, UI and listening remain unverified.
- Host-owned normalized gain automation can map differently after the range
  expansion. Saved dB values restore directly, with no old-silence compatibility
  mapping; plugin state cannot rewrite host automation lanes.
- Fixed +12 dB Rise raises triggered noise relative to old +3 dB projects;
  this is intentional and does not introduce a limiter.

Conclusion:
- The control simplification adds no realtime resource or
  synchronization hazards. Overall status remains FAIL due to existing fallback
  engine blockers outside this change.
