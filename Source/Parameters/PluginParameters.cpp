#include "PluginParameters.h"
#include "SampleSpecificRealtimeCache.h"

namespace PluginParameters
{
    const std::array<SampleSpecificParameter, 4> sampleSpecificParameters {{
        { samplePunchId,
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getPunchAmountForMidiNote(note); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setPunchAmountForMidiNote(note, value); } },
        { samplePitchSemitonesId,
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getPitchSemitonesForMidiNote(note); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          {
              cache.setPitchSemitonesForMidiNote(note, juce::jlimit(
                  samplePitchSemitonesMinimum, samplePitchSemitonesMaximum, value));
          } },
        { sampleGainDbId,
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getGainDbForMidiNote(note); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setGainDbForMidiNote(note, value); } },
        { sampleFormantSemitonesId,
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getFormantSemitonesForMidiNote(note); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setFormantSemitonesForMidiNote(note, value); } }
    }};

    const SampleSpecificParameter* findSampleSpecificParameter(const juce::String& parameterId) noexcept
    {
        for (const auto& parameter : sampleSpecificParameters)
            if (parameterId == parameter.id)
                return &parameter;

        return nullptr;
    }

    bool isSampleSpecificParameterId(const juce::String& parameterId) noexcept
    {
        return findSampleSpecificParameter(parameterId) != nullptr;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add(std::make_unique<juce::AudioParameterFloat>(
                       rzhavchinaId,
                       "Rzhavchina",
                       juce::NormalisableRange<float>(0.0f, 1.0f),
                       0.0f),
                   std::make_unique<juce::AudioParameterFloat>(
                       sustainShortenId,
                       "Pomyatost",
                       juce::NormalisableRange<float>(0.0f, 1.0f),
                       0.0f),
                   std::make_unique<juce::AudioParameterBool>(
                       warpEnabledId,
                       "Tempo sync",
                       true),
                   std::make_unique<juce::AudioParameterFloat>(
                       samplePunchId,
                       "Punch",
                       juce::NormalisableRange<float>(samplePunchMinimum,
                                                       samplePunchMaximum),
                       samplePunchDefault),
                   std::make_unique<juce::AudioParameterFloat>(
                       samplePitchSemitonesId,
                       "Pitch",
                       juce::NormalisableRange<float>(samplePitchSemitonesMinimum,
                                                       samplePitchSemitonesMaximum),
                       samplePitchSemitonesDefault),
                   std::make_unique<juce::AudioParameterBool>(
                       samplePitchPreserveLengthId,
                       "Pitch keep length",
                       samplePitchPreserveLengthDefault),
                   std::make_unique<juce::AudioParameterFloat>(
                       sampleGainDbId,
                       "Gain",
                       juce::NormalisableRange<float>(sampleGainDbMinimum, sampleGainDbMaximum),
                       sampleGainDbDefault,
                       juce::AudioParameterFloatAttributes().withLabel("dB")),
                   std::make_unique<juce::AudioParameterFloat>(
                       legacyLpcFormantSemitonesId,
                       "Unused (legacy)",
                       juce::NormalisableRange<float>(sampleFormantSemitonesMinimum,
                                                       sampleFormantSemitonesMaximum),
                       sampleFormantSemitonesDefault,
                       juce::AudioParameterFloatAttributes().withLabel("st")),
                   std::make_unique<juce::AudioParameterFloat>(
                       sampleFormantSemitonesId,
                       "Formant",
                       juce::NormalisableRange<float>(sampleFormantSemitonesMinimum,
                                                       sampleFormantSemitonesMaximum),
                       sampleFormantSemitonesDefault,
                       juce::AudioParameterFloatAttributes().withLabel("st")));

        return layout;
    }
}
