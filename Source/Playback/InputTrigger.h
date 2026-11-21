#pragma once

#include <algorithm>
#include <cmath>

class InputTrigger
{
public:
    static constexpr int suppressedRetrigger = -1;

    void prepare(double sampleRate) noexcept
    {
        const double rate = std::isfinite(sampleRate) && sampleRate >= 1000.0 && sampleRate <= 768000.0
            ? sampleRate : 44100.0;
        peakFrames = static_cast<int>(std::round(rate * 0.005));
        rearmFrames = static_cast<int>(std::round(rate * 0.012));
        minimumHitIntervalFrames = static_cast<int>(std::ceil(rate * 0.060));
        retriggerWindowStart = static_cast<int>(std::ceil(rate * 0.210));
        retriggerWindowEnd = static_cast<int>(std::floor(rate * 0.260));
        attackEnvelopeCoefficient = static_cast<float>(-std::expm1(-1.0 / (rate * 0.001)));
        localEnvelopeCoefficient = static_cast<float>(-std::expm1(-1.0 / (rate * 0.010)));
        reset();
    }

    void reset() noexcept
    {
        stage = Stage::waiting;
        frames = 0;
        holdoffFramesRemaining = 0;
        peak = 0.0f;
        lastHitPeak = 0.0f;
        framesSinceHit = -1;
        attackEnvelope = 0.0f;
        localEnvelope = 0.0f;
    }

#if JUCE_DEBUG
    float getPeak() const noexcept { return peak; }
#endif

    // Feed the largest absolute channel amplitude for each input frame.
    // Positive values are MIDI velocities. A suppressed retrigger returns -1.
    int process(float level) noexcept
    {
        constexpr float threshold = 0.017782794f; // -35 dBFS
        constexpr float maximum = 0.56234133f;    // -5 dBFS
        constexpr float rearmLevel = 0.012589254f; // -38 dBFS
        constexpr float retriggerPeakRatio = 0.63095734f; // -4 dB relative to the last hit
        constexpr float inverseAttackRatio = 1.0f / 3.0f;

        // Track recent input across blocks so a new attack can stand out from
        // a tail that never goes quiet. Both envelopes use only past and current audio.
        attackEnvelope += attackEnvelopeCoefficient * (level - attackEnvelope);
        const float previousLocalEnvelope = localEnvelope;
        localEnvelope += localEnvelopeCoefficient * (attackEnvelope - localEnvelope);

        if (holdoffFramesRemaining > 0)
            --holdoffFramesRemaining;
        if (framesSinceHit >= 0 && ++framesSinceHit > retriggerWindowEnd)
            framesSinceHit = -1;

        if (stage == Stage::rearming)
        {
            frames = level < rearmLevel ? frames + 1 : 0;
            if (holdoffFramesRemaining > 0)
                return 0;
            if (frames >= rearmFrames)
            {
                stage = Stage::waiting;
                return 0;
            }
            if (level < threshold || attackEnvelope < threshold
                || attackEnvelope * inverseAttackRatio < previousLocalEnvelope)
                return 0;
            stage = Stage::waiting;
        }

        if (stage == Stage::waiting)
        {
            if (level < threshold)
                return 0;
            stage = Stage::measuring;
            frames = 0;
            peak = 0.0f;
            holdoffFramesRemaining = minimumHitIntervalFrames;
        }

        peak = std::max(peak, level);
        if (++frames < peakFrames)
            return 0;

        stage = Stage::rearming;
        frames = 0;
        // Match the delayed, quieter repeats observed with speaker monitoring.
        // Keep the last accepted hit as the reference, and hold off so the
        // same repeat cannot immediately start another peak window.
        if (framesSinceHit >= retriggerWindowStart && peak <= lastHitPeak * retriggerPeakRatio)
            return suppressedRetrigger;

        framesSinceHit = 0;
        lastHitPeak = peak;
        if (peak >= maximum)
            return 127;

        const float decibels = 20.0f * std::log10(peak);
        return std::clamp(1 + static_cast<int>(std::round((decibels + 35.0f) * (126.0f / 30.0f))),
                          1, 126);
    }

private:
    enum class Stage { waiting, measuring, rearming };
    Stage stage = Stage::waiting;
    int peakFrames = 221;
    int rearmFrames = 529;
    int minimumHitIntervalFrames = 2646;
    int retriggerWindowStart = 9261;
    int retriggerWindowEnd = 11466;
    int holdoffFramesRemaining = 0;
    int framesSinceHit = -1;
    int frames = 0;
    float peak = 0.0f;
    float lastHitPeak = 0.0f;
    float attackEnvelope = 0.0f;
    float localEnvelope = 0.0f;
    float attackEnvelopeCoefficient = 0.02242057f; // 1 ms at 44.1 kHz
    float localEnvelopeCoefficient = 0.00226500f;  // 10 ms at 44.1 kHz
};
