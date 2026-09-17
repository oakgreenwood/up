#include "OttProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    struct BandSettings
    {
        double upwardThresholdDb;
        double downwardThresholdDb;
        double downwardSlope; // Reciprocal ratio; zero represents infinity:1.
        double attackMs;
        double releaseMs;
        double outputGainDb;
    };

    // Reference thresholds/ratios/times, ordered low / mid / high. The RMS
    // recovery and band makeup are calibrated to the supplied dry/OTT renders;
    // these envelope time constants are not a model of Ableton's internals.
    constexpr double inputGainDb = 5.2;
    constexpr double masterOutputGainDb = -17.0;
    constexpr double timeScale = 0.65;
    constexpr double rmsReleaseScale = 0.15;
    constexpr double upwardSlope = 1.0 / 4.17;
    constexpr std::array<BandSettings, 3> settings {{
        { -41.0, -33.8, 1.0 / 66.0, 47.8, 282.0, 12.5 },
        { -41.0, -30.2, 1.0 / 66.0, 22.4, 282.0, 7.75 },
        { -41.0, -37.5,        0.0, 13.5, 132.0, 11.0 }
    }};

    double sanitiseAmount(float amount) noexcept
    {
        return std::isfinite(amount) ? juce::jlimit(0.0, 1.0, (double) amount) : 0.0;
    }

    double compressionGainDb(double envelopePower, const BandSettings& band) noexcept
    {
        // A constant input gain commutes with the RMS power follower.
        // Include it in the detector level and once in the applied gain because
        // the audio bands themselves are still at their original input level.
        // The -120 dB detector floor only bounds log/boost at digital silence;
        // there is no quiet-tail gate, fade-out or separate 24 dB lift cap.
        const double levelDb = std::max(-120.0,
            10.0 * std::log10(std::max(envelopePower, 1.0e-24)) + inputGainDb);
        const double upward = std::max(0.0,
            band.upwardThresholdDb - levelDb) * (1.0 - upwardSlope);
        const double downward = std::max(0.0,
            levelDb - band.downwardThresholdDb) * (1.0 - band.downwardSlope);
        // A common output trim in every band is equivalent to trimming their
        // sum. The depth ramp also smooths this trim from bypass.
        return inputGainDb + upward - downward + band.outputGainDb + masterOutputGainDb;
    }
}

void OttProcessor::prepare(double sampleRate, float initialAmount)
{
    const double rate = std::isfinite(sampleRate) && sampleRate >= 1000.0 && sampleRate <= 768000.0
        ? sampleRate : 44100.0;
    const juce::dsp::ProcessSpec spec { rate, 1, maxChannels };
    lowCrossover.prepare(spec);
    highCrossover.prepare(spec);
    lowPhaseCompensation.prepare(spec);
    lowCrossover.setCutoffFrequency(std::min(88.0, rate * 0.2));
    const double highCutoff = std::min(2500.0, rate * 0.45);
    highCrossover.setCutoffFrequency(highCutoff);
    lowPhaseCompensation.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
    lowPhaseCompensation.setCutoffFrequency(highCutoff);

    depth.reset(rate, 0.02);
    bypassBlend.reset(rate, 0.02);
    for (size_t band = 0; band < bands.size(); ++band)
    {
        auto& state = bands[band];
        state.attack = std::exp(-1.0 / (rate * settings[band].attackMs * timeScale * 0.001));
        state.release = std::exp(-1.0 / (rate * settings[band].releaseMs
                                       * timeScale * rmsReleaseScale * 0.001));
    }

    reset();
    const double amount = sanitiseAmount(initialAmount);
    depth.setCurrentAndTargetValue(amount);
    bypassBlend.setCurrentAndTargetValue(amount > 0.0 ? 1.0 : 0.0);
    prepared = true;
}

void OttProcessor::reset() noexcept
{
    lowCrossover.reset();
    highCrossover.reset();
    lowPhaseCompensation.reset();
    for (auto& band : bands)
        band.envelopePower = 0.0;
    depth.setCurrentAndTargetValue(0.0);
    bypassBlend.setCurrentAndTargetValue(0.0);
}

void OttProcessor::process(juce::AudioBuffer<float>& buffer, float amount) noexcept
{
    if (!prepared)
        return;

    const int channels = std::min(buffer.getNumChannels(), maxChannels);
    const int frames = buffer.getNumSamples();
    if (channels == 0 || frames == 0)
        return;

    const juce::ScopedNoDenormals noDenormals;
    const double target = sanitiseAmount(amount);
    depth.setTargetValue(target);
    bypassBlend.setTargetValue(target > 0.0 ? 1.0 : 0.0);
    auto* const* output = buffer.getArrayOfWritePointers();

    for (int frame = 0; frame < frames; ++frame)
    {
        std::array<double, maxChannels> dry {};
        std::array<std::array<double, maxChannels>, bandCount> split {};
        for (int channel = 0; channel < channels; ++channel)
        {
            const float input = output[channel][frame];
            dry[(size_t) channel] = std::isfinite(input) ? (double) input : 0.0;
            double low = 0.0, upper = 0.0;
            lowCrossover.processSample(channel, dry[(size_t) channel], low, upper);
            highCrossover.processSample(channel, upper, split[1][(size_t) channel], split[2][(size_t) channel]);
            // Match the high split's phase on the low branch: unity band gains
            // then sum to a flat-magnitude allpass, without a crossover notch.
            split[0][(size_t) channel] = lowPhaseCompensation.processSample(channel, low);
        }

        const double currentDepth = depth.getNextValue();
        const double blend = bypassBlend.getNextValue();
        std::array<double, bandCount> gains {};
        for (size_t band = 0; band < bands.size(); ++band)
        {
            auto& state = bands[band];
            double peak = 0.0;
            for (int channel = 0; channel < channels; ++channel)
                peak = std::max(peak, std::abs(split[band][(size_t) channel]));

            const double power = peak * peak;
            const double coefficient = power > state.envelopePower ? state.attack : state.release;
            state.envelopePower = coefficient * state.envelopePower + (1.0 - coefficient) * power;

            // Update gain from this frame's detector value. Holding a large
            // upward gain for 16 frames let new hits escape before compression.
            // The power follower and depth ramp already smooth the gain curve.
            gains[band] = currentDepth > 0.0
                ? std::pow(10.0, currentDepth * compressionGainDb(state.envelopePower, settings[band]) * 0.05)
                : 1.0;
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            // Filters/detectors keep tracking at zero, but the output is the
            // exact dry sample once the short bypass transition has finished.
            double result = dry[(size_t) channel];
            if (blend > 0.0)
            {
                double wet = 0.0;
                for (size_t band = 0; band < bands.size(); ++band)
                    wet += split[band][(size_t) channel] * gains[band];
                result += blend * (wet - result);
            }
            output[channel][frame] = std::isfinite(result)
                && std::abs(result) <= (double) std::numeric_limits<float>::max()
                ? (float) result : 0.0f;
        }
    }
}
