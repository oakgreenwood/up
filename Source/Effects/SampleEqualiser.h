#pragma once

#include <array>
#include <juce_dsp/juce_dsp.h>

class SampleEqualiser final
{
public:
    static constexpr int bandCount = 4;
    static constexpr double q = 1.0;
    static constexpr double tailSeconds = 0.5;
    using Coefficients = std::array<double, 5>; // b0, b1, b2, a1, a2; a0 = 1

    static double validSampleRate(double rate) noexcept;
    static Coefficients coefficients(int band, double frequency, double gainDb, double rate) noexcept;
    static double magnitude(const Coefficients&, double frequency, double rate) noexcept;
    void prepare(double rate) noexcept;
    void setBand(int band, float frequency, float gainDb, bool immediate = false) noexcept;
    void reset() noexcept;
    void process(float& left, float& right) noexcept;
    bool hasTail() const noexcept;

private:
    static constexpr Coefficients identityCoefficients() noexcept
    {
        return { 1.0, 0.0, 0.0, 0.0, 0.0 };
    }

    static bool coefficientsAreFiniteAndStable(const Coefficients&) noexcept;

    struct Band
    {
        Coefficients current { identityCoefficients() };
        Coefficients target { identityCoefficients() };
        Coefficients step {};
        std::array<std::array<double, 2>, 2> state {};
        float frequency = -1.0f, gain = 0.0f;
        int remaining = 0;
        bool active = false;
    };
    std::array<Band, bandCount> bands;
    double sampleRate = 44100.0;
    int rampSamples = 441;
};
