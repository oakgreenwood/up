#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Effects/SampleEqualiser.h"
#include "../Effects/SampleSpectrum.h"

class AudioPluginAudioProcessor;

class SampleEqualiserEditor final : public juce::Component,
                                   private juce::Timer
{
public:
    explicit SampleEqualiserEditor(AudioPluginAudioProcessor&);
    ~SampleEqualiserEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void endDrag();
    void selectionChanged();
    std::function<void()> onSelected;

private:
    void timerCallback() override;
    bool refreshResponse();
    float responseDbAt(float frequency) const;
    float responseY(float db) const;
    void rebuildPlotPoints();
    void rebuildSpectrumPath();
    juce::Rectangle<float> plotBounds() const;
    float maximumFrequency() const;
    float frequencyToX(float frequency) const;
    float xToFrequency(float x) const;
    float gainToY(float gain) const;
    float yToGain(float y) const;
    juce::Point<float> dotPosition(int band) const;
    float value(juce::RangedAudioParameter*) const;
    int selectedNote() const;

    AudioPluginAudioProcessor& processor;
    SampleSpectrum& spectrum;
    std::array<juce::RangedAudioParameter*, 4> frequencies {}, gains {};
    juce::dsp::FFT fft { SampleSpectrum::fftOrder };
    juce::dsp::WindowingFunction<float> window { SampleSpectrum::fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, SampleSpectrum::fftSize * 2> fftLeft {}, fftRight {};
    std::array<float, SampleSpectrum::fftSize / 2 + 1> levels {};
    struct PlotPoint
    {
        float x = 0.0f, frequency = 20.0f, bin = 0.0f;
    };
    // Two vertices per logical pixel, capped for unusually large editors.
    static constexpr int maximumPlotSegments = 2048;
    std::array<PlotPoint, maximumPlotSegments + 1> plotPoints {};
    int plotSegments = 0;
    std::array<float, 4> displayedFrequencies {}, displayedGains {};
    std::array<SampleEqualiser::Coefficients, 4> displayedCoefficients {};
    std::array<juce::Point<float>, 4> dots {};
    juce::Path responsePath, spectrumPath;
    bool responseDirty = true;
    double previousUpdateMs = 0.0;
    float displaySampleRate = 44100.0f;
    int displayedNote = -1;
    int draggedBand = -1, lastTouchedBand = 3, dragGroup = -1;
    juce::Point<float> dragOffset;
};
