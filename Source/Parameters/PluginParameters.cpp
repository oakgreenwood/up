#include "PluginParameters.h"
#include "SampleSpecificRealtimeCache.h"

namespace PluginParameters
{
    const std::array<SampleSpecificParameter, 14> sampleSpecificParameters {{
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
          { cache.setFormantSemitonesForMidiNote(note, value); } },
        { sampleMonoAmountId,
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getMonoAmountForMidiNote(note); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setMonoAmountForMidiNote(note, value); } },
        { samplePanId,
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getPanForMidiNote(note); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setPanForMidiNote(note, value); } },
        { sampleEqFrequencyIds[0],
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getEqFrequencyForMidiNote(note, 0); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setEqFrequencyForMidiNote(note, 0, value); }, sampleEqFrequencyIds[0], "EQ" },
        { sampleEqGainIds[0],
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getEqGainForMidiNote(note, 0); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setEqGainForMidiNote(note, 0, value); }, sampleEqFrequencyIds[0], "EQ" },
        { sampleEqFrequencyIds[1],
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getEqFrequencyForMidiNote(note, 1); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setEqFrequencyForMidiNote(note, 1, value); }, sampleEqFrequencyIds[0], "EQ" },
        { sampleEqGainIds[1],
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getEqGainForMidiNote(note, 1); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setEqGainForMidiNote(note, 1, value); }, sampleEqFrequencyIds[0], "EQ" },
        { sampleEqFrequencyIds[2],
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getEqFrequencyForMidiNote(note, 2); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setEqFrequencyForMidiNote(note, 2, value); }, sampleEqFrequencyIds[0], "EQ" },
        { sampleEqGainIds[2],
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getEqGainForMidiNote(note, 2); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setEqGainForMidiNote(note, 2, value); }, sampleEqFrequencyIds[0], "EQ" },
        { sampleEqFrequencyIds[3],
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getEqFrequencyForMidiNote(note, 3); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setEqFrequencyForMidiNote(note, 3, value); }, sampleEqFrequencyIds[0], "EQ" },
        { sampleEqGainIds[3],
          [] (const SampleSpecificRealtimeCache& cache, int note) noexcept
          { return cache.getEqGainForMidiNote(note, 3); },
          [] (SampleSpecificRealtimeCache& cache, int note, float value) noexcept
          { cache.setEqGainForMidiNote(note, 3, value); }, sampleEqFrequencyIds[0], "EQ" }
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
                                                       samplePunchMaximum,
                                                       samplePunchInterval),
                       samplePunchDefault),
                   std::make_unique<juce::AudioParameterFloat>(
                       samplePitchSemitonesId,
                       "Pitch",
                       juce::NormalisableRange<float>(samplePitchSemitonesMinimum,
                                                       samplePitchSemitonesMaximum,
                                                       semitoneInterval),
                       samplePitchSemitonesDefault),
                   std::make_unique<juce::AudioParameterBool>(
                       samplePitchPreserveLengthId,
                       "Pitch keep length",
                       samplePitchPreserveLengthDefault),
                   std::make_unique<juce::AudioParameterFloat>(
                       sampleGainDbId,
                       "Gain",
                       juce::NormalisableRange<float>(sampleGainDbMinimum,
                                                       sampleGainDbMaximum,
                                                       sampleGainDbInterval),
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
                                                       sampleFormantSemitonesMaximum,
                                                       semitoneInterval),
                       sampleFormantSemitonesDefault,
                       juce::AudioParameterFloatAttributes().withLabel("st")),
                   std::make_unique<juce::AudioParameterFloat>(
                       sampleMonoAmountId,
                       "Mono",
                       juce::NormalisableRange<float>(sampleMonoAmountMinimum,
                                                       sampleMonoAmountMaximum,
                                                       sampleMonoAmountInterval),
                       sampleMonoAmountDefault),
                   std::make_unique<juce::AudioParameterFloat>(
                       samplePanId,
                       "Panorama",
                       juce::NormalisableRange<float>(samplePanMinimum,
                                                       samplePanMaximum,
                                                       samplePanInterval),
                       samplePanDefault),
                   std::make_unique<juce::AudioParameterFloat>(
                       ottAmountId,
                       "OTT",
                       juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
                       ottAmountDefault));

        constexpr const char* names[] { "EQ Low Shelf", "EQ Bell 1", "EQ Bell 2", "EQ High Shelf" };
        for (size_t i = 0; i < sampleEqFrequencyIds.size(); ++i)
        {
            juce::NormalisableRange<float> frequencyRange(20.0f, 20000.0f, 0.0f);
            frequencyRange.setSkewForCentre(1000.0f);
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                sampleEqFrequencyIds[i], juce::String(names[i]) + " Frequency", frequencyRange,
                sampleEqDefaultFrequencies[i], juce::AudioParameterFloatAttributes().withLabel("Hz")));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                sampleEqGainIds[i], juce::String(names[i]) + " Gain",
                juce::NormalisableRange<float>(-15.0f, 15.0f, 0.1f), 0.0f,
                juce::AudioParameterFloatAttributes().withLabel("dB")));
        }
        return layout;
    }
}
