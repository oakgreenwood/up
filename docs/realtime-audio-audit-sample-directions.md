# Alternating Sample Directions

Realtime audio audit: FAIL

Audited realtime entry points:
- `AudioPluginAudioProcessor::processBlock` MIDI dispatch, including zero-block
  and end-of-block events; `processBlockBypassed` sequence retention.
- `PercussionSynthesiser::noteOn`, render-range timing, voice selection/stealing,
  and voice start, stop and render callbacks.
- `prepareToPlay` sequence initialization with rendering stopped.

Reachability inspected:
- Host position -> `HostTempoTracker::update` -> observed transport-start flag
  -> `resetPlaybackSequence` before any MIDI dispatch, including zero blocks.
- Short MIDI note-on -> synth dispatch -> velocity-layer selection -> nearest
  direction pool -> random variation -> voice start -> sequence update.
- Render ranges -> saturating idle-frame counter; note-on -> four-second
  frame/monotonic timeout -> sequence reset before choosing the next direction.
- Actual directional sound -> bounded scan of eight voices -> same-group
  opposite-direction choke -> latency countdown -> final-output quadratic fade
  -> stopped voice. Up fades over 40 ms; down fades over 130 ms. Voice stealing
  prefers another voice so either outgoing fade can complete.
- Note-off/controllers -> existing JUCE callbacks; these do not update direction
  history. Unmapped notes and failed voice allocation do not update it either.
- Voice start -> `beginPlayback` -> existing effect resets, velocity gain,
  atomic per-group parameters, original playback or pinned warp-cache lookup /
  live Rubber Band fallback.
- Construction -> filename parser -> sample loading -> prepared variation
  pools; stopped teardown -> clear maps/voices -> join warp worker -> free sounds.

Findings:

[BLOCKER] `Source/Playback/RealtimeWarpPlayer.cpp:78`
Call path: note start -> `PercussionVoice::beginPlayback` -> live warp cache
miss -> `RealtimeWarpPlayer::start` -> Rubber Band reset / rate reconfiguration.
Issue: The existing R2 fallback can allocate or reclaim memory on audio.
`external/rubberband/src/faster/R2Stretcher.cpp:197` calls scavenging and
`reconfigure`; `:736` retains allocating window/resampler/buffer branches.
Why it matters: The wider playback path is not proven realtime-safe when a
warped sound has no prepared cache. Direction selection does not introduce or
change this fallback; the new group 26 files have no warp metadata and use
ordinary one-shot playback.
Minimal fix: Replace the fallback with a proven allocation-free stretch path
or prepared, safely published playback buffers. This remains outside the
requested naming/alternation change.

Cross-thread ownership:
- Parsed names and registered sounds: constructed before rendering; parsing,
  decoding, sorting, map insertion and pool growth stay off audio. Existing
  synth sound ownership keeps borrowed pool and voice pointers alive.
- Layer maps / pool vectors: processor-owned and structurally immutable while
  rendering. Only audio updates each pool's scalar `lastChoice`. Removing the
  separate history map also removes its note-time `operator[]` / resize path.
  Maps are reclaimed during stopped teardown, before source sound destruction.
- Sequence: synth-owned scalar last-note / next-direction fields, shared by all
  MIDI channels, plus idle-frame/time fields, accessed only by audio during
  playback. Preparation and map rebuilding reset them only with rendering
  stopped; timeout resets run on audio. No UI reader, saved state,
  atomic pointer, new lock or ownership transfer is introduced.
- Transport-start memory: one tracker-owned boolean updated by audio only when
  valid host position is available; the returned start flag is a stack value.
  No publication or reclamation is needed, and `resetMotion` does not alter it.
- Sounds / audio / metadata: construction-thread-owned immutable resources,
  borrowed by voices and the warp worker. The synth retains references when a
  voice stops or is stolen, preventing final audio-thread sound destruction.
  Teardown clears voices, joins the worker, then destroys sounds and metadata.
  The direction tag is assigned during loading, before any audio/worker reader.
- Choke state: voice-owned scalar delay/fade counters, two fixed frame counts
  and two prepared reciprocals. Only audio schedules/advances them. Preparation
  computes the 40 ms up / 130 ms down durations and effect latency while
  rendering is stopped. Scheduling copies the outgoing direction's count/scale
  into the active fade state. No new buffers,
  queues, publication or dynamic ownership are added.
- Warp caches: unchanged worker-owned fixed slots, acquire/release publication,
  bounded two-attempt reader pinning, and decrement-only audio lease release.
  Replacement cannot reclaim a pinned predecessor; the worker reclaims it later.
- Parameter cache: existing fixed per-note scalar atomics; direction and velocity
  variants share the same mapped MIDI root note. No state-tree access is added.

Verification:
- Static audit only, after the final implementation edits. No build, automated
  tests, host validation or listening test was requested or run, per `AGENTS.md`.
- Inspected first-hit up, repeated up/down alternation, changing velocity,
  independent variation histories, another-group reset, ordinary groups,
  separate pitch slots, shared MIDI channels, incoming simultaneous-event order,
  absent direction/layer fallback and unchanged note-off/controller behavior.
- Re-inspected after the bidirectional 40/130 ms change: actual versus requested
  direction fallbacks, same-group cross-channel/velocity choking, other
  groups/pitch slots, repeated hits in both directions, natural source end, existing effect
  drain, hard stop, voice reuse and saturated polyphony.
- Matching directions are searched across layers before ordinary/opposite
  fallback. Searches visit at most the prepared layer count per direction;
  the current inventory has at most four layers, and group 26 has three.
  Random selection uses the existing synth-local JUCE PRNG with at most one
  reroll. Pool indices and nonempty checks precede indexing; histories cannot
  outlive or grow independently of their immutable pools.
- JUCE `addVoice` reserves its stealing scratch before rendering. Existing synth
  recursive locks are retained; sample/voice mutation remains in construction,
  stopped preparation and teardown. The worker uses its construction-time
  inventory and does not mutate the synth or new direction state during audio.
- The idle counter saturates at four seconds of frames; adding a positive
  `int` render count cannot overflow its `uint64_t` storage. Threshold calculation
  validates the rate before conversion. A successful trigger resets both frame
  and wall-clock history; note-off/unmapped/failed starts do not restart it.
  Exact-threshold hits reset before selection. Offline rendering advances frames
  independently of processing speed; host callback suspension is covered by
  the elapsed monotonic clock. JUCE uses `mach_absolute_time` on macOS and
  `QueryPerformanceCounter` on Windows with prepared scale factors, no allocation
  or locks. Those implementations were inspected locally.
- Re-inspected after reducing the pause to four seconds: the initial 176400-frame
  threshold, rate-derived `ceil(rate * 4)` threshold and 4000 ms monotonic check
  agree. This constant-only change adds no realtime work or ownership changes.
- Re-inspected after adding playback-start reset: the first known running
  report and each observed stopped-to-playing/recording edge reset before MIDI.
  Continued playback does not reset on each block; stop, seek, tempo changes
  and missing position do not create a start. This adds constant scalar work,
  uses the existing single host-position query, and reaches only the bounded
  scalar sequence reset. No voice stop, sample destruction, lock, allocation,
  state-tree operation or UI callback is added. Verification was static only.
- The 40 ms and 130 ms frame counts use a finite rate bounded to 1000..768000 Hz
  (44100 fallback). Counts therefore remain positive and at most 99840 frames,
  safely within int range. At 48 kHz these are 1920 and 6240 frames. The reciprocal
  denominator is at least one. Two-entry arrays are allocated inline; scheduling
  indexes 0 for outgoing up and 1 for outgoing down, rejecting ordinary sounds.
  Each render chunk ends at the next delay/fade boundary; the countdown cannot
  underflow and both stereo channels share one squared gain per frame. The first
  fade gain is unity and the last is exact zero. Clearing at zero also suppresses
  residual formant/EQ tails and releases only the existing decrement-only cache
  lease; synth ownership prevents final sound destruction. One-frame, changing,
  zero and large blocks do not change the fade duration or grow scratch storage.
  Further hits in either direction cannot restart a pending fade. Ordinary voices retain unity
  gain and their previous note-off behavior. Borrowed velocity gain is unchanged.
- Re-inspected the new up-triggered down fade after final code changes: same-note
  stop handling preserves the outgoing down voice before allocation, then the
  bounded scan schedules only the actual opposite direction after a new voice
  is found. Failed allocation does not schedule a choke. Actual ordinary
  fallbacks retain the old path. Stealing uses the same opposite-direction
  predicate in both directions and allocates no scratch or new voice storage.
  The render loop and final clearing path are unchanged; scalar schedule/index
  reads add no allocation, lock, UI call, file access or reclamation. Existing
  synth locks still have no new UI/loader contender. Verification was static only.

Residual risks:
- The existing reachable Rubber Band fallback remains a realtime blocker.
- macOS arm64/x86_64, Windows compilation, DAW behavior and audible sample
  transitions remain untested. The change uses portable C++ / JUCE only.
- A group missing a direction everywhere necessarily falls back to an ordinary
  or opposite-direction sound; the requested sequence still advances per hit.
- If all eight voices are matching opposite-direction voices, a new directional
  hit must still hard-steal one. The others retain their scheduled ease-out.
  Longer fades make voice saturation more likely during very rapid shaking;
  the eight-voice limit and source/note-off end conditions remain unchanged.
- Fade timing follows existing MIDI coalescing and reported voice-effect latency;
  audio transitions and timeout behavior have not been listening/host tested.
- Playback-start detection requires host transport reports. A stop/restart
  entirely between callbacks cannot be distinguished from continuous playback;
  missing transport data cannot supply a start edge.

Conclusion:
- Direction selection, choke scheduling/fading and timeout handling add no
  allocation, reclamation or synchronization to note selection/rendering;
  the reachable existing live-warp fallback keeps the full
  audited playback path at FAIL.
