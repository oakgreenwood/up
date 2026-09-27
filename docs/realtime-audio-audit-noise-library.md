# Multiple custom noise imports audit

Realtime audio audit: FAIL

Audited realtime entry points:
- Processor processBlock/processBlockBypassed, prepareToPlay and releaseResources.
- BackgroundNoisePlayer process/reset, imported-table pin acquire/release,
  source adoption, trim handling and playhead publication.
- Existing synth MIDI callbacks and voice rendering as noise hit producers.
- Loader, import/state callbacks, waveform/drop/menu UI and teardown as lifetime
  and publication partners for the changed custom-source tables.

Reachability inspected:
- Drop(s) -> non-audio queue and stable empty-index reservation -> existing
  processor parameter gestures -> decoder worker -> immutable source preparation,
  WAV/Base64 encoding -> library budget check -> pinned table publication.
- Audio source choice -> bounded two-attempt table acquisition -> source.get()
  at a fixed index -> existing fades/trim/looping -> scalar table-pin release.
- Save -> wait for current/queued jobs -> encode all successful indexed entries;
  restore -> generation cancellation/unpublish -> queued indexed decoding ->
  APVTS replaceState after removing the large imported payload.
- Library revision -> message-thread sparse menu rebuild -> ParameterAttachment
  mapping stable item ID to denormalized parameter value. Per-source identity
  -> selected waveform refresh and scalar-only playhead identity comparison.
- JUCE ParameterAttachment notification/async behavior and existing framework
  synth locks; prior noise DSP and Rubber Band fallback hazard paths.

Findings:

[BLOCKER] Source/Playback/RealtimeWarpPlayer.cpp:74, :156, :326, :345
Call path: realtime fallback start/live rate change/loop reset -> Rubber Band
R2 reset/reconfigure; render -> repeated feeding until output is available.
Issue: Existing reset scavenges resources, configuration can allocate, and the
feed loop has no practical callback budget. Vendored R2 retains scavenging at
213, window allocation at 761/769 and resampler construction at 800.
Why it matters: The wider plugin render path is not strictly realtime-safe.
Minimal fix: Bound fallback memory/reclamation/work or prepare output off thread.
These pre-existing engine repairs are outside the custom-noise library task.

[RISK] Source/PercussionSynthesiser.cpp:6, :14
Call path: range rendering / MIDI dispatch -> JUCE CriticalSection.
Issue: Existing framework synchronization remains; no additional synth mutation
or render lock is introduced here.
Why it matters: This is not a whole-plugin lock-free guarantee.
Minimal fix: Preserve stopped inventory/preparation/teardown.

Cross-thread ownership:
- Each immutable Source is allocated, decoded, sanitized, summarized and encoded
  on the persistent loader. Before publication it gets a runtime identity. It is
  converted to shared ownership on that worker, never on audio.
- Three processor-owned ImportedSlot tables each contain 64 shared_ptr<const
  Source> entries and an atomic reader count. A table is written only by the
  worker after reserving an unpublished zero-reader slot with CAS to -1. Copying
  the current table shares existing audio without copying its buffers. Pointer
  assignments and all resulting source/refcount destruction occur on the worker.
- Worker publication, UI table reads and state reads share importMutex. Audio
  never takes that mutex. It pins a published table using at most two CAS tries
  and rechecks publication before reading a shared pointer with get(). It never
  copies, resets or destroys a shared pointer. Active playback retains a table
  pin across blocks/fades; the candidate has a separate per-call pin. Releasing
  either only decrements an atomic. At most two tables can be pinned by the
  single render thread, leaving the third available for publication. Only the
  worker may wait for a free table.
- Appending the same Source pointer to a newer table does not invalidate active
  playback's older table pin. Restoring a library unpublishes tables but cannot
  destroy pinned sources; old playback fades out before releasing its pin.
  Retired table references are reclaimed on worker reuse or stopped teardown.
- Request deque and name reservations are protected by importMutex on non-audio
  threads. Unique occupied indices bound pending plus published entries to 64.
  Drops append without changing generation; restore cancels older work and
  clears/replaces queued jobs. Generation checks before publication prevent stale
  jobs clearing/replacing a newer reservation. Cancelled decode/source cleanup
  occurs on the worker, not audio.
- Source identity atomics contain no pointer. UI playhead reads use only these
  and the existing packed uint64 snapshot. Audio reads identity from its pinned
  Source. Appending a table leaves existing identities unchanged. Identity values
  are bounded to fit the packed source key and skip zero; wrap requires billions
  of successful preparations. UI waveform data is copied under the non-audio
  mutex; per-source revision avoids interrupting unrelated trim gestures.
- Parameter objects live for the processor lifetime. The source choice has a
  fixed reserved range, so adding files does not reconstruct it. The custom UI
  ParameterAttachment captures the editor, is destroyed before its controls and
  cancels pending updates; onChange is cleared during editor teardown. It uses
  the same JUCE notification mechanism as the former ComboBoxAttachment.
- Audio/metadata tables survive while audio reads them. Processor teardown first
  stops rendering; the noise destructor signals/joins its worker, releases the
  final active pin, then destroys table owners. UI/state copies own only names,
  waveform summaries and encoded strings, not live source pointers.

Verification:
- Static audit only after final code changes. No build, executable validation,
  host/UI run, listening test, sanitizer or allocation profiler was run, following
  AGENTS.md's build/validation-only-on-request instruction.
- Render selection clamps finite input to embedded+64 bounds before converting
  to an integer. Custom source lookup is one fixed-array index after a bounded
  pin acquisition. A valid pinned table with an empty requested entry is silent;
  only failed pin acquisition may fall back to the active borrowed source.
- Source adoption retains the candidate table while releasing the previous pin.
  A reused table cannot be mutated while a render reader can access it. RAII
  cleanup releases only atomic pins, including early exits, reset and mute.
- No new audio-thread loops, allocations, container mutation, owning copies,
  destructor reclamation, I/O or locks were introduced. Existing zero/tiny/large
  block chunking, interpolation bounds, finite sanitization and fades remain.
- Decoding/encoding checks cancellation every <=16384 frames. Each file is limited
  to 128 MiB decoded audio; the published library to 512 MiB and 64 entries.
  State restore applies an aggregate encoded-size bound too. Byte sums use 64-bit
  arithmetic; source key arithmetic is bounded for the packed playhead snapshot.
- A save waits until all accepted jobs complete (or shutdown), including an
  in-flight job already removed from the queue. Restore updates this pending
  flag for its replacement queue and notifies waiters. The loader never calls
  host/UI code while save waits. Failed jobs free only their reserved empty index
  and leave published entries intact; they are omitted from saved payloads.
- Saved Source children retain explicit sparse indices, names and portable WAVs.
  The chosen relative custom index is saved separately from bundled inventory.
  Legacy single-import payloads map to index 0. Missing custom state unpublishes
  the old library. All successful entries save even while bundled noise plays.
- Sparse menu item IDs map directly to raw source indices. JUCE's ordinary
  ComboBoxAttachment normalizes by visible item count, so it was replaced with
  ParameterAttachment to avoid selecting a different source when entries appear
  or failed/empty indices are omitted. Initial and later automation updates use
  the current parameter value, without writing selection during menu rebuilds.

Residual risks:
- The existing Rubber Band blockers remain. No new realtime blocker was found
  in the custom library implementation under static inspection.
- macOS/Windows builds and host save/reopen remain unverified. Runtime checks
  should cover batch drops, duplicate filenames, save during decoding, restore
  during playback, failed files, source switching and editor reopen.
- Portable saved state can be large. The 512 MiB decoded cap applies to the
  current library; retired tables, encoded strings, pending restore strings and
  temporary decoding/serialization may temporarily consume additional memory.
- Extending the old one-custom-source choice range may change host-owned
  normalized automation from older builds. Saved selections remap by custom
  index; plugin state cannot rewrite host automation lanes.
- Trim is still one shared start/end pair, reset by each accepted drop; per-file
  trims and deletion controls are outside the requested scope. Failed selected
  reservations remain silent until a valid source is selected or added there.

Conclusion:
- Custom imports now form a persistent, indexed library with bounded render
  access and worker-only source ownership changes. Overall audit status remains
  FAIL because of the pre-existing Rubber Band fallback blockers.
