# Pump / Follow volume curve audit

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and `processBlockBypassed`.
- Synth note dispatch and `PercussionVoice::renderNextBlock` as hit producers.
- Preparation/release as envelope reset and parameter-lifetime partners.

Reachability inspected:
- Processor cached atomics -> `NoiseSidechain::setParameters` -> finite time
  clamps and cached exponential coefficients / quadratic release increment.
- Existing original/cached/realtime-warp hit production -> latency-aligned
  `addHit` -> `finishBlock` -> unchanged Follow follower and new `advancePump`
  -> existing smoothed depth mapping -> background-noise amplitude multiply.
- Reset -> all new Pump scalars initialized; zero/tiny/large host ranges retain
  the existing <=512-frame chunk and <=128-frame voice scratch paths.
- Existing Rubber Band fallback reset/reconfiguration and JUCE synth locking.
  The unchanged trigger, delay, sample ownership and publication paths retain
  the detailed inspection in [the transient audit](realtime-audio-audit-noise-transients.md).

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:156`, `:326`, `:345`
Call path: voice fallback start/live rate change/loop restart -> R2 reset and
reconfigure; fallback render -> repeated feeding until output arrives.
Issue: Existing audio-thread resource scavenging, possible window/resampler
allocation and lack of a practical feeding-work budget remain. Re-inspected R2
reset (`external/rubberband/src/faster/R2Stretcher.cpp:213`) and window allocation
branches (`:761`, `:769`); this task changes none of them.
Why it matters: These pre-existing blockers prevent a whole-path realtime pass.
Minimal fix: Bound fallback allocation/reclamation and work, or prepare fallback
output off thread. That engine repair remains outside this Pump-curve change.

[RISK] `Source/PercussionSynthesiser.cpp:6`, `:14`
Call path: audio range and MIDI dispatch -> inherited JUCE CriticalSection.
Issue: Existing synth locking remains; no new lock or inventory mutation is added.
Why it matters: The broader plugin is not claimed to be lock-free.
Minimal fix: Preserve stopped inventory creation, preparation and teardown.

Cross-thread ownership:
- New Pump targets, release snapshots, phase, increment and envelope values are
  plain processor-owned scalars inside `NoiseSidechain`. Only audio changes them
  during rendering/bypass; preparation/release reset while rendering is stopped.
  They allocate nothing and own no resources requiring reclamation.
- The existing detector array is audio-owned: voices write hits, finish replaces
  them with modulation, then noise consumes it before reuse. No new buffer or
  handoff is introduced. Both Pump and Follow update continuously, so changing
  slider sign does not adopt stale state; the existing ramp crosses neutral.
- Parameter atomics remain APVTS-owned for processor lifetime, published by the
  host/UI and read through cached pointers once per block. New derived scalars
  never copy/replace the state tree. No new parameter or serialized field exists.
- Sound tables and metadata remain immutable after stopped construction; synth
  ownership outlives voice borrows. Warp leases and imported-noise slots keep
  their scalar pin release and worker reclamation. The change touches none of
  their publication or destruction paths. No live replacement is added.

Verification:
- Static audit only after final relevant code changes. No build, executable
  validation, listening test or profiler run, per AGENTS.md's request-only rule.
- New per-frame work is fixed scalar arithmetic with no loops, allocation,
  destruction, locks, I/O, container operations or transcendental functions.
  The only enclosing new-use loop remains bounded by detector capacity 512.
- Finite rate/time clamps bound the release increment to (0,1]; phase clamps
  to [0,1], so `1 - t*t` stays [0,1] and release reaches exact zero ducking.
  Release snapshots and hit targets remain [0,1]. Attack coefficient squared
  stays (0,1), giving a convex update; the existing 1e-5 snap reaches targets.
  Existing processBlock denormal suppression remains in effect.
- Retrigger targets are at least the current Pump strength envelope. No hit
  resets its value; attack only deepens/maintains ducking. Release snapshots
  track the final attack sample, avoiding discontinuity at the stage boundary.
  Time automation changes the release phase speed without resetting phase.
- At fixed depth, release restores the ducked gain as t^2: halfway gives 25%
  recovery, the endpoint 100%. Full Pump reaches exact zero during attack after
  snap; neutral remains unity. Attack is twice as fast as the old exponential
  response while its stage duration still follows the Attack control.
- At the initial Pump change, Follow's coefficient, target/retrigger calculation,
  envelope update, depth mapping and amount ramp were unchanged. The subsequent
  Follow gain change is audited below. Its unused old unit envelope was
  removed; Pump now owns its own unit envelope and strength envelope.
- Reset clears all Pump histories and sets phase to completed. Zero frames do
  not advance state. One-frame blocks and large/changing blocks share the same
  frame arithmetic, with state carried across chunks and no new buffer indices.

Residual risks:
- Existing Rubber Band blockers remain; no new realtime blocker was found in
  the Pump curve change under static inspection.
- macOS/Windows builds, listening and host automation remain unverified. Short
  Attack settings now duck more sharply; dense retriggers/long Release can keep
  noise heavily ducked as intended. The UI Attack duration still controls the
  whole attack stage, while 99.9% ducking is reached halfway through that stage.

Conclusion:
- The Pump curve implementation is bounded and allocation-free under static
  inspection. The wider audited path remains FAIL due to existing Rubber Band
  fallback hazards, with no unrelated engine changes made.


## Follow gain follow-up

Static audit completed after the +3 dB Follow implementation. The overall status
above and its existing Rubber Band blockers remain unchanged.

Audited realtime entry points and reachability:
- `processBlock` -> fixed hit capture -> `NoiseSidechain::finishBlock` -> existing
  noise render. The changed expression adds a compile-time scalar multiply only
  on the Follow branch and increases its multiplier clamp to the same constant.
- Re-inspected final mapping, processor chunk handoff and
  `BackgroundNoisePlayer::process` modulation multiplication. The renderer does
  not clamp modulation to unity, so the extra gain reaches the output.

Cross-thread ownership:
- `followTriggerGain` is a compile-time double with no dynamic initialization,
  ownership or reclamation. Existing audio-owned array/envelopes and parameter
  atomics retain the lifetimes described above. No new shared mutable state,
  buffer, synchronization or state serialization is introduced.

Verification:
- Static audit only; no build, runtime tests or listening checks were run.
- For finite depth d and envelope L in [0,1], the Follow multiplier is
  `1-d+d*B*L`, with B=1.4125375446227544 (approximately +3 dB). Its bounds are
  [0,B]. At full Follow it is B*L, so no-hit silence remains exact; neutral is
  exactly unity. Pump retains its original expression and [0,1] clamp.
- The boost is inside the existing smoothed depth blend. Envelope timing,
  retriggering and 80/20 strength are unchanged. No additional discontinuity at
  neutral, loop, allocation, destructor, lock or per-frame transcendental occurs.
- The existing <=512-frame loop and scratch indexing are unchanged for zero,
  single-frame and larger/changing host blocks. The fixed multiplier is bounded
  and finite; source sanitization and finite base-gain limits remain in place.

Residual risks:
- The boosted noise has 3 dB higher peaks at full Follow, so output headroom is
  lower. This does not add limiting; mixing can exceed 0 dBFS as before.
- Listening/host and macOS/Windows builds remain unperformed. No new realtime
  blocker was found; the pre-existing broader-path blockers remain unresolved.
