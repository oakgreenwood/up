# OTT reference calibration

The user supplied `abletonNoOTT.wav` and `abletonOTT.wav`: stereo, 48 kHz,
24-bit PCM, 163405 frames (3.40427 seconds) each. Both source files were read
without normalization or modification. This comparison calibrates the global
OTT's dynamics; it does not establish a waveform-identical Ableton clone.

The user subsequently reported a -8 dB setting on the Ableton OTT setup,
interpreted here as output attenuation; whether it is the device's master
Output or a following gain stage has not been confirmed. Calibration targeted
the exported WAV directly, so any attenuation already present in that file
is included in the fitted makeup. Do not apply another -8 dB correction to
the current calibration. Our requested -7 dB master trim is retained; the
fitted band makeup is not an independently measured Ableton band-output setting.
Absolute peak/RMS comparisons below include output gain, so peak differences
alone do not measure compression strength. A uniform output trim does not
change the contrast between transients and quiet detail.

The user confirmed that `ourOTT.wav` is the plugin's output **before** the
latest reference-based DSP changes. It has the same format and duration. Its peak is
-0.02 dBFS and whole-file RMS is -20.29 dBFS. Relative to the Ableton render,
the median output in the two quietest dry-window groups below is 13.04 and
10.39 dB lower, while its peak is 7.03 dB higher. This confirms the reported
quiet-detail/transient imbalance, although the peak difference alone also
depends on output gain. Its Amount and complete export gain chain were not
established, so it is distinct from the controlled previous DSP render below;
it was not used to fit the revised coefficients.

## Quiet detail relative to loud sections

To compare dynamics without confusing them with constant output attenuation,
subtract each file's own median level in the loud sections from its quiet-section
levels. Sections are the same non-overlapping 10 ms windows in each file; loud
sections have a dry-file RMS of -20 to 0 dBFS. Each table value is the median
output level for the indicated dry-window group minus that file's loud-section
median. More negative values mean quiet detail sits further below loud sections.
This analysis does not modify or normalize the WAV files.

| Dry-window range, dBFS | Dry | Ableton | Supplied `ourOTT.wav` before changes | Revised isolated DSP |
| --- | ---: | ---: | ---: | ---: |
| -100 to -65 | -61.63 dB | -29.65 dB | -46.12 dB | -30.39 dB |
| -65 to -50 | -45.46 dB | -24.74 dB | -38.56 dB | -23.20 dB |
| -50 to -35 | -32.40 dB | -19.32 dB | -27.94 dB | -19.95 dB |

In the two quietest groups, the supplied before-change render leaves detail
13.82 to 16.47 dB further below loud sections than Ableton does. The revised
isolated C++ render is within 0.74 to 1.54 dB of Ableton on the same measure.
These relative-level differences are unaffected by a constant -8 dB output
trim. They support the detector/recovery changes already made; they do not
establish a complete host-render match or require another overall gain cut.

## Changes supported by the comparison

- Replace the amplitude follower with a stereo-linked RMS power follower.
- Compute the gain from the current frame instead of retaining/interpolating
  a target for 16 frames. A controlled analysis with only this change reduced
  the old processor's peak from -2.73 to -11.76 dBFS, while its quiet levels
  remained too low. The target hold was a significant source of excess peaks.
- Calibrate RMS release to `baseRelease * 0.65 * 0.15`, allowing quiet tails
  to recover. RMS attack remains `baseAttack * 0.65`.
- Calibrate low/mid/high makeup to +12.5/+7.75/+11 dB. These are fitted values;
  the earlier assumption of zero band makeup did not match the renders.
- Retain the supplied thresholds, ratios, 88 Hz / 2.5 kHz crossovers,
  +5.2 dB input and requested -7 dB master trim. Amount scales the combined
  gain in dB and remains an exact normal-signal bypass at zero.

## Actual C++ renders

An isolated harness compiled `Source/Effects/OttProcessor.cpp` and linked the
project's existing JUCE objects on macOS arm64. It processed the supplied dry
file at Amount 100%, with 257-frame blocks and approximately one second of
preceding digital silence. Both the baseline and final production code were
rendered with the same harness. The numbers below are from those C++ renders,
not just a simulated model.

| Measurement | Ableton reference | Previous isolated DSP | Revised isolated DSP |
| --- | ---: | ---: | ---: |
| Sample peak, dBFS | -7.05 | -2.73 | -7.84 |
| Whole-file RMS, dBFS | -26.10 | -34.67 | -27.91 |
| RMS-level error over 10 ms windows, dB | — | 18.35 | 3.73 |
| Level error on sections excluded from coefficient fitting, dB | — | 17.40 | 3.82 |

The short-window error is the root-mean-square difference of stereo RMS levels
in dB, using non-overlapping 480-frame windows and excluding windows whose dry
level is below -90 dBFS. Coefficient fitting used a -85 dBFS threshold and
alternating 0.4-second sections; the intervening sections supplied the reported
held-out result. Several detector families were compared on the same reference,
so this is within-clip checking, not an independent collection of validation
recordings. No loudness normalization or fitted overall gain is applied to the
reported C++ results.

Quiet-detail levels are grouped by the dry file's 10 ms RMS. Each cell is the
median output RMS for windows in that group:

| Dry-window range, dBFS | Ableton output | Previous OTT output | Revised OTT output |
| --- | ---: | ---: | ---: |
| -100 to -65 | -50.32 | -77.35 | -51.88 |
| -65 to -50 | -45.42 | -66.01 | -44.69 |
| -50 to -35 | -40.00 | -58.34 | -41.45 |
| -35 to -20 | -32.68 | -45.99 | -34.30 |
| -20 to 0 | -20.68 | -31.24 | -21.49 |

## Focused verification

The actual processor passed an isolated C++ harness checking:

- Exact agreement between a continuous render and a sequence of 0, 1, 3, 16,
  257, 4096, 7 and 511-frame blocks.
- Zero-Amount bypass, silence, stereo equality and opposite-polarity symmetry.
- Mono/stereo/no-channel buffers; sample rates from 1 kHz to 768 kHz;
  preparation with invalid rates; repeated preparation.
- Finite processing/recovery with NaN, infinity and maximum finite float input,
  and valid, out-of-range and non-finite Amount targets.
- No C++ `new`/`delete` calls during guarded `process` calls. This is not a
  complete malloc/OS-allocation interposition test; allocation-free behavior
  was also checked in the source and JUCE dependencies.

A local timing sample, including buffer refill and linked Debug JUCE filter
calls, processed 21.33 seconds of stereo audio in 0.308 seconds. This indicates
the per-frame log/power cost is practical on this machine, not a worst-case
realtime guarantee. Full VST3/AU builds, host validation and Windows/x86_64 tests
were not run. See the [realtime audit](realtime-audio-audit-ott.md) for existing
plugin-wide blockers.

The revised output is still about 1.8 dB lower in whole-file RMS than the
reference, and individual short windows differ. Matching these dynamics on one
loop does not guarantee identical behavior on other material or at every Amount.
The detector, filter phase and knee behavior remain this plugin's implementation.
