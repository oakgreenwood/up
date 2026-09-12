# Apply to All: realtime audio audit

Realtime audio audit: FAIL

Audited realtime entry points:
- Processor APVTS `parameterChanged`, including host/audio-thread automation.
- Editor `parameterValueChanged` / `parameterGestureChanged`, which JUCE may also
  invoke on a host/audio thread while the editor is open.
- `processBlock`, inherited `processBlockBypassed`, synth note dispatch,
  `PercussionVoice::startNote`, `stopNote`, and `renderNextBlock` as consumers of
  the copied sample-specific values.

Reachability inspected:
- `parameterChanged -> findSampleSpecificParameter -> registered writeCache
  callback -> SampleSpecificRealtimeCache`.
- Editor parameter listeners -> current native thread-ID comparison -> immediate
  return on audio. On the editor thread they record only scalar edit information;
  the existing 30 Hz timer formats labels and updates components later.
- Message-thread button -> `applySampleSpecificParameterToAll` -> registered cache
  writes and `SampleSpecificParameterState::setValue` -> selected APVTS value and
  host state-change notification. No audio/render caller reaches the batch method.
- `processBlock -> sampler.renderNextBlock -> startNote / renderNextBlock ->
  per-note Pitch / Punch / Keep length cache reads`.
- One-shot start -> fixed-slot pitch-cache lease; rendering -> sample renderer,
  Punch/sustain envelopes and declicker; voice end -> atomic lease release.
- Existing loop pitch updates -> warp cache lookup/build/switch and realtime
  Rubber Band fallback. Existing tempo prewarming and MIDI message dispatch were
  also inspected for remaining blockers.
- Pinned JUCE parameter attachments/listener dispatch, native thread-ID queries,
  parameter normalization, string comparison, synthesis, bypass, and MIDI storage.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:119`, `:156`, `:339`
Call path: loop start/render -> engine preparation and scratch-buffer growth.
Issue: Existing channel changes or large host/engine requests can reconstruct
the engine or resize buffers. The feed/reset loop lacks a practical work cap for
very short looping sources that reset before producing output.
Why it matters: Pitch changes on loops can reach allocation/destruction and
uncontrolled processing on audio.
Minimal fix: Prepare supported engines/scratch storage off audio, process bounded
chunks, and bound fallback feed/reset work.

[BLOCKER] `Source/PluginProcessor.cpp:334`
Call path: `processBlock` MIDI activity loop and JUCE synth dispatch ->
`MidiMessageMetadata::getMessage` -> `MidiMessage::allocateSpace` / destructor.
Issue: Existing long MIDI/SysEx messages use heap storage.
Why it matters: Hosts supplying these messages can cause audio-thread allocation
and freeing; normal note/controller messages fit inline.
Minimal fix: Inspect/filter unsupported long messages as views and avoid their
materialization in synth dispatch. This is an existing MIDI-path issue.

Cross-thread ownership:
- Registry: static immutable array of literal IDs and capture-free function
  pointers, initialized before processor use. No dynamic registry mutation,
  function-object allocation, ownership transfer, or audio-thread destruction.
- Per-note values: processor-owned fixed scalar atomics, initialized before
  playback; written by parameter callbacks, message-thread batch edits, or
  coordinated non-realtime restore. Voices and the pitch worker only read the
  atomics. Existing compile-time checks require float/bool lock-freedom. Pitch
  conversion runs once per write, within the existing -8..8 semitone bounds;
  Punch stays in 0..1 and Keep length converts at 0.5. Non-finite input is rejected.
- Editor tracking: vector and borrowed parameter pointers created on the message
  thread. The captured editor thread ID is immutable. Other threads return before
  reading mutable tracking state, touching UI, or allocating. Listener removal
  precedes destruction. Native thread-ID queries use `pthread_self` on macOS and
  `GetCurrentThreadId` on Windows; the added callbacks avoid JUCE's mutex-taking
  `MessageManager::isThisTheMessageThread` query.
- State trees: batch writes and save/restore may allocate and lock, but audio
  callbacks never access them. Save mirrors all registered cache values into
  `parameters.copyState()`; restore uses `parameters.replaceState()` and APVTS
  defaults for missing group values. The batch does not mutate sample inventory,
  synth voices, metadata, or buffer ownership.
- Original sounds/metadata: immutable during playback, retained by the sampler;
  the one-shot worker borrows them. Existing teardown stops voices, joins the
  worker, then clears sounds on the control thread.
- One-shot pitch snapshots: worker-owned fixed slots; publication uses
  release/acquire ordering, and voice leases pin slots via atomic reader counts.
  Audio acquisition makes at most two attempts; release only decrements a counter.
  Successive replacements use unpinned slots. Retired buffers are destroyed only
  by the worker or during coordinated teardown.
- Warp buffers now use persistent-worker-owned fixed slots and bounded audio
  leases; see `realtime-audio-audit-recent-warp-cache.md`. JUCE synth and
  parameter-listener locks remain framework mechanisms. The batch
  does not acquire the synth lock, and new editor callbacks do no UI formatting
  while JUCE holds its parameter-listener lock.

Verification:
- Static audit only, after the final relevant implementation changes.
- Inspected the changes against a snapshot of the pre-existing working tree and
  traced all registered callbacks to their concrete cache methods.
- Traced gesture start/change/end for sliders, wheel/keyboard edits, resets,
  accessibility edits, and buttons through pinned JUCE attachments; checked
  selection refresh suppression, global-effect exclusion, captured-value copying,
  empty inventory/unknown IDs/non-finite input, save/restore, and editor teardown.
- Checked zero/tiny/variable-block behavior: no batch loop or new scratch storage
  runs on audio. Voices/sample renderer return for zero frames, guard non-finite
  positions/ratios, clamp final-frame interpolation, and use bounded loop wrapping.
  The pre-existing realtime-warp fallback remains subject to the findings above.
- No build, runtime tests, plugin validation, sanitizer, or listening comparison
  was run, following the repository instruction to run them only when requested.

Residual risks:
- The realtime warp fallback and MIDI blockers prevent a plugin-wide realtime-safety pass.
- Group values publish individually, so playback concurrent with a batch can
  briefly observe some groups before others. Concurrent host automation remains
  another writer for the selected group.
- Applying Pitch/Keep length broadly can request multiple background renders.
  The one-shot coordinator prioritizes the five most recent groups, renders
  variations within each group on six workers, then prepares other groups. The
  separate warp worker retains its recent-sound scheduling. See
  `realtime-audio-audit-parallel-pitch-cache.md` for the one-shot pool audit. New one-shot hits use a previous prepared buffer or the original
  until ready; warped cache misses use the realtime fallback. Running one-shots
  keep their latched pitch/mode; Punch retains its existing live response.
- Build compatibility and host UI/state round trips on macOS/Windows still need
  runtime verification. Existing per-sample envelope/transient costs were inspected
  but not profiled; bulk copying does not add voices or render-thread traversal.

Conclusion:
- No new blocker was found in the registry dispatch, edit tracking, or batch
  publication. The overall inspected playback path remains unsafe for realtime
  use in the existing warp/MIDI cases described above.
