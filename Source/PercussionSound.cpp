#include "PercussionSound.h"
#include <rubberband/RubberBandStretcher.h>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace
{
    constexpr double warpBpmQuantum = 0.01;
    constexpr double warpPitchSemitoneQuantum = 0.01;
}

PercussionSound::PercussionSound(const juce::String& soundName,
                                   juce::AudioFormatReader& source,
                                   const juce::BigInteger& notes,
                                   int midiNoteForNormalPitch,
                                   double attackTimeSeconds,
                                   double releaseTimeSeconds,
                                   double maxSampleLengthSeconds,
                                   const juce::String& wavResourceNameForMetadata,
                                   const juce::String& wavOriginalFilenameForMetadata,
                                   double originalBpmIn)
    : name(soundName),
      sourceSampleRate(source.sampleRate),
      midiRootNote(midiNoteForNormalPitch),
      midiNotes(notes),
      attackTime(attackTimeSeconds),
      releaseTime(releaseTimeSeconds),
      originalBpm(originalBpmIn)
{
    if (sourceSampleRate <= 0.0)
        sourceSampleRate = 48000.0;

    auto numSamples = static_cast<int>(juce::jmin((juce::int64) (sourceSampleRate * maxSampleLengthSeconds),
                                                  source.lengthInSamples));

    data.setSize((int) source.numChannels, numSamples);
    source.read(&data, 0, numSamples, 0, true, true);

    lengthInSeconds = (double) numSamples / sourceSampleRate;
    attack = attackTimeSeconds;
    release = releaseTimeSeconds;

    // load transient metadata from BinaryData (if present)
    metadata = loadMetadataForResource(wavResourceNameForMetadata,
                                       wavOriginalFilenameForMetadata);
    if (metadata == nullptr)
        metadata = std::make_unique<SampleMetadata>();

    if (metadata->sampleRate <= 0.0)
        metadata->sampleRate = 48000.0;

    if (std::abs(metadata->sampleRate - sourceSampleRate) > 1.0)
    {
        DBG("PercussionSound: metadata sampleRate (" << metadata->sampleRate
            << ") differs from audio sampleRate (" << sourceSampleRate
            << ") for " << wavResourceNameForMetadata
            << ". Warp processing will use audio sample rate.");
    }

    if (!metadata->hasTransients())
        metadata->transients.push_back(0.0);

    // Always trust measured audio duration over JSON.
    metadata->lengthSec = lengthInSeconds;

    warpEnabled = metadata->warp;

    DBG("PercussionSound: metadata prepared for " << wavResourceNameForMetadata
        << " (hasTransientJson=" << (metadata->hasTransientJson ? "true" : "false")
        << ", sampleRate=" << metadata->sampleRate
        << ", lengthSec=" << metadata->lengthSec
        << ", transients=" << metadata->transients.size()
        << ", loop=" << (metadata->loop ? "true" : "false")
        << ", ignoreTransientShaper=" << (metadata->ignoreTransientShaper ? "true" : "false")
        << ", warp=" << (metadata->warp ? "true" : "false") << ")");
}

double PercussionSound::quantizeWarpBpm(double hostBpm) noexcept
{
    const double safeBpm = juce::jmax(1.0, hostBpm);
    return std::round(safeBpm / warpBpmQuantum) * warpBpmQuantum;
}

double PercussionSound::quantizeWarpPitchRatio(double pitchRatio) noexcept
{
    const double safePitchRatio = juce::jmax(1.0e-9, pitchRatio);
    const double semitones = 12.0 * std::log2(safePitchRatio);
    const double quantizedSemitones = std::round(semitones / warpPitchSemitoneQuantum)
                                    * warpPitchSemitoneQuantum;

    if (std::abs(quantizedSemitones) < warpPitchSemitoneQuantum * 0.5)
        return 1.0;

    return std::pow(2.0, quantizedSemitones / 12.0);
}

double PercussionSound::warpBaseBpmForHost(double originalBpm, double hostBpm) noexcept
{
    const double safeOriginalBpm = juce::jmax(1.0, originalBpm);
    const double safeHostBpm = juce::jmax(1.0, hostBpm);

    // For very slow host tempos, switch to half-time anchor (153 -> 76.5)
    // so loops speed up instead of stretching too far.
    return (safeHostBpm <= 90.0) ? (safeOriginalBpm * 0.5) : safeOriginalBpm;
}

double PercussionSound::warpTimeRatioForHost(double originalBpm, double hostBpm) noexcept
{
    const double safeHostBpm = juce::jmax(1.0, hostBpm);
    return warpBaseBpmForHost(originalBpm, safeHostBpm) / safeHostBpm;
}

void PercussionSound::setVelocityLayerInfo(int groupIndex,
                                            int groupCount,
                                            int minVelocity,
                                            int maxVelocity) noexcept
{
    velocityGroupCount = juce::jmax(1, groupCount);
    velocityGroupIndex = juce::jlimit(1, velocityGroupCount, groupIndex);
    velocityMin = juce::jlimit(1, 127, minVelocity);
    velocityMax = juce::jlimit(1, 127, maxVelocity);
    if (velocityMax < velocityMin)
        velocityMax = velocityMin;
}

std::unique_ptr<PercussionSound::WarpedCache> PercussionSound::renderWarpedCache(double hostBpm,
                                                                                 double pitchRatio,
                                                                                 const std::function<bool()>& shouldCancel) const
{
    if (metadata == nullptr || shouldCancel())
        return nullptr;

    const int srcChannels = juce::jlimit(1, 2, data.getNumChannels());
    const int srcSamples  = data.getNumSamples();

    if (srcSamples <= 0)
        return nullptr;

    auto cache = std::make_unique<WarpedCache>();
    cache->bpm = hostBpm;
    cache->pitchRatio = quantizeWarpPitchRatio(pitchRatio);
    // Warp cache must always use actual audio sample rate, not JSON metadata sampleRate.
    // JSON sampleRate can differ from the loaded WAV rate and would bias pitch.
    cache->sourceSampleRate = juce::jmax(1.0, sourceSampleRate);

    const double timeRatio = warpTimeRatioForHost(originalBpm, hostBpm);
    cache->timeRatio = timeRatio;
    const double outputLength = std::round((double) srcSamples * timeRatio);
    if (!std::isfinite(outputLength) || outputLength < 1.0
        || outputLength > (double) std::numeric_limits<int>::max())
        return nullptr;
    const int targetOutputSamples = (int) outputLength;

    const auto opts =
        RubberBand::RubberBandStretcher::OptionProcessOffline
      | RubberBand::RubberBandStretcher::OptionThreadingNever
      | RubberBand::RubberBandStretcher::OptionTransientsCrisp
      | RubberBand::RubberBandStretcher::OptionDetectorPercussive
      | RubberBand::RubberBandStretcher::OptionWindowShort
      | RubberBand::RubberBandStretcher::OptionChannelsTogether;

    RubberBand::RubberBandStretcher stretcher(
        (size_t) cache->sourceSampleRate,
        (size_t) srcChannels,
        opts,
        timeRatio,
        cache->pitchRatio);

    // RubberBand's offline pitch shift stretches to timeRatio * pitchRatio,
    // then resamples back to timeRatio. Key-frame targets must live in that
    // pre-resample stretch domain or pitched loop transients drift in tempo.
    const double keyFrameTargetRatio = timeRatio * cache->pitchRatio;

    // Key-frame map from transient list (source frame -> pre-resample stretched frame)
    std::map<size_t, size_t> keyFrames;
    keyFrames[0] = 0;

    const double sr = cache->sourceSampleRate;
    for (double t : metadata->transients)
    {
        if (!std::isfinite(t) || t <= 0.0 || t >= (double) srcSamples / sr)
            continue;

        const size_t srcFrame = (size_t) std::floor(t * sr);
        const size_t dstFrame = (size_t) std::llround((double) srcFrame * keyFrameTargetRatio);
        if (srcFrame > 0 && srcFrame < (size_t) srcSamples)
            keyFrames[srcFrame] = dstFrame;
    }

    const size_t lastSrc = (size_t) srcSamples;
    keyFrames[lastSrc] = (size_t) std::llround((double) lastSrc * keyFrameTargetRatio);

    stretcher.setTimeRatio(timeRatio);
    stretcher.setPitchScale(cache->pitchRatio);
    stretcher.setExpectedInputDuration((size_t) srcSamples);
    stretcher.setKeyFrameMap(keyFrames);

    constexpr int chunkSize = 1024;
    stretcher.setMaxProcessSize(chunkSize);
    std::array<const float*, 2> inPtrs {};
    for (int offset = 0; offset < srcSamples; offset += chunkSize)
    {
        if (shouldCancel())
            return nullptr;
        const int count = juce::jmin(chunkSize, srcSamples - offset);
        inPtrs[0] = data.getReadPointer(0, offset);
        inPtrs[1] = (srcChannels > 1) ? data.getReadPointer(1, offset) : inPtrs[0];
        stretcher.study(inPtrs.data(), (size_t) count, offset + count == srcSamples);
    }
    for (int offset = 0; offset < srcSamples; offset += chunkSize)
    {
        if (shouldCancel())
            return nullptr;
        const int count = juce::jmin(chunkSize, srcSamples - offset);
        inPtrs[0] = data.getReadPointer(0, offset);
        inPtrs[1] = (srcChannels > 1) ? data.getReadPointer(1, offset) : inPtrs[0];
        stretcher.process(inPtrs.data(), (size_t) count, offset + count == srcSamples);
    }

    if (shouldCancel())
        return nullptr;

    const size_t pad = (size_t)(stretcher.getStartDelay() + stretcher.getLatency() + 128);
    size_t estimatedOut = (size_t) targetOutputSamples + pad;
    estimatedOut = juce::jmax<size_t>(estimatedOut, (size_t) srcSamples);
    if (estimatedOut > (size_t) std::numeric_limits<int>::max())
        return nullptr;

    cache->buffer.setSize(srcChannels, (int) estimatedOut, false, true, true);

    std::array<float*, 2> outPtrs {};
    size_t written = 0;
    int safety = 0;

    while (safety < 4096)
    {
        if (shouldCancel())
            return nullptr;
        const int availableFrames = stretcher.available();
        if (availableFrames <= 0)
            break;

        const size_t available = (size_t) availableFrames;
        const size_t needed = written + available;
        if (needed > (size_t) std::numeric_limits<int>::max())
            return nullptr;
        if ((size_t) cache->buffer.getNumSamples() < needed)
            cache->buffer.setSize(srcChannels, (int) needed, true, true, true);

        outPtrs[0] = cache->buffer.getWritePointer(0, (int) written);
        outPtrs[1] = (srcChannels > 1) ? cache->buffer.getWritePointer(1, (int) written)
                                       : outPtrs[0];

        const size_t got = stretcher.retrieve(outPtrs.data(), available);
        written += got;

        if (got == 0)
            break;

        ++safety;
    }

    if (written < (size_t) targetOutputSamples)
    {
        const int clearStart = (int) written;
        cache->buffer.clear(clearStart, targetOutputSamples - clearStart);
    }

    cache->buffer.setSize(srcChannels, targetOutputSamples, true, true, true);
    return cache;
}

bool PercussionSound::appliesToNote(int midiNoteNumber)
{
    return midiNotes[midiNoteNumber];
}

bool PercussionSound::appliesToChannel(int midiChannel)
{
    juce::ignoreUnused(midiChannel);
    return true; // applies to all channels
}
