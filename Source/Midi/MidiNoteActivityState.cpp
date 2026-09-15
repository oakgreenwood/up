#include "MidiNoteActivityState.h"

static_assert(std::atomic<float>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);

MidiNoteActivityState::MidiNoteActivityState()
{
    for (auto& generation : generations)
        generation.store(0, std::memory_order_relaxed);

    reset();
}

void MidiNoteActivityState::reset() noexcept
{
    for (auto& activeNoteCount : activeNoteCounts)
        activeNoteCount = 0;

    for (auto& velocity : velocities)
        velocity.store(0.0f, std::memory_order_relaxed);

    for (auto& generation : generations)
        generation.fetch_add(1, std::memory_order_relaxed);
}

void MidiNoteActivityState::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        reset();
        return;
    }

    if (message.isNoteOn())
    {
        const int midiNote = message.getNoteNumber();
        if (midiNote < 0 || midiNote >= midiNoteCount)
            return;

        ++activeNoteCounts[(size_t) midiNote];
        velocities[(size_t) midiNote].store(message.getFloatVelocity(), std::memory_order_relaxed);
        generations[(size_t) midiNote].fetch_add(1, std::memory_order_relaxed);
        latestNoteOnMidiNote.store(midiNote, std::memory_order_relaxed);
        latestNoteOnGeneration.fetch_add(1, std::memory_order_release);
        return;
    }

    if (message.isNoteOff())
    {
        const int midiNote = message.getNoteNumber();
        if (midiNote < 0 || midiNote >= midiNoteCount)
            return;

        auto& activeNoteCount = activeNoteCounts[(size_t) midiNote];
        if (activeNoteCount > 0)
            --activeNoteCount;

        if (activeNoteCount <= 0)
        {
            activeNoteCount = 0;
            velocities[(size_t) midiNote].store(0.0f, std::memory_order_relaxed);
            generations[(size_t) midiNote].fetch_add(1, std::memory_order_relaxed);
        }
    }
}

float MidiNoteActivityState::getVelocityForMidiNote(int midiNote) const noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount)
        return 0.0f;

    return velocities[(size_t) midiNote].load(std::memory_order_relaxed);
}

uint32_t MidiNoteActivityState::getGenerationForMidiNote(int midiNote) const noexcept
{
    if (midiNote < 0 || midiNote >= midiNoteCount)
        return 0;

    return generations[(size_t) midiNote].load(std::memory_order_relaxed);
}

int MidiNoteActivityState::getLatestNoteOnMidiNote() const noexcept
{
    return latestNoteOnMidiNote.load(std::memory_order_relaxed);
}

uint32_t MidiNoteActivityState::getLatestNoteOnGeneration() const noexcept
{
    return latestNoteOnGeneration.load(std::memory_order_acquire);
}
