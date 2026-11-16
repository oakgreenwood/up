# SHAKE Virtual MIDI And Return-Input Check

Realtime audio audit: FAIL

Audited realtime entry points:
- processBlock/processBlockBypassed, preparation/release, synth MIDI dispatch,
  voice selection/start/render, local SHAKE queue rejection and MIDI capture.
- Editor gesture/timer/teardown and helper worker/message lifecycle as shared
  ownership, publication and returned-MIDI boundaries.

Reachability inspected:
- Editor TRACK MIDI/SHAKE state -> new local target/session or invalid target ->
  release-store active virtual flag -> audio acquire-load plus saved-mode read ->
  accept bounded local commands or reject local note-ons / flush old note-offs.
- Gesture -> per-note generation baseline/deadline -> helper fixed UDP stroke ->
  system MIDI source -> DAW input -> short incoming MIDI note-on counter increment
  -> ordinary sample playback/capture -> message timer acknowledgement or alert.
- Editor timer -> discovery/lease -> bounded UDP replies. Mouse motion uses cached
  readiness; it performs no discovery/file read. Helper worker handles MIDI and
  note deadlines; helper message timer handles discovery-file publication.
- Returned MIDI -> synth/voice -> live-warp cache miss -> RealtimeWarpPlayer::start
  -> RubberBand reset/scavenger/reconfiguration.

Findings:
[BLOCKER] Source/Playback/RealtimeWarpPlayer.cpp:78
Call path: voice start -> live warp fallback -> R2Stretcher::reset -> scavenger
and reconfiguration (external/rubberband/src/faster/R2Stretcher.cpp:197).
Issue: The retained path can allocate/reclaim memory.
Why it matters: Returned SHAKE input reaches the same playback path.
Minimal fix: Prepared playback or an allocation-free fallback; outside this task.

Cross-thread ownership:
- shakeVirtualMidiActive: processor-owned, compile-time lock-free bool. Message
  writes on mode/enable/visibility/bypass/target changes and clears at teardown.
  Target changes precede release publication; audio acquire-loads. On entering
  TRACK MIDI the target is invalid; on leaving it a fresh target generation
  invalidates earlier queued commands before the active flag clears.
- virtualMidiInput: existing saved atomic int, now the TRACK MIDI preference.
  New/missing state defaults 0 (internal), positive old routes restore as 1.
  User clicks and non-realtime restore write; message/save and audio policy read.
  Compile-time lock-free, relaxed scalar mode reads carry no owning data. A
  true saved mode independently rejects local ons before the editor target
  update, and an active flag can keep rejecting them during the switch off.
  Only message sends the existing nonparameter host state-change notification;
  no new parameter/state field, APVTS snapshot or host call enters render.
- Existing local SHAKE SPSC: restored for internal mode. Message is sole producer,
  audio sole consumer. <=128 command slots, <=32 consumed per nonempty callback,
  <=224 short events (<=2016 serialized MIDI bytes), bounded per-note gates.
  Queue full drops new strokes; UI restores/rebases the gesture on failed enqueue.
  Commands carry target/reset generations and explicit up/down note selection;
  voice dispatch uses the same note/choke/cache path as returned MIDI. TRACK MIDI
  invalidates this target and never enqueues local triggers. Old gates flush.
- incomingNoteOnGenerations: 128 processor-owned uint32 atomics, explicitly
  initialized in construction and compile-time lock-free. Only normal incoming
  positive-velocity note-ons increment them. Local generated events, resets,
  note-offs and other pitches cannot acknowledge a stroke. Message reads one
  independent scalar; relaxed ordering suffices. Lifecycle does not reset them.
- Pending checks/alert flag: fixed 128-entry message-owned array. Baseline is
  captured before send, deadline is first unanswered stroke +1000 ms, and an
  unchanged baseline cannot extend it. Failed sends also arm checks. Timer scans
  <=128 entries; the warning flag is set before asynchronous alert creation and
  no editor pointer is captured by a completion callback. Disabling/hiding SHAKE,
  group changes, TRACK MIDI/SHAKE off, bypass or editor destruction cancels checks.
  Explicit TRACK MIDI selection gates the check; no recording flag is consulted.
  Transport start/stop alone neither cancels nor rearms it. No audio read/reclamation of UI objects.
- VirtualMidiClient: editor/message owns socket, identity, sequence, readiness,
  strings and cookie. Decoder validates fixed 64-byte packets; <=16 replies per
  poll, zero-time readiness waits, one-second discovery and 250 ms lease claims.
  It releases on target/enable/bypass/visibility/teardown changes. No OS MIDI,
  socket, JSON or file API becomes reachable from plugin audio.
- Helper: single worker owns the sole port, lease fields and 30 ms note gate;
  batches <=32 packets between deadline checks, rejects stale/duplicate/wrong
  owner traffic and closes notes on release/lease expiry. Immutable startup
  metadata is release/acquire published to its message timer for discovery I/O.
  Shutdown stops that timer, joins the worker, and reclaims MIDI output there.
  Discovery is private per-user (POSIX 0700/0600; Windows per-user ACL context).
- Samples, caches and voice storage retain existing ownership/reclamation rules.
  The removed Ableton transport scalar, file poller and companion add no remaining
  shared state. Native transport is used for existing take/gesture behavior only.

Verification:
- Static audit after final TRACK MIDI control/routing changes. No build,
  installer execution, tests, allocation instrumentation, plugin validation,
  listening or DAW run was requested/run for this revision. Inspected both
  modes, switching during queued/held hits, restored state, helper readiness,
  stop/start, bypass and editor lifetime after the final relevant code changes.
- Traced matching/wrong pitches, note-off-only traffic, unrelated local events,
  continued strokes without input, failed send/helper unavailable, resumed input,
  stop/play/record transitions, target changes, bypass, hidden/closed editors
  and failed-send gesture rollback. The existing native recording flag no longer
  gates/cancels these checks; the explicit saved TRACK MIDI state does.
- Normal render reads the active flag and, when false, the saved route scalar.
  Restored local scheduling stays within the existing fixed limits. Incoming short note-on adds the
  existing bounded counter increment. Zero blocks defer local queue consumption
  but can dispatch/acknowledge incoming MIDI. Variable/large host blocks and tiny
  voice sub-blocks retain existing bounded scheduling/chunking behavior.
- Inspected short-message filtering, fixed note bounds, note alias/choke identity,
  shared sound-root cache reads, unchanged DSP arithmetic and retained warp reset.
  No new allocation, destruction, buffer growth, lock/wait, file/device call,
  APVTS snapshot/replace or unbounded retry is introduced on plugin audio.
- Source inspection confirms no transport.json poller, expiry scalar, transport
  override type or Control Surface source/installer remains in the active code.
  No format/host name or recording flag controls SHAKE routing. macOS AU/VST3 and Windows use the
  same existing helper protocol; helper binary/protocol version are unchanged.

Residual risks:
- Existing RubberBand allocation blocker, JUCE synth locks and wrapper MIDI
  capacity assumptions from previous playback audits remain. No overall PASS.
- A matching input is a heuristic, not proof of originating port or recorded
  clip. Another controller playing that pitch can acknowledge it. Input delayed
  >1 second or suspended processing may warn despite a configured device.
  Enable-input guidance can also indicate missing arming/monitoring, filtering,
  an unavailable helper or another instance holding its lease.
- Native host recording flags still determine cache record-start boundaries;
  the observed Live VST3 missing flag can affect cache splits during playback.
  It no longer affects TRACK MIDI routing, its input checking or DAW input
  recording. User selection supplies intent to check the external route.
- TRACK MIDI audition requires input monitoring even while stopped; internal
  mode requires neither helper nor incoming MIDI and never arms this warning.
  One shared source can reach multiple armed tracks/DAWs; users must route it
  to the intended instance. Returned input is never echoed by this plugin.
- Helper timing/OS buffering is not sample-accurate. Runtime/SDK provisioning,
  signing, host port refresh, reconnects, killed-helper held notes and Windows
  11 backend/DAW compatibility still need platform/host testing. Windows 10 is
  outside this iteration. Pro Tools needs a separate AAX instrument build.

Conclusion:
- TRACK MIDI explicitly selects external routing and the bounded input check;
  off restores internal audition without warnings. The retained reachable
  RubberBand blocker keeps the full audit at FAIL.
