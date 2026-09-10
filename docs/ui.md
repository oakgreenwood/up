# UI

Read for selector layout or MIDI activity. For selected-group identity, parameter
attachments, automation, or persistence, see [parameters and state](parameters-and-state.md).
Audio/message-thread activity handoff changes require the
[realtime audio audit](../.agents/skills/realtime-audio-audit/SKILL.md).

## Sample Group Selector

The bottom `SampleGroupSelector` runs only on the message thread. Each item is
one stable `(noteIndex, pitchIndex)` group from the loader's keyboard map.

- Buttons keep fixed size; extra width goes into gaps, with a minimum of `15 px`.
  Scroll horizontally when groups cannot fit.
- Resting buttons clip at the bottom edge, `6 px` below their original
  half-visible position. A held MIDI note lifts its button by up to `10 px`,
  scaled by note-on velocity; note-off returns it.
- The selected dot stays anchored to the resting position, raised an extra
  `10 px`, independent of button activity.

## Activity Handoff

Audio writes note velocities/generation counters to `MidiNoteActivityState`.
The editor builds `midiNote -> sampleGroupIndex` once from loaded groups, polls
128 fixed note generations, and updates only buttons whose related note changed.
