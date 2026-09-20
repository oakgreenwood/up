# UI

Read for selector layout or MIDI activity. For selected-group identity, parameter
attachments, automation, or persistence, see [parameters and state](parameters-and-state.md).
Audio/message-thread activity handoff changes require the
[realtime audio audit](../.agents/skills/realtime-audio-audit/SKILL.md).

## Sample Effects Modal

Sample-specific controls are hidden from the main editor until the centred
`EDIT SAMPLES` button above the sample-group sticks is clicked. The modal uses
an opaque `#D9D9D9` background, spans the editor width inside the existing
12-pixel padding, is anchored to the bottom of that padded area, and occupies
70% of the editor height. It contains Gain, Punch, Pitch, Formant, Mono,
Panorama, the sample equaliser, and Apply to All.

`EDIT SAMPLES` is hidden while the modal is open. The sample-group sticks use
the same 12-pixel left, right and bottom padding as the modal, so their lower
edge aligns with the bottom of the open modal.

The modal does not dim the rest of the editor. Global effect controls are hidden
while it is open. A click on empty editor background outside the modal closes
it, while an outside interactive child control keeps it open. The cross icon
just above the modal's top-right corner also closes it.

## Global OTT

The global `OTT` knob sits immediately right of Pomyatost, with the existing
image knob style and an `OTT` label, without a numeric input or value display.
It controls compression strength from 0% to 100% in 1% steps, defaults to 0%,
and double-click resets to 0%. Its ordinary APVTS attachment supplies automation
and saved global state. Sample selection and Apply to All do not change it.

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

## Sample Equaliser

The EQ panel is 425 x 170 pixels, giving a 2.5:1 width-to-height ratio. Plot
coordinates reach the black border with no inner padding. Dots on the endpoints
are clipped by the component edge but remain draggable.
The transparent graph below the sample controls has a thin black border, no grid
or frequency/dB labels, and four solid black, unnumbered draggable dots:
low shelf, bell 1, bell 2, high shelf. Drag horizontally for frequency on a
logarithmic 20 Hz..20 kHz axis and vertically for -15..+15 dB gain. All Q values
are fixed at 1.0. Each band defaults to 0 dB; default frequencies are 100, 500,
2500 and 10000 Hz. The upper graph/filter frequency is limited to 45% of the
current sample rate when that is below 20 kHz. There are no extra knobs, band
switches, Q controls or numeric editors.

The black curve displays the combined response of the actual four filters.
Each dot displays its own band's frequency and gain independently of the
combined curve, with no connector lines. Vertical dragging directly sets that
band's gain within -15..+15 dB; the other bands do not affect its position.
The combined response is clipped to the display range. Coincident dots can
be selected in turn by repeated clicks. Dragging preserves the initial grab offset
and sends paired frequency/gain host gestures. Changing samples or closing the
editor ends the gesture; a drag cannot continue writing into a different sample.
The EQ has its own 60 Hz message-thread timer; the other editor controls retain
their 30 Hz timer. The response curve is cached until values, rate or geometry
change. Frequency/bin coordinates are cached by rate and geometry. The spectrum
uses two polygon segments per logical pixel, capped at 2048 segments, and keeps
FFT peaks that fall between segment endpoints when multiple bins share a segment.
Monotone cubic interpolation smooths the widely spaced low-frequency bins without
overshooting their measured levels; peak preservation is skipped in these expanded
segments to avoid steps. A further triangular average spans six display segments
(normally three pixels) on each side to soften remaining corners. It slightly
softens narrow peaks and operates on fresh display values without accumulating
across frames. This smoothing runs only on the message thread and does not change
the filters or FFT size. Silent unchanged paths do not request further repaints.

The spectrum, filled with opaque `#8E8B8B`, shows only the selected sample group's summed playing voices,
after EQ and before global Rzhavchina/OTT. A message-thread 2048-point Hann FFT
combines stereo channel powers without phase cancellation. Windows overlap by
75%, publishing a fresh complete frame every 512 audio samples after the first
2048 samples. Only the newest matching packet is transformed each timer tick,
limiting work to two FFTs per tick. Display levels use a -90..0 dBFS range
independently of the EQ gain axis and decay at 60 dB per second using elapsed
time, so the higher refresh rate does not speed up the decay.
Selection clears the old display; tagged frames from other notes/rates are
ignored. Spectrum capture stops with the editor closed, and its fixed queue may
drop display frames without blocking audio. Host blocks above 32768 frames skip
analysis only; EQ and playback still process the whole block.

EQ appears once in the existing Apply to All dropdown. Clicking a dot or editing
any EQ parameter selects it; applying EQ copies all eight values as one effect.
The dots are its only editing controls (no arrow-key EQ editing).

## Apply To All

`APPLY TO ALL` sits below the sample-specific effects. An effect dropdown sits
directly to its right, with no value or status text below the button. The dropdown
lists the effects registered in `PluginParameters::sampleSpecificParameters`.
Scalar effects use the parameter's host-facing name; grouped parameters use one
shared effect entry, so EQ appears once. It starts with Gain selected. The collapsed dropdown has no border
or background; only its selected effect name and arrow are visible.

Editing a registered sample-specific effect or clicking its knob or effect-name
label selects that effect in the dropdown. A click selects immediately without
requiring a value change and gives the editor keyboard focus for arrow controls.
The user may also choose an effect directly. Clicking the button reads the selected
effect's current settings from the selected sample and copies them to all sample
groups (all eight frequency/gain values for EQ). Changing samples keeps the effect selection, so the button then uses the
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
