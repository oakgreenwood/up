# Live Sample MIDI Cache

Realtime audio audit: FAIL

The later [Ableton insertion audit](realtime-audio-audit-ableton-insertion.md)
covers the additional immutable original-position scalar and its snapshot handoff.

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` and `processBlockBypassed`.
- Generated/incoming MIDI dispatch, synth callbacks and rendering.
- Stopped `prepareToPlay` / `releaseResources` finalization.
- Message-thread live snapshot/export and file drag.

Reachability inspected:
- Callback -> single host-position query -> separate playing/recording flags ->
  `ShakeMidiTake::beginBlock` -> playback-start / recording-start replacement.
- Transport replacement -> sampler sequence reset / shake queue invalidation;
  message movement observes the existing shake-reset generation.
- Merged generated/incoming sample events -> existing synth dispatch -> capture;
  immutable sample-note inventory filters unmapped incoming notes.
- Captured note-on -> four-second frame/monotonic idle check, only while neither
  playing nor recording -> possible replacement beginning at that event.
- Callback end -> beat clock / published prefix; stopped idle without held notes,
  bypass, release or preparation -> close notes / finalize.
- Editor export -> slot pin -> acquired prefix count/length -> immutable starts
  and atomic ends -> message-thread snapshot/MIDI file -> native file drag.
- Editor timer/movement -> packed availability -> drag-button enable state.
- Existing voice/source/effect/warp paths, also covered by the
  [shake MIDI audit](realtime-audio-audit-shake-midi.md) and
  [sample-direction audit](realtime-audio-audit-sample-directions.md).

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:78`
Call path: generated/incoming warp note -> cache miss -> live Rubber Band
start/reset and rate reconfiguration.
Issue: The retained R2 reset calls the emergency scavenger and `reconfigure`,
which can reclaim/allocate memory. These implementations were re-inspected.
Why it matters: The full reachable playback path remains not realtime-safe on
warped cache misses. Capture does not introduce/change this fallback.
Minimal fix: Replace the live fallback with proven allocation-free playback or
prepared, safely published buffers. This remains outside MIDI take capture.

[RISK] `Source/Midi/ShakeMidiState.h:142` (`writeOutput`)
Call path: callback completion -> existing generated MIDI serialization.
Issue: Allocation-free output depends on the pinned VST3/AU wrappers retaining
at least 2048 bytes of MIDI capacity for <=224 events / 2016 serialized bytes.
Why it matters: Changing JUCE/wrappers requires re-auditing this guarantee.
Incoming capture does not add events to output or change its budget.
Minimal fix: Preserve the preparation/bounds described in the shake MIDI audit.

Cross-thread ownership:
- Take slots: three processor-owned fixed arrays of 32768 notes allocated during
  construction. Audio reserves an unpinned slot with a -1 marker only to reset
  metadata, then releases that marker. Starts, pitch, velocity and channel are
  immutable after publication; audio appends into unpublished entries only.
  Existing note ends alone may change through lock-free atomic doubles.
  UI may therefore pin/read the current prefix while audio appends elsewhere.
- Replacement: audio first invalidates publication, then attempts at most three
  strong compare/exchanges to reserve a slot with zero pins. A pinned predecessor
  cannot be reset/reused. One message-thread export reader can pin at most one
  slot, leaving slots available without waiting. Audio never destroys a snapshot
  or clears note arrays. Stopped processor teardown reclaims the arrays.
- Publication: a lock-free uint64 packs generation, slot index and first-note
  availability. A generation detects replacement even if reset and first note
  occur between UI polls. Atomic length is stored before release prefix count;
  an acquired count cannot expose starts beyond that acquired snapshot clock.
  UI pins, rechecks generation, acquires metadata, copies, then rechecks again.
  Pinning has at most two attempts and releases via RAII on return/retry/throw.
- Atomic ends: -1 means held. A snapshot closes held notes at its acquired end
  and clamps concurrently closed notes to that same end. This does not stop
  live notes. Atomic double/bool/int/uint64 lock-freedom is asserted for targets.
  Snapshot vectors, MIDI sequences, streams and file I/O remain message-only.
- Active-note keys, count, clock and last-hit/transport memory: audio-owned.
  The fixed 16*128 key table separates identical pitches on different channels.
  Replacement/finalization visits at most 2048 keys; channel panic visits 128.
  Preparation/release use the existing JUCE stopped-render lifecycle contract.
- Host position and mapped-note inventory: one scalar host update per callback;
  missing position retains capture's last known mode. Sample-note flags are
  construction-written and immutable during rendering. No UI/state tree access.
- Native file drag: message-owned file creation and OS publication through JUCE.
  Files survive editor/processor destruction and take resets for delayed import.
  No file drag work runs on audio.
- Gesture queue, inline short MIDI, sounds/metadata and warp leases retain the
  preceding audits' ownership. Voice cache lease reset only decrements pins;
  worker/stopped teardown owns reclamation. Existing synth recursive locks have
  no new UI/loader contender; sample/voice mutation remains audio or stopped.

Verification:
- Static audit only after final relevant code changes. Builds, automated tests,
  host validation and listening tests were not requested/run per `AGENTS.md`.
- Inspected independent playback/recording starts, simultaneous starts (one
  replacement), punch-in, punch-out while playing, stop and empty replacements.
  Idle cannot split playing/recording takes. Stopped idle finalization preserves
  the published generation; only the next hit replaces it. Held notes prevent
  idle finalization until note-off. Frame and monotonic checks cover suspension
  and offline timing; first stopped-take hit is beat zero.
- Missing position does not invent transport transitions. Without any known
  host mode, capture behaves as stopped audition. Loop/seek changes append
  elapsed beats. Lifecycle/bypass finalizes; later starts/notes begin a new take.
- Inspected same-block replacement, channel-separated overlapping pitches,
  mapped/unmapped notes, channel all-notes-off/all-sound-off and live snapshots
  during append/closure/replacement. Snapshot pins prevent reuse while reading;
  only atomic ends/metadata change inside a published prefix.
- Zero blocks observe transitions and incoming MIDI exactly once but consume
  no queued shake gestures or beat time. Positions clamp to 0..frames, including
  incoming end-of-block events. Variable/oversized blocks require no resizing.
- Rate is finite, bounded to 1000..768000 Hz, otherwise 44100. BPM is finite,
  bounded to 1..1000, otherwise 153. Idle seconds saturate at four. Beat time
  saturates at 65536 and count at 32768; a cap retains a partial prefix and marks
  the filename. Active indices are -1 or valid note indices; channel/note bounds
  precede indexing. No unbounded retry or cache traversal occurs on audio.
- Audio work is constant per note, <=128 closures per channel panic, <=2048 keys
  per replacement/finalization, and <=3 reservation attempts. Incoming capture
  is linear in host event count alongside the existing once-per-event dispatch.
  There is no allocation, destruction, I/O, new lock, UI call, large note-array
  clear or take-length-dependent copy on audio.
- Generated output remains generated-only and keeps the existing bounded MIDI
  budget; incoming capture cannot create MIDI feedback. Note messages retain
  inline JUCE storage. Live warp still reaches the established blocker above.
- MIDI-file ticks remain bounded: 65536*960 = 62914560, plus at most one minimum
  duration tick, within signed-int and MIDI delta limits. Note ends are at least
  one tick after starts and end-of-track follows all notes. File construction,
  allocation, flush and failed-write cleanup stay on message.

Residual risks:
- The existing live Rubber Band fallback remains a realtime blocker.
- macOS/Windows builds and DAW drag/import behavior remain
  untested. Host callback/transport reporting limits observation latency; UI
  timers can be delayed. The monotonic hit check catches resumed stopped pauses.
- Per-block tempo, MIDI tick rounding, loop placement and latency compensation
  need host testing. The cache preserves notes/velocity/channels, not arbitrary
  CC/sustain/pitch-wheel automation or random variation choices. Live snapshots
  close held notes at their end. Same-tick retriggers have file rounding limits.
- Cache is runtime-only. Playback/recording starts replace it even if empty;
  overflow keeps a partial prefix. Exported files persist until temp cleanup.
- AU-in-Ableton SHAKE remains hidden; mapped incoming MIDI can populate its
  cache. Export does not add automatic track insertion or change host policy.

Conclusion:
- The bounded live cache/prefix protocol adds no audio-thread allocation,
  contention or reclamation. Existing reachable live warp keeps the full
  audited playback path at FAIL.
