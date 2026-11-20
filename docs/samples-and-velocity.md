# Samples And Velocity

Read for sample loading/naming, MIDI mapping, velocity groups/gain, or variations.
For transient JSON or note-off behavior, also read
[playback, warp, and transients](playback-warp-and-transients.md).

## Loading And Naming

`juce_add_binary_data` embeds `samples/*.wav`; the CMake glob excludes `*.wav.asd`.
The parser strips BinaryData's `samples_` prefix, then accepts a numeric note
index `N` with optional `vX` (velocity group), `nY` (variation), and `pZ` (pitch
slot), e.g. `1_v2_n3_p1.wav`. Each omitted optional index defaults to `1`.
An optional final `_u` or `_d` token selects an alternating playback direction,
e.g. `26_v2_n3_u.wav` or `26_v2_n3_p2_d.wav`. Direction tokens are case-insensitive.
Duplicate `v`/`n`/`p` tokens, non-positive values, unknown tokens, or repeated /
non-final direction tokens skip the sample.

Pitch slots and directional pairs expand the note map before MIDI assignment.
Each `(noteIndex, pitchIndex)` with any direction suffix reserves two adjacent
notes: the first always requests `_u`, the second always requests `_d`. Ordinary
slots reserve one. Later numbered groups shift by the inserted slots, preserving
numeric gaps and existing pitch-slot order. `midiNote = 48 + mappedNoteIndex - 1`
continues to identify the group's first note. A directional pair extending past
MIDI 127 is omitted entirely. Existing MIDI clips may need remapping.

Playback/editing uses one group per `(noteIndex, pitchIndex)`, ignoring
velocity/variation/direction. The UI grid combines all pitch groups with the
same `noteIndex` into one cell and displays all mapped MIDI notes, including
up/down pairs. `1_v1_n2` and `1_v1_n3` share a cell; `4_v1_n1_p1` and
`4_v1_n1_p2` also share a cell but retain independent editing groups. The SAMPLE
tab's start/end trim belongs to the selected pitch group. The start maps onto each layer/variation's own length; the end
removes the same amount of time from each source's end. Its waveform
uses the lowest numbered variation in the velocity layer selected by MIDI
velocity 64, or the nearest populated layer if that one is empty.

`getEmbeddedSampleGroups()` shares the loader's resource-name collection and
pitch-slot mapping to inventory numbered noise-trigger choices before APVTS is
constructed. It does not decode audio. Noise trigger selection uses the group's
mapped MIDI root note and therefore includes all of its velocity layers,
variations and playing voices. The inventory stays immutable during rendering.
Each loaded sound also owns an immutable noise-trigger table: one start hit for
one-shots, JSON markers for loops/warp sources, and prepared source peak values.
The noise's 80/20 strength rule combines these peaks with the existing velocity,
sample Gain and ADSR multipliers; it does not change percussion velocity gain.
See [noise modulation](playback-warp-and-transients.md#noise-pump--follow).

## Velocity Inventory And Selection

Current `samples/*.wav` inventory:

The warp/loop flags and optional original BPM come from each sample's transient
JSON. Every bundled `warp=true` source—sample groups 13, 20, and 22–25—
declares `"bpm": 153`.

| Note index | MIDI note | Loaded velocity groups and variations |
| --- | ---: | --- |
| 1 | 48 | `v1:n1-n4`, `v2:n1-n4`, `v3:n1-n4`, `v4:n1-n4` |
| 2 | 49 | `v1:n1-n3`, `v2:n1-n2`, `v3:n1-n2` |
| 3 | 50 | `v1:n1-n3`, `v2:n1-n3` |
| 4 | 51 | `v1:n1-n3`, `v2:n1-n3` |
| 5 | 52 | `v1:n1-n4` |
| 6 | 53 | `v1:n1-n3` |
| 7 | 54 | `v1:n1-n3`, `v2:n1-n3`, `v3:n1-n3`, `v4:n1-n3` |
| 8 | 55 | `v1:n1-n3` |
| 9 | 56 | `v1:n1-n4`, `v2:n1-n3`, `v3:n1-n3` |
| 10-25 | 57-72 | one default sample each: `v1:n1` |
| 26 | 73 (`u`), 74 (`d`) | `v1:n1-n4`, `v2:n1-n4`, `v3:n1-n4`, each with `_u` and `_d` |

Per MIDI note, `groupCount` is the highest velocity-group index, not the count
of non-empty groups. If a selected group is empty, search outward by distance,
checking the higher group before the lower group at each distance.

Choose a random variation within the resolved velocity layer and direction.
With multiple variations, a repeat of that pool's last variation has a 65%
chance to reroll to a different one. Ordinary samples retain their existing
selection behavior.

## Alternating Directions

Directional MIDI notes select fixed directions, independently of playback
history: repeating the first note repeatedly requests `_u`; repeating the second
requests `_d`. Both notes share one selector entry, waveform, effects/trim cache
and noise-trigger identity, keyed by the first note. Different pitch slots remain
separate groups. Note-off tracks each actual input note independently. Velocity
and variation selection remain independent for each direction.

SHAKE starts on the group's `_u` note and alternates with its `_d` note. Four
seconds without a stroke, playback/recording starts, selecting another group,
and playing another mapped sample group reset SHAKE to `_u`. Fixed incoming
notes always retain their named direction, including after a reset.

If the selected velocity layer lacks the required direction, search the other
layers by distance, preferring the higher layer on ties. A matching direction
anywhere in the group takes priority over ordinary samples. If that direction
is absent from every layer, use the nearest ordinary pool, then the nearest
opposite-direction pool. The requested MIDI direction remains fixed even when its sample is borrowed. Borrowed samples retain their actual velocity-layer
gain ranges; the incoming velocity is clamped to that range for the gain ramp.

Starting an actual `_d` sample fades all active `_u` voices in the same mapped
group over 40 ms. Starting an actual `_u` fades that group's active `_d` voices
over 130 ms. These rules apply across velocity layers, variations and MIDI
channels; other groups and pitch slots are unaffected. Choking follows the
actual sound's immutable direction tag, including direction fallbacks. Ordinary
fallback sounds do not schedule this fade.

The fade begins when the new opposite-direction hit reaches the output after
the existing voice-effect latency. The duration belongs to the outgoing sound:
40 ms for up, 130 ms for down. A quadratic ease-out multiplies the final voice
output, including effect tails, from unity to exact zero, then clears the voice.
Further hits do not postpone a scheduled fade. Directional voice stealing
prefers a voice other than the group's opposite-direction voices so their fades
can complete; if every voice is a matching opposite-direction voice, the existing
hard steal remains necessary within the fixed voice count.

The loader builds separate ordinary/up/down variation pools and their scalar
repeat histories before rendering. Note selection only reads existing storage
and updates scalar history; it never grows a container or releases sample
ownership. See the [direction playback audit](realtime-audio-audit-sample-directions.md)
for the inspected path and existing warp fallback blocker.

### Cursor-Generated Hits

The immutable group inventory records whether any successfully loaded sound has
a direction suffix. The sample editor uses this flag to enable `SHAKE` without
reading mutable synthesiser state. Gestures use the
selected group's two adjacent MIDI notes. TRACK MIDI selects the route: on sends through
`ULICHPERC MIDI`, with returned DAW MIDI playing/capturing the sound; off uses
the existing bounded local queue for internal playback/capture.
The gesture tracks alternating stroke direction and restores its previous state
if sending fails. Transport resets still invalidate the gesture generation, but
routing and the expected-note input check do not depend on recording state.
Only TRACK MIDI mode arms the missing-input warning; internal audition stays quiet.

The export cache captures local/returned SHAKE notes and other incoming MIDI notes for
mapped samples, retaining velocity and MIDI channel. Its four-second take split
applies only while stopped; the existing up/down playback timeout above still
applies independently during all transport modes. See [captured MIDI](ui.md#captured-midi)
for transport boundaries and live copy/drag snapshots. Incoming capture does not
add incoming notes to the generated plugin MIDI output.

Generated local/helper notes use channel 1 and a 30 ms gate, shortened before an
overlapping retrigger. Ordinary one-shots still ignore note-off. Warped/looping
sounds retain their existing incoming MIDI note-off semantics. Returned notes
feed audio, MIDI activity, noise triggers and capture; they are not echoed to
plugin MIDI output. Local mode retains its supported generated MIDI output.
Recording/replaying notes retains random variation policy.
See [shake UI](ui.md#cursor-shake-mode) and the
[virtual MIDI audit](realtime-audio-audit-virtual-midi.md).

### Touchpad-Generated Hits

The numbered pad uses its lowest loaded pitch slot as the primary note for
Touchpad Live Play. When a physical contact is available, a touch outside the
centred 45% by 45% rectangle selects that pad's `p2` mapped MIDI note if loaded.
Each pitch keeps its own velocity layers, variations and sample-specific
settings. Both share the generated-note gate, local MIDI output and take capture
used by cursor shake. See [Touchpad Live Play](ui.md#touchpad-live-play).

### Microphone-Generated Hits

INPUT CONTROL plays the non-directional pad's primary mapped MIDI note. At a
peak of -35 dBFS or above, the detector measures the maximum absolute input
amplitude in a 5 ms window, across both channels if stereo. It maps peak level
linearly in dB from velocity 1 at -35 dBFS to 127 at -5 dBFS, with louder
signals capped at 127. Onsets are at least 30 ms apart, and the input must
stay below -38 dBFS for 12 ms before it rearms. To reduce retriggering from
speaker bleed, a peak at least 4 dB quieter than the last accepted hit is
suppressed if it arrives 210–260 ms later. Hits use the ordinary velocity
layers and variations.
TRACK MIDI determines whether the hit uses the local gate, take capture and
supported plugin MIDI output or is sent through the DAW's MIDI input.

## Velocity Ranges

Convert JUCE velocity once at note-on:

```cpp
midiVelocity = clamp(round(velocity * 127.0f), 1, 127);
```

| Group count | Group 1 | Group 2 | Group 3 | Group 4 |
| ---: | --- | --- | --- | --- |
| 1 | 1-127 | - | - | - |
| 2 | 1-63 | 64-127 | - | - |
| 3 | 1-42 | 43-85 | 86-127 | - |
| 4 | 1-31 | 32-63 | 64-100 | 101-127 |

Four groups use the special ranges above. Other counts use
`ceil(groupIndex * 128 / groupCount)`, clamped to MIDI velocity `1..127`.

## Velocity Gain

Select one group and variation without crossfading. `PercussionVoice::startNote`
computes the within-group gain ramp. Render multiplies `env * velocityGain` by
the independently smoothed sample-specific Gain multiplier (-20..20 dB, default
0 dB); the velocity calculation below remains unchanged:

```cpp
t = groupMax > groupMin
      ? clamp((midiVelocity - groupMin) / (groupMax - groupMin), 0.0f, 1.0f)
      : 0.0f;

groupSpanDb = 20.0f / groupCount;
velocityGainDb = (-0.5f * groupSpanDb) + (t * groupSpanDb) + 4.0f;
velocityGain = Decibels::decibelsToGain(velocityGainDb);
```

`velocityGroupIndex` does not directly affect the formula. Every group uses the
same ramp; gain resets at group boundaries as `t` returns to `0`.

| Group count | dB span per selected group | Gain at group min | Gain at group midpoint | Gain at group max |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 20.00 dB | -6.00 dB / 0.50x | +4.00 dB / 1.58x | +14.00 dB / 5.01x |
| 2 | 10.00 dB | -1.00 dB / 0.89x | +4.00 dB / 1.58x | +9.00 dB / 2.82x |
| 3 | 6.67 dB | +0.67 dB / 1.08x | +4.00 dB / 1.58x | +7.33 dB / 2.33x |
| 4 | 5.00 dB | +1.50 dB / 1.19x | +4.00 dB / 1.58x | +6.50 dB / 2.11x |

Boundary examples:

- 4 groups: velocity `31` ends group 1 at `+6.50 dB`; `32` starts group 2 at `+1.50 dB`.
- 3 groups: velocity `42` ends at `+7.33 dB`; `43` starts the next group at `+0.67 dB`.
- 2 groups: velocity `63` ends at `+9.00 dB`; `64` starts the next group at `-1.00 dB`.
