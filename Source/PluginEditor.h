#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "PluginProcessor.h"
#include "UI/SampleGroupSelector.h"

class CustomLookAndFeel;

class ImageKnobSlider final : public juce::Slider
{
public:
    bool hitTest(int x, int y) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();
        const auto radius = 0.5f * juce::jmin(bounds.getWidth(), bounds.getHeight());
        return centre.getDistanceFrom({ static_cast<float>(x), static_cast<float>(y) }) <= radius;
    }
};

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer,
                                              private juce::AudioProcessorParameter::Listener
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    // juce::Slider bitDepthSlider;
    // juce::Slider sampleRateSlider;
    ImageKnobSlider rzhavSlider;
    ImageKnobSlider sustainSlider;
    ImageKnobSlider sampleGainSlider;
    ImageKnobSlider samplePunchSlider;
    ImageKnobSlider samplePitchSlider;
    juce::Label rzhavLabel;
    juce::Label sustainLabel;
    juce::Label sampleGainLabel;
    juce::Label samplePunchLabel;
    juce::Label samplePitchLabel;
    juce::TextButton samplePitchModeButton { "Keep length" };
    juce::Label samplePitchStatusLabel;
    juce::TextButton applyToAllButton { "APPLY TO ALL" };
    juce::Label applyToAllStatusLabel;
    juce::ToggleButton warpButton;
    SampleGroupSelector sampleGroupSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rzhavAttachment;

    // std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bitDepthAttachment;
    // std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePunchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> warpAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> samplePitchModeAttachment;

private:
    struct SampleSpecificSliderBinding
    {
        juce::Slider* slider = nullptr;
        juce::String parameterId;
    };

    struct SampleSpecificEditBinding
    {
        juce::RangedAudioParameter* parameter = nullptr;
        bool gestureInProgress = false;
        float lastGestureValue = 0.0f;
    };

    void bindSliderToParameter(juce::Slider& slider,
                               const juce::String& parameterId,
                               std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment);
    void refreshSampleSpecificControls();
    void refreshPitchModeStatus();
    void refreshApplyToAllButton();
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    float getParameterDefaultValue(const juce::String& parameterId) const;
    void rebuildSampleGroupActivityMap();
    void timerCallback() override;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;
    std::unique_ptr<CustomLookAndFeel> customLNF;
    std::vector<SampleSpecificSliderBinding> sampleSpecificSliderBindings;
    std::vector<SampleSpecificEditBinding> sampleSpecificEditBindings;
    // Capture at editor construction: JUCE's message-thread query takes a mutex.
    const juce::Thread::ThreadID editorThreadId { juce::Thread::getCurrentThreadId() };
    juce::RangedAudioParameter* lastEditedSampleParameter = nullptr;
    float lastEditedSampleValue = 0.0f;
    bool ignoreSampleSpecificEdits = false;
    bool applyToAllButtonNeedsRefresh = false;
    std::array<int, AudioPluginAudioProcessor::midiNoteActivityCount> sampleGroupIndexByMidiNote {};
    std::array<uint32_t, AudioPluginAudioProcessor::midiNoteActivityCount> observedMidiNoteActivityGenerations {};
    int displayedSampleGroupIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
