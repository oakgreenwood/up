#pragma once

#include <algorithm>
#include <cmath>

// Source-time fades shared by playback and the waveform overlay.
struct SampleEnvelope
{
    double duration = 0.0;
    double attack = 0.0;
    double release = 0.0;

    static SampleEnvelope resolve(double durationSeconds, double attackSeconds,
                                  double releaseSeconds) noexcept
    {
        const double duration = std::isfinite(durationSeconds)
            ? std::max(0.0, durationSeconds) : 0.0;
        const auto bounded = [duration] (double value)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0, duration) : 0.0;
        };
        return { duration, bounded(attackSeconds), bounded(releaseSeconds) };
    }

    float gain(double positionSeconds) const noexcept
    {
        if (!std::isfinite(positionSeconds))
            return 0.0f;
        const double fadeIn = attack > 0.0 ? positionSeconds / attack : 1.0;
        const double fadeOut = release > 0.0 ? (duration - positionSeconds) / release : 1.0;
        return static_cast<float>(std::clamp(std::min(fadeIn, fadeOut), 0.0, 1.0));
    }
};
