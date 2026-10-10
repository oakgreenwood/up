#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "PluginProcessor.h"
#include "UI/SampleGroupSelector.h"
#include "UI/SampleEqualiserEditor.h"
#include "UI/SampleWaveformEditor.h"
#include "UI/NoiseWaveformEditor.h"

class CustomLookAndFeel;

enum class ValueDragAxis { undecided, vertical, horizontal };

class KeyboardStepSlider final : public juce::Slider
{
public:
    bool keyPressed(const juce::KeyPress& key) override;
};

class ImageKnobSlider final : public juce::Slider
{
public:
    ImageKnobSlider()
    {
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
    }

    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    bool hitTest(int x, int y) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();
        const auto radius = 0.5f * juce::jmin(bounds.getWidth(), bounds.getHeight());
        return centre.getDistanceFrom({ static_cast<float>(x), static_cast<float>(y) }) <= radius;
    }

private:
    ValueDragAxis dragAxis = ValueDragAxis::undecided;
    juce::Point<float> dragStartPosition;
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
    void refreshDisplay();
    bool editing() const noexcept { return isBeingEdited(); }
    bool dragging() const noexcept { return dragGesture.has_value(); }
    void setManualGestureParameter(juce::RangedAudioParameter* parameter) noexcept
    {
        manualGestureParameter = parameter;
    }
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void visibilityChanged() override;

private:
    juce::TextEditor* createEditorComponent() override;
    void textEditorReturnKeyPressed(juce::TextEditor& editor) override;
    void textEditorEscapeKeyPressed(juce::TextEditor& editor) override;
    void textEditorFocusLost(juce::TextEditor& editor) override;
    void sliderValueChanged(juce::Slider*) override;
    void commitEdit(const juce::String& text);
    void refreshValue();
    void endDrag();

    juce::Slider& slider;
    const Validation validation;
    const juce::String displaySuffix;
    juce::RangedAudioParameter* manualGestureParameter = nullptr;
    std::optional<juce::Slider::ScopedDragNotification> dragGesture;
    double dragStartValue = 0.0;
    ValueDragAxis dragAxis = ValueDragAxis::undecided;
    bool canDrag = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NumericValueInput)
};

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              public juce::FileDragAndDropTarget,
                                              private juce::Timer,
                                              private juce::AudioProcessorParameter::Listener
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void visibilityChanged() override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    // juce::Slider bitDepthSlider;
    // juce::Slider sampleRateSlider;
    ImageKnobSlider rzhavSlider;
    ImageKnobSlider sustainSlider;
    ImageKnobSlider ottSlider;
    juce::ComboBox noiseSourceSelector;
    juce::StringArray displayedNoiseNames;
    uint32_t displayedNoiseRevision = 0;
    KeyboardStepSlider noiseGainSlider;
    juce::Label noiseLabel;
    juce::Label noiseSourceLabel, noiseTriggerLabel, noiseFollowLabel;
    juce::ComboBox noiseTriggerSelector;
    KeyboardStepSlider noiseModulationSlider;
    juce::Label noiseModulationLabel;
    ImageKnobSlider noiseReleaseSlider;
    juce::Label noiseReleaseLabel;
    NumericValueInput noiseReleaseValueInput {
        noiseReleaseSlider, "Noise release value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    ImageKnobSlider sampleGainSlider;
    ImageKnobSlider samplePunchSlider;
    ImageKnobSlider samplePitchSlider;
    ImageKnobSlider sampleFormantSlider;
    ImageKnobSlider sampleMonoSlider;
    ImageKnobSlider samplePanSlider;
    KeyboardStepSlider sampleAttackSlider;
    KeyboardStepSlider sampleReleaseSlider;
    KeyboardStepSlider sampleStartMsSlider;
    KeyboardStepSlider sampleEndMsSlider;
    KeyboardStepSlider sampleLoopBpmSlider;
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
    NumericValueInput sampleAttackValueInput {
        sampleAttackSlider, "Sample attack value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    NumericValueInput sampleReleaseValueInput {
        sampleReleaseSlider, "Sample release value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    NumericValueInput sampleStartValue {
        sampleStartMsSlider, "Sample start value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    NumericValueInput sampleEndValue {
        sampleEndMsSlider, "Sample end value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    NumericValueInput sampleLoopBpmValueInput {
        sampleLoopBpmSlider, "Sample target BPM", NumericValueInput::Validation::signedDecimal, " BPM"
    };
    juce::Label sampleLoopBpmLabel;
    juce::Label sampleOriginalBpmLabel;
    juce::TextButton sampleTempoSyncButton { "TEMPO SYNC" };
    juce::TextButton sampleLoopModeButton { "PRESERVE PITCH" };
    juce::Label rzhavLabel;
    juce::Label sustainLabel;
    juce::Label ottLabel;
    juce::Label sampleGainLabel;
    juce::Label samplePunchLabel;
    juce::Label samplePitchLabel;
    juce::Label sampleFormantLabel;
    juce::Label sampleMonoLabel;
    juce::Label samplePanLabel;
    juce::Label sampleAttackLabel;
    juce::Label sampleReleaseLabel;
    juce::Label sampleStartLabel;
    juce::Label sampleEndLabel;
    juce::Label sampleBeatSnapLabel;
    juce::ComboBox sampleBeatSnapTarget;
    juce::TextButton sampleBeatSnapButton { "BEAT SNAP" };
    juce::TextButton applyToAllButton { "APPLY TO ALL" };
    juce::ComboBox applyToAllEffectSelector;
    SampleGroupSelector sampleGroupSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rzhavAttachment;

    // std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bitDepthAttachment;
    // std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ottAttachment;
    std::unique_ptr<juce::ParameterAttachment> noiseSourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> noiseTriggerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseModulationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePunchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleFormantAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleMonoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePanAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleLoopBpmAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sampleLoopModeAttachment;

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
    void updateSampleTrimInputs();
    void commitSampleTrimInput(juce::Slider&, const char* parameterId);
    juce::RangedAudioParameter* getSelectedApplyToAllParameter() const;
    void selectApplyToAllEffect(juce::RangedAudioParameter& parameter);
    void refreshApplyToAllButton();
    void setSampleEffectsModalVisible(bool shouldBeVisible);
    void updateSampleEditorTabVisibility();
    void openNoiseFileChooser();
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    float getParameterDefaultValue(const juce::String& parameterId) const;
    void rebuildSampleGroupActivityMap();
    void selectSampleGroupForEditing(int groupIndex);
    void timerCallback() override;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;
    std::unique_ptr<juce::Component> sampleEffectsModalSurface;
    std::unique_ptr<juce::Button> sampleEffectsCloseButton;
    juce::TextButton effectsTabButton { "EFFECTS" };
    juce::TextButton sampleTabButton { "SAMPLE" };
    SampleEqualiserEditor equaliserEditor;
    SampleWaveformEditor sampleWaveformEditor;
    NoiseWaveformEditor noiseWaveform;
    std::unique_ptr<juce::Component> noisePanelSurface;
    std::unique_ptr<juce::FileChooser> noiseFileChooser;
    juce::TextButton editSamplesButton { "EDIT SAMPLES" };
    std::unique_ptr<CustomLookAndFeel> customLNF;
    std::vector<SampleSpecificSliderBinding> sampleSpecificSliderBindings;
    std::vector<SampleSpecificEditBinding> sampleSpecificEditBindings;
    std::vector<juce::RangedAudioParameter*> applyToAllParameters;
    // Capture at editor construction: JUCE's message-thread query takes a mutex.
    const juce::Thread::ThreadID editorThreadId { juce::Thread::getCurrentThreadId() };
    juce::RangedAudioParameter* pendingApplyToAllParameter = nullptr;
    bool ignoreSampleSpecificEdits = false;
    bool ignoreSampleTrimInputChanges = false;
    std::array<int, AudioPluginAudioProcessor::midiNoteActivityCount> sampleGroupIndexByMidiNote {};
    std::array<uint32_t, AudioPluginAudioProcessor::midiNoteActivityCount> observedMidiNoteActivityGenerations {};
    bool sampleEffectsModalOpen = false;
    bool effectsTabSelected = true;
    bool noiseFileDragActive = false;
    int displayedSampleGroupIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
