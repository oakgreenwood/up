#include "SampleEqualiserEditor.h"
#include "../PluginProcessor.h"
#include "../Parameters/PluginParameters.h"
#include <cmath>

SampleEqualiserEditor::SampleEqualiserEditor(AudioPluginAudioProcessor& p)
    : processor(p), spectrum(p.getSampleSpectrum())
{
    setName("Sample equaliser");
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    for (size_t band = 0; band < frequencies.size(); ++band)
    {
        frequencies[band] = p.parameters.getParameter(PluginParameters::sampleEqFrequencyIds[band]);
        gains[band] = p.parameters.getParameter(PluginParameters::sampleEqGainIds[band]);
    }
    selectionChanged();
    spectrum.enabled.store(true, std::memory_order_relaxed);
    previousUpdateMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

SampleEqualiserEditor::~SampleEqualiserEditor()
{
    stopTimer();
    endDrag();
    spectrum.enabled.store(false, std::memory_order_relaxed);
}

juce::Rectangle<float> SampleEqualiserEditor::plotBounds() const
{
    // Align the plot with the centre of the one-pixel border, without padding.
    return getLocalBounds().toFloat().reduced(0.5f);
}

float SampleEqualiserEditor::maximumFrequency() const
{
    return juce::jmin(20000.0f, displaySampleRate * 0.45f);
}

float SampleEqualiserEditor::frequencyToX(float frequency) const
{
    const auto bounds = plotBounds();
    return bounds.getX() + bounds.getWidth()
        * std::log(juce::jlimit(20.0f, maximumFrequency(), frequency) / 20.0f)
        / std::log(maximumFrequency() / 20.0f);
}

float SampleEqualiserEditor::xToFrequency(float x) const
{
    const auto bounds = plotBounds();
    return 20.0f * std::pow(maximumFrequency() / 20.0f,
        juce::jlimit(0.0f, 1.0f, (x - bounds.getX()) / juce::jmax(1.0f, bounds.getWidth())));
}

float SampleEqualiserEditor::gainToY(float gain) const
{
    const auto bounds = plotBounds();
    return bounds.getCentreY() - gain * bounds.getHeight() / 30.0f;
}

float SampleEqualiserEditor::yToGain(float y) const
{
    const auto bounds = plotBounds();
    return juce::jlimit(-15.0f, 15.0f, (bounds.getCentreY() - y) * 30.0f / juce::jmax(1.0f, bounds.getHeight()));
}

float SampleEqualiserEditor::value(juce::RangedAudioParameter* parameter) const
{
    const float current = parameter->getValue();
    return parameter->convertFrom0to1(std::isfinite(current) ? current : parameter->getDefaultValue());
}

juce::Point<float> SampleEqualiserEditor::dotPosition(int band) const
{
    return dots[(size_t) band];
}

float SampleEqualiserEditor::responseDbAt(float frequency) const
{
    double magnitude = 1.0;
    for (size_t band = 0; band < displayedCoefficients.size(); ++band)
        magnitude *= SampleEqualiser::magnitude(displayedCoefficients[band], frequency, displaySampleRate);
    return static_cast<float>(20.0 * std::log10(juce::jmax(1.0e-12, magnitude)));
}

float SampleEqualiserEditor::responseY(float db) const
{
    const auto bounds = plotBounds();
    return juce::jlimit(bounds.getY(), bounds.getBottom(), gainToY(db));
}

void SampleEqualiserEditor::rebuildPlotPoints()
{
    const auto bounds = plotBounds();
    plotSegments = bounds.isEmpty() ? 0
        : juce::jlimit(1, maximumPlotSegments, juce::roundToInt(bounds.getWidth() * 2.0f));
    for (int i = 0; i <= plotSegments && plotSegments > 0; ++i)
    {
        auto& point = plotPoints[(size_t) i];
        point.x = i == plotSegments ? bounds.getRight()
            : bounds.getX() + bounds.getWidth() * static_cast<float>(i) / static_cast<float>(plotSegments);
        point.frequency = xToFrequency(point.x);
        point.bin = juce::jlimit(0.0f, static_cast<float>(levels.size() - 1),
            point.frequency * SampleSpectrum::fftSize / displaySampleRate);
    }
    responseDirty = true;
}

bool SampleEqualiserEditor::refreshResponse()
{
    bool changed = responseDirty;
    for (size_t band = 0; band < frequencies.size(); ++band)
    {
        const float frequency = value(frequencies[band]), gain = value(gains[band]);
        if (responseDirty || frequency != displayedFrequencies[band] || gain != displayedGains[band])
        {
            displayedFrequencies[band] = frequency;
            displayedGains[band] = gain;
            displayedCoefficients[band] = SampleEqualiser::coefficients(static_cast<int>(band),
                frequency, gain, displaySampleRate);
            changed = true;
        }
    }
    if (!changed)
        return false;
    responseDirty = false;
    responsePath.clear();
    if (plotSegments == 0)
        return true;
    for (int band = 0; band < 4; ++band)
    {
        const float x = frequencyToX(displayedFrequencies[(size_t) band]);
        dots[(size_t) band] = { x, gainToY(displayedGains[(size_t) band]) };
    }
    responsePath.startNewSubPath(plotPoints[0].x, responseY(responseDbAt(plotPoints[0].frequency)));
    for (int i = 1; i <= plotSegments; ++i)
    {
        const auto& point = plotPoints[(size_t) i];
        responsePath.lineTo(point.x, responseY(responseDbAt(point.frequency)));
    }
    return true;
}

void SampleEqualiserEditor::rebuildSpectrumPath()
{
    spectrumPath.clear();
    if (plotSegments == 0)
        return;
    const auto bounds = plotBounds();
    std::array<float, maximumPlotSegments + 1> displayLevels {};
    for (int i = 0; i <= plotSegments; ++i)
    {
        const auto& point = plotPoints[(size_t) i];
        const auto lower = static_cast<size_t>(point.bin);
        const auto upper = juce::jmin(lower + 1, levels.size() - 1);
        const float t = point.bin - static_cast<float>(lower);
        const float delta = levels[upper] - levels[lower];
        // Monotone cubic interpolation rounds sparse bass-bin transitions
        // without introducing peaks or dips beyond the measured endpoints.
        const auto tangent = [] (float a, float b)
        {
            return a * b > 0.0f ? 2.0f * a * b / (a + b) : 0.0f;
        };
        const float leftSlope = lower > 0 ? tangent(levels[lower] - levels[lower - 1], delta) : delta;
        const float rightSlope = upper + 1 < levels.size()
            ? tangent(delta, levels[upper + 1] - levels[upper]) : delta;
        const float t2 = t * t, t3 = t2 * t;
        float db = (2.0f * t3 - 3.0f * t2 + 1.0f) * levels[lower]
            + (t3 - 2.0f * t2 + t) * leftSlope
            + (-2.0f * t3 + 3.0f * t2) * levels[upper]
            + (t3 - t2) * rightSlope;
        db = juce::jlimit(juce::jmin(levels[lower], levels[upper]),
            juce::jmax(levels[lower], levels[upper]), db);
        // Preserve narrow peaks only when multiple bins share a segment;
        // imposing bin peaks on expanded bass segments would create steps.
        const float previousBin = i > 0 ? plotPoints[(size_t) i - 1].bin : point.bin;
        if (point.bin - previousBin > 1.0f)
            for (size_t bin = static_cast<size_t>(std::ceil(previousBin)); bin <= lower; ++bin)
                db = juce::jmax(db, levels[bin]);
        displayLevels[(size_t) i] = db;
    }
    spectrumPath.startNewSubPath(bounds.getX(), bounds.getBottom());
    // A small triangular average softens corners over roughly three pixels
    // either side. Keep the FFT levels intact so smoothing never accumulates.
    constexpr int smoothingRadius = 6;
    constexpr float weightSum = (smoothingRadius + 1) * (smoothingRadius + 1);
    for (int i = 0; i <= plotSegments; ++i)
    {
        float smoothedDb = 0.0f;
        for (int offset = -smoothingRadius; offset <= smoothingRadius; ++offset)
        {
            const int index = juce::jlimit(0, plotSegments, i + offset);
            const float weight = static_cast<float>(smoothingRadius + 1 - std::abs(offset));
            smoothedDb += displayLevels[(size_t) index] * weight;
        }
        smoothedDb /= weightSum;
        spectrumPath.lineTo(plotPoints[(size_t) i].x,
            bounds.getBottom() - bounds.getHeight() * (smoothedDb + 90.0f) / 90.0f);
    }
    spectrumPath.lineTo(bounds.getRight(), bounds.getBottom());
    spectrumPath.closeSubPath();
}

void SampleEqualiserEditor::resized()
{
    rebuildPlotPoints();
    refreshResponse();
    rebuildSpectrumPath();
}

int SampleEqualiserEditor::selectedNote() const
{
    const int group = processor.getSelectedSampleGroupIndex();
    const auto& groups = processor.getSampleGroups();
    return group >= 0 && group < static_cast<int>(groups.size()) ? groups[(size_t) group].midiNote : -1;
}

void SampleEqualiserEditor::selectionChanged()
{
    endDrag();
    displayedNote = selectedNote();
    levels.fill(-90.0f);
    SampleSpectrum::Frame discarded;
    for (int i = 0; i < 4 && spectrum.pop(discarded); ++i) {}
    displaySampleRate = spectrum.sampleRate.load(std::memory_order_relaxed);
    rebuildPlotPoints();
    refreshResponse();
    rebuildSpectrumPath();
    previousUpdateMs = juce::Time::getMillisecondCounterHiRes();
    repaint();
}

void SampleEqualiserEditor::timerCallback()
{
    if (displayedNote != selectedNote())
        selectionChanged();
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float decayDb = static_cast<float>(juce::jmax(0.0, nowMs - previousUpdateMs) * 0.06);
    previousUpdateMs = nowMs;
    if (draggedBand >= 0 && dragGroup != processor.getSelectedSampleGroupIndex())
        endDrag();
    const auto rate = spectrum.sampleRate.load(std::memory_order_relaxed);
    bool spectrumChanged = false;
    if (rate != displaySampleRate)
    {
        displaySampleRate = rate;
        levels.fill(-90.0f);
        rebuildPlotPoints();
        spectrumChanged = true;
    }
    // Keep only the newest matching complete frame. Stereo powers are combined
    // after the FFT so opposite-polarity channels cannot erase the spectrum.
    SampleSpectrum::Frame incoming, latest;
    bool haveFrame = false;
    const int note = selectedNote();
    for (int i = 0; i < 4 && spectrum.pop(incoming); ++i)
        if (incoming.note == note && incoming.sampleRate == displaySampleRate)
        {
            latest = incoming;
            haveFrame = true;
        }
    for (auto& level : levels)
    {
        const float decayed = juce::jmax(-90.0f, level - decayDb);
        spectrumChanged = spectrumChanged || decayed != level;
        level = decayed;
    }
    if (haveFrame)
    {
        fftLeft.fill(0.0f);
        fftRight.fill(0.0f);
        for (size_t i = 0; i < SampleSpectrum::fftSize; ++i)
        {
            fftLeft[i] = std::isfinite(latest.left[i]) ? latest.left[i] : 0.0f;
            fftRight[i] = std::isfinite(latest.right[i]) ? latest.right[i] : 0.0f;
        }
        window.multiplyWithWindowingTable(fftLeft.data(), SampleSpectrum::fftSize);
        window.multiplyWithWindowingTable(fftRight.data(), SampleSpectrum::fftSize);
        fft.performFrequencyOnlyForwardTransform(fftLeft.data());
        fft.performFrequencyOnlyForwardTransform(fftRight.data());
        constexpr double normalisation = 4.0 / SampleSpectrum::fftSize;
        for (size_t i = 0; i < levels.size(); ++i)
        {
            const double l = fftLeft[i] * normalisation, r = fftRight[i] * normalisation;
            const double power = 0.5 * (l * l + r * r);
            const float db = std::isfinite(power) ? static_cast<float>(10.0 * std::log10(juce::jmax(1.0e-9, power))) : -90.0f;
            const float newLevel = juce::jmax(levels[i], juce::jlimit(-90.0f, 0.0f, db));
            spectrumChanged = spectrumChanged || newLevel != levels[i];
            levels[i] = newLevel;
        }
    }
    const bool responseChanged = refreshResponse();
    if (spectrumChanged)
        rebuildSpectrumPath();
    if (spectrumChanged || responseChanged)
        repaint();
}

void SampleEqualiserEditor::paint(juce::Graphics& g)
{
    refreshResponse();
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(plotBounds().toNearestInt());
        g.setColour(juce::Colour(0xff8e8b8b));
        g.fillPath(spectrumPath);
        g.setColour(juce::Colours::black);
        g.strokePath(responsePath, juce::PathStrokeType(2.0f));
    }
    for (int band = 0; band < 4; ++band)
    {
        const auto dot = dotPosition(band);
        const float radius = band == draggedBand ? 8.0f : 7.0f;
        g.setColour(juce::Colours::black);
        g.fillEllipse(dot.x - radius, dot.y - radius, radius * 2.0f, radius * 2.0f);
    }
    g.setColour(juce::Colours::black);
    g.drawRect(getLocalBounds().toFloat().reduced(0.5f), 1.0f);
}

void SampleEqualiserEditor::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown() || selectedNote() < 0)
        return;
    endDrag();
    refreshResponse();
    float nearest = 16.0f;
    // Cycle coincident dots so all four remain reachable after overlap.
    for (int i = 0; i < 4; ++i)
    {
        const int band = (lastTouchedBand + 1 + i) % 4;
        const float distance = dotPosition(band).getDistanceFrom(event.position);
        if (distance < nearest)
        {
            nearest = distance;
            draggedBand = band;
        }
    }
    if (draggedBand >= 0)
    {
        lastTouchedBand = draggedBand;
        dragGroup = processor.getSelectedSampleGroupIndex();
        dragOffset = dotPosition(draggedBand) - event.position;
        frequencies[(size_t) draggedBand]->beginChangeGesture();
        gains[(size_t) draggedBand]->beginChangeGesture();
        if (onSelected) onSelected();
        repaint();
    }
}

void SampleEqualiserEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedBand < 0)
        return;
    if (dragGroup != processor.getSelectedSampleGroupIndex())
    {
        endDrag();
        return;
    }
    const auto position = event.position + dragOffset;
    refreshResponse();
    auto* frequency = frequencies[(size_t) draggedBand];
    auto* gain = gains[(size_t) draggedBand];
    const float newFrequency = xToFrequency(position.x);
    const float newGain = yToGain(position.y);
    frequency->setValueNotifyingHost(frequency->convertTo0to1(newFrequency));
    gain->setValueNotifyingHost(gain->convertTo0to1(newGain));
    refreshResponse();
    repaint();
}

void SampleEqualiserEditor::mouseUp(const juce::MouseEvent&)
{
    endDrag();
}

void SampleEqualiserEditor::endDrag()
{
    if (draggedBand < 0)
        return;
    const auto band = static_cast<size_t>(draggedBand);
    draggedBand = -1;
    frequencies[band]->endChangeGesture();
    gains[band]->endChangeGesture();
    repaint();
}
