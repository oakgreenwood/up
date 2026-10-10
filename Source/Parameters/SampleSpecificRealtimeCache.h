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
    void setStartForMidiNote(int midiNote, float position) noexcept;
    float getStartForMidiNote(int midiNote) const noexcept;
    void setEndForMidiNote(int midiNote, float position) noexcept;
    float getEndForMidiNote(int midiNote) const noexcept;
    void setTrimReferenceDurationForMidiNote(int midiNote, float seconds) noexcept;
    float getEndTrimSecondsForMidiNote(int midiNote) const noexcept;
    void setAttackMsForMidiNote(int midiNote, float milliseconds) noexcept;
    float getAttackMsForMidiNote(int midiNote) const noexcept;
    void setReleaseMsForMidiNote(int midiNote, float milliseconds) noexcept;
    float getReleaseMsForMidiNote(int midiNote) const noexcept;
    void setLoopBpmForMidiNote(int midiNote, float bpm) noexcept;
    float getLoopBpmForMidiNote(int midiNote) const noexcept;
    void setLoopTapeModeForMidiNote(int midiNote, bool tape) noexcept;
    bool getLoopTapeModeForMidiNote(int midiNote) const noexcept;
    void setFormantSemitonesForMidiNote(int midiNote, float semitones) noexcept;
    float getFormantSemitonesForMidiNote(int midiNote) const noexcept;
    float getFormantRatioForMidiNote(int midiNote) const noexcept;

    void setEqFrequencyForMidiNote(int note, int band, float value) noexcept;
    void setEqGainForMidiNote(int note, int band, float value) noexcept;
    float getEqFrequencyForMidiNote(int note, int band) const noexcept;
    float getEqGainForMidiNote(int note, int band) const noexcept;

private:
    static constexpr int midiNoteCount = 128;

    std::array<std::array<std::atomic<float>, midiNoteCount>, 4> eqFrequency, eqGain;
    std::array<std::atomic<float>, midiNoteCount> gainDbByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> gainLinearByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> pitchRatioByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> pitchSemitonesByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> punchAmountByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> monoAmountByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> panByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> startByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> endByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> trimReferenceSecondsByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> endTrimSecondsByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> attackMsByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> releaseMsByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> loopBpmByMidiNote;
    std::array<std::atomic<bool>, midiNoteCount> loopTapeModeByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> formantSemitonesByMidiNote;
    std::array<std::atomic<float>, midiNoteCount> formantRatioByMidiNote;
};
