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
  return on audio. On the editor thread they record only a borrowed parameter
  pointer; the existing 30 Hz timer changes the effect dropdown later.
- Message-thread button -> `applySampleSpecificParameterToAll` -> registered cache
  writes and `SampleSpecificParameterState::setValue` -> selected APVTS value and
  host state-change notification. The source value is one atomic cache read for
  the selected sample. No audio/render caller reaches the batch method.
- `processBlock -> sampler.renderNextBlock -> startNote / renderNextBlock ->
  per-note Pitch / Punch / Gain / Formant cache reads`.
- One-shot start -> scalar Pitch ratio snapshot; rendering -> direct sample
  varispeed, Punch/sustain envelopes and declicker.
- Existing loop pitch updates -> warp cache lookup/build/switch and realtime
  Rubber Band fallback. Existing tempo prewarming and MIDI message dispatch were
  also inspected for remaining blockers.
- Pinned JUCE parameter attachments/listener dispatch, native thread-ID queries,
  parameter normalization, string comparison, synthesis, bypass, and MIDI storage.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:49`, `:120`, `:155`, `:338`
Call path: loop start/render -> engine preparation and scratch-buffer growth.
Issue: Existing channel changes or large host/engine requests can reconstruct
the engine or resize buffers. The feed/reset loop lacks a practical work cap for
very short looping sources that reset before producing output.
Why it matters: Pitch changes on loops can reach allocation/destruction and
uncontrolled processing on audio.
Minimal fix: Prepare supported engines/scratch storage off audio, process bounded
chunks, and bound fallback feed/reset work.

[BLOCKER] `Source/PluginProcessor.cpp:316`
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
  coordinated non-realtime restore. Voices and the warp worker only read the
  atomics. Existing compile-time checks require float/bool lock-freedom. Pitch and
  Formant conversion runs once per write, within -12..12 semitone bounds; Punch
  stays in 0..1 with 0.01 cache snapping and Gain in -20..20 dB. Non-finite input
  is rejected.
- Editor tracking: vector and borrowed parameter pointers created on the message
  thread. The captured editor thread ID is immutable. Other threads return before
  reading mutable tracking state, touching UI, or allocating. Listener removal
  precedes destruction. Native thread-ID queries use `pthread_self` on macOS and
  `GetCurrentThreadId` on Windows; the added callbacks avoid JUCE's mutex-taking
  `MessageManager::isThisTheMessageThread` query. Dropdown item strings and UI
  changes are created and used only on the message thread.
- State trees: batch writes and save/restore may allocate and lock, but audio
  callbacks never access them. Save mirrors all registered cache values into
  `parameters.copyState()`; restore uses `parameters.replaceState()` and APVTS
  defaults for missing group values. The batch does not mutate sample inventory,
  synth voices, metadata, or buffer ownership.
- Original sounds/metadata: immutable during playback and retained by the sampler.
  Existing teardown stops voices and the warp worker before clearing sounds on
  the control thread.
- One-shot pitch: voices read the per-note ratio at note start and keep that
  scalar snapshot. `SamplePlaybackRenderer` reads the immutable source directly;
  no one-shot pitch worker, prepared buffer, lease, or audio-thread reclamation
  remains.
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
  selection refresh suppression, global-effect exclusion, manual and automatic
  effect selection, click-time source-value copying, empty inventory/unknown IDs/
  non-finite input, save/restore, and editor teardown.
- Checked Punch percentage parsing and display: values are limited to `0..100`,
  accept whole digits and an optional trailing `%`, and scale by 1/100 on commit.
  The 1% UI interval maps to 0.01 internally; cache snapping is fixed
  work in the parameter callback, and renderers continue to load one scalar amount.
- Checked display-only unit suffixes for Gain (`dB`), Pitch/Formant (`st`), and
  Punch (`%`). Parsing removes only the configured suffix on the message thread;
  parameter and render paths remain unchanged.
- Checked arrow edits from knobs, value fields, and the editor-level selected
  effect target. They run on the message thread, change one slider interval with
  a bounded parameter gesture, and introduce no render-thread work or ownership.
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
- Applying Pitch broadly can request new background warp caches. The warp worker
  retains its recent-sound scheduling; cache misses use the realtime fallback.
  One-shots use direct varispeed, and running one-shots keep their note-start
  pitch snapshot. Punch retains its existing live response.
- Build compatibility and host UI/state round trips on macOS/Windows still need
  runtime verification. Existing per-sample envelope/transient costs were inspected
  but not profiled; bulk copying does not add voices or render-thread traversal.

Conclusion:
- No new blocker was found in the registry dispatch, edit tracking, or batch
  publication. The overall inspected playback path remains unsafe for realtime
  use in the existing warp/MIDI cases described above.
