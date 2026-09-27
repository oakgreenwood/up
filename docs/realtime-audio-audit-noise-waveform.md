# Noise waveform and loop-region realtime audit

The later [import audit](realtime-audio-audit-noise-import.md) covers replaceable
imported sources, worker publication and portable saved audio.

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and `processBlockBypassed`.
- Transitively, `BackgroundNoisePlayer::process`, `resolveRegion`, `reset`,
  interpolation, overlap crossfade and JUCE scalar smoothing.
- Construction, preparation/release, APVTS state and editor lifetime were
  inspected as publication and ownership partners.

Reachability inspected:
- Host block -> cached start/end atomic loads -> bounded render chunks ->
  existing sidechain modulation -> noise region resolution -> scalar fades ->
  bounded interpolation/crossfade -> output mix and playhead wrap.
- Bypass -> noise reset; preparation -> sample-rate validation and ramp reset.
- Processor construction -> embedded WAV decoding/sanitization -> immutable
  1024-bin waveform summaries -> message-thread waveform painting.
- UI handles -> APVTS gestures/scalar targets -> next host-block parameter read.
- The unchanged voice, MIDI, detector and warp paths retain the findings in the
  [sidechain audit](realtime-audio-audit-noise-sidechain.md). This report extends
  that inspection for trim playback and waveform publication.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:154`, `:328`, `:345`
Call path: existing voice fallback start / rate change / loop reset -> Rubber
Band R2 reset/reconfiguration; render -> repeated feeding until output appears.
Issue: Reset can reclaim resources, reconfiguration can allocate, and fallback
feeding has no practical per-callback work budget.
Why it matters: Noise's bounded chunks and immutable buffers do not resolve
these pre-existing realtime hazards in percussion playback.
Minimal fix: Bound the fallback's internal resources, reclamation and feeding,
or prepare fallback audio off thread. This separate playback work remains open.

[RISK] `Source/PercussionSynthesiser.cpp:6`, `:14`
Call path: existing synth range rendering / MIDI dispatch -> JUCE synth lock.
Issue: The existing framework synchronization remains reachable.
Why it matters: Inventory changes during rendering could contend; this is not
a whole-plugin lock-free guarantee.
Minimal fix: Keep synth inventory/preparation/teardown outside active rendering.
The waveform editor does not call or mutate the synth.

Cross-thread ownership:
- Noise sources and peak summaries are processor-owned vector entries, created
  and fully populated in construction before rendering/editor creation. They
  are never replaced, resized or modified during playback. Audio borrows sample
  buffers; the UI borrows a const summary pointer. Processor teardown reclaims
  them after rendering and editor use stop. Source changes select an index;
  rapid consecutive changes cannot retire storage still in use.
- Start/end targets are APVTS-owned scalar atomics with processor lifetime.
  UI/host/state publish values; audio reads cached pointers once per host block.
  JUCE asserts float atomic lock-freedom on the supported targets. Independent
  start/end publications may cross temporarily; region resolution always gives
  a valid nonempty range for a nonempty source. No snapshot ownership changes.
- Region bounds, playhead, crossfade length and both smoothers are audio-owned
  scalar state. Preparation/release reset them with rendering stopped; bypass
  resets them on audio. A pending edit retains the old region during fade-out
  and adopts the latest requested region at zero gain. No queue or allocation.
- The UI timer, drawing and paired host gestures run on the message thread.
  Source changes, hiding the waveform and destruction end active gestures.
  Summary reads do not decode audio or invoke the audio engine. APVTS state
  copy/replace remains in state callbacks, outside the new render path.
- Modulation borrows the existing fixed sidechain array for the current chunk.
  Existing percussion/warp ownership remains as documented by the sidechain
  audit and its linked reports; trim adds no voice or cache ownership.

Verification:
- Static audit only, after the final relevant code changes. No build, executable
  tests, pluginval, UI launch or listening check was run, following the
  repository instruction to run builds/validation only on request.
- Each nonempty source resolves to `0 <= start < end <= sourceLength`, with an
  exclusive end. Nonfinite targets use defaults; finite targets clamp to 0..1.
  Crossing/equal bounds resolve to at least one frame, with start authoritative.
- Interpolation's second frame clamps to `end - 1`. Overlap is at most half the
  region, so its head reads stay inside and the wrap period stays positive.
  One-frame regions have no overlap. Sample-rate mismatch and very short loops
  use one modulo operation, without a source-length-dependent wrapping loop.
  The final endpoint guard handles floating-point rounding to the exclusive end
  when a small remainder is added to a large source offset.
- Validated source/output rates are finite in 1..768 kHz. The 2 ms region ramp
  and existing 10 ms gain ramp use JUCE scalar SmoothedValue operations, with no
  allocation, resource release or lock. Finite sanitized source samples and
  bounded sidechain/gain values keep the new DSP state finite.
- Muted/empty sources, zero frames and zero channels return without accessing
  audio. Source switches fade the old source before adoption. Region edits
  fade out, restart at the selected start and fade in; completed silence resets
  playback. Storage and processing bounds do not depend on host block size.
- Waveform bin arithmetic uses 64-bit products and reads only validated source
  frames, including sources shorter than 1024 frames. Construction computes
  peaks once. Painting visits at most 1024 bins; the 30 Hz UI timer only repaints
  when source or effective bounds change. No audio-thread UI operation occurs.
- Both parameters are appended, saved/restored with APVTS and default to the
  full source for old state. Trims are global normalized positions and remain
  unchanged when choosing another source. They are excluded from Apply to All.
- CMake includes the waveform component; the implementation uses existing JUCE
  and standard C++ APIs without platform-specific dependencies.

Residual risks:
- Existing Rubber Band blockers and synth synchronization remain unresolved.
  No new blocking allocation, reclamation, shared mutation, unbounded loop or
  invalid frame access was found in the inspected noise/trim path.
- Compilation on macOS/Windows, actual panel layout, host automation/restore,
  rapid trim edits and source changes still need runtime verification.
- The 2 ms fade suppresses discontinuities but repeated dragging can produce
  brief level dips. Very short regions naturally change the sound. Listening
  checks are still needed across sample rates and envelope settings.

Conclusion:
- The new waveform and trim path has bounded, allocation-free render work under
  static inspection. The overall inspected realtime path remains FAIL because
  of the pre-existing Rubber Band fallback blockers.
