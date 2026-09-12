# Remove preserved-length one-shot pitch

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and inherited bypass processing.
- Synth MIDI dispatch; `PercussionVoice::startNote`, `stopNote`, `renderNextBlock`.
- Parameter callbacks and voice preparation/teardown as playback dependencies.

Reachability inspected:
- Note start -> `beginPlayback` -> latch pitch -> original sample buffer ->
  `updateSampleRendererPitchRatio`.
- Render -> `SamplePlaybackRenderer::render` -> interpolation, ADSR, smoothed
  Gain, Punch, sustain, declicker -> `clearActivePlayback`.
- Parameter callback/Apply to All/state restore -> three-entry sample-specific
  registry -> scalar pitch/Punch/Gain cache -> voice reads.
- Warp note/cache transitions -> existing `WarpCachePrewarmer` leases and
  `RealtimeWarpPlayer`; processor teardown -> clear voices -> join warp worker
  -> clear sounds.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:119`, `:156`
Call path: warped sample cache miss -> realtime start/render -> prepare/reset/
buffer resizing/Rubber Band processing.
Issue: The current checkout retains the R2 fallback, with possible engine
reconstruction on channel changes, internal reset allocation/reclamation,
scratch growth, and a feed loop without a practical no-output work bound.
Why it matters: Warp misses remain capable of allocation or excessive work on
audio. Non-warp one-shots cannot enter this branch.
Minimal fix: Prebuild engine configurations, use fixed chunks and bound feeding,
or move fallback stretching off audio. This task does not alter warp behavior.

[BLOCKER] `Source/PluginProcessor.cpp:315`
Call path: process MIDI activity and JUCE dispatch -> `getMessage`.
Issue: Existing long MIDI/SysEx messages can allocate/free heap storage.
Why it matters: Host input can still trigger audio-thread heap operations.
Minimal fix: Filter unsupported long messages as views before materialization.

[PERF] `Source/Playback/SustainTailShaper.cpp:32`
Call path: sample render -> sustain gain.
Issue: Existing per-sample powers/exponentials and traversal of the immutable
transient inventory remain. Pitch-down increases the audible playback duration.
Why it matters: The removed worker pool saves background work; it does not remove
the cost of rendering longer voices or their shaping effects.
Minimal fix: Profile shaping before considering coefficient/state preparation.

Cross-thread ownership:
- Original sounds/metadata: constructed by the loader and immutable during
  playback. Voices borrow buffers; the synth retains sound ownership. Stopping
  a voice cannot release the synth's final sound reference. Teardown stops and
  clears voices, joins the remaining warp worker, then clears sounds.
- One-shot pitch snapshots, leases and workers: removed entirely. No one-shot
  cache publication, retirement or worker shutdown remains on any path.
- Warp snapshots: unchanged fixed worker-owned slots, release publication,
  bounded atomic reader acquisition, decrement-only audio release. Pinned old
  buffers survive replacements and are reclaimed only by the worker.
- Parameters: three fixed registry entries, scalar lock-free atomics for audio;
  pitch is read at one-shot note start. State trees and UI remain off the render
  path. The legacy mode parameter keeps its host slot and APVTS serialization,
  but has no registry entry, per-note cache, playback reader, or UI attachment.
- Voice ADSR/Gain/source position and borrowed metadata pointers: audio-owned
  during playback. No new allocation, destruction, lock, or container growth.
- JUCE synth synchronization and the unchanged warp fallback retain their prior
  constraints; the removed workers no longer share access to sample data.

Verification:
- Static audit after the final code changes. Inspected neutral/positive/negative
  one-shot pitch, per-sample selection and Apply to All, saved mode=true/false/
  missing, note-off, voice stealing, zero/tiny/large blocks, mono/stereo output,
  source-rate conversion, sample end, warp-to-one-shot voice reuse and teardown.
- Source step remains `sourceRate / hostRate * pitchRatio`; one-shot Punch uses
  `1 / pitchRatio` to map transients, keeping its rise/decay in playback seconds.
  Existing finite/range checks, end handling and interpolation bounds remain.
- The original eight-voice count and host parameter order are preserved. CMake
  only drops the deleted worker files; no platform or dependency changes.
- Source search found no remaining one-shot cache/mode/UI references except the
  intentionally inert host parameter. No build, runtime tests, plugin validation
  or listening test was requested or run, per repository instructions.

Residual risks:
- Existing projects that enabled Keep length now play those one-shots with
  pitch-dependent duration. The legacy parameter may still appear in a host's
  generic parameter list; changing it has no effect.
- The existing R2 warp and long-MIDI blockers remain. Host runtime behavior on
  macOS arm64/x86_64 and Windows has not been tested for this change.

Conclusion:
- Removing the mode adds no realtime hazard to one-shot playback and removes
  its preparation pool. The wider reachable plugin path remains FAIL because
  of the retained warp fallback and long-MIDI blockers.
