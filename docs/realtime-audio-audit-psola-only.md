# Single PSOLA Formant option

Updated after the 95% EQ, 15% saturation-blend increase and negative-formant
makeup gain refinement.

Realtime audio audit: FAIL

Audited realtime entry points:
- Processor `processBlock`/float bypass, synth MIDI dispatch and voice rendering.
- Voice start/stop, stealing, source completion and effect drain.
- Parameter callbacks and preparation/teardown as lifetime dependencies.

Reachability inspected:
- Four-entry parameter registry -> scalar Formant cache -> one ratio load ->
  PSOLA and retained EQ/saturation/makeup-gain target updates.
- Voice source render -> fixed stereo/128-frame scratch -> PSOLA -> retained
  `formantColouration` LPC EQ/saturation -> smoothed makeup gain -> mix/global effects.
- Note start -> reset both remaining stages; source end -> combined drain ->
  decrement-only warp lease release and sound-reference release.
- Preparation -> stop voices -> prepare PSOLA rings/coloration tables -> latency
  reporting. Teardown clears voices, joins worker, then clears original sounds.
- UI attachment/selection/Apply to All/save/restore through the surviving PSOLA
  ID. The inert old LPC slot has no realtime cache or registered listener.
- Existing DSP internals and ownership retain the inspection in the historical
  [Formant3 audit](realtime-audio-audit-formant3.md). The leading LPC instance is
  removed entirely from construction, preparation, note reset and rendering.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:129`, `:157`
Call path: warp cache miss/transition -> start/render -> engine preparation,
reset, required-input scratch growth and no-output feed loop.
Issue: Existing audio-reachable engine allocation/reclamation, buffer resizing
and practically uncontrolled feed work remain.
Why it matters: The unchanged warp fallback can allocate or overrun audio time.
Minimal fix: Prepare engine configurations and bounded scratch/work outside
rendering, or move fallback rendering off audio. This is outside option removal.

[BLOCKER] `Source/PluginProcessor.cpp:316`
Call path: activity/synth MIDI dispatch -> `metadata.getMessage`.
Issue: Existing long MIDI/SysEx materialization may allocate and free storage.
Why it matters: Unsupported long host messages can trigger audio-thread heap work.
Minimal fix: Filter unsupported long messages through non-owning metadata before
both activity handling and synth dispatch.

[PERF] `Source/PercussionVoice.cpp:101`
Call path: voice rendering -> PSOLA detector/grains -> LPC coloration/saturation.
Issue: Remaining DSP costs are unchanged: bounded YIN analysis (at most 136 * 192
differences per approximately 10 ms), bounded pitch-mark search, at most one grain
per frame with half-period retry spacing, and at most `4 * Pmax + 1` grain frames.
Coloration uses six 512-point complex FFTs per 128-frame hop and a 20-order solve.
The new makeup ramp advances once per frame with two channel multiplications
when active. Gain logarithm/power conversion occurs only on ratio changes or
note reset, with no new per-frame transcendental operations.
Why it matters: Dense retriggers/high polyphony still need CPU measurement.
Removing the leading LPC instance reduces storage, reset work and active DSP;
neutral still skips grain/spectral correction while retaining fixed delay.
Minimal fix: Measure target CPU and tiny-block/full-polyphony behaviour before
making a performance guarantee. No benchmark was requested or run.

[RISK] `Source/Effects/FormantShifter.h:39`
Call path: retained coloration -> JUCE complex FFT.
Issue: Windows fallback FFT has a private per-instance SpinLock; macOS uses
prepared vDSP plans. The remaining instance is only rendered by its voice's
audio thread under the existing stopped-preparation lifecycle.
Why it matters: Sharing the instance or concurrent preparation would invalidate
the non-contention argument. This is not a claim of a lock-free FFT backend.
Minimal fix: Preserve private ownership and off-render preparation/destruction.

Cross-thread ownership:
- Formant parameters: fixed scalar atomics, compile-time lock-free float check,
  UI/host/state writers and audio ratio reader. Relaxed independent scalar stores
  suffice; later values overwrite earlier values with no ownership transfer.
  The obsolete LPC atomic arrays/functions are removed. No queue is added.
- Registry/group identity: immutable definitions and loaded group inventory;
  atomic selected index. State trees remain on existing save/restore/UI paths.
  No tree lookup, copy or replacement enters rendering or the parameter callback.
- PSOLA vectors, detector state, FFT/tables/rings and saturation history: direct
  voice ownership. Allocation occurs at construction/preparation; audio mutates
  prepared state only. Note reset fills prepared storage. Destruction occurs
  after rendering stops when voices are cleared, with one fewer FFT instance.
- Original samples/metadata/layer maps: loader-created immutable data retained
  by the synth inventory, so voice-reference release cannot destroy a live sound.
  Worker shutdown precedes sound-inventory destruction.
- Makeup gain: a direct voice-owned scalar SmoothedValue, prepared outside
  rendering, seeded at note reset, and mutated only by audio during playback.
  It derives from the same sanitized ratio already read for coloration. There
  is no new shared object, queue, parameter, publication or reclamation path.
- Warp snapshots: worker-owned immutable fixed slots with release/acquire
  publication, at most two audio pin attempts and decrement-only release.
  Replacement cannot reclaim a buffer pinned by a draining voice; worker reclaims
  retired slots after the final reader. The reduced drain shortens retention.
- Tempo flags/activity/UI: existing scalar atomic handoffs remain. Knobs,
  attachments and listener bindings are message-thread-owned. Editor tracking
  callbacks return off that thread. Synth locks and reserved steal storage retain
  the fixed eight-voice lifecycle; no concurrent inventory mutation is added.

Verification:
- Static audit only, after final code changes. Inspected the surviving registry,
  cache declarations/definitions, attachment, renderer registration, note/reset/
  render path, host latency/tail, state synchronization and teardown. Revisited
  JUCE FFT backends and existing warp/MIDI blockers. After the latest changes,
  re-inspected gain preparation/reset, sanitized target conversion, stereo frame
  processing and the new EQ/saturation bounds.
- PSOLA is unchanged. EQ uses 95% of the original bounded log gain (+/-22.8 dB)
  and saturation blend rises from 0.216 to 0.2484 (1.15 times). The existing
  positive 0.5..2 ratio limits, finite checks, fixed model/lag/table bounds,
  numerical floors, prepared rings and feed-forward corrections remain as audited.
- Makeup target is `pow(10, 3 * clamp(-log2(ratio), 0, 1) / 20)` for ratio below
  unity, otherwise exactly 1. Both callers sanitize to finite 0.5..2 first, so
  the target remains 1..approximately 1.41254. No zero/negative multiplicative
  target, unchecked logarithm or runaway feedback is possible. A 20 ms ramp
  initialized in prepare advances once per frame, shared across channels, after
  saturation. Note reset seeds it immediately; moving to nonnegative Formant
  ramps to unity, and steady unity skips multiplication. Finite output checks
  prevent extreme finite input overflowing the boost into a non-finite output.
  The gain adds no allocation, lock, loop, delay line or audio tail memory.
- Zero voice blocks return; positive blocks use at most 128 scratch frames.
  Source completion sets a positive drain, which strictly decreases until release.
  Both remaining stages reset on new notes, and prepare stops voices before
  allocating/updating sample-rate-dependent resources.
- Latency is `512 + 4 * ceil(rate / 60) + 1`, including at neutral: 3713 frames
  at 48 kHz. Drain is PSOLA's existing tail plus 1024 coloration frames: 6627
  frames at 48 kHz. The processor and voice use matching formulas and valid-rate
  fallback. Silent bypass still clears existing output storage.
- PSOLA's serialized ID `sampleFormant3Semitones` and host position are preserved,
  now displayed as Formant. Old PSOLA state/automation remains mapped to this
  effect. The old LPC slot `sampleFormantSemitones` is inert and excluded from
  selection/cache/Apply to All. Missing PSOLA state defaults to zero; old LPC
  values are never reinterpreted as PSOLA settings.
- No builds, runtime tests, plugin validation, CPU or listening tests run, per
  repository instructions. Static review does not establish runtime sonic quality.

Residual risks:
- macOS arm64/x86_64/universal and Windows runtime behaviour remains untested.
- Existing warp/MIDI blockers, tracking errors on noise/chords/short hits,
  grain aliasing, and coloration level changes remain. The single option still
  includes LPC-based EQ as requested in the earlier PSOLA refinement.
- Stronger EQ/saturation and up to +3 dB makeup can increase output level and
  downstream clipping; the makeup cap limits the added boost, not total sample
  peak level. Listening and CPU behaviour have not been measured for this tuning.
- Live-monitoring delay remains about 77 ms at 48 kHz; hard stops/stealing can
  truncate delayed tails. Projects formerly using the removed LPC option will
  sound different, and its reserved host slot may appear as `Unused (legacy)`.

Conclusion:
- No new blocking hazard was found in the current Formant refinement. The wider reachable
  render path remains FAIL due to the existing warp and long-MIDI blockers.
