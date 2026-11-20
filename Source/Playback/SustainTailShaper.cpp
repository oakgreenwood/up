#include "SustainTailShaper.h"

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

void SustainTailShaper::setMetadata(const SampleMetadata* newMetadata) noexcept
{
    metadata = newMetadata;
    resetPosition();
}

void SustainTailShaper::resetPosition() noexcept
{
    currentTransientIndex = 0;
}

bool SustainTailShaper::shouldShape(float amount) const noexcept
{
    return metadata != nullptr
        && !metadata->ignoreTransientShaper
        && !metadata->transients.empty()
        && amount > 0.0f;
}

float SustainTailShaper::getMakeupGain(float amount, bool hasTransientData) noexcept
{
    constexpr float maxSustainShortenMakeupDb = 3.0f;
    return hasTransientData && amount > 0.0f
        ? juce::Decibels::decibelsToGain(amount * maxSustainShortenMakeupDb) : 1.0f;
}

float SustainTailShaper::getGain(double timeSec, float amount)
{
    if (!shouldShape(amount))
        return 1.0f;

    const auto& transients = metadata->transients;

    while (currentTransientIndex + 1 < (int) transients.size()
           && timeSec >= transients[(size_t) currentTransientIndex + 1])
        ++currentTransientIndex;

    const double t0 = transients[(size_t) currentTransientIndex];
    const double t1 = (currentTransientIndex + 1 < (int) transients.size())
                        ? transients[(size_t) currentTransientIndex + 1]
                        : metadata->lengthSec;

    if (timeSec < t0)
        return 1.0f;

    const double segLen = t1 - t0;
    if (segLen <= 0.0)
        return 1.0f;

    const double aRaw = juce::jlimit(0.0, 1.0, (double) amount);
    constexpr double lowerKnee = 0.5;
    constexpr double lowerResponseAtHalf = 0.30;
    constexpr double lowerCurve = 2.5;
    constexpr double upperCurve = 1.1;

    double a = 0.0;
    if (aRaw <= lowerKnee)
    {
        const double t = aRaw / lowerKnee;
        a = lowerResponseAtHalf * std::pow(t, lowerCurve);
    }
    else
    {
        const double t = (aRaw - lowerKnee) / (1.0 - lowerKnee);
        a = lowerResponseAtHalf + (1.0 - lowerResponseAtHalf) * std::pow(t, upperCurve);
    }

    constexpr double holdCurve = 15.0;
    const double holdFrac = std::pow(1.0 - a, holdCurve);
    constexpr double protectedTransientSecAtMax = 0.012;
    const double holdFloorFrac = juce::jlimit(0.0, 1.0, (protectedTransientSecAtMax * a) / segLen);
    const double effectiveHoldFrac = juce::jmax(holdFrac, holdFloorFrac);
    const double fadeStart = t0 + effectiveHoldFrac * segLen;

    if (timeSec <= fadeStart)
        return 1.0f;
    if (timeSec >= t1)
        return 0.0f;

    const double x = (timeSec - fadeStart) / juce::jmax(1e-9, (t1 - fadeStart));

    constexpr double kMin = 0.2;
    const bool singleTransient = (transients.size() <= 1);
    const double kMax = singleTransient ? 45.0 : 14.0;
    const double k = kMin + (kMax - kMin) * a;

    const double e0 = std::exp(-k * 0.0);
    const double e1 = std::exp(-k * 1.0);
    const double ex = std::exp(-k * x);

    const double gain = (ex - e1) / (e0 - e1);
    return (float) juce::jlimit(0.0, 1.0, gain);
}
