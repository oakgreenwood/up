#include "RealtimeWarpPlayer.h"

#include "PunchEnvelope.h"
#include "SampleEnvelope.h"

#include <cmath>
#include <limits>

bool RealtimeWarpPlayer::prepare(double playbackSampleRate, int channelCount, int maxExpectedBlockSize)
{
    stretcher = nullptr;
    stretcherChannels = 0;
    if (!std::isfinite(playbackSampleRate) || playbackSampleRate < 1.0
        || playbackSampleRate >= static_cast<double>(std::numeric_limits<size_t>::max()))
    {
        stretcherSampleRate = 0;
        return false;
    }

    const int channels = juce::jlimit(1, 2, channelCount);
    const auto sampleRate = static_cast<size_t>(playbackSampleRate);

    for (size_t i = 0; i < preparedStretchers.size(); ++i)
    {
        auto& prepared = preparedStretchers[i];
        if (i >= static_cast<size_t>(channels))
        {
            prepared.reset();
            continue;
        }
        if (prepared != nullptr && stretcherSampleRate == sampleRate)
            continue;

        const auto options =
            RubberBand::RubberBandStretcher::OptionProcessRealTime
          | RubberBand::RubberBandStretcher::OptionThreadingNever
          | RubberBand::RubberBandStretcher::OptionTransientsCrisp
          | RubberBand::RubberBandStretcher::OptionDetectorPercussive
          | RubberBand::RubberBandStretcher::OptionPitchHighConsistency
          | RubberBand::RubberBandStretcher::OptionWindowShort
          | RubberBand::RubberBandStretcher::OptionChannelsTogether;

        prepared = std::make_unique<RubberBand::RubberBandStretcher>(
            sampleRate,
            i + 1,
            options);
    }

    stretcherSampleRate = sampleRate;
    const int capacity = juce::jmax(4096, maxExpectedBlockSize);
    inputBuffer.setSize(channels, capacity, false, false, true);
    outputScratch.setSize(channels, capacity, false, false, true);
    return true;
}

bool RealtimeWarpPlayer::start(int sourceStartSample,
                               double sourceStartTimeSec,
                               int regionStartSample,
                               int regionEndSample,
                               double timeRatio,
                               double activeSourceSampleRate,
                               double playbackSampleRate,
                               int channelCount,
                               double pitchScaleMultiplier)
{
    const int channels = juce::jlimit(1, 2, channelCount);
    auto* prepared = preparedStretchers[static_cast<size_t>(channels - 1)].get();
    if (prepared == nullptr || !std::isfinite(playbackSampleRate)
        || playbackSampleRate < 1.0
        || std::floor(playbackSampleRate) != static_cast<double>(stretcherSampleRate)
        || inputBuffer.getNumChannels() < channels || outputScratch.getNumChannels() < channels
        || inputBuffer.getNumSamples() <= 0 || outputScratch.getNumSamples() <= 0
        || regionStartSample < 0 || regionEndSample <= regionStartSample)
        return false;

    stretcher = prepared;
    stretcherChannels = channels;
    stretcher->reset();
    regionStart = regionStartSample;
    regionEnd = regionEndSample;
    nextRegionStart = regionStart;
    nextRegionEnd = regionEnd;
    sourcePosition = juce::jlimit(regionStart, regionEnd - 1, sourceStartSample);
    ended = false;
    outputTimeSec = juce::jmax(0.0, sourceStartTimeSec);
    setPitchScaleMultiplier(pitchScaleMultiplier);
    setRubberBandRates(timeRatio, activeSourceSampleRate, playbackSampleRate);
    return true;
}

void RealtimeWarpPlayer::reset()
{
    if (stretcher)
        stretcher->reset();

    sourcePosition = 0;
    regionStart = 0;
    regionEnd = 0;
    nextRegionStart = 0;
    nextRegionEnd = 0;
    ended = false;
    currentTimeRatio = 1.0;
    currentPitchScaleMultiplier = 1.0;
    outputTimeSec = 0.0;
}

void RealtimeWarpPlayer::setLoopTrim(int nextStart, int nextEnd, int currentEnd) noexcept
{
    nextRegionStart = nextStart;
    nextRegionEnd = juce::jmax(nextStart + 1, nextEnd);
    regionEnd = juce::jmax(regionStart + 1, currentEnd);
}

RealtimeWarpPlayer::Result RealtimeWarpPlayer::render(juce::AudioBuffer<float>& outputBuffer,
                                                       int startSample,
                                                       int numSamples,
                                                       const PercussionSound& sound,
                                                       const juce::AudioBuffer<float>& source,
                                                       double activeSourceSampleRate,
                                                       double playbackSampleRate,
                                                       double hostBpm,
                                                       bool loopWhileHeld,
                                                       juce::ADSR& adsr,
                                                       double attackSeconds,
                                                       double releaseSeconds,
                                                       float velocityGain,
                                                       juce::SmoothedValue<float>& sampleGain,
                                                       float punchAmount,
                                                       const SampleMetadata* punchMetadata,
                                                       float sustainAmount,
                                                       float sustainMakeupGain,
                                                       double pitchScaleMultiplier,
                                                       SustainTailShaper& sustainShaper,
                                                       NoteStartDeclicker& declicker,
                                                       NoiseTriggerTracker& noiseTrigger,
                                                       float* noiseHits)
{
    Result result;

    if (stretcher == nullptr || numSamples <= 0)
        return result;

    const int sourceNumSamples = source.getNumSamples();
    const int sourceNumChans = source.getNumChannels();
    const int outNumChans = outputBuffer.getNumChannels();

    if (sourceNumSamples <= 0 || sourceNumChans <= 0)
    {
        result.finished = true;
        return result;
    }

    int boundedEnd = juce::jlimit(1, sourceNumSamples, regionEnd);
    if (regionStart < 0 || regionStart >= boundedEnd || sourcePosition < regionStart)
    {
        result.finished = true;
        return result;
    }

    const int channels = juce::jlimit(1, 2, sourceNumChans);
    if (channels != stretcherChannels)
    {
        result.finished = true;
        return result;
    }

    const double ratio = sound.warpTimeRatioForTarget(hostBpm);
    const double oldPitchScaleMultiplier = currentPitchScaleMultiplier;
    setPitchScaleMultiplier(pitchScaleMultiplier);

    if (std::abs(ratio - currentTimeRatio) > 1e-6
        || std::abs(oldPitchScaleMultiplier - currentPitchScaleMultiplier) > 1e-6)
    {
        setRubberBandRates(ratio, activeSourceSampleRate, playbackSampleRate);
    }

    const bool doSustainShorten = sustainShaper.shouldShape(sustainAmount);
    const bool doPunch = punchAmount > 0.0f;
    const double playbackSr = juce::jmax(1.0, playbackSampleRate);
    const double sourceStepSec = (1.0 / playbackSr) / juce::jmax(1e-9, currentTimeRatio);
    bool trimmed = regionStart > 0 || boundedEnd < sourceNumSamples;
    const double safeSourceRate = juce::jmax(1.0, activeSourceSampleRate);
    double regionStartSec = regionStart / safeSourceRate;
    double regionEndSec = boundedEnd / safeSourceRate;
    const double halfSourceFrameSec = 0.5 / safeSourceRate;
    double trimFadeSec = juce::jmin(0.002, 0.5 * (regionEndSec - regionStartSec));
    auto envelope = SampleEnvelope::resolve(regionEndSec - regionStartSec,
                                            attackSeconds, releaseSeconds);
    const auto refreshLoopBounds = [&]() noexcept
    {
        boundedEnd = juce::jlimit(1, sourceNumSamples, regionEnd);
        trimmed = regionStart > 0 || boundedEnd < sourceNumSamples;
        regionStartSec = regionStart / safeSourceRate;
        regionEndSec = boundedEnd / safeSourceRate;
        trimFadeSec = juce::jmin(0.002, 0.5 * (regionEndSec - regionStartSec));
        envelope = SampleEnvelope::resolve(regionEndSec - regionStartSec,
                                           attackSeconds, releaseSeconds);
    };
    const auto getTrimGain = [&]() noexcept
    {
        const float envelopeGain = envelope.gain(outputTimeSec - regionStartSec + halfSourceFrameSec);
        if (!trimmed || trimFadeSec <= 0.0)
            return envelopeGain;
        const double fromStart = (outputTimeSec - regionStartSec + halfSourceFrameSec) / trimFadeSec;
        const double toEnd = (regionEndSec - outputTimeSec - halfSourceFrameSec) / trimFadeSec;
        return envelopeGain * static_cast<float>(juce::jlimit(0.0, 1.0, juce::jmin(fromStart, toEnd)));
    };

    int produced = 0;
    int sourceFeeds = 0;

    while (produced < numSamples)
    {
        int available = (int) stretcher->available();

        if (available <= 0)
        {
            if (!ended)
            {
                // A very short trimmed loop may need many source wraps before
                // Rubber Band yields output. Continue across callbacks.
                if (++sourceFeeds > 8)
                    return result;
                const int remaining = boundedEnd - sourcePosition;
                if (remaining <= 0)
                {
                    if (loopWhileHeld)
                    {
                        resetForLoop(activeSourceSampleRate, playbackSampleRate, sustainShaper);
                        refreshLoopBounds();
                        result.wrapped = true;
                        noiseTrigger.restartLoop();
                        continue;
                    }

                    stretcher->process(nullptr, 0, true);
                    ended = true;
                    continue;
                }

                const size_t required = juce::jmax<size_t>(1, stretcher->getSamplesRequired());
                // Rubber Band's demand may exceed scratch capacity. Feed it in
                // pieces, and only mark the actual last source piece as final.
                const int toFeed = static_cast<int>(juce::jmin(required,
                    static_cast<size_t>(remaining), static_cast<size_t>(inputBuffer.getNumSamples())));
                const bool isLastBlock = !loopWhileHeld && toFeed == remaining;

                if (toFeed > 0)
                {
                    inputBuffer.copyFrom(0, 0, source, 0, sourcePosition, toFeed);
                    if (channels > 1)
                        inputBuffer.copyFrom(1, 0, source, 1, sourcePosition, toFeed);

                    const float* input0 = inputBuffer.getReadPointer(0);
                    const float* input1 = (channels > 1) ? inputBuffer.getReadPointer(1) : input0;
                    inputPtrs[0] = input0;
                    inputPtrs[1] = input1;

                    stretcher->process(inputPtrs.data(), (size_t) toFeed, isLastBlock);
                    sourcePosition = juce::jmin(sourcePosition + toFeed, boundedEnd);
                    if (isLastBlock)
                        ended = true;
                    continue;
                }

                if (loopWhileHeld)
                {
                    resetForLoop(activeSourceSampleRate, playbackSampleRate, sustainShaper);
                    refreshLoopBounds();
                    result.wrapped = true;
                    noiseTrigger.restartLoop();
                    continue;
                }

                stretcher->process(nullptr, 0, true);
                ended = true;
                continue;
            }

            // Final input is drained; the source-time envelope has reached its end.
            result.finished = true;
            return result;
        }

        const int toGet = juce::jmin(available, numSamples - produced, outputScratch.getNumSamples());

        float* out0 = outputScratch.getWritePointer(0);
        float* out1 = (channels > 1) ? outputScratch.getWritePointer(1) : out0;

        outputPtrs[0] = out0;
        outputPtrs[1] = out1;

        const int retrieved = static_cast<int>(stretcher->retrieve(outputPtrs.data(), (size_t) toGet));
        if (retrieved <= 0)
            return result;

        for (int i = 0; i < retrieved; ++i)
        {
            const float env = adsr.getNextSample();
            const float gain = env * velocityGain * sampleGain.getNextValue() * getTrimGain();

            if (!adsr.isActive())
            {
                result.finished = true;
                return result;
            }

            noiseHits[produced + i] = noiseTrigger.advance(outputTimeSec, gain);

            const float inL = outputScratch.getSample(0, i);
            const float inR = (outNumChans > 1) ? outputScratch.getSample(juce::jmin(1, channels - 1), i) : inL;

            float sampleL = inL * gain;
            float sampleR = inR * gain;

            if (doPunch)
            {
                const double punchTimeSec = outputTimeSec * juce::jmax(1.0e-9, currentTimeRatio);
                const float punchGain = PunchEnvelope::getGain(punchTimeSec,
                                                               punchMetadata,
                                                               currentTimeRatio,
                                                               punchAmount);
                sampleL *= punchGain;
                sampleR *= punchGain;
            }

            if (doSustainShorten)
            {
                const float sustainGain = sustainShaper.getGain(outputTimeSec, sustainAmount);
                sampleL *= sustainGain;
                sampleR *= sustainGain;
            }

            sampleL *= sustainMakeupGain;
            sampleR *= sustainMakeupGain;

            const float declickGain = declicker.getNextGain();
            sampleL *= declickGain;
            sampleR *= declickGain;

            if (outNumChans > 0)
                outputBuffer.addSample(0, startSample + produced + i, sampleL);
            if (outNumChans > 1)
                outputBuffer.addSample(1, startSample + produced + i, sampleR);

            for (int ch = 2; ch < outNumChans; ++ch)
                outputBuffer.addSample(ch, startSample + produced + i, 0.5f * (sampleL + sampleR));

            outputTimeSec += sourceStepSec;
        }

        produced += retrieved;
    }

    return result;
}

double RealtimeWarpPlayer::makeRubberBandRatio(double musicalTimeRatio,
                                               double activeSourceSampleRate,
                                               double playbackSampleRate) noexcept
{
    const double safeMusicalRatio = juce::jmax(1e-9, musicalTimeRatio);
    const double sourceSr = juce::jmax(1.0, activeSourceSampleRate);
    const double playbackSr = juce::jmax(1.0, playbackSampleRate);
    return safeMusicalRatio * (playbackSr / sourceSr);
}

double RealtimeWarpPlayer::makeRubberBandPitchScale(double activeSourceSampleRate,
                                                    double playbackSampleRate,
                                                    double pitchScaleMultiplier) noexcept
{
    const double sourceSr = juce::jmax(1.0, activeSourceSampleRate);
    const double playbackSr = juce::jmax(1.0, playbackSampleRate);
    return (sourceSr / playbackSr) * juce::jmax(1e-9, pitchScaleMultiplier);
}

void RealtimeWarpPlayer::setRubberBandRates(double timeRatio,
                                            double activeSourceSampleRate,
                                            double playbackSampleRate)
{
    if (stretcher == nullptr)
        return;

    currentTimeRatio = timeRatio;
    stretcher->setPitchScale(makeRubberBandPitchScale(activeSourceSampleRate,
                                                      playbackSampleRate,
                                                      currentPitchScaleMultiplier));
    stretcher->setTimeRatio(makeRubberBandRatio(timeRatio, activeSourceSampleRate, playbackSampleRate));
}

void RealtimeWarpPlayer::setPitchScaleMultiplier(double pitchScaleMultiplier) noexcept
{
    currentPitchScaleMultiplier = juce::jmax(1e-9, pitchScaleMultiplier);
}

void RealtimeWarpPlayer::resetForLoop(double activeSourceSampleRate,
                                      double playbackSampleRate,
                                      SustainTailShaper& sustainShaper)
{
    if (stretcher != nullptr && regionEnd - regionStart >= 4096)
    {
        stretcher->reset();
        setRubberBandRates(currentTimeRatio, activeSourceSampleRate, playbackSampleRate);
    }

    regionStart = nextRegionStart;
    regionEnd = nextRegionEnd;
    sourcePosition = regionStart;
    ended = false;
    outputTimeSec = regionStart / juce::jmax(1.0, activeSourceSampleRate);
    sustainShaper.resetPosition();
}
