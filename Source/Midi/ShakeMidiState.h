#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>

// Single message-thread producer, single audio-thread consumer. No buffers or
// ownership cross this boundary. Overflow drops new gestures, never note-offs.
class ShakeMidiState
{
public:
    static constexpr int maxTriggersPerBlock = 32;
    static constexpr int maxEvents = 128 + 3 * maxTriggersPerBlock;
    static_assert(maxEvents * (3 + sizeof(int32_t) + sizeof(uint16_t)) <= 2048);
    struct Event { juce::MidiMessage message; int position = 0; bool resetSequence = false; };

    // Message thread only. A new session invalidates commands for the old target.
    void setTarget(int midiNote) noexcept
    {
        if (midiNote < 0 || midiNote > 127)
            midiNote = -1;
        const auto previous = target.load(std::memory_order_relaxed);
        const uint64_t generation = ((previous >> 8) + 1) & 0xffffffffffffffULL;
        target.store((generation << 8) | static_cast<uint64_t>(midiNote + 1), std::memory_order_release);
    }

    bool trigger(int velocity, bool down = false) noexcept
    {
        const auto session = target.load(std::memory_order_acquire);
        if ((session & 0xff) == 0)
            return false;
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = (write + 1) % queueSize;
        if (next == readIndex.load(std::memory_order_acquire))
            return false;
        queue[write] = { session, juce::jlimit(1, 127, velocity), getResetGeneration(),
                         juce::Time::getMillisecondCounterHiRes(), down };
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    void resetForPlayback() noexcept
    {
        resetGeneration.fetch_add(1, std::memory_order_relaxed);
        // The producer may still be running. Invalidate its previous commands
        // with a generation rather than mutating its queue index or target.
        for (auto& frames : noteOffFrames)
            if (frames > 0)
                frames = 1;
        discardQueued();
    }

    uint32_t getResetGeneration() const noexcept { return resetGeneration.load(std::memory_order_relaxed); }

    void prepare(double rate) noexcept
    {
        const double safeRate = std::isfinite(rate) && rate >= 1000.0 && rate <= 768000.0 ? rate : 44100.0;
        sampleRate = safeRate;
        gateFrames = juce::jmax(1, static_cast<int>(std::round(safeRate * 0.03)));
        resetForPlayback();
    }

    void beginBlock(int frames, bool allowTriggers, double nowMs) noexcept
    {
        eventCount = 0;
        if (frames <= 0)
            return;

        for (int note = 0; note < 128; ++note)
        {
            auto& remaining = noteOffFrames[static_cast<size_t>(note)];
            if (remaining <= 0)
                continue;
            if (!allowTriggers || remaining <= frames)
            {
                append(juce::MidiMessage::noteOff(1, note), allowTriggers ? remaining - 1 : 0);
                remaining = 0;
            }
            else
                remaining -= frames;
        }

        const auto end = writeIndex.load(std::memory_order_acquire);
        auto read = readIndex.load(std::memory_order_relaxed);
        int consumed = 0;
        double firstTriggerMs = -1.0;
        while (read != end && consumed++ < maxTriggersPerBlock)
        {
            const auto command = queue[read];
            read = (read + 1) % queueSize;
            const auto session = target.load(std::memory_order_acquire);
            if (!allowTriggers || command.session != session
                || command.reset != getResetGeneration() || (session & 0xff) == 0
                || nowMs - command.timeMs > 250.0)
                continue;
            const int note = static_cast<int>(session & 0xff) - 1 + (command.down ? 1 : 0);
            if (note > 127)
                continue;
            if (firstTriggerMs < 0.0)
                firstTriggerMs = command.timeMs;
            // Start the first queued hit immediately and preserve relative
            // stroke timing when several moves arrived between callbacks.
            const int position = static_cast<int>(std::round(juce::jlimit(0.0,
                static_cast<double>(frames - 1), (command.timeMs - firstTriggerMs) * sampleRate * 0.001)));
            auto& remaining = noteOffFrames[static_cast<size_t>(note)];
            // Close an overlapping generated note before retriggering it.
            // A previously scheduled later note-off for this note is removed.
            for (int i = 0; i < eventCount;)
                if (events[static_cast<size_t>(i)].message.isNoteOff()
                    && events[static_cast<size_t>(i)].position > position
                    && events[static_cast<size_t>(i)].message.getNoteNumber() == note)
                {
                    for (int j = i + 1; j < eventCount; ++j)
                        events[static_cast<size_t>(j - 1)] = events[static_cast<size_t>(j)];
                    --eventCount;
                    remaining = 1;
                }
                else
                    ++i;
            if (remaining > 0)
                append(juce::MidiMessage::noteOff(1, note), position);
            append(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(command.velocity)),
                   position, session != lastAcceptedSession);
            lastAcceptedSession = session;
            if (gateFrames <= frames - position)
            {
                append(juce::MidiMessage::noteOff(1, note), position + gateFrames - 1);
                remaining = 0;
            }
            else
                remaining = gateFrames - (frames - position);
        }
        readIndex.store(read, std::memory_order_release);
    }

    int size() const noexcept { return eventCount; }
    const Event& get(int index) const noexcept { return events[static_cast<size_t>(index)]; }

    // For the pinned JUCE VST3/AU wrappers: both reserve >=2048 MIDI bytes
    // before rendering. <=224 three-byte events need <=2016 bytes. No swap or
    // resize is performed, so that capacity cannot be lost between callbacks.
    void writeOutput(juce::MidiBuffer& output, bool supportedWrapper) const
    {
        output.clear();
        if (supportedWrapper)
            for (int i = 0; i < eventCount; ++i)
                output.addEvent(events[static_cast<size_t>(i)].message, events[static_cast<size_t>(i)].position);
    }

private:
    struct Command
    {
        uint64_t session = 0;
        int velocity = 1;
        uint32_t reset = 0;
        double timeMs = 0.0;
        bool down = false;
    };
    static constexpr unsigned queueSize = 128;
    void discardQueued() noexcept
    { readIndex.store(writeIndex.load(std::memory_order_acquire), std::memory_order_release); }

    void append(juce::MidiMessage message, int position, bool resetSequence = false) noexcept
    {
        jassert(eventCount < maxEvents);
        int index = eventCount++;
        while (index > 0 && events[static_cast<size_t>(index - 1)].position > position)
        {
            events[static_cast<size_t>(index)] = events[static_cast<size_t>(index - 1)];
            --index;
        }
        events[static_cast<size_t>(index)] = { message, position, resetSequence };
    }

    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    std::array<Command, queueSize> queue {};
    std::atomic<unsigned> readIndex { 0 }, writeIndex { 0 };
    std::atomic<uint64_t> target { 0 };
    uint64_t lastAcceptedSession = 0;
    std::atomic<uint32_t> resetGeneration { 0 };
    std::array<int, 128> noteOffFrames {};
    std::array<Event, maxEvents> events;
    int gateFrames = 1323;
    double sampleRate = 44100.0;
    int eventCount = 0;
};
