#include "CustomLookAndFeel.h"

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
    rotarySliders.reserve(6);
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::rzhavSliderId,
        PluginUI::RotarySliderRenderer(makeRzhavSliderConfig())
    });
    rotarySliders.push_back(RotarySliderEntry {
        PluginUI::sustainSliderId,
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
