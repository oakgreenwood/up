#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <functional>
#include <memory>

#include "SampleMetadata.h"
#include "Playback/NoiseTrigger.h"

class WarpCachePrewarmer;

class PercussionSound : public juce::SynthesiserSound
{
public:
    struct Waveform
    {
        static constexpr int bins = 1024;
        std::array<std::array<float, bins>, 2> minimum {};
        std::array<std::array<float, bins>, 2> maximum {};
        int channels = 0;
        int samples = 0;
        double sampleRate = 0.0;
        float peak = 0.0f;
    };

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
    const Waveform& getWaveform() const noexcept { return waveform; }
    double getSourceSampleRate() const noexcept { return sourceSampleRate; }
    int getMidiRootNote() const noexcept { return midiRootNote; }
    const std::vector<NoiseTriggerPoint>& getNoiseTriggerPoints() const noexcept { return noiseTriggerPoints; }
    bool isOneShot() const noexcept { return !warpEnabled && (metadata == nullptr || !metadata->loop); }
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
    void setVariationIndex(int index) noexcept { variationIndex = index; }
    int getVariationIndex() const noexcept { return variationIndex; }

    // Warp info
    bool isWarpEnabled() const noexcept { return warpEnabled; }
    double getOriginalBpm() const noexcept { return originalBpm; }

    static double quantizeWarpBpm(double hostBpm) noexcept;
    static double quantizeWarpPitchRatio(double pitchRatio) noexcept;
    double warpBaseBpmForTarget(double targetBpm) const noexcept;
    double warpTimeRatioForTarget(double targetBpm) const noexcept;

    // Transient metadata (used by PercussionVoice)
    std::unique_ptr<SampleMetadata> metadata = nullptr;

private:
    friend class WarpCachePrewarmer;
    std::unique_ptr<WarpedCache> renderWarpedCache(
        double hostBpm, double pitchRatio,
        const std::function<bool()>& shouldCancel) const;

    juce::String name;

    juce::AudioBuffer<float> data;
    Waveform waveform;
    std::vector<NoiseTriggerPoint> noiseTriggerPoints;

    double sourceSampleRate = 48000.0;
    int midiRootNote = 60;
    int warpCacheIndex = -1;
    int variationIndex = 1;

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
