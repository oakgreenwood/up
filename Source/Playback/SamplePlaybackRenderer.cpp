#include "SamplePlaybackRenderer.h"

#include "PunchEnvelope.h"

#include <cmath>

SamplePlaybackRenderer::Result SamplePlaybackRenderer::render(juce::AudioBuffer<float>& outputBuffer,
                                                              int startSample,
                                                              int numSamples,
                                                              const juce::AudioBuffer<float>& source,
                                                              State& state,
                                                              double playbackSampleRate,
                                                              bool loopWhileHeld,
                                                              juce::ADSR& adsr,
                                                              float velocityGain,
                                                              juce::SmoothedValue<float>& sampleGain,
                                                              float punchAmount,
                                                              const SampleMetadata* punchMetadata,
                                                              float sustainAmount,
                                                              float sustainMakeupGain,
                                                              SustainTailShaper& sustainShaper,
                                                              NoteStartDeclicker& declicker)
{
    Result result;

    if (numSamples <= 0)
        return result;

    const int sourceNumSamples = source.getNumSamples();
    const int sourceNumChans = source.getNumChannels();
    const int outNumChans = outputBuffer.getNumChannels();

    if (sourceNumSamples <= 0 || sourceNumChans <= 0
        || !std::isfinite(state.sourceSamplePosition) || state.sourceSamplePosition < 0.0
        || !std::isfinite(state.pitchRatio) || state.pitchRatio <= 0.0
        || !std::isfinite(state.activeSourceSampleRate) || state.activeSourceSampleRate <= 0.0
        || !std::isfinite(playbackSampleRate) || playbackSampleRate <= 0.0)
    {
        result.finished = true;
        return result;
    }

    const float* srcL = source.getReadPointer(0);
    const float* srcR = (sourceNumChans > 1) ? source.getReadPointer(1) : nullptr;

    const bool doSustainShorten = sustainShaper.shouldShape(sustainAmount);
    const bool doPunch = punchAmount > 0.0f;
    const double activeSourceSampleRate = juce::jmax(1e-9, state.activeSourceSampleRate);
    const double outputSampleRate = juce::jmax(1.0, playbackSampleRate);
    const double sourceFramesPerOutputSample = juce::jmax(1e-9, state.pitchRatio);
    const double unwarpedPunchTimeRatio = activeSourceSampleRate
                                        / (sourceFramesPerOutputSample * outputSampleRate);
    const double punchTimeRatio = state.usingWarpCache ? state.currentTimeRatio
                                                     : unwarpedPunchTimeRatio;

    const auto getPunchPlaybackTimeSec = [&state, activeSourceSampleRate, sourceFramesPerOutputSample, outputSampleRate]() noexcept
    {
        if (state.usingWarpCache)
            return state.sourceSamplePosition / activeSourceSampleRate;

        return state.sourceSamplePosition / (sourceFramesPerOutputSample * outputSampleRate);
    };

    const auto getOriginalSourceTimeSec = [&state]() noexcept
    {
        const double playbackTimeSec = state.sourceSamplePosition
                                     / juce::jmax(1e-9, state.activeSourceSampleRate);
        return playbackTimeSec
             / juce::jmax(1e-9, (state.usingWarpCache ? state.currentTimeRatio : 1.0));
    };

    auto handleSourceEnd = [&]() -> bool
    {
        if (state.sourceSamplePosition < (double) sourceNumSamples)
            return false;

        if (loopWhileHeld)
        {
            state.sourceSamplePosition = std::fmod(state.sourceSamplePosition, (double) sourceNumSamples);
            sustainShaper.resetPosition();
            return false;
        }

        // There is no source left to release. Previously the exhausted branch
        // stopped advancing ADSR, leaving one-shot voices (and caches) held forever.
        result.finished = true;
        return true;
    };

    float* out0 = (outNumChans > 0) ? outputBuffer.getWritePointer(0, startSample) : nullptr;
    float* out1 = (outNumChans > 1) ? outputBuffer.getWritePointer(1, startSample) : nullptr;

    if (outNumChans >= 2 && out0 != nullptr && out1 != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (handleSourceEnd())
                break;

            const int pos = (int) state.sourceSamplePosition;
            const int next = juce::jmin(pos + 1, sourceNumSamples - 1);

            const float frac = (float) (state.sourceSamplePosition - (double) pos);

            const float s1L = srcL[pos];
            const float s2L = srcL[next];
            float inL = s1L + frac * (s2L - s1L);

            float inR = inL;
            if (srcR != nullptr)
            {
                const float s1R = srcR[pos];
                const float s2R = srcR[next];
                inR = s1R + frac * (s2R - s1R);
            }

            const float env = adsr.getNextSample();
            const float gain = env * velocityGain * sampleGain.getNextValue();

            float sampleL = inL * gain;
            float sampleR = inR * gain;

            if (doPunch || doSustainShorten)
            {
                const double timeSec = getOriginalSourceTimeSec();

                if (doPunch)
                {
                    const double punchTimeSec = getPunchPlaybackTimeSec();
                    const float punchGain = PunchEnvelope::getGain(punchTimeSec,
                                                                   punchMetadata,
                                                                   punchTimeRatio,
                                                                   punchAmount);
                    sampleL *= punchGain;
                    sampleR *= punchGain;
                }

                if (doSustainShorten)
                {
                    const float sustainGain = sustainShaper.getGain(timeSec, sustainAmount);
                    sampleL *= sustainGain;
                    sampleR *= sustainGain;
                }
            }

            sampleL *= sustainMakeupGain;
            sampleR *= sustainMakeupGain;

            const float declickGain = declicker.getNextGain();
            sampleL *= declickGain;
            sampleR *= declickGain;

            out0[i] += sampleL;
            out1[i] += sampleR;

            for (int ch = 2; ch < outNumChans; ++ch)
            {
                float* out = outputBuffer.getWritePointer(ch, startSample);
                out[i] += 0.5f * (sampleL + sampleR);
            }

            state.sourceSamplePosition += state.pitchRatio;
        }

        result.finished = result.finished || (!loopWhileHeld && state.sourceSamplePosition >= sourceNumSamples)
                                          || !adsr.isActive();
        return result;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (handleSourceEnd())
            break;

        const int pos = (int) state.sourceSamplePosition;
        const int next = juce::jmin(pos + 1, sourceNumSamples - 1);

        const float frac = (float) (state.sourceSamplePosition - (double) pos);

        const float s1L = srcL[pos];
        const float s2L = srcL[next];
        float inL = s1L + frac * (s2L - s1L);

        float inR = inL;
        if (srcR != nullptr)
        {
            const float s1R = srcR[pos];
            const float s2R = srcR[next];
            inR = s1R + frac * (s2R - s1R);
        }

        const float env = adsr.getNextSample();
        const float gain = env * velocityGain * sampleGain.getNextValue();

        float sampleL = inL * gain;
        float sampleR = inR * gain;

        if (doPunch || doSustainShorten)
        {
            const double timeSec = getOriginalSourceTimeSec();

            if (doPunch)
            {
                const double punchTimeSec = getPunchPlaybackTimeSec();
                const float punchGain = PunchEnvelope::getGain(punchTimeSec,
                                                               punchMetadata,
                                                               punchTimeRatio,
                                                               punchAmount);
                sampleL *= punchGain;
                sampleR *= punchGain;
            }

            if (doSustainShorten)
            {
                const float sustainGain = sustainShaper.getGain(timeSec, sustainAmount);
                sampleL *= sustainGain;
                sampleR *= sustainGain;
            }
        }

        sampleL *= sustainMakeupGain;
        sampleR *= sustainMakeupGain;

        const float declickGain = declicker.getNextGain();
        sampleL *= declickGain;
        sampleR *= declickGain;

        for (int ch = 0; ch < outNumChans; ++ch)
        {
            const float value = (ch == 0 ? sampleL
                                         : (ch == 1 ? sampleR
                                                    : 0.5f * (sampleL + sampleR)));
            outputBuffer.addSample(ch, startSample + i, value);
        }

        state.sourceSamplePosition += state.pitchRatio;
    }

    result.finished = result.finished || (!loopWhileHeld && state.sourceSamplePosition >= sourceNumSamples)
                                      || !adsr.isActive();
    return result;
}
