# Parameters And State

Read for parameter definitions, automation, serialization, sample-specific values,
or audio-thread publication. See [UI](ui.md) for selector appearance/activity and
[playback, warp, and transients](playback-warp-and-transients.md) for pitch/punch
behavior or warp caches.

## Host Parameters

APVTS parameters are serialized with `parameters.copyState()` and restored with
`parameters.replaceState(...)`.

| Parameter ID | Display name | Type/range | Default | Main use |
| --- | --- | --- | ---: | --- |
| `rzhavchina` | `Rzhavchina` | float `0..1` | `0` | Bit depth and sample-rate reduction in `processBlock` |
| `sustainShorten` | `Pomyatost` | float `0..1` | `0` | Sustain/tail shortening in voices |
| `warpEnabled` | `Tempo sync (legacy)` | bool | `true` | Inert legacy host slot; warp timing is controlled per group |
| `samplePunch` | `Punch` | float `0..1`, `0.01` step | `0` | Sample-specific transient volume boost; editor displays `0%..100%` |
| `samplePitchSemitones` | `Pitch` | float `-12..12` semitones, `0.1` step | `0` | Sample-specific pitch offset for the selected sample group |
| `samplePitchPreserveLength` | `Pitch keep length` | bool | `false` | Inert legacy host parameter; no playback effect |
| `sampleGainDb` | `Gain` | float `-20..20` dB, `0.1` step | `0` | Sample-specific volume, smoothed over 10 ms |
| `sampleFormantSemitones` | `Unused (legacy)` | float `-12..12` semitones | `0` | Inert former LPC option; no playback effect |
| `sampleFormant3Semitones` | `Formant` | float `-12..12` semitones, `0.1` step | `0` | Additional formant shift using pitch-synchronous grains |
| `sampleMonoAmount` | `Mono` | float `0..1`, `0.01` step | `0` | Sample-specific stereo-to-mono blend after Formant; editor displays `0%..100%` |
| `samplePan` | `Panorama` | float `-50..50`, `1` step | `0` | Sample-specific stereo positioning after Mono; editor displays `50L..C..50R` |
| `sampleAttackMs` | `Sample attack` | float `0..2000` ms, `1` step | `0` | Per-group voice attack |
| `sampleReleaseMs` | `Sample release` | float `0..2000` ms, `1` step | `60` | Per-group source-end fade and key-held note-off release |
| `sampleLoopBpm` | `Sample target BPM` | float `0..300` BPM, `1` step | `0` | Per-group target for warp-enabled samples; zero follows project BPM |
| `sampleLoopTapeMode` | `Sample speed and pitch` | bool | `false` | Per-group direct playback when true; Rubber Band preserves pitch when false |
| `ottAmount` | `OTT` | float `0..1`, `0.01` step | `0` | Global three-band upward/downward compression depth after Rzhavchina |

OTT is appended after Panorama, preserving existing host parameter indices. It
is global: selection changes and Apply to All do not read or write it, and it has
no per-note cache or sample-specific registry entry. A constructor-cached APVTS
scalar atomic supplies the audio target once per block. The normal APVTS tree
saves/restores it, including automation with the editor closed. Before replacing
older state without `ottAmount`, restore inserts a zero-valued `PARAM` child so
an already-used instance also returns to bypass instead of retaining its current
OTT value. See [OTT playback](playback-warp-and-transients.md#global-ott).

## Sample Equaliser Parameters

Eight host parameters are appended after OTT, preserving every existing host
index: `sampleEqLowFrequency`, `sampleEqLowGain`, `sampleEqBell1Frequency`,
`sampleEqBell1Gain`, `sampleEqBell2Frequency`, `sampleEqBell2Gain`,
`sampleEqHighFrequency`, `sampleEqHighGain`. Frequency ranges are 20..20000 Hz
(continuous); gain ranges are -15..+15 dB in 0.1 dB steps. Default frequencies
are 100/500/2500/10000 Hz and every gain defaults to zero. Q is a fixed DSP
constant, 1.0, rather than a parameter.

The 20-entry sample-specific registry includes these eight values alongside
Gain, Punch, Pitch, Formant, Mono, Panorama, trim and envelope values. The fixed realtime cache stores
four frequency and four gain atomic arrays indexed by MIDI root note; writes
reject non-finite input, clamp frequencies/gains and snap gains to 0.1 dB.
Selection, host automation with the editor closed, cache mirroring on save and
sample-specific restore use the same registry. Missing values in older state
restore the default flat curve, including when loading into an already-used
instance, through cache rebuilding and selected-parameter synchronization.

Optional `effectId`/`effectName` registry fields group the eight EQ parameters
under one EQ entry in Apply to All. The processor snapshots all member values
from the selected group, then copies them through the existing cache/state
publication path. Publication remains independent per scalar/group, not a
transaction visible atomically to audio. Existing scalar effects still copy only
their own value. UI gesture tracking observes all eight host parameters, while
mapping them to the one EQ menu entry. There are no extra editing controls.

## Background Noise Parameters

`noiseSource` (choice, default first source) and `noiseGainDb` (float, default
silence) are appended after EQ, preserving existing host indices. The gain uses
the finite sentinel -60.1 for the far-left `−∞` position; audible values run
from -60 to +10 dB in 0.1 dB steps. This avoids serializing IEEE infinity.
Both are global APVTS values and use constructor-cached scalar atomics on audio.
No sample-specific registry entry, listener or per-note cache is involved.

Normal APVTS copy/replace saves/restores both parameters, including automation
without the editor. Older states insert the silent gain and first-source defaults
before replacement, even in an already-used instance. State also saves the source
name as `noiseSourceName` and remaps the saved choice when a later build changes
the sorted inventory. A removed source restores silent with the first source
selected. Automation choice indices remain tied to the build's inventory.
Opening/reopening the editor does not reset restored gain. Playback position is
not serialized. Embedded buffers stay immutable during restore; audio follows the restored
scalar targets using its normal fades. Imported buffers use the pinned-slot
publication described below.

Four additional global parameters follow the original noise controls:

| ID | Range | Default |
| --- | --- | --- |
| `noiseModulation` | -1 (Pump)..0 (neutral)..+1 (Follow), 0.01 steps | 0 |
| `noiseTrigger` | All samples, then numbered sample groups | All samples |
| `noiseAttackMs` (inert legacy) | 1..2000 ms, 1 ms steps | 5 ms |
| `noiseReleaseMs` | 1..2000 ms, 1 ms steps | 150 ms |

Time parameters use a skewed range with 100 ms at the midpoint. Active controls
use standard APVTS attachments, cached audio scalar pointers and copy/replace state;
none participates in sample selection or Apply to All. Older state receives
these defaults before replacement, including when loaded into an already-used
instance. The trigger also saves `noiseTriggerNoteIndex`/`noiseTriggerPitchIndex`
to restore the same group across reordered inventories. If that group disappears,
restore selects All samples and returns modulation to neutral. Existing automation
choice indices remain specific to the build's inventory, as with noise sources.
Detector envelopes and noise playheads are not serialized. Trigger changes retain
the current envelopes and filter subsequent hit events. The 80/20 strength rule
and JSON-triggered pulses are unchanged by the simplified controls. Per-voice transient cursors and delayed
hits are runtime-only and reset with playback. Pump's separate sharper attack
and quadratic gain recovery use the fixed Attack and adjustable Release; their phase
and envelope values are also runtime-only. Follow retains its exponential curve.

`noiseAttackMs` and `noiseRiseDb` are now inert legacy host slots. Keeping their
IDs/order preserves subsequent parameter indices and old state parsing. Their
new defaults are 5 ms and +12 dB, but the render path always supplies these fixed
constants, ignoring legacy saved values and automation. No editor controls or
audio cached pointers remain for these two parameters. Release remains adjustable
and automatable, with the same 1..2000 ms range and 150 ms default.

Follow boost retains the square-root curve reaching maximum at depth 0.3;
background attenuation and Pump depth now use `sqrt(abs(amount))` over the
full travel. Both remain neutral at zero and reach maximum at the endpoints;
raw depth 0.7 (+/-35 on a -50..+50 scale) gives about 83.7% effective depth.
The existing modulation parameter
range/ID is unchanged. The +15/+50 positions refer to this same 30% fraction of
the positive half, not new parameter units.

Saved noise gain values restore directly, without a range-version marker or
old-silence migration. A saved -40.1 dB now represents audible gain in the new
range. Missing gain still defaults to the current -60.1 silence sentinel.
Host-owned normalized automation may map differently with the expanded range.

`noiseStart` and `noiseEnd` are appended after the envelope controls. They are
continuous normalized global parameters in 0..1, defaulting to 0/1 (the entire
source), saved/restored through APVTS. Older states receive those full-range
defaults even when loaded into an already-trimmed instance. The same relative
range applies when switching noise files; trims are not stored per source.

Audio and waveform display share `BackgroundNoisePlayer::resolveRegion`: start
rounds down to a frame and end rounds up to an exclusive boundary. Non-finite
values fall back to 0/1, and crossing/equal host values safely resolve to at least
one frame, with start authoritative. UI dragging keeps the handles ordered with
a minimum gap of one frame or 1e-6 of the source, whichever is larger. Each drag
brackets a standard host gesture, ending on release, source switch, hiding or
editor destruction. A separate runtime-only scalar gesture counter pauses noise
during dragging and restarts it afterward; it is not an APVTS parameter, is not
saved, and never changes the gain parameter. Audio snapshots gesture state before
reading the constructor-cached start/end atomics.

### Imported noise audio

The source choice reserves 64 stable custom indices after the bundled files.
The parameter ID/layout slot and embedded source indices are unchanged. Host
choice names are `Imported audio 1` through `Imported audio 64`; the editor shows
only populated/pending entries, with their cleaned filenames. Square brackets
and their contents are removed and whitespace collapsed; duplicate drop names
receive numeric suffixes. Each accepted drop reserves a new entry, selects it
and resets the shared start/end to 0/1 via host gestures. Gain/modulation remain
unchanged. The non-parameter state-change notification marks each addition dirty.

The dropdown uses `juce::ParameterAttachment` with stable item IDs instead of
ComboBoxAttachment's item-count normalization, so pending/failed/empty slots and
later additions do not shift selection. Source choice automation still uses a
fixed range; extending the former one-custom-slot range may change pre-existing
host-owned normalized automation lanes. Saved selections remap by stable index.

`noiseUsesImportedSource` and `noiseImportedIndex` preserve custom selection
independently of bundled inventory order. `ImportedNoise` stores one `Source`
child per successful custom entry, with its index, cleaned name and Base64
32-bit float PCM WAV. Every imported file is included even when a bundled source
is selected; original file paths are not needed on reopening. The existing
single-import `ImportedNoise` name/wav format restores into custom index 0.
Gain, shared trim and modulation retain normal APVTS serialization.

Limits are 64 entries, 128 MiB decoded mono/stereo audio per file and 512 MiB
for the current library. Unsupported/oversized files do not replace older audio.
Sparse indices are preserved; failed reservations are reusable. The decoded and
encoded-size limits bound the portable payload, which can still make state large.

Save waits for all pending imports on the non-realtime state thread. The loader
never calls host/UI callbacks or waits for the message thread. Restore cancels
older work, unpublishes the previous library and queues decoding by stable index.
It removes the large imported payload from the live APVTS tree before replaceState.
Old active playback fades out; a restored selected entry plays only when ready.
Missing custom state clears the library; corrupt entries do not publish audio.
Closing/reopening the editor retains the processor's library; reopening a saved
project restores every successfully embedded entry. An unrelated new instance
starts without custom entries.

## Program Metadata

The processor reports one program at index `0`, named `Default`. The nonempty
name satisfies the VST3 validator's program-name requirement. Program selection
and renaming are no-ops; selecting this program does not reset parameter values.

## Sample-Specific State

`AudioPluginAudioProcessor` stores the selected group index and serializes both
that index and its stable `(noteIndex, pitchIndex)` key. Restore by key, with
index fallback for older state. Clicking a selector item changes the selected group immediately. MIDI notes
only remember the last played mapped sample; clicking `EDIT SAMPLES` selects
that sample and synchronizes the host-facing parameters and editor controls.
There is no MIDI selection debounce, and playback does not change the editing
selection. The remembered note is runtime-only and is not serialized.

`SampleSpecificParameterState` stores per-sample values in a `sampleSpecific`
ValueTree child, keyed by `(noteIndex, pitchIndex)` with legacy index fallback.
Its `ValueTree`, `CriticalSection`, string IDs, and linear scans belong only to
UI edits and state save/restore; never read it from audio.

For `samplePitchSemitones`, `samplePunch`, `sampleGainDb`,
`sampleFormant3Semitones`, `sampleMonoAmount`, `samplePan`, `sampleStart`,
`sampleEnd`, `sampleAttackMs`, `sampleReleaseMs`, and the eight EQ parameters:

- Processor helpers read/write the selected group's stored values.
- APVTS slider/button attachments report gestures/changes for host automation
  and undo. Processor parameter listeners update the atomic per-note cache,
  including when the editor is closed. They never access the state tree.
- Selection changes synchronize host-facing parameters to the newly selected
  group's cached values; state restore does the same after rebuilding the cache.
- `SampleSpecificRealtimeCache` is authoritative during use, indexed by the
  loaded group's MIDI note. It stores pitch semitones, the precomputed pitch
  ratio, Punch, Gain in dB with a precomputed linear multiplier, and
  Formant semitones with a precomputed frequency ratio, plus Mono amount and
  Panorama position, normalized trim endpoints and derived end reduction, plus
  the four EQ frequencies/gains. Voices avoid state-tree access and
  pitch exponentiation; the ratio is computed only when the parameter changes.
- `getStateInformation` mirrors all cached groups to `SampleSpecificParameterState`
  before writing that child into `parameters.copyState()`. Restore uses
  `parameters.replaceState(...)` and rebuilds the cache from the saved child.
  Buffers and pending background work are not saved.
- After restoring per-sample values and tempo sync, request a full warp-cache
  startup pass through a scalar generation counter. The worker waits for a fresh
  valid host-BPM callback, then prepares all warp variants at their restored pitch
  even when transport is stopped. Matching caches are reused; superseded renders
  cancel. State restore does not render or wait for the worker. See
  [warp cache startup](playback-warp-and-transients.md#pitch-aware-warp-caches).
- One-shots latch pitch at note start and play the original sample at the
  corresponding speed. No one-shot pitch-preparation worker or cache remains.

The removed Keep length mode retains its original host parameter ID, position,
name and default as an inert compatibility entry, so subsequent parameter indices
(including Gain) stay stable in existing projects. APVTS still saves/restores its
value, but nothing reads it for playback, selection, or Apply to All. Old
sample-specific mode properties may round-trip in the state tree but are ignored.

Sample start/end are appended after all earlier host slots, default to 0/1 for
older projects, and are saved per selected sample group. The waveform sends
ordinary host parameter gestures. The processor caches each group's displayed
sample duration and derives an absolute duration removed from the end from its
normalized end parameter. Voices latch the proportional start and absolute end
duration at note start, resolve them against each variation's length, and keep
at least one frame even for crossed or automated endpoints. Held loop voices read
updated trim atomics at render boundaries, applying a new start at the next
repeat and a new end in the current pass only while it remains ahead of playback.
One-shots keep the note-on range. The trim parameters
are excluded from the Effects tab's Apply to All dropdown.

Sample Attack and Release follow Sample end in the host parameter layout,
preserving earlier parameter indices. Both are sample-specific integers in
0..2000 ms, defaulting to 0 and 60 ms. They use the same per-group
save/restore, selected-control synchronization and host automation as trim, and
are excluded from Apply to All. The voice reads the two atomic cached values
at note start and applies source-time edge fades through `SampleEnvelope`;
Release also supplies the key-held note-off ADSR release. Later edits affect
new notes. The waveform's slanted handles and numeric inputs share these
parameters and standard host gestures for non-warp samples. For `warp=true`
groups, the editor hides the Attack/Release fields and line handles while the
saved and automatable parameter values remain available to playback.

Sample playback position is runtime-only and is not serialized. Each voice
publishes its MIDI note, note-on order and progress through its latched trim
region in one lock-free atomic integer. The editor reads these snapshots to
display the newest active voice in the selected group; an empty snapshot hides
the playhead. No sample buffers or parameter trees cross this audio-to-UI path.

Punch remains stored and published as `0.0..1.0`. Its editor scales the value to
the whole percentage `0%..100%`; typed percentages divide by 100 before the
slider/APVTS gesture. The editor rejects numeric input above 100, decimal values,
and non-percentage characters. Cache writes reject non-finite input, clamp to the
same range, and snap to 0.01 so
restored per-sample values match the displayed percent.

Mono uses the same percentage representation and validation as Punch: an internal
0..1 value in 0.01 steps and whole percentages from 0% to 100% in the editor.
Both percentage inputs reject oversized numeric pastes before integer overflow
can bypass the range check. Mono cache writes reject non-finite values, clamp to
0..1 and snap to 0.01. Missing Mono state defaults to zero. The host parameter is
appended after Formant, preserving every existing host parameter index. Voices
read one scalar atomic per voice render call and smooth live changes over 10 ms,
including effect tails. See [Mono playback](playback-warp-and-transients.md#sample-mono)
and [the Mono realtime audit](realtime-audio-audit-mono.md).

Panorama is appended after Mono, preserving existing host parameter indices.
Its signed -50..50 range has one-unit steps and a zero/centre default, including
when restoring older state. Cache writes reject non-finite values, clamp to the
range and round to an integer. Audio reads one scalar atomic per voice render
call and maps it to -1..1 for 10 ms smoothing. It operates after Mono on stereo
outputs; mono output buses bypass the pan matrix. The editor displays zero as
`C`, negative positions as an unsigned number followed by `L`, and positive
positions followed by `R`. Signed integer input, `C`, and unsigned `L`/`R`
notation share strict range/character validation; see [UI](ui.md#panorama).
See [Panorama playback](playback-warp-and-transients.md#sample-panorama) and
[the Panorama realtime audit](realtime-audio-audit-panorama.md).

Gain defaults to 0 dB for missing values in older state. Its cache rejects non-finite
input and clamps to -20..20 dB; conversion to linear gain occurs only on writes.
Voices read only the linear atomic and smooth live changes, including on existing
hits. The value field renders its unit as `dB`; Pitch and Formant render `st`, and
Punch and Mono render `%`. See [the Gain realtime audit](realtime-audio-audit-gain.md).

Formant is the former PSOLA Formant3 option, renamed in the editor and host.
`PluginParameters::sampleFormantSemitonesId` intentionally retains the serialized
ID `sampleFormant3Semitones` and its existing layout slot, so prior PSOLA values
and host automation continue to control the surviving effect. Its range remains
-12..12 semitones in 0.1-semitone steps, default zero. The per-note cache rejects non-finite writes,
clamps the range and computes `2^(semitones/12)` on writes. One ratio read drives
both PSOLA and its following EQ/saturation stage, with 20 ms smoothing.

The old LPC ID `sampleFormantSemitones` retains an inert host slot named
`Unused (legacy)` to preserve subsequent parameter indices. It has no knob,
realtime cache, listener or Apply to All registration. Its host value and old
per-sample properties may round-trip in state but never affect playback. Old
LPC values are not reinterpreted as PSOLA values. Missing PSOLA values default to
zero. Selection, editor-closed automation, state and Apply to All now use the
registry: Gain, Punch, Pitch, Formant, Mono, Panorama and the eight EQ parameters.

Formant 0 retains natural varispeed formants. The surviving PSOLA version keeps
its existing post-processing: LPC envelope EQ at 95% of the original correction
in dB, followed by asymmetric saturation with a 0..24.84% blend tied to absolute
knob offset. This coloration remains active at nonzero Formant even when pitch
tracking falls back to dry. The separate LPC-only effect is removed from voice
rendering. Negative Formant adds a separate post-effect gain boost, linear in
semitones/dB from 0 dB at zero to +3 dB at -12 semitones, smoothed over 20 ms.
Zero/positive offsets add no boost. This derives from the existing Formant ratio
and does not modify the sample Gain knob or add saved state. See [Formant playback](playback-warp-and-transients.md#formant).

## Apply To All

`APPLY TO ALL` copies the effect selected in the adjacent dropdown to every loaded
sample group, including all velocity layers/variations that share that group's
MIDI note. Settings are read from the selected sample when the button is clicked;
EQ copies all eight values together as an effect.
Other parameters and the selected group stay as they were. The dropdown is built
from `PluginParameters::sampleSpecificParameters`, using each APVTS parameter's
host-facing name, or the registry's shared effect name for grouped parameters.
Gain, Punch, Pitch, Formant, Mono, Panorama and EQ are registered.
The sample trim endpoints use the same persistence and realtime registry but
are not offered in the dropdown.

The editor observes APVTS change gestures for every registered parameter.
Selection changes, control refreshes, ordinary host automation, and state restore
do not change the dropdown selection. An actual UI edit auto-selects its effect.
Clicking a sample-specific knob or its effect-name label also selects the effect
immediately, without requiring a value change or sending a parameter change.
This click clears any previously queued edit selection and gives the editor
keyboard focus for arrow controls. The user may also select another registered
effect directly. Switching groups
retains the effect selection while changing the source value used on the next
click. The selection lasts for the current editor session and is not serialized.
Each new editor starts with Gain selected. The button is disabled when there are
no sample groups or no valid effect selection; beginning a gesture without
changing a value does not count as an edit. Knob drags, wheel/keyboard edits,
double-click resets, committed sample-effect value inputs, and attached
toggles use the same tracking. The value inputs use a slider change gesture only
when a complete committed value changes the parameter; typing, cancellation and
automation refresh do not auto-select an effect.

The dropdown selection is also the editor's arrow-key target. Up/Right adds one
parameter interval and Down/Left subtracts one interval, using a normal slider
gesture and the parameter range's clamping. This is 0.1 dB for Gain, 1% (0.01
internally) for Punch/Mono, 0.1 semitone for Pitch/Formant, and 1 for Panorama.
Selecting an effect in
the dropdown returns keyboard focus to the editor so the next arrow key adjusts
that effect; arrow keys continue to navigate while the dropdown menu is open.

The processor's message-thread-only `applySampleSpecificParameterToAll` normalizes
the value through its APVTS range, writes the registered realtime cache and stored
state for every group, then updates the selected host parameter if needed. It also
marks non-parameter state as changed so hosts can save the batch even when the
selected parameter already matched. Playback reads the existing scalar atomics;
the batch never walks groups or touches `ValueTree` on audio. Updates publish per
group rather than as an indivisible snapshot. Existing warp-cache preparation, live
Punch response, and one-shot note-start pitch latching still apply.

One-shot pitch changes need no background preparation; subsequent hits use the
new value directly. Warp samples retain their existing startup/recent-five cache
policy. See [playback](playback-warp-and-transients.md).

See [the Apply to All realtime audit](realtime-audio-audit-apply-to-all.md) for the
inspection scope and existing render-path blockers.

## Adding Sample-Specific Effects

- Define the APVTS parameter/range/default in `PluginParameters.cpp` and add one
  entry to `PluginParameters::sampleSpecificParameters` (update its array size in
  the header and implementation). Supply allocation-free `noexcept` cache read
  and write callbacks using denormalized float values; convert bool/choice values
  in the callbacks and enforce the effect's valid range. This single registry
  drives recognition, processor listeners, selection synchronization, save/restore,
  UI edit tracking, and Apply to All where `includeInApplyToAll` is true. Do not add effect-specific button logic or
  separate parameter-ID lists.
- Multi-parameter effects may share `effectId` (the representative parameter ID)
  and `effectName` in the same registry so Apply to All copies the whole effect.
- Add the per-note value to `SampleSpecificRealtimeCache`, including initialization
  and any derived DSP values. Prefer preallocated arrays sized to the MIDI-note
  inventory, scalar atomics, or another lock-free handoff. Allocate/resize only
  outside audio, e.g. construction, sample loading, or `prepareToPlay`.
- Use APVTS control attachments so UI edits emit change gestures. Sliders use
  `bindSliderToParameter`; attached buttons and choice controls are tracked via
  the same parameter listeners automatically. Custom controls must emit matching
  `beginChangeGesture`/`endChangeGesture` around edits. Use processor helpers for
  selected-group access; audio uses the cache.
- In `processBlock`, voices, and other realtime paths, never read
  `SampleSpecificParameterState`, `ValueTree`, strings, or locked structures.
  Use the resolved mapped note/group index, not filename parsing or UI-state lookup.
- For expensive DSP updates (e.g. RubberBand pitch/time), use audio-safe smoothing,
  debounce, crossfade, or offline caching suited to sound and CPU cost; do not
  apply raw UI-rate updates.
