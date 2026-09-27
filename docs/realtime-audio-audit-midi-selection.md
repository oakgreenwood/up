# MIDI sample selection audit

Realtime audio audit: PASS

Audited realtime entry points:
- `processBlock` MIDI dispatch, scoped to the activity/last-played-note handoff.
- `prepareToPlay` activity reset before processing.

Reachability inspected:
- `processBlock` -> short-message dispatch -> `MidiNoteActivityState::handleMidiMessage`
  -> note-on scalar publication / note-off activity / `reset`.
- Processor construction -> loaded sample groups -> `enableSampleNote`.
- EDIT SAMPLES message-thread callback -> atomic note read -> existing group selection.

Findings:
- Saturated the existing active-note count at `int` maximum to prevent signed
  overflow from repeated note-ons without note-offs. Ordinary activity is unchanged.
- Note recording adds one bounds-checked fixed-array lookup and at most one
  lock-free scalar store. No allocations, destruction, locks, UI calls, or retries.
- Reset visits fixed arrays of 128 entries and retains the remembered note.
  The handoff has no sample-rate or block-size dependence, including zero blocks.
- Removed the latest-note generation counter: the note is self-contained scalar
  state, so its relaxed atomic load/store needs no accompanying publication fence.

Cross-thread ownership:
- `sampleNotes`: processor-owned fixed array, populated only during construction
  after loading samples, immutable during audio processing. No dynamic replacement.
- Latest mapped note: processor-owned atomic integer, audio writer and message-thread
  reader; initialized to -1. Compile-time assertions require lock-free atomics on
  macOS and Windows targets. No pointer ownership or audio-thread reclamation.
- Activity arrays: processor-owned fixed arrays; audio writes atomic velocities and
  generations, UI reads them. Active counts belong to audio after preparation.
- Editor note-to-group map: message-thread-only fixed array, built from the loaded
  immutable sample-group inventory. Selection and APVTS synchronization stay on
  the message thread. Storage is destroyed with its owning processor/editor.

Verification:
- Static audit only. No build, runtime tests, or plugin validation requested/run.

Residual risks:
- Interactive host behavior and cross-platform compilation remain untested.
- Audit scope is the changed MIDI handoff, not unrelated synthesis/DSP branches.
- A MIDI event concurrent with the click can be observed on either side of the
  atomic read; no attempt is made to block audio for UI synchronization.

Conclusion:
- No blocking realtime issue remains in the inspected handoff.
