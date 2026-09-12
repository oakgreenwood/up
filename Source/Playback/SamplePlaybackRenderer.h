#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "NoteStartDeclicker.h"
#include "SustainTailShaper.h"

struct SampleMetadata;

class SamplePlaybackRenderer
{
public:
    struct State
    {
        double sourceSamplePosition = 0.0;
        double pitchRatio = 1.0;
        double activeSourceSampleRate = 48000.0;
        double currentTimeRatio = 1.0;
        bool usingWarpCache = false;
    };

    struct Result
    {
        bool finished = false;
    };

    Result render(juce::AudioBuffer<float>& outputBuffer,
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
                  NoteStartDeclicker& declicker);
};
