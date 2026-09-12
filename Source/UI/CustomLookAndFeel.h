#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "RotarySliderRenderer.h"
#include "TempoSyncButtonRenderer.h"

namespace PluginUI
{
inline constexpr const char* rzhavSliderId = "rzhavSlider";
inline constexpr const char* sustainSliderId = "sustainSlider";
inline constexpr const char* sampleGainSliderId = "sampleGainSlider";
inline constexpr const char* samplePunchSliderId = "samplePunchSlider";
inline constexpr const char* samplePitchSliderId = "samplePitchSlider";
inline constexpr const char* tempoSyncButtonId = "tempoSyncButton";
}

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();

    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

private:
    struct RotarySliderEntry
    {
        juce::String componentId;
        PluginUI::RotarySliderRenderer renderer;
    };

    std::vector<RotarySliderEntry> rotarySliders;
    PluginUI::TempoSyncButtonRenderer tempoSyncButton;
};
