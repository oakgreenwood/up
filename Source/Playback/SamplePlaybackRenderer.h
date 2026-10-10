#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "NoteStartDeclicker.h"
#include "NoiseTrigger.h"
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
        int regionStart = 0;
        int regionEnd = 0;
        int nextRegionStart = 0;
        int nextRegionEnd = 0;
        double attackSeconds = 0.0;
        double releaseSeconds = 0.0;
    };

    struct Result
    {
        bool finished = false;
        bool wrapped = false;
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
                  NoteStartDeclicker& declicker,
                  NoiseTriggerTracker& noiseTrigger,
                  float* noiseHits);
};
