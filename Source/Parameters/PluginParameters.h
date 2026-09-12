#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

class SampleSpecificRealtimeCache;

namespace PluginParameters
{
    inline constexpr const char* stateType = "params";

    inline constexpr const char* rzhavchinaId = "rzhavchina";
    inline constexpr const char* sustainShortenId = "sustainShorten";
    inline constexpr const char* warpEnabledId = "warpEnabled";
    inline constexpr const char* sampleGainDbId = "sampleGainDb";
    inline constexpr float sampleGainDbMinimum = -10.0f;
    inline constexpr float sampleGainDbMaximum = 10.0f;
    inline constexpr float sampleGainDbDefault = 0.0f;

    inline constexpr const char* samplePunchId = "samplePunch";
    inline constexpr const char* samplePitchSemitonesId = "samplePitchSemitones";
    inline constexpr const char* samplePitchPreserveLengthId = "samplePitchPreserveLength";
    inline constexpr bool samplePitchPreserveLengthDefault = false;

    inline constexpr float samplePunchMinimum = 0.0f;
    inline constexpr float samplePunchMaximum = 1.0f;
    inline constexpr float samplePunchDefault = 0.0f;

    inline constexpr float samplePitchSemitonesMinimum = -8.0f;
    inline constexpr float samplePitchSemitonesMaximum = 8.0f;
    inline constexpr float samplePitchSemitonesDefault = 0.0f;

    struct SampleSpecificParameter
    {
        const char* id;
        float (*readCache)(const SampleSpecificRealtimeCache&, int midiNote) noexcept;
        void (*writeCache)(SampleSpecificRealtimeCache&, int midiNote, float value) noexcept;
    };

    // Register each sample-specific effect once. These callbacks must be realtime-safe.
    // The registry drives automation, selection, persistence, and Apply to All.
    extern const std::array<SampleSpecificParameter, 4> sampleSpecificParameters;
    const SampleSpecificParameter* findSampleSpecificParameter(const juce::String& parameterId) noexcept;
    bool isSampleSpecificParameterId(const juce::String& parameterId) noexcept;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
