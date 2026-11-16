# Cursor Shake And Generated MIDI

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and `processBlockBypassed`.
- Synth MIDI dispatch / sequence reset / voice start, stop and rendering.
- Stopped `prepareToPlay` / `releaseResources` and the message-thread mouse
  producer as the publication and lifecycle boundaries.

Reachability inspected:
- Editor mouse listener -> `ShakeGesture::move` -> bounded command publication.
- Processor callback -> `ShakeMidiState::beginBlock` -> fixed pending note-offs
  and generated events -> merge with incoming MIDI -> existing synth dispatch.
- First target-session note-on -> scalar sequence reset -> ordinary velocity /
  direction selection -> voice start / choke / source and effect rendering.
- Completed input traversal -> generated-only `MidiBuffer` output -> pinned
  JUCE VST3 event-list conversion or AU MIDI output callback.
- Playback start / preparation / bypass -> reset generation, queued command
  invalidation, and pending-note closure; editor close only changes target.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:78`
Call path: incoming warp note -> existing cache miss -> live Rubber Band start
and reset / rate reconfiguration.
Issue: The retained R2 path can allocate/reclaim memory during reset and
reconfiguration. Its reset/scavenger/reconfigure implementation was re-inspected.
Why it matters: The full processBlock playback path remains not realtime-safe
on a warped cache miss. Current directional group 26 is a normal one-shot;
gesture/MIDI publication does not add this blocker or alter stretching.
Minimal fix: Replace the live fallback with a proven allocation-free path or
prepared, safely published playback buffers. This remains outside shake mode.

[RISK] `Source/Midi/ShakeMidiState.h:142` (`writeOutput`)
Call path: completed callback -> bounded short-event serialization -> host output.
Issue: Allocation-free output relies on the pinned JUCE wrapper preparations:
VST3 reserves its MIDI buffer with `ensureSize(2048)` before processing; AU
reserves both swapped MIDI buffers to the same size. `clear` preserves capacity.
Why it matters: A future wrapper/version change that removes this preparation
could cause `addEvent` to allocate. Current output is at most 224 three-byte
events, or 2016 bytes in the inspected MIDI-buffer implementation. A compile-time
bound verifies the event budget. Output is disabled for other wrapper types.
Minimal fix: Re-audit preparation/serialization when changing JUCE/formats;
retain the bounded output budget and pre-render capacity guarantee.

Cross-thread ownership:
- Gesture state and recursive mouse listener: editor/message-thread-only;
  no mouse/UI calls from audio. Listener removed before member destruction.
  Movement velocity is calculated on message, not the 30 Hz timer/audio thread.
- Directional group inventory: flags built while loading, immutable thereafter.
  UI reads the existing group vector; no mutable synth access is needed.
- Queue: fixed 128-entry processor-owned array, single message producer and
  single audio consumer. Release-published write index follows complete scalar
  command assignment; acquire reads precede audio copies. Consumer releases its
  read index after use; the producer never overwrites unread slots. Overflow
  rejects the newest gesture. Stale sessions/generations and commands older than
  250 ms are discarded. A fixed snapshot and 32-command callback cap bound work.
- Target session: one lock-free packed uint64 atomic contains generation and
  MIDI note plus one. Only message writes it; audio validates it per command.
  Disabling, selection changes and editor closure invalidate queued targets.
- Playback reset: lock-free scalar generation, audio writer/message reader.
  Queue invalidation advances only the consumer index; an in-flight publication
  with the previous generation is rejected later. Stopped lifecycle callbacks
  may reset audio-owned counters under the existing host lifecycle contract.
- Pending note-offs, last accepted session, generated event array: audio-owned
  fixed storage. Every generated message is a short note-on/off with inline JUCE
  storage. Copies, insertion sorting, removal and teardown do not destroy heap
  payloads. There is no owning buffer/pointer handoff or retired allocation.
- Sounds/metadata/warp caches: unchanged processor/synth-owned immutable sounds
  and worker-owned pinned cache slots. Voice clearing retains synth sound
  ownership and only decrements cache reader pins. Worker/stopped teardown
  performs reclamation. See the sample-direction audit for the existing path.
- Host MIDI buffer: borrowed for one callback, traversed before any modification.
  It is cleared once and populated only for VST3/AU using their existing capacity;
  it is never swapped, resized or stored by the processor.

Verification:
- Static audit only after final relevant code changes. Builds, automated tests,
  host validation and listening tests were not requested/run per `AGENTS.md`.
- Inspected first-hit/session reset, reversal/curve thresholds, four-second idle
  reset, host-start generation, selection changes, non-directional eligibility,
  button off, editor hide/destruction, re-entry and ordinary knob dragging.
- The subsequent known-host visibility filter is UI-only: `PluginHostType`
  construction is cached once on the message thread, outside audio. AU in Live
  cannot enable the mode; hiding reuses the existing target-invalidation path.
  No render entry point, queue, MIDI serialization or ownership rule changes.
- Inspected repeated same-note commands, commands with equal/clamped timestamps,
  closing an overlapping generated note before retrigger, pending note-off at a
  new hit's exact position, gate expiration, source end and mode disable. Future
  scheduled offs are replaced while earlier/equal-position offs remain ordered.
  Positive/zero/changing/oversized blocks preserve fixed storage. A zero block
  does not consume gestures or advance gates; the next nonempty block resumes.
- Generated positions clamp to `0..frames-1`; the first queued hit is immediate
  and later hits preserve relative event times within that callback. Prepared
  gate length is finite, positive and bounded by the existing valid-rate range.
  Every event is inserted in stable time order; callback merge consumes each
  incoming/generated event once without invalidating input iterators.
- Pending note-offs visit 128 notes. At most 32 commands add at most three events
  each, so `128 + 32*3 = 224` bounds event storage/output. Sorting and replacement
  scan this bounded list; all MIDI-message copies stay short/inline. No retry
  loop, queue wait, scratch growth, allocation or new lock is added on audio.
- Inspected JUCE MIDI-buffer insertion (six bytes of header plus three MIDI
  bytes), retained-capacity clear, VST3/AU MIDI preallocation, VST3 note-event
  conversion, AU's preallocated 64 KiB packet list and stack MIDI-event-list
  callback path. Callback installation/block copying occurs outside rendering;
  this code only emits short channel messages, not SysEx/converter payloads.
- Existing synth recursive locks remain; only audio mutates/dispatches voices
  while rendering. UI only publishes scalar commands. No APVTS/state-tree access,
  file I/O, logging, parameter-ID lookup or UI callback is introduced on audio.
- New functionality uses portable C++/JUCE. Lock-free integer atomics are required
  by static assertions on macOS arm64/x86_64 and Windows targets.

Residual risks:
- The existing live Rubber Band fallback remains a realtime blocker.
- Compilation, host recording routes, latency compensation, subjective speed
  mapping and jitter/reversal feel remain untested. Mouse sampling and UI stalls
  limit the earliest reliable speed estimate; audio block size bounds handoff
  latency. Several queued hits in a very small callback may share a position.
- Live MIDI-output recording requires host exposure/routing of that output. VST3/AU
  capability declarations cannot make every DAW record it. No virtual MIDI port
  or separate MIDI-effect format is added. Standalone performs internal playback.
  The later [live take cache](realtime-audio-audit-shake-cache.md) exports both
  generated and mapped incoming notes without requiring an output recording route.
- Recording only the generated stream does not capture simultaneous external
  MIDI that also influenced the shared u/d state, nor the random variation seed.
- Warp/loop directional sounds retain normal note-off behavior with the generated
  30 ms gate. Current directional sources are ordinary one-shots.
- At a concurrent disable/start boundary, an already accepted event can land in
  the current callback. Subsequent commands observe the new session/generation.

Conclusion:
- The bounded shake/MIDI path adds no audio-thread allocation or locking under
  the inspected VST3/AU wrapper guarantees. The reachable existing warp fallback
  keeps the full audited playback path at FAIL.
