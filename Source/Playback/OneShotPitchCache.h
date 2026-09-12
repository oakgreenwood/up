#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

class PercussionSound;
class PercussionSynthesiser;
class SampleSpecificRealtimeCache;

// A coordinator publishes/reclaims groups; six workers render independent
// variations. Audio only records plays and pins/unpins fixed slots.
class OneShotPitchCache final : private juce::Thread
{
    struct Slot;
    struct RenderBatch;

public:
    static constexpr int maximumVoices = 8;
    static constexpr int renderWorkerCount = 6;
    static constexpr int recentGroupCount = 5;

    class Lease final
    {
    public:
        Lease() = default;
        ~Lease() { reset(); }
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        void reset() noexcept;
        const juce::AudioBuffer<float>* getBuffer() const noexcept { return buffer; }
        double getSampleRate() const noexcept { return sampleRate; }

    private:
        friend class OneShotPitchCache;
        Slot* slot = nullptr;
        const juce::AudioBuffer<float>* buffer = nullptr;
        double sampleRate = 0.0;
        JUCE_DECLARE_NON_COPYABLE(Lease)
    };

    enum class Status { ready, preparing, failed };

    OneShotPitchCache(PercussionSynthesiser&, const SampleSpecificRealtimeCache&);
    ~OneShotPitchCache() override;
    // Single audio/render thread. The lease must not outlive this cache.
    Lease acquire(const PercussionSound&) noexcept;
    void recordPlayed(const PercussionSound&) noexcept;
    bool supportsMidiNote(int midiNote) const noexcept;
    Status getStatus(int midiNote) const noexcept;
    void setPlaybackSampleRate(double sampleRate) noexcept;

private:
    static constexpr int noteCount = 128;
    static constexpr int slotCount = maximumVoices + 2;
    static constexpr int writingSlot = -1;

    struct Snapshot
    {
        double sampleRate = 0.0;
        std::vector<juce::AudioBuffer<float>> buffers;
    };
    struct Slot
    {
        // -1: worker has exclusive access; otherwise number of audio leases.
        std::atomic<int> readers { 0 };
        std::unique_ptr<Snapshot> snapshot;
    };
    struct Group
    {
        std::vector<const PercussionSound*> sounds; // Immutable after construction.
        std::array<Slot, slotCount> slots;
        std::atomic<int> publishedSlot { -1 };
        std::atomic<float> readyPitch { 1.0f };
        std::atomic<float> failedPitch { 0.0f };
        std::atomic<double> readySampleRate { 0.0 };
        std::atomic<double> failedSampleRate { 0.0 };
        std::atomic<uint64_t> lastPlayed { 0 };
        float observedPitch = 1.0f; // Coordinator only.
        double observedSampleRate = 0.0;
        bool observedPreserveLength = false;
        double changedAtMs = 0.0;
    };

    void run() override;
    void refreshRequests();
    bool needsRender(int midiNote) const noexcept;
    std::array<int, recentGroupCount> recentGroups() const noexcept;
    int nextGroupToRender() const noexcept;
    bool shouldYieldToRecent(int midiNote) const noexcept;
    bool requestStillCurrent(int midiNote, float pitch, double sampleRate) const noexcept;
    std::unique_ptr<Snapshot> renderGroup(int midiNote, float pitch, double sampleRate,
                                          bool& deferred);
    bool renderSound(const PercussionSound&, float pitch, juce::AudioBuffer<float>&,
                     int midiNote, double outputSampleRate, const std::atomic<bool>& cancelled);
    static void reclaimRetiredSlots(Group&);

    const SampleSpecificRealtimeCache& parameters;
    std::atomic<double> playbackSampleRate { 0.0 };
    std::atomic<uint64_t> playCounter { 0 };
    std::array<Group, noteCount> groups;
    juce::ThreadPool renderWorkers { renderWorkerCount };
};
