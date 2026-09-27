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
#include <algorithm>

juce::StringArray BackgroundNoisePlayer::getSourceNames()
{
    juce::StringArray names;
    for (int i = 0; i < NoiseBinaryData::namedResourceListSize; ++i)
        names.add(juce::String(NoiseBinaryData::originalFilenames[i]).upToLastOccurrenceOf(".", false, false));
    for (int i = 0; i < maximumCustomSources; ++i)
        names.add("Imported audio " + juce::String(i + 1));
    return names;
}

BackgroundNoisePlayer::BackgroundNoisePlayer() : juce::Thread("Noise importer")
{
    for (auto& identity : publishedIdentities)
        identity.store(0);
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
// Keep a complete portable library below JUCE/host integer-sized state limits.
constexpr int64_t maximumLibraryBytes = 512 * 1024 * 1024;
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

int BackgroundNoisePlayer::importFile(const juce::File& file)
{
    int index = -1;
    {
        const std::lock_guard<std::mutex> lock(importMutex);
        if (stopping)
        {
            importError = "The noise importer could not start.";
            return -1;
        }
        for (int i = 0; i < maximumCustomSources; ++i)
            if (importedNames[static_cast<size_t>(i)].isEmpty())
            {
                index = i;
                break;
            }
        if (index < 0)
        {
            importError = "This instance already contains 64 custom noises.";
            return -1;
        }
        const auto baseName = importedDisplayName(file.getFileNameWithoutExtension());
        auto name = baseName;
        for (int suffix = 2; std::find(importedNames.begin(), importedNames.end(), name)
                                  != importedNames.end(); ++suffix)
            name = baseName + " (" + juce::String(suffix) + ")";
        requests.push_back({ index, file, name, {} });
        importedNames[static_cast<size_t>(index)] = name;
        importPending = true;
        importedRevision.fetch_add(1);
    }
    importWake.notify_one();
    return importedSourceIndex() + index;
}

juce::StringArray BackgroundNoisePlayer::getAvailableSourceNames() const
{
    const std::lock_guard<std::mutex> lock(importMutex);
    auto names = getSourceNames();
    for (int i = 0; i < maximumCustomSources; ++i)
        names.set(importedSourceIndex() + i, importedNames[static_cast<size_t>(i)]);
    return names;
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
        juce::ValueTree saved(importedStateType);
        const auto& table = importedSlots[static_cast<size_t>(slot)].sources;
        for (int i = 0; i < maximumCustomSources; ++i)
            if (const auto& source = table[static_cast<size_t>(i)])
            {
                juce::ValueTree entry("Source");
                entry.setProperty("index", i, nullptr);
                entry.setProperty("name", source->name, nullptr);
                entry.setProperty("wav", source->encodedWav, nullptr);
                saved.appendChild(entry, nullptr);
            }
        state.appendChild(saved, nullptr);
    }
}

void BackgroundNoisePlayer::restoreImportedState(const juce::ValueTree& state)
{
    const auto saved = state.getChildWithName(importedStateType);
    {
        const std::lock_guard<std::mutex> lock(importMutex);
        importGeneration.fetch_add(1); // Cancels previous decoding, not sibling drops.
        requests.clear();
        importedNames.fill(juce::String());
        importError.clear();
        int64_t encodedBytes = 0;
        const auto enqueue = [this, &encodedBytes] (const juce::ValueTree& entry, int index)
        {
            if (index < 0 || index >= maximumCustomSources
                || importedNames[static_cast<size_t>(index)].isNotEmpty())
                return;
            const auto name = importedDisplayName(entry.getProperty("name", "Imported audio").toString());
            const auto wav = entry.getProperty("wav").toString();
            if (wav.isEmpty())
                return;
            if (encodedBytes + wav.length()
                > (maximumLibraryBytes + maximumCustomSources * 4096) * 4 / 3 + 4)
            {
                importError = "The saved custom noise library exceeds the supported audio size.";
                return;
            }
            encodedBytes += wav.length();
            requests.push_back({ index, juce::File{}, name, wav });
            importedNames[static_cast<size_t>(index)] = name;
        };
        if (saved.hasProperty("wav"))
            enqueue(saved, 0); // Existing single-import projects.
        else
            for (int i = 0; i < saved.getNumChildren(); ++i)
            {
                const auto entry = saved.getChild(i);
                if (entry.hasType("Source"))
                    enqueue(entry, static_cast<int>(entry.getProperty("index", -1)));
            }
        importPending = !requests.empty();
        publishedImported.store(-1);
        for (auto& identity : publishedIdentities)
            identity.store(0);
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
        importWake.wait(lock, [this] { return stopping || !requests.empty(); });
        if (stopping)
            return;
        const auto generation = importGeneration.load();
        auto request = std::move(requests.front());
        requests.pop_front();
        const auto& file = request.file;
        const auto& name = request.name;
        const auto& encoded = request.wav;
        lock.unlock();
        std::unique_ptr<Source> source;
        std::shared_ptr<const Source> preparedSource;
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
            if (source != nullptr)
            {
                source->identity = nextSourceIdentity++;
                if (nextSourceIdentity >= std::numeric_limits<uint32_t>::max()
                    - static_cast<uint32_t>(importedSourceIndex()))
                    nextSourceIdentity = 1;
                preparedSource = std::move(source);
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
        bool libraryFull = false;
        if (preparedSource != nullptr)
        {
            int64_t bytes = static_cast<int64_t>(preparedSource->audio.getNumSamples())
                * preparedSource->audio.getNumChannels() * sizeof(float);
            const int current = publishedImported.load();
            if (current >= 0)
                for (const auto& existing : importedSlots[static_cast<size_t>(current)].sources)
                    if (existing != nullptr)
                        bytes += static_cast<int64_t>(existing->audio.getNumSamples())
                            * existing->audio.getNumChannels() * sizeof(float);
            libraryFull = bytes > maximumLibraryBytes;
            if (libraryFull)
                preparedSource.reset();
        }
        if (preparedSource != nullptr)
        {
            // At most two tables can be pinned by the single audio reader. The
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
            const int previous = publishedImported.load();
            if (previous >= 0)
                slot.sources = importedSlots[static_cast<size_t>(previous)].sources;
            else
                slot.sources.fill(nullptr);
            slot.sources[static_cast<size_t>(request.index)] = std::move(preparedSource);
            const auto identity = slot.sources[static_cast<size_t>(request.index)]->identity;
            slot.readers.store(0);
            publishedImported.store(destination);
            publishedIdentities[static_cast<size_t>(request.index)].store(identity);
            importedRevision.fetch_add(1);
        }
        else
        {
            importedNames[static_cast<size_t>(request.index)].clear();
            importedRevision.fetch_add(1);
            importError = libraryFull
                ? "Could not add " + name + ": the custom noise library exceeds 512 MB of decoded audio."
                : "Could not load " + name + ". Choose a supported, readable audio file "
                  "with no more than 128 MB of decoded mono/stereo audio.";
        }
        importPending = !requests.empty();
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
    if (sourceIndex >= importedSourceIndex()
        && sourceIndex < importedSourceIndex() + maximumCustomSources)
    {
        const std::lock_guard<std::mutex> lock(importMutex);
        const int slot = publishedImported.load();
        if (slot < 0)
            return false;
        const auto& source = importedSlots[static_cast<size_t>(slot)].sources[
            static_cast<size_t>(sourceIndex - importedSourceIndex())];
        if (source == nullptr)
            return false;
        destination = source->waveform;
        return true;
    }
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(sources.size()))
        return false;
    destination = sources[static_cast<size_t>(sourceIndex)].waveform;
    return true;
}

float BackgroundNoisePlayer::getPlayheadPosition(int sourceIndex) const noexcept
{
    if (sourceIndex < 0 || sourceIndex >= importedSourceIndex() + maximumCustomSources)
        return -1.0f;
    uint32_t sourceKey = static_cast<uint32_t>(sourceIndex);
    if (sourceIndex >= importedSourceIndex())
    {
        const auto identity = publishedIdentities[static_cast<size_t>(sourceIndex - importedSourceIndex())].load();
        if (identity == 0)
            return -1.0f;
        sourceKey = static_cast<uint32_t>(importedSourceIndex()) + identity;
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
        ? static_cast<int>(std::round(juce::jlimit(0.0f, static_cast<float>(importedSourceIndex() + maximumCustomSources - 1), sourceIndex))) : 0;
    const bool imported = requestedSource >= importedSourceIndex();
    const int requestedSlot = imported ? acquireImported() : -1;
    // This guard releases only an atomic reader pin, never the source itself.
    struct PinRelease
    {
        BackgroundNoisePlayer& owner;
        int slot;
        ~PinRelease() { owner.releaseImported(slot); }
    } pin { *this, requestedSlot };
    const Source* requestedData = imported
        ? (requestedSlot >= 0 ? importedSlots[static_cast<size_t>(requestedSlot)].sources[
            static_cast<size_t>(requestedSource - importedSourceIndex())].get() : nullptr)
        : &sources[static_cast<size_t>(requestedSource)];
    // An unsuccessful bounded acquisition should not interrupt an existing loop.
    if (imported && requestedSlot < 0 && publishedImported.load() >= 0)
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
    const uint32_t sourceKey = activeImportedSlot >= 0
        ? static_cast<uint32_t>(importedSourceIndex()) + activeData->identity
        : static_cast<uint32_t>(activeSource);
    const auto normalizedPosition = static_cast<uint32_t>(
        juce::jlimit(0.0, 1.0, position / activeData->audio.getNumSamples())
        * static_cast<double>(std::numeric_limits<uint32_t>::max()));
    playheadSnapshot.store(gain.getCurrentValue() > 0.0f
        ? ((static_cast<uint64_t>(sourceKey) + 1u) << 32) | normalizedPosition : 0,
        std::memory_order_relaxed);
}
