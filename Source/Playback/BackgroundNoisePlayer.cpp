#include "BackgroundNoisePlayer.h"
#include "Parameters/PluginParameters.h"
#include <NoiseBinaryData.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <limits>
#include <memory>
#include <cstdint>
#include <chrono>
#include <exception>

juce::StringArray BackgroundNoisePlayer::getSourceNames()
{
    juce::StringArray names;
    for (int i = 0; i < NoiseBinaryData::namedResourceListSize; ++i)
        names.add(juce::String(NoiseBinaryData::originalFilenames[i]).upToLastOccurrenceOf(".", false, false));
    names.add("Imported audio");
    return names;
}

BackgroundNoisePlayer::BackgroundNoisePlayer() : juce::Thread("Noise importer")
{
    juce::WavAudioFormat format;
    sources.resize(static_cast<size_t>(NoiseBinaryData::namedResourceListSize));
    for (int i = 0; i < NoiseBinaryData::namedResourceListSize; ++i)
    {
        int size = 0;
        const auto* data = NoiseBinaryData::getNamedResource(NoiseBinaryData::namedResourceList[i], size);
        if (data == nullptr || size <= 0)
            continue;

        auto stream = std::make_unique<juce::MemoryInputStream>(data, static_cast<size_t>(size), false);
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
        if (reader == nullptr || reader->lengthInSamples <= 0
            || reader->lengthInSamples > std::numeric_limits<int>::max()
            || reader->numChannels == 0 || !std::isfinite(reader->sampleRate)
            || reader->sampleRate < 1000.0 || reader->sampleRate > 768000.0)
            continue;

        auto& source = sources[static_cast<size_t>(i)];
        source.audio.setSize(juce::jmin(2, static_cast<int>(reader->numChannels)),
                             static_cast<int>(reader->lengthInSamples));
        if (!reader->read(&source.audio, 0, source.audio.getNumSamples(), 0, true, true))
        {
            source.audio.setSize(0, 0);
            continue;
        }
        source.sampleRate = reader->sampleRate;
        prepareWaveform(source);
    }
    prepare(outputSampleRate);
    if (!startThread())
        stopping = true;
}

void BackgroundNoisePlayer::prepareWaveform(Source& source)
{
    // Sanitize once off audio; keep interpolation and gain bounded even for
    // float WAV files containing invalid or out-of-range sample values.
    for (int channel = 0; channel < source.audio.getNumChannels(); ++channel)
    {
        auto* samples = source.audio.getWritePointer(channel);
        for (int frame = 0; frame < source.audio.getNumSamples(); ++frame)
            samples[frame] = std::isfinite(samples[frame])
                ? juce::jlimit(-1.0f, 1.0f, samples[frame]) : 0.0f;
    }
    auto& waveform = source.waveform;
    waveform.samples = source.audio.getNumSamples();
    for (int bin = 0; bin < Waveform::bins; ++bin)
    {
        const int first = static_cast<int>(static_cast<int64_t>(bin) * waveform.samples / Waveform::bins);
        const int end = juce::jmax(first + 1, static_cast<int>(
            static_cast<int64_t>(bin + 1) * waveform.samples / Waveform::bins));
        float low = 0.0f, high = 0.0f;
        for (int channel = 0; channel < source.audio.getNumChannels(); ++channel)
        {
            const auto* samples = source.audio.getReadPointer(channel);
            for (int frame = first; frame < end; ++frame)
            {
                low = juce::jmin(low, samples[frame]);
                high = juce::jmax(high, samples[frame]);
            }
        }
        waveform.minimum[static_cast<size_t>(bin)] = low;
        waveform.maximum[static_cast<size_t>(bin)] = high;
        waveform.peak = juce::jmax(waveform.peak, juce::jmax(-low, high));
    }
}

namespace
{
constexpr int64_t maximumImportedBytes = 128 * 1024 * 1024;
const juce::Identifier importedStateType { "ImportedNoise" };
static_assert(std::atomic<int>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);

juce::String importedDisplayName(const juce::String& name)
{
    juce::String result;
    int bracketDepth = 0;
    bool spacePending = false;
    for (const auto character : name)
    {
        if (character == '[')
        {
            ++bracketDepth;
            continue;
        }
        if (character == ']')
        {
            bracketDepth = juce::jmax(0, bracketDepth - 1);
            continue;
        }
        if (bracketDepth > 0)
            continue;
        if (juce::CharacterFunctions::isWhitespace(character))
        {
            spacePending = result.isNotEmpty();
            continue;
        }
        if (spacePending)
            result += " ";
        spacePending = false;
        result += juce::String::charToString(character);
    }
    return result.isNotEmpty() ? result : juce::String("Imported audio");
}
}

BackgroundNoisePlayer::~BackgroundNoisePlayer()
{
    {
        const std::lock_guard<std::mutex> lock(importMutex);
        stopping = true;
        importGeneration.fetch_add(1);
    }
    importWake.notify_all();
    stopThread(-1);
    reset(); // Host rendering has stopped before processor destruction.
}

int BackgroundNoisePlayer::importedSourceIndex() noexcept
{
    return NoiseBinaryData::namedResourceListSize;
}

bool BackgroundNoisePlayer::canImportFile(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return formats.findFormatForFileExtension(file.getFileExtension()) != nullptr;
}

void BackgroundNoisePlayer::importFile(const juce::File& file)
{
    {
        const std::lock_guard<std::mutex> lock(importMutex);
        if (stopping)
        {
            importError = "The noise importer could not start.";
            return;
        }
        requestedFile = file;
        requestedName = importedDisplayName(file.getFileNameWithoutExtension());
        requestedWav.clear();
        importError.clear();
        importPending = true;
        importGeneration.fetch_add(1);
    }
    importWake.notify_one();
}

juce::String BackgroundNoisePlayer::getImportedName() const
{
    const std::lock_guard<std::mutex> lock(importMutex);
    const int slot = publishedImported.load();
    return slot >= 0 ? importedSlots[static_cast<size_t>(slot)].source->name : juce::String();
}

juce::String BackgroundNoisePlayer::takeImportError()
{
    const std::lock_guard<std::mutex> lock(importMutex);
    auto error = importError;
    importError.clear();
    return error;
}

void BackgroundNoisePlayer::writeImportedState(juce::ValueTree& state)
{
    // State serialization is non-realtime. An immediate save after a drop must
    // include that drop, even if decoding has not finished yet.
    std::unique_lock<std::mutex> lock(importMutex);
    importFinished.wait(lock, [this] { return !importPending || stopping; });
    state.removeChild(state.getChildWithName(importedStateType), nullptr);
    const int slot = publishedImported.load();
    if (slot >= 0)
    {
        const auto& source = *importedSlots[static_cast<size_t>(slot)].source;
        juce::ValueTree saved(importedStateType);
        saved.setProperty("name", source.name, nullptr);
        saved.setProperty("wav", source.encodedWav, nullptr);
        state.appendChild(saved, nullptr);
    }
}

void BackgroundNoisePlayer::restoreImportedState(const juce::ValueTree& state)
{
    const auto saved = state.getChildWithName(importedStateType);
    {
        const std::lock_guard<std::mutex> lock(importMutex);
        importGeneration.fetch_add(1); // Cancels any older in-flight drop/restore.
        requestedFile = juce::File{};
        requestedName = importedDisplayName(saved.getProperty("name", "Imported audio").toString());
        requestedWav = saved.getProperty("wav").toString();
        importError.clear();
        importPending = saved.isValid();
        publishedImported.store(-1);
        importedRevision.fetch_add(1);
    }
    importWake.notify_one();
    importFinished.notify_all();
}

std::unique_ptr<BackgroundNoisePlayer::Source> BackgroundNoisePlayer::decode(
    juce::AudioFormatReader& reader, uint32_t generation)
{
    if (reader.lengthInSamples <= 0 || reader.lengthInSamples > std::numeric_limits<int>::max()
        || reader.numChannels == 0 || !std::isfinite(reader.sampleRate)
        || reader.sampleRate < 1000.0 || reader.sampleRate > 768000.0)
        return {};
    const int channels = static_cast<int>(juce::jmin(2u, reader.numChannels));
    if (reader.lengthInSamples * channels * static_cast<int64_t>(sizeof(float)) > maximumImportedBytes)
        return {};
    auto source = std::make_unique<Source>();
    source->sampleRate = reader.sampleRate;
    source->audio.setSize(channels, static_cast<int>(reader.lengthInSamples));
    for (int first = 0; first < source->audio.getNumSamples();)
    {
        if (generation != importGeneration.load())
            return {};
        const int count = juce::jmin(16384, source->audio.getNumSamples() - first);
        if (!reader.read(&source->audio, first, count, first, true, channels > 1))
            return {};
        first += count;
    }
    prepareWaveform(*source);
    return source;
}

void BackgroundNoisePlayer::run()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (;;)
    {
        std::unique_lock<std::mutex> lock(importMutex);
        importWake.wait(lock, [this] { return stopping || importPending; });
        if (stopping)
            return;
        const auto generation = importGeneration.load();
        const auto file = requestedFile;
        const auto name = requestedName;
        const auto encoded = requestedWav;
        lock.unlock();
        std::unique_ptr<Source> source;
        try
        {
            juce::MemoryBlock savedBytes;
            std::unique_ptr<juce::AudioFormatReader> reader;
            if (file != juce::File())
                reader.reset(formats.createReaderFor(file));
            else if (encoded.length() <= (maximumImportedBytes + 4096) * 4 / 3 + 4)
            {
                bool decoded = false;
                {
                    juce::MemoryOutputStream stream(savedBytes, false);
                    decoded = juce::Base64::convertFromBase64(stream, encoded);
                }
                if (decoded)
                    reader.reset(formats.createReaderFor(
                        std::make_unique<juce::MemoryInputStream>(savedBytes, false)));
            }
            if (reader != nullptr)
                source = decode(*reader, generation);
            if (source != nullptr && generation == importGeneration.load())
            {
                source->name = name;
                if (file == juce::File())
                    source->encodedWav = encoded;
                else
                {
                    // Canonical PCM WAV keeps restored imports portable across
                    // platforms regardless of which decoder opened the original.
                    juce::MemoryBlock wav;
                    std::unique_ptr<juce::OutputStream> stream =
                        std::make_unique<juce::MemoryOutputStream>(wav, false);
                    juce::WavAudioFormat format;
                    auto writer = format.createWriterFor(stream,
                        juce::AudioFormatWriterOptions().withSampleRate(source->sampleRate)
                            .withNumChannels(source->audio.getNumChannels()).withBitsPerSample(32)
                            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
                    bool written = writer != nullptr;
                    for (int first = 0; written && first < source->audio.getNumSamples();)
                    {
                        const int count = juce::jmin(16384, source->audio.getNumSamples() - first);
                        written = generation == importGeneration.load()
                            && writer->writeFromAudioSampleBuffer(source->audio, first, count);
                        first += count;
                    }
                    writer.reset(); // Finalizes the header before embedding.
                    if (written)
                        source->encodedWav = juce::Base64::toBase64(wav.getData(), wav.getSize());
                    else
                        source.reset();
                }
            }
        }
        catch (const std::exception&)
        {
            source.reset();
        }
        lock.lock();
        if (stopping)
            return;
        if (generation != importGeneration.load())
            continue;
        if (source != nullptr)
        {
            // At most two slots can be pinned by the single audio reader. The
            // worker alone replaces sources; UI/state copies use importMutex.
            int destination = -1;
            while (destination < 0 && !stopping && generation == importGeneration.load())
            {
                for (int i = 0; i < static_cast<int>(importedSlots.size()); ++i)
                {
                    if (i == publishedImported.load())
                        continue;
                    int expected = 0;
                    if (importedSlots[static_cast<size_t>(i)].readers.compare_exchange_strong(expected, -1))
                    {
                        destination = i;
                        break;
                    }
                }
                if (destination < 0)
                    importWake.wait_for(lock, std::chrono::milliseconds(10));
            }
            if (destination < 0)
                continue;
            auto& slot = importedSlots[static_cast<size_t>(destination)];
            slot.source = std::move(source); // Destruction is loader-only.
            slot.readers.store(0);
            publishedImported.store(destination);
            importedRevision.fetch_add(1);
        }
        else
        {
            importError = "Could not load the noise audio. Choose a supported, readable audio file "
                          "with no more than 128 MB of decoded mono/stereo audio.";
        }
        importPending = false;
        requestedFile = juce::File{};
        requestedWav.clear();
        lock.unlock();
        importFinished.notify_all();
    }
}

int BackgroundNoisePlayer::acquireImported() noexcept
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const int index = publishedImported.load();
        if (index < 0)
            return -1;
        auto& slot = importedSlots[static_cast<size_t>(index)];
        int readers = slot.readers.load();
        if (readers < 0 || !slot.readers.compare_exchange_strong(readers, readers + 1))
            continue;
        if (publishedImported.load() == index)
            return index;
        slot.readers.fetch_sub(1);
    }
    return -1;
}

void BackgroundNoisePlayer::releaseImported(int slot) noexcept
{
    if (slot >= 0)
        importedSlots[static_cast<size_t>(slot)].readers.fetch_sub(1);
}

void BackgroundNoisePlayer::prepare(double sampleRate) noexcept
{
    outputSampleRate = std::isfinite(sampleRate) && sampleRate >= 1000.0 && sampleRate <= 768000.0
        ? sampleRate : 44100.0;
    gain.reset(outputSampleRate, 0.01);
    regionFade.reset(outputSampleRate, 0.002);
    reset();
}

void BackgroundNoisePlayer::reset() noexcept
{
    playheadSnapshot.store(0, std::memory_order_relaxed);
    trimStopPending = false;
    gain.setCurrentAndTargetValue(0.0f);
    regionFade.setCurrentAndTargetValue(0.0f);
    position = 0.0;
    activeSource = -1;
    activeData = nullptr;
    releaseImported(activeImportedSlot);
    activeImportedSlot = -1;
    activeRegion = {};
    crossfadeFrames = 0;
}

BackgroundNoisePlayer::Region BackgroundNoisePlayer::resolveRegion(int samples, float start, float end) noexcept
{
    if (samples <= 0)
        return {};
    const double from = std::isfinite(start) ? juce::jlimit(0.0, 1.0, static_cast<double>(start)) : 0.0;
    const double to = std::isfinite(end) ? juce::jlimit(0.0, 1.0, static_cast<double>(end)) : 1.0;
    const int first = juce::jmin(samples - 1, static_cast<int>(from * samples));
    return { first, juce::jlimit(first + 1, samples, static_cast<int>(std::ceil(to * samples))) };
}

bool BackgroundNoisePlayer::copyWaveform(int sourceIndex, Waveform& destination) const
{
    if (sourceIndex == importedSourceIndex())
    {
        const std::lock_guard<std::mutex> lock(importMutex);
        const int slot = publishedImported.load();
        if (slot < 0)
            return false;
        destination = importedSlots[static_cast<size_t>(slot)].source->waveform;
        return true;
    }
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(sources.size()))
        return false;
    destination = sources[static_cast<size_t>(sourceIndex)].waveform;
    return true;
}

float BackgroundNoisePlayer::getPlayheadPosition(int sourceIndex) const noexcept
{
    if (sourceIndex < 0 || sourceIndex > importedSourceIndex())
        return -1.0f;
    int sourceKey = sourceIndex;
    if (sourceIndex == importedSourceIndex())
    {
        const int slot = publishedImported.load();
        if (slot < 0)
            return -1.0f;
        sourceKey += slot;
    }
    const auto snapshot = playheadSnapshot.load(std::memory_order_relaxed);
    if ((snapshot >> 32) != static_cast<uint64_t>(sourceKey) + 1u)
        return -1.0f;
    return static_cast<float>(static_cast<double>(static_cast<uint32_t>(snapshot))
        / static_cast<double>(std::numeric_limits<uint32_t>::max()));
}

void BackgroundNoisePlayer::setTrimActive(bool active) noexcept
{
    const auto state = trimGestureState.load(std::memory_order_relaxed);
    if (((state & 1u) != 0) != active)
        trimGestureState.store(state + 1u, std::memory_order_release);
}

void BackgroundNoisePlayer::process(juce::AudioBuffer<float>& output,
                                    float sourceIndex, float gainDb, const float* modulation,
                                    float start, float end, uint32_t gestureState) noexcept
{
    const int channels = juce::jmin(2, output.getNumChannels());
    if (channels == 0)
    {
        playheadSnapshot.store(0, std::memory_order_relaxed);
        return;
    }
    if (output.getNumSamples() == 0)
        return;

    if (gestureState != lastTrimGestureState)
    {
        lastTrimGestureState = gestureState;
        trimStopPending = activeSource >= 0;
    }
    // Complete a smooth stop even if mouse-up arrives before the fade ends.
    // Once reset, the first unpaused render restarts at the final trimmed start.
    const bool trimPaused = (gestureState & 1u) != 0 || trimStopPending;
    const int requestedSource = std::isfinite(sourceIndex)
        ? static_cast<int>(std::round(juce::jlimit(0.0f, static_cast<float>(importedSourceIndex()), sourceIndex))) : 0;
    const bool imported = requestedSource == importedSourceIndex();
    const int requestedSlot = imported ? acquireImported() : -1;
    // This guard releases only an atomic reader pin, never the source itself.
    struct PinRelease
    {
        BackgroundNoisePlayer& owner;
        int slot;
        ~PinRelease() { owner.releaseImported(slot); }
    } pin { *this, requestedSlot };
    const Source* requestedData = imported
        ? (requestedSlot >= 0 ? importedSlots[static_cast<size_t>(requestedSlot)].source.get() : nullptr)
        : &sources[static_cast<size_t>(requestedSource)];
    // An unsuccessful bounded acquisition should not interrupt an existing loop.
    if (imported && requestedData == nullptr && publishedImported.load() >= 0)
        requestedData = activeSource == requestedSource ? activeData : nullptr;
    const bool sameSource = requestedSource == activeSource && requestedData == activeData;
    const float targetGain = !trimPaused && requestedData != nullptr && std::isfinite(gainDb) && gainDb > PluginParameters::noiseGainDbSilence
        ? juce::Decibels::decibelsToGain(juce::jlimit(PluginParameters::noiseGainDbMinimum,
                                                   PluginParameters::noiseGainDbMaximum, gainDb)) : 0.0f;
    const auto requestedRegion = resolveRegion(requestedData != nullptr ? requestedData->audio.getNumSamples() : 0,
                                                start, end);
    const auto adoptRegion = [&]() noexcept
    {
        if (requestedData != activeData)
        {
            releaseImported(activeImportedSlot);
            activeImportedSlot = requestedSlot;
            if (activeImportedSlot >= 0)
                importedSlots[static_cast<size_t>(activeImportedSlot)].readers.fetch_add(1);
        }
        activeData = requestedData;
        activeSource = requestedSource;
        activeRegion = requestedRegion;
        position = activeRegion.start;
        crossfadeFrames = juce::jmin(static_cast<int>(activeData->sampleRate * 0.01),
                                     (activeRegion.end - activeRegion.start) / 2);
        regionFade.setCurrentAndTargetValue(0.0f);
        regionFade.setTargetValue(1.0f);
    };
    if (activeSource < 0)
    {
        if (targetGain == 0.0f)
        {
            trimStopPending = false;
            return;
        }
        adoptRegion();
    }
    // Finish fading out the old source before adopting another immutable buffer.
    gain.setTargetValue(sameSource ? targetGain : 0.0f);
    bool regionChanged = !trimPaused && sameSource
        && (requestedRegion.start != activeRegion.start || requestedRegion.end != activeRegion.end);
    regionFade.setTargetValue(regionChanged ? 0.0f : 1.0f);
    auto* left = output.getWritePointer(0);
    auto* right = channels > 1 ? output.getWritePointer(1) : nullptr;

    for (int frame = 0; frame < output.getNumSamples(); ++frame)
    {
        if (!gain.isSmoothing() && gain.getCurrentValue() == 0.0f)
        {
            if (targetGain == 0.0f)
            {
                reset();
                return;
            }
            adoptRegion();
            regionChanged = false;
            gain.setTargetValue(targetGain);
        }

        if (regionChanged && !regionFade.isSmoothing() && regionFade.getCurrentValue() == 0.0f)
        {
            adoptRegion();
            regionChanged = false;
        }

        const auto& source = *activeData;
        const int length = activeRegion.end - activeRegion.start;
        if (length <= 0)
        {
            reset();
            return;
        }
        const auto read = [this, &source] (int channel, double at) noexcept
        {
            const int first = static_cast<int>(at);
            const int second = juce::jmin(first + 1, activeRegion.end - 1);
            const float fraction = static_cast<float>(at - first);
            const auto* samples = source.audio.getReadPointer(channel);
            return samples[first] + fraction * (samples[second] - samples[first]);
        };
        const auto loopedSample = [&] (int channel) noexcept
        {
            const float sample = read(channel, position);
            const double fadeStart = activeRegion.end - crossfadeFrames;
            if (crossfadeFrames == 0 || position < fadeStart)
                return sample;
            const double headPosition = activeRegion.start + position - fadeStart;
            const float mix = static_cast<float>((position - fadeStart) / crossfadeFrames);
            return sample + mix * (read(channel, headPosition) - sample);
        };

        const float amplitude = gain.getNextValue();
        if (amplitude == 0.0f && targetGain == 0.0f)
        {
            reset();
            return;
        }
        const float l = loopedSample(0);
        const float r = source.audio.getNumChannels() > 1 ? loopedSample(1) : l;
        const float modulatedGain = amplitude * regionFade.getNextValue() * modulation[frame];
        left[frame] += (right != nullptr ? l : 0.5f * l + 0.5f * r) * modulatedGain;
        if (right != nullptr)
            right[frame] += r * modulatedGain;

        position += source.sampleRate / outputSampleRate;
        if (position >= activeRegion.end)
        {
            position = activeRegion.start + crossfadeFrames
                + std::fmod(position - activeRegion.end, static_cast<double>(length - crossfadeFrames));
            // Adding a tiny remainder to a large source offset can round to
            // the exclusive endpoint. Keep the next interpolation read inside.
            if (position >= activeRegion.end)
                position = activeRegion.start + crossfadeFrames;
        }
    }
    const int sourceKey = activeSource + (activeImportedSlot >= 0 ? activeImportedSlot : 0);
    const auto normalizedPosition = static_cast<uint32_t>(
        juce::jlimit(0.0, 1.0, position / activeData->audio.getNumSamples())
        * static_cast<double>(std::numeric_limits<uint32_t>::max()));
    playheadSnapshot.store(gain.getCurrentValue() > 0.0f
        ? ((static_cast<uint64_t>(sourceKey) + 1u) << 32) | normalizedPosition : 0,
        std::memory_order_relaxed);
}
