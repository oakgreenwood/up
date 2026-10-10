# Sample trimming

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and `processBlockBypassed`.
- `PercussionSynthesiser` MIDI dispatch/render and `PercussionVoice::startNote`,
  `stopNote`, and `renderNextBlock`.
- APVTS sample-start/end callbacks that can run on the host audio thread.

Reachability inspected:
- MIDI note -> selected velocity layer/variation -> voice note start -> two
  per-note atomic trim reads -> proportional start and absolute end reduction
  -> bounded frame range for the selected sound.
- Voice render -> original or offline-warp buffer -> `SamplePlaybackRenderer`
  interpolation, boundary fade, end/loop handling -> voice effects and mix.
- Voice render -> live-warp cache miss/transition -> `RealtimeWarpPlayer`
  source feeding, short-loop wrap, boundary fade, and output retrieval.
- UI gestures/state restore -> APVTS callbacks -> scalar trim cache;
  waveform summary access stays off audio rendering.
- Typed START/END millisecond fields and keyboard arrows -> representative
  duration conversion on the message thread -> the same APVTS start/end
  parameters and per-note atomics as waveform handle gestures.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:77`, `:382`
Call path: warp sample start or long loop wrap -> `RubberBandStretcher::reset`
-> R2 `reset`/`reconfigure`.
Issue: The retained Rubber Band R2 fallback can scavenge/deallocate and
conditionally allocate windows, resamplers, or channel storage while resetting
or changing ratios. This predates sample trimming and remains reachable through
the existing live-warp path.
Why it matters: A cache miss or warp transition may perform heap work on audio.
Minimal fix: Prepare every required R2 configuration and reset behavior outside
rendering, or replace the live fallback with an allocation-free prepared path.

[RISK] `Source/PercussionSynthesiser.cpp:12`, `:19`
Call path: process block -> synth range render/MIDI dispatch.
Issue: JUCE's synthesiser uses a `CriticalSection`. Its immutable sound/layer
inventory is not edited during playback, and waveform selection only reads it,
but external concurrent synth mutation would contend with audio.
Why it matters: A contended lock can miss the callback deadline.
Minimal fix: Keep construction, preparation and teardown outside active render.

Cross-thread ownership:
- Trim values: APVTS/processor listeners write fixed lock-free float atomics
  per MIDI note. The representative duration is prepared while rebuilding the
  sample cache; end-param writes derive absolute seconds removed from that
  duration. Audio loads the proportional start and derived end duration once
  at note start and retains plain voice-owned values. Crossed updates resolve
  to at least one frame. State-tree storage and selection synchronization do
  not run in the voice.
- Original sounds, metadata and waveform bins: created during embedded sample
  loading; retained by the synthesiser and immutable during render. The editor
  borrows one summary from the representative sound; it does not read PCM.
  Processor teardown clears voices and joins the worker before freeing sounds.
- Offline warp caches: the existing worker publishes full immutable buffers;
  audio pins fixed slots through leases. Lease release only decrements a reader
  count, and worker reclamation frees retired buffers off audio. Trimming maps
  the latched end duration through the cache's time ratio without changing its
  data.
- Live warp engines and scratch: owned per voice and prepared outside render.
  Trim adds only scalar bounds and at most eight source-feed attempts per voice
  render call. The existing R2 reset/reconfigure behavior remains the blocker.

Verification:
- Static audit only. Project instructions prohibit builds and validation unless
  requested; no build, plugin validation, GUI run, or listening test was run.
- Reviewed default 0/1, crossed/automated endpoints, one-frame regions,
  different variation lengths and sample rates, representative-duration cache
  rebuild, direct/cached/live-warp playback, loop wrapping, source switches,
  zero and single-frame render calls, and larger host blocks.
- Source reads stay within `[start, end)`; interpolation clamps its look-ahead
  to `end - 1`. Loop modulo uses a positive range and guards floating-point
  rounding at the exclusive end. Full-length playback retains its prior bounds.
- Waveform bins are built off audio. UI draws fixed bins and emits host
  gestures; it never scans sample buffers during a drag or typed edit. Typed
  endpoints clamp to at least one representative frame between START and END;
  END retains the same absolute-duration reduction across group members.

Residual risks:
- The existing Rubber Band reset/reconfigure blocker prevents a realtime PASS.
- A long trimmed live-warp loop may reach that existing reset path more often
  than its full-length source would.
- No macOS arm64/x86_64, Windows, host automation, GUI, or listening run has
  verified the trim behavior. Very short live-warp loops may need several
  callbacks to fill the stretcher before output is audible.

Conclusion:
- The new trim reads and frame-bound calculations are bounded and allocation
  free, but the reachable live-warp fallback keeps the overall audit at FAIL.
