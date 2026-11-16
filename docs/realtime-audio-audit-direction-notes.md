# Fixed Direction MIDI Notes

Realtime audio audit: FAIL

Audited realtime entry points:
- processBlock/processBlockBypassed, synth MIDI dispatch/noteOn/voice stealing,
  voice start/render and SHAKE queue scheduling.

Reachability inspected:
- Loader inventories names, expands pitch/direction slots and registers aliases
  before processing. Audio noteOn resolves a down-note alias to its canonical
  root, selects a fixed direction from existing velocity pools, starts a voice
  using the actual incoming note and chokes opposite voices using the root.
- Gesture -> fixed command queue/direction bit -> bounded short event scheduling
  -> MIDI output/capture; virtual gesture -> existing helper -> incoming MIDI.
- Voice uses immutable sound root for shared effects/cache/noise/spectrum and
  playhead, while JUCE retains actual input note/channel for note-off.
- Existing voice live-warp fallback -> RealtimeWarpPlayer::start -> RubberBand
  reset/reconfiguration, including retained allocation/reclamation branches.

Findings:
[BLOCKER] Source/Playback/RealtimeWarpPlayer.cpp:78
Call path: voice start -> live warp fallback -> R2Stretcher::reset.
Issue: Retained RubberBand reset can reclaim/allocate through the scavenger and
reconfiguration (external/rubberband/src/faster/R2Stretcher.cpp:197).
Why it matters: Either directional note can reach this path on a cache miss.
Minimal fix: Prepared playback or an allocation-free fallback; outside this task.

Cross-thread ownership:
- Alias array: 128 processor/synth-owned integers, initialized before processing,
  rebuilt only with rendering stopped. Audio reads in constant time; no owning
  pointers, publication replacement or audio reclamation.
- Sample inventory and sounds: constructor-built, immutable while processing;
  sound root stays canonical. Existing loader/worker/sample lifetimes unchanged.
- SHAKE direction: message-owned scalar gesture state, copied into the existing
  preallocated SPSC command with the existing release/acquire publication.
  Failed enqueue/send restores gesture history. Queue capacity/overflow and
  audio event capacity are unchanged; new down-note bounds reject values >127.
- MIDI activity: both physical notes are enabled before processing. Existing
  atomic activity/generation handoff maps both to one editor entry. Capture
  preserves actual notes; UI combines activity without changing audio storage.
- Other-group incoming notes publish a gesture reset through the existing
  lock-free generation. No UI object or APVTS state is touched from audio.

Verification:
- Static audit only after final relevant code changes. No build, test, plugin
  validation or listening/DAW run was requested/run.
- Inspected fixed pool selection, fixed voice loops/choke identity, shared cache
  root reads, separate input-note gates, capture filtering and editor alias map.
- Both loader/inventory paths use the same expansion. Directional groups reserve
  two slots even with only one suffix present; pairs past note 127 are omitted.
  Missing directions retain existing nearest-layer/ordinary/opposite fallback.
- Queue consumes <=32 triggers/block with unchanged <=224 short events. Added
  arithmetic is scalar; zero blocks retain commands/gates, and variable block
  chunking and tiny voice render ranges retain existing behavior. No new DSP,
  buffer growth, lock, allocation, destruction, file/device I/O or host call.

Residual risks:
- Existing synth CriticalSection and MIDI wrapper capacity assumptions remain
  as documented in the previous SHAKE audits. Full reachable warp path fails.
- Later groups shift MIDI notes. Old clips need remapping; effects remain keyed
  by existing note/pitch group identity. AU/VST3 and helper recording need host
  testing, including missing directions, pitch slots, rapid reversals, note-offs,
  velocity borrowing, choke fades and stopped/transport-start timeout resets.

Conclusion:
- The new alias/gesture work is bounded and uses existing storage/lifetimes;
  the retained reachable RubberBand blocker keeps the full audit at FAIL.
