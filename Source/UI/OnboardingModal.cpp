#include "OnboardingModal.h"

#include <utility>

namespace PluginUI
{
OnboardingModal::OnboardingModal()
{
    setWantsKeyboardFocus(true);
}

void OnboardingModal::show(juce::String newTitle, juce::String newInstructions, juce::Image newScreenshot)
{
    title = std::move(newTitle);
    instructions = std::move(newInstructions);
    screenshot = std::move(newScreenshot);
    setTitle(title);
    setDescription(instructions);
    resized();
    setVisible(true);
    toFront(true);
    repaint();
}

void OnboardingModal::resized()
{
    const juce::Rectangle<float> panelProportion { 0.3f, 0.3f, 0.4f, 0.4f };
    panelBounds = getLocalBounds().toFloat().getProportion(panelProportion).toNearestInt();
    auto content = panelBounds.reduced(16).toFloat();
    textBounds = {};
    imageBounds = {};
    if (content.isEmpty())
        return;

    juce::AttributedString text;
    text.setLineSpacing(3.0f);
    text.append(title + "\n", juce::Font(juce::FontOptions(18.0f, juce::Font::bold)), juce::Colours::black);
    text.append(instructions, juce::Font(juce::FontOptions(16.0f)), juce::Colours::black);
    textLayout.createLayout(text, content.getWidth());

    const auto textHeight = juce::jmin(textLayout.getHeight(), content.getHeight());
    if (screenshot.isValid())
    {
        textBounds = content.removeFromTop(textHeight);
        imageBounds = content.withTrimmedTop(15.0f).toNearestInt();
    }
    else
    {
        textBounds = content.withSizeKeepingCentre(content.getWidth(), textHeight);
    }
}

void OnboardingModal::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.7f));
    g.setColour(juce::Colour(0xffa5a4a4));
    g.fillRect(panelBounds);
    g.setColour(juce::Colours::black);
    g.drawRect(panelBounds, 1);

    if (!textBounds.isEmpty())
    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(textBounds.getSmallestIntegerContainer());
        textLayout.draw(g, textBounds);
    }

    if (screenshot.isValid() && !imageBounds.isEmpty())
        g.drawImageWithin(screenshot, imageBounds.getX(), imageBounds.getY(),
                          imageBounds.getWidth(), imageBounds.getHeight(),
                          juce::RectanglePlacement::xMid | juce::RectanglePlacement::yTop);
}

void OnboardingModal::mouseUp(const juce::MouseEvent&)
{
    setVisible(false);
}

bool OnboardingModal::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
        setVisible(false);
    return true;
}
}
