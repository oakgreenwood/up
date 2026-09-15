# UI

Read for selector layout or MIDI activity. For selected-group identity, parameter
attachments, automation, or persistence, see [parameters and state](parameters-and-state.md).
Audio/message-thread activity handoff changes require the
[realtime audio audit](../.agents/skills/realtime-audio-audit/SKILL.md).

## Sample Gain

The `Gain` knob sits immediately left of Punch and Pitch, using the same image
knob style. It controls the selected sample group from -20 to +20 dB in 0.1 dB
steps, defaults to 0 dB, and resets to 0 dB on double-click. Its editable value
field displays values such as `+6.0 dB` and uses the same signed one-decimal
formatting, validation, focus styling, hidden caret, commit/cancel behavior,
automation handling, and selection refresh as Pitch and Formant. Its APVTS
attachment participates in automation and Apply to All.

## Punch

The Punch knob retains its internal `0.0..1.0` range and uses 0.01 steps. Its
editable field presents that value as a whole percentage from `0%` to `100%`.
Typing `50` or `50%` commits an internal value of `0.5`. Input accepts values
from 0 through 100, ASCII digits, and one optional trailing percent sign. Values
above 100, signs, decimal points, letters, whitespace, and misplaced or repeated
percent signs are rejected immediately. The field shares the other numeric
inputs' focus, commit/cancel, automation, and sample-selection behavior.

## Mono

The Mono knob sits to the right of Formant, with the same image knob and editable
percentage input as Punch. The sample-effect row is Gain, Punch, Pitch, Formant,
Mono, Panorama. It ranges from 0% (original stereo) to 100% (summed mono), in 1% steps,
defaults to 0%, and double-click resets it to 0%.

Typing `50` or `50%` commits 50%. The shared percentage validator accepts ASCII
digits and one optional trailing `%`, rejects signs, decimals, whitespace,
letters, repeated/misplaced `%` and values above 100, including oversized pasted
integers without overflow. Empty/incomplete input restores the current value on
commit. Enter, Tab/focus loss, Escape, selection refresh and automation during
editing follow the existing numeric input behavior. Arrow edits change by 1%.
The attachment and parameter registry provide automation, per-sample saved state,
Apply to All and automatic effect-dropdown selection on an actual UI edit.

## Panorama

Panorama sits immediately to the right of Mono. It is sample-specific, ranges
from -50 to +50 in one-unit steps, and defaults/resets on double-click to 0.
The input displays `C` at zero, `1L` through `50L` on the left, and `1R` through
`50R` on the right. Negative signed integers choose the left side; positive
integers choose the right. The knob, arrow keys and APVTS range all use step 1.

The input accepts signed integers such as `-25`, `0`, `+25`, unsigned direction
notation such as `25L`/`25R`, and `C` (letters are case-insensitive). It rejects
magnitudes over 50, decimals, whitespace, arbitrary letters, multiple signs or
directions, and combinations of a sign with an L/R suffix. Range checking is
digit-by-digit to reject oversized pastes without integer overflow. Empty text,
a sign or an L/R alone can occur during editing but cannot commit a value.
The existing numeric input handles commit/cancel, selection refresh and host
automation; successful edits select Panorama in Apply to All. Restored missing
values display `C`. Panorama processes the sound after Mono; see
[playback](playback-warp-and-transients.md#sample-panorama).

## Apply To All

`APPLY TO ALL` sits below the sample-specific effects. An effect dropdown sits
directly to its right, with no value or status text below the button. The dropdown
lists every parameter in `PluginParameters::sampleSpecificParameters`, using the
parameter's host-facing name, so newly registered sample-specific effects appear
automatically. It starts with Gain selected. The collapsed dropdown has no border
or background; only its selected effect name and arrow are visible.

Editing a registered sample-specific effect selects that effect in the dropdown;
the user may also choose one directly. Clicking the button reads the selected
effect's current value from the selected sample and copies it to all sample
groups. Changing samples keeps the effect selection, so the button then uses the
new sample's current value. Changing a global effect does not affect the dropdown.

The selected effect is also the keyboard target. With no modifier keys, Up or
Right increases Gain, Pitch, or Formant by 0.1, while Down or Left decreases it
by 0.1. Punch and Mono change by 1%, which converts to 0.01 in their stored `0..1` range.
Panorama changes by 1, crossing from `1L` through `C` to `1R` as it increases.
Values clamp at their parameter bounds. Arrow edits work from the knob, its value
field, or after choosing an effect in the dropdown, and send a normal parameter
change gesture.

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
background caching. Pitch ranges from -12 to +12 semitones in 0.1-semitone
steps. An editable field below the knob displays exactly one decimal place and
includes the sign for non-negative values and the `st` suffix, for example
`+11.3 st`. Input accepts an optional single leading sign, ASCII digits and one
decimal point following a digit. Typing or pasting extra fractional digits keeps
only the first: `+11.35` immediately becomes `+11.3`. The exact displayed unit
suffix may remain in the field, but arbitrary letters, whitespace, other
characters, misplaced signs and multiple decimal points reject the entire edit
without replacing selected text. Empty text, a lone sign and a trailing decimal
point are allowed while editing; committing these incomplete values restores the
latest parameter display with its unit.

Clicking the value selects its text for editing. Enter, Tab or clicking elsewhere
commits a complete value; Escape cancels. Valid numeric input still uses the
existing parameter range and snapping. The fields are separate editable labels
listening to their sliders, so automation updates the knob/audio while leaving
an unfinished edit intact. Committing a changed value sends a normal slider
change gesture; cancelling or closing an untouched field displays the latest
automated value. Selection refresh discards any remaining edit for the old
sample before displaying the new sample's value.
Formatting and parsing callbacks are installed after the APVTS attachment, which
otherwise replaces them during construction.

At rest, each sample-effect value is drawn without a background
or border. While editing, the full input rectangle uses a light-grey background
and a visible focus outline.
Selected text remains black and has no colored selection highlight. The caret is
hidden while typing. Units are visible before editing and restored after a value
is committed.

## Formant

The single Formant knob sits immediately right of Pitch; the sample-effect row
is Gain, Punch, Pitch, Formant, Mono, Panorama. It is the former PSOLA Formant3 option, retaining
its EQ/saturation coloration, -12..+12 semitone range, zero default and double-click
reset. Its APVTS attachment supports selection refresh, automation, state and
Apply to All. The separate LPC knob is removed. Host display and the Apply to All
effect selection also say Formant; the underlying PSOLA parameter ID is preserved
for saved values and automation. Its editable field uses the same validation,
editing behavior and signed one-decimal semitone format as Pitch. At zero,
natural varispeed formants remain.
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
Each note-on also publishes the latest played MIDI note through a scalar atomic
generation handoff. After an idle period, the editor selects the first mapped
note immediately. Further mapped notes keep only the latest selection pending;
500 ms after the final note-on, the editor selects that last sample. This avoids
rapid control changes while playing a phrase without delaying an isolated hit.
A quick note-on/note-off between timer ticks is still recognized. Manually
clicking a selector item cancels the pending MIDI selection, and notes with no
loaded sample group do not change or extend the selection burst.
