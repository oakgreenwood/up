# Background noise realtime audit

This records the initial noise implementation. The subsequent
[sidechain audit](realtime-audio-audit-noise-sidechain.md) covers current chunked
rendering and the resolved long-MIDI allocation path.

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock`, `processBlockBypassed`, and
  `getTailLengthSeconds`.
- Noise parameter reads, with constructor loading, prepare/release, editor
  attachments, state save/restore and teardown as lifetime/publication partners.

Reachability inspected:
- `processBlock` -> two cached APVTS scalar loads ->
  `BackgroundNoisePlayer::process` -> finite/clamped targets -> JUCE
  `Decibels::decibelsToGain`, linear `SmoothedValue`, immutable buffer reads,
  interpolation/loop overlap and output addition.
- Source changes -> fade target zero -> scalar index/position replacement ->
  fade in. Silence -> reset scalar state -> early return without source reads.
- Bypass -> noise reset -> `SmoothedValue::setCurrentAndTargetValue` and scalars.
- Tail query -> gain atomic -> infinite enabled tail or existing finite tail.
  The local JUCE VST3 wrapper explicitly maps infinity to `kInfiniteTail`.
- Existing MIDI activity and JUCE synth owning-message dispatch; existing
  realtime-warp start/reset/ratio changes and its output-feeding loop were
  re-inspected for the already documented blockers. Unchanged percussion DSP
  remains covered by the prior [EQ audit](realtime-audio-audit-equaliser.md)
  and its linked audits; this change does not newly certify unrelated DSP.

Findings:

[BLOCKER] `Source/PluginProcessor.cpp:370`, `:376`
Call path: activity handling / JUCE synth dispatch -> MIDI metadata.getMessage.
Issue: Existing long/SysEx messages create and destroy owning JUCE MIDI storage
on audio. The synth also converts the same unfiltered metadata internally.
Why it matters: Unsupported long messages can allocate and deallocate in render.
Minimal fix: Filter unsupported long messages through non-owning metadata before
both paths. This predates the noise implementation and remains unresolved.

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:154`, `:328`, `:345`
Call path: fallback start / ratio changes / loop reset -> Rubber Band R2 reset
and reconfiguration; render -> repeated feeding until output becomes available.
Issue: Existing R2 reset scavenges resources; reconfiguration can allocate
windows/resamplers, and the feeding loop lacks a practical callback work budget.
Why it matters: Prepared wrapper buffers do not bound internal allocation,
reclamation/possible contention, or callback duration.
Minimal fix: Bound those library operations and fallback work or move preparation
off audio. These existing blockers remain outside the noise feature's scope.

[RISK] JUCE synth and parameter framework
Call path: unchanged synth dispatch -> CriticalSection; host automation -> JUCE
parameter listeners and editor attachments.
Issue: Framework synchronization remains; this is not a whole-plugin lock-free
guarantee. Noise adds no listener, lock or worker on the render path.
Why it matters: Preparation/inventory mutation and teardown must stay stopped.
Minimal fix: Preserve that lifecycle; assess existing framework contention
separately, as documented in prior audits.

Cross-thread ownership:
- Noise buffers and inventory: processor member constructed before rendering.
  Construction allocates/decodes all WAVs, validates length/rate/channels and
  sanitizes samples. Once constructed, the vector and buffers are immutable.
  Audio borrows references by a bounded index; UI/state read only embedded names.
  Publication is the processor's pre-render construction lifecycle. Destruction
  occurs at stopped processor teardown, never source changes or mute. Rapid
  replacement changes only an integer; every previous source stays alive.
- Noise source/gain: APVTS owns scalar atomics for the processor lifetime.
  Host/UI/state publish through APVTS; audio caches pointers at construction and
  loads with relaxed ordering. The pinned JUCE parameter implementations assert
  float atomics are always lock-free. Independent values do not require a
  snapshot or resource reclamation. APVTS state trees and source-name remapping
  never enter the new render branch.
- Playback position, active source and gain smoother: audio-owned scalar state;
  prepare/release may reset only outside concurrent rendering. Bypass resets on
  audio, without replacing buffers. UI/state never mutate the playhead directly.
- Noise widgets/attachments: editor-owned and destroyed on the message thread;
  attachments are declared after controls and therefore destroyed first. Closing
  the editor does not destroy playback resources or reset saved gain.

Verification:
- Static audit only, after the final implementation changes. Inspected source,
  final integration diff, JUCE binary-resource naming/CMake generation,
  reader/buffer interfaces, parameter attributes/attachments, SmoothedValue,
  VST3 tail conversion, and existing synth/Rubber Band dependencies.
- No build, executable test, pluginval, GUI inspection, listening comparison or
  runtime allocation/profile check was run, following the repository's explicit
  instruction to run build/validation only when requested.
- Zero samples/channels return without advancement; one-frame and oversized
  blocks use the same loop bounded by host frames and at most two channels.
  There is no source-length scan, retry loop, scratch growth or owning temporary
  in render. Source decoding/scanning is constructor-only.
- Rates are finite within 1000..768000 Hz; source length is positive and fits
  int. Loop overlap is at most half the source. Interpolation wraps its second
  index; modulo uses a strictly positive period and leaves position in range,
  even when one output frame skips multiple short loops. One-frame sources work
  without an overlap. Source switches reset position before reading the new file.
- Gain targets are zero or bounded by -40..+10 dB. Non-finite gain mutes; invalid
  source indices fall back or clamp. Sanitized source values and interpolation
  stay bounded. The processor's existing denormal guard covers noise rendering.
- Linear ramp length is 10..7680 host frames, advances once per output frame and
  reaches exact zero. Stop resets on the final fade sample, then reads no source.
  Loop boundary overlap and source fades add bounded arithmetic, without heap
  storage or audio-thread destruction. One gain exponentiation occurs per block.

Residual risks:
- Existing long-MIDI and Rubber Band blockers remain unresolved. Static review
  found no new blocking issue in noise rendering/publication/lifetime.
- macOS arm64/x86_64/universal and Windows compilation and host/UI behavior are
  untested. Source switching, repeated loops and automation need listening tests.
- Preloading increases per-instance memory and construction time; embedded WAVs
  increase each plugin binary's size. Linear sample-rate conversion is not a
  band-limited resampler. The loop overlap slightly shortens the repeated period.
- Adding noise at +10 dB can exceed full-scale; no limiter was requested/added.
  Hosts may suspend processing when their audio engine is stopped, independently
  of the plugin's transport-independent rendering and infinite enabled tail.

Conclusion:
- Noise adds bounded, allocation-free render work under static inspection; the
  wider inspected realtime path remains FAIL due to the existing blockers above.
