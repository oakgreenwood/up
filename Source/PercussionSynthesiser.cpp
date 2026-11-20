#include "PercussionSynthesiser.h"
#include "PercussionVoice.h"

#include <algorithm>
#include <cmath>

void PercussionSynthesiser::renderAudioRange(juce::AudioBuffer<float>& output, int start, int count)
{
    if (count <= 0)
        return;
    const juce::ScopedLock guard(lock);
    if (lastPlayedMidiNote >= 0)
        idleFrames = juce::jmin(sequenceTimeoutFrames, idleFrames + static_cast<uint64_t>(count));
    if (output.getNumChannels() > 0)
        renderVoices(output, start, count);
}

void PercussionSynthesiser::dispatchMidi(const juce::MidiMessage& message)
{
    const juce::ScopedLock guard(lock);
    handleMidiEvent(message);
}

bool PercussionSynthesiser::hasActiveVoices() const noexcept
{
    return std::any_of(voices.begin(), voices.end(),
                       [] (const auto* voice) { return voice->isVoiceActive(); });
}

void PercussionSynthesiser::clearLayerMappings()
{
    downNoteRoots.fill(-1);
    registeredByMidiNote.clear();
    noteLayers.clear();
    resetPlaybackSequence();
}

void PercussionSynthesiser::resetPlaybackSequence() noexcept
{
    lastPlayedMidiNote = -1;
    nextDirection = SampleDirection::up;
    idleFrames = 0;
    lastPlayedTimeMs = 0.0;
    const double rate = getSampleRate();
    const double safeRate = std::isfinite(rate) && rate >= 1.0 && rate <= 768000.0 ? rate : 44100.0;
    sequenceTimeoutFrames = static_cast<uint64_t>(std::ceil(safeRate * 4.0));
}

void PercussionSynthesiser::registerLayeredSound(PercussionSound* sound,
                                              int midiNote,
                                              int velocityGroupIndex,
                                              int variationIndex,
                                              SampleDirection direction)
{
    if (sound == nullptr || midiNote < 0 || midiNote > 127)
        return;

    RegisteredSound item;
    item.sound = sound;
    item.velocityGroupIndex = juce::jmax(1, velocityGroupIndex);
    item.variationIndex = juce::jmax(1, variationIndex);
    item.direction = direction;
    sound->setVariationIndex(item.variationIndex);
    sound->setDirection(direction);

    registeredByMidiNote[midiNote].push_back(item);
}

std::pair<int, int> PercussionSynthesiser::computeVelocityRange(int groupIndex, int groupCount) noexcept
{
    groupIndex = juce::jlimit(1, juce::jmax(1, groupCount), groupIndex);
    groupCount = juce::jmax(1, groupCount);

    if (groupCount == 4)
    {
        switch (groupIndex)
        {
            case 1: return { 1, 31 };
            case 2: return { 32, 63 };
            case 3: return { 64, 100 };
            default: return { 101, 127 };
        }
    }

    const auto start = static_cast<int>(std::ceil(((double) (groupIndex - 1) * 128.0)
                                                   / (double) groupCount));
    const auto end = static_cast<int>(std::ceil(((double) groupIndex * 128.0)
                                                 / (double) groupCount)) - 1;

    int minVelocity = juce::jlimit(1, 127, start);
    int maxVelocity = juce::jlimit(1, 127, end);
    if (groupIndex == 1)
        minVelocity = 1;
    if (groupIndex == groupCount)
        maxVelocity = 127;
    if (maxVelocity < minVelocity)
        maxVelocity = minVelocity;

    return { minVelocity, maxVelocity };
}

void PercussionSynthesiser::finalizeLayerMappings()
{
    noteLayers.clear();
    resetPlaybackSequence();

    for (auto& entry : registeredByMidiNote)
    {
        const int midiNote = entry.first;
        auto& registered = entry.second;
        if (registered.empty())
            continue;

        std::sort(registered.begin(), registered.end(),
                  [] (const RegisteredSound& a, const RegisteredSound& b)
                  {
                      if (a.velocityGroupIndex != b.velocityGroupIndex)
                          return a.velocityGroupIndex < b.velocityGroupIndex;
                      return a.variationIndex < b.variationIndex;
                  });

        int groupCount = 1;
        for (const auto& item : registered)
            groupCount = juce::jmax(groupCount, item.velocityGroupIndex);

        NoteLayers layers;
        layers.groups.resize((size_t) groupCount);
        for (int g = 1; g <= groupCount; ++g)
        {
            auto [minVelocity, maxVelocity] = computeVelocityRange(g, groupCount);
            auto& group = layers.groups[(size_t) (g - 1)];
            group.index = g;
            group.minVelocity = minVelocity;
            group.maxVelocity = maxVelocity;
        }

        for (const auto& item : registered)
        {
            const int idx = juce::jlimit(0, groupCount - 1, item.velocityGroupIndex - 1);
            auto& group = layers.groups[(size_t) idx];
            if (item.sound != nullptr)
            {
                item.sound->setVelocityLayerInfo(group.index,
                                                 groupCount,
                                                 group.minVelocity,
                                                 group.maxVelocity);
                group.pools[static_cast<size_t>(item.direction)].sounds.push_back(item.sound);
                layers.hasDirectionalSamples |= item.direction != SampleDirection::none;
            }
        }

        noteLayers[midiNote] = std::move(layers);
    }
}

int PercussionSynthesiser::velocityToMidi(float velocity) noexcept
{
    const float clamped = juce::jlimit(0.0f, 1.0f, velocity);
    return juce::jlimit(1, 127, (int) std::lround(clamped * 127.0f));
}

int PercussionSynthesiser::chooseGroupForVelocity(const NoteLayers& layers, int midiVelocity) noexcept
{
    if (layers.groups.empty())
        return -1;

    for (size_t i = 0; i < layers.groups.size(); ++i)
    {
        const auto& group = layers.groups[i];
        if (midiVelocity >= group.minVelocity && midiVelocity <= group.maxVelocity)
            return (int) i;
    }

    if (midiVelocity < layers.groups.front().minVelocity)
        return 0;
    return (int) layers.groups.size() - 1;
}

PercussionSynthesiser::VariationPool* PercussionSynthesiser::findNearestPool(
    NoteLayers& layers, int preferredGroupIdx, SampleDirection direction) noexcept
{
    if (preferredGroupIdx < 0 || preferredGroupIdx >= (int) layers.groups.size())
        return nullptr;

    const auto directionIndex = static_cast<size_t>(direction);
    auto& preferred = layers.groups[(size_t) preferredGroupIdx].pools[directionIndex];
    if (!preferred.sounds.empty())
        return &preferred;

    for (int delta = 1; delta < (int) layers.groups.size(); ++delta)
    {
        const int up = preferredGroupIdx + delta;
        if (up < (int) layers.groups.size())
        {
            auto& pool = layers.groups[(size_t) up].pools[directionIndex];
            if (!pool.sounds.empty())
                return &pool;
        }

        const int down = preferredGroupIdx - delta;
        if (down >= 0)
        {
            auto& pool = layers.groups[(size_t) down].pools[directionIndex];
            if (!pool.sounds.empty())
                return &pool;
        }
    }

    return nullptr;
}

PercussionSound* PercussionSynthesiser::chooseVariation(NoteLayers& layers,
                                                       int preferredGroupIdx,
                                                       SampleDirection direction) noexcept
{
    auto* pool = findNearestPool(layers, preferredGroupIdx, direction);
    if (pool == nullptr && direction != SampleDirection::none)
    {
        // Exhaust matching directions across velocity layers before falling
        // back to ordinary samples, then to the opposite direction.
        pool = findNearestPool(layers, preferredGroupIdx, SampleDirection::none);
        if (pool == nullptr)
            pool = findNearestPool(layers, preferredGroupIdx,
                direction == SampleDirection::up ? SampleDirection::down : SampleDirection::up);
    }

    if (pool == nullptr)
        return nullptr;

    auto& sounds = pool->sounds;
    int choice = rng.nextInt((int) sounds.size());
    if (sounds.size() > 1 && choice == pool->lastChoice && rng.nextDouble() < 0.65)
    {
        int reroll = rng.nextInt((int) sounds.size() - 1);
        if (reroll >= pool->lastChoice)
            ++reroll;
        choice = reroll;
    }

    pool->lastChoice = choice;
    return sounds[(size_t) choice];
}

void PercussionSynthesiser::rememberPlayedGroup(int midiNote, SampleDirection direction, double timeMs) noexcept
{
    lastPlayedMidiNote = midiNote;
    nextDirection = direction == SampleDirection::up ? SampleDirection::down : SampleDirection::up;
    idleFrames = 0;
    lastPlayedTimeMs = timeMs;
}

juce::SynthesiserVoice* PercussionSynthesiser::findVoiceToSteal(
    juce::SynthesiserSound* soundToPlay, int midiChannel, int midiNoteNumber) const
{
    if (const auto* sound = dynamic_cast<const PercussionSound*>(soundToPlay))
        if (sound->getDirection() != SampleDirection::none)
        {
            const auto opposite = sound->getDirection() == SampleDirection::up
                ? SampleDirection::down : SampleDirection::up;
            // Preserve opposite-direction fades whenever another victim exists.
            juce::SynthesiserVoice* oldest = nullptr;
            for (auto* voice : voices)
                if (auto* percussion = dynamic_cast<PercussionVoice*>(voice))
                    if (!percussion->isPlayingDirectionalSample(getGroupRootNote(midiNoteNumber), opposite)
                        && voice->canPlaySound(soundToPlay))
                        if (oldest == nullptr || voice->wasStartedBefore(*oldest))
                            oldest = voice;
            if (oldest != nullptr)
                return oldest;
        }

    return juce::Synthesiser::findVoiceToSteal(soundToPlay, midiChannel, midiNoteNumber);
}

void PercussionSynthesiser::noteOn(int midiChannel, int midiNoteNumber, float velocity)
{
    const juce::ScopedLock sl(lock);

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (lastPlayedMidiNote >= 0
        && (idleFrames >= sequenceTimeoutFrames || nowMs - lastPlayedTimeMs >= 4000.0))
        resetPlaybackSequence();

    PercussionSound* selected = nullptr;
    SampleDirection direction = SampleDirection::none;

    const int rootNote = getGroupRootNote(midiNoteNumber);
    const auto it = noteLayers.find(rootNote);
    if (it != noteLayers.end())
    {
        const int midiVelocity = velocityToMidi(velocity);
        const int groupIdx = chooseGroupForVelocity(it->second, midiVelocity);
        if (it->second.hasDirectionalSamples)
            direction = midiNoteNumber == rootNote ? SampleDirection::up : SampleDirection::down;
        selected = chooseVariation(it->second, groupIdx, direction);
    }

    if (selected != nullptr)
    {
        const auto actualDirection = selected->getDirection();
        const auto opposite = actualDirection == SampleDirection::up ? SampleDirection::down
            : (actualDirection == SampleDirection::down ? SampleDirection::up : SampleDirection::none);
        for (auto* voice : voices)
            if (voice->getCurrentlyPlayingNote() == midiNoteNumber && voice->isPlayingChannel(midiChannel))
            {
                // An opposite-direction voice retains its output until the new hit
                // reaches the same effect latency, then fades independently.
                if (opposite != SampleDirection::none)
                    if (auto* percussion = dynamic_cast<PercussionVoice*>(voice))
                        if (percussion->isPlayingDirectionalSample(rootNote, opposite))
                            continue;
                stopVoice(voice, 1.0f, true);
            }

        if (auto* voice = findFreeVoice(selected, midiChannel, midiNoteNumber, isNoteStealingEnabled()))
        {
            if (opposite != SampleDirection::none)
                for (auto* activeVoice : voices)
                    if (activeVoice != voice)
                        if (auto* percussion = dynamic_cast<PercussionVoice*>(activeVoice))
                            if (percussion->isPlayingDirectionalSample(rootNote, opposite))
                                percussion->fadeOutForOppositeSample();
            startVoice(voice, selected, midiChannel, midiNoteNumber, velocity);
            rememberPlayedGroup(rootNote, direction, nowMs);
        }
        return;
    }

    // Fallback to JUCE default behavior if this note has no layered mapping.
    for (auto* sound : sounds)
    {
        if (sound->appliesToNote(midiNoteNumber) && sound->appliesToChannel(midiChannel))
        {
            for (auto* voice : voices)
                if (voice->getCurrentlyPlayingNote() == midiNoteNumber && voice->isPlayingChannel(midiChannel))
                    stopVoice(voice, 1.0f, true);

            if (auto* voice = findFreeVoice(sound, midiChannel, midiNoteNumber, isNoteStealingEnabled()))
            {
                startVoice(voice, sound, midiChannel, midiNoteNumber, velocity);
                rememberPlayedGroup(midiNoteNumber, SampleDirection::none, nowMs);
            }
        }
    }
}
