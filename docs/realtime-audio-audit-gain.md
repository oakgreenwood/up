# Sample Gain realtime audio audit

Realtime audio audit: FAIL

Audited realtime entry points:
- Processor `processBlock`, inherited bypass behavior, synth MIDI dispatch.
- `PercussionVoice::startNote`, `stopNote`, `renderNextBlock`.
- Processor and editor parameter listeners reached by host automation.

Reachability inspected:
- Parameter callback -> four-entry registry -> `setGainDbForMidiNote` -> fixed
  atomic arrays. Selection, Apply to All, and save/restore use the same registry.
- Voice note start -> cached linear gain -> smoother reset/current value.
- Voice render -> cached linear target -> sample renderer (mono/stereo) or
  realtime warp renderer -> one smoother advance per output frame, before mixing.
- Renderer -> ADSR, PunchEnvelope, SustainTailShaper, NoteStartDeclicker.
- Voice playback switches -> one-shot/warp cache acquisition and lease release;
  original sound/metadata access and existing realtime Rubber Band fallback.
- Pinned JUCE SmoothedValue reset/target/step implementation, parameter constructor,
  synth locking/dispatch, and MidiMessage allocation implementation.

Findings:

[BLOCKER] Source/Playback/RealtimeWarpPlayer.cpp:49, :120, :157, :340
Call path: voice start/render -> realtime warp start/prepare/render/ensureBuffers.
Issue: Existing engine reconstruction and scratch-buffer growth remain reachable
on audio. The feed/reset loop has no practical cap for tiny looping sources that
reset before producing output.
Why it matters: Allocation/destruction and uncontrolled work can interrupt audio.
Minimal fix: Prepare engines off audio, use bounded chunks in fixed scratch
storage, and cap feed/reset work. These pre-existing issues are outside Gain.

[BLOCKER] Source/PluginProcessor.cpp:334
Call path: processBlock activity dispatch / JUCE synth dispatch -> getMessage ->
MidiMessage heap allocation and destruction for long MIDI messages.
Issue: Existing long-message handling can allocate and free on audio.
Why it matters: Long host MIDI/SysEx messages can interrupt audio.
Minimal fix: Inspect unsupported messages as views and filter before materializing
or passing them through synth dispatch. This pre-existing issue is outside Gain.

Cross-thread ownership:
- Gain: processor-owned fixed arrays of 128 scalar float atomics. Construct/reset
  outside render; parameter callbacks and control-thread edits write. Voices read
  only the linear scalar. Float lock-freedom is compile-time asserted for supported
  macOS/Windows builds. Relaxed ordering suffices for independent scalar values;
  audio never depends on a coherent pair of dB/linear reads. No owning objects,
  queues, destruction, locks, or state-tree access are added to audio.
- Smoothing: inline voice-owned scalar state, mutated only by voice playback.
  Unchanged targets do not restart ramps. Voice reuse resets gain to the new
  group's value; playback switches retain the current ramp. No heap reclamation.
- Registry: immutable literal IDs and capture-free function pointers. UI knobs,
  attachments, image renderers and labels are message-thread objects. Existing
  editor callbacks reject other threads before accessing mutable UI tracking.
- Original sounds, sample inventory, and metadata remain immutable while playing,
  retained by the synth. Teardown stops voices, joins cache workers, then clears
  sounds. Gain changes neither source buffers nor metadata.
- One-shot and warp snapshots: workers publish fixed slots with release/acquire;
  voices pin via atomic reader counts, with at most two acquisition attempts.
  Releases decrement counts only. Workers replace/reclaim unpinned retired slots;
  repeated replacements never mutate pinned slots. Gain adds no cache jobs.
- APVTS and per-group ValueTree persistence remain outside render. Missing Gain
  entries restore via the registered 0 dB default. Existing JUCE synth/listener
  locks remain framework mechanisms; Gain adds no new lock or sample mutation.

Verification:
- Static audit only after final relevant code changes; no build, tests, plugin
  validation, listening test, or GUI run, following AGENTS.md.
- Inspected both renderer signatures/call sites and all three sample output loops.
  Gain advances once per frame, independent of channel count or sub-block size.
  Zero-length voice calls return without advancing. Gain needs no scratch buffer
  and adds constant work per sample even for larger host blocks.
- Finite values are clamped to -10..10 dB before a single conversion per write,
  yielding about 0.316..3.162 linear gain. Invalid notes return unity; non-finite
  writes are ignored. Neutral gain is exactly unity. New notes configure the
  10 ms ramp using their playback rate, with a fallback for invalid rates.
- Reviewed registry-driven selection refresh, save/restore, and Apply to All.
  Parameter is appended to the layout, preserving existing parameter order.

Residual risks:
- Existing warp and long-MIDI blockers prevent a plugin-wide realtime pass.
- macOS/Windows builds, host automation/state round trips, UI, and listening
  checks remain unperformed. Positive gain can increase output peaks.
- Concurrent automation and batch edits retain existing per-group publication
  semantics; batches are not atomic across all groups.

Conclusion:
- No new realtime blocker was found in Gain. The overall inspected playback path
  remains FAIL due to the pre-existing warp and MIDI issues above.
