#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "PercussionSound.h"
#include "SampleLibrary/SampleNameParser.h"

class PercussionSynthesiser : public juce::Synthesiser
{
public:
    // Audio-thread range rendering lets the processor bound its sidechain
    // storage without copying MIDI or replaying future events at chunk edges.
    void renderAudioRange(juce::AudioBuffer<float>& output, int start, int count);
    void dispatchMidi(const juce::MidiMessage& message);

    void clearLayerMappings();
    // Audio-thread state; lifecycle callers must have rendering stopped.
    void resetPlaybackSequence() noexcept;

    void registerLayeredSound(PercussionSound* sound,
                              int midiNote,
                              int velocityGroupIndex,
                              int variationIndex,
                              SampleDirection direction = SampleDirection::none);

    void finalizeLayerMappings();
    void registerDirectionalNotePair(int root) noexcept
    {
        if (root >= 0 && root < 127)
            downNoteRoots[static_cast<size_t>(root + 1)] = root;
    }
    int getGroupRootNote(int note) const noexcept
    {
        return note >= 0 && note < 128 && downNoteRoots[static_cast<size_t>(note)] >= 0
            ? downNoteRoots[static_cast<size_t>(note)] : note;
    }

    void noteOn(int midiChannel,
                int midiNoteNumber,
                float velocity) override;

private:
    juce::SynthesiserVoice* findVoiceToSteal(juce::SynthesiserSound* soundToPlay,
                                            int midiChannel, int midiNoteNumber) const override;

    struct RegisteredSound
    {
        PercussionSound* sound = nullptr;
        int velocityGroupIndex = 1;
        int variationIndex = 1;
        SampleDirection direction = SampleDirection::none;
    };

    struct VariationPool
    {
        std::vector<PercussionSound*> sounds;
        int lastChoice = -1;
    };

    struct VelocityGroup
    {
        int index = 1;
        int minVelocity = 1;
        int maxVelocity = 127;
        std::array<VariationPool, 3> pools;
    };

    struct NoteLayers
    {
        std::vector<VelocityGroup> groups;
        bool hasDirectionalSamples = false;
    };

    static std::pair<int, int> computeVelocityRange(int groupIndex, int groupCount) noexcept;
    static int velocityToMidi(float velocity) noexcept;
    static int chooseGroupForVelocity(const NoteLayers& layers, int midiVelocity) noexcept;
    static VariationPool* findNearestPool(NoteLayers& layers,
                                         int preferredGroupIdx,
                                         SampleDirection direction) noexcept;
    PercussionSound* chooseVariation(NoteLayers& layers,
                                    int preferredGroupIdx,
                                    SampleDirection direction) noexcept;
    void rememberPlayedGroup(int midiNote, SampleDirection direction, double timeMs) noexcept;

    std::unordered_map<int, std::vector<RegisteredSound>> registeredByMidiNote;
    std::unordered_map<int, NoteLayers> noteLayers;
    int lastPlayedMidiNote = -1;
    SampleDirection nextDirection = SampleDirection::up;
    uint64_t idleFrames = 0;
    uint64_t sequenceTimeoutFrames = 176400; // Four seconds at 44.1 kHz, updated on reset.
    double lastPlayedTimeMs = 0.0;
    juce::Random rng;
    std::array<int, 128> downNoteRoots = [] { std::array<int, 128> notes {}; notes.fill(-1); return notes; }();
};
