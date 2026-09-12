#include "RealtimeWarpPlayer.h"

#include "PunchEnvelope.h"

#include <climits>
#include <cmath>

bool RealtimeWarpPlayer::prepare(double playbackSampleRate, int channelCount, int maxExpectedBlockSize)
{
    const int channels = juce::jlimit(1, 2, channelCount);
    const auto sampleRate = (size_t) juce::jmax(1.0, playbackSampleRate);

    const bool rebuild = !stretcher
                       || stretcherSampleRate != sampleRate
                       || stretcherChannels != channels;

    if (rebuild)
    {
        const auto options =
            RubberBand::RubberBandStretcher::OptionProcessRealTime
          | RubberBand::RubberBandStretcher::OptionThreadingNever
          | RubberBand::RubberBandStretcher::OptionTransientsCrisp
          | RubberBand::RubberBandStretcher::OptionDetectorPercussive
          | RubberBand::RubberBandStretcher::OptionPitchHighConsistency
          | RubberBand::RubberBandStretcher::OptionWindowShort
          | RubberBand::RubberBandStretcher::OptionChannelsTogether;

        stretcher = std::make_unique<RubberBand::RubberBandStretcher>(
            sampleRate,
            (size_t) channels,
            options);

        stretcherSampleRate = sampleRate;
        stretcherChannels = channels;
    }

    ensureBuffers(channels, juce::jmax(4096, maxExpectedBlockSize));
    return stretcher != nullptr;
}

bool RealtimeWarpPlayer::start(int sourceStartSample,
                               double sourceStartTimeSec,
                               double timeRatio,
                               double activeSourceSampleRate,
                               double playbackSampleRate,
                               int channelCount,
                               double pitchScaleMultiplier)
{
    if (!prepare(playbackSampleRate, channelCount))
        return false;

    stretcher->reset();
    sourcePosition = juce::jmax(0, sourceStartSample);
    ended = false;
    outputTimeSec = juce::jmax(0.0, sourceStartTimeSec);
    setPitchScaleMultiplier(pitchScaleMultiplier);
    setRubberBandRates(timeRatio, activeSourceSampleRate, playbackSampleRate);
    ensureBuffers(stretcherChannels, 4096);
    return true;
}

void RealtimeWarpPlayer::reset()
{
    if (stretcher)
        stretcher->reset();

    sourcePosition = 0;
    ended = false;
    currentTimeRatio = 1.0;
    currentPitchScaleMultiplier = 1.0;
    outputTimeSec = 0.0;
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
                                                       float velocityGain,
                                                       juce::SmoothedValue<float>& sampleGain,
                                                       float punchAmount,
                                                       const SampleMetadata* punchMetadata,
                                                       float sustainAmount,
                                                       float sustainMakeupGain,
                                                       double pitchScaleMultiplier,
                                                       SustainTailShaper& sustainShaper,
                                                       NoteStartDeclicker& declicker)
{
    Result result;

    if (stretcher == nullptr)
        return result;

    const int sourceNumSamples = source.getNumSamples();
    const int sourceNumChans = source.getNumChannels();
    const int outNumChans = outputBuffer.getNumChannels();

    if (sourceNumSamples <= 0 || sourceNumChans <= 0)
    {
        result.finished = true;
        return result;
    }

    const double ratio = PercussionSound::warpTimeRatioForHost(sound.getOriginalBpm(), hostBpm);
    const double oldPitchScaleMultiplier = currentPitchScaleMultiplier;
    setPitchScaleMultiplier(pitchScaleMultiplier);

    if (std::abs(ratio - currentTimeRatio) > 1e-6
        || std::abs(oldPitchScaleMultiplier - currentPitchScaleMultiplier) > 1e-6)
    {
        setRubberBandRates(ratio, activeSourceSampleRate, playbackSampleRate);
    }

    const int channels = juce::jlimit(1, 2, sourceNumChans);
    ensureBuffers(channels, numSamples);

    const bool doSustainShorten = sustainShaper.shouldShape(sustainAmount);
    const bool doPunch = punchAmount > 0.0f;
    const double playbackSr = juce::jmax(1.0, playbackSampleRate);
    const double sourceStepSec = (1.0 / playbackSr) / juce::jmax(1e-9, currentTimeRatio);

    int produced = 0;

    while (produced < numSamples)
    {
        int available = (int) stretcher->available();

        if (available <= 0)
        {
            if (!ended)
            {
                const int remaining = sourceNumSamples - sourcePosition;
                if (remaining <= 0)
                {
                    if (loopWhileHeld)
                    {
                        resetForLoop(activeSourceSampleRate, playbackSampleRate, sustainShaper);
                        continue;
                    }

                    stretcher->process(nullptr, 0, true);
                    ended = true;
                    adsr.noteOff();
                    continue;
                }

                const size_t required = juce::jmax<size_t>(1, stretcher->getSamplesRequired());
                const int requiredInt = (int) juce::jlimit<size_t>(1, (size_t) INT_MAX, required);

                if (inputBuffer.getNumSamples() < requiredInt)
                {
                    inputBuffer.setSize(channels, requiredInt, false, false, true);
                    outputScratch.setSize(channels, juce::jmax(requiredInt, numSamples), false, false, true);
                }

                const bool isLastBlock = (!loopWhileHeld && remaining <= requiredInt);
                const int toFeed = juce::jmin(requiredInt, remaining);

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
                    sourcePosition = juce::jmin(sourcePosition + toFeed, sourceNumSamples);
                    if (isLastBlock)
                    {
                        ended = true;
                        adsr.noteOff();
                    }
                    continue;
                }

                if (loopWhileHeld)
                {
                    resetForLoop(activeSourceSampleRate, playbackSampleRate, sustainShaper);
                    continue;
                }

                stretcher->process(nullptr, 0, true);
                ended = true;
                adsr.noteOff();
                continue;
            }

            for (; produced < numSamples; ++produced)
            {
                adsr.getNextSample();
                if (!adsr.isActive())
                {
                    result.finished = true;
                    return result;
                }
            }

            return result;
        }

        const int toGet = juce::jmin(available, numSamples - produced);

        float* out0 = outputScratch.getWritePointer(0);
        float* out1 = (channels > 1) ? outputScratch.getWritePointer(1) : out0;

        outputPtrs[0] = out0;
        outputPtrs[1] = out1;

        stretcher->retrieve(outputPtrs.data(), (size_t) toGet);

        for (int i = 0; i < toGet; ++i)
        {
            const float env = adsr.getNextSample();
            const float gain = env * velocityGain * sampleGain.getNextValue();

            if (!adsr.isActive())
            {
                result.finished = true;
                return result;
            }

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

        produced += toGet;
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
    if (stretcher != nullptr)
    {
        stretcher->reset();
        setRubberBandRates(currentTimeRatio, activeSourceSampleRate, playbackSampleRate);
    }

    sourcePosition = 0;
    ended = false;
    outputTimeSec = 0.0;
    sustainShaper.resetPosition();
}

void RealtimeWarpPlayer::ensureBuffers(int channelCount, int sampleCount)
{
    const int channels = juce::jlimit(1, 2, channelCount);
    const int samples = juce::jmax(1, sampleCount);

    if (inputBuffer.getNumChannels() != channels || inputBuffer.getNumSamples() < samples)
    {
        inputBuffer.setSize(channels, samples, false, false, true);
        outputScratch.setSize(channels, samples, false, false, true);
    }
}
