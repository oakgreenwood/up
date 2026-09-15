#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "PercussionSound.h"
#include "Effects/FormantShifter.h"
#include "Effects/PsolaFormantShifter.h"
#include "Playback/NoteStartDeclicker.h"
#include "Playback/RealtimeWarpPlayer.h"
#include "Playback/SamplePlaybackRenderer.h"
#include "Playback/SustainTailShaper.h"
#include "Parameters/SampleSpecificRealtimeCache.h"
#include "Warp/WarpCachePrewarmer.h"

// Voice that plays PercussionSound and shortens sustain tails
// based on transient JSON metadata.
class PercussionVoice : public juce::SynthesiserVoice
{
public:
    PercussionVoice();

    // Connects the voice to the "sustainShorten" parameter (0..1)
    void setSustainParam(std::atomic<float>* p);

    // Only play PercussionSound instances
    bool canPlaySound(juce::SynthesiserSound* sound) override;

    void startNote(int midiNoteNumber, float velocity,
                   juce::SynthesiserSound* sound,
                   int currentPitchWheelPosition) override;

    void stopNote(float velocity, bool allowTailOff) override;

    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample,
                         int numSamples) override;
    void prepareRealtimeWarpResources(double playbackSampleRate, int samplesPerBlock);
    void setHostBpmParam(std::atomic<double>* p) { hostBpmParam = p; }
    void setWarpEnabledParam(std::atomic<bool>* p) { warpEnabledParam = p; }
    void setHostBpmMovingParam(std::atomic<bool>* p) { hostBpmMovingParam = p; }
    void setSampleSpecificCache(const SampleSpecificRealtimeCache* cache) noexcept { sampleSpecificCache = cache; }
    void setWarpCachePrewarmer(WarpCachePrewarmer* cache) noexcept { warpCachePrewarmer = cache; }

private:
    void renderSourceBlock(juce::AudioBuffer<float>&, int startSample, int numSamples);
    void finishSourcePlayback() noexcept;
    void beginPlayback(float velocity);
    void clearActivePlayback();
    void resetWarpFlags();
    void maybeSwitchToReadyWarpCache(double effectiveSamplePitchRatio);
    void maybeSwitchWarpCacheToRealtime(double effectiveSamplePitchRatio);
    void maybeSwitchLengthPreservedPitchToRealtime(double effectiveSamplePitchRatio);
    bool tryUseWarpCache(double hostBpm,
                         double samplePitchRatio,
                         double sourceTimeSec,
                         bool requestIfMissing,
                         bool triggerDeclick);
    bool switchToWarpCache(WarpCachePrewarmer::Lease cache,
                           double sourceTimeSec,
                           double playbackSampleRate,
                           bool triggerDeclick);
    bool switchToOriginalPlaybackFromSourceTime(double sourceTimeSec,
                                                double playbackSampleRate,
                                                bool triggerDeclick);
    bool switchToRealtimeWarpFromOriginal(int startSourceSample,
                                          double sourceTimeSec,
                                          double timeRatio,
                                          double sourceSampleRate,
                                          double playbackSampleRate,
                                          int channels,
                                          double samplePitchRatio,
                                          bool triggerDeclick);
    double updateWarpLoopPitchDebounce(int numSamples);
    void resetWarpLoopPitchDebounce(double pitchRatio) noexcept;
    void updateSampleRendererPitchRatio() noexcept;
    float getSustainAmount() const noexcept;
    float getCurrentSamplePunchAmount() const noexcept;
    float getCurrentSampleGain() const noexcept;
    float getCurrentSampleMonoAmount() const noexcept;
    float getCurrentSamplePan() const noexcept;
    void applySamplePan(float& left, float& right, bool stereoOutput) noexcept;
    float getCurrentFormantRatio() const noexcept;
    double getCurrentSamplePitchRatio() const noexcept;
    double getCurrentHostBpm() const noexcept;
    double getCurrentOriginalSourceTimeSec() const noexcept;
    bool shouldTimeWarpForCurrentHost() const noexcept;
    bool shouldPreserveLengthForPitch() const noexcept;
    bool shouldDebounceWarpLoopPitch() const noexcept;
    bool shouldUsePitchWarpCache(double samplePitchRatio) const noexcept;
    int getWarpLoopPitchDebounceSampleCount() const noexcept;

    PercussionSound* currentSound = nullptr;
    WarpCachePrewarmer::Lease activeWarpCache;
    const juce::AudioBuffer<float>* activeBuffer = nullptr;
    const SampleMetadata* metadata = nullptr;

    SamplePlaybackRenderer::State playbackState;
    SamplePlaybackRenderer sampleRenderer;
    RealtimeWarpPlayer realtimeWarpPlayer;
    SustainTailShaper sustainShaper;
    NoteStartDeclicker noteStartDeclicker;
    PsolaFormantShifter formantShifter;
    FormantShifter formantColouration;
    int formantDrainSamples = FormantShifter::tailSamples
                              + PsolaFormantShifter::tailForSampleRate(44100.0);
    juce::AudioBuffer<float> voiceScratch { 2, FormantShifter::hopSize };
    bool sourceFinished = false;
    int formantTailRemaining = 0;

    // Simple ADSR for amplitude (attack/release from PercussionSound)
    juce::ADSR adsr;

    // Pointer to APVTS parameter for sustain shortening
    std::atomic<float>* sustainAmountParam = nullptr;
    float velocityGain = 1.0f;
    juce::SmoothedValue<float> sampleGain { 1.0f };
    juce::SmoothedValue<float> sampleMonoAmount { 0.0f };
    juce::SmoothedValue<float> samplePan { 0.0f };
    float lastPanPosition = 0.0f;
    double panNormalization = 1.0;
    
    std::atomic<double>* hostBpmParam = nullptr;
    std::atomic<bool>* warpEnabledParam = nullptr;
    std::atomic<bool>* hostBpmMovingParam = nullptr;
    const SampleSpecificRealtimeCache* sampleSpecificCache = nullptr;
    WarpCachePrewarmer* warpCachePrewarmer = nullptr;
    double notePitchRatio = 1.0;

    double appliedWarpLoopPitchRatio = 1.0;
    double pendingWarpLoopPitchRatio = 1.0;
    int warpLoopPitchDebounceSamplesRemaining = 0;

    bool isWarping = false;
    bool isRealtimeWarping = false;
};
