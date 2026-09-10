# DSP Checks

Inspect reachable DSP/playback for CPU spikes, NaN/Inf, and invalid memory access:

- Zero/tiny divisors, invalid square-root/log inputs, exponential overflow.
- Unstable feedback/filter coefficients; sample-rate-dependent values before
  a valid rate is established.
- Interpolation boundaries and kernel look-ahead/look-behind requirements.
- Negative/non-finite playback or warp ratios; metadata indices outside samples.
- Integer overflow in sample/frame calculations and signed/unsigned conversions.
- Abrupt parameter jumps requiring smoothing.
- Denormals/subnormals in filters, envelopes, feedback, or long decays.

Use `juce::ScopedNoDenormals` or equivalent when susceptible to denormal slowdowns,
not mechanically on unaffected paths. Where practical, prevent invalid state
from persisting in voices/feedback; do not blindly clip output to mask unknown
DSP correctness.
