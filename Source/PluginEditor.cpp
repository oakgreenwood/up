#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/PluginParameters.h"
#include "UI/CustomLookAndFeel.h"

namespace
{
void configureKnobLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::black);
    label.setFont(juce::Font(juce::FontOptions(15.0f)));
    label.setInterceptsMouseClicks(false, false);
}
}

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    juce::ignoreUnused (processorRef);
    setSize (900, 600);

    customLNF = std::make_unique<CustomLookAndFeel>();

    addAndMakeVisible(rzhavSlider);
    rzhavSlider.setComponentID(PluginUI::rzhavSliderId);
    rzhavSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    rzhavSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rzhavSlider.setLookAndFeel(customLNF.get());
    rzhavSlider.setDoubleClickReturnValue(true, 0.0);
    rzhavSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(rzhavSlider, PluginParameters::rzhavchinaId, rzhavAttachment);

    addAndMakeVisible(rzhavLabel);
    configureKnobLabel(rzhavLabel, "Rzhavchina");

    addAndMakeVisible(sustainSlider);
    sustainSlider.setComponentID(PluginUI::sustainSliderId);
    sustainSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sustainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sustainSlider.setLookAndFeel(customLNF.get());
    sustainSlider.setDoubleClickReturnValue(true, 0.0);
    sustainSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(sustainSlider, PluginParameters::sustainShortenId, sustainAttachment);

    addAndMakeVisible(sustainLabel);
    configureKnobLabel(sustainLabel, "Pomyatost");

    addAndMakeVisible(samplePunchSlider);
    samplePunchSlider.setComponentID(PluginUI::samplePunchSliderId);
    samplePunchSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    samplePunchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    samplePunchSlider.setLookAndFeel(customLNF.get());
    samplePunchSlider.setDoubleClickReturnValue(true, PluginParameters::samplePunchDefault);
    samplePunchSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(samplePunchSlider, PluginParameters::samplePunchId, samplePunchAttachment);

    addAndMakeVisible(samplePunchLabel);
    configureKnobLabel(samplePunchLabel, "Punch");

    addAndMakeVisible(samplePitchSlider);
    samplePitchSlider.setComponentID(PluginUI::samplePitchSliderId);
    samplePitchSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    samplePitchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    samplePitchSlider.setLookAndFeel(customLNF.get());
    samplePitchSlider.setDoubleClickReturnValue(true, PluginParameters::samplePitchSemitonesDefault);
    samplePitchSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(samplePitchSlider, PluginParameters::samplePitchSemitonesId, samplePitchAttachment);

    addAndMakeVisible(samplePitchLabel);
    configureKnobLabel(samplePitchLabel, "Pitch");

    addAndMakeVisible(warpButton);
    warpButton.setComponentID(PluginUI::tempoSyncButtonId);
    warpButton.setButtonText({});
    warpButton.setLookAndFeel(customLNF.get());
    warpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
    processorRef.parameters, PluginParameters::warpEnabledId, warpButton);

    sampleGroupSelector.setSampleGroups(processorRef.getSampleGroups());
    rebuildSampleGroupActivityMap();
    sampleGroupSelector.setSelectedIndex(processorRef.getSelectedSampleGroupIndex());
    sampleGroupSelector.onSelectedIndexChanged = [this] (int selectedIndex)
    {
        processorRef.setSelectedSampleGroupIndex(selectedIndex);
        sampleGroupSelector.setSelectedIndex(processorRef.getSelectedSampleGroupIndex());
        refreshSampleSpecificControls();
    };
    addAndMakeVisible(sampleGroupSelector);

    refreshSampleSpecificControls();
    startTimerHz(30);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    stopTimer();
    sampleGroupSelector.onSelectedIndexChanged = {};

    // Clear L&F pointers before destroying the owned look and feel.
    rzhavSlider.setLookAndFeel(nullptr);
    sustainSlider.setLookAndFeel(nullptr);
    samplePunchSlider.setLookAndFeel(nullptr);
    samplePitchSlider.setLookAndFeel(nullptr);
    warpButton.setLookAndFeel(nullptr);
    customLNF.reset();
}

void AudioPluginAudioProcessorEditor::bindSliderToParameter(
    juce::Slider& slider,
    const juce::String& parameterId,
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters,
        parameterId,
        slider);

    if (!PluginParameters::isSampleSpecificParameterId(parameterId))
        return;

    sampleSpecificSliderBindings.push_back(SampleSpecificSliderBinding { &slider, parameterId });

    auto* sliderPtr = &slider;
    slider.onValueChange = [this, sliderPtr, parameterId]
    {
        if (refreshingSampleSpecificControls)
            return;

        processorRef.setSampleSpecificParameterValue(parameterId, static_cast<float>(sliderPtr->getValue()));
    };
}

void AudioPluginAudioProcessorEditor::refreshSampleSpecificControls()
{
    refreshingSampleSpecificControls = true;

    for (const auto& binding : sampleSpecificSliderBindings)
    {
        if (binding.slider == nullptr)
            continue;

        const float fallbackValue = getParameterDefaultValue(binding.parameterId);
        const float sampleValue = processorRef.getSampleSpecificParameterValue(binding.parameterId, fallbackValue);
        binding.slider->setValue(sampleValue, juce::sendNotificationSync);
    }

    refreshingSampleSpecificControls = false;
}

float AudioPluginAudioProcessorEditor::getParameterDefaultValue(const juce::String& parameterId) const
{
    if (auto* parameter = processorRef.parameters.getParameter(parameterId))
        return parameter->convertFrom0to1(parameter->getDefaultValue());

    return 0.0f;
}

void AudioPluginAudioProcessorEditor::rebuildSampleGroupActivityMap()
{
    sampleGroupIndexByMidiNote.fill(-1);
    observedMidiNoteActivityGenerations.fill(0);

    const auto& sampleGroups = processorRef.getSampleGroups();
    for (int groupIndex = 0; groupIndex < static_cast<int>(sampleGroups.size()); ++groupIndex)
    {
        const int midiNote = sampleGroups[(size_t) groupIndex].midiNote;
        if (midiNote >= 0 && midiNote < AudioPluginAudioProcessor::midiNoteActivityCount)
            sampleGroupIndexByMidiNote[(size_t) midiNote] = groupIndex;
    }
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    for (int midiNote = 0; midiNote < AudioPluginAudioProcessor::midiNoteActivityCount; ++midiNote)
    {
        const auto noteIndex = (size_t) midiNote;
        const uint32_t generation = processorRef.getMidiNoteActivityGeneration(midiNote);
        if (observedMidiNoteActivityGenerations[noteIndex] == generation)
            continue;

        observedMidiNoteActivityGenerations[noteIndex] = generation;

        const int groupIndex = sampleGroupIndexByMidiNote[noteIndex];
        if (groupIndex >= 0)
        {
            sampleGroupSelector.setActivityVelocity(
                groupIndex,
                processorRef.getMidiNoteActivityVelocity(midiNote));
        }
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colours::white);
}

void AudioPluginAudioProcessorEditor::resized()
{
    constexpr int knobSize = 47;
    constexpr int labelWidth = 96;
    constexpr int labelHeight = 22;
    constexpr int labelGap = 11;

    const auto placeKnobWithLabel = [=] (juce::Slider& slider,
                                         juce::Label& label,
                                         int labelLeft,
                                         int knobCentreY)
    {
        const int knobCentreX = labelLeft + labelWidth / 2;

        slider.setBounds(knobCentreX - knobSize / 2,
                         knobCentreY - knobSize / 2,
                         knobSize,
                         knobSize);

        label.setBounds(labelLeft,
                        knobCentreY + knobSize / 2 + labelGap,
                        labelWidth,
                        labelHeight);
    };

    placeKnobWithLabel(rzhavSlider, rzhavLabel, 0, 222);
    placeKnobWithLabel(sustainSlider, sustainLabel, 81, 222);
    placeKnobWithLabel(samplePunchSlider, samplePunchLabel, getWidth() - labelWidth - 105, 222);
    placeKnobWithLabel(samplePitchSlider, samplePitchLabel, getWidth() - labelWidth - 24, 222);
    warpButton.setBounds(23, 18, 170, 110);

    const int selectorHeight = SampleGroupSelector::getPreferredHeight();
    sampleGroupSelector.setBounds(0,
                                  getHeight() - selectorHeight,
                                  getWidth(),
                                  selectorHeight);
}
