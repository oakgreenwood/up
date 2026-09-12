#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include <juce_core/juce_core.h>
#include "../PercussionSound.h"

class PercussionSynthesiser;
class SampleSpecificRealtimeCache;

// One worker prepares/reclaims warp buffers. Audio only records plays, publishes
// scalar requests, and pins fixed slots; it never dispatches jobs or frees audio.
class WarpCachePrewarmer final : private juce::Thread
{
    struct Slot;
    struct Entry;

public:
    static constexpr int recentSampleCount = 5;

    class Lease final
    {
    public:
        Lease() = default;
        ~Lease() { reset(); }
        Lease(Lease&&) noexcept;
        Lease& operator=(Lease&&) noexcept;
        void reset() noexcept;
        const PercussionSound::WarpedCache* get() const noexcept { return cache; }
        const PercussionSound::WarpedCache* operator->() const noexcept { return cache; }
        explicit operator bool() const noexcept { return cache != nullptr; }
        bool operator==(std::nullptr_t) const noexcept { return cache == nullptr; }
        bool operator!=(std::nullptr_t) const noexcept { return cache != nullptr; }

    private:
        friend class WarpCachePrewarmer;
        Slot* slot = nullptr;
        const PercussionSound::WarpedCache* cache = nullptr;
        JUCE_DECLARE_NON_COPYABLE(Lease)
    };

    WarpCachePrewarmer(PercussionSynthesiser&, const SampleSpecificRealtimeCache&,
                      const std::atomic<bool>& enabled, const std::atomic<double>& bpm);
    ~WarpCachePrewarmer() override;

    // Audio thread only. Returns true on a tempo-sync disable transition.
    bool update(bool warpEnabled, bool hostTransportRunning, bool hostBpmAvailable) noexcept;
    // After state restore, rebuild the inventory once fresh host timing arrives.
    void requestStartupPreparation() noexcept;
    void recordPlayed(const PercussionSound&) noexcept;
    Lease acquire(const PercussionSound&, double bpm, double pitchRatio,
                  bool requestIfMissing) noexcept;

private:
    struct Slot
    {
        std::atomic<int> readers { 0 }; // -1 reserves the slot for the worker.
        uint64_t key = 0;
        std::unique_ptr<PercussionSound::WarpedCache> cache;
    };
    struct Entry
    {
        Entry(const PercussionSound* source, int voiceCount)
            : sound(source), slots((size_t) voiceCount + 4) {}
        const PercussionSound* sound;
        std::vector<Slot> slots; // Fixed at construction; never resized.
        std::atomic<int> baseSlot { -1 }, pitchedSlot { -1 };
        std::atomic<uint64_t> lastPlayed { 0 }, requestedKey { 0 };
        uint64_t observedPitchKey = 0, observedRequestKey = 0;
        double pitchChangedAtMs = 0.0, requestChangedAtMs = 0.0;
        uint64_t failedBaseKey = 0, failedPitchedKey = 0;
    };

    static uint64_t makeKey(double bpm, double pitchRatio) noexcept;
    static double keyBpm(uint64_t) noexcept;
    static double keyPitch(uint64_t) noexcept;
    static bool neutralKey(uint64_t) noexcept;
    Entry* findEntry(const PercussionSound&) const noexcept;
    static bool hasCache(const Entry&, uint64_t) noexcept; // Worker only.
    static void reclaim(Entry&);
    static void unpublish(Entry&) noexcept;
    enum class RenderPurpose { recent, demand, startup, base };
    bool render(Entry&, uint64_t key, RenderPurpose);
    void run() override;

    const SampleSpecificRealtimeCache& parameters;
    const std::atomic<bool>& enabled;
    const std::atomic<double>& hostBpm;
    std::atomic<bool> transportRunning { false };
    std::atomic<bool> startupTempoReady { false };
    std::atomic<uint64_t> startupGeneration { 1 };
    std::vector<std::unique_ptr<Entry>> entries; // Immutable inventory.
    std::atomic<uint64_t> playCounter { 0 };
    bool lastWarpEnabled = true; // Audio only.
};
