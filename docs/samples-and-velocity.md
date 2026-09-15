# Samples And Velocity

Read for sample loading/naming, MIDI mapping, velocity groups/gain, or variations.
For transient JSON or note-off behavior, also read
[playback, warp, and transients](playback-warp-and-transients.md).

## Loading And Naming

`juce_add_binary_data` embeds `samples/*.wav`; the CMake glob excludes `*.wav.asd`.
The parser strips BinaryData's `samples_` prefix, then accepts a numeric note
index `N` with optional `vX` (velocity group), `nY` (variation), and `pZ` (pitch
slot), e.g. `1_v2_n3_p1.wav`. Each omitted optional index defaults to `1`.
Duplicate `v`/`n`/`p` tokens, non-positive values, or unknown tokens skip the sample.

Pitch slots expand the note map before MIDI assignment:
`midiNote = 48 + mappedNoteIndex - 1`.
The selector uses the same map: one group per `(noteIndex, pitchIndex)`, ignoring
velocity/variation. `1_v1_n2` and `1_v1_n3` share an item; `4_v1_n1_p1` and
`4_v1_n1_p2` have separate items.

## Velocity Inventory And Selection

Current `samples/*.wav` inventory:

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

Per MIDI note, `groupCount` is the highest velocity-group index, not the count
of non-empty groups. If a selected group is empty, search outward by distance,
checking the higher group before the lower group at each distance.

Choose a random variation within the resolved group. With multiple variations,
a repeat of that note/group's last variation has a 65% chance to reroll to a
different one.

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
