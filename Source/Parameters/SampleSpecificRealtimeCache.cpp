#include "SampleSpecificRealtimeCache.h"
#include "PluginParameters.h"

#include <cmath>

#include <juce_core/juce_core.h>

static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

SampleSpecificRealtimeCache::SampleSpecificRealtimeCache()
{
    reset();
}

void SampleSpecificRealtimeCache::reset() noexcept
{
    for (int band = 0; band < 4; ++band)
        for (int note = 0; note < midiNoteCount; ++note)
        {
            setEqFrequencyForMidiNote(note, band, PluginParameters::sampleEqDefaultFrequencies[(size_t) band]);
            setEqGainForMidiNote(note, band, 0.0f);
        }
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
    for (auto& start : startByMidiNote)
        start.store(0.0f, std::memory_order_relaxed);
    for (auto& end : endByMidiNote)
        end.store(1.0f, std::memory_order_relaxed);
    for (auto& duration : trimReferenceSecondsByMidiNote)
        duration.store(0.0f, std::memory_order_relaxed);
    for (auto& endTrim : endTrimSecondsByMidiNote)
        endTrim.store(0.0f, std::memory_order_relaxed);
    for (auto& attack : attackMsByMidiNote)
        attack.store(PluginParameters::sampleAttackMsDefault, std::memory_order_relaxed);
    for (auto& release : releaseMsByMidiNote)
        release.store(PluginParameters::sampleReleaseMsDefault, std::memory_order_relaxed);
    for (auto& bpm : loopBpmByMidiNote)
        bpm.store(0.0f, std::memory_order_relaxed);
    for (auto& tape : loopTapeModeByMidiNote)
        tape.store(false, std::memory_order_relaxed);
}

void SampleSpecificRealtimeCache::setLoopBpmForMidiNote(int note, float bpm) noexcept
{
    if (note >= 0 && note < midiNoteCount && std::isfinite(bpm))
        loopBpmByMidiNote[(size_t) note].store(std::round(juce::jlimit(0.0f, 300.0f, bpm)),
                                              std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getLoopBpmForMidiNote(int note) const noexcept
{
    return note >= 0 && note < midiNoteCount
        ? loopBpmByMidiNote[(size_t) note].load(std::memory_order_relaxed) : 0.0f;
}

void SampleSpecificRealtimeCache::setLoopTapeModeForMidiNote(int note, bool tape) noexcept
{
    if (note >= 0 && note < midiNoteCount)
        loopTapeModeByMidiNote[(size_t) note].store(tape, std::memory_order_relaxed);
}

bool SampleSpecificRealtimeCache::getLoopTapeModeForMidiNote(int note) const noexcept
{
    return note >= 0 && note < midiNoteCount
        && loopTapeModeByMidiNote[(size_t) note].load(std::memory_order_relaxed);
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

void SampleSpecificRealtimeCache::setStartForMidiNote(int midiNote, float position) noexcept
{
    if (midiNote >= 0 && midiNote < midiNoteCount && std::isfinite(position))
        startByMidiNote[(size_t) midiNote].store(juce::jlimit(0.0f, 1.0f, position),
                                               std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getStartForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? startByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed) : 0.0f;
}

void SampleSpecificRealtimeCache::setEndForMidiNote(int midiNote, float position) noexcept
{
    if (midiNote >= 0 && midiNote < midiNoteCount && std::isfinite(position))
    {
        const auto index = (size_t) midiNote;
        const float end = juce::jlimit(0.0f, 1.0f, position);
        const float referenceSeconds = trimReferenceSecondsByMidiNote[index].load(std::memory_order_relaxed);
        endTrimSecondsByMidiNote[index].store((1.0f - end) * referenceSeconds,
                                             std::memory_order_relaxed);
        endByMidiNote[index].store(end, std::memory_order_relaxed);
    }
}

float SampleSpecificRealtimeCache::getEndForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? endByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed) : 1.0f;
}

void SampleSpecificRealtimeCache::setTrimReferenceDurationForMidiNote(int midiNote, float seconds) noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount || !std::isfinite(seconds))
        return;
    const auto index = (size_t) midiNote;
    const float duration = juce::jmax(0.0f, seconds);
    trimReferenceSecondsByMidiNote[index].store(duration, std::memory_order_relaxed);
    const float end = endByMidiNote[index].load(std::memory_order_relaxed);
    endTrimSecondsByMidiNote[index].store((1.0f - end) * duration, std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getEndTrimSecondsForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? endTrimSecondsByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed) : 0.0f;
}

void SampleSpecificRealtimeCache::setAttackMsForMidiNote(int midiNote, float milliseconds) noexcept
{
    if (midiNote >= 0 && midiNote < midiNoteCount && std::isfinite(milliseconds))
        attackMsByMidiNote[(size_t) midiNote].store(
            std::round(juce::jlimit(0.0f, PluginParameters::sampleEnvelopeMsMaximum, milliseconds)),
            std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getAttackMsForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? attackMsByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed)
        : PluginParameters::sampleAttackMsDefault;
}

void SampleSpecificRealtimeCache::setReleaseMsForMidiNote(int midiNote, float milliseconds) noexcept
{
    if (midiNote >= 0 && midiNote < midiNoteCount && std::isfinite(milliseconds))
        releaseMsByMidiNote[(size_t) midiNote].store(
            std::round(juce::jlimit(0.0f, PluginParameters::sampleEnvelopeMsMaximum, milliseconds)),
            std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getReleaseMsForMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < midiNoteCount
        ? releaseMsByMidiNote[(size_t) midiNote].load(std::memory_order_relaxed)
        : PluginParameters::sampleReleaseMsDefault;
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

void SampleSpecificRealtimeCache::setEqFrequencyForMidiNote(int note, int band, float value) noexcept
{
    if (note >= 0 && note < midiNoteCount && band >= 0 && band < 4 && std::isfinite(value))
        eqFrequency[(size_t) band][(size_t) note].store(juce::jlimit(20.0f, 20000.0f, value), std::memory_order_relaxed);
}

void SampleSpecificRealtimeCache::setEqGainForMidiNote(int note, int band, float value) noexcept
{
    if (note >= 0 && note < midiNoteCount && band >= 0 && band < 4 && std::isfinite(value))
        eqGain[(size_t) band][(size_t) note].store(std::round(juce::jlimit(-15.0f, 15.0f, value) * 10.0f) / 10.0f,
                                               std::memory_order_relaxed);
}

float SampleSpecificRealtimeCache::getEqFrequencyForMidiNote(int note, int band) const noexcept
{
    if (band < 0 || band >= 4)
        return 1000.0f;
    return note >= 0 && note < midiNoteCount
        ? eqFrequency[(size_t) band][(size_t) note].load(std::memory_order_relaxed)
        : PluginParameters::sampleEqDefaultFrequencies[(size_t) band];
}

float SampleSpecificRealtimeCache::getEqGainForMidiNote(int note, int band) const noexcept
{
    return note >= 0 && note < midiNoteCount && band >= 0 && band < 4
        ? eqGain[(size_t) band][(size_t) note].load(std::memory_order_relaxed) : 0.0f;
}
