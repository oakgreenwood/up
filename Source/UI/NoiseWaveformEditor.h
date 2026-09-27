#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Playback/BackgroundNoisePlayer.h"

class AudioPluginAudioProcessor;

class NoiseWaveformEditor final : public juce::Component,
                                 public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit NoiseWaveformEditor(AudioPluginAudioProcessor&);
    ~NoiseWaveformEditor() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void visibilityChanged() override;
    void endDrag();
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void refresh();
    juce::Rectangle<float> plotBounds() const;

    AudioPluginAudioProcessor& processor;
    std::atomic<float>* sourceValue = nullptr;
    std::atomic<float>* startValue = nullptr;
    std::atomic<float>* endValue = nullptr;
    juce::RangedAudioParameter* startParameter = nullptr;
    juce::RangedAudioParameter* endParameter = nullptr;
    juce::RangedAudioParameter* draggedParameter = nullptr;
    const BackgroundNoisePlayer::Waveform* waveform = nullptr;
    BackgroundNoisePlayer::Waveform waveformStorage;
    uint32_t displayedRevision = 0;
    int displayedSource = -1;
    float displayedStart = 0.0f, displayedEnd = 1.0f;
    float displayedPlayhead = -1.0f;
    float dragOffset = 0.0f;
};
