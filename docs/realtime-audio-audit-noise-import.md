# Imported noise realtime audit

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock`, `processBlockBypassed` and the
  transitively called `BackgroundNoisePlayer::process` / `reset`.
- Prepare/release, processor/editor teardown, file-drop handlers and state
  callbacks were inspected as ownership/publication partners.

Reachability inspected:
- Host block -> existing cached parameters / bounded chunks / sidechain ->
  noise source index -> at most two slot-pin attempts -> gain/region fades ->
  immutable source interpolation -> mix -> scalar pin release.
- Source adoption -> retain a reader pin for the active loop -> release old pin;
  mute/bypass/release -> reset -> release pin without deleting the source.
- Drop -> queued latest file request -> JUCE loader thread -> format decoder ->
  finite sanitized samples / waveform summary -> float-WAV encoding -> fixed
  unpublished slot reservation -> publication.
- Save -> APVTS copy -> wait for import completion -> embedded WAV child -> XML.
  Restore -> generation cancellation / unpublish -> worker decode -> APVTS
  replacement with the large payload removed from the live tree.
- UI polling -> protected name/summary copies and revision -> repaint/menu text.
- Local JUCE reader buffer routing, float WAV writer, standard Base64, thread
  startup/teardown, ComboBox text update and non-parameter host notification.
- Unchanged percussion/synth/warp paths retain the findings in the prior
  [waveform audit](realtime-audio-audit-noise-waveform.md) and
  [sidechain audit](realtime-audio-audit-noise-sidechain.md). The existing
  Rubber Band reset/reconfigure call sites and vendored R2 implementations
  were re-inspected for the unresolved blocker below.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:74`, `:154`, `:328`, `:345`
Call path: percussion fallback start / ratio change / loop reset -> R2
reset/reconfiguration; rendering -> repeated feeding until output appears.
Issue: Existing reset scavenges resources, reconfiguration can allocate windows
and resamplers, and feeding has no practical per-callback work budget.
Why it matters: Background noise loading does not remove these reachable
allocation/reclamation and uncontrolled-work hazards in percussion playback.
Minimal fix: Bound/preallocate fallback resources and work, or prepare fallback
audio off thread. This separate pre-existing playback work remains open.

[RISK] `Source/PercussionSynthesiser.cpp:6`, `:14`
Call path: existing synth rendering / MIDI dispatch -> framework synth lock.
Issue: Existing JUCE synthesis synchronization remains reachable.
Why it matters: Whole-plugin lock freedom cannot be claimed; inventory mutation
during rendering could contend.
Minimal fix: Preserve stopped synth setup/teardown. The new importer neither
calls nor changes the synth.

[PERF] `Source/Playback/BackgroundNoisePlayer.cpp:187`
Call path: non-realtime state save -> pending-import completion wait -> WAV/XML.
Issue: Immediate saves can wait for decoding; embedded float audio increases
state size and serialization memory/time. Imported decoded data is capped at
128 MiB, and slot history is fixed at three sources plus one in-progress result.
Why it matters: Large imports can delay project saving or stopped teardown.
Minimal fix: Keep this work outside rendering. Runtime host-save behavior and
large-file memory use still need verification; no audio-thread wait was added.

Cross-thread ownership:
- Bundled sources: created/sanitized during construction, immutable before the
  worker/editor/audio start, read by audio and copied into the UI. The vector is
  never resized after construction; destruction happens at stopped teardown.
- Imported slots: three fixed processor-owned entries. The loader creates a
  candidate privately. Under the non-audio import mutex it chooses a slot other
  than the currently published slot and CAS-reserves readers from 0 to -1.
  Only then may it replace/delete that slot's unique_ptr. It releases the
  reservation and publishes the fully constructed source using sequentially
  consistent atomics. Source data never mutates after publication.
- Audio pinning: at most two bounded attempts increment a nonnegative reader
  count, then recheck publication before reading the pointer. A writer cannot
  reserve a pinned slot; an acquisition racing reuse either fails reservation,
  rejects stale publication, or sees a fully initialized replacement. No raw
  pointer is read before the pin succeeds. Audio keeps one active-slot pin
  across callbacks and a temporary candidate pin within a call. Adoption retains
  the candidate before its temporary pin is released. Final audio release only
  decrements an atomic, never a shared_ptr/unique_ptr reference or heap resource.
- Rapid replacements: unpublished pinned predecessors remain valid throughout
  fades. The worker waits/retries off audio if all eligible slots are pinned;
  it never forces reclamation. Three slots cover the active and temporary audio
  pins plus a publication slot. The last accepted request supersedes earlier
  work by generation. Clearing restored state unpublishes without destroying an
  active source. Retired storage is reclaimed on worker reuse or stopped teardown.
- Request fields, error text and saved strings: UI/state write under importMutex;
  the worker snapshots them, decodes outside the mutex and checks generation
  before publishing. No host or UI callback runs from the worker. Save's condition
  wait releases the mutex, allowing the worker to complete without message-thread
  cooperation. Older canceled work cannot overwrite a newer restore/drop.
- UI/state readers: names and the published source's small waveform summary are
  copied under the same mutex as publication/replacement. The UI owns its summary
  and reads no replaceable audio pointer. The saved encoded string is reference
  counted only off audio. Filename-identical replacements still change revision.
- Scalar DSP/playheads/smoothers remain audio-owned. APVTS targets retain their
  cached atomic lifetime; only the existing UI/state pathways manipulate trees,
  strings, parameter gestures or host notifications. The new integer atomics
  require compile-time lock freedom on the supported targets.
- Teardown sets cancellation, wakes the worker and joins it before destroying
  slots. JUCE provides its native thread setup, including the macOS autorelease
  pool. Host audio/editor access must already have stopped, as for the existing
  processor lifetime. Closing only the editor leaves the import/worker intact.

Trim-gesture follow-up (after the final relevant changes):
- `NoiseWaveformEditor::mouseDown` and `endDrag` publish a uint32 gesture counter
  through the processor. Message thread is the sole writer; odd means active.
  The audio callback acquires the counter before reading final trim targets,
  then passes one snapshot to all chunks. Existing lock-free integer assertions
  cover this atomic. No string, mutex, host call or allocation enters rendering.
- Counter changes mark an active source for one completed gain fade-out. Audio
  owns the observed counter and pending-stop flag. Region adoption is suppressed
  during this stop; reset releases only the existing scalar source pin. The
  first unpaused chunk restarts at the final range. Completed gestures between
  callbacks are detected by the counter even if audio never saw its odd state.
- Re-inspected held/released drags, mouse-up before fade completion, same-position
  clicks, already-muted/unloaded sources, source replacement, bypass/reset,
  zero-frame and <=512-frame chunks. No new resource ownership or unbounded
  loop is introduced. Silence gain still prevents restart. All existing gesture
  cleanup paths release the temporary pause without serializing UI state.
- Display-name cleanup runs only in import/state callbacks under the existing
  non-audio mutex. It modifies the prepared label, never source audio or the
  actual file path. Transparent panel painting is message-thread-only.
- Static audit only for this follow-up; no rebuild or runtime tests were run.
  Earlier successful builds below predate these latest UI/trim changes. The
  overall FAIL status remains due to the existing Rubber Band blockers.

Verification:
- Playhead follow-up: re-inspected `processBlock -> BackgroundNoisePlayer::process`
  and reset/bypass/prepare/release after adding a single uint64 position/identity
  snapshot. Audio writes it once per completed chunk; reset and zero-channel
  output clear it. A compile-time assertion requires lock-free uint64 atomics.
  The UI reads the self-contained scalar with relaxed ordering, using source and
  imported-slot identity to reject an outgoing source's cursor. It borrows no
  new pointer and changes no source ownership or reclamation.
- The publication path is reached only after nonempty valid source rendering.
  Its finite position is clamped to 0..1 before conversion to uint32; a 32-bit
  shift packs identity into the other half of uint64. Zero-frame calls do not
  advance playback; fade-out/reset returns clear the snapshot. The existing UI
  timer repaints on position changes and draws one line on the message thread.
  No allocation, mutex, UI call or unbounded work was introduced on audio.
  This latest follow-up received static inspection only, without a new build or
  runtime test. The overall FAIL status remains due to existing warp blockers.
- Initial verification was a static audit. In the subsequent build-failure
  follow-up, reproduced ambiguous `requestedFile = {}` assignments and changed
  both to explicit `juce::File{}`. Re-inspected the state/worker call sites and
  JUCE File move assignment: any path-string release stays under the existing
  non-audio mutex; rendering, publication and source reclamation are unchanged.
- Debug VST3 and AU subsequently built successfully with
  `cmake --build build --config Debug --target ulichperc_VST3 ulichperc_AU --parallel 4`.
  `lipo -archs` confirmed both bundles contain x86_64 and arm64. CMake's existing
  post-build rules copied them to the user's plugin directories. No UI run,
  listening checks, sanitizer, allocation profiler or pluginval was run.
- Render/reset contain no added file access, allocation, pointer ownership
  destruction, mutex, condition-variable wait, queue growth or thread operation.
  Slot acquisition is two attempts; per-frame work and storage remain bounded
  for zero, one-frame, changing and oversized host blocks.
- An unsuccessful pin attempt retains an already active imported source when
  possible. Missing imports fade an old source out and stay silent. Replacement
  identity is the pinned immutable source, not just the fixed dropdown index,
  so same-choice/same-filename replacement still uses the normal source fade.
- Reader length/rate/channel validation precedes allocation. Imports cap decoded
  mono/stereo float bytes with 64-bit arithmetic, then sanitize finite samples.
  Decoding/encoding checks cancellation every <=16384 frames. Waveform building
  and Base64 conversion are worker-only. Restored Base64 length is bounded before
  decoding; standard Base64 has no untrusted allocation-size prefix.
- Existing region/interpolation bounds, one-frame wrap, endpoint rounding guard,
  2 ms trim ramps, 10 ms source/gain ramps and sidechain bounds are preserved.
- Source indices remain valid with the appended custom choice. State saves its
  stable imported identity and audio even while a bundled source is selected.
  Old states clear a prior custom publication; APVTS defaults remain intact.
- UI replacements end waveform gestures and refresh summary copies. ComboBox
  item renaming explicitly refreshes selected text without emitting a parameter
  change. Import errors are surfaced only by the message-thread timer.
- CMake enables the bundled JUCE MP3 decoder; other basic/platform decoders and
  float-WAV state use existing JUCE modules, without hardcoded paths or a new
  external codec dependency.

Residual risks:
- The pre-existing Rubber Band blockers and synth framework synchronization
  remain. No new blocking issue was found in the imported-noise render path.
- Windows compilation and runtime decoder coverage, drag/drop from Finder/Explorer
  and hosts, save/reopen after moving the original, rapid replacements/restores,
  invalid/oversized files and audible transitions remain runtime checks.
- Restored imported audio starts after asynchronous decoding completes. State
  saving/teardown can wait for local or network filesystem/decoder work; these
  APIs are non-realtime and must not be invoked from a host's rendering callback.
- A failed drop retains previous custom audio when present; otherwise the chosen
  custom slot is silent. Gain is unchanged and import never forces noise on.

Conclusion:
- The new import/render handoff has fixed storage and bounded allocation-free
  audio work under static inspection. The overall inspected path remains FAIL
  because the pre-existing Rubber Band fallback blockers are unresolved.
