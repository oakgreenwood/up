#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "PercussionSound.h"

class PercussionSynthesiser : public juce::Synthesiser
{
public:
    // Audio-thread range rendering lets the processor bound its sidechain
    // storage without copying MIDI or replaying future events at chunk edges.
    void renderAudioRange(juce::AudioBuffer<float>& output, int start, int count);
    void dispatchMidi(const juce::MidiMessage& message);

    void clearLayerMappings();

    void registerLayeredSound(PercussionSound* sound,
                              int midiNote,
                              int velocityGroupIndex,
                              int variationIndex);

    void finalizeLayerMappings();

    void noteOn(int midiChannel,
                int midiNoteNumber,
                float velocity) override;

private:
    struct RegisteredSound
    {
        PercussionSound* sound = nullptr;
        int velocityGroupIndex = 1;
        int variationIndex = 1;
    };

    struct VelocityGroup
    {
        int index = 1;
        int minVelocity = 1;
        int maxVelocity = 127;
        std::vector<PercussionSound*> sounds;
    };

    struct NoteLayers
    {
        std::vector<VelocityGroup> groups;
    };

    static std::pair<int, int> computeVelocityRange(int groupIndex, int groupCount) noexcept;
    static int velocityToMidi(float velocity) noexcept;
    static int chooseGroupForVelocity(const NoteLayers& layers, int midiVelocity) noexcept;
    PercussionSound* chooseVariation(int midiNote,
                                      const NoteLayers& layers,
                                      int preferredGroupIdx) noexcept;

    std::unordered_map<int, std::vector<RegisteredSound>> registeredByMidiNote;
    std::unordered_map<int, NoteLayers> noteLayers;
    std::unordered_map<int, std::vector<int>> lastVariationIndexByNote;
    juce::Random rng;
};
