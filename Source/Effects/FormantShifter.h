#pragma once

#include <array>
#include <complex>
#include <juce_dsp/juce_dsp.h>

// Per-voice LPC spectral-envelope shifting, after pitch/time processing. Pitch is
// already present in the input envelope, so scale is an additional formant shift.
// All storage and FFT plans are created with the voice, never during rendering.
class FormantShifter final
{
public:
    static constexpr int fftOrder = 9;
    static constexpr int frameSize = 1 << fftOrder;
    static constexpr int hopSize = frameSize / 4;
    static constexpr int latencySamples = frameSize;
    static constexpr int tailSamples = 2 * frameSize;

    FormantShifter();
    void prepare(double sampleRate) noexcept;
    void reset(float ratio) noexcept;
    void setRatio(float ratio) noexcept;
    void process(float& left, float& right) noexcept;

private:
    static constexpr int modelOrder = 20;
    static constexpr int binCount = frameSize / 2 + 1;
    using Spectrum = std::array<std::complex<float>, frameSize>;
    using Frame = std::array<float, frameSize>;
    using Bins = std::array<float, binCount>;
    void processFrame() noexcept;
    bool analyseEnvelope(float peak) noexcept;
    float envelopeAt(float bin) const noexcept;
    static float modelBandWeight(float bin) noexcept;
    static float saturationAmount(float ratio) noexcept;
    static float lowerFormantGainForRatio(float ratio) noexcept;
    void applySaturation(float& left, float& right) noexcept;

    juce::dsp::FFT fft { fftOrder };
    std::array<Frame, 2> input {};
    std::array<Frame, 2> correction {};
    std::array<Spectrum, 2> spectra {};
    Spectrum scratch {}, autocorrelation {}, modelResponse {};
    Frame window {};
    Bins power {}, analysisBins {}, preEmphasisPower {}, deEmphasisLog {}, logEnvelope {};
    std::array<double, modelOrder + 1> bandwidthExpansion {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> shift { 1.0f };
    juce::SmoothedValue<float> saturationMix { 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowerFormantGain { 1.0f };
    std::array<double, 2> previousDriveInput {}, previousDriveRoot {};
    bool driveReady = false;
    float modelBinsPerOutputBin = 1.0f;
    float envelopeMemory = 0.0f;
    bool envelopeReady = false;
    int position = 0;
    int hopPosition = 0;
};
