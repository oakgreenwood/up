#include "NoiseWaveformEditor.h"
#include "../PluginProcessor.h"
#include "../Parameters/PluginParameters.h"
#include <cmath>

NoiseWaveformEditor::NoiseWaveformEditor(AudioPluginAudioProcessor& p) : processor(p)
{
    setName("Noise loop start and end");
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    sourceValue = p.parameters.getRawParameterValue(PluginParameters::noiseSourceId);
    startValue = p.parameters.getRawParameterValue(PluginParameters::noiseStartId);
    endValue = p.parameters.getRawParameterValue(PluginParameters::noiseEndId);
    startParameter = p.parameters.getParameter(PluginParameters::noiseStartId);
    endParameter = p.parameters.getParameter(PluginParameters::noiseEndId);
    refresh();
    startTimerHz(30);
}

NoiseWaveformEditor::~NoiseWaveformEditor()
{
    stopTimer();
    endDrag();
}

juce::Rectangle<float> NoiseWaveformEditor::plotBounds() const
{
    // Space outside the frame keeps endpoint handles fully visible and hittable.
    return getLocalBounds().toFloat().reduced(6.0f, 1.0f);
}

void NoiseWaveformEditor::refresh()
{
    const float rawSource = sourceValue->load(std::memory_order_relaxed);
    const int source = std::isfinite(rawSource) ? juce::roundToInt(juce::jlimit(0.0f,
        static_cast<float>(processor.parameters.getParameter(PluginParameters::noiseSourceId)->getNumSteps() - 1),
        rawSource)) : 0;
    const bool sourceChanged = source != displayedSource;
    const auto revision = processor.getNoiseImportRevision();
    const bool importedChanged = revision != displayedRevision;
    displayedRevision = revision;
    if (sourceChanged || (importedChanged && source == BackgroundNoisePlayer::importedSourceIndex()))
    {
        endDrag();
        displayedSource = source;
        waveform = processor.copyNoiseWaveform(source, waveformStorage) ? &waveformStorage : nullptr;
    }
    const int length = waveform != nullptr ? waveform->samples : 0;
    const auto region = BackgroundNoisePlayer::resolveRegion(length,
        startValue->load(std::memory_order_relaxed), endValue->load(std::memory_order_relaxed));
    const float start = length > 0 ? static_cast<float>(region.start) / static_cast<float>(length) : 0.0f;
    const float end = length > 0 ? static_cast<float>(region.end) / static_cast<float>(length) : 1.0f;
    const float playhead = length > 0 ? processor.getNoisePlayheadPosition(source) : -1.0f;
    if (sourceChanged || importedChanged || start != displayedStart || end != displayedEnd
        || playhead != displayedPlayhead)
    {
        displayedStart = start;
        displayedEnd = end;
        displayedPlayhead = playhead;
        repaint();
    }
}

void NoiseWaveformEditor::timerCallback()
{
    refresh();
    const auto error = processor.takeNoiseImportError();
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                              "Noise import", error);
}

bool NoiseWaveformEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && BackgroundNoisePlayer::canImportFile(juce::File(files[0]));
}

void NoiseWaveformEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (!isInterestedInFileDrag(files))
        return;
    endDrag();
    processor.importNoiseFile(juce::File(files[0]));
}

void NoiseWaveformEditor::paint(juce::Graphics& g)
{
    const auto bounds = plotBounds();
    if (bounds.isEmpty())
        return;
    const float startX = bounds.getX() + displayedStart * bounds.getWidth();
    const float endX = bounds.getX() + displayedEnd * bounds.getWidth();
    g.setColour(juce::Colours::black.withAlpha(0.20f));
    g.fillRect(bounds.withRight(startX));
    g.fillRect(bounds.withLeft(endX));
    g.setColour(juce::Colours::black);
    if (waveform != nullptr && waveform->samples > 0)
    {
        const int columns = juce::jlimit(1, BackgroundNoisePlayer::Waveform::bins,
                                        static_cast<int>(std::ceil(bounds.getWidth())));
        const float amplitude = bounds.getHeight() * 0.43f / juce::jmax(0.001f, waveform->peak);
        for (int column = 0; column < columns; ++column)
        {
            const int first = column * BackgroundNoisePlayer::Waveform::bins / columns;
            const int last = (column + 1) * BackgroundNoisePlayer::Waveform::bins / columns;
            float low = 0.0f, high = 0.0f;
            for (int bin = first; bin < last; ++bin)
            {
                low = juce::jmin(low, waveform->minimum[static_cast<size_t>(bin)]);
                high = juce::jmax(high, waveform->maximum[static_cast<size_t>(bin)]);
            }
            const float x = bounds.getX() + static_cast<float>(column) * bounds.getWidth() / static_cast<float>(columns);
            g.drawLine(x, bounds.getCentreY() - juce::jmax(0.5f, high * amplitude),
                       x, bounds.getCentreY() - juce::jmin(-0.5f, low * amplitude), 1.0f);
        }
    }
    if (displayedPlayhead >= displayedStart && displayedPlayhead < displayedEnd)
    {
        const float x = bounds.getX() + displayedPlayhead * bounds.getWidth();
        g.setColour(juce::Colour(0xffcdd0c3));
        g.drawVerticalLine(juce::roundToInt(x), bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);
        g.setColour(juce::Colours::black);
    }
    g.drawRect(bounds, 1.5f);
    for (const float x : { startX, endX })
    {
        g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 1.0f);
        g.fillRect(x - 4.0f, bounds.getCentreY() - 11.0f, 8.0f, 22.0f);
    }
}

void NoiseWaveformEditor::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
        return;
    refresh();
    endDrag();
    if (waveform == nullptr || waveform->samples <= 0 || plotBounds().isEmpty())
        return;
    const auto bounds = plotBounds();
    const float startX = bounds.getX() + displayedStart * bounds.getWidth();
    const float endX = bounds.getX() + displayedEnd * bounds.getWidth();
    // The midpoint rule also keeps the start handle reachable when automation
    // collapses both displayed handles onto the same pixel.
    const bool startHandle = event.position.x <= (startX + endX) * 0.5f;
    const float handleX = startHandle ? startX : endX;
    draggedParameter = startHandle ? startParameter : endParameter;
    dragOffset = std::abs(event.position.x - handleX) <= 8.0f ? event.position.x - handleX : 0.0f;
    processor.setNoiseTrimActive(true);
    draggedParameter->beginChangeGesture();
    mouseDrag(event);
}

void NoiseWaveformEditor::mouseDrag(const juce::MouseEvent& event)
{
    refresh(); // A source switch also ends any drag that began on the previous file.
    if (draggedParameter == nullptr || waveform == nullptr || waveform->samples <= 0)
        return;
    const auto bounds = plotBounds();
    const float gap = juce::jmax(1.0e-6f, 1.0f / static_cast<float>(waveform->samples));
    const float position = (event.position.x - dragOffset - bounds.getX()) / juce::jmax(1.0f, bounds.getWidth());
    const float value = draggedParameter == startParameter
        ? juce::jlimit(0.0f, juce::jmax(0.0f, displayedEnd - gap), position)
        : juce::jlimit(juce::jmin(1.0f, displayedStart + gap), 1.0f, position);
    draggedParameter->setValueNotifyingHost(draggedParameter->convertTo0to1(value));
    refresh();
}

void NoiseWaveformEditor::mouseUp(const juce::MouseEvent&) { endDrag(); }

void NoiseWaveformEditor::visibilityChanged()
{
    if (!isShowing())
        endDrag();
}

void NoiseWaveformEditor::endDrag()
{
    if (draggedParameter != nullptr)
    {
        draggedParameter->endChangeGesture();
        processor.setNoiseTrimActive(false);
    }
    draggedParameter = nullptr;
}
