# UI

Read for selector layout or MIDI activity. For selected-group identity, parameter
attachments, automation, or persistence, see [parameters and state](parameters-and-state.md).
Audio/message-thread activity handoff changes require the
[realtime audio audit](../.agents/skills/realtime-audio-audit/SKILL.md).

## Sample Gain

The `Gain` knob sits immediately left of Punch and Pitch, using the same image
knob style. It controls the selected sample group from -10 to +10 dB, defaults
to 0 dB, and resets to 0 dB on double-click. Its APVTS attachment participates
in selection refresh, automation, and Apply to All.

## Apply To All

`APPLY TO ALL` sits below the sample-specific effects on the right. It starts
disabled, with `Edit a sample effect first` beneath it. After editing a registered
sample effect, the label displays the captured effect name and value; clicking
the button copies just that value to all sample groups. The selected sample stays
selected. Switching samples or changing a global effect does not replace the
captured edit. Reopening the editor starts a fresh edit history.

Edit tracking listens to APVTS parameter gestures, independently of control type.
New effects registered in `PluginParameters::sampleSpecificParameters` therefore
participate automatically when their controls use standard attachments/gestures.
Ordinary automation, selection synchronization, and UI refreshes do not count as
edits. See [parameters and state](parameters-and-state.md#apply-to-all) for batch
publication and persistence.

## Pitch

The Pitch knob changes playback speed and duration for non-warp one-shots,
affecting new hits only. The Keep length button and pitch-preparation status
label are removed. Warp-enabled samples retain their existing tempo/pitch processing and
background caching.

## Formant

The single Formant knob sits immediately right of Pitch; the sample-effect row
is Gain, Punch, Pitch, Formant. It is the former PSOLA Formant3 option, retaining
its EQ/saturation coloration, -12..+12 semitone range, zero default and double-click
reset. Its APVTS attachment supports selection refresh, automation, state and
Apply to All. The separate LPC knob is removed. Host display and captured edit
labels also say Formant; the underlying PSOLA parameter ID is preserved for
saved values and automation. At zero, natural varispeed formants remain.
See [playback](playback-warp-and-transients.md#formant) for tracking/latency limits
and [state](parameters-and-state.md) for the inert former LPC host slot.

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
