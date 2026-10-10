#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <utility>
#include <vector>
#include "../PercussionSound.h"

class AudioPluginAudioProcessor;

class SampleWaveformEditor final : public juce::Component,
                                   private juce::Timer
{
public:
    enum class BeatSnapTarget { off, transients, quarterNote, eighthNote, sixteenthNote };

    explicit SampleWaveformEditor(AudioPluginAudioProcessor&);
    ~SampleWaveformEditor() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void visibilityChanged() override;
    void selectionChanged();
    void endDrag();
    void setBeatSnapTarget(BeatSnapTarget target);
    double getDurationMs() const noexcept;
    double getStartMs() const noexcept { return displayedStart * getDurationMs(); }
    double getEndMs() const noexcept { return displayedEnd * getDurationMs(); }

private:
    void timerCallback() override;
    void refresh();
    juce::Rectangle<float> plotBounds() const;
    std::pair<float, float> envelopeHandlePositions() const;
    double gridStepFrames() const noexcept;
    int nearestSnapFrame(double targetFrame, int otherFrame, bool movingStart) const noexcept;

    AudioPluginAudioProcessor& processor;
    juce::RangedAudioParameter* startParameter = nullptr;
    juce::RangedAudioParameter* endParameter = nullptr;
    juce::RangedAudioParameter* attackParameter = nullptr;
    juce::RangedAudioParameter* releaseParameter = nullptr;
    juce::RangedAudioParameter* draggedParameter = nullptr;
    const PercussionSound::Waveform* waveform = nullptr;
    int displayedGroup = -1;
    float displayedStart = 0.0f;
    float displayedEnd = 1.0f;
    float displayedPlayhead = -1.0f;
    float displayedAttackMs = 0.0f;
    float displayedReleaseMs = 60.0f;
    bool hideEnvelopeControls = false;
    BeatSnapTarget beatSnapTarget = BeatSnapTarget::transients;
    double originalBpm = 0.0;
    std::vector<int> transientFrames;
    float dragOffset = 0.0f;
};
