#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/PluginParameters.h"
#include "UI/CustomLookAndFeel.h"

#include <cmath>

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

    addAndMakeVisible(sampleGainSlider);
    sampleGainSlider.setComponentID(PluginUI::sampleGainSliderId);
    sampleGainSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sampleGainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sampleGainSlider.setLookAndFeel(customLNF.get());
    sampleGainSlider.setDoubleClickReturnValue(true, PluginParameters::sampleGainDbDefault);
    sampleGainSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(sampleGainSlider, PluginParameters::sampleGainDbId, sampleGainAttachment);

    addAndMakeVisible(sampleGainLabel);
    configureKnobLabel(sampleGainLabel, "Gain");

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

    addAndMakeVisible(sampleFormantSlider);
    sampleFormantSlider.setComponentID(PluginUI::sampleFormantSliderId);
    sampleFormantSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sampleFormantSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sampleFormantSlider.setLookAndFeel(customLNF.get());
    sampleFormantSlider.setDoubleClickReturnValue(true, PluginParameters::sampleFormantSemitonesDefault);
    sampleFormantSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(sampleFormantSlider, PluginParameters::sampleFormantSemitonesId, sampleFormantAttachment);

    addAndMakeVisible(sampleFormantLabel);
    configureKnobLabel(sampleFormantLabel, "Formant");

    addAndMakeVisible(applyToAllButton);
    applyToAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
    applyToAllButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    applyToAllButton.onClick = [this]
    {
        if (lastEditedSampleParameter == nullptr)
            return;

        const juce::ScopedValueSetter<bool> guard(ignoreSampleSpecificEdits, true);
        if (processorRef.applySampleSpecificParameterToAll(
                lastEditedSampleParameter->paramID, lastEditedSampleValue))
            refreshSampleSpecificControls();
    };
    addAndMakeVisible(applyToAllStatusLabel);
    configureKnobLabel(applyToAllStatusLabel, {});
    applyToAllStatusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    applyToAllStatusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);

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
        const juce::ScopedValueSetter<bool> guard(ignoreSampleSpecificEdits, true);
        processorRef.setSelectedSampleGroupIndex(selectedIndex);
        sampleGroupSelector.setSelectedIndex(processorRef.getSelectedSampleGroupIndex());
        refreshSampleSpecificControls();
    };
    addAndMakeVisible(sampleGroupSelector);

    for (const auto& definition : PluginParameters::sampleSpecificParameters)
        if (auto* parameter = processorRef.parameters.getParameter(definition.id))
        {
            sampleSpecificEditBindings.push_back(SampleSpecificEditBinding { parameter });
            parameter->addListener(this);
        }

    refreshSampleSpecificControls();
    refreshApplyToAllButton();
    startTimerHz(30);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    stopTimer();
    for (const auto& binding : sampleSpecificEditBindings)
        binding.parameter->removeListener(this);
    sampleGroupSelector.onSelectedIndexChanged = {};

    // Clear L&F pointers before destroying the owned look and feel.
    rzhavSlider.setLookAndFeel(nullptr);
    sustainSlider.setLookAndFeel(nullptr);
    sampleGainSlider.setLookAndFeel(nullptr);
    samplePunchSlider.setLookAndFeel(nullptr);
    samplePitchSlider.setLookAndFeel(nullptr);
    sampleFormantSlider.setLookAndFeel(nullptr);
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

    // Processor listeners mirror values even when this editor is closed.
}

void AudioPluginAudioProcessorEditor::refreshSampleSpecificControls()
{
    const juce::ScopedValueSetter<bool> guard(ignoreSampleSpecificEdits, true);
    displayedSampleGroupIndex = processorRef.getSelectedSampleGroupIndex();
    for (const auto& binding : sampleSpecificSliderBindings)
    {
        if (binding.slider == nullptr)
            continue;

        const float fallbackValue = getParameterDefaultValue(binding.parameterId);
        const float sampleValue = processorRef.getSampleSpecificParameterValue(binding.parameterId, fallbackValue);
        binding.slider->setValue(sampleValue, juce::sendNotificationSync);
    }
}

void AudioPluginAudioProcessorEditor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting)
{
    // Host callbacks may run on audio. Edit tracking and UI updates stay on the
    // message thread; ordinary automation/selection updates have no UI gesture.
    if (juce::Thread::getCurrentThreadId() != editorThreadId || ignoreSampleSpecificEdits)
        return;

    for (auto& binding : sampleSpecificEditBindings)
        if (binding.parameter->getParameterIndex() == parameterIndex)
        {
            binding.gestureInProgress = gestureIsStarting;
            binding.lastGestureValue = binding.parameter->getValue();
            return;
        }
}

void AudioPluginAudioProcessorEditor::parameterValueChanged(int parameterIndex, float newValue)
{
    if (juce::Thread::getCurrentThreadId() != editorThreadId || ignoreSampleSpecificEdits)
        return;

    if (!std::isfinite(newValue))
        return;

    for (auto& binding : sampleSpecificEditBindings)
        if (binding.parameter->getParameterIndex() == parameterIndex)
        {
            if (binding.gestureInProgress && binding.lastGestureValue != newValue)
            {
                binding.lastGestureValue = newValue;
                lastEditedSampleParameter = binding.parameter;
                lastEditedSampleValue = binding.parameter->convertFrom0to1(newValue);
                // JUCE may hold its parameter-listener lock here. Format text
                // and update components later, in the existing editor timer.
                applyToAllButtonNeedsRefresh = true;
            }
            return;
        }
}

void AudioPluginAudioProcessorEditor::refreshApplyToAllButton()
{
    applyToAllButtonNeedsRefresh = false;
    const bool canApply = lastEditedSampleParameter != nullptr && !processorRef.getSampleGroups().empty();
    applyToAllButton.setEnabled(canApply);

    juce::String description = "Edit a sample effect first";
    if (canApply)
    {
        description = lastEditedSampleParameter->getName(40) + ": "
            + lastEditedSampleParameter->getText(
                lastEditedSampleParameter->convertTo0to1(lastEditedSampleValue), 24);
        const auto unit = lastEditedSampleParameter->getLabel();
        if (unit.isNotEmpty())
            description += " " + unit;
    }
    applyToAllStatusLabel.setText(description, juce::dontSendNotification);
    applyToAllButton.setTooltip(canApply ? "Apply " + description + " to all samples."
                                       : "Change a sample-specific effect, then apply that value to all samples.");
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
    if (applyToAllButtonNeedsRefresh)
        refreshApplyToAllButton();

    if (displayedSampleGroupIndex != processorRef.getSelectedSampleGroupIndex())
    {
        sampleGroupSelector.setSelectedIndex(processorRef.getSelectedSampleGroupIndex());
        refreshSampleSpecificControls();
    }
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
    placeKnobWithLabel(sampleGainSlider, sampleGainLabel, getWidth() - labelWidth - 267, 222);
    placeKnobWithLabel(samplePunchSlider, samplePunchLabel, getWidth() - labelWidth - 186, 222);
    placeKnobWithLabel(samplePitchSlider, samplePitchLabel, getWidth() - labelWidth - 105, 222);
    placeKnobWithLabel(sampleFormantSlider, sampleFormantLabel, getWidth() - labelWidth - 24, 222);
    applyToAllButton.setBounds(getWidth() - 201, 320, 177, 28);
    applyToAllStatusLabel.setBounds(getWidth() - 201, 352, 177, 18);
    warpButton.setBounds(23, 18, 170, 110);

    const int selectorHeight = SampleGroupSelector::getPreferredHeight();
    sampleGroupSelector.setBounds(0,
                                  getHeight() - selectorHeight,
                                  getWidth(),
                                  selectorHeight);
}
