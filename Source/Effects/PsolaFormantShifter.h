#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

// Streaming TD-PSOLA formant experiment: resample pitch-mark-centred grains,
// keeping their output centres at the original pitch spacing. No LPC or FFT.
// prepare() owns all allocation; note reset/render touch prepared storage only.
class PsolaFormantShifter final
{
public:
    PsolaFormantShifter();
    void prepare(double sampleRate);
    void reset(float ratio) noexcept;
    void setRatio(float ratio) noexcept;
    void process(float& left, float& right) noexcept;

    static int latencyForSampleRate(double sampleRate) noexcept;
    static int tailForSampleRate(double sampleRate) noexcept;

private:
    static constexpr int analysisCapacity = 512;
    static constexpr int comparisonSamples = 192;
    static constexpr int maximumLag = 136;
    static constexpr int windowTableSize = 1024;
    using Stereo = std::array<float, 2>;
    static double safeSampleRate(double sampleRate) noexcept;
    void analysePitch() noexcept;
    void scheduleGrain(float ratio, float confidence) noexcept;
    double findPitchMark(double predicted, double radius, bool first) const noexcept;
    void addGrain(int64_t mark, double period, float ratio, float confidence) noexcept;
    float read(int channel, int64_t index) const noexcept;
    float interpolate(int channel, double index) const noexcept;
    float window(double distance) const noexcept;

    std::vector<Stereo> input, correction;
    size_t ringMask = 0;
    std::array<Stereo, analysisCapacity> analysis {};
    std::array<float, analysisCapacity> normalisedAnalysis {};
    std::array<double, maximumLag + 1> differences {};
    std::array<float, windowTableSize + 1> windowTable {};
    std::array<double, 2> filter1 {}, filter2 {}, decimationSum {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> shift { 1.0f };
    juce::SmoothedValue<float> voicing { 0.0f };
    double filterCoefficient = 0.0;
    double periodSamples = 0.0;
    double nextMark = 0.0;
    int64_t lastMark = 0, sampleIndex = 0, nextAttempt = 0;
    int maxPeriod = 1, minPeriod = 1, latency = 1;
    int decimation = 1, decimationCount = 0;
    int analysisPosition = 0, analysisCount = 0, analysisCountdown = 0;
    int analysisHop = 1, minLag = 2, maxLag = maximumLag;
    int referenceChannel = 0;
    bool pitchReady = false, marksReady = false, hasScheduledGrain = false;
};
