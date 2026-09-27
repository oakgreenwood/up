# Pump / Follow square-root depth audit

Realtime audio audit: FAIL

Audited realtime entry points:
- Processor processBlock / bypass, prepare and release.
- Existing synth MIDI/voice callbacks as hit producers for noise modulation.

Reachability inspected:
- Cached modulation parameter -> existing 10 ms amount smoother -> absolute
  depth clamp -> cached square root -> Pump strength/unit-envelope blend or
  Follow background attenuation -> bounded gain -> noise output multiply.
- Follow boost retains its independent raw-depth square-root curve and +15
  saturation point; fixed 5 ms Attack, +12 dB Rise, hit envelopes and Release
  are unchanged. Existing chunk rendering and noise gain consumers were read.
- Existing fallback hazards and ownership remain as documented in the
  [controls audit](realtime-audio-audit-noise-controls.md) and linked audits;
  vendored Rubber Band hazard locations were rechecked.

Findings:

[BLOCKER] Source/Playback/RealtimeWarpPlayer.cpp:74, :156, :326, :345
Call path: fallback start/rate update/loop reset -> Rubber Band R2 reset and
reconfiguration; render -> repeated feeding until output appears.
Issue: Existing resource scavenging, possible allocation and lack of a practical
feeding budget remain. Vendored R2 locations include scavenging at 213, window
allocation at 761/769 and resampler creation at 800.
Why it matters: The broader render path remains unsafe for strict realtime use.
Minimal fix: Bound fallback allocation/reclamation/work or prepare fallback
output off thread. These unrelated engine repairs are outside the curve change.

[RISK] Source/PercussionSynthesiser.cpp:6, :14
Call path: existing audio range / MIDI dispatch -> JUCE CriticalSection.
Issue: Existing framework locking remains; no new lock or inventory mutation.
Why it matters: No whole-plugin lock-free guarantee is made.
Minimal fix: Preserve stopped preparation, inventory setup and teardown.

Cross-thread ownership:
- `lastRawDepth` and `curvedDepth` are audio-owned doubles inside the existing
  processor-owned detector. Reset only writes scalar values. They introduce no
  allocation, buffer, pointer ownership, publication or reclamation.
- APVTS scalar targets retain their cached atomic publication. No parameter ID,
  range, default, attachment, state migration or tree access changes are made.
- The detector array remains audio-only: voices add hits, finish writes gains,
  then noise consumes those gains before reuse. Sample/metadata, latency rings,
  warp leases and imported sources retain the prior ownership/reclamation paths;
  no new shared object or live replacement is introduced.

Verification:
- Static audit only after final relevant code changes. No build, executable
  tests, UI run, listening or runtime allocation profile, per AGENTS.md.
- Square-root input is explicitly clamped to [0,1]. The output therefore stays
  finite in [0,1]. It is cached until raw depth changes, adding at most one
  bounded scalar square root per frame during a ramp/automation and none at
  steady depth. No extra loop, allocation, destructor, I/O or lock is added.
- Reset invalidates the cache and initializes depth to zero. Existing finite
  target clamps and linear smoothing supply bounded values on first use and
  subsequent changes. Zero blocks do not advance state; one-frame and large
  blocks retain the same <=512-frame outer processing and existing array bounds.
- Centre maps to exact zero depth/unity noise gain on either side. Endpoints
  still map to full depth. sqrt(0.2), sqrt(0.5), sqrt(0.7) give approximately
  44.7%, 70.7%, 83.7% effective depth: +/-35 does not cap the effect early.
- Pump substitutes curved depth in both existing depth terms; its temporal
  envelope is unchanged. Follow uses base=1-sqrt(abs(amount)) but retains its
  original boost curve based on raw positive amount, so its +15 ceiling is not
  accidentally moved nearer centre. Output clamps stay [0,1] / [0,Rise gain].
- Computing curves from the actual smoothed raw amount preserves continuity and
  exact neutral when crossing sign; no independently lagging curve is introduced.

Residual risks:
- Existing Rubber Band blockers remain; no new realtime blocker was found.
- macOS/Windows compilation, listening and host automation remain unverified.
  The square root intentionally makes small centre movements more audible;
  existing smoothing remains, but its steeper near-zero slope should be heard
  in a host before assessing the subjective response.
- Existing saved/automated raw amounts now produce the stronger mapped response,
  as requested. State values and slider position/fill are not remapped.

Conclusion:
- The new depth mapping is bounded and allocation-free under static inspection.
  Overall status remains FAIL due to existing fallback engine blockers.
