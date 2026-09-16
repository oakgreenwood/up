#pragma once

#include <array>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <rubberband/RubberBandStretcher.h>

#include "../PercussionSound.h"
#include "NoteStartDeclicker.h"
#include "SustainTailShaper.h"

class RealtimeWarpPlayer
{
public:
    struct Result
    {
        bool finished = false;
    };

    // Call only during preparation, with rendering stopped. Prepares engines for
    // every source channel count up to channelCount and fixes scratch capacity.
    bool prepare(double playbackSampleRate, int channelCount, int maxExpectedBlockSize = 4096);
    bool start(int sourceStartSample,
               double sourceStartTimeSec,
               double timeRatio,
               double activeSourceSampleRate,
               double playbackSampleRate,
               int channelCount,
               double pitchScaleMultiplier = 1.0);

    void reset();
    bool isReady() const noexcept { return stretcher != nullptr; }
    double getCurrentSourceTimeSec() const noexcept { return outputTimeSec; }

    Result render(juce::AudioBuffer<float>& outputBuffer,
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
                  NoteStartDeclicker& declicker);

private:
    static double makeRubberBandRatio(double musicalTimeRatio,
                                      double activeSourceSampleRate,
                                      double playbackSampleRate) noexcept;
    static double makeRubberBandPitchScale(double activeSourceSampleRate,
                                           double playbackSampleRate,
                                           double pitchScaleMultiplier) noexcept;

    void setRubberBandRates(double timeRatio,
                            double activeSourceSampleRate,
                            double playbackSampleRate);
    void setPitchScaleMultiplier(double pitchScaleMultiplier) noexcept;
    void resetForLoop(double activeSourceSampleRate,
                      double playbackSampleRate,
                      SustainTailShaper& sustainShaper);
    std::array<std::unique_ptr<RubberBand::RubberBandStretcher>, 2> preparedStretchers;
    // Borrows the selected prepared engine; start() never transfers ownership.
    RubberBand::RubberBandStretcher* stretcher = nullptr;
    size_t stretcherSampleRate = 0;
    int stretcherChannels = 0;

    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> outputScratch;
    std::array<const float*, 2> inputPtrs {};
    std::array<float*, 2> outputPtrs {};

    int sourcePosition = 0;
    bool ended = false;
    double currentTimeRatio = 1.0;
    double currentPitchScaleMultiplier = 1.0;
    double outputTimeSec = 0.0;
};
