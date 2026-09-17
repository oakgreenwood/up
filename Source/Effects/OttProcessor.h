#pragma once

#include <array>
#include <juce_dsp/juce_dsp.h>

// Global, stereo-linked three-band upward/downward compression.
class OttProcessor
{
public:
    void prepare(double sampleRate, float initialAmount);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, float amount) noexcept;

    static constexpr double tailSeconds = 0.5;

private:
    static constexpr int maxChannels = 2;
    static constexpr int bandCount = 3;
    struct BandState
    {
        double envelopePower = 0.0;
        double attack = 0.0;
        double release = 0.0;
    };

    // Storage is allocated only by prepare; processSample never resizes it.
    juce::dsp::LinkwitzRileyFilter<double> lowCrossover;
    juce::dsp::LinkwitzRileyFilter<double> highCrossover;
    juce::dsp::LinkwitzRileyFilter<double> lowPhaseCompensation;
    std::array<BandState, bandCount> bands;
    juce::SmoothedValue<double> depth;
    juce::SmoothedValue<double> bypassBlend;
    bool prepared = false;
};
