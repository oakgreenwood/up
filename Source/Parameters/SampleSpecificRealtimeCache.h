#pragma once

#include <array>
#include <atomic>

class SampleSpecificRealtimeCache final
{
public:
    SampleSpecificRealtimeCache();

    void reset() noexcept;
    void setGainDbForMidiNote(int midiNote, float decibels) noexcept;
    float getGainDbForMidiNote(int midiNote) const noexcept;
    float getGainLinearForMidiNote(int midiNote) const noexcept;
    void setPitchSemitonesForMidiNote(int midiNote, float semitones) noexcept;
    float getPitchRatioForMidiNote(int midiNote) const noexcept;
    float getPitchSemitonesForMidiNote(int midiNote) const noexcept;
    void setPunchAmountForMidiNote(int midiNote, float amount) noexcept;
    float getPunchAmountForMidiNote(int midiNote) const noexcept;
    void setMonoAmountForMidiNote(int midiNote, float amount) noexcept;
    float getMonoAmountForMidiNote(int midiNote) const noexcept;
    void setPanForMidiNote(int midiNote, float position) noexcept;
    float getPanForMidiNote(int midiNote) const noexcept;
    void setFormantSemitonesForMidiNote(int midiNote, float semitones) noexcept;
    float getFormantSemitonesForMidiNote(int midiNote) const noexcept;
    float getFormantRatioForMidiNote(int midiNote) const noexcept;

private:
    static constexpr int midiNoteCount = 128;

    std::array<std::atomic<float>, midiNoteCount> gainDbByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> gainLinearByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> pitchRatioByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> pitchSemitonesByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> punchAmountByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> monoAmountByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> panByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> formantSemitonesByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> formantRatioByMidiNote;
};
