> Historical audit: this prototype has been removed. SHAKE now uses the system
> MIDI helper independently of recording state; see the current
> [virtual MIDI audit](realtime-audio-audit-virtual-midi.md). The following
> describes the retired implementation, not active source/runtime requirements.

# Ableton Arrangement Transport Bridge

Realtime audio audit: FAIL

Audited realtime entry points:
- processBlock, processBlockBypassed, prepareToPlay/releaseResources, host tempo
  queries and their reachable SHAKE scheduling, MIDI capture and voice playback.
- Editor timer, constructor/destructor, companion listeners/heartbeat and
  shutdown as the publication/lifetime boundaries.

Reachability inspected:
- Live Song listeners/heartbeat -> private atomic transport.json replacement ->
  existing Ableton client pollTransport on message -> packed atomic publication.
- processBlock -> monotonic expiry check -> HostTempoTracker native query plus
  fresh bridge positives -> effective start edges / MIDI take capture / sequence
  reset. Native PPQ and BPM remain separate; no bridge timeline is invented.
- Editor route/menu/input-warning checks and audio local queue suppression ->
  same fresh-recording policy. Native recording is stored separately from the
  bridge so stale/expired bridge-only truth cannot latch into native fallback.
- Incoming MIDI -> synth/voice start -> live warp fallback -> stretcher reset
  and RubberBand scavenger/reconfiguration.

Findings:
[BLOCKER] Source/Playback/RealtimeWarpPlayer.cpp:78
Call path: voice start -> live warp fallback -> R2Stretcher::reset -> scavenger
and reconfiguration (external/rubberband/src/faster/R2Stretcher.cpp:197).
Issue: The retained path can allocate/reclaim memory.
Why it matters: Returned virtual MIDI still uses this existing voice path.
Minimal fix: Prepared playback or allocation-free fallback; outside this bridge.

Cross-thread ownership:
- abletonTransportState: processor-owned uint64 atomic, compile-time required
  lock-free. Message is the active writer; prepare/release also clear it under
  the host's existing rendering-stopped lifecycle contract. Low two bits hold
  playing/recording; high bits hold monotonic expiry milliseconds. Audio/message
  load one scalar; relaxed ordering suffices because no second object/buffer is
  published. Counter time comes from JUCE's existing monotonic clock. Deadline
  space far exceeds supported machine uptimes; unsigned shifts do not allocate.
  There is no pointer ownership, snapshot queue, retry or audio reclamation.
- hostRecording: native-only atomic bool. Native position availability/recording
  are returned separately from merged transport. With fresh bridge but missing
  native position, native recording is cleared rather than preserving a stale
  native positive. Expiry/editor close therefore cannot retain bridge truth.
- Optional HostTransportState: two stack booleans, passed by value through the
  existing tempo query. It has no heap, destructor work or ownership. Transport
  edges are computed after merging; source PPQ/BPM stay unchanged. No new host
  query occurs on audio or message.
- AbletonMidiBridgeClient: editor/message-owned; process-specific directory and
  file/JSON/string work remain there. Transport reads are independent of legacy
  insertion requests, throttled to 100 ms, capped at 4 KiB. PID/protocol/kind/
  boolean/timestamp validation rejects other processes, malformed, future or
  stale snapshots. Timestamp bounds precede age subtraction to avoid signed
  overflow. Remaining freshness is <=2000 ms; unchanged files cannot extend it.
- Editor teardown clears the scalar, releases helper routing and stops its
  existing timer. No new timer thread, async worker, callback capture or ownership
  is introduced. An editor's processor outlives it. Closed editors stop polling.
- Script-owned Song/listeners/session and temporary file: live in Live's Control
  Surface, which schedules and calls them outside plugin render. Disconnect
  disables publication, removes listeners and removes only its own session's
  state/temp files; base disconnect tears down scheduled work. Private POSIX
  directory/file permissions are 0700/0600; Windows uses the per-user directory.
  It reads Song properties but changes no Live state and emits no MIDI.
- Existing samples/caches/queues retain their ownership and reclamation rules;
  this feature changes only scalar transport inputs to the existing paths.

Verification:
- Static audit after final relevant code changes. No configure/build, Python
  execution, tests, allocation instrumentation, DAW run or listening test was
  requested/run for this implementation. Installation is separate from testing.
- With filesystem approval, ran the macOS installer successfully. It copied
  the two script files into the user's default Live User Library, under
  `Remote Scripts/ULICHPERC_Transport`. No Live preferences were changed;
  restart and one-time Control Surface selection remain user steps.
- Inspected normal/bypass/lifecycle, missing/fresh/stale transport, expiry without
  polling, native positive preservation, explicit stop, editor close, count-in,
  recording start while already playing, source restart and unchanged heartbeat.
- Zero/variable blocks and tiny voice ranges add constant scalar work only.
  Existing fixed queue/event limits and render chunking remain unchanged.
  Shared selection, capture origin and reset behavior use the merged state.
- Inspected pinned JUCE variant integer/bool APIs, native process-ID pattern,
  host detection, existing note scheduling/capture and warp reset implementation.
  Script uses Python 3 standard-library APIs compatible with Live 11/12.
- Installers copy only owned script files and create the named directory. They
  do not edit preferences, launch/quit Live, delete other scripts or require a
  manual script launch at each recording. Windows installer remains unexecuted.

Residual risks:
- Existing RubberBand allocation blocker and JUCE synth locking/capacity issues
  from the earlier playback audits remain. A compile would not establish PASS.
- Live script API loading, listener delivery, scheduler timing, host/version/
  edition and macOS/Windows AUv2/VST3 combinations need real DAW tests. This is
  an Arrangement transport bridge, not Session clip or per-track/punch capture
  detection. Native recording flags can still support those modes.
- Polling adds up to roughly 100 ms plus message scheduling delay; the very first
  stroke at the exact record transition is not guaranteed to switch in time.
  Script and message-thread local file I/O may stall their UI; plugin audio never
  waits on it. Expiry falls back to native, which remains false in the tested
  Live VST3 setup if the script stops. Reconnection can create a fresh take edge.
- Normal in-process hosting is required for PID matching. A separately bridged/
  sandboxed plugin process will not discover the Live process's file. One
  Control Surface entry should be configured. No global cross-process scan or
  guess of the current project is added.
- The editor must be open for this bridge's polling. Native-only transport is
  restored when it closes; SHAKE itself already requires an open editor.
- Initial script installation/Control Surface selection and system MIDI input
  routing remain necessary. The transport companion does not implement ADD MIDI
  insertion or replace the MIDI helper.

Conclusion:
- New render work is bounded scalar state/expiry handling. The retained reachable
  RubberBand blocker keeps the full audit at FAIL; real Live validation remains.
