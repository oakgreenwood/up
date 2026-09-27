#include "CustomLookAndFeel.h"
#include <cmath>

namespace
{
PluginUI::RotarySliderRendererConfig makeRzhavSliderConfig()
{
    PluginUI::RotarySliderRendererConfig config;
    config.imageResourceName = "pomyatos_png";
    config.minAngleRadians = juce::degreesToRadians(-100.0f);
    config.maxAngleRadians = juce::degreesToRadians(0.0f);
    return config;
}

PluginUI::RotarySliderRendererConfig makeSustainSliderConfig()
{
    PluginUI::RotarySliderRendererConfig config;
    config.imageResourceName = "pomyatos_png";
    config.minAngleRadians = juce::degreesToRadians(-100.0f);
    config.maxAngleRadians = juce::degreesToRadians(0.0f);
    return config;
}

PluginUI::RotarySliderRendererConfig makeSamplePitchSliderConfig()
{
    PluginUI::RotarySliderRendererConfig config;
    config.imageResourceName = "pomyatos_png";
    config.minAngleRadians = juce::degreesToRadians(-100.0f);
    config.maxAngleRadians = juce::degreesToRadians(0.0f);
    return config;
}

PluginUI::RotarySliderRendererConfig makeSamplePunchSliderConfig()
{
    PluginUI::RotarySliderRendererConfig config;
    config.imageResourceName = "pomyatos_png";
    config.minAngleRadians = juce::degreesToRadians(-100.0f);
    config.maxAngleRadians = juce::degreesToRadians(0.0f);
    return config;
}

PluginUI::RotarySliderRendererConfig makeSampleGainSliderConfig()
{
    PluginUI::RotarySliderRendererConfig config;
    config.imageResourceName = "pomyatos_png";
    config.minAngleRadians = juce::degreesToRadians(-100.0f);
    config.maxAngleRadians = juce::degreesToRadians(0.0f);
    return config;
}
}

CustomLookAndFeel::CustomLookAndFeel()
{
    setColour(juce::PopupMenu::backgroundColourId, PluginUI::noisePanelBackground);
    setColour(juce::PopupMenu::textColourId, juce::Colours::black);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::black);
    setColour(juce::PopupMenu::highlightedTextColourId, PluginUI::noisePanelBackground);
    rotarySliders.reserve(10);
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::noiseReleaseSliderId,
        PluginUI::RotarySliderRenderer(makeSustainSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::rzhavSliderId,
        PluginUI::RotarySliderRenderer(makeRzhavSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::sustainSliderId,
        PluginUI::RotarySliderRenderer(makeSustainSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::ottSliderId,
        PluginUI::RotarySliderRenderer(makeSustainSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::samplePunchSliderId,
        PluginUI::RotarySliderRenderer(makeSamplePunchSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::sampleGainSliderId,
        PluginUI::RotarySliderRenderer(makeSampleGainSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::samplePitchSliderId,
        PluginUI::RotarySliderRenderer(makeSamplePitchSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::sampleFormantSliderId,
        PluginUI::RotarySliderRenderer(makeSamplePitchSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::sampleMonoSliderId,
        PluginUI::RotarySliderRenderer(makeSamplePunchSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::samplePanSliderId,
        PluginUI::RotarySliderRenderer(makeSamplePitchSliderConfig())
    });
}

void CustomLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                    int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    if (box.getComponentID() != PluginUI::noiseSourceSelectorId
        && box.getComponentID() != PluginUI::noiseTriggerSelectorId)
    {
        juce::LookAndFeel_V4::drawComboBox(g, width, height, isButtonDown,
                                         buttonX, buttonY, buttonW, buttonH, box);
        return;
    }
    const float x = static_cast<float>(width) - 15.0f;
    const float y = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath(x - 6.0f, y - 3.0f);
    arrow.lineTo(x, y + 3.0f);
    arrow.lineTo(x + 6.0f, y - 3.0f);
    g.setColour(juce::Colours::black);
    g.strokePath(arrow, juce::PathStrokeType(2.0f));
}

juce::Font CustomLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    if (box.getComponentID() == PluginUI::noiseSourceSelectorId
        || box.getComponentID() == PluginUI::noiseTriggerSelectorId)
        return juce::Font(juce::FontOptions(20.0f, juce::Font::bold));
    return juce::LookAndFeel_V4::getComboBoxFont(box);
}

void CustomLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    juce::LookAndFeel_V4::positionComboBoxText(box, label);
    if (box.getComponentID() == PluginUI::noiseSourceSelectorId
        || box.getComponentID() == PluginUI::noiseTriggerSelectorId)
    {
        label.setBounds(0, 0, juce::jmax(0, box.getWidth() - 34), box.getHeight());
        label.setMinimumHorizontalScale(1.0f);
    }
}

void CustomLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float minSliderPos, float maxSliderPos,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool bipolar = slider.getComponentID() == PluginUI::noiseModulationSliderId;
    if (!bipolar && slider.getComponentID() != PluginUI::noiseGainSliderId)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                               sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const float fillStart = bipolar ? centreX : static_cast<float>(x);
    g.setColour(PluginUI::noiseTrackColour);
    g.fillRect(static_cast<float>(x), centreY - 6.0f, static_cast<float>(width), 12.0f);
    g.setColour(juce::Colours::black);
    g.fillRect(juce::jmin(fillStart, sliderPos), centreY - 6.0f,
               std::abs(sliderPos - fillStart), 12.0f);
    g.fillRoundedRectangle(sliderPos - 4.5f, centreY - 11.0f, 9.0f, 22.0f, 3.5f);
}

void CustomLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         float sliderPosProportional,
                                         float rotaryStartAngle,
                                         float rotaryEndAngle,
                                         juce::Slider& slider)
{
    // JUCE routes RotaryVerticalDrag sliders through this callback; renderers name the actual visual behavior.
    const auto componentId = slider.getComponentID();

    for (const auto& rotarySlider : rotarySliders)
    {
        if (componentId == rotarySlider.componentId
            && rotarySlider.renderer.draw(g, x, y, width, height, sliderPosProportional))
        {
            return;
        }
    }

    juce::LookAndFeel_V4::drawRotarySlider(g,
                                           x,
                                           y,
                                           width,
                                           height,
                                           sliderPosProportional,
                                           rotaryStartAngle,
                                           rotaryEndAngle,
                                           slider);
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics& g,
                                         juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    if (button.getComponentID() == PluginUI::tempoSyncButtonId
        && tempoSyncButton.draw(g, button))
    {
        return;
    }

    juce::LookAndFeel_V4::drawToggleButton(g,
                                           button,
                                           shouldDrawButtonAsHighlighted,
                                           shouldDrawButtonAsDown);
}
