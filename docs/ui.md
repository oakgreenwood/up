# UI

Read for selector layout or MIDI activity. For selected-group identity, parameter
attachments, automation, or persistence, see [parameters and state](parameters-and-state.md).
Audio/message-thread activity handoff changes require the
[realtime audio audit](../.agents/skills/realtime-audio-audit/SKILL.md).

## Background Noise

The global page groups noise in a `NOISE RYTHM` panel, following the supplied
reference's placement, with a transparent panel over the editor background, a
black heading/border, #6B6D68 uppercase captions, black waveform/text and
#8E8B8B slider tracks.
Inside the frame, controls run vertically: SOURCE dropdown, waveform, TRIGGER
dropdown, VOLUME, then PUMP/FOLLOW. A single Release knob sits centered along
the bottom with an editable integer-ms readout. Attack and Rise controls have
been removed. The other global controls remain to its left. The former global
Tempo sync button is removed; loop tempo and playback mode are in the SAMPLE tab.
The panel is two-thirds of its former width (399 px in the 900 px editor),
with its left edge and height unchanged. Dropdowns, waveform and slider tracks
use the narrower bounds; the centered Release knob remains 47x47 px, preserving
its image proportions. Text sizes are unchanged.

Source starts with an `Upload file` action, then lists every embedded `noise/*.wav` without its extension, followed by
one entry for each custom noise. Empty reserved host slots are omitted. Both dropdowns
use bold black text, simple chevrons and no box/background; long source names
truncate with an ellipsis. Trigger defaults to All samples and includes each
numbered sample group. Volume fills its thick track from the left; modulation
fills from centre toward the thumb, with PUMP/FOLLOW captions at opposite ends.
Both sliders have black rounded vertical handles.

`Upload file` opens an asynchronous native file picker with multi-selection and
an extension filter from the same registered audio decoders used by noise imports.
It adds files through the existing import queue. The action is not a source or
host parameter choice: the default and current selection stay unchanged when
opening or cancelling the picker. It stays first when the custom list refreshes.
Closing the editor cancels its pending picker. Selected files still undergo the
normal decode and size checks; an extension alone does not guarantee valid audio.

`NoiseWaveformEditor` draws the full source from 1024 immutable min/max bins,
combining stereo peaks without cancellation and scaling by the source peak for
visibility. A black frame contains the waveform; excluded audio is shaded with
20% black. Drag either endpoint handle to change the loop region. Clicking
elsewhere in the waveform moves the nearest handle. Grabbing either handle
smoothly stops noise once; playback stays stopped throughout the drag and
restarts from the final selected start after release. Its gain setting is
unchanged, so a muted source stays muted. Canceling a drag by hiding/closing the
editor, switching source or replacing imported audio also releases the temporary
stop. Handles stay ordered, with
extra hit space around the full-file endpoints. Even a host-collapsed region's
start remains reachable from its left side. Source changes terminate a drag;
a 30 Hz message-thread timer follows source/trim automation without touching
sample data. Bundled peak summaries are built during processor construction; imported peaks
are built on the background loader and copied into the UI on publication.

A single one-pixel `#CDD0C3` vertical playhead line follows active noise playback
at the existing 30 Hz UI refresh rate. It is shown only on the currently playing
source and within the displayed trim range. Stopping/muting noise, completing
the trim-stop fade, bypass and release clear it. The line resumes when playback
restarts. Pump/Follow modulation does not stop the underlying loop or its cursor.
The line has no marker, label or separate interaction.

Drop one or several supported audio files anywhere over the plugin editor to add custom
noises without replacing earlier entries. Imports queue in the background; the
last accepted file is selected and trim resets to the full file. Previously
added sounds remain selectable. Up to 64 custom entries are supported. Imported
display names remove square brackets and their contents
(including multiple/nested groups), then trim/collapse whitespace. For example,
`Street [120 BPM] [take 2].wav` displays as `Street`; an empty result displays as
`Imported audio`. Repeated names get numeric suffixes. This cleaned name is saved
and is also applied to older
restored imports. The actual file on disk is not renamed.
Gain remains unchanged, so a muted instance stays muted. WAV,
AIFF, FLAC, Ogg/Vorbis and MP3 are supported, with additional codecs supplied by
the platform. Imported audio is embedded in saved plugin state and survives
moving/deleting the original file. Further drops append entries; bundled and
previous custom choices remain available. Decode failures show an error dialog. The
128 MiB per-file and 512 MiB total-library limits apply to decoded mono/stereo
float audio, not compressed file size. Failed files leave existing entries intact.
While supported files hover anywhere over the editor, the entire noise panel
is covered by an opaque `#A5A4A4` overlay with centred black `Drop noise` text.
The editor owns the drop target, so moving across child controls keeps the
overlay active. It clears on exit, drop or editor hiding. Drops also work while
Edit Samples is open; the overlay appears at the noise panel's usual bounds.

Volume defaults fully left to `−∞ dB` (off); its next step is -60 dB, then
0.1 dB steps through +10 dB. Hover/drag shows gain in the popup; double-click
returns to silence. Raising gain starts the selected region independently of
MIDI and transport. Returning fully left fades out and stops playback.

Pump/Follow defaults to zero/centre. Left ducks noise, reaching complete ducking
on hits after the attack. Right lowers the untriggered background throughout
its travel. Both sides use square-root depth for a stronger response near centre
and a gentler approach to full effect: +/-35 on the -50..+50 scale corresponds
to about 84% effective depth. Slider position/fill still represents the raw saved
amount. The separate Follow trigger boost
uses a square-root response, reaching its ceiling 30% of the way from centre to
full right (+15 on a -50..+50 scale); full right reaches silence between hits
after release. Double-click returns to neutral. Release uses the existing image
knob and editable integer-ms readout, ranging 1..2000 ms with 150 ms default and
double-click reset. Attack is fixed at 5 ms and Follow Rise at +12 dB, including
for older projects; neither has a knob or value readout. Release returns noise
in Pump and fades it out in Follow.

The whole panel hides while Edit Samples is open, ending any active waveform
gesture. Noise parameters remain global, saved/automatable and excluded from
Apply to All. See [trim playback](playback-warp-and-transients.md#background-noise)
for the 2 ms edit fades and region-boundary crossfade.

## Sample Effects Modal

Sample-specific controls are hidden from the main editor until the centred
`EDIT SAMPLES` button above the sample-group sticks is clicked. The modal uses
an opaque `#D9D9D9` background, spans the editor width inside the existing
12-pixel padding, is anchored to the bottom of that padded area, and occupies
70% of the editor height. Two tabs, `EFFECTS` and `SAMPLE`, sit on one row
centred at the top. `EFFECTS` is initially selected and contains Pitch, Formant, Mono,
Punch, the sample equaliser, and Apply to All. `SAMPLE` contains Gain, Panorama,
and a sample waveform with start/end trim handles. A column on its right shows
START, ATTACK, END and RELEASE in milliseconds, with grey captions and black
editable values. Each accepts typing or keyboard arrows in 1 ms steps. START
and END use the representative source's timeline; the waveform handles and
fields stay synchronized. Switching tabs preserves values and the selected
sample group. For groups whose representative sample has `warp=true`, these
four value fields are hidden. Their trim handles remain available on the
waveform, while the Attack/Release line handles and fade shading are hidden.

For `warp=true, loop=false` groups, a `BEAT SNAP` button appears below the
waveform. It starts off and toggles transient snapping on or off. For
`warp=true, loop=true` groups, a `BEAT SNAP` label and snap-target dropdown
appear instead. `OFF` is the first dropdown option; `TRANSIENTS` is selected
by default. The button and dropdown retain separate choices across sample/tab
changes for the current editor session. The dropdown also offers `1/4`, `1/8`,
and `1/16` note grids,
anchored at the source start and calculated from the representative source's
original BPM, independent of target/project tempo. When snapping is enabled,
small downward triangles in `#8E8B8B` appear immediately above the waveform's
top border at the selected transient or grid points. Dragging or clicking to
move either trim handle selects the nearest valid target, or the actual
beginning/end of the sample for its respective handle.
The handles stay at least one sample frame apart. Sample boundaries remain
available even without a metadata transient or grid point there, but do not get
triangles unless a metadata transient is present.
Changing either control leaves existing trim values intact until edited. Snapped trims
use the existing saved/automatable start/end parameters; the editor-only controls
do not constrain host automation or add a saved parameter. Non-warp groups
retain free trimming even when a snap target is selected for another group.

For a sample group whose representative source has `warp=true` or `loop=true`,
the SAMPLE tab also shows a target BPM input, TEMPO SYNC button, PRESERVE
PITCH / SPEED + PITCH mode button, and the original source BPM in `#8E8B8B`
text. The displayed original BPM comes from JSON when present, otherwise from
the 153 BPM source fallback. The default target follows the project's changing
tempo. TEMPO SYNC is fully opaque while following the project and 60% opaque
when off; switching it off sets the target to the original source BPM, so later
project tempo changes do not change playback speed. Switching it on resumes
following the project. Entering a number also fixes the target for that group
and shows the button at 60% opacity. This mode follows the existing saved
per-group target BPM parameter (zero means synced). Samples without warp or loop
metadata hide these controls.

The `SAMPLE` waveform shows the first numbered variation from the velocity
layer that contains MIDI velocity 64. If that layer is empty, it uses the
nearest populated layer.
It uses 1024 min/max bins per channel prepared while embedded samples load,
without scanning source audio in the editor. Stereo sources have two stacked
lanes; mono sources have one. The waveform takes about one third of the modal
width with an approximately 3:1 plot, and the waveform/value group is centred.
For `warp=true` groups, the waveform width is 1.6 times the ordinary width,
with its height unchanged; it is centred by itself because the value column
is hidden.
The full waveform is drawn inside a black frame; the excluded parts are shaded.
Drag either handle to set normalized
start/end positions. Clicking elsewhere moves the nearest handle, and the
handles stay at least one displayed sample frame apart. Changing groups,
switching tabs, closing the modal, or hiding the editor ends a trim gesture.
The trim applies to every velocity layer and variation in the group. The start
handle uses each source's own length. The end handle removes the same duration
from the end of every source, so different original lengths remain different.
An already sounding one-shot keeps the range it had at note start. A held loop
uses a changed start from its next repeat; it uses a changed end in the current
pass if that end is still ahead of its playback position, or from the next
repeat otherwise.

A one-pixel `#CDD0C3` line shows playback in the SAMPLE waveform at the same
30 Hz refresh rate as the noise playhead. For overlapping hits, it follows the
most recently started voice still playing in the selected group. Each voice's
position within its own trimmed region is mapped onto the displayed variation's
trim range, so variations of different lengths still show their progress. The
line disappears when that source finishes, the selected group is idle, or the
plugin is bypassed.

Attack and Release belong to the selected sample group. Both accept 0..2000 ms;
Attack defaults to 0 ms and Release to 60 ms. For non-warp groups, slanted line
handles above the plot set the attack's end and release's beginning. Drag right to lengthen
Attack, or left to lengthen Release. Grey triangles show both source-time
fades, anchored to the trimmed start/end; changing trim moves the fade handles
with those boundaries. Fades clamp to the selected region's length, and may
overlap. The same parameter gestures drive the line handles and numeric fields.
Release now also fades the end of one-shot samples; key-held samples retain
their note-off fade.

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

The `Gain` knob sits on the `SAMPLE` tab beside Panorama, using the same image
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

The Mono knob sits to the right of Formant on `EFFECTS`, with the same image knob
and editable percentage input as Punch. It ranges from 0% (original stereo) to
100% (summed mono), in 1% steps, defaults to 0%, and double-click resets it to 0%.

Typing `50` or `50%` commits 50%. The shared percentage validator accepts ASCII
digits and one optional trailing `%`, rejects signs, decimals, whitespace,
letters, repeated/misplaced `%` and values above 100, including oversized pasted
integers without overflow. Empty/incomplete input restores the current value on
commit. Enter, Tab/focus loss, Escape, selection refresh and automation during
editing follow the existing numeric input behavior. Arrow edits change by 1%.
The attachment and parameter registry provide automation, per-sample saved state,
Apply to All and automatic effect-dropdown selection on an actual UI edit.

## Panorama

Panorama sits to the right of Gain on `SAMPLE`. It is sample-specific, ranges
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
lists the effects marked for Apply to All in
`PluginParameters::sampleSpecificParameters`.
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
change gesture. Holding Shift with an arrow multiplies the usual value step by
ten for focused knobs, sliders, numeric value fields (including while editing),
and the selected effect. The same shortcut works on macOS, Windows, and Linux; other
modifier combinations retain their normal key behavior.

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

Every editable numeric value and rotary knob supports dragging in either
direction: up or right increases, down or left decreases. After the pointer
moves three pixels, the larger horizontal or vertical displacement chooses
the axis for that drag; the axis then stays fixed until release. Numeric values
use their linked slider's value curve and 150-pixel full-range sensitivity.
A numeric drag sends one host change gesture, while a click still opens text
entry. This applies to sample effects, sample timing, loop BPM, and noise
Release. Selection changes or hiding a field end an active numeric drag.

## Formant

The single Formant knob sits immediately right of Pitch on `EFFECTS`, followed
by Mono and Punch. It is the former PSOLA Formant3 option, retaining
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
Each mapped note-on remembers the last played sample's MIDI note in a scalar
atomic, including while the editor is closed. Unmapped notes and note-offs do
not replace it. MIDI activity never changes the editing selection.
Clicking `EDIT SAMPLES` reads that note directly and selects its sample before
opening the modal, without polling delay or selection throttling/debounce.
Until a mapped sample has played, opening the modal keeps the current selection.
Manual selection remains immediate and stays selected during subsequent MIDI
playback; reopening the modal selects the latest played sample again.
