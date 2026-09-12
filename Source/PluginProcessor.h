#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include "Effects/RzhavProcessor.h"
#include "Midi/MidiNoteActivityState.h"
#include "PercussionSynthesiser.h"
#include "Parameters/SampleSpecificParameterState.h"
#include "Parameters/SampleSpecificRealtimeCache.h"
#include "SampleLibrary/PercussionSampleLibrary.h"
#include "Tempo/HostTempoTracker.h"
#include "Warp/WarpCachePrewarmer.h"

//==============================================================================
class AudioPluginAudioProcessor final : public juce::AudioProcessor,
                                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int midiNoteActivityCount = MidiNoteActivityState::getMidiNoteCount();

    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* sustainShortenParam = nullptr;
    std::atomic<float>* warpParamRaw = nullptr;
    std::atomic<float>* rzhavParam = nullptr;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    const std::vector<PercussionSampleLibrary::SampleGroupInfo>& getSampleGroups() const noexcept;
    int getSelectedSampleGroupIndex() const noexcept;
    // Message-thread selection; synchronizes the host-visible parameters.
    void setSelectedSampleGroupIndex(int groupIndex);
    float getMidiNoteActivityVelocity(int midiNote) const noexcept;
    uint32_t getMidiNoteActivityGeneration(int midiNote) const noexcept;

    float getSampleSpecificParameterValue(const juce::String& parameterId, float fallbackValue) const;
    void setSampleSpecificParameterValue(const juce::String& parameterId, float value);
    // Message thread only; copies one effect without changing the selected group.
    bool applySampleSpecificParameterToAll(const juce::String& parameterId, float value);

private:
    void addPercussionVoices();
    void updateVoiceSharedState();
    void clampSelectedSampleGroupIndex() noexcept;
    void parameterChanged(const juce::String& parameterId, float value) override;
    void synchroniseSelectedSampleParameters();
    void storeSampleSpecificCache();
    void rebuildSampleSpecificCache();

    //==============================================================================
    std::atomic<bool> warpEnabledAtomic { true };
    HostTempoTracker hostTempo;
    RzhavProcessor rzhavProcessor;
    PercussionSynthesiser sampler;
    MidiNoteActivityState midiNoteActivity;
    std::vector<PercussionSampleLibrary::SampleGroupInfo> sampleGroups;
    std::atomic<int> selectedSampleGroupIndex { -1 };
    SampleSpecificParameterState sampleSpecificParameters;
    SampleSpecificRealtimeCache sampleSpecificCache;
    std::unique_ptr<WarpCachePrewarmer> warpCachePrewarmer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
