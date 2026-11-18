# Sample grid handoff audit

Realtime audio audit: PASS

Scope: the new grid's consumers of existing playback snapshots, sample selection,
and the dedicated LP area/start/close control of the existing SHAKE command handoff. No render implementation,
sample ownership, DSP algorithm or queue implementation changed in this task.
This is not a replacement for the existing playback/DSP audits.

Audited realtime entry points:
- `processBlock`: existing SHAKE target/queue consumption observed by the new LP UI.
- `PercussionVoice::startNote`, `renderNextBlock`, and clear-playback paths:
  existing packed playhead publication consumed by the new activity display.

Reachability inspected:
- Editor timer -> `getSamplePlayheadProgress` -> fixed voice inventory ->
  `getPlayheadSnapshot` (relaxed atomic load).
- Existing voice publication -> finite/bounded normalized source progress ->
  packed `uint64_t` atomic store; playback clear publishes zero.
- Grid LP -> message-thread selection/disabled area -> CLICK TO START ->
  message-thread update -> `setShakeSampleGroup` ->
  `ShakeMidiState::setTarget` (release atomic session publication).
- Existing cursor gesture -> `triggerShake` -> fixed SPSC command queue;
  `processBlock` -> `beginBlock` accepts the existing session-tagged commands.
- Grid ED -> message-thread parameter selection and existing sample-editor setup.
- Area cross/outside click/editor hide -> message-thread disable/reset -> target
  invalidation, virtual-active flag clear, helper release and warning cancellation.
- Area cursor moves are gated by local bounds and the start toggle before any
  queue push/helper send. Leaving the area and dragging rebase the local gesture.
- Clicking inside the area except TRACK MIDI toggles the existing message-thread enable flag;
  stopping follows the same target invalidation/helper release path as closing,
  while leaving the area open. The START/STOP prompt passes mouse events through
  to the surface, avoiding a second button callback/toggle. Text/visibility
  updates remain message-thread-only.
- TRACK MIDI uses the existing message-thread setter and atomic runtime routing;
  clicking it also sets the existing shake-enable UI flag before the same
  session update. It does not touch synthesiser voices or synchronise with the
  render lock; no new audio callback, ownership or allocation path is introduced.

Findings:
- No new realtime-reachable allocation, deallocation, UI access, locks, or
  unbounded work is introduced. Grid vectors, strings, buttons, callbacks,
  note formatting, page aggregation, and repainting stay on the message thread.
- The timer polls every immutable pitch group, including hidden pages. This
  adds group-count times voice-count work to the message thread only. It never
  calls synthesiser note-on/off or changes voices/sounds.
- Existing SHAKE queue consumption is capped at 32 commands per nonempty block;
  overflow rejects the newest trigger. Session changes invalidate queued old
  targets without freeing any resources. Zero-sized blocks consume no triggers;
  frame positions are clamped to the actual nonzero block range.

Cross-thread ownership:
- Grid cells/pages/activity vectors and shake-area UI: created, mutated and destroyed by the
  message thread; no audio pointers or references to them are published.
- Sample-group inventory and voices: processor-owned, built before rendering,
  stable while the editor is alive; processor teardown clears voices after
  suspension. Grid cells copy only inventory numbers and note names.
- Playhead snapshots: audio-written scalar atomics, message-thread readers;
  packed snapshots own no memory. `uint64_t` lock-freedom is asserted in the
  voice implementation for supported targets. No audio-side reclamation.
- SHAKE target/queue: processor-owned fixed storage, message producer/audio
  consumer; release/acquire publication and scalar session invalidation remain
  unchanged. Queue/target atomic lock-freedom is asserted in its implementation.
- Per-sample parameters: ED uses the existing message-thread selection and
  realtime-cache synchronization, without changing cache publication/lifetime.

Verification:
- Static audit only; no build, runtime interaction, or audio validation run,
  following the project's instruction to run these only when requested.

Residual risks:
- Host interaction, actual hover visuals, and platform builds remain unverified.
- Activity updates at the existing 30 Hz editor refresh rate. Very short sounds
  that start and finish between timer ticks may not be visible.
- Existing DSP and third-party warp fallback limitations are outside this
  UI/handoff change; their previous audit findings are not superseded.

Conclusion:
- PASS for the grid's added snapshot consumers and existing command handoff:
  the new UI introduces no additional realtime work or shared object ownership.
