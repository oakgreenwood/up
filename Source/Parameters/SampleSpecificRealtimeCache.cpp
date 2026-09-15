#include "SampleSpecificRealtimeCache.h"
#include "PluginParameters.h"

#include <cmath>

#include <juce_core/juce_core.h>

static_assert(std::atomic<float>::is_always_lock_free);

SampleSpecificRealtimeCache::SampleSpecificRealtimeCache()
{
    reset();
}

void SampleSpecificRealtimeCache::reset() noexcept
{
    for (auto& semitones : formantSemitonesByMidiNote)
        semitones.store(PluginParameters::sampleFormantSemitonesDefault, std::memory_order_relaxed);
    for (auto& ratio : formantRatioByMidiNote)
        ratio.store(1.0f, std::memory_order_relaxed);
    for (auto& gainDb : gainDbByMidiNote)
        gainDb.store(PluginParameters::sampleGainDbDefault, std::memory_order_relaxed);
    for (auto& gainLinear : gainLinearByMidiNote)
        gainLinear.store(1.0f, std::memory_order_relaxed);

    for (auto& pitchRatio : pitchRatioByMidiNote)
        pitchRatio.store(1.0f, std::memory_order_relaxed);
    for (auto& semitones : pitchSemitonesByMidiNote)
        semitones.store(0.0f, std::memory_order_relaxed);

    for (auto& punchAmount : punchAmountByMidiNote)
        punchAmount.store(PluginParameters::samplePunchDefault, std::memory_order_relaxed);
    for (auto& monoAmount : monoAmountByMidiNote)
        monoAmount.store(PluginParameters::sampleMonoAmountDefault, std::memory_order_relaxed);
    for (auto& pan : panByMidiNote)
        pan.store(PluginParameters::samplePanDefault, std::memory_order_relaxed);
}

void SampleSpecificRealtimeCache::setFormantSemitonesForMidiNote(int midiNote, float semitones) noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount || !std::isfinite(semitones))
        return;

    semitones = juce::jlimit(PluginParameters::sampleFormantSemitonesMinimum,
                            PluginParameters::sampleFormantSemitonesMaximum, semitones);
    formantRatioByMidiNote[(size_t) midiNote].store(std::exp2(semitones / 12.0f), std::memory_order_relaxed);
    formantSemitonesByMidiNote[(size_t) midiNote].store(semitones, std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getFormantSemitonesForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? formantSemitonesByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed) : 0.0f;
}

float SampleSpecificRealtimeCache::getFormantRatioForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? formantRatioByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed) : 1.0f;
}

void SampleSpecificRealtimeCache::setPitchSemitonesForMidiNote(int midiNote, float semitones) noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount || !std::isfinite(semitones))
        return;

    semitones = juce::jlimit(PluginParameters::samplePitchSemitonesMinimum,
                            PluginParameters::samplePitchSemitonesMaximum, semitones);
    const auto pitchRatio = static_cast<float>(std::pow(2.0, static_cast<double>(semitones) / 12.0));
    if (!std::isfinite(pitchRatio) || pitchRatio <= 0.0f)
        return;

    pitchRatioByMidiNote[(size_t) midiNote].store(pitchRatio, std::memory_order_relaxed);
    pitchSemitonesByMidiNote[(size_t) midiNote].store(semitones, std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getPitchRatioForMidiNote(int midiNote) const noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount)
        return 1.0f;

    const float pitchRatio = pitchRatioByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed);
    return (std::isfinite(pitchRatio) && pitchRatio > 0.0f) ? pitchRatio : 1.0f;
}

float SampleSpecificRealtimeCache::getPitchSemitonesForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? pitchSemitonesByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed) : 0.0f;
}

void SampleSpecificRealtimeCache::setPunchAmountForMidiNote(int midiNote, float amount) noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount || !std::isfinite(amount))
        return;

    const float clamped = juce::jlimit(PluginParameters::samplePunchMinimum,
                                      PluginParameters::samplePunchMaximum, amount);
    const float snapped = std::round(clamped / PluginParameters::samplePunchInterval)
        * PluginParameters::samplePunchInterval;
    punchAmountByMidiNote[(size_t) midiNote].store(
        juce::jlimit(PluginParameters::samplePunchMinimum,
                     PluginParameters::samplePunchMaximum, snapped),
        std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getPunchAmountForMidiNote(int midiNote) const noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount)
        return PluginParameters::samplePunchDefault;

    const float amount = punchAmountByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed);
    return std::isfinite(amount)
        ? juce::jlimit(PluginParameters::samplePunchMinimum,
                       PluginParameters::samplePunchMaximum, amount)
        : PluginParameters::samplePunchDefault;
}

void SampleSpecificRealtimeCache::setMonoAmountForMidiNote(int midiNote, float amount) noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount || !std::isfinite(amount))
        return;

    const float clamped = juce::jlimit(PluginParameters::sampleMonoAmountMinimum,
                                      PluginParameters::sampleMonoAmountMaximum, amount);
    const float snapped = std::round(clamped / PluginParameters::sampleMonoAmountInterval)
        * PluginParameters::sampleMonoAmountInterval;
    monoAmountByMidiNote[(size_t) midiNote].store(
        juce::jlimit(PluginParameters::sampleMonoAmountMinimum,
                     PluginParameters::sampleMonoAmountMaximum, snapped),
        std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getMonoAmountForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? monoAmountByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed)
        : PluginParameters::sampleMonoAmountDefault;
}

void SampleSpecificRealtimeCache::setPanForMidiNote(int midiNote, float position) noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount || !std::isfinite(position))
        return;

    const float clamped = juce::jlimit(PluginParameters::samplePanMinimum,
                                      PluginParameters::samplePanMaximum, position);
    const float snapped = std::round(clamped / PluginParameters::samplePanInterval)
        * PluginParameters::samplePanInterval;
    panByMidiNote[(size_t) midiNote].store(snapped, std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getPanForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? panByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed)
        : PluginParameters::samplePanDefault;
}

void SampleSpecificRealtimeCache::setGainDbForMidiNote(int midiNote, float decibels) noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount || !std::isfinite(decibels))
        return;

    decibels = juce::jlimit(PluginParameters::sampleGainDbMinimum,
                           PluginParameters::sampleGainDbMaximum, decibels);
    const float linear = std::pow(10.0f, decibels / 20.0f);
    gainLinearByMidiNote[(size_t) midiNote].store(linear, std::memory_order_relaxed);
    gainDbByMidiNote[(size_t) midiNote].store(decibels, std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getGainDbForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? gainDbByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed)
        : PluginParameters::sampleGainDbDefault;
}

float SampleSpecificRealtimeCache::getGainLinearForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? gainLinearByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed) : 1.0f;
}
