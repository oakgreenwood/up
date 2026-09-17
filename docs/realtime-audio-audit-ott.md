# Global OTT compression

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock`, including the new global OTT
  stage after Rzhavchina and its cached APVTS parameter read.
- Float `processBlockBypassed`, which clears output and does not run OTT.
- Preparation/release, parameter attachment and state restore as lifecycle and
  publication partners. Existing synth/voice/warp paths were checked for the
  previously documented blockers; this is not a fresh certification of every
  unrelated DSP implementation.

Reachability inspected:
- `processBlock -> atomic<float>::load -> OttProcessor::process`.
- OTT -> finite input -> two Linkwitz-Riley splits and low-branch allpass ->
  stereo-linked RMS power envelopes -> detector level including +5.2 dB input ->
  depth-scaled input/upward/downward/output gain on every frame -> summed
  output and smoothed bypass -> finite float conversion.
- JUCE `LinkwitzRileyFilter::processSample`, `SmoothedValue::setTargetValue` /
  `getNextValue`, and `AudioBuffer` pointer access; preparation-only filter
  `prepare` / coefficient updates and fixed-size resets.
- Parameter layout -> ordinary slider attachment -> APVTS raw atomic; state
  save/copy and restore/replace, including a missing-OTT migration. OTT has no
  sample-specific callbacks, per-note storage or Apply to All registration.
- Existing `processBlock -> MIDI metadata.getMessage -> Synthesiser` dispatch;
  voice original/cache/realtime-warp branches and fixed scratch; warp lease
  acquisition/release, worker publication/reclamation and processor teardown.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:154`, `:328`, `:345`
Call path: voice fallback start/rate change/loop wrap -> Rubber Band reset and
reconfiguration; render -> repeated input feeding until output becomes available.
Issue: Existing Rubber Band internals may allocate/reclaim storage and the
wrapper feed/reset loop lacks a practical per-callback iteration budget.
Why it matters: Preallocated wrapper buffers do not bound library allocation or
callback execution time. `R2Stretcher::reset` scavenges retired resources and
reconfigures; reconfiguration can create windows/resamplers and resize buffers.
Minimal fix: Bound library capacities/reclamation and fallback work, or prepare
fallback audio off thread. These existing issues are outside the global OTT
change; see [the fixed-capacity audit](realtime-audio-audit-warp-fixed-capacity.md).

[BLOCKER] `Source/PluginProcessor.cpp:330`
Call path: activity handling and JUCE synth dispatch -> metadata.getMessage ->
owning MidiMessage construction/destruction.
Issue: Existing handling copies long MIDI/SysEx messages into heap storage;
JUCE's synthesis path independently repeats this conversion.
Why it matters: Unsupported host messages can allocate and free memory on audio.
Minimal fix: Filter unsupported long messages through non-owning metadata before
both activity handling and synth dispatch. This predates OTT.

[RISK] JUCE synthesis and parameter attachments
Call path: render/note dispatch -> Synthesiser CriticalSection; host automation
-> APVTS listener notification and attachment asynchronous UI update.
Issue: Existing JUCE synchronization/notification mechanisms remain. OTT uses
the same APVTS attachment pattern as the existing controls, not custom UI calls
from rendering.
Why it matters: Concurrent synth inventory changes or attachment/framework
contention invalidate a strictly lock-free claim for the whole plugin.
Minimal fix: Keep inventory/preparation/teardown outside active rendering and
assess framework automation notification contention separately.

Cross-thread ownership:
- OTT amount: the processor's APVTS creates and owns the scalar atomic for its
  lifetime. UI/host/state writes use the parameter system; audio loads through
  a constructor-cached pointer with relaxed ordering. Existing compile-time
  assertions require float atomics to be lock-free. Replacing the scalar value
  transfers no resource ownership and requires no reclamation.
- OTT filters, power envelopes and depth/bypass smoothers: processor-owned members.
  Stopped preparation allocates two-channel JUCE filter vectors and computes
  coefficients. Only rendering mutates history while active; UI and state
  restore touch only the parameter. Release resets fixed state; processor
  destruction releases vector storage after rendering has stopped. Repeated
  preparation and sample-rate changes reset history before reuse.
- Per-frame band samples, gains and dry values: small fixed stack arrays with
  trivial destruction. No shared pointer, queue, heap scratch or dynamic
  container is created or destroyed by OTT processing.
- Existing original samples/metadata/inventory: loaded before playback, then
  retained and immutable while voices and the worker read them. Teardown stops
  voices, joins the worker, then clears original sounds. OTT receives only the
  already-mixed host buffer and does not acquire sample ownership.
- Existing warp snapshots: worker-owned fixed slots use release/acquire
  publication and bounded reader pin attempts. Audio release decrements a
  count; worker reclamation waits for no readers. Replacements preserve pinned
  predecessors. OTT adds no interaction with this protocol.
- Editor controls/attachments and saved ValueTrees: existing message-thread /
  host state lifecycle. Migration creates its PARAM child in state restore,
  never from the render path. State restore does not reset live OTT filters.

Verification:
- Static audit after the final RMS/calibration implementation, plus an isolated
  native C++ build/render/check harness on macOS arm64. The actual
  `OttProcessor.cpp` linked against existing JUCE objects. No full plugin build,
  pluginval, GUI run, sanitizer or Windows/x86_64 runtime check was performed.
- The reference comparison requested by the user used the supplied dry WAV as
  input to both the previous and revised C++ processor at 100% Amount. RMS
  level error over 10 ms windows fell from 18.35 to 3.73 dB; the revised sample
  peak was -7.84 dBFS versus -7.05 dBFS in the reference. See
  [the comparison report](ott-reference-comparison.md) for methodology and limits.
- RMS power smoothing replaces amplitude smoothing. Effective low/mid/high
  attacks are 31.07/14.56/8.775 ms; release times are calibrated to
  27.495/27.495/12.87 ms. Preparation uses positive finite denominators and
  coefficients strictly between zero and one over 1000..768000 Hz. The power
  envelope is a bounded weighted average of nonnegative squared magnitudes.
  Invalid preparation rates fall back to 44100 Hz. Rate changes reset history.
- Gain now comes from the current frame's envelope, removing the 16-frame
  hold/interpolation and its stale upward gain on new hits. Depth and bypass
  retain their 20 ms ramps (at most 15360 frames). Per-frame work is bounded by
  three bands and two channels, with no queue, backlog or while loop.
- Per-frame log/power evaluation adds CPU cost. A local timing sample processed
  21.33 seconds of stereo input in 0.308 seconds, including buffer refill and
  Debug JUCE filter calls. This supports practical local use, not a worst-case
  CPU guarantee on all targets. Zero depth skips these log/power evaluations.
- Zero/no-channel calls return before pointer access. The C++ harness verified
  exact output equality between continuous rendering and varying blocks of
  0, 1, 3, 16, 257, 4096, 7 and 511 frames. Storage is independent of host block
  length. Mono/stereo, silence, equal/opposite-polarity channels and zero-depth
  bypass checks passed.
- Non-finite input is removed before recursive state. Squaring float-range
  audio after stable double filtering remains in double range. `10*log10`
  reads positive power; the -120 dBFS detector floor bounds upward gain to
  approximately 60.055 dB. Including input, the largest band makeup and master
  trim gives at most 70.755 dB total boost. Exponentiation is finite and output
  conversion checks float range. ScopedNoDenormals protects decaying state.
  Maximum float, NaN/Inf, subsequent silence, rate changes and invalid Amount
  checks all produced finite output in the harness.
- Low/mid/high makeup is +12.5/+7.75/+11 dB, calibrated from the reference;
  thresholds and reciprocal ratios are unchanged. The +5.2 dB input appears
  in detector level and once in the applied gain. The -7 dB master trim is
  applied after the compression curve, scaling with depth. Curve segments
  remain continuous, with slopes 1/4.17, 1 and 1/66 (zero for the high band).
- Guarded process calls reported zero C++ allocations and deallocations.
  This instrumentation covers `new`/`delete`, not every possible allocator;
  source inspection also confirms the new path has no allocation, owning
  pointer release, file access, lock, UI call or coefficient rebuild.
- The new parameter is appended, saving/restoring through the existing APVTS
  tree. Missing older state explicitly inserts zero before replaceState, whose
  default behavior would otherwise retain the current value. Sample selection
  and Apply to All continue to iterate their unchanged six-entry registry.

Residual risks:
- Existing warp and long-MIDI blockers prevent a plugin-wide realtime pass.
  No new blocking issue was found in the OTT processing/state handoff.
- The isolated macOS arm64 processor compiled and passed focused checks; full
  host/plugin behavior, macOS x86_64 and Windows remain untested. Changes use
  portable C++17 and the existing JUCE DSP module.
- Calibration uses one reference loop with internal held-out sections. The
  result is closer in dynamics, not waveform-identical; additional material
  needs listening comparisons. This is not a peak limiter, and transient
  overshoot remains possible. Filter phase, knee and detector behavior can
  differ from Ableton. The short bypass transition can affect magnitude.
- Filters/detectors run at zero for warm re-entry. Full-plugin CPU under load,
  maximum-rate worst-case timing and the 0.5-second tail allowance are not
  comprehensively measured.

Conclusion:
- The new OTT stage has bounded, allocation-free render work by inspection.
  The wider inspected path remains FAIL because of existing warp and long-MIDI
  blockers. Focused native DSP checks passed; full plugin/platform and broader
  listening verification remain outstanding.
