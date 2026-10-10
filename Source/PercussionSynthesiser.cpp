#include "PercussionSynthesiser.h"

#include <algorithm>
#include <cmath>

void PercussionSynthesiser::renderAudioRange(juce::AudioBuffer<float>& output, int start, int count)
{
    if (count <= 0 || output.getNumChannels() == 0)
        return;
    const juce::ScopedLock guard(lock);
    renderVoices(output, start, count);
}

void PercussionSynthesiser::dispatchMidi(const juce::MidiMessage& message)
{
    const juce::ScopedLock guard(lock);
    handleMidiEvent(message);
}

void PercussionSynthesiser::clearLayerMappings()
{
    registeredByMidiNote.clear();
    noteLayers.clear();
    lastVariationIndexByNote.clear();
}

void PercussionSynthesiser::registerLayeredSound(PercussionSound* sound,
                                              int midiNote,
                                              int velocityGroupIndex,
                                              int variationIndex)
{
    if (sound == nullptr || midiNote < 0 || midiNote > 127)
        return;

    RegisteredSound item;
    item.sound = sound;
    item.velocityGroupIndex = juce::jmax(1, velocityGroupIndex);
    item.variationIndex = juce::jmax(1, variationIndex);
    sound->setVariationIndex(item.variationIndex);

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
    lastVariationIndexByNote.clear();

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
                group.sounds.push_back(item.sound);
            }
        }

        noteLayers[midiNote] = std::move(layers);
        lastVariationIndexByNote[midiNote] = std::vector<int>(noteLayers[midiNote].groups.size(), -1);
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

PercussionSound* PercussionSynthesiser::chooseVariation(int midiNote,
                                                      const NoteLayers& layers,
                                                      int preferredGroupIdx) noexcept
{
    if (preferredGroupIdx < 0 || preferredGroupIdx >= (int) layers.groups.size())
        return nullptr;

    int resolvedGroupIdx = preferredGroupIdx;
    const std::vector<PercussionSound*>* pool = &layers.groups[(size_t) preferredGroupIdx].sounds;

    if (pool->empty())
    {
        for (int delta = 1; delta < (int) layers.groups.size(); ++delta)
        {
            const int up = preferredGroupIdx + delta;
            if (up >= 0 && up < (int) layers.groups.size())
            {
                const auto& upPool = layers.groups[(size_t) up].sounds;
                if (!upPool.empty())
                {
                    resolvedGroupIdx = up;
                    pool = &upPool;
                    break;
                }
            }

            const int down = preferredGroupIdx - delta;
            if (down >= 0 && down < (int) layers.groups.size())
            {
                const auto& downPool = layers.groups[(size_t) down].sounds;
                if (!downPool.empty())
                {
                    resolvedGroupIdx = down;
                    pool = &downPool;
                    break;
                }
            }
        }
    }

    if (pool->empty())
        return nullptr;

    int choice = rng.nextInt((int) pool->size());

    auto& noteLast = lastVariationIndexByNote[midiNote];
    if ((int) noteLast.size() < (int) layers.groups.size())
        noteLast.resize(layers.groups.size(), -1);

    int& lastChoice = noteLast[(size_t) resolvedGroupIdx];
    if (pool->size() > 1 && choice == lastChoice && rng.nextDouble() < 0.65)
    {
        int reroll = rng.nextInt((int) pool->size() - 1);
        if (reroll >= lastChoice)
            ++reroll;
        choice = reroll;
    }

    lastChoice = choice;
    return (*pool)[(size_t) choice];
}

void PercussionSynthesiser::noteOn(int midiChannel, int midiNoteNumber, float velocity)
{
    const juce::ScopedLock sl(lock);

    PercussionSound* selected = nullptr;

    const auto it = noteLayers.find(midiNoteNumber);
    if (it != noteLayers.end())
    {
        const int midiVelocity = velocityToMidi(velocity);
        const int groupIdx = chooseGroupForVelocity(it->second, midiVelocity);
        selected = chooseVariation(midiNoteNumber, it->second, groupIdx);
    }

    if (selected != nullptr)
    {
        for (auto* voice : voices)
            if (voice->getCurrentlyPlayingNote() == midiNoteNumber && voice->isPlayingChannel(midiChannel))
                stopVoice(voice, 1.0f, true);

        if (auto* voice = findFreeVoice(selected, midiChannel, midiNoteNumber, isNoteStealingEnabled()))
            startVoice(voice, selected, midiChannel, midiNoteNumber, velocity);
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
                startVoice(voice, sound, midiChannel, midiNoteNumber, velocity);
        }
    }
}
