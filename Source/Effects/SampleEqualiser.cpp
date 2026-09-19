#include "SampleEqualiser.h"
#include <cmath>
#include <complex>
#include <limits>

namespace
{
constexpr double stateSilenceThreshold = 1.0e-15;
constexpr double stabilityMargin = 1.0e-12;
}

double SampleEqualiser::validSampleRate(double rate) noexcept
{
    return std::isfinite(rate) && rate >= 1000.0 && rate <= 768000.0 ? rate : 44100.0;
}

SampleEqualiser::Coefficients SampleEqualiser::coefficients(int band, double frequency,
                                                           double gainDb, double rate) noexcept
{
    if (band < 0 || band >= bandCount)
        return identityCoefficients();

    rate = validSampleRate(rate);
    frequency = juce::jlimit(20.0, juce::jmin(20000.0, rate * 0.45),
                            std::isfinite(frequency) ? frequency : 1000.0);
    gainDb = juce::jlimit(-15.0, 15.0, std::isfinite(gainDb) ? gainDb : 0.0);
    if (gainDb == 0.0)
        return identityCoefficients();

    const double gain = std::pow(10.0, gainDb / 20.0);
    using Factory = juce::dsp::IIR::ArrayCoefficients<double>;
    const auto c = band == 0 ? Factory::makeLowShelf(rate, frequency, q, gain)
                 : band == 3 ? Factory::makeHighShelf(rate, frequency, q, gain)
                             : Factory::makePeakFilter(rate, frequency, q, gain);
    if (!std::isfinite(c[3]) || std::abs(c[3]) <= std::numeric_limits<double>::min())
        return identityCoefficients();

    const Coefficients normalised {
        c[0] / c[3], c[1] / c[3], c[2] / c[3], c[4] / c[3], c[5] / c[3]
    };
    return coefficientsAreFiniteAndStable(normalised) ? normalised : identityCoefficients();
}

double SampleEqualiser::magnitude(const Coefficients& c, double frequency, double rate) noexcept
{
    const auto z = std::polar(1.0, -juce::MathConstants<double>::twoPi * frequency / validSampleRate(rate));
    const auto denominator = 1.0 + c[3] * z + c[4] * z * z;
    if (!std::isfinite(std::abs(denominator))
        || std::abs(denominator) <= std::numeric_limits<double>::min())
        return 1.0;

    const double result = std::abs((c[0] + c[1] * z + c[2] * z * z) / denominator);
    return std::isfinite(result) ? result : 1.0;
}

bool SampleEqualiser::coefficientsAreFiniteAndStable(const Coefficients& c) noexcept
{
    for (const auto value : c)
        if (!std::isfinite(value))
            return false;

    // Jury stability conditions for 1 + a1 z^-1 + a2 z^-2. Their valid
    // region is convex, so the per-sample linear ramp between valid endpoints
    // also keeps every intermediate denominator stable.
    const double a1 = c[3];
    const double a2 = c[4];
    return std::abs(a2) < 1.0
        && 1.0 + a1 + a2 > stabilityMargin
        && 1.0 - a1 + a2 > stabilityMargin;
}

void SampleEqualiser::prepare(double rate) noexcept
{
    sampleRate = validSampleRate(rate);
    rampSamples = juce::jmax(1, static_cast<int>(sampleRate * 0.01));
    bands = {};
}

void SampleEqualiser::setBand(int index, float frequency, float gainDb, bool immediate) noexcept
{
    if (index < 0 || index >= bandCount)
        return;

    auto& band = bands[static_cast<size_t>(index)];
    const float safeFrequency = juce::jlimit(20.0f,
        static_cast<float>(juce::jmin(20000.0, sampleRate * 0.45)),
        std::isfinite(frequency) ? frequency : 1000.0f);
    const float safeGain = juce::jlimit(-15.0f, 15.0f,
        std::isfinite(gainDb) ? gainDb : 0.0f);

    if (!immediate && safeFrequency == band.frequency && safeGain == band.gain)
        return;

    band.frequency = safeFrequency;
    band.gain = safeGain;

    // A neutral, already-silent band is an exact bypass. Frequency automation
    // at 0 dB therefore does no trigonometry and no per-sample filter work.
    if (safeGain == 0.0f && !band.active)
    {
        band.current = band.target = identityCoefficients();
        band.step = {};
        band.remaining = 0;
        return;
    }

    band.target = safeGain == 0.0f
        ? identityCoefficients()
        : coefficients(index, safeFrequency, safeGain, sampleRate);
    band.remaining = immediate ? 0 : rampSamples;
    if (immediate)
    {
        band.current = band.target;
        band.step = {};
        band.active = safeGain != 0.0f;
    }
    else
    {
        for (size_t i = 0; i < band.step.size(); ++i)
            band.step[i] = (band.target[i] - band.current[i]) / rampSamples;
        band.active = true;
    }
}

void SampleEqualiser::reset() noexcept
{
    for (auto& band : bands)
    {
        band.state = {};
        if (band.gain == 0.0f && band.remaining == 0)
            band.active = false;
    }
}

bool SampleEqualiser::hasTail() const noexcept
{
    for (const auto& band : bands)
        if (band.active)
            return true;
    return false;
}

void SampleEqualiser::process(float& left, float& right) noexcept
{
    double values[] { std::isfinite(left) ? left : 0.0, std::isfinite(right) ? right : 0.0 };
    for (auto& band : bands)
    {
        if (!band.active)
            continue;

        if (band.remaining > 0)
        {
            for (size_t i = 0; i < band.current.size(); ++i)
                band.current[i] += band.step[i];
            if (--band.remaining == 0)
                band.current = band.target;
        }
        const auto& c = band.current;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto& s = band.state[ch];
            const double out = c[0] * values[ch] + s[0];
            s[0] = c[1] * values[ch] - c[3] * out + s[1];
            s[1] = c[2] * values[ch] - c[4] * out;
            if (!std::isfinite(out) || !std::isfinite(s[0]) || !std::isfinite(s[1])
                || std::abs(out) > std::numeric_limits<float>::max())
            {
                s = {};
                values[ch] = 0.0;
            }
            else
                values[ch] = out;
        }

        if (band.remaining == 0 && band.gain == 0.0f)
        {
            bool stateIsSilent = true;
            for (const auto& channel : band.state)
                for (const auto stateValue : channel)
                    stateIsSilent = stateIsSilent && std::abs(stateValue) <= stateSilenceThreshold;

            if (stateIsSilent)
            {
                band.state = {};
                band.current = band.target = identityCoefficients();
                band.step = {};
                band.active = false;
            }
        }
    }
    left = static_cast<float>(values[0]);
    right = static_cast<float>(values[1]);
}
