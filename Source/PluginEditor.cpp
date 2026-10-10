#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/PluginParameters.h"
#include "UI/CustomLookAndFeel.h"

#include <cmath>

namespace
{
constexpr int editorPadding = 12;
constexpr int uploadNoiseItemId = -1; // UI action, outside the positive source IDs.

class NoisePanelSurface final : public juce::Component
{
public:
    NoisePanelSurface()
    {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
        g.drawText("NOISE RYTHM", 0, 0, getWidth(), 42, juce::Justification::centredLeft);
        g.drawRect(getLocalBounds().withTrimmedTop(48).toFloat().reduced(1.0f), 2.0f);
    }
};

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
    // Only Shift changes the step. Leave command/control/alt shortcuts alone
    // on every platform, including where JUCE aliases Command to Control.
    if (key.getModifiers().withoutFlags(juce::ModifierKeys::shiftModifier)
            .isAnyModifierKeyDown())
        return 0;

    const int code = key.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
        return 1;
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
        return -1;
    return 0;
}

bool nudgeSliderFromArrow(juce::Slider& slider,
                          const juce::KeyPress& key,
                          double valueBeforeNudge)
{
    const int direction = getArrowDirection(key);
    const double interval = slider.getInterval() > 0.0
        ? slider.getInterval() : slider.getRange().getLength() * 0.01;
    if (direction == 0 || interval <= 0.0)
        return false;

    const juce::Slider::ScopedDragNotification gesture(slider);
    const double multiplier = key.getModifiers().isShiftDown() ? 10.0 : 1.0;
    slider.setValue(valueBeforeNudge + direction * interval * multiplier,
                    juce::sendNotificationSync);
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

bool KeyboardStepSlider::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isShiftDown()
        && nudgeSliderFromArrow(*this, key, getValue()))
        return true;

    return juce::Slider::keyPressed(key);
}

bool ImageKnobSlider::keyPressed(const juce::KeyPress& key)
{
    if (getArrowDirection(key) == 0)
        return juce::Slider::keyPressed(key);

    if (key.getModifiers().isShiftDown())
        return nudgeSliderFromArrow(*this, key, getValue());

    const juce::Slider::ScopedDragNotification gesture(*this);
    return juce::Slider::keyPressed(key);
}

void ImageKnobSlider::mouseDown(const juce::MouseEvent& event)
{
    dragStartPosition = event.position;
    dragAxis = ValueDragAxis::undecided;
    juce::Slider::mouseDown(event);
}

void ImageKnobSlider::mouseDrag(const juce::MouseEvent& event)
{
    const float horizontal = event.position.x - dragStartPosition.x;
    const float vertical = event.position.y - dragStartPosition.y;
    if (dragAxis == ValueDragAxis::undecided)
    {
        if (juce::jmax(std::abs(horizontal), std::abs(vertical)) < 3.0f)
            return;
        dragAxis = std::abs(horizontal) > std::abs(vertical)
            ? ValueDragAxis::horizontal : ValueDragAxis::vertical;
    }

    if (dragAxis == ValueDragAxis::horizontal)
        juce::Slider::mouseDrag(event.withNewPosition(
            juce::Point<float> { event.position.x, dragStartPosition.y - horizontal }));
    else
        juce::Slider::mouseDrag(event);
}

void ImageKnobSlider::mouseUp(const juce::MouseEvent& event)
{
    juce::Slider::mouseUp(event);
    dragAxis = ValueDragAxis::undecided;
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
    setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
    setKeyboardType(juce::TextInputTarget::decimalKeyboard);
    slider.addListener(this);
    refreshValue();
}

NumericValueInput::~NumericValueInput()
{
    endDrag();
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

void NumericValueInput::mouseDown(const juce::MouseEvent& event)
{
    canDrag = event.mods.isLeftButtonDown() && isEnabled() && !isBeingEdited();
    dragStartValue = slider.getValue();
    dragAxis = ValueDragAxis::undecided;
}

void NumericValueInput::mouseDrag(const juce::MouseEvent& event)
{
    if (!canDrag || !event.mods.isLeftButtonDown())
        return;

    if (!dragGesture.has_value())
    {
        const int horizontal = event.getDistanceFromDragStartX();
        const int vertical = event.getDistanceFromDragStartY();
        if (juce::jmax(std::abs(horizontal), std::abs(vertical)) < 3)
            return;

        dragAxis = std::abs(horizontal) > std::abs(vertical)
            ? ValueDragAxis::horizontal : ValueDragAxis::vertical;
        dragGesture.emplace(slider);
        if (manualGestureParameter != nullptr)
            manualGestureParameter->beginChangeGesture();
    }

    const int movement = dragAxis == ValueDragAxis::horizontal
        ? event.getDistanceFromDragStartX() : -event.getDistanceFromDragStartY();
    const double dragExtent = juce::jmax(1, slider.getMouseDragSensitivity());
    const double proportion = juce::jlimit(0.0, 1.0,
        slider.valueToProportionOfLength(dragStartValue)
            + movement / dragExtent);
    slider.setValue(slider.proportionOfLengthToValue(proportion), juce::sendNotificationSync);
}

void NumericValueInput::mouseUp(const juce::MouseEvent& event)
{
    const bool couldDrag = canDrag;
    canDrag = false;
    if (dragGesture.has_value())
    {
        endDrag();
        return;
    }

    if (couldDrag && event.mouseWasDraggedSinceMouseDown()
        && event.getDistanceFromDragStart() < 4
        && contains(event.getPosition()) && !event.mods.isPopupMenu())
    {
        showEditor();
        return;
    }

    juce::Label::mouseUp(event);
}

void NumericValueInput::visibilityChanged()
{
    if (!isShowing())
        endDrag();
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
    endDrag();
    hideEditor(true);
    refreshValue();
}

void NumericValueInput::endDrag()
{
    canDrag = false;
    if (!dragGesture.has_value())
        return;

    if (manualGestureParameter != nullptr)
        manualGestureParameter->endChangeGesture();
    dragGesture.reset();
    dragAxis = ValueDragAxis::undecided;
}

void NumericValueInput::refreshDisplay()
{
    if (!isBeingEdited())
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
      equaliserEditor(p),
      sampleWaveformEditor(p),
      noiseWaveform(p)
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

    noisePanelSurface = std::make_unique<NoisePanelSurface>();
    addAndMakeVisible(*noisePanelSurface);

    const auto configureNoiseCaption = [this] (juce::Label& label, const char* text)
    {
        addAndMakeVisible(label);
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        label.setColour(juce::Label::textColourId, PluginUI::noiseCaptionColour);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setBorderSize(juce::BorderSize<int>(0));
    };
    configureNoiseCaption(noiseSourceLabel, "SOURCE");
    configureNoiseCaption(noiseTriggerLabel, "TRIGGER");
    configureNoiseCaption(noiseLabel, "VOLUME");
    configureNoiseCaption(noiseModulationLabel, "PUMP");
    configureNoiseCaption(noiseFollowLabel, "FOLLOW");
    noiseFollowLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(noiseWaveform);

    addAndMakeVisible(noiseSourceSelector);
    noiseSourceSelector.setName("Noise source");
    noiseSourceSelector.setComponentID(PluginUI::noiseSourceSelectorId);
    noiseSourceSelector.setLookAndFeel(customLNF.get());
    displayedNoiseRevision = processorRef.getNoiseImportRevision();
    displayedNoiseNames = processorRef.getAvailableNoiseNames();
    noiseSourceSelector.addItem("Upload file", uploadNoiseItemId);
    for (int i = 0; i < displayedNoiseNames.size(); ++i)
        if (displayedNoiseNames[i].isNotEmpty())
            noiseSourceSelector.addItem(displayedNoiseNames[i], i + 1);
    noiseSourceSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    noiseSourceSelector.setColour(juce::ComboBox::textColourId, juce::Colours::black);
    noiseSourceSelector.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    noiseSourceSelector.setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    // Entries can be sparse while imports load/fail. Use stable item IDs,
    // rather than ComboBoxAttachment's item-count-based normalization.
    noiseSourceAttachment = std::make_unique<juce::ParameterAttachment>(
        *processorRef.parameters.getParameter(PluginParameters::noiseSourceId),
        [this] (float value)
        {
            noiseSourceSelector.setSelectedId(juce::roundToInt(value) + 1, juce::dontSendNotification);
        });
    noiseSourceSelector.onChange = [this]
    {
        const int id = noiseSourceSelector.getSelectedId();
        if (id == uploadNoiseItemId)
        {
            // Keep the current source selected, including if the picker is cancelled.
            noiseSourceAttachment->sendInitialUpdate();
            openNoiseFileChooser();
        }
        else if (id > 0)
            noiseSourceAttachment->setValueAsCompleteGesture(static_cast<float>(id - 1));
    };
    noiseSourceAttachment->sendInitialUpdate();

    addAndMakeVisible(noiseGainSlider);
    noiseGainSlider.setName("Noise gain");
    noiseGainSlider.setComponentID(PluginUI::noiseGainSliderId);
    noiseGainSlider.setLookAndFeel(customLNF.get());
    noiseGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    noiseGainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    noiseGainSlider.setDoubleClickReturnValue(true, PluginParameters::noiseGainDbSilence);
    noiseGainSlider.setPopupDisplayEnabled(true, true, this);
    noiseGainSlider.setColour(juce::Slider::thumbColourId, juce::Colours::black);
    noiseGainSlider.setColour(juce::Slider::trackColourId, juce::Colours::black);
    noiseGainSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xffdddddd));
    bindSliderToParameter(noiseGainSlider, PluginParameters::noiseGainDbId, noiseGainAttachment);
    noiseGainSlider.setTextValueSuffix(" dB");

    addAndMakeVisible(noiseTriggerSelector);
    noiseTriggerSelector.setName("Noise trigger");
    noiseTriggerSelector.setComponentID(PluginUI::noiseTriggerSelectorId);
    noiseTriggerSelector.setLookAndFeel(customLNF.get());
    noiseTriggerSelector.setTooltip("Samples whose audio controls the noise envelope.");
    noiseTriggerSelector.addItemList(PluginParameters::getNoiseTriggerNames(), 1);
    noiseTriggerSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    noiseTriggerSelector.setColour(juce::ComboBox::textColourId, juce::Colours::black);
    noiseTriggerSelector.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    noiseTriggerSelector.setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    noiseTriggerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.parameters, PluginParameters::noiseTriggerId, noiseTriggerSelector);

    addAndMakeVisible(noiseModulationSlider);
    noiseModulationSlider.setName("Noise pump / follow");
    noiseModulationSlider.setComponentID(PluginUI::noiseModulationSliderId);
    noiseModulationSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    noiseModulationSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    noiseModulationSlider.setLookAndFeel(customLNF.get());
    noiseModulationSlider.setDoubleClickReturnValue(true, 0.0);
    noiseModulationSlider.setTooltip("Left: pump. Centre: unchanged. Right: lower background noise; trigger boost reaches maximum early.");
    bindSliderToParameter(noiseModulationSlider, PluginParameters::noiseModulationId, noiseModulationAttachment);

    const auto configureNoiseKnob = [this] (ImageKnobSlider& slider, juce::Label& label,
        NumericValueInput& valueInput, const char* componentId, const char* parameterId,
        const char* name, const char* unit, float defaultValue,
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        addAndMakeVisible(slider);
        slider.setName(juce::String("Noise ") + name);
        slider.setComponentID(componentId);
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setLookAndFeel(customLNF.get());
        slider.setDoubleClickReturnValue(true, defaultValue);
        slider.setMouseDragSensitivity(150);
        bindSliderToParameter(slider, parameterId, attachment);
        configureSignedDecimalValueInput(slider, unit);
        if (juce::String(unit) == " ms")
            slider.textFromValueFunction = [] (double value) { return juce::String(value, 0) + " ms"; };
        slider.updateText();
        valueInput.discardEdit();
        addAndMakeVisible(valueInput);
        addAndMakeVisible(label);
        configureKnobLabel(label, name);
        label.setColour(juce::Label::textColourId, PluginUI::noiseCaptionColour);
        label.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    };
    configureNoiseKnob(noiseReleaseSlider, noiseReleaseLabel, noiseReleaseValueInput,
        PluginUI::noiseReleaseSliderId, PluginParameters::noiseReleaseMsId, "RELEASE", " ms",
        PluginParameters::noiseReleaseMsDefault, noiseReleaseAttachment);

    sampleEffectsModalSurface = std::make_unique<SampleEffectsModalSurface>();
    addAndMakeVisible(*sampleEffectsModalSurface);

    const auto configureSampleTab = [this] (juce::TextButton& button)
    {
        addAndMakeVisible(button);
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colours::black);
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    };
    configureSampleTab(effectsTabButton);
    configureSampleTab(sampleTabButton);
    effectsTabButton.onClick = [this]
    {
        effectsTabSelected = true;
        updateSampleEditorTabVisibility();
    };
    sampleTabButton.onClick = [this]
    {
        effectsTabSelected = false;
        updateSampleEditorTabVisibility();
    };

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

    addAndMakeVisible(sampleWaveformEditor);
    addAndMakeVisible(sampleBeatSnapLabel);
    sampleBeatSnapLabel.setText("BEAT SNAP", juce::dontSendNotification);
    sampleBeatSnapLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8e8b8b));
    sampleBeatSnapLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    sampleBeatSnapLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(sampleBeatSnapTarget);
    sampleBeatSnapTarget.setName("Beat snap targets");
    sampleBeatSnapTarget.setTooltip("Choose OFF, transients, or musical-grid snap points");
    sampleBeatSnapTarget.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    sampleBeatSnapTarget.setColour(juce::ComboBox::textColourId, juce::Colours::black);
    sampleBeatSnapTarget.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    sampleBeatSnapTarget.setColour(juce::ComboBox::arrowColourId, juce::Colours::black);
    sampleBeatSnapTarget.addItem("OFF", 1);
    sampleBeatSnapTarget.addItem("TRANSIENTS", 2);
    sampleBeatSnapTarget.addItem("1/4", 3);
    sampleBeatSnapTarget.addItem("1/8", 4);
    sampleBeatSnapTarget.addItem("1/16", 5);
    sampleBeatSnapTarget.setSelectedId(2, juce::dontSendNotification);
    sampleBeatSnapTarget.onChange = [this]
    {
        updateSampleEditorTabVisibility();
    };
    addAndMakeVisible(sampleBeatSnapButton);
    sampleBeatSnapButton.setClickingTogglesState(true);
    sampleBeatSnapButton.setTooltip("Snap start and end to transient points");
    sampleBeatSnapButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
    sampleBeatSnapButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8e8b8b));
    sampleBeatSnapButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    sampleBeatSnapButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    sampleBeatSnapButton.onClick = [this] { updateSampleEditorTabVisibility(); };

    const auto configureSampleEnvelopeInput = [this] (juce::Slider& slider,
        NumericValueInput& valueInput, juce::Label& label, const char* parameterId,
        const char* caption,
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        addChildComponent(slider);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setMouseDragSensitivity(150);
        bindSliderToParameter(slider, parameterId, attachment);
        configureSignedDecimalValueInput(slider, " ms");
        slider.textFromValueFunction = [] (double value)
        {
            return juce::String(value, 0) + " ms";
        };
        slider.updateText();
        valueInput.discardEdit();
        addAndMakeVisible(valueInput);
        addAndMakeVisible(label);
        configureKnobLabel(label, caption);
    };
    configureSampleEnvelopeInput(sampleAttackSlider, sampleAttackValueInput, sampleAttackLabel,
        PluginParameters::sampleAttackMsId, "ATTACK:", sampleAttackAttachment);
    configureSampleEnvelopeInput(sampleReleaseSlider, sampleReleaseValueInput, sampleReleaseLabel,
        PluginParameters::sampleReleaseMsId, "RELEASE:", sampleReleaseAttachment);
    const auto configureTrimInput = [this] (juce::Slider& slider, NumericValueInput& input,
                                            const char* parameterId)
    {
        addChildComponent(slider);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setMouseDragSensitivity(150);
        slider.setRange(0.0, 1.0, 1.0);
        configureSignedDecimalValueInput(slider, " ms");
        slider.textFromValueFunction = [] (double value)
        {
            return juce::String(value, 0) + " ms";
        };
        slider.updateText();
        addAndMakeVisible(input);
        input.setManualGestureParameter(processorRef.parameters.getParameter(parameterId));
        input.refreshDisplay();
        slider.onValueChange = [this, sliderPtr = &slider, parameterId]
        {
            commitSampleTrimInput(*sliderPtr, parameterId);
        };
    };
    configureTrimInput(sampleStartMsSlider, sampleStartValue, PluginParameters::sampleStartId);
    configureTrimInput(sampleEndMsSlider, sampleEndValue, PluginParameters::sampleEndId);
    addChildComponent(sampleLoopBpmSlider);
    sampleLoopBpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sampleLoopBpmSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sampleLoopBpmSlider.setMouseDragSensitivity(150);
    bindSliderToParameter(sampleLoopBpmSlider, PluginParameters::sampleLoopBpmId,
                          sampleLoopBpmAttachment);
    sampleLoopBpmSlider.textFromValueFunction = [this] (double value)
    {
        const double bpm = value >= 1.0 ? value : processorRef.getProjectBpm();
        return juce::String(bpm, std::abs(bpm - std::round(bpm)) < 0.05 ? 0 : 1) + " BPM";
    };
    sampleLoopBpmSlider.valueFromTextFunction = [this] (const juce::String& text)
    {
        double value = 0.0;
        return parseSignedDecimalInput(text, " BPM", value)
            ? value : sampleLoopBpmSlider.getValue();
    };
    sampleLoopBpmSlider.updateText();
    addAndMakeVisible(sampleLoopBpmValueInput);
    sampleLoopBpmValueInput.discardEdit();
    addAndMakeVisible(sampleLoopBpmLabel);
    sampleLoopBpmLabel.setText("BPM:", juce::dontSendNotification);
    sampleLoopBpmLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8e8b8b));
    sampleLoopBpmLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    addAndMakeVisible(sampleOriginalBpmLabel);
    sampleOriginalBpmLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8e8b8b));
    sampleOriginalBpmLabel.setFont(juce::Font(juce::FontOptions(14.0f)));
    addAndMakeVisible(sampleTempoSyncButton);
    sampleTempoSyncButton.setClickingTogglesState(true);
    sampleTempoSyncButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
    sampleTempoSyncButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffeeeeee));
    sampleTempoSyncButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    sampleTempoSyncButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    sampleTempoSyncButton.setTooltip("Follow project tempo when on; use the sample's original BPM when off");
    sampleTempoSyncButton.onClick = [this]
    {
        const double originalBpm = processorRef.getSampleGroupOriginalBpm(
            processorRef.getSelectedSampleGroupIndex());
        const bool sync = sampleTempoSyncButton.getToggleState();
        sampleTempoSyncButton.setAlpha(sync ? 1.0f : 0.6f);
        const juce::Slider::ScopedDragNotification gesture(sampleLoopBpmSlider);
        sampleLoopBpmSlider.setValue(sync ? 0.0 : originalBpm, juce::sendNotificationSync);
    };
    addAndMakeVisible(sampleLoopModeButton);
    sampleLoopModeButton.setClickingTogglesState(true);
    sampleLoopModeButton.setTooltip("Switch between Rubber Band pitch preservation and direct speed/pitch playback");
    sampleLoopModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.parameters, PluginParameters::sampleLoopTapeModeId, sampleLoopModeButton);
    sampleStartLabel.setText("START:", juce::dontSendNotification);
    sampleEndLabel.setText("END:", juce::dontSendNotification);
    for (auto* label : { &sampleStartLabel, &sampleAttackLabel, &sampleEndLabel, &sampleReleaseLabel })
    {
        addAndMakeVisible(*label);
        label->setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        label->setColour(juce::Label::textColourId, juce::Colour(0xff8e8b8b));
        label->setJustificationType(juce::Justification::centredLeft);
    }
    for (juce::Label* value : { static_cast<juce::Label*>(&sampleStartValue),
                              static_cast<juce::Label*>(&sampleAttackValueInput),
                              static_cast<juce::Label*>(&sampleEndValue),
                              static_cast<juce::Label*>(&sampleReleaseValueInput) })
    {
        addAndMakeVisible(*value);
        value->setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        value->setColour(juce::Label::textColourId, juce::Colours::black);
        value->setJustificationType(juce::Justification::centredLeft);
    }
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

    sampleGroupSelector.setSampleGroups(processorRef.getSampleGroups());
    rebuildSampleGroupActivityMap();
    sampleGroupSelector.setSelectedIndex(processorRef.getSelectedSampleGroupIndex());
    sampleGroupSelector.onSelectedIndexChanged = [this] (int selectedIndex)
    {
        selectSampleGroupForEditing(selectedIndex);
    };
    addAndMakeVisible(sampleGroupSelector);

    addAndMakeVisible(editSamplesButton);
    editSamplesButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
    editSamplesButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    editSamplesButton.onClick = [this]
    {
        const int midiNote = processorRef.getLatestMidiNoteOnNote();
        if (midiNote >= 0 && midiNote < AudioPluginAudioProcessor::midiNoteActivityCount)
        {
            const int groupIndex = sampleGroupIndexByMidiNote[(size_t) midiNote];
            if (groupIndex >= 0)
                selectSampleGroupForEditing(groupIndex);
        }
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
            if (!definition.includeInApplyToAll)
                continue;
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
    noiseFileChooser.reset();
    equaliserEditor.endDrag();
    sampleWaveformEditor.endDrag();
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
    effectsTabButton.onClick = {};
    sampleTabButton.onClick = {};
    if (sampleEffectsCloseButton != nullptr)
        sampleEffectsCloseButton->onClick = {};

    // Clear L&F pointers before destroying the owned look and feel.
    rzhavSlider.setLookAndFeel(nullptr);
    sustainSlider.setLookAndFeel(nullptr);
    ottSlider.setLookAndFeel(nullptr);
    noiseModulationSlider.setLookAndFeel(nullptr);
    noiseGainSlider.setLookAndFeel(nullptr);
    noiseSourceSelector.onChange = {};
    noiseSourceSelector.setLookAndFeel(nullptr);
    noiseTriggerSelector.setLookAndFeel(nullptr);
    noiseReleaseSlider.setLookAndFeel(nullptr);
    sampleGainSlider.setLookAndFeel(nullptr);
    samplePunchSlider.setLookAndFeel(nullptr);
    samplePitchSlider.setLookAndFeel(nullptr);
    sampleFormantSlider.setLookAndFeel(nullptr);
    sampleMonoSlider.setLookAndFeel(nullptr);
    samplePanSlider.setLookAndFeel(nullptr);
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
    sampleWaveformEditor.selectionChanged();
    // An edit for the previous sample must never be committed to a new selection.
    sampleGainValueInput.discardEdit();
    samplePunchValueInput.discardEdit();
    samplePitchValueInput.discardEdit();
    sampleFormantValueInput.discardEdit();
    sampleMonoValueInput.discardEdit();
    samplePanValueInput.discardEdit();
    sampleAttackValueInput.discardEdit();
    sampleReleaseValueInput.discardEdit();
    sampleStartValue.discardEdit();
    sampleEndValue.discardEdit();
    sampleLoopBpmValueInput.discardEdit();
    displayedSampleGroupIndex = processorRef.getSelectedSampleGroupIndex();
    const double originalBpm = processorRef.getSampleGroupOriginalBpm(displayedSampleGroupIndex);
    sampleOriginalBpmLabel.setText(originalBpm > 0.0
        ? "ORIGINAL: " + juce::String(originalBpm, 0) + " BPM" : juce::String(),
        juce::dontSendNotification);
    updateSampleEditorTabVisibility();
    for (const auto& binding : sampleSpecificSliderBindings)
    {
        if (binding.slider == nullptr)
            continue;

        const float fallbackValue = getParameterDefaultValue(binding.parameterId);
        const float sampleValue = processorRef.getSampleSpecificParameterValue(binding.parameterId, fallbackValue);
        binding.slider->setValue(sampleValue, juce::sendNotificationSync);
    }
    const bool tempoSynced = sampleLoopBpmSlider.getValue() < 1.0;
    sampleTempoSyncButton.setToggleState(tempoSynced, juce::dontSendNotification);
    sampleTempoSyncButton.setAlpha(tempoSynced ? 1.0f : 0.6f);
    updateSampleTrimInputs();
    resized();
}

void AudioPluginAudioProcessorEditor::updateSampleTrimInputs()
{
    sampleWaveformEditor.selectionChanged();
    const double durationMs = sampleWaveformEditor.getDurationMs();
    const double sliderMaximum = juce::jmax(1.0, std::ceil(durationMs));
    const juce::ScopedValueSetter<bool> guard(ignoreSampleTrimInputChanges, true);
    for (auto* slider : { &sampleStartMsSlider, &sampleEndMsSlider })
        if (slider->getMaximum() != sliderMaximum)
            slider->setRange(0.0, sliderMaximum, 1.0);
    if (!sampleStartValue.editing())
    {
        sampleStartMsSlider.setValue(std::round(sampleWaveformEditor.getStartMs()),
                                     juce::dontSendNotification);
        sampleStartValue.refreshDisplay();
    }
    if (!sampleEndValue.editing())
    {
        sampleEndMsSlider.setValue(std::round(sampleWaveformEditor.getEndMs()),
                                   juce::dontSendNotification);
        sampleEndValue.refreshDisplay();
    }
}

void AudioPluginAudioProcessorEditor::commitSampleTrimInput(juce::Slider& slider,
                                                             const char* parameterId)
{
    if (ignoreSampleTrimInputChanges)
        return;

    auto* parameter = processorRef.parameters.getParameter(parameterId);
    const auto* waveform = processorRef.getSampleWaveform(processorRef.getSelectedSampleGroupIndex());
    const double durationMs = sampleWaveformEditor.getDurationMs();
    if (parameter == nullptr || waveform == nullptr || waveform->samples <= 0 || durationMs <= 0.0)
        return;

    sampleWaveformEditor.selectionChanged();
    const double gap = 1.0 / waveform->samples;
    const double position = slider.getValue() / durationMs;
    const bool isStart = &slider == &sampleStartMsSlider;
    const double value = isStart
        ? juce::jlimit(0.0, juce::jmax(0.0, sampleWaveformEditor.getEndMs() / durationMs - gap),
                       position)
        : juce::jlimit(juce::jmin(1.0, sampleWaveformEditor.getStartMs() / durationMs + gap),
                       1.0, position);
    const float current = processorRef.getSampleSpecificParameterValue(parameterId,
                                                                         isStart ? 0.0f : 1.0f);
    if (std::abs(value - current) > 1.0e-7)
    {
        const bool inputDragging = (isStart ? sampleStartValue : sampleEndValue).dragging();
        if (!inputDragging)
            parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(value)));
        if (!inputDragging)
            parameter->endChangeGesture();
    }
    sampleWaveformEditor.selectionChanged();
    if ((isStart ? sampleStartValue : sampleEndValue).editing())
        slider.setValue(std::round(isStart ? sampleWaveformEditor.getStartMs()
                                           : sampleWaveformEditor.getEndMs()),
                        juce::dontSendNotification);
    updateSampleTrimInputs();
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

    if (sampleEffectsModalSurface != nullptr)
        sampleEffectsModalSurface->setVisible(shouldBeVisible);
    if (sampleEffectsCloseButton != nullptr)
        sampleEffectsCloseButton->setVisible(shouldBeVisible);

    updateSampleEditorTabVisibility();

    juce::Component* const globalEffectComponents[] = {
        &rzhavSlider, &rzhavLabel,
        &sustainSlider, &sustainLabel,
        &ottSlider, &ottLabel,
        &noiseSourceSelector, &noiseGainSlider, &noiseLabel,
        &noiseSourceLabel, &noiseTriggerLabel, &noiseFollowLabel, &noiseWaveform,
        &noiseTriggerSelector, &noiseModulationSlider, &noiseModulationLabel,
        &noiseReleaseSlider, &noiseReleaseLabel, &noiseReleaseValueInput
    };
    for (auto* component : globalEffectComponents)
        component->setVisible(!shouldBeVisible);
    if (noisePanelSurface != nullptr)
        noisePanelSurface->setVisible(!shouldBeVisible);

    repaint();
}

void AudioPluginAudioProcessorEditor::updateSampleEditorTabVisibility()
{
    const bool showEffects = sampleEffectsModalOpen && effectsTabSelected;
    const bool showSample = sampleEffectsModalOpen && !effectsTabSelected;
    if (!showEffects)
        equaliserEditor.endDrag();

    effectsTabButton.setVisible(sampleEffectsModalOpen);
    sampleTabButton.setVisible(sampleEffectsModalOpen);
    effectsTabButton.setToggleState(effectsTabSelected, juce::dontSendNotification);
    sampleTabButton.setToggleState(!effectsTabSelected, juce::dontSendNotification);

    juce::Component* const effectsComponents[] = {
        &samplePunchSlider, &samplePunchValueInput, &samplePunchLabel,
        &samplePitchSlider, &samplePitchValueInput, &samplePitchLabel,
        &sampleFormantSlider, &sampleFormantValueInput, &sampleFormantLabel,
        &sampleMonoSlider, &sampleMonoValueInput, &sampleMonoLabel,
        &equaliserEditor, &applyToAllButton, &applyToAllEffectSelector
    };
    for (auto* component : effectsComponents)
        component->setVisible(showEffects);

    juce::Component* const sampleComponents[] = {
        &sampleGainSlider, &sampleGainValueInput, &sampleGainLabel,
        &samplePanSlider, &samplePanValueInput, &samplePanLabel,
        &sampleWaveformEditor,
    };
    for (auto* component : sampleComponents)
        component->setVisible(showSample);
    const int selectedGroup = processorRef.getSelectedSampleGroupIndex();
    const bool warpMarked = processorRef.isSampleGroupWarpMarked(selectedGroup);
    const auto* metadata = processorRef.getSampleMetadata(selectedGroup);
    const bool loopMarked = metadata != nullptr && metadata->loop;
    const bool showLoopBeatSnap = showSample && warpMarked && loopMarked;
    sampleBeatSnapLabel.setVisible(showLoopBeatSnap);
    sampleBeatSnapTarget.setVisible(showLoopBeatSnap);
    sampleBeatSnapButton.setVisible(showSample && warpMarked && !loopMarked);
    using Target = SampleWaveformEditor::BeatSnapTarget;
    const auto target = !warpMarked ? Target::off
                      : !loopMarked ? (sampleBeatSnapButton.getToggleState() ? Target::transients : Target::off)
                      : sampleBeatSnapTarget.getSelectedId() == 1 ? Target::off
                      : sampleBeatSnapTarget.getSelectedId() == 3 ? Target::quarterNote
                      : sampleBeatSnapTarget.getSelectedId() == 4 ? Target::eighthNote
                      : sampleBeatSnapTarget.getSelectedId() == 5 ? Target::sixteenthNote
                      : Target::transients;
    sampleWaveformEditor.setBeatSnapTarget(target);
    const bool showSampleTimingInputs = showSample && !warpMarked;
    for (auto* component : { static_cast<juce::Component*>(&sampleStartValue),
                             static_cast<juce::Component*>(&sampleStartLabel),
                             static_cast<juce::Component*>(&sampleEndValue),
                             static_cast<juce::Component*>(&sampleEndLabel),
                             static_cast<juce::Component*>(&sampleAttackValueInput),
                             static_cast<juce::Component*>(&sampleAttackLabel),
                             static_cast<juce::Component*>(&sampleReleaseValueInput),
                             static_cast<juce::Component*>(&sampleReleaseLabel) })
        component->setVisible(showSampleTimingInputs);
    const bool showWarp = showSample
        && processorRef.isSampleGroupWarpEnabled(processorRef.getSelectedSampleGroupIndex());
    for (auto* component : { static_cast<juce::Component*>(&sampleLoopBpmLabel),
                             static_cast<juce::Component*>(&sampleLoopBpmValueInput),
                             static_cast<juce::Component*>(&sampleTempoSyncButton),
                             static_cast<juce::Component*>(&sampleLoopModeButton),
                             static_cast<juce::Component*>(&sampleOriginalBpmLabel) })
        component->setVisible(showWarp);
}

void AudioPluginAudioProcessorEditor::openNoiseFileChooser()
{
    // Use the same registered decoders as noise imports, including platform codecs.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    noiseFileChooser = std::make_unique<juce::FileChooser>(
        "Upload noise", juce::File(), formats.getWildcardForAllFormats(), true, false, this);
    noiseFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::canSelectMultipleItems,
        [editor = juce::Component::SafePointer<AudioPluginAudioProcessorEditor>(this)]
        (const juce::FileChooser& chooser)
        {
            if (editor == nullptr)
                return;

            juce::StringArray paths;
            for (const auto& file : chooser.getResults())
                paths.add(file.getFullPathName());
            if (!paths.isEmpty())
                editor->filesDropped(paths, 0, 0);
        });
}

bool AudioPluginAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (BackgroundNoisePlayer::canImportFile(juce::File(path)))
            return true;
    return false;
}

void AudioPluginAudioProcessorEditor::fileDragEnter(const juce::StringArray& files, int, int)
{
    noiseFileDragActive = isInterestedInFileDrag(files);
    repaint();
}

void AudioPluginAudioProcessorEditor::fileDragExit(const juce::StringArray&)
{
    noiseFileDragActive = false;
    repaint();
}

void AudioPluginAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    noiseFileDragActive = false;
    repaint();
    if (!isInterestedInFileDrag(files))
        return;

    noiseWaveform.endDrag();
    for (const auto& path : files)
        if (BackgroundNoisePlayer::canImportFile(juce::File(path)))
            processorRef.importNoiseFile(juce::File(path));
}

void AudioPluginAudioProcessorEditor::visibilityChanged()
{
    if (!isShowing())
    {
        sampleWaveformEditor.endDrag();
        noiseFileDragActive = false;
        repaint();
    }
}

void AudioPluginAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    if (!noiseFileDragActive || noisePanelSurface == nullptr)
        return;

    // Paint above every child without introducing another drag target.
    const auto bounds = noisePanelSurface->getBounds();
    g.setColour(juce::Colour(0xffa5a4a4));
    g.fillRect(bounds);
    g.setColour(juce::Colours::black);
    g.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
    g.drawText("Drop noise", bounds, juce::Justification::centred);
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
    updateSampleTrimInputs();
    if (!sampleLoopBpmValueInput.editing()
        && sampleLoopBpmSlider.getValue() < 1.0)
        sampleLoopBpmValueInput.setText(sampleLoopBpmSlider.getTextFromValue(0.0),
                                         juce::dontSendNotification);
    const bool tempoSynced = sampleLoopBpmSlider.getValue() < 1.0;
    sampleTempoSyncButton.setToggleState(tempoSynced, juce::dontSendNotification);
    sampleTempoSyncButton.setAlpha(tempoSynced ? 1.0f : 0.6f);
    sampleLoopModeButton.setButtonText(sampleLoopModeButton.getToggleState()
        ? "SPEED + PITCH" : "PRESERVE PITCH");
    const auto revision = processorRef.getNoiseImportRevision();
    if (revision != displayedNoiseRevision)
    {
        displayedNoiseRevision = revision;
        const auto names = processorRef.getAvailableNoiseNames();
        if (names != displayedNoiseNames)
        {
            displayedNoiseNames = names;
            noiseSourceSelector.clear(juce::dontSendNotification);
            noiseSourceSelector.addItem("Upload file", uploadNoiseItemId);
            for (int i = 0; i < names.size(); ++i)
                if (names[i].isNotEmpty())
                    noiseSourceSelector.addItem(names[i], i + 1);
            noiseSourceAttachment->sendInitialUpdate();
        }
    }
    if (pendingApplyToAllParameter != nullptr)
    {
        auto* parameter = pendingApplyToAllParameter;
        pendingApplyToAllParameter = nullptr;
        selectApplyToAllEffect(*parameter);
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
    const int noisePanelWidth = juce::roundToInt((getWidth() - 290 - editorPadding) / 1.5);
    const juce::Rectangle<int> noiseBounds(290, editorPadding, noisePanelWidth, 500);
    if (noisePanelSurface != nullptr)
        noisePanelSurface->setBounds(noiseBounds);
    const int noiseLeft = noiseBounds.getX() + 24;
    const int noiseWidth = noiseBounds.getWidth() - 48;
    const int noiseTop = noiseBounds.getY();
    noiseSourceLabel.setBounds(noiseLeft, noiseTop + 62, noiseWidth, 20);
    noiseSourceSelector.setBounds(noiseLeft, noiseTop + 83, noiseWidth, 30);
    noiseWaveform.setBounds(noiseLeft - 6, noiseTop + 125, noiseWidth + 12, 78);
    noiseTriggerLabel.setBounds(noiseLeft, noiseTop + 218, noiseWidth, 20);
    noiseTriggerSelector.setBounds(noiseLeft, noiseTop + 239, noiseWidth, 30);
    noiseLabel.setBounds(noiseLeft, noiseTop + 280, noiseWidth, 20);
    noiseGainSlider.setBounds(noiseLeft - 8, noiseTop + 302, noiseWidth + 16, 26);
    noiseModulationLabel.setBounds(noiseLeft, noiseTop + 335, noiseWidth / 2, 20);
    noiseFollowLabel.setBounds(noiseLeft + noiseWidth / 2, noiseTop + 335, noiseWidth / 2, 20);
    noiseModulationSlider.setBounds(noiseLeft - 8, noiseTop + 358, noiseWidth + 16, 26);
    placeKnobWithValueAndLabel(noiseReleaseSlider, noiseReleaseLabel, noiseReleaseValueInput,
                               noiseLeft + noiseWidth / 2 - labelWidth / 2, noiseTop + 412);

    const int modalHeight = getHeight() * 7 / 10;
    const auto modalBounds = juce::Rectangle<int>(
        editorPadding,
        getHeight() - editorPadding - modalHeight,
        juce::jmax(0, getWidth() - editorPadding * 2),
        modalHeight);
    if (sampleEffectsModalSurface != nullptr)
        sampleEffectsModalSurface->setBounds(modalBounds);

    constexpr int tabWidth = 112;
    constexpr int tabGap = 8;
    constexpr int tabHeight = 30;
    const int tabsLeft = modalBounds.getCentreX() - (2 * tabWidth + tabGap) / 2;
    effectsTabButton.setBounds(tabsLeft, modalBounds.getY() + 14, tabWidth, tabHeight);
    sampleTabButton.setBounds(tabsLeft + tabWidth + tabGap,
                              modalBounds.getY() + 14, tabWidth, tabHeight);

    const int modalKnobCentreY = modalBounds.getY() + 82;
    constexpr int effectsKnobStep = 110;
    const int effectsKnobsLeft = modalBounds.getCentreX()
        - (labelWidth + 3 * effectsKnobStep) / 2;
    placeKnobWithValueAndLabel(samplePitchSlider, samplePitchLabel, samplePitchValueInput,
                               effectsKnobsLeft, modalKnobCentreY);
    placeKnobWithValueAndLabel(sampleFormantSlider, sampleFormantLabel, sampleFormantValueInput,
                               effectsKnobsLeft + effectsKnobStep, modalKnobCentreY);
    placeKnobWithValueAndLabel(sampleMonoSlider, sampleMonoLabel, sampleMonoValueInput,
                               effectsKnobsLeft + 2 * effectsKnobStep, modalKnobCentreY);
    placeKnobWithValueAndLabel(samplePunchSlider, samplePunchLabel, samplePunchValueInput,
                               effectsKnobsLeft + 3 * effectsKnobStep, modalKnobCentreY);

    constexpr int sampleKnobStep = 120;
    const int sampleKnobsLeft = modalBounds.getCentreX() - (labelWidth + sampleKnobStep) / 2;
    placeKnobWithValueAndLabel(sampleGainSlider, sampleGainLabel, sampleGainValueInput,
                               sampleKnobsLeft, modalKnobCentreY);
    placeKnobWithValueAndLabel(samplePanSlider, samplePanLabel, samplePanValueInput,
                               sampleKnobsLeft + sampleKnobStep, modalKnobCentreY);
    const int baseWaveformWidth = juce::jmax(1, modalBounds.getWidth() / 3);
    const bool wideWaveform = processorRef.isSampleGroupWarpMarked(
        processorRef.getSelectedSampleGroupIndex());
    const int waveformWidth = wideWaveform
        ? juce::roundToInt(baseWaveformWidth * 1.6f) : baseWaveformWidth;
    const int waveformHeight = juce::jmax(1, baseWaveformWidth / 3) + 32;
    constexpr int valuesWidth = 162;
    constexpr int valuesGap = 24;
    const int waveformLeft = modalBounds.getCentreX()
        - (wideWaveform ? waveformWidth : waveformWidth + valuesGap + valuesWidth) / 2;
    const int waveformTop = modalBounds.getY() + 171;
    sampleWaveformEditor.setBounds(waveformLeft, waveformTop, waveformWidth, waveformHeight);
    const int valuesLeft = waveformLeft + waveformWidth + valuesGap;
    const int valuesTop = waveformTop + 31 + (waveformHeight - 32 - 112) / 2;
    const auto placeSampleValue = [=] (juce::Label& label, juce::Label& value, int row)
    {
        label.setBounds(valuesLeft, valuesTop + row * 28, 76, 26);
        value.setBounds(valuesLeft + 76, valuesTop + row * 28, 86, 26);
    };
    placeSampleValue(sampleStartLabel, sampleStartValue, 0);
    placeSampleValue(sampleAttackLabel, sampleAttackValueInput, 1);
    placeSampleValue(sampleEndLabel, sampleEndValue, 2);
    placeSampleValue(sampleReleaseLabel, sampleReleaseValueInput, 3);
    const int loopControlsTop = waveformTop + waveformHeight + 13;
    sampleLoopBpmLabel.setBounds(waveformLeft, loopControlsTop, 52, 27);
    sampleLoopBpmValueInput.setBounds(waveformLeft + 48, loopControlsTop, 94, 27);
    sampleTempoSyncButton.setBounds(waveformLeft + 150, loopControlsTop, 134, 27);
    sampleLoopModeButton.setBounds(waveformLeft + 294, loopControlsTop, 160, 27);
    sampleOriginalBpmLabel.setBounds(waveformLeft, loopControlsTop + 31,
                                     wideWaveform ? juce::jmax(0, waveformWidth - 236) : 250, 24);
    sampleBeatSnapLabel.setBounds(waveformLeft + waveformWidth - 228,
                                  loopControlsTop + 31, 90, 24);
    sampleBeatSnapTarget.setBounds(waveformLeft + waveformWidth - 132,
                                   loopControlsTop + 31, 126, 24);
    sampleBeatSnapButton.setBounds(waveformLeft + waveformWidth - 116,
                                   loopControlsTop + 31, 110, 24);
    constexpr int applyToAllWidth = 150;
    constexpr int effectSelectorWidth = 116;
    constexpr int applyToAllGap = 8;
    const int applyToAllLeft = modalBounds.getCentreX()
        - (applyToAllWidth + applyToAllGap + effectSelectorWidth) / 2;
    applyToAllButton.setBounds(
        applyToAllLeft, modalBounds.getY() + 346, applyToAllWidth, 28);
    applyToAllEffectSelector.setBounds(
        applyToAllLeft + applyToAllWidth + applyToAllGap,
        modalBounds.getY() + 346, effectSelectorWidth, 28);
    constexpr int equaliserHeight = 170;
    constexpr int equaliserWidth = equaliserHeight * 5 / 2;
    equaliserEditor.setBounds(modalBounds.getCentreX() - equaliserWidth / 2,
                              modalBounds.getY() + 171, equaliserWidth, equaliserHeight);

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
