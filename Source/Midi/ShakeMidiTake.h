#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Audio appends immutable note starts and publishes a readable prefix. Only
// note ends can change in that prefix, through lock-free atomics. Message pins
// a slot while copying; all MIDI-file creation and file I/O stay on message.
class ShakeMidiTake
{
public:
    static constexpr int maxNotes = 32768;
    static constexpr double maxTakeBeats = 65536.0;

    struct Note
    {
        double start = 0.0, end = 0.0;
        int number = 0, velocity = 1;
        int channel = 1;
    };

    struct Snapshot
    {
        std::vector<Note> notes;
        double length = 0.0;
        bool truncated = false;
        double arrangementStart = -1.0;
    };

    struct Status { uint64_t generation; bool available; };

    bool beginBlock(bool positionAvailable, bool playing, bool recording, int frames,
                    double rate, double bpm, double nowMs,
                    bool arrangementPositionAvailable, double arrangementPosition) noexcept
    {
        blockFrames = juce::jmax(0, frames);
        firstBlockFrame = 0;
        lastHitInBlock = -1;
        blockTimeMs = nowMs;
        const double safeRate = std::isfinite(rate) && rate >= 1000.0 && rate <= 768000.0 ? rate : 44100.0;
        const double safeBpm = std::isfinite(bpm) && bpm >= 1.0 ? juce::jmin(1000.0, bpm) : 153.0;
        secondsPerFrame = 1.0 / safeRate;
        beatsPerFrame = safeBpm / (60.0 * safeRate);
        blockArrangementStart = arrangementPositionAvailable && std::isfinite(arrangementPosition)
            && arrangementPosition >= 0.0 ? arrangementPosition : -1.0;
        bool started = false;
        if (positionAvailable)
        {
            if ((playing && !wasPlaying) || (recording && !wasRecording))
            {
                startTake(0);
                started = true;
            }
            wasPlaying = playing;
            wasRecording = recording;
        }
        return started;
    }

    void capture(const juce::MidiMessage& message, int position) noexcept
    {
        position = juce::jlimit(0, blockFrames, position);
        if (message.isNoteOn())
        {
            const double idle = (lastHitInBlock >= 0 ? 0.0 : idleSeconds)
                + juce::jmax(0, position - juce::jmax(0, lastHitInBlock)) * secondsPerFrame;
            const double hitTimeMs = blockTimeMs + position * secondsPerFrame * 1000.0;
            if (writingSlot < 0 || (!wasPlaying && !wasRecording
                && (idle >= 4.0 || (lastHitMs >= 0.0 && hitTimeMs - lastHitMs >= 4000.0))))
                startTake(position);
            lastHitInBlock = position;
            lastHitMs = hitTimeMs;
        }
        if (writingSlot < 0)
            return;
        const double beat = beatAt(position);
        const int channel = message.getChannel();
        if (channel < 1 || channel > 16)
            return;
        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (int note = 0; note < 128; ++note)
                closeNote((channel - 1) * 128 + note, beat);
            return;
        }
        if (!message.isNoteOn() && !message.isNoteOff())
            return;
        const int number = message.getNoteNumber();
        if (number < 0 || number >= 128)
            return;
        auto& take = slots[static_cast<size_t>(writingSlot)];
        const int key = (channel - 1) * 128 + number;
        closeNote(key, beat);
        if (message.isNoteOn())
        {
            if (noteCount >= maxNotes || beat >= maxTakeBeats)
            {
                take.truncated.store(true, std::memory_order_relaxed);
                return;
            }
            activeNotes[static_cast<size_t>(key)] = noteCount;
            auto& note = take.notes[static_cast<size_t>(noteCount++)];
            note.start = beat;
            note.end.store(-1.0, std::memory_order_relaxed);
            note.number = number;
            note.velocity = message.getVelocity();
            note.channel = channel;
            ++activeCount;
        }
    }

    void endBlock() noexcept
    {
        idleSeconds = juce::jmin(4.0, (lastHitInBlock >= 0 ? 0.0 : idleSeconds)
            + (blockFrames - juce::jmax(0, lastHitInBlock)) * secondsPerFrame);
        if (writingSlot >= 0)
        {
            const double end = cursorBeats + (blockFrames - firstBlockFrame) * beatsPerFrame;
            if (end > maxTakeBeats)
                slots[static_cast<size_t>(writingSlot)].truncated.store(true, std::memory_order_relaxed);
            cursorBeats = juce::jmin(maxTakeBeats, end);
            publishProgress();
            // Retain the finished take through a stopped pause. The next hit,
            // rather than the timeout itself, gives the next take a new ID.
            if (!wasPlaying && !wasRecording && idleSeconds >= 4.0 && activeCount == 0)
                finish();
        }
    }

    // Audio, or stopped lifecycle callbacks. Do not clear note storage here.
    void finish() noexcept
    {
        if (writingSlot >= 0)
        {
            closeActive(cursorBeats);
            publishProgress();
            writingSlot = -1;
        }
        wasPlaying = wasRecording = false;
        idleSeconds = 4.0;
        lastHitMs = -1.0;
    }

    Status getStatus() const noexcept
    {
        const auto state = published.load(std::memory_order_acquire);
        return { state >> 3, (state & availableBit) != 0 };
    }

    bool hasTake() const noexcept { return getStatus().available; }

    // Message thread only. Allocation is intentional here, never on audio.
    std::optional<Snapshot> copyLatest() const
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const auto state = published.load(std::memory_order_acquire);
            if ((state & availableBit) == 0)
                return {};
            const int index = static_cast<int>(state & 3) - 1;
            auto& take = slots[static_cast<size_t>(index)];
            int readers = take.readers.load(std::memory_order_relaxed);
            if (readers < 0 || !take.readers.compare_exchange_strong(readers, readers + 1,
                    std::memory_order_acquire, std::memory_order_relaxed))
                continue;
            struct Pin
            {
                std::atomic<int>& readers;
                ~Pin() { readers.fetch_sub(1, std::memory_order_release); }
            } pin { take.readers };
            if (state != published.load(std::memory_order_acquire))
                continue;
            Snapshot result;
            const int count = take.count.load(std::memory_order_acquire);
            result.length = take.length.load(std::memory_order_acquire);
            result.truncated = take.truncated.load(std::memory_order_relaxed);
            result.arrangementStart = take.arrangementStart;
            result.notes.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                const auto& note = take.notes[static_cast<size_t>(i)];
                const double end = note.end.load(std::memory_order_relaxed);
                result.notes.push_back({ note.start,
                    end < 0.0 ? result.length : juce::jlimit(note.start, result.length, end),
                    note.number, note.velocity, note.channel });
            }
            if (state != published.load(std::memory_order_acquire))
                continue;
            return result;
        }
        return {};
    }

    // Message thread only. A retained temp file survives editor close and the
    // native drag callback, allowing the DAW to import it asynchronously.
    juce::File createMidiFile(double* arrangementStart = nullptr) const
    {
        if (arrangementStart != nullptr)
            *arrangementStart = -1.0;
        const auto snapshot = copyLatest();
        if (!snapshot || snapshot->notes.empty())
            return {};
        if (arrangementStart != nullptr)
            *arrangementStart = snapshot->arrangementStart;
        constexpr int ticksPerBeat = 960;
        juce::MidiMessageSequence sequence;
        double lastTick = std::round(snapshot->length * ticksPerBeat);
        for (const auto& note : snapshot->notes)
        {
            const double start = std::round(note.start * ticksPerBeat);
            const double end = juce::jmax(start + 1.0, std::round(note.end * ticksPerBeat));
            auto on = juce::MidiMessage::noteOn(note.channel, note.number, static_cast<juce::uint8>(note.velocity));
            on.setTimeStamp(start);
            sequence.addEvent(on);
            auto off = juce::MidiMessage::noteOff(note.channel, note.number);
            off.setTimeStamp(end);
            sequence.addEvent(off);
            lastTick = juce::jmax(lastTick, end);
        }
        auto end = juce::MidiMessage::endOfTrack();
        end.setTimeStamp(lastTick);
        sequence.addEvent(end);
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote(ticksPerBeat);
        midi.addTrack(sequence);
        const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("ulichpercs-midi");
        if (!directory.createDirectory().wasOk())
            return {};
        const juce::String prefix = snapshot->truncated ? "ulichpercs-shake-partial-" : "ulichpercs-shake-";
        const auto file = directory.getNonexistentChildFile(prefix + juce::Uuid().toString(), ".mid", false);
        bool success = false;
        {
            auto stream = file.createOutputStream();
            if (stream && stream->openedOk())
            {
                success = midi.writeTo(*stream, 0);
                stream->flush();
                success = success && stream->getStatus().wasOk();
            }
        }
        if (success)
            return file;
        file.deleteFile();
        return {};
    }

private:
    struct StoredNote
    {
        double start = 0.0;
        std::atomic<double> end { -1.0 };
        int number = 0, velocity = 1, channel = 1;
    };

    struct Slot
    {
        std::atomic<int> readers { 0 }; // -1 only while resetting an unpinned slot.
        std::array<StoredNote, maxNotes> notes;
        std::atomic<int> count { 0 };
        std::atomic<double> length { 0.0 };
        std::atomic<bool> truncated { false };
        // Immutable for a published generation; protected by the existing pins.
        double arrangementStart = -1.0;
    };

    double beatAt(int position) const noexcept
    {
        return juce::jmin(maxTakeBeats, cursorBeats
            + juce::jmax(0, position - firstBlockFrame) * beatsPerFrame);
    }

    void closeNote(int key, double beat) noexcept
    {
        auto& active = activeNotes[static_cast<size_t>(key)];
        if (active >= 0)
        {
            slots[static_cast<size_t>(writingSlot)].notes[static_cast<size_t>(active)]
                .end.store(beat, std::memory_order_relaxed);
            active = -1;
            --activeCount;
        }
    }

    void closeActive(double beat) noexcept
    {
        for (int key = 0; key < static_cast<int>(activeNotes.size()); ++key)
            closeNote(key, beat);
    }

    void publishProgress() noexcept
    {
        auto& take = slots[static_cast<size_t>(writingSlot)];
        take.length.store(cursorBeats, std::memory_order_release);
        take.count.store(noteCount, std::memory_order_release);
        published.store((generation << 3) | static_cast<uint64_t>(writingSlot + 1)
            | (noteCount > 0 ? availableBit : 0), std::memory_order_release);
    }

    void startTake(int position) noexcept
    {
        if (writingSlot >= 0)
            closeActive(beatAt(position));
        writingSlot = -1;
        generation = (generation + 1) & (~uint64_t(0) >> 3);
        if (generation == 0)
            ++generation;
        published.store(generation << 3, std::memory_order_release);
        cursorBeats = 0.0;
        firstBlockFrame = position;
        noteCount = activeCount = 0;
        activeNotes.fill(-1);
        for (int i = 0; i < slotCount; ++i)
        {
            int expected = 0;
            if (!slots[static_cast<size_t>(i)].readers.compare_exchange_strong(expected, -1,
                    std::memory_order_acq_rel, std::memory_order_relaxed))
                continue;
            writingSlot = i;
            auto& take = slots[static_cast<size_t>(i)];
            take.count.store(0, std::memory_order_relaxed);
            take.length.store(0.0, std::memory_order_relaxed);
            take.truncated.store(false, std::memory_order_relaxed);
            take.arrangementStart = blockArrangementStart < 0.0 ? -1.0
                : blockArrangementStart + ((wasPlaying || wasRecording) ? position * beatsPerFrame : 0.0);
            take.readers.store(0, std::memory_order_release);
            return;
        }
    }

    static constexpr int slotCount = 3;
    static constexpr uint64_t availableBit = 4;
    static_assert(std::atomic<int>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(std::atomic<double>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::unique_ptr<Slot[]> slots { std::make_unique<Slot[]>(slotCount) };
    std::atomic<uint64_t> published { 0 };
    std::array<int, 16 * 128> activeNotes {};
    uint64_t generation = 0;
    int writingSlot = -1, blockFrames = 0;
    int noteCount = 0, activeCount = 0, firstBlockFrame = 0, lastHitInBlock = -1;
    double cursorBeats = 0.0, beatsPerFrame = 0.0, secondsPerFrame = 0.0;
    double idleSeconds = 4.0, lastHitMs = -1.0, blockTimeMs = 0.0;
    double blockArrangementStart = -1.0;
    bool wasPlaying = false, wasRecording = false;
};
