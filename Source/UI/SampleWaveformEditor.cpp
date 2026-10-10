#include "SampleWaveformEditor.h"
#include "../PluginProcessor.h"
#include "../Parameters/PluginParameters.h"
#include "../Playback/SampleEnvelope.h"

#include <cmath>

SampleWaveformEditor::SampleWaveformEditor(AudioPluginAudioProcessor& p) : processor(p)
{
    setName("Sample trimming, attack and release");
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    startParameter = p.parameters.getParameter(PluginParameters::sampleStartId);
    endParameter = p.parameters.getParameter(PluginParameters::sampleEndId);
    attackParameter = p.parameters.getParameter(PluginParameters::sampleAttackMsId);
    releaseParameter = p.parameters.getParameter(PluginParameters::sampleReleaseMsId);
    refresh();
    startTimerHz(30);
}

SampleWaveformEditor::~SampleWaveformEditor()
{
    stopTimer();
    endDrag();
}

juce::Rectangle<float> SampleWaveformEditor::plotBounds() const
{
    return getLocalBounds().toFloat().reduced(6.0f, 1.0f)
        .withTrimmedTop(hideEnvelopeControls ? 6.0f : 30.0f);
}

double SampleWaveformEditor::getDurationMs() const noexcept
{
    return waveform != nullptr && std::isfinite(waveform->sampleRate) && waveform->sampleRate > 0.0
        ? waveform->samples * 1000.0 / waveform->sampleRate : 0.0;
}

std::pair<float, float> SampleWaveformEditor::envelopeHandlePositions() const
{
    const auto bounds = plotBounds();
    const double duration = getDurationMs() * 0.001;
    const auto envelope = SampleEnvelope::resolve(
        (displayedEnd - displayedStart) * duration,
        displayedAttackMs * 0.001, displayedReleaseMs * 0.001);
    const double attack = duration > 0.0 ? envelope.attack / duration : 0.0;
    const double release = duration > 0.0 ? envelope.release / duration : 0.0;
    return { bounds.getX() + static_cast<float>(displayedStart + attack) * bounds.getWidth(),
             bounds.getX() + static_cast<float>(displayedEnd - release) * bounds.getWidth() };
}

void SampleWaveformEditor::refresh()
{
    const int group = processor.getSelectedSampleGroupIndex();
    const bool groupChanged = group != displayedGroup;
    if (groupChanged)
    {
        endDrag();
        displayedGroup = group;
        waveform = processor.getSampleWaveform(group);
        originalBpm = processor.getSampleGroupOriginalBpm(group);
        transientFrames.clear();
        const auto* metadata = processor.getSampleMetadata(group);
        if (metadata != nullptr && metadata->warp && waveform != nullptr
            && waveform->samples > 0 && std::isfinite(waveform->sampleRate)
            && waveform->sampleRate > 0.0)
        {
            for (const double time : metadata->transients)
            {
                const double frame = time * waveform->sampleRate;
                if (std::isfinite(frame) && frame >= 0.0 && frame <= waveform->samples)
                    transientFrames.push_back(static_cast<int>(std::round(frame)));
            }
        }
    }
    const bool hideEnvelope = processor.isSampleGroupWarpMarked(group);

    const int length = waveform != nullptr ? waveform->samples : 0;
    float start = 0.0f, end = 1.0f;
    if (length > 0)
    {
        const auto rawStart = processor.getSampleSpecificParameterValue(
            PluginParameters::sampleStartId, 0.0f);
        const auto rawEnd = processor.getSampleSpecificParameterValue(
            PluginParameters::sampleEndId, 1.0f);
        const int first = juce::jlimit(0, length - 1,
            static_cast<int>(std::floor(juce::jlimit(0.0, 1.0,
                static_cast<double>(rawStart)) * length)));
        const double removed = (1.0 - juce::jlimit(0.0, 1.0,
            static_cast<double>(rawEnd))) * length;
        const int last = juce::jmax(first + 1, length - static_cast<int>(std::ceil(removed)));
        start = static_cast<float>(first) / length;
        end = static_cast<float>(last) / length;
    }
    const float attack = processor.getSampleSpecificParameterValue(
        PluginParameters::sampleAttackMsId, PluginParameters::sampleAttackMsDefault);
    const float release = processor.getSampleSpecificParameterValue(
        PluginParameters::sampleReleaseMsId, PluginParameters::sampleReleaseMsDefault);
    const float progress = length > 0 ? processor.getSamplePlayheadProgress(group) : -1.0f;
    const float playhead = progress >= 0.0f ? start + progress * (end - start) : -1.0f;
    if (groupChanged || start != displayedStart || end != displayedEnd
        || attack != displayedAttackMs || release != displayedReleaseMs
        || playhead != displayedPlayhead || hideEnvelope != hideEnvelopeControls)
    {
        displayedStart = start;
        displayedEnd = end;
        displayedAttackMs = attack;
        displayedReleaseMs = release;
        displayedPlayhead = playhead;
        hideEnvelopeControls = hideEnvelope;
        repaint();
    }
}

void SampleWaveformEditor::timerCallback() { refresh(); }
void SampleWaveformEditor::selectionChanged() { refresh(); }

void SampleWaveformEditor::setBeatSnapTarget(BeatSnapTarget target)
{
    endDrag();
    beatSnapTarget = target;
    repaint();
}

double SampleWaveformEditor::gridStepFrames() const noexcept
{
    if (waveform == nullptr || !std::isfinite(waveform->sampleRate)
        || waveform->sampleRate <= 0.0 || !std::isfinite(originalBpm)
        || originalBpm <= 0.0)
        return 0.0;

    const double beats = beatSnapTarget == BeatSnapTarget::quarterNote ? 1.0
                       : beatSnapTarget == BeatSnapTarget::eighthNote ? 0.5
                       : beatSnapTarget == BeatSnapTarget::sixteenthNote ? 0.25 : 0.0;
    const double step = waveform->sampleRate * 60.0 * beats / originalBpm;
    return std::isfinite(step) && step > 0.0 ? juce::jmax(1.0, step) : 0.0;
}

int SampleWaveformEditor::nearestSnapFrame(double targetFrame, int otherFrame,
                                           bool movingStart) const noexcept
{
    const int length = waveform->samples;
    int nearest = movingStart ? (otherFrame > 0 ? 0 : -1)
                              : (otherFrame < length ? length : -1);
    const auto consider = [&] (int frame)
    {
        if (movingStart ? frame >= otherFrame : frame <= otherFrame)
            return;
        if (nearest < 0 || std::abs(frame - targetFrame) < std::abs(nearest - targetFrame))
            nearest = frame;
    };

    if (beatSnapTarget == BeatSnapTarget::transients)
    {
        for (const int frame : transientFrames)
            consider(frame);
    }
    else if (const double step = gridStepFrames(); step > 0.0)
    {
        // Grid points are whole musical note divisions from source frame zero.
        const double first = movingStart ? 1.0
            : juce::jmax(1.0, std::ceil((otherFrame + 0.5) / step));
        const double last = movingStart
            ? std::floor((otherFrame - 0.5) / step)
            : std::floor((length - 0.5) / step);
        if (first <= last)
        {
            const double index = juce::jlimit(first, last,
                std::floor(juce::jlimit(0.0, static_cast<double>(length), targetFrame) / step));
            consider(static_cast<int>(std::round(index * step)));
            consider(static_cast<int>(std::round(juce::jmin(last, index + 1.0) * step)));
        }
    }
    return nearest;
}

void SampleWaveformEditor::paint(juce::Graphics& g)
{
    const auto bounds = plotBounds();
    if (bounds.isEmpty())
        return;

    const float startX = bounds.getX() + displayedStart * bounds.getWidth();
    const float endX = bounds.getX() + displayedEnd * bounds.getWidth();
    const auto [attackX, releaseX] = envelopeHandlePositions();
    g.setColour(juce::Colours::black.withAlpha(0.20f));
    g.fillRect(bounds.withRight(startX));
    g.fillRect(bounds.withLeft(endX));
    if (!hideEnvelopeControls)
    {
        juce::Path attackShade, releaseShade;
        attackShade.addTriangle(startX, bounds.getY(), attackX, bounds.getY(), startX, bounds.getBottom());
        releaseShade.addTriangle(releaseX, bounds.getY(), endX, bounds.getY(), endX, bounds.getBottom());
        g.fillPath(attackShade);
        g.fillPath(releaseShade);
    }
    g.setColour(juce::Colours::black);

    if (waveform != nullptr && waveform->samples > 0)
    {
        const int columns = juce::jlimit(1, PercussionSound::Waveform::bins,
            static_cast<int>(std::ceil(bounds.getWidth())));
        const int channels = juce::jlimit(1, 2, waveform->channels);
        const float channelHeight = bounds.getHeight() / channels;
        const float amplitude = channelHeight * 0.43f / juce::jmax(0.001f, waveform->peak);
        for (int channel = 0; channel < channels; ++channel)
        {
            const float centreY = bounds.getY() + (channel + 0.5f) * channelHeight;
            g.drawLine(bounds.getX(), centreY, bounds.getRight(), centreY, 0.5f);
            for (int column = 0; column < columns; ++column)
            {
                const int first = column * PercussionSound::Waveform::bins / columns;
                const int last = (column + 1) * PercussionSound::Waveform::bins / columns;
                float low = 0.0f, high = 0.0f;
                for (int bin = first; bin < last; ++bin)
                {
                    low = juce::jmin(low, waveform->minimum[(size_t) channel][(size_t) bin]);
                    high = juce::jmax(high, waveform->maximum[(size_t) channel][(size_t) bin]);
                }
                const float x = bounds.getX() + static_cast<float>(column) * bounds.getWidth() / columns;
                g.drawLine(x, centreY - juce::jmax(0.5f, high * amplitude),
                           x, centreY - juce::jmin(-0.5f, low * amplitude), 1.0f);
            }
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
    if (beatSnapTarget != BeatSnapTarget::off && hideEnvelopeControls
        && waveform != nullptr && waveform->samples > 0)
    {
        g.setColour(juce::Colour(0xff8e8b8b));
        const auto drawMarker = [&] (int frame)
        {
            const float x = bounds.getX() + static_cast<float>(frame)
                / waveform->samples * bounds.getWidth();
            juce::Path marker;
            marker.addTriangle(x - 3.0f, bounds.getY() - 6.0f,
                               x + 3.0f, bounds.getY() - 6.0f, x, bounds.getY() - 1.0f);
            g.fillPath(marker);
        };
        if (beatSnapTarget == BeatSnapTarget::transients)
        {
            for (const int frame : transientFrames)
                drawMarker(frame);
        }
        else if (const double step = gridStepFrames(); step > 0.0)
        {
            const double count = std::floor((waveform->samples - 0.5) / step);
            const double stride = juce::jmax(1.0, std::ceil(count / juce::jmax(1.0f, bounds.getWidth())));
            for (double index = 1.0; index <= count; index += stride)
                drawMarker(static_cast<int>(std::round(index * step)));
        }
        g.setColour(juce::Colours::black);
    }
    if (!hideEnvelopeControls)
    {
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.drawLine(startX, bounds.getBottom(), attackX, bounds.getY(), 0.75f);
        g.drawLine(releaseX, bounds.getY(), endX, bounds.getBottom(), 0.75f);
        g.setColour(juce::Colours::black);
        g.drawLine(attackX - 4.0f, bounds.getY() - 7.0f,
                   attackX + 4.0f, bounds.getY() - 23.0f, 1.5f);
        g.drawLine(releaseX - 4.0f, bounds.getY() - 23.0f,
                   releaseX + 4.0f, bounds.getY() - 7.0f, 1.5f);
    }
    for (const float x : { startX, endX })
    {
        g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 1.0f);
        g.fillRect(x - 4.0f, bounds.getCentreY() - 11.0f, 8.0f, 22.0f);
    }
}

void SampleWaveformEditor::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
        return;

    refresh();
    endDrag();
    if (waveform == nullptr || waveform->samples <= 0 || plotBounds().isEmpty())
        return;

    const auto bounds = plotBounds();
    if (event.position.y < bounds.getY())
    {
        if (hideEnvelopeControls)
            return;
        const auto [attackX, releaseX] = envelopeHandlePositions();
        const bool attackHandle = std::abs(event.position.x - attackX)
            < std::abs(event.position.x - releaseX)
            || (attackX == releaseX && event.position.x <= attackX);
        draggedParameter = attackHandle ? attackParameter : releaseParameter;
        const float handleX = attackHandle ? attackX : releaseX;
        dragOffset = std::abs(event.position.x - handleX) <= 10.0f
            ? event.position.x - handleX : 0.0f;
        if (draggedParameter != nullptr)
        {
            draggedParameter->beginChangeGesture();
            mouseDrag(event);
        }
        return;
    }
    const float startX = bounds.getX() + displayedStart * bounds.getWidth();
    const float endX = bounds.getX() + displayedEnd * bounds.getWidth();
    const bool startHandle = event.position.x <= (startX + endX) * 0.5f;
    const float handleX = startHandle ? startX : endX;
    draggedParameter = startHandle ? startParameter : endParameter;
    dragOffset = std::abs(event.position.x - handleX) <= 8.0f
        ? event.position.x - handleX : 0.0f;
    draggedParameter->beginChangeGesture();
    mouseDrag(event);
}

void SampleWaveformEditor::mouseDrag(const juce::MouseEvent& event)
{
    refresh();
    if (draggedParameter == nullptr || waveform == nullptr || waveform->samples <= 0)
        return;

    const auto bounds = plotBounds();
    const float gap = juce::jmax(1.0e-6f, 1.0f / waveform->samples);
    const float position = (event.position.x - dragOffset - bounds.getX())
        / juce::jmax(1.0f, bounds.getWidth());
    float value = 0.0f;
    if (draggedParameter == attackParameter || draggedParameter == releaseParameter)
    {
        const float duration = static_cast<float>(getDurationMs());
        const float distance = draggedParameter == attackParameter
            ? position - displayedStart : displayedEnd - position;
        value = std::round(juce::jlimit(0.0f,
            juce::jmin(PluginParameters::sampleEnvelopeMsMaximum,
                        (displayedEnd - displayedStart) * duration), distance * duration));
    }
    else if (beatSnapTarget != BeatSnapTarget::off && hideEnvelopeControls)
    {
        const bool movingStart = draggedParameter == startParameter;
        const int otherFrame = static_cast<int>(std::round(static_cast<double>(
            movingStart ? displayedEnd : displayedStart) * waveform->samples));
        const int nearestFrame = nearestSnapFrame(
            static_cast<double>(position) * waveform->samples, otherFrame, movingStart);
        // Host automation may have left the opposite handle at an invalid edge.
        if (nearestFrame < 0)
            return;
        value = static_cast<float>(nearestFrame) / waveform->samples;
        // Trim playback floors source positions. Avoid rounding down one frame
        // when storing an exact snap frame in a normalized float parameter.
        if (static_cast<double>(value) * waveform->samples < nearestFrame)
            value = std::nextafter(value, 1.0f);
    }
    else
        value = draggedParameter == startParameter
            ? juce::jlimit(0.0f, juce::jmax(0.0f, displayedEnd - gap), position)
            : juce::jlimit(juce::jmin(1.0f, displayedStart + gap), 1.0f, position);
    draggedParameter->setValueNotifyingHost(draggedParameter->convertTo0to1(value));
    refresh();
}

void SampleWaveformEditor::mouseUp(const juce::MouseEvent&) { endDrag(); }

void SampleWaveformEditor::visibilityChanged()
{
    if (!isShowing())
        endDrag();
}

void SampleWaveformEditor::endDrag()
{
    if (draggedParameter != nullptr)
        draggedParameter->endChangeGesture();
    draggedParameter = nullptr;
}
