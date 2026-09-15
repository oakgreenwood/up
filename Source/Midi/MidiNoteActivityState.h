#pragma once

#include <array>
#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>

class MidiNoteActivityState final
{
public:
    static constexpr int getMidiNoteCount() noexcept { return midiNoteCount; }

    MidiNoteActivityState();

    void reset() noexcept;
    void handleMidiMessage(const juce::MidiMessage& message) noexcept;
    float getVelocityForMidiNote(int midiNote) const noexcept;
    uint32_t getGenerationForMidiNote(int midiNote) const noexcept;
    int getLatestNoteOnMidiNote() const noexcept;
    uint32_t getLatestNoteOnGeneration() const noexcept;

private:
    static constexpr int midiNoteCount = 128;

    std::array<std::atomic<float>, midiNoteCount> velocities;
    std::array<std::atomic<uint32_t>, midiNoteCount> generations;
    std::array<int, midiNoteCount> activeNoteCounts {};
    std::atomic<int> latestNoteOnMidiNote { -1 };
    std::atomic<uint32_t> latestNoteOnGeneration { 0 };
};
