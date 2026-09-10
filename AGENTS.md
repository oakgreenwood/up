# AGENTS.md

## Project

Sample-based C++/JUCE/CMake audio plugin. Targets macOS (Apple Silicon arm64,
Intel x86_64, universal binaries when needed) and Windows. Primary formats: VST3
and AU.

## Task References

Use [ULICHPERC.md](ULICHPERC.md) for the source map. Before editing, read the
references for the affected topics; do not load unrelated documents:

| Affected topic | Read |
| --- | --- |
| Sample loading/naming, MIDI mapping, velocity layers/gain, variations | [Samples and velocity](docs/samples-and-velocity.md) |
| Playback, warp, transients, pitch/punch DSP, `processBlock` | [Playback, warp, and transients](docs/playback-warp-and-transients.md) |
| Parameters, automation, plugin state, sample-specific values/realtime caches | [Parameters and state](docs/parameters-and-state.md) |
| Selector layout and MIDI activity UI | [UI](docs/ui.md) |
| Builds, CMake, dependencies, formats, platforms/architectures | [Building](docs/building.md) |

Follow a topic document's cross-links when the task affects those areas. Keep
implementation facts in the relevant topic document when updating behavior.

## Workflow

- Inspect relevant files first. For non-trivial tasks, restate the goal and
  identify missing information or risky assumptions. Ask about missing
  information before proceeding; state assumptions explicitly.
- Give a short plan before implementing any non-trivial task or any change to
  CMake/builds, cross-platform behavior, audio/DSP, sample loading/playback,
  threading/synchronization, plugin state, performance-sensitive code, or
  `processBlock`.
- Prefer correctness over completeness, simple solutions, and existing patterns.
  Avoid unnecessary architecture changes and unrelated refactors.
- Implement in small, reviewable steps and verify the result matches the goal.
- Don't run build or validation after every change. Do it only when asked.

## Code Quality

- Use modern C++ and RAII; avoid raw owning pointers. Prefer `std::unique_ptr`,
  `std::vector`, and `std::array`.
- Use clear names and small, focused functions/classes. Separate responsibilities
  into classes when that improves readability.

## Completion

- Consider macOS/Windows implications and respect realtime constraints.
- For applicable changes, the audit below runs after the final relevant code
  changes and reports `PASS`, or explicitly reports unresolved blockers.
- Summarize files and behavior changed, verification, remaining risks, and TODOs.

## Realtime Audio Rules (Critical)

Inside the audio thread (`processBlock` and related code):

- No allocation, file I/O, UI calls, logging, or printing.
- No locks (mutex, etc.) unless unavoidable.
- Keep processing deterministic and fast.

Clearly state if any proposed solution is NOT realtime-safe.

## Realtime Audio Audit Skill

Apply [realtime-audio-audit](.agents/skills/realtime-audio-audit/SKILL.md)
whenever a change touches or can change the behavior of:

- `processBlock` or `processBlockBypassed`
- any function transitively called from the realtime render path
- `juce::Synthesiser` / `juce::SynthesiserVoice` rendering or note/controller callbacks
- sample or voice playback
- interpolation, warp, transient processing, envelopes, filters, or other realtime DSP
- audio-thread parameter reads
- sample/metadata/state publication between loader/message threads and the audio thread
- ownership or lifetime of objects read or released by the audio thread
- audio-thread containers, scratch buffers, queues, atomics, or synchronization
- performance-sensitive code in the render path

The skill supplements the basic rules above. It must inspect hidden allocation/deallocation, destruction, contention, ownership/reclamation, bounded execution time, variable/zero block sizes, JUCE-specific realtime traps, and DSP numerical edge cases.

A successful compile is not a substitute for the audit.

For applicable tasks, perform the audit after the final relevant implementation changes, not only before editing.

## JUCE-Specific Rules

- Respect JUCE threading: UI only on the message thread, audio only on the audio
  thread.
- Handle `prepareToPlay`, `processBlock`, `releaseResources`, sample-rate changes,
  and block-size changes correctly; avoid assumptions about DAW behavior.
- Use a clear parameter/state system, such as `AudioProcessorValueTreeState`,
  with reliable serialization. Every host-visible parameter must be saved and
  restored by plugin state. For APVTS, `getStateInformation` should serialize
  `parameters.copyState()` and `setStateInformation` should restore the same
  tree with `parameters.replaceState(...)`.
