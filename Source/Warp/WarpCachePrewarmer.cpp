#include "WarpCachePrewarmer.h"
#include "../PercussionSynthesiser.h"
#include "../Parameters/SampleSpecificRealtimeCache.h"

#include <cmath>
#include <limits>
#include <utility>

static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);

namespace
{
    constexpr int pitchOffset = 4800; // Hundredths of a semitone, +/- 48 semitones.
    constexpr double pitchDebounceMs = 80.0;
    constexpr double bpmDebounceMs = 120.0;
}

WarpCachePrewarmer::Lease::Lease(Lease&& other) noexcept
    : slot(std::exchange(other.slot, nullptr)), cache(std::exchange(other.cache, nullptr)) {}

WarpCachePrewarmer::Lease& WarpCachePrewarmer::Lease::operator=(Lease&& other) noexcept
{
    if (this != &other)
    {
        reset();
        slot = std::exchange(other.slot, nullptr);
        cache = std::exchange(other.cache, nullptr);
    }
    return *this;
}

void WarpCachePrewarmer::Lease::reset() noexcept
{
    cache = nullptr;
    if (auto* previous = std::exchange(slot, nullptr))
        previous->readers.fetch_sub(1, std::memory_order_release);
}

WarpCachePrewarmer::WarpCachePrewarmer(PercussionSynthesiser& sampler,
                                      const SampleSpecificRealtimeCache& values,
                                      const std::atomic<bool>& warpEnabled,
                                      const std::atomic<double>& bpm)
    : juce::Thread("Warp cache preparation"), parameters(values),
      enabled(warpEnabled), hostBpm(bpm)
{
    for (int i = 0; i < sampler.getNumSounds(); ++i)
    {
        auto* sound = dynamic_cast<PercussionSound*>(sampler.getSound(i).get());
        if (sound == nullptr || !sound->isWarpEnabled())
            continue;
        sound->setWarpCacheIndex((int) entries.size());
        entries.push_back(std::make_unique<Entry>(sound, sampler.getNumVoices()));
    }
    startThread();
}

WarpCachePrewarmer::~WarpCachePrewarmer()
{
    // Processor stops/clears voices before joining us, then frees source sounds.
    stopThread(-1);
}

bool WarpCachePrewarmer::update(bool warpEnabled, bool running, bool hostBpmAvailable) noexcept
{
    transportRunning.store(running, std::memory_order_relaxed);
    if (hostBpmAvailable)
        startupTempoReady.store(true, std::memory_order_release);
    const bool disabled = lastWarpEnabled && !warpEnabled;
    lastWarpEnabled = warpEnabled;
    return disabled;
}

void WarpCachePrewarmer::requestStartupPreparation() noexcept
{
    startupTempoReady.store(false, std::memory_order_relaxed);
    startupGeneration.fetch_add(1, std::memory_order_release);
}

WarpCachePrewarmer::Entry* WarpCachePrewarmer::findEntry(const PercussionSound& sound) const noexcept
{
    const int index = sound.getWarpCacheIndex();
    if (index < 0 || (size_t) index >= entries.size())
        return nullptr;
    auto* entry = entries[(size_t) index].get();
    return entry->sound == &sound ? entry : nullptr;
}

void WarpCachePrewarmer::recordPlayed(const PercussionSound& sound) noexcept
{
    if (auto* entry = findEntry(sound))
        entry->lastPlayed.store(playCounter.fetch_add(1, std::memory_order_relaxed) + 1,
                                std::memory_order_release);
}

uint64_t WarpCachePrewarmer::makeKey(double bpm, double pitchRatio) noexcept
{
    if (!std::isfinite(bpm) || bpm < 1.0 || !std::isfinite(pitchRatio) || pitchRatio <= 0.0)
        return 0;
    const double bpmUnits = std::round(bpm * 100.0);
    const double pitchUnits = std::round(1200.0 * std::log2(pitchRatio));
    if (!std::isfinite(bpmUnits) || bpmUnits > std::numeric_limits<uint32_t>::max()
        || pitchUnits < -pitchOffset || pitchUnits > pitchOffset)
        return 0;
    return ((uint64_t) bpmUnits << 16) | (uint64_t) (pitchUnits + pitchOffset);
}

double WarpCachePrewarmer::keyBpm(uint64_t key) noexcept { return (double) (key >> 16) * 0.01; }
double WarpCachePrewarmer::keyPitch(uint64_t key) noexcept
{
    return std::exp2(((double) (key & 0xffff) - pitchOffset) / 1200.0);
}
bool WarpCachePrewarmer::neutralKey(uint64_t key) noexcept { return (key & 0xffff) == pitchOffset; }

WarpCachePrewarmer::Lease WarpCachePrewarmer::acquire(const PercussionSound& sound,
                                                     double bpm, double pitchRatio,
                                                     bool requestIfMissing) noexcept
{
    Lease result;
    auto* entry = findEntry(sound);
    const auto key = makeKey(bpm, pitchRatio);
    if (entry == nullptr || key == 0)
        return result;
    auto& published = neutralKey(key) ? entry->baseSlot : entry->pitchedSlot;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const int index = published.load(std::memory_order_acquire);
        if (index < 0)
            break;
        auto& slot = entry->slots[(size_t) index];
        int readers = slot.readers.load(std::memory_order_relaxed);
        if (readers < 0 || !slot.readers.compare_exchange_strong(readers, readers + 1,
                                                                std::memory_order_acquire,
                                                                std::memory_order_relaxed))
            continue;
        if (slot.key == key && slot.cache != nullptr)
        {
            result.slot = &slot;
            result.cache = slot.cache.get();
            return result;
        }
        slot.readers.fetch_sub(1, std::memory_order_release);
        break;
    }
    // One packed key prevents torn BPM/pitch requests. Last request wins.
    if (requestIfMissing)
        entry->requestedKey.store(key, std::memory_order_release);
    return result;
}

bool WarpCachePrewarmer::hasCache(const Entry& entry, uint64_t key) noexcept
{
    const int index = (neutralKey(key) ? entry.baseSlot : entry.pitchedSlot).load(std::memory_order_relaxed);
    return index >= 0 && entry.slots[(size_t) index].key == key;
}

void WarpCachePrewarmer::reclaim(Entry& entry)
{
    const int base = entry.baseSlot.load(std::memory_order_relaxed);
    const int pitched = entry.pitchedSlot.load(std::memory_order_relaxed);
    for (int i = 0; i < (int) entry.slots.size(); ++i)
    {
        if (i == base || i == pitched)
            continue;
        auto& slot = entry.slots[(size_t) i];
        int expected = 0;
        if (slot.readers.compare_exchange_strong(expected, -1, std::memory_order_acquire))
        {
            slot.cache.reset();
            slot.key = 0;
            slot.readers.store(0, std::memory_order_release);
        }
    }
}

void WarpCachePrewarmer::unpublish(Entry& entry) noexcept
{
    entry.baseSlot.store(-1, std::memory_order_release);
    entry.pitchedSlot.store(-1, std::memory_order_release);
    entry.requestedKey.store(0, std::memory_order_release);
    entry.observedRequestKey = 0;
    entry.failedBaseKey = entry.failedPitchedKey = 0;
}

bool WarpCachePrewarmer::render(Entry& entry, uint64_t key, RenderPurpose purpose)
{
    if (key == 0 || hasCache(entry, key))
        return false;
    auto& failed = neutralKey(key) ? entry.failedBaseKey : entry.failedPitchedKey;
    if (failed == key)
        return false;

    const auto generation = startupGeneration.load(std::memory_order_acquire);
    const auto stillWanted = [this, &entry, key, purpose, generation]
    {
        if (threadShouldExit() || !enabled.load(std::memory_order_relaxed)
            || startupGeneration.load(std::memory_order_acquire) != generation
            || makeKey(hostBpm.load(std::memory_order_relaxed), 1.0) != makeKey(keyBpm(key), 1.0))
            return false;
        if (purpose == RenderPurpose::startup
            && !startupTempoReady.load(std::memory_order_acquire))
            return false;
        if (purpose == RenderPurpose::base || purpose == RenderPurpose::startup)
        {
            if (purpose == RenderPurpose::base && !transportRunning.load(std::memory_order_relaxed))
                return false;
            // Yield inventory prewarming when a played sound changes pitch or
            // an active cache miss publishes a more urgent exact-key request.
            for (const auto& owned : entries)
            {
                if (owned.get() == &entry)
                    continue; // A hit asking for this same render need not cancel it.
                const auto request = owned->requestedKey.load(std::memory_order_acquire);
                if (request != 0 && makeKey(keyBpm(request), 1.0) == makeKey(keyBpm(key), 1.0)
                    && !hasCache(*owned, request)
                    && (neutralKey(request) ? owned->failedBaseKey : owned->failedPitchedKey) != request)
                    return false;
                if (owned->lastPlayed.load(std::memory_order_acquire) != 0
                    && makeKey(keyBpm(key), parameters.getPitchRatioForMidiNote(
                           owned->sound->getMidiRootNote())) != owned->observedPitchKey)
                    return false;
            }
            if (purpose == RenderPurpose::base)
                return true;
        }
        if (purpose == RenderPurpose::demand)
            return entry.requestedKey.load(std::memory_order_acquire) == key;
        return makeKey(keyBpm(key), parameters.getPitchRatioForMidiNote(entry.sound->getMidiRootNote())) == key;
    };
    if (!stillWanted())
        return false;
    std::unique_ptr<PercussionSound::WarpedCache> prepared;
    try
    {
        prepared = entry.sound->renderWarpedCache(keyBpm(key), keyPitch(key),
                                                  [&stillWanted] { return !stillWanted(); });
    }
    catch (...) { /* Keep the existing publication; never throw out of the worker. */ }
    if (!stillWanted())
        return true;
    if (prepared == nullptr)
    {
        failed = key;
        return true;
    }

    const int base = entry.baseSlot.load(std::memory_order_relaxed);
    const int pitched = entry.pitchedSlot.load(std::memory_order_relaxed);
    for (int i = 0; i < (int) entry.slots.size(); ++i)
    {
        if (i == base || i == pitched)
            continue;
        auto& slot = entry.slots[(size_t) i];
        int expected = 0;
        if (!slot.readers.compare_exchange_strong(expected, -1, std::memory_order_acquire))
            continue;
        slot.cache = std::move(prepared);
        slot.key = key;
        slot.readers.store(0, std::memory_order_release);
        (neutralKey(key) ? entry.baseSlot : entry.pitchedSlot).store(i, std::memory_order_release);
        failed = 0;
        auto expectedKey = key;
        entry.requestedKey.compare_exchange_strong(expectedKey, 0, std::memory_order_relaxed);
        return true;
    }
    // No audio cleanup fallback. Retry on the worker if publication raced a pin.
    return true;
}

void WarpCachePrewarmer::run()
{
    uint64_t observedBpm = 0;
    uint64_t observedStartupGeneration = 0;
    bool startupPending = true;
    double bpmChangedAtMs = 0.0;
    bool wasEnabled = true;
    bool wasTempoReady = false;
    while (!threadShouldExit())
    {
        const auto generation = startupGeneration.load(std::memory_order_acquire);
        const bool startupRequested = generation != observedStartupGeneration;
        const bool tempoReady = startupTempoReady.load(std::memory_order_acquire);
        const bool isEnabled = enabled.load(std::memory_order_relaxed);
        const auto bpmKey = makeKey(hostBpm.load(std::memory_order_relaxed), 1.0);
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (startupRequested)
        {
            observedStartupGeneration = generation;
            startupPending = true;
        }
        if (bpmKey != observedBpm || isEnabled != wasEnabled || startupRequested
            || (tempoReady && !wasTempoReady))
        {
            observedBpm = bpmKey;
            bpmChangedAtMs = now;
        }
        for (auto& entry : entries)
        {
            if (startupRequested)
                entry->failedBaseKey = entry->failedPitchedKey = 0;
            if (!isEnabled && wasEnabled)
                unpublish(*entry);
            reclaim(*entry);
        }
        wasEnabled = isEnabled;
        wasTempoReady = tempoReady;
        if (!isEnabled || bpmKey == 0)
        {
            wait(20);
            continue;
        }

        // Keep only the five greatest play stamps; repeated hits promote one entry.
        std::array<std::pair<uint64_t, Entry*>, recentSampleCount> recent {};
        for (auto& owned : entries)
        {
            auto& entry = *owned;
            auto requested = entry.requestedKey.load(std::memory_order_acquire);
            if (requested != 0
                && makeKey(keyBpm(requested), 1.0) != bpmKey)
                entry.requestedKey.compare_exchange_strong(requested, 0,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed);

            auto stamp = entry.lastPlayed.load(std::memory_order_acquire);
            Entry* candidate = &entry;
            for (auto& position : recent)
                if (stamp > position.first)
                {
                    std::swap(stamp, position.first);
                    std::swap(candidate, position.second);
                }

            const auto pitchKey = makeKey(keyBpm(bpmKey),
                parameters.getPitchRatioForMidiNote(entry.sound->getMidiRootNote()));
            if (pitchKey != entry.observedPitchKey)
            {
                entry.observedPitchKey = pitchKey;
                entry.pitchChangedAtMs = now;
                entry.failedPitchedKey = 0;
            }
            const auto request = entry.requestedKey.load(std::memory_order_acquire);
            if (request != entry.observedRequestKey)
            {
                entry.observedRequestKey = request;
                entry.requestChangedAtMs = now;
            }
        }

        bool didWork = false;
        bool urgentPending = false;
        if (now - bpmChangedAtMs >= bpmDebounceMs)
        {
            for (const auto& item : recent)
            {
                auto* entry = item.second;
                if (entry != nullptr && entry->observedPitchKey != 0
                    && !hasCache(*entry, entry->observedPitchKey)
                    && (neutralKey(entry->observedPitchKey) ? entry->failedBaseKey
                                                          : entry->failedPitchedKey) != entry->observedPitchKey)
                    urgentPending = true;
                if (entry != nullptr && now - entry->pitchChangedAtMs >= pitchDebounceMs
                    && render(*entry, entry->observedPitchKey, RenderPurpose::recent))
                {
                    didWork = true;
                    break; // Re-evaluate tempo/pitch/recency after each render.
                }
            }
            // Active sounds outside the recent five can still request a cache.
            if (!didWork)
                for (auto& entry : entries)
                {
                    const auto key = entry->requestedKey.load(std::memory_order_acquire);
                    if (key != 0 && !hasCache(*entry, key)
                        && (neutralKey(key) ? entry->failedBaseKey : entry->failedPitchedKey) != key)
                        urgentPending = true;
                    if (key != 0 && now - entry->requestChangedAtMs >= pitchDebounceMs
                        && render(*entry, key, RenderPurpose::demand))
                    {
                        didWork = true;
                        break;
                    }
                }
            // A load/restore pass includes every warp variant at its current
            // pitch. It waits for actual host timing but not Play or a note-on.
            if (!didWork && !urgentPending && startupPending && tempoReady)
            {
                bool allPrepared = true;
                for (auto& entry : entries)
                {
                    const auto key = entry->observedPitchKey;
                    if (key == 0 || hasCache(*entry, key)
                        || (neutralKey(key) ? entry->failedBaseKey : entry->failedPitchedKey) == key)
                        continue;
                    allPrepared = false;
                    if (now - entry->pitchChangedAtMs >= pitchDebounceMs
                        && render(*entry, key, RenderPurpose::startup))
                    {
                        didWork = true;
                        break;
                    }
                }
                if (allPrepared)
                    startupPending = false;
            }
            // Preserve BPM-only prewarming of the remaining inventory during transport.
            if (!didWork && !urgentPending && !(startupPending && tempoReady)
                && transportRunning.load(std::memory_order_relaxed))
                for (auto& entry : entries)
                    if (render(*entry, bpmKey, RenderPurpose::base))
                        break;
        }
        wait(20);
    }
}
