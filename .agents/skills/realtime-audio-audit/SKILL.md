---
name: realtime-audio-audit
description: Audit JUCE/C++ realtime safety when changes affect processBlock/processBlockBypassed, transitive render code, Synthesiser/SynthesiserVoice or MIDI callbacks, sample/voice playback, DSP, audio-thread parameter reads, or state shared with loader/message threads. Check allocation/destruction, blocking, publication/lifetime, bounded work, block sizes, and numerical safety. Exclude UI-only or build-only changes unless they affect the realtime path.
---

# Realtime Audio Audit

Supplement `AGENTS.md` by inspecting hidden/transitive hazards. Compilation and
absence of obvious allocation/locks in an edited function do not establish safety.

## Scope And References

Start from every affected realtime entry point: `AudioProcessor::processBlock`,
`processBlockBypassed`, `Synthesiser::renderNextBlock`,
`SynthesiserVoice::renderNextBlock`, rendering-time note start/stop and
MIDI/controller callbacks, and custom audio-device/render-thread callbacks.

Trace relevant branches until their behavior is understood, including helpers,
indirect callbacks, concrete virtual implementations, stored lambdas/function
objects, state adoption, and destructors triggered by assignment, erase, reset,
swap, or reference-count release. Helpers used on other threads still require
this audit if audio can reach them. Include untouched reachable dependencies.

- Read [JUCE checks](references/juce-checks.md) for reachable JUCE processing,
  buffers, APVTS, synthesis, or MIDI. Rendering requires variable/zero host-block
  and tiny voice-sub-block checks.
- Read [DSP checks](references/dsp-checks.md) for reachable playback, interpolation,
  warp/transient traversal, envelopes, filters, parameter-driven DSP, or
  sample/frame arithmetic; inspect numerical and bounds behavior.
- Use [search heuristics](references/search-heuristics.md) to locate entry points
  and hidden operations. Inspect semantics and wrappers; keyword hits alone
  are not findings.

## Allocation And Destruction

Check for allocation, storage growth, heap-backed copies, variable-sized data
movement, and destruction beyond explicit `new`/`malloc`:

- `std::vector`, `std::string`, `std::deque`, associative/JUCE dynamic containers;
  `push_back`, `emplace_back`, `insert`, `resize`, `reserve`, assignment,
  concatenation, and capacity growth.
- `std::function` creation/assignment; `std::make_unique`, `std::make_shared`,
  `std::shared_ptr`, `std::weak_ptr`, and reference-counted JUCE objects.
- `unique_ptr::reset`, `shared_ptr` assignment/reset, `optional::reset`, `variant`
  replacement, `erase`, `clear`, and temporary destruction.
- Copy-on-write/final reference release; `juce::AudioBuffer::setSize`; JUCE strings,
  trees, arrays, images, streams, and other heap-backed objects created/modified
  during rendering.

Off-thread allocation does not make audio-thread reclamation safe. Trace final
releases when replacing shared pointers, clearing owning containers, swapping
samples, and decrementing reference counts in voice/render callbacks. Explicitly
identify which thread destroys old samples/metadata; uncertain reclamation is
`INCOMPLETE`, or `FAIL` if an unsafe path is established.

## Synchronization, Publication, And Ownership

Inspect `juce::CriticalSection`, `juce::SpinLock`, `std::mutex` (including recursive
and shared mutexes), condition variables, `WaitableEvent`, joins, synchronization
sleeps/yields, message-thread synchronization, and internally locking APIs.
Thread safety alone does not establish realtime safety.

For atomics, prefer scalar types and verify target lock-freedom where it matters
and sufficient memory ordering. Neither `std::atomic<std::shared_ptr<T>>` nor a
raw atomic pointer swap proves safe publication/reclamation.

For **every shared object**, record creator/writer/reader threads, mutation policy,
publication, ownership, reclamation thread, and what happens if a replacement
arrives before its predecessor retires. This is mandatory for sample sets/buffers,
warp/transient metadata, lookup tables, and other large immutable snapshots.
Prefer immutable, prepared data. Prove that audio sees a fully constructed object,
that it remains alive while read, and that audio never performs its destruction.
Flag concurrent in-place mutation unless synchronization is safe and non-blocking.

For queues, prefer bounded, preallocated SPSC/ring buffers when appropriate.
Verify fixed capacity, allocation-free push/pop, clear producer/consumer ownership,
an explicit overflow policy protecting partially consumed data, no consumer wait,
and no audio-thread cleanup fallback. Use a simpler atomic scalar or immutable
snapshot when sufficient; do not add queues merely to satisfy the audit.

## Bounded Work And Performance

Establish practical worst-case bounds, not just average performance:

- Identify termination and worst-case iterations of every reachable `while` loop.
- Inspect dependence on sample/file length instead of render range, queue backlog,
  malformed/external metadata, open-ended searches, unbounded containers,
  recursion, sorting, or structure rebuilding.
- Bound voices visited. Verify warp/transient indices advance monotonically
  within validated arrays. Include pathological parameter values.

Report plausible realtime costs from repeated parameter-ID lookup, rebuilding
unchanged coefficients/state, format/string conversion, large metadata traversal,
cache-unfriendly pointer chasing, or per-sample transcendental functions when a
suitable prepared/incremental equivalent exists. Avoid premature micro-optimization.

## Verification And Status

After static inspection, build affected targets for implementation changes when
the environment permits. Run relevant existing allocation/stress tests, sanitizers,
or plugin validation; do not add unrelated tooling or claim unperformed checks.
Inspect implementations or authoritative documentation for material unknown
JUCE/third-party behavior.

Report exactly one status:

- `PASS`: no blocking issue found in the inspected realtime path.
- `FAIL`: a realtime-reachable blocker exists, including possible allocation or
  resource destruction/deallocation, buffer growth, a contending lock/wait,
  unsafe shared mutation/reclamation, unbounded/practically uncontrolled work,
  APVTS state-copy/state-replace, plausible host/sample input causing out-of-bounds
  access, or unhandled runaway non-finite DSP state.
- `INCOMPLETE`: relevant code, ownership, third-party behavior, or thread
  interaction cannot be inspected well enough to establish safety.

Distinguish unavoidable JUCE/host mechanisms from project code introducing
avoidable contention; use judgment without weakening the requirements.

## Required report format

Use this structure:

```text
Realtime audio audit: PASS | FAIL | INCOMPLETE

Audited realtime entry points:
- ...

Reachability inspected:
- entry -> helper -> helper
- ...

Findings:
[BLOCKER | RISK | PERF] file:line
Call path: ...
Issue: ...
Why it matters: ...
Minimal fix: ...

Cross-thread ownership:
- object: ...
  publish: ...
  audio read: ...
  reclamation: ...

Verification:
- build/tests/tools actually run
- or "static audit only"

Residual risks:
- ...

Conclusion:
- one short statement explaining the final status
```

Omit empty finding categories, but never omit the final status, the audited entry points, verification, or residual risks.

## Completion

Run the audit after the final relevant implementation changes before completing
an applicable coding task. Fix project-code blockers within the task's scope,
then re-audit the affected paths. Never weaken requirements to obtain `PASS`.
