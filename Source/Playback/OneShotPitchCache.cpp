#include "OneShotPitchCache.h"

#include "Parameters/SampleSpecificRealtimeCache.h"
#include "PercussionSynthesiser.h"
#include <rubberband/RubberBandStretcher.h>
#include <cmath>
#include <limits>
#include <utility>

static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

namespace
{
    bool samePitch(float a, float b) noexcept { return std::abs(a - b) < 1.0e-6f; }
}

// Only the coordinator creates/submits/joins batches. The vector is sized before
// submission; each job claims a different element and owns its Rubber Band engine.
struct OneShotPitchCache::RenderBatch
{
    struct Job final : juce::ThreadPoolJob
    {
        explicit Job(RenderBatch& batchToRender)
            : juce::ThreadPoolJob("One-shot pitch variation"), batch(batchToRender) {}

        JobStatus runJob() override
        {
            try
            {
                const auto& sounds = batch.owner.groups[(size_t) batch.note].sounds;
                while (!batch.cancelled.load(std::memory_order_relaxed))
                {
                    const auto index = batch.nextSound.fetch_add(1, std::memory_order_relaxed);
                    if (index >= sounds.size())
                        break;
                    if (!batch.owner.requestStillCurrent(batch.note, batch.pitch, batch.sampleRate))
                    {
                        batch.cancelled.store(true, std::memory_order_relaxed);
                        break;
                    }
                    if (!batch.owner.renderSound(*sounds[index], batch.pitch,
                            batch.result->buffers[index], batch.note, batch.sampleRate, batch.cancelled))
                    {
                        if (!batch.cancelled.load(std::memory_order_relaxed)
                            && batch.owner.requestStillCurrent(batch.note, batch.pitch, batch.sampleRate))
                            batch.failed.store(true, std::memory_order_relaxed);
                        batch.cancelled.store(true, std::memory_order_relaxed);
                        break;
                    }
                }
            }
            catch (...)
            {
                batch.failed.store(true, std::memory_order_relaxed);
                batch.cancelled.store(true, std::memory_order_relaxed);
            }
            return jobHasFinished;
        }

        RenderBatch& batch;
    };

    RenderBatch(OneShotPitchCache& cache, int midiNote, float ratio, double rate)
        : owner(cache), note(midiNote), pitch(ratio), sampleRate(rate),
          result(std::make_unique<Snapshot>())
    {
        result->sampleRate = rate;
        result->buffers.resize(owner.groups[(size_t) note].sounds.size());
    }

    ~RenderBatch()
    {
        // Also handles exceptions during partial submission. Jobs and their
        // borrowed output buffers must survive until every pool job has left.
        cancelled.store(true, std::memory_order_relaxed);
        for (auto& job : jobs)
            if (job != nullptr)
                owner.renderWorkers.removeJob(job.get(), true, -1);
    }

    OneShotPitchCache& owner;
    const int note;
    const float pitch;
    const double sampleRate;
    std::unique_ptr<Snapshot> result;
    std::atomic<size_t> nextSound { 0 };
    std::atomic<bool> cancelled { false }, failed { false };
    std::array<std::unique_ptr<Job>, renderWorkerCount> jobs;
};

OneShotPitchCache::Lease::Lease(Lease&& other) noexcept
    : slot(std::exchange(other.slot, nullptr)), buffer(std::exchange(other.buffer, nullptr)),
      sampleRate(other.sampleRate)
{
}

OneShotPitchCache::Lease& OneShotPitchCache::Lease::operator=(Lease&& other) noexcept
{
    if (this != &other)
    {
        reset();
        slot = std::exchange(other.slot, nullptr);
        buffer = std::exchange(other.buffer, nullptr);
        sampleRate = other.sampleRate;
    }
    return *this;
}

void OneShotPitchCache::Lease::reset() noexcept
{
    buffer = nullptr;
    if (auto* previous = std::exchange(slot, nullptr))
        previous->readers.fetch_sub(1, std::memory_order_release);
}

OneShotPitchCache::OneShotPitchCache(PercussionSynthesiser& sampler,
                                     const SampleSpecificRealtimeCache& values)
    : juce::Thread("One-shot pitch preparation"), parameters(values)
{
    // The sampler inventory stays alive and immutable until the worker stops.
    for (int i = 0; i < sampler.getNumSounds(); ++i)
    {
        auto* sound = dynamic_cast<PercussionSound*>(sampler.getSound(i).get());
        if (sound == nullptr || !sound->isOneShot())
            continue;
        const int note = sound->getMidiRootNote();
        if (note < 0 || note >= noteCount)
            continue;
        auto& group = groups[(size_t) note];
        sound->setOneShotPitchIndex(static_cast<int>(group.sounds.size()));
        group.sounds.push_back(sound);
    }
    startThread();
}

OneShotPitchCache::~OneShotPitchCache()
{
    // Stop voices first. The coordinator cancels/joins its batch before exiting;
    // the pool is then destroyed before groups or source sounds can disappear.
    stopThread(-1);
}

bool OneShotPitchCache::supportsMidiNote(int midiNote) const noexcept
{
    return midiNote >= 0 && midiNote < noteCount && !groups[(size_t) midiNote].sounds.empty();
}

void OneShotPitchCache::recordPlayed(const PercussionSound& sound) noexcept
{
    const int note = sound.getMidiRootNote();
    if (supportsMidiNote(note))
        groups[(size_t) note].lastPlayed.store(
            playCounter.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
}

void OneShotPitchCache::setPlaybackSampleRate(double sampleRate) noexcept
{
    if (std::isfinite(sampleRate) && sampleRate >= 1.0)
        playbackSampleRate.store(sampleRate, std::memory_order_relaxed);
}

OneShotPitchCache::Status OneShotPitchCache::getStatus(int midiNote) const noexcept
{
    if (!supportsMidiNote(midiNote) || !parameters.getPitchPreserveLengthForMidiNote(midiNote))
        return Status::ready;
    const float pitch = parameters.getPitchRatioForMidiNote(midiNote);
    const double sampleRate = playbackSampleRate.load(std::memory_order_relaxed);
    const auto& group = groups[(size_t) midiNote];
    if (samePitch(pitch, 1.0f) || (samePitch(pitch, group.readyPitch.load(std::memory_order_acquire))
        && sampleRate == group.readySampleRate.load(std::memory_order_relaxed)))
        return Status::ready;
    if (!isThreadRunning() || (samePitch(pitch, group.failedPitch.load(std::memory_order_relaxed))
        && sampleRate == group.failedSampleRate.load(std::memory_order_relaxed)))
        return Status::failed;
    return Status::preparing;
}

OneShotPitchCache::Lease OneShotPitchCache::acquire(const PercussionSound& sound) noexcept
{
    Lease lease;
    const int note = sound.getMidiRootNote();
    const int soundIndex = sound.getOneShotPitchIndex();
    if (!supportsMidiNote(note) || soundIndex < 0)
        return lease;
    auto& group = groups[(size_t) note];
    // A worker may retire a slot between the index load and the pin. Retry a
    // bounded number of times; the original, unshifted sample is always available.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const int index = group.publishedSlot.load(std::memory_order_acquire);
        if (index < 0)
            break;
        auto& slot = group.slots[(size_t) index];
        int readers = slot.readers.load(std::memory_order_relaxed);
        if (readers < 0 || !slot.readers.compare_exchange_strong(readers, readers + 1,
                                                                 std::memory_order_acquire,
                                                                 std::memory_order_relaxed))
            continue;
        // The writer cannot modify this slot until the lease releases it.
        if (slot.snapshot != nullptr && (size_t) soundIndex < slot.snapshot->buffers.size())
        {
            lease.slot = &slot;
            lease.buffer = &slot.snapshot->buffers[(size_t) soundIndex];
            lease.sampleRate = slot.snapshot->sampleRate;
            return lease;
        }
        slot.readers.fetch_sub(1, std::memory_order_release);
    }
    return lease;
}

bool OneShotPitchCache::requestStillCurrent(int midiNote, float pitch, double sampleRate) const noexcept
{
    return !threadShouldExit()
        && parameters.getPitchPreserveLengthForMidiNote(midiNote)
        && samePitch(pitch, parameters.getPitchRatioForMidiNote(midiNote))
        && sampleRate == playbackSampleRate.load(std::memory_order_relaxed);
}

void OneShotPitchCache::reclaimRetiredSlots(Group& group)
{
    const int published = group.publishedSlot.load(std::memory_order_relaxed);
    for (int i = 0; i < slotCount; ++i)
    {
        if (i == published)
            continue;
        auto& slot = group.slots[(size_t) i];
        int expected = 0;
        if (slot.readers.compare_exchange_strong(expected, writingSlot, std::memory_order_acquire))
        {
            slot.snapshot.reset(); // All superseded buffer destruction stays off audio.
            slot.readers.store(0, std::memory_order_release);
        }
    }
}

void OneShotPitchCache::refreshRequests()
{
    const double sampleRate = playbackSampleRate.load(std::memory_order_relaxed);
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int note = 0; note < noteCount; ++note)
    {
        auto& group = groups[(size_t) note];
        if (group.sounds.empty())
            continue;
        reclaimRetiredSlots(group);
        const float pitch = parameters.getPitchRatioForMidiNote(note);
        const bool preserve = parameters.getPitchPreserveLengthForMidiNote(note);
        if (!samePitch(pitch, group.observedPitch) || sampleRate != group.observedSampleRate
            || preserve != group.observedPreserveLength)
        {
            group.observedPitch = pitch;
            group.observedSampleRate = sampleRate;
            group.observedPreserveLength = preserve;
            group.changedAtMs = now;
            group.failedPitch.store(0.0f, std::memory_order_relaxed);
        }
        if (samePitch(pitch, 1.0f))
        {
            group.publishedSlot.store(-1, std::memory_order_release);
            group.readyPitch.store(1.0f, std::memory_order_release);
        }
    }
}

bool OneShotPitchCache::needsRender(int note) const noexcept
{
    if (!supportsMidiNote(note) || !parameters.getPitchPreserveLengthForMidiNote(note))
        return false;
    const auto& group = groups[(size_t) note];
    const float pitch = parameters.getPitchRatioForMidiNote(note);
    const double rate = playbackSampleRate.load(std::memory_order_relaxed);
    return std::isfinite(pitch) && pitch > 0.0f && !samePitch(pitch, 1.0f)
        && std::isfinite(rate) && rate >= 1.0
        && !(samePitch(pitch, group.readyPitch.load(std::memory_order_relaxed))
             && rate == group.readySampleRate.load(std::memory_order_relaxed))
        && !(samePitch(pitch, group.failedPitch.load(std::memory_order_relaxed))
             && rate == group.failedSampleRate.load(std::memory_order_relaxed));
}

std::array<int, OneShotPitchCache::recentGroupCount> OneShotPitchCache::recentGroups() const noexcept
{
    std::array<int, recentGroupCount> notes;
    notes.fill(-1);
    std::array<uint64_t, recentGroupCount> stamps {};
    for (int note = 0; note < noteCount; ++note)
    {
        auto stamp = groups[(size_t) note].lastPlayed.load(std::memory_order_acquire);
        int candidate = note;
        for (size_t i = 0; i < notes.size(); ++i)
            if (stamp > stamps[i])
            {
                std::swap(stamp, stamps[i]);
                std::swap(candidate, notes[i]);
            }
    }
    return notes;
}

int OneShotPitchCache::nextGroupToRender() const noexcept
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const auto settled = [this, now](int note)
    {
        const auto& group = groups[(size_t) note];
        return now - group.changedAtMs >= 80.0
            && samePitch(group.observedPitch, parameters.getPitchRatioForMidiNote(note))
            && group.observedSampleRate == playbackSampleRate.load(std::memory_order_relaxed)
            && group.observedPreserveLength == parameters.getPitchPreserveLengthForMidiNote(note);
    };
    bool recentPending = false;
    for (const int note : recentGroups())
        if (needsRender(note))
        {
            recentPending = true;
            if (settled(note))
                return note;
        }
    // Let recent requests finish debouncing before starting less urgent work.
    if (!recentPending)
        for (int note = 0; note < noteCount; ++note)
            if (needsRender(note) && settled(note))
                return note;
    return -1;
}

bool OneShotPitchCache::shouldYieldToRecent(int midiNote) const noexcept
{
    const auto recent = recentGroups();
    for (const int note : recent)
        if (note == midiNote)
            return false; // Finish an in-flight recent group without restarting it.
    for (const int note : recent)
        if (needsRender(note))
            return true;
    return false;
}

void OneShotPitchCache::run()
{
    while (!threadShouldExit())
    {
        refreshRequests();
        const int note = nextGroupToRender();
        if (note < 0)
        {
            wait(20); // Audio/parameter callbacks only publish scalar atomics.
            continue;
        }
        auto& group = groups[(size_t) note];
        const float pitch = parameters.getPitchRatioForMidiNote(note);
        const double sampleRate = playbackSampleRate.load(std::memory_order_relaxed);
        bool deferred = false;
        std::unique_ptr<Snapshot> rendered;
        try { rendered = renderGroup(note, pitch, sampleRate, deferred); }
        catch (...) { /* Keep the last prepared group on allocation/engine failure. */ }
        if (deferred || !requestStillCurrent(note, pitch, sampleRate))
            continue;
        if (rendered == nullptr)
        {
            group.failedPitch.store(pitch, std::memory_order_relaxed);
            group.failedSampleRate.store(sampleRate, std::memory_order_relaxed);
            continue;
        }
        // At most maximumVoices slots are pinned, plus one published slot.
        const int published = group.publishedSlot.load(std::memory_order_relaxed);
        for (int i = 0; i < slotCount; ++i)
        {
            if (i == published)
                continue;
            auto& slot = group.slots[(size_t) i];
            int expected = 0;
            if (!slot.readers.compare_exchange_strong(expected, writingSlot, std::memory_order_acquire))
                continue;
            slot.snapshot = std::move(rendered);
            slot.readers.store(0, std::memory_order_release);
            group.publishedSlot.store(i, std::memory_order_release);
            group.readySampleRate.store(sampleRate, std::memory_order_relaxed);
            group.readyPitch.store(pitch, std::memory_order_release);
            break;
        }
    }
}

std::unique_ptr<OneShotPitchCache::Snapshot> OneShotPitchCache::renderGroup(int midiNote, float pitch,
                                                                        double sampleRate, bool& deferred)
{
    RenderBatch batch(*this, midiNote, pitch, sampleRate);
    const size_t jobCount = juce::jmin(batch.jobs.size(), batch.result->buffers.size());
    for (size_t i = 0; i < jobCount; ++i)
    {
        batch.jobs[i] = std::make_unique<RenderBatch::Job>(batch);
        renderWorkers.addJob(batch.jobs[i].get(), false);
    }
    double lastRefreshMs = juce::Time::getMillisecondCounterHiRes();
    while (renderWorkers.getNumJobs() != 0)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastRefreshMs >= 20.0)
        {
            // Observe all edits while a group renders, so Apply to All's other
            // requests debounce concurrently instead of paying 80 ms per group.
            refreshRequests();
            lastRefreshMs = now;
        }
        if (!requestStillCurrent(midiNote, pitch, sampleRate) || shouldYieldToRecent(midiNote))
            batch.cancelled.store(true, std::memory_order_relaxed);
        wait(5); // Coordinator only; pool completion is synchronized by JUCE's lock.
    }
    deferred = batch.cancelled.load(std::memory_order_relaxed)
            && !batch.failed.load(std::memory_order_relaxed);
    if (batch.cancelled.load(std::memory_order_relaxed))
        return nullptr;
    return std::move(batch.result);
}

bool OneShotPitchCache::renderSound(const PercussionSound& sound, float pitch,
                                   juce::AudioBuffer<float>& output, int midiNote, double outputSampleRate,
                                   const std::atomic<bool>& cancelled)
{
    const auto& source = sound.getAudioData();
    const int frames = source.getNumSamples();
    const int channels = source.getNumChannels();
    const double sampleRate = sound.getSourceSampleRate();
    if (frames <= 0 || channels < 1 || channels > 2 || !std::isfinite(sampleRate) || sampleRate < 1.0)
        return false;

    // Fold sample-rate conversion into Rubber Band's own resampling stage.
    // Internal stretch stays at pitch; final resampling is outputRate/(sourceRate*pitch).
    // Cached playback then advances exactly one frame per host sample.
    const double rateRatio = outputSampleRate / sampleRate;
    const double outputLength = std::round((double) frames * rateRatio);
    if (!std::isfinite(outputLength) || outputLength < 1.0
        || outputLength > (double) std::numeric_limits<int>::max())
        return false;
    const int outputFrames = (int) outputLength;

    using Stretcher = RubberBand::RubberBandStretcher;
    Stretcher stretcher((size_t) sampleRate, (size_t) channels,
                        Stretcher::OptionProcessOffline | Stretcher::OptionEngineFiner
                        | Stretcher::OptionWindowStandard | Stretcher::OptionChannelsTogether
                        | Stretcher::OptionThreadingNever,
                        rateRatio, pitch / rateRatio);
    constexpr int chunkSize = 1024;
    stretcher.setMaxProcessSize(chunkSize);
    stretcher.setExpectedInputDuration((size_t) frames);

    // Let R3 choose local timing without constraining it to metadata transients.
    // Punch still uses the original transient times during cached playback.

    std::array<const float*, 2> input {};
    for (int offset = 0; offset < frames;)
    {
        if (cancelled.load(std::memory_order_relaxed)
            || !requestStillCurrent(midiNote, pitch, outputSampleRate))
            return false;
        const int count = juce::jmin(chunkSize, frames - offset);
        for (int ch = 0; ch < channels; ++ch)
            input[(size_t) ch] = source.getReadPointer(ch, offset);
        stretcher.study(input.data(), (size_t) count, offset + count == frames);
        offset += count;
    }

    output.setSize(channels, outputFrames);
    output.clear();
    juce::AudioBuffer<float> scratch(channels, chunkSize);
    int written = 0;
    for (int offset = 0; offset < frames;)
    {
        if (cancelled.load(std::memory_order_relaxed)
            || !requestStillCurrent(midiNote, pitch, outputSampleRate))
            return false;
        const int count = juce::jmin(chunkSize, frames - offset);
        for (int ch = 0; ch < channels; ++ch)
            input[(size_t) ch] = source.getReadPointer(ch, offset);
        stretcher.process(input.data(), (size_t) count, offset + count == frames);
        offset += count;
        while (const int available = stretcher.available())
        {
            if (available < 0)
                break;
            if (cancelled.load(std::memory_order_relaxed)
                || !requestStillCurrent(midiNote, pitch, outputSampleRate))
                return false;
            const auto retrieved = stretcher.retrieve(scratch.getArrayOfWritePointers(),
                                                       (size_t) juce::jmin(chunkSize, available));
            if (retrieved == 0)
                return false;
            const int copyCount = juce::jmin((int) retrieved, outputFrames - written);
            for (int ch = 0; ch < channels; ++ch)
                output.copyFrom(ch, written, scratch, ch, 0, copyCount);
            written += copyCount;
        }
    }
    for (int ch = 0; ch < channels; ++ch)
        for (int offset = 0; offset < outputFrames;)
        {
            if (cancelled.load(std::memory_order_relaxed)
                || !requestStillCurrent(midiNote, pitch, outputSampleRate))
                return false;
            const int count = juce::jmin(chunkSize, outputFrames - offset);
            const auto* samples = output.getReadPointer(ch, offset);
            for (int i = 0; i < count; ++i)
                if (!std::isfinite(samples[i]))
                    return false;
            offset += count;
        }
    return written > 0;
}
