#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "PluginProcessor.h"
#include "UI/SampleGroupSelector.h"
#include "UI/SampleEqualiserEditor.h"

class CustomLookAndFeel;

class ImageKnobSlider final : public juce::Slider
{
public:
    bool keyPressed(const juce::KeyPress& key) override;

    bool hitTest(int x, int y) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();
        const auto radius = 0.5f * juce::jmin(bounds.getWidth(), bounds.getHeight());
        return centre.getDistanceFrom({ static_cast<float>(x), static_cast<float>(y) }) <= radius;
    }
};

// A separate label keeps text editing independent of automated slider updates.
class NumericValueInput final : public juce::Label,
                                private juce::Slider::Listener
{
public:
    enum class Validation
    {
        signedDecimal,
        percentage,
        panorama
    };

    NumericValueInput(juce::Slider& controlledSlider,
                      const juce::String& name,
                      Validation validation = Validation::signedDecimal,
                      const juce::String& displaySuffix = {});
    ~NumericValueInput() override;

    void discardEdit();
    bool keyPressed(const juce::KeyPress& key) override;

private:
    juce::TextEditor* createEditorComponent() override;
    void textEditorReturnKeyPressed(juce::TextEditor& editor) override;
    void textEditorEscapeKeyPressed(juce::TextEditor& editor) override;
    void textEditorFocusLost(juce::TextEditor& editor) override;
    void sliderValueChanged(juce::Slider*) override;
    void commitEdit(const juce::String& text);
    void refreshValue();

    juce::Slider& slider;
    const Validation validation;
    const juce::String displaySuffix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NumericValueInput)
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
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    // juce::Slider bitDepthSlider;
    // juce::Slider sampleRateSlider;
    ImageKnobSlider rzhavSlider;
    ImageKnobSlider sustainSlider;
    ImageKnobSlider ottSlider;
    ImageKnobSlider sampleGainSlider;
    ImageKnobSlider samplePunchSlider;
    ImageKnobSlider samplePitchSlider;
    ImageKnobSlider sampleFormantSlider;
    ImageKnobSlider sampleMonoSlider;
    ImageKnobSlider samplePanSlider;
    NumericValueInput sampleGainValueInput {
        sampleGainSlider, "Gain value", NumericValueInput::Validation::signedDecimal, " dB"
    };
    NumericValueInput samplePunchValueInput {
        samplePunchSlider, "Punch value", NumericValueInput::Validation::percentage, "%"
    };
    NumericValueInput samplePitchValueInput {
        samplePitchSlider, "Pitch value", NumericValueInput::Validation::signedDecimal, " st"
    };
    NumericValueInput sampleFormantValueInput {
        sampleFormantSlider, "Formant value", NumericValueInput::Validation::signedDecimal, " st"
    };
    NumericValueInput sampleMonoValueInput {
        sampleMonoSlider, "Mono value", NumericValueInput::Validation::percentage, "%"
    };
    NumericValueInput samplePanValueInput {
        samplePanSlider, "Panorama value", NumericValueInput::Validation::panorama
    };
    juce::Label rzhavLabel;
    juce::Label sustainLabel;
    juce::Label ottLabel;
    juce::Label sampleGainLabel;
    juce::Label samplePunchLabel;
    juce::Label samplePitchLabel;
    juce::Label sampleFormantLabel;
    juce::Label sampleMonoLabel;
    juce::Label samplePanLabel;
    juce::TextButton applyToAllButton { "APPLY TO ALL" };
    juce::ComboBox applyToAllEffectSelector;
    juce::ToggleButton warpButton;
    SampleGroupSelector sampleGroupSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rzhavAttachment;

    // std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bitDepthAttachment;
    // std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ottAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePunchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleFormantAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleMonoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePanAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> warpAttachment;

private:
    struct SampleSpecificSliderBinding
    {
        juce::Slider* slider = nullptr;
        juce::String parameterId;
        juce::Label* label = nullptr;
    };

    struct SampleSpecificEditBinding
    {
        juce::RangedAudioParameter* parameter = nullptr;
        bool gestureInProgress = false;
        float lastGestureValue = 0.0f;
    };

    void bindSliderToParameter(juce::Slider& slider,
                               const juce::String& parameterId,
                               std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                               juce::Label* label = nullptr);
    void refreshSampleSpecificControls();
    juce::RangedAudioParameter* getSelectedApplyToAllParameter() const;
    void selectApplyToAllEffect(juce::RangedAudioParameter& parameter);
    void refreshApplyToAllButton();
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    float getParameterDefaultValue(const juce::String& parameterId) const;
    void rebuildSampleGroupActivityMap();
    void selectSampleGroupForEditing(int groupIndex);
    void timerCallback() override;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;
    SampleEqualiserEditor equaliserEditor;
    std::unique_ptr<CustomLookAndFeel> customLNF;
    std::vector<SampleSpecificSliderBinding> sampleSpecificSliderBindings;
    std::vector<SampleSpecificEditBinding> sampleSpecificEditBindings;
    std::vector<juce::RangedAudioParameter*> applyToAllParameters;
    // Capture at editor construction: JUCE's message-thread query takes a mutex.
    const juce::Thread::ThreadID editorThreadId { juce::Thread::getCurrentThreadId() };
    juce::RangedAudioParameter* pendingApplyToAllParameter = nullptr;
    bool ignoreSampleSpecificEdits = false;
    std::array<int, AudioPluginAudioProcessor::midiNoteActivityCount> sampleGroupIndexByMidiNote {};
    std::array<uint32_t, AudioPluginAudioProcessor::midiNoteActivityCount> observedMidiNoteActivityGenerations {};
    uint32_t observedLatestMidiNoteOnGeneration = 0;
    uint32_t lastMappedMidiNoteOnTimeMs = 0;
    int pendingMidiSampleGroupIndex = -1;
    bool midiSelectionBurstActive = false;
    int displayedSampleGroupIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
