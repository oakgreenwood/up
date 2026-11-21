#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace PluginUI
{
class OnboardingModal final : public juce::Component
{
public:
    OnboardingModal();

    void show(juce::String title, juce::String instructions, juce::Image screenshot);

private:
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    juce::String title;
    juce::String instructions;
    juce::Image screenshot;
    juce::TextLayout textLayout;
    juce::Rectangle<int> panelBounds;
    juce::Rectangle<float> textBounds;
    juce::Rectangle<int> imageBounds;
};
}
