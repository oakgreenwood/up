#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <memory>

#include "SampleMetadata.h"

class WarpCachePrewarmer;

class PercussionSound : public juce::SynthesiserSound
{
public:
    struct WarpedCache
    {
        double bpm = 0.0;
        double pitchRatio = 1.0;
        double timeRatio = 1.0;
        double sourceSampleRate = 44100.0;
        juce::AudioBuffer<float> buffer;
    };

    PercussionSound(const juce::String& soundName,
                    juce::AudioFormatReader& source,
                    const juce::BigInteger& notes,
                    int midiNoteForNormalPitch,
                    double attackTimeSeconds,
                    double releaseTimeSeconds,
                    double maxSampleLengthSeconds,
                    const juce::String& wavResourceNameForMetadata,
                    const juce::String& wavOriginalFilenameForMetadata,
                    double originalBpmIn);

    // SynthesiserSound overrides
    bool appliesToNote (int midiNoteNumber) override;
    bool appliesToChannel (int midiChannel) override;

    // Audio data access
    const juce::AudioBuffer<float>& getAudioData() const noexcept { return data; }
    double getSourceSampleRate() const noexcept { return sourceSampleRate; }
    int getMidiRootNote() const noexcept { return midiRootNote; }
    bool isOneShot() const noexcept { return !warpEnabled && (metadata == nullptr || !metadata->loop); }
    // Assigned once before playback, when registering the background pitch cache.
    void setOneShotPitchIndex(int index) noexcept { oneShotPitchIndex = index; }
    int getOneShotPitchIndex() const noexcept { return oneShotPitchIndex; }
    // Assigned once while the warp-cache worker builds its immutable inventory.
    void setWarpCacheIndex(int index) noexcept { warpCacheIndex = index; }
    int getWarpCacheIndex() const noexcept { return warpCacheIndex; }
    void setVelocityLayerInfo(int groupIndex,
                              int groupCount,
                              int minVelocity,
                              int maxVelocity) noexcept;
    int getVelocityGroupIndex() const noexcept { return velocityGroupIndex; }
    int getVelocityGroupCount() const noexcept { return velocityGroupCount; }
    int getVelocityMin() const noexcept { return velocityMin; }
    int getVelocityMax() const noexcept { return velocityMax; }

    // Warp info
    bool isWarpEnabled() const noexcept { return warpEnabled; }
    double getOriginalBpm() const noexcept { return originalBpm; }

    static double quantizeWarpBpm(double hostBpm) noexcept;
    static double quantizeWarpPitchRatio(double pitchRatio) noexcept;
    static double warpBaseBpmForHost(double originalBpm, double hostBpm) noexcept;
    static double warpTimeRatioForHost(double originalBpm, double hostBpm) noexcept;

    // Transient metadata (used by PercussionVoice)
    std::unique_ptr<SampleMetadata> metadata = nullptr;

private:
    friend class WarpCachePrewarmer;
    std::unique_ptr<WarpedCache> renderWarpedCache(
        double hostBpm, double pitchRatio,
        const std::function<bool()>& shouldCancel) const;

    juce::String name;

    juce::AudioBuffer<float> data;

    double sourceSampleRate = 48000.0;
    int midiRootNote = 60;
    int oneShotPitchIndex = -1;
    int warpCacheIndex = -1;

    juce::BigInteger midiNotes;

    double attackTime  = 0.001;
    double releaseTime = 0.05;

    double lengthInSeconds = 0.0;
    double attack  = 0.001;
    double release = 0.05;

    // Warp configuration
    bool warpEnabled = false;
    double originalBpm = 153.0;

    // Velocity layer metadata for note-on gain shaping
    int velocityGroupIndex = 1;
    int velocityGroupCount = 1;
    int velocityMin = 1;
    int velocityMax = 127;

};
