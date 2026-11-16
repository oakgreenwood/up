# Ableton MIDI Insertion

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock`, including host-position capture,
  take boundaries, note capture and prefix publication.
- `processBlockBypassed`, preparation and release finalization retain their
  existing take lifecycle; no bridge work is added to those paths.
- Message-thread MIDI export, parameter-token updates and bridge polling were
  checked for separation from the audio path.

Reachability inspected:
- Callback -> existing single `HostTempoTracker::update` position query ->
  finite optional PPQ scalar -> `ShakeMidiTake::beginBlock`.
- Transport start / first captured note -> existing `startTake` slot reservation
  -> immutable original Arrangement position -> existing prefix publication.
- UI -> slot pin / publication recheck -> acquired prefix and immutable origin
  -> private snapshot -> MIDI file -> JSON request and later acknowledgement.
- UI -> ULICH Link parameter notification -> APVTS scalar/host notification.
  The parameter is absent from the sample-specific listener registry and DSP.
- Existing playback/warp reachability is retained; see the
  [cache audit](realtime-audio-audit-shake-cache.md).

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:78`
Call path: note start -> live warp fallback -> stretcher reset -> Rubber Band R2
reset/scavenger/reconfiguration.
Issue: The established fallback can reclaim/allocate memory. Its reset path was
re-inspected; this task does not alter it.
Why it matters: The full reachable playback path cannot receive PASS.
Minimal fix: Replace the live fallback with allocation-free playback or prepared
buffers. This remains outside host clip insertion.

[RISK] `integrations/ableton/ULICHPERC/bridge.py`
Call path: Live Control Surface update -> validate request -> locate device ->
create/stage MIDI clip -> duplicate into Arrangement -> cleanup/acknowledge.
Issue: These host/control-plane APIs, their undo behavior and parameter exposure
need real Live testing. They are not called from the plugin's audio callback.
Why it matters: Static review does not establish host compatibility or success.
Minimal fix: Test the documented setup and error paths in the intended hosts.

Cross-thread ownership:
- Original Arrangement position: audio writes a scalar while the existing slot
  reservation holds `readers == -1`. It is immutable throughout publication of
  that generation. Audio cannot reserve/reset a pinned slot. UI acquires a pin,
  rechecks publication and copies the origin alongside its note snapshot before
  releasing the pin. The reservation's release and successful pin's acquire
  also prevent a stale reader from observing reset storage as an older take.
- Host PPQ/block origin: audio-owned stack/member scalars; unavailable or
  non-finite positions are not inferred. Negative positions cannot be inserted.
  No new audio-thread container, allocation, destruction, lock or retry is added.
- Notes/prefix/end atomics: ownership and bounds remain those of the cache audit.
  Three slots are allocated at construction; stopped processor teardown owns
  reclamation. UI snapshots, streams and JSON own their allocations on message.
- ULICH Link: constructed in the appended APVTS slot, saved/restored by the same
  APVTS tree and defaulted for older state on the non-realtime restore path.
  Project audio code never looks it up, reads it or handles its notifications.
- Bridge client: editor/message-owned. File I/O, JSON/MIDI parsing, host gesture
  notification, polling, cancellation and destruction stay on message. There
  is no worker/network callback that can invoke plugin rendering or vice versa.
- Live script: owns its decoded request, temporary host clips and response
  files in Live's Control Surface callbacks. The plugin only sees file data;
  no Live/Python pointer or object crosses into plugin audio state.

Verification:
- Static source audit after the implementation changes. No build, automated
  validation, allocation test, DAW run or listening test was requested/run.
- Inspected independent playback/recording starts, punch-in, stopped first-hit
  origin, missing/non-finite/negative PPQ, zero blocks and frame-offset origins.
- Origin storage adds constant work. Existing note capture/reservation limits
  remain unchanged: <=3 slot attempts and <=2048 active keys on replacement.
- Existing rate/BPM guards bound frame arithmetic; no array index derives from
  PPQ. The origin is fixed once and does not drift with tempo, loops or seeks.
- UI export returns origin from the same pinned snapshot as its file's notes;
  it cannot mix two take generations. Subsequent MIDI-file/JSON work is local.
- Requests cap at 32768 notes, 8 MiB JSON and existing finite take length. Live
  validates note bounds/timing, insertion range, routing token, expiry and
  destination conditions before creating a clip. Device traversal is bounded.
- Polling is at most 5 Hz while pending and times out after 15 seconds. Host
  mutation is never automatically retried after acknowledgement-write failure.

Residual risks:
- The existing live Rubber Band fallback remains a realtime blocker.
- The bridge has not been compiled or tested in Live on macOS/Windows or AU/VST3.
  Custom plugin configurations may require exposing ULICH Link. An out-of-process
  host sandbox cannot use this same-process mailbox.
- Insertion/cleanup/acknowledgement can fail after some host work completes.
  Inspect the track before retrying. Existing-clip overlap checks prevent normal
  duplicate insertion at the same location; no global Undo action is simulated.
- Host PPQ reporting and latency compensation determine observed start accuracy.
  Loops/seeks preserve the existing linear elapsed-beat cache behavior.

Conclusion:
- Retaining the origin adds scalar-only work to capture. Host communication
  stays outside render. The unchanged reachable warp fallback keeps the full
  realtime audit at FAIL.
