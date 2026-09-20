#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/PluginParameters.h"
#include "UI/CustomLookAndFeel.h"

#include <cmath>

namespace
{
constexpr uint32_t midiSampleSelectionDebounceMs = 500;
constexpr int editorPadding = 12;

class SampleEffectsModalSurface final : public juce::Component
{
public:
    SampleEffectsModalSurface()
    {
        setName("Sample effects modal");
        setOpaque(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xffd9d9d9));
    }
};

class SampleEffectsCloseButton final : public juce::Button
{
public:
    SampleEffectsCloseButton()
        : juce::Button("Close sample effects")
    {
        setTooltip("Close sample effects");
        setWantsKeyboardFocus(true);
    }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        const auto colour = juce::Colours::black.withAlpha(isButtonDown ? 0.55f
                                                        : (isMouseOver ? 0.8f : 1.0f));
        const auto bounds = getLocalBounds().toFloat().reduced(5.0f);
        g.setColour(colour);
        g.drawLine(bounds.getTopLeft().getX(), bounds.getTopLeft().getY(),
                   bounds.getBottomRight().getX(), bounds.getBottomRight().getY(), 2.0f);
        g.drawLine(bounds.getTopRight().getX(), bounds.getTopRight().getY(),
                   bounds.getBottomLeft().getX(), bounds.getBottomLeft().getY(), 2.0f);
    }
};

int getArrowDirection(const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown())
        return 0;

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::rightKey)
        return 1;
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey)
        return -1;
    return 0;
}

bool nudgeSliderFromArrow(juce::Slider& slider,
                          const juce::KeyPress& key,
                          double valueBeforeNudge)
{
    const int direction = getArrowDirection(key);
    const double interval = slider.getInterval();
    if (direction == 0 || interval <= 0.0)
        return false;

    const juce::Slider::ScopedDragNotification gesture(slider);
    slider.setValue(valueBeforeNudge + direction * interval, juce::sendNotificationSync);
    return true;
}

bool normaliseSignedDecimalInput(const juce::String& text,
                                 const juce::String& displaySuffix,
                                 juce::String& normalised)
{
    const bool hasDisplaySuffix = displaySuffix.isNotEmpty() && text.endsWith(displaySuffix);
    const auto numericText = hasDisplaySuffix
        ? text.dropLastCharacters(displaySuffix.length())
        : text;

    int index = 0;
    if (numericText.startsWithChar('+') || numericText.startsWithChar('-'))
        ++index;

    int integerDigits = 0;
    int decimalPoint = -1;

    for (; index < numericText.length(); ++index)
    {
        const auto character = numericText[index];
        if (character >= '0' && character <= '9')
        {
            if (decimalPoint < 0)
                ++integerDigits;
            continue;
        }

        if (character == '.' && decimalPoint < 0 && integerDigits > 0)
        {
            decimalPoint = index;
            continue;
        }

        return false;
    }

    // Validate the entire text before dropping excess fractional digits, so a
    // malformed paste never becomes a different, apparently valid number.
    normalised = decimalPoint < 0
        ? numericText
        : numericText.substring(0, decimalPoint + 2);
    if (hasDisplaySuffix)
        normalised += displaySuffix;
    return normalised.length() <= 16;
}

bool parseSignedDecimalInput(const juce::String& text,
                             const juce::String& displaySuffix,
                             double& parsedValue)
{
    juce::String normalised;
    if (!normaliseSignedDecimalInput(text, displaySuffix, normalised))
        return false;

    if (displaySuffix.isNotEmpty() && normalised.endsWith(displaySuffix))
        normalised = normalised.dropLastCharacters(displaySuffix.length());

    if (normalised.isEmpty() || normalised == "+" || normalised == "-"
        || normalised.endsWithChar('.'))
        return false;

    parsedValue = normalised.getDoubleValue();
    return std::isfinite(parsedValue);
}

bool normalisePercentageInput(const juce::String& text, juce::String& normalised)
{
    if (text.length() > 16)
        return false;

    const bool hasSuffix = text.endsWithChar('%');
    const auto numericText = hasSuffix ? text.dropLastCharacters(1) : text;

    int percentage = 0;
    for (int index = 0; index < numericText.length(); ++index)
    {
        const auto character = numericText[index];
        if (character < '0' || character > '9')
            return false;

        // Check each digit before the accumulator can overflow on a long paste.
        percentage = percentage * 10 + static_cast<int>(character - '0');
        if (percentage > 100)
            return false;
    }

    normalised = numericText + (hasSuffix ? "%" : "");
    return true;
}

bool parsePercentageInput(const juce::String& text, double& parsedValue)
{
    juce::String normalised;
    if (!normalisePercentageInput(text, normalised))
        return false;

    if (normalised.endsWithChar('%'))
        normalised = normalised.dropLastCharacters(1);

    if (normalised.isEmpty())
        return false;

    parsedValue = normalised.getDoubleValue();
    return std::isfinite(parsedValue) && parsedValue >= 0.0 && parsedValue <= 100.0;
}

bool normalisePanoramaInput(const juce::String& text, juce::String& normalised)
{
    if (text.length() > 16)
        return false;

    normalised = text.toUpperCase();
    if (normalised == "C")
        return true;

    const bool hasDirection = normalised.endsWithChar('L') || normalised.endsWithChar('R');
    const auto numericText = hasDirection ? normalised.dropLastCharacters(1) : normalised;
    const bool hasSign = numericText.startsWithChar('-') || numericText.startsWithChar('+');
    if (hasDirection && hasSign)
        return false;

    int magnitude = 0;
    for (int index = hasSign ? 1 : 0; index < numericText.length(); ++index)
    {
        const auto character = numericText[index];
        if (character < '0' || character > '9')
            return false;

        magnitude = magnitude * 10 + static_cast<int>(character - '0');
        if (magnitude > static_cast<int>(PluginParameters::samplePanMaximum))
            return false;
    }

    // Empty text, a sign or a direction alone can occur during an edit.
    return true;
}

bool parsePanoramaInput(const juce::String& text, double& parsedValue)
{
    juce::String normalised;
    if (!normalisePanoramaInput(text, normalised))
        return false;
    if (normalised == "C")
    {
        parsedValue = 0.0;
        return true;
    }

    const bool isLeft = normalised.endsWithChar('L');
    if (isLeft || normalised.endsWithChar('R'))
        normalised = normalised.dropLastCharacters(1);
    if (normalised.isEmpty() || normalised == "-" || normalised == "+")
        return false;

    parsedValue = normalised.getDoubleValue() * (isLeft ? -1.0 : 1.0);
    return true;
}

class NumericTextEditor final : public juce::TextEditor
{
public:
    NumericTextEditor(const juce::String& name,
                      NumericValueInput::Validation inputValidation,
                      const juce::String& suffix,
                      juce::Slider& controlledSlider)
        : juce::TextEditor(name),
          validation(inputValidation),
          displaySuffix(suffix),
          slider(controlledSlider) {}

    void insertTextAtCaret(const juce::String& insertion) override
    {
        const auto currentText = getText();
        const auto selection = getHighlightedRegion();
        const auto candidate = currentText.replaceSection(selection.getStart(),
                                                          selection.getLength(), insertion);
        juce::String normalised;
        const bool isValid = validation == NumericValueInput::Validation::percentage
            ? normalisePercentageInput(candidate, normalised)
            : (validation == NumericValueInput::Validation::panorama
                ? normalisePanoramaInput(candidate, normalised)
                : normaliseSignedDecimalInput(candidate, displaySuffix, normalised));
        if (!isValid || normalised == currentText)
            return;

        // Keep the original selection on rejection. For accepted edits, replace
        // the suffix through JUCE's normal insertion path to retain undo/redo.
        const int caret = juce::jmin(selection.getStart() + insertion.length(), normalised.length());
        setHighlightedRegion({ selection.getStart(), currentText.length() });
        juce::TextEditor::insertTextAtCaret(normalised.substring(selection.getStart()));
        setCaretPosition(caret);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const double valueBeforeNudge = slider.getValueFromText(getText());
        if (!nudgeSliderFromArrow(slider, key, valueBeforeNudge))
            return juce::TextEditor::keyPressed(key);

        setText(slider.getTextFromValue(slider.getValue()), false);
        setHighlightedRegion({ 0, getText().length() });
        return true;
    }

private:
    const NumericValueInput::Validation validation;
    const juce::String displaySuffix;
    juce::Slider& slider;
};

void configureKnobLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::black);
    label.setFont(juce::Font(juce::FontOptions(15.0f)));
    label.setInterceptsMouseClicks(false, false);
}

void configureSignedDecimalValueInput(juce::Slider& slider, const juce::String& displaySuffix)
{
    // SliderAttachment replaces these functions, so configure them after binding.
    slider.setNumDecimalPlacesToDisplay(1);
    slider.textFromValueFunction = [displaySuffix] (double value)
    {
        if (std::abs(value) < 0.05)
            value = 0.0;

        return juce::String(value >= 0.0 ? "+" : "")
            + juce::String(value, 1) + displaySuffix;
    };
    auto* sliderPointer = &slider;
    slider.valueFromTextFunction = [sliderPointer, displaySuffix] (const juce::String& text)
    {
        double value = 0.0;
        return parseSignedDecimalInput(text, displaySuffix, value)
            ? value
            : sliderPointer->getValue();
    };
    slider.updateText();
}

void configurePercentageValueInput(juce::Slider& slider)
{
    slider.setNumDecimalPlacesToDisplay(0);
    slider.textFromValueFunction = [] (double value)
    {
        const double percentage = juce::jlimit(0.0, 1.0, value) * 100.0;
        return juce::String(percentage, 0) + "%";
    };
    auto* sliderPointer = &slider;
    slider.valueFromTextFunction = [sliderPointer] (const juce::String& text)
    {
        double percentage = 0.0;
        return parsePercentageInput(text, percentage)
            ? percentage / 100.0
            : sliderPointer->getValue();
    };
    slider.updateText();
}

void configurePanoramaValueInput(juce::Slider& slider)
{
    slider.setNumDecimalPlacesToDisplay(0);
    slider.textFromValueFunction = [] (double value)
    {
        const int position = juce::roundToInt(juce::jlimit(
            static_cast<double>(PluginParameters::samplePanMinimum),
            static_cast<double>(PluginParameters::samplePanMaximum), value));
        return position == 0 ? juce::String("C")
                             : juce::String(std::abs(position)) + (position < 0 ? "L" : "R");
    };
    auto* sliderPointer = &slider;
    slider.valueFromTextFunction = [sliderPointer] (const juce::String& text)
    {
        double position = 0.0;
        return parsePanoramaInput(text, position) ? position : sliderPointer->getValue();
    };
    slider.updateText();
}
}

bool ImageKnobSlider::keyPressed(const juce::KeyPress& key)
{
    if (getArrowDirection(key) == 0)
        return juce::Slider::keyPressed(key);

    const juce::Slider::ScopedDragNotification gesture(*this);
    return juce::Slider::keyPressed(key);
}

NumericValueInput::NumericValueInput(juce::Slider& controlledSlider,
                                     const juce::String& name,
                                     Validation inputValidation,
                                     const juce::String& suffix)
    : juce::Label(name, {}),
      slider(controlledSlider),
      validation(inputValidation),
      displaySuffix(suffix)
{
    setEditable(true, true, false);
    setJustificationType(juce::Justification::centred);
    setFont(juce::Font(juce::FontOptions(15.0f)));
    setColour(juce::Label::textColourId, juce::Colours::black);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setKeyboardType(juce::TextInputTarget::decimalKeyboard);
    slider.addListener(this);
    refreshValue();
}

NumericValueInput::~NumericValueInput()
{
    slider.removeListener(this);
}

juce::TextEditor* NumericValueInput::createEditorComponent()
{
    auto editor = std::make_unique<NumericTextEditor>(getName(), validation, displaySuffix, slider);
    editor->setFont(getFont());
    editor->setJustification(juce::Justification::centred);
    editor->setColour(juce::TextEditor::textColourId, juce::Colours::black);
    editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xffe6e6e6));
    editor->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff808080));
    editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::black);
    editor->setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    editor->setCaretVisible(false);
    // Label takes ownership of the editor returned by this JUCE factory hook.
    return editor.release();
}

bool NumericValueInput::keyPressed(const juce::KeyPress& key)
{
    return nudgeSliderFromArrow(slider, key, slider.getValue())
        || juce::Label::keyPressed(key);
}

void NumericValueInput::textEditorReturnKeyPressed(juce::TextEditor& editor)
{
    commitEdit(editor.getText());
}

void NumericValueInput::textEditorEscapeKeyPressed(juce::TextEditor&)
{
    discardEdit();
}

void NumericValueInput::textEditorFocusLost(juce::TextEditor& editor)
{
    commitEdit(editor.getText());
}

void NumericValueInput::commitEdit(const juce::String& text)
{
    const bool textChanged = text != getText();
    hideEditor(true);
    // Merely opening and closing a field must not overwrite new automation.
    if (textChanged)
    {
        const double value = slider.getNormalisableRange().snapToLegalValue(slider.getValueFromText(text));
        if (value != slider.getValue())
        {
            const juce::Slider::ScopedDragNotification gesture(slider);
            slider.setValue(value, juce::sendNotificationSync);
        }
    }
    refreshValue();
}

void NumericValueInput::discardEdit()
{
    hideEditor(true);
    refreshValue();
}

void NumericValueInput::sliderValueChanged(juce::Slider*)
{
    if (!isBeingEdited())
        refreshValue();
}

void NumericValueInput::refreshValue()
{
    setText(slider.getTextFromValue(slider.getValue()), juce::dontSendNotification);
}

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      equaliserEditor(p)
{
    juce::ignoreUnused (processorRef);
    setSize (900, 600);
    setWantsKeyboardFocus(true);

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

    addAndMakeVisible(ottSlider);
    ottSlider.setComponentID(PluginUI::ottSliderId);
    ottSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    ottSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    ottSlider.setLookAndFeel(customLNF.get());
    ottSlider.setDoubleClickReturnValue(true, PluginParameters::ottAmountDefault);
    ottSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(ottSlider, PluginParameters::ottAmountId, ottAttachment);

    addAndMakeVisible(ottLabel);
    configureKnobLabel(ottLabel, "OTT");

    sampleEffectsModalSurface = std::make_unique<SampleEffectsModalSurface>();
    addAndMakeVisible(*sampleEffectsModalSurface);

    addAndMakeVisible(sampleGainSlider);
    sampleGainSlider.setComponentID(PluginUI::sampleGainSliderId);
    sampleGainSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sampleGainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sampleGainSlider.setLookAndFeel(customLNF.get());
    sampleGainSlider.setDoubleClickReturnValue(true, PluginParameters::sampleGainDbDefault);
    sampleGainSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(sampleGainSlider, PluginParameters::sampleGainDbId, sampleGainAttachment, &sampleGainLabel);
    configureSignedDecimalValueInput(sampleGainSlider, " dB");
    sampleGainValueInput.discardEdit();
    addAndMakeVisible(sampleGainValueInput);

    addAndMakeVisible(sampleGainLabel);
    configureKnobLabel(sampleGainLabel, "Gain");

    addAndMakeVisible(samplePunchSlider);
    samplePunchSlider.setComponentID(PluginUI::samplePunchSliderId);
    samplePunchSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    samplePunchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    samplePunchSlider.setLookAndFeel(customLNF.get());
    samplePunchSlider.setDoubleClickReturnValue(true, PluginParameters::samplePunchDefault);
    samplePunchSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(samplePunchSlider, PluginParameters::samplePunchId, samplePunchAttachment, &samplePunchLabel);
    configurePercentageValueInput(samplePunchSlider);
    samplePunchValueInput.discardEdit();
    addAndMakeVisible(samplePunchValueInput);

    addAndMakeVisible(samplePunchLabel);
    configureKnobLabel(samplePunchLabel, "Punch");

    addAndMakeVisible(samplePitchSlider);
    samplePitchSlider.setComponentID(PluginUI::samplePitchSliderId);
    samplePitchSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    samplePitchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    samplePitchSlider.setLookAndFeel(customLNF.get());
    samplePitchSlider.setDoubleClickReturnValue(true, PluginParameters::samplePitchSemitonesDefault);
    samplePitchSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(samplePitchSlider, PluginParameters::samplePitchSemitonesId, samplePitchAttachment, &samplePitchLabel);
    configureSignedDecimalValueInput(samplePitchSlider, " st");
    samplePitchValueInput.discardEdit();
    addAndMakeVisible(samplePitchValueInput);

    addAndMakeVisible(samplePitchLabel);
    configureKnobLabel(samplePitchLabel, "Pitch");

    addAndMakeVisible(sampleFormantSlider);
    sampleFormantSlider.setComponentID(PluginUI::sampleFormantSliderId);
    sampleFormantSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sampleFormantSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sampleFormantSlider.setLookAndFeel(customLNF.get());
    sampleFormantSlider.setDoubleClickReturnValue(true, PluginParameters::sampleFormantSemitonesDefault);
    sampleFormantSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(sampleFormantSlider, PluginParameters::sampleFormantSemitonesId, sampleFormantAttachment, &sampleFormantLabel);
    configureSignedDecimalValueInput(sampleFormantSlider, " st");
    sampleFormantValueInput.discardEdit();
    addAndMakeVisible(sampleFormantValueInput);

    addAndMakeVisible(sampleFormantLabel);
    configureKnobLabel(sampleFormantLabel, "Formant");

    addAndMakeVisible(sampleMonoSlider);
    sampleMonoSlider.setComponentID(PluginUI::sampleMonoSliderId);
    sampleMonoSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sampleMonoSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sampleMonoSlider.setLookAndFeel(customLNF.get());
    sampleMonoSlider.setDoubleClickReturnValue(true, PluginParameters::sampleMonoAmountDefault);
    sampleMonoSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(sampleMonoSlider, PluginParameters::sampleMonoAmountId, sampleMonoAttachment, &sampleMonoLabel);
    configurePercentageValueInput(sampleMonoSlider);
    sampleMonoValueInput.discardEdit();
    addAndMakeVisible(sampleMonoValueInput);

    addAndMakeVisible(sampleMonoLabel);
    configureKnobLabel(sampleMonoLabel, "Mono");

    addAndMakeVisible(samplePanSlider);
    samplePanSlider.setComponentID(PluginUI::samplePanSliderId);
    samplePanSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    samplePanSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    samplePanSlider.setLookAndFeel(customLNF.get());
    samplePanSlider.setDoubleClickReturnValue(true, PluginParameters::samplePanDefault);
    samplePanSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(samplePanSlider, PluginParameters::samplePanId, samplePanAttachment, &samplePanLabel);
    configurePanoramaValueInput(samplePanSlider);
    samplePanValueInput.discardEdit();
    addAndMakeVisible(samplePanValueInput);

    addAndMakeVisible(samplePanLabel);
    configureKnobLabel(samplePanLabel, "Panorama");

    addAndMakeVisible(equaliserEditor);
    equaliserEditor.onSelected = [this]
    {
        pendingApplyToAllParameter = nullptr;
        if (auto* parameter = processorRef.parameters.getParameter(PluginParameters::sampleEqFrequencyIds[0]))
            selectApplyToAllEffect(*parameter);
    };

    addAndMakeVisible(applyToAllButton);
    applyToAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
    applyToAllButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    applyToAllButton.onClick = [this]
    {
        auto* parameter = getSelectedApplyToAllParameter();
        if (parameter == nullptr)
            return;

        const float fallbackValue = parameter->convertFrom0to1(parameter->getDefaultValue());
        const float currentValue = processorRef.getSampleSpecificParameterValue(
            parameter->paramID, fallbackValue);
        const juce::ScopedValueSetter<bool> guard(ignoreSampleSpecificEdits, true);
        if (processorRef.applySampleSpecificParameterToAll(parameter->paramID, currentValue))
            refreshSampleSpecificControls();
    };

    addAndMakeVisible(applyToAllEffectSelector);
    applyToAllEffectSelector.setName("Effect to apply to all samples");
    applyToAllEffectSelector.setTextWhenNothingSelected("SELECT EFFECT");
    applyToAllEffectSelector.setTooltip("Choose which sample-specific effect to apply to all samples.");
    applyToAllEffectSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    applyToAllEffectSelector.setColour(juce::ComboBox::textColourId, juce::Colours::black);
    applyToAllEffectSelector.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    applyToAllEffectSelector.setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    applyToAllEffectSelector.onChange = [this]
    {
        pendingApplyToAllParameter = nullptr;
        refreshApplyToAllButton();
        grabKeyboardFocus();
    };

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
        midiSelectionBurstActive = false;
        pendingMidiSampleGroupIndex = -1;
        selectSampleGroupForEditing(selectedIndex);
    };
    addAndMakeVisible(sampleGroupSelector);

    addAndMakeVisible(editSamplesButton);
    editSamplesButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
    editSamplesButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    editSamplesButton.onClick = [this]
    {
        setSampleEffectsModalVisible(true);
    };

    sampleEffectsCloseButton = std::make_unique<SampleEffectsCloseButton>();
    addAndMakeVisible(*sampleEffectsCloseButton);
    sampleEffectsCloseButton->onClick = [this]
    {
        setSampleEffectsModalVisible(false);
    };

    for (const auto& definition : PluginParameters::sampleSpecificParameters)
        if (auto* parameter = processorRef.parameters.getParameter(definition.id))
        {
            sampleSpecificEditBindings.push_back(SampleSpecificEditBinding { parameter });
            if (definition.effectId == nullptr || parameter->paramID == definition.effectId)
            {
                applyToAllParameters.push_back(parameter);
                applyToAllEffectSelector.addItem(
                    definition.effectName != nullptr ? juce::String(definition.effectName) : parameter->getName(40),
                    static_cast<int>(applyToAllParameters.size()));
            }
            if (parameter->paramID == PluginParameters::sampleGainDbId)
                applyToAllEffectSelector.setSelectedId(
                    static_cast<int>(applyToAllParameters.size()), juce::dontSendNotification);
            parameter->addListener(this);
        }

    for (const auto& binding : sampleSpecificSliderBindings)
    {
        binding.slider->addMouseListener(this, false);
        if (binding.label != nullptr)
        {
            binding.label->setInterceptsMouseClicks(true, false);
            binding.label->addMouseListener(this, false);
        }
    }

    refreshSampleSpecificControls();
    refreshApplyToAllButton();
    resized();
    setSampleEffectsModalVisible(false);
    startTimerHz(30);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    equaliserEditor.endDrag();
    equaliserEditor.onSelected = {};
    stopTimer();
    for (const auto& binding : sampleSpecificSliderBindings)
    {
        binding.slider->removeMouseListener(this);
        if (binding.label != nullptr)
            binding.label->removeMouseListener(this);
    }
    for (const auto& binding : sampleSpecificEditBindings)
        binding.parameter->removeListener(this);
    sampleGroupSelector.onSelectedIndexChanged = {};
    applyToAllEffectSelector.onChange = {};
    editSamplesButton.onClick = {};
    if (sampleEffectsCloseButton != nullptr)
        sampleEffectsCloseButton->onClick = {};

    // Clear L&F pointers before destroying the owned look and feel.
    rzhavSlider.setLookAndFeel(nullptr);
    sustainSlider.setLookAndFeel(nullptr);
    ottSlider.setLookAndFeel(nullptr);
    sampleGainSlider.setLookAndFeel(nullptr);
    samplePunchSlider.setLookAndFeel(nullptr);
    samplePitchSlider.setLookAndFeel(nullptr);
    sampleFormantSlider.setLookAndFeel(nullptr);
    sampleMonoSlider.setLookAndFeel(nullptr);
    samplePanSlider.setLookAndFeel(nullptr);
    warpButton.setLookAndFeel(nullptr);
    customLNF.reset();
}

void AudioPluginAudioProcessorEditor::bindSliderToParameter(
    juce::Slider& slider,
    const juce::String& parameterId,
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
    juce::Label* label)
{
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters,
        parameterId,
        slider);

    if (!PluginParameters::isSampleSpecificParameterId(parameterId))
        return;

    sampleSpecificSliderBindings.push_back(SampleSpecificSliderBinding { &slider, parameterId, label });

    // Processor listeners mirror values even when this editor is closed.
}

void AudioPluginAudioProcessorEditor::refreshSampleSpecificControls()
{
    const juce::ScopedValueSetter<bool> guard(ignoreSampleSpecificEdits, true);
    equaliserEditor.selectionChanged();
    // An edit for the previous sample must never be committed to a new selection.
    sampleGainValueInput.discardEdit();
    samplePunchValueInput.discardEdit();
    samplePitchValueInput.discardEdit();
    sampleFormantValueInput.discardEdit();
    sampleMonoValueInput.discardEdit();
    samplePanValueInput.discardEdit();
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
                // JUCE may hold its parameter-listener lock here. Change the
                // ComboBox later, in the existing editor timer.
                pendingApplyToAllParameter = binding.parameter;
            }
            return;
        }
}

juce::RangedAudioParameter* AudioPluginAudioProcessorEditor::getSelectedApplyToAllParameter() const
{
    const int parameterIndex = applyToAllEffectSelector.getSelectedId() - 1;
    if (parameterIndex < 0 || parameterIndex >= static_cast<int>(applyToAllParameters.size()))
        return nullptr;

    return applyToAllParameters[(size_t) parameterIndex];
}

void AudioPluginAudioProcessorEditor::selectApplyToAllEffect(juce::RangedAudioParameter& parameter)
{
    const auto* definition = PluginParameters::findSampleSpecificParameter(parameter.paramID);
    const auto effectId = definition != nullptr && definition->effectId != nullptr
        ? juce::String(definition->effectId) : parameter.paramID;
    for (int parameterIndex = 0; parameterIndex < static_cast<int>(applyToAllParameters.size()); ++parameterIndex)
        if (applyToAllParameters[(size_t) parameterIndex]->paramID == effectId)
        {
            applyToAllEffectSelector.setSelectedId(parameterIndex + 1, juce::dontSendNotification);
            refreshApplyToAllButton();
            return;
        }
}

void AudioPluginAudioProcessorEditor::refreshApplyToAllButton()
{
    auto* parameter = getSelectedApplyToAllParameter();
    const bool canApply = parameter != nullptr && !processorRef.getSampleGroups().empty();
    applyToAllButton.setEnabled(canApply);
    applyToAllButton.setTooltip(canApply
        ? juce::String("Apply the selected sample's current ")
            + applyToAllEffectSelector.getText() + " settings to all samples."
        : "Select or edit a sample-specific effect to apply it to all samples.");
}

void AudioPluginAudioProcessorEditor::setSampleEffectsModalVisible(bool shouldBeVisible)
{
    sampleEffectsModalOpen = shouldBeVisible;
    editSamplesButton.setVisible(!shouldBeVisible);

    if (!shouldBeVisible)
        equaliserEditor.endDrag();

    if (sampleEffectsModalSurface != nullptr)
        sampleEffectsModalSurface->setVisible(shouldBeVisible);
    if (sampleEffectsCloseButton != nullptr)
        sampleEffectsCloseButton->setVisible(shouldBeVisible);

    juce::Component* const sampleEffectComponents[] = {
        &sampleGainSlider, &sampleGainValueInput, &sampleGainLabel,
        &samplePunchSlider, &samplePunchValueInput, &samplePunchLabel,
        &samplePitchSlider, &samplePitchValueInput, &samplePitchLabel,
        &sampleFormantSlider, &sampleFormantValueInput, &sampleFormantLabel,
        &sampleMonoSlider, &sampleMonoValueInput, &sampleMonoLabel,
        &samplePanSlider, &samplePanValueInput, &samplePanLabel,
        &equaliserEditor, &applyToAllButton, &applyToAllEffectSelector
    };
    for (auto* component : sampleEffectComponents)
        component->setVisible(shouldBeVisible);

    juce::Component* const globalEffectComponents[] = {
        &rzhavSlider, &rzhavLabel,
        &sustainSlider, &sustainLabel,
        &ottSlider, &ottLabel,
        &warpButton
    };
    for (auto* component : globalEffectComponents)
        component->setVisible(!shouldBeVisible);

    repaint();
}

void AudioPluginAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
        return;

    // Child controls receive their own mouse events. Only an actual click on
    // the otherwise empty editor background dismisses the modal.
    if (sampleEffectsModalOpen && event.eventComponent == this)
    {
        setSampleEffectsModalVisible(false);
        return;
    }

    for (const auto& binding : sampleSpecificSliderBindings)
        if (event.eventComponent == binding.slider || event.eventComponent == binding.label)
            if (auto* parameter = processorRef.parameters.getParameter(binding.parameterId))
            {
                grabKeyboardFocus();
                // The click takes precedence over any edit queued before focus moved.
                pendingApplyToAllParameter = nullptr;
                selectApplyToAllEffect(*parameter);
                return;
            }
}

bool AudioPluginAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    auto* parameter = getSelectedApplyToAllParameter();
    if (parameter == nullptr)
        return juce::AudioProcessorEditor::keyPressed(key);

    for (const auto& binding : sampleSpecificSliderBindings)
        if (binding.slider != nullptr && binding.parameterId == parameter->paramID)
            return nudgeSliderFromArrow(*binding.slider, key, binding.slider->getValue())
                || juce::AudioProcessorEditor::keyPressed(key);

    return juce::AudioProcessorEditor::keyPressed(key);
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

void AudioPluginAudioProcessorEditor::selectSampleGroupForEditing(int groupIndex)
{
    const juce::ScopedValueSetter<bool> guard(ignoreSampleSpecificEdits, true);
    if (groupIndex != processorRef.getSelectedSampleGroupIndex())
    {
        equaliserEditor.endDrag();
        processorRef.setSelectedSampleGroupIndex(groupIndex);
    }

    sampleGroupSelector.setSelectedIndex(processorRef.getSelectedSampleGroupIndex());
    if (displayedSampleGroupIndex != processorRef.getSelectedSampleGroupIndex())
        refreshSampleSpecificControls();
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    if (pendingApplyToAllParameter != nullptr)
    {
        auto* parameter = pendingApplyToAllParameter;
        pendingApplyToAllParameter = nullptr;
        selectApplyToAllEffect(*parameter);
    }

    const uint32_t latestNoteOnGeneration = processorRef.getLatestMidiNoteOnGeneration();
    if (observedLatestMidiNoteOnGeneration != latestNoteOnGeneration)
    {
        observedLatestMidiNoteOnGeneration = latestNoteOnGeneration;
        const int midiNote = processorRef.getLatestMidiNoteOnNote();
        if (midiNote >= 0 && midiNote < AudioPluginAudioProcessor::midiNoteActivityCount)
        {
            const int groupIndex = sampleGroupIndexByMidiNote[(size_t) midiNote];
            if (groupIndex >= 0)
            {
                pendingMidiSampleGroupIndex = groupIndex;
                lastMappedMidiNoteOnTimeMs = juce::Time::getMillisecondCounter();
                if (!midiSelectionBurstActive)
                {
                    midiSelectionBurstActive = true;
                    selectSampleGroupForEditing(groupIndex);
                }
            }
        }
    }

    if (midiSelectionBurstActive
        && static_cast<uint32_t>(juce::Time::getMillisecondCounter()
                                 - lastMappedMidiNoteOnTimeMs) >= midiSampleSelectionDebounceMs)
    {
        midiSelectionBurstActive = false;
        if (pendingMidiSampleGroupIndex >= 0)
            selectSampleGroupForEditing(pendingMidiSampleGroupIndex);
        pendingMidiSampleGroupIndex = -1;
    }

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
    constexpr int valueInputHeight = 20;
    constexpr int valueInputWidth = 76;
    constexpr int valueLabelGap = 3;

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

    const auto placeKnobWithValueAndLabel = [=] (juce::Slider& slider,
                                                  juce::Label& label,
                                                  NumericValueInput& input,
                                                  int labelLeft,
                                                  int knobCentreY)
    {
        placeKnobWithLabel(slider, label, labelLeft, knobCentreY);
        input.setBounds(labelLeft + (labelWidth - valueInputWidth) / 2,
                        label.getBottom() + valueLabelGap, valueInputWidth, valueInputHeight);
    };

    placeKnobWithLabel(rzhavSlider, rzhavLabel, 0, 222);
    placeKnobWithLabel(sustainSlider, sustainLabel, 81, 222);
    placeKnobWithLabel(ottSlider, ottLabel, 162, 222);

    const int modalHeight = getHeight() * 7 / 10;
    const auto modalBounds = juce::Rectangle<int>(
        editorPadding,
        getHeight() - editorPadding - modalHeight,
        juce::jmax(0, getWidth() - editorPadding * 2),
        modalHeight);
    if (sampleEffectsModalSurface != nullptr)
        sampleEffectsModalSurface->setBounds(modalBounds);

    const int modalKnobCentreY = modalBounds.getY() + 68;
    placeKnobWithValueAndLabel(sampleGainSlider, sampleGainLabel, sampleGainValueInput,
                               getWidth() - labelWidth - 429, modalKnobCentreY);
    placeKnobWithValueAndLabel(samplePunchSlider, samplePunchLabel, samplePunchValueInput,
                               getWidth() - labelWidth - 348, modalKnobCentreY);
    placeKnobWithValueAndLabel(samplePitchSlider, samplePitchLabel, samplePitchValueInput,
                               getWidth() - labelWidth - 267, modalKnobCentreY);
    placeKnobWithValueAndLabel(sampleFormantSlider, sampleFormantLabel, sampleFormantValueInput,
                               getWidth() - labelWidth - 186, modalKnobCentreY);
    placeKnobWithValueAndLabel(sampleMonoSlider, sampleMonoLabel, sampleMonoValueInput,
                               getWidth() - labelWidth - 105, modalKnobCentreY);
    placeKnobWithValueAndLabel(samplePanSlider, samplePanLabel, samplePanValueInput,
                               getWidth() - labelWidth - 24, modalKnobCentreY);
    constexpr int applyToAllWidth = 150;
    constexpr int effectSelectorWidth = 116;
    constexpr int applyToAllGap = 8;
    constexpr int applyToAllRight = 24;
    const int effectSelectorLeft = getWidth() - applyToAllRight - effectSelectorWidth;
    applyToAllButton.setBounds(
        effectSelectorLeft - applyToAllGap - applyToAllWidth,
        modalBounds.getY() + 152, applyToAllWidth, 28);
    applyToAllEffectSelector.setBounds(
        effectSelectorLeft, modalBounds.getY() + 152, effectSelectorWidth, 28);
    warpButton.setBounds(23, 18, 170, 110);
    constexpr int equaliserHeight = 170;
    constexpr int equaliserWidth = equaliserHeight * 5 / 2;
    equaliserEditor.setBounds(24, modalBounds.getY() + 198, equaliserWidth, equaliserHeight);

    constexpr int closeButtonSize = 26;
    constexpr int closeButtonGap = 6;
    if (sampleEffectsCloseButton != nullptr)
        sampleEffectsCloseButton->setBounds(
            modalBounds.getRight() - closeButtonSize,
            modalBounds.getY() - closeButtonSize - closeButtonGap,
            closeButtonSize,
            closeButtonSize);

    const int selectorHeight = SampleGroupSelector::getPreferredHeight();
    sampleGroupSelector.setBounds(editorPadding,
                                  getHeight() - editorPadding - selectorHeight,
                                  juce::jmax(0, getWidth() - editorPadding * 2),
                                  selectorHeight);

    constexpr int editSamplesButtonWidth = 150;
    constexpr int editSamplesButtonHeight = 26;
    constexpr int editSamplesButtonGap = 8;
    editSamplesButton.setBounds(
        (getWidth() - editSamplesButtonWidth) / 2,
        sampleGroupSelector.getY() - editSamplesButtonHeight - editSamplesButtonGap,
        editSamplesButtonWidth,
        editSamplesButtonHeight);
}
