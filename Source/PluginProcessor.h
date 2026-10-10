#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include "Effects/RzhavProcessor.h"
#include "Effects/OttProcessor.h"
#include "Effects/SampleSpectrum.h"
#include "Effects/NoiseSidechain.h"
#include "Playback/BackgroundNoisePlayer.h"
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
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;

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
    const PercussionSound::Waveform* getSampleWaveform(int groupIndex) const noexcept;
    // Message-thread view of immutable, processor-owned source metadata.
    const SampleMetadata* getSampleMetadata(int groupIndex) const noexcept;
    float getSamplePlayheadProgress(int groupIndex) const noexcept;
    bool isSampleGroupWarpEnabled(int groupIndex) const noexcept;
    bool isSampleGroupWarpMarked(int groupIndex) const noexcept;
    double getSampleGroupOriginalBpm(int groupIndex) const noexcept;
    double getProjectBpm() const noexcept { return hostTempo.getBpm(); }
    int getSelectedSampleGroupIndex() const noexcept;
    // Message-thread selection; synchronizes the host-visible parameters.
    void setSelectedSampleGroupIndex(int groupIndex);
    float getMidiNoteActivityVelocity(int midiNote) const noexcept;
    uint32_t getMidiNoteActivityGeneration(int midiNote) const noexcept;
    int getLatestMidiNoteOnNote() const noexcept;

    SampleSpectrum& getSampleSpectrum() noexcept { return sampleSpectrum; }
    bool copyNoiseWaveform(int sourceIndex, BackgroundNoisePlayer::Waveform& waveform) const
    { return backgroundNoise.copyWaveform(sourceIndex, waveform); }
    float getNoisePlayheadPosition(int sourceIndex) const noexcept
    { return backgroundNoise.getPlayheadPosition(sourceIndex); }
    void importNoiseFile(const juce::File&);
    juce::StringArray getAvailableNoiseNames() const { return backgroundNoise.getAvailableSourceNames(); }
    juce::String takeNoiseImportError() { return backgroundNoise.takeImportError(); }
    uint32_t getNoiseImportRevision() const noexcept { return backgroundNoise.getImportedRevision(); }
    uint32_t getNoiseSourceRevision(int sourceIndex) const noexcept
    { return backgroundNoise.getSourceRevision(sourceIndex); }
    void setNoiseTrimActive(bool active) noexcept { backgroundNoise.setTrimActive(active); }

    float getSampleSpecificParameterValue(const juce::String& parameterId, float fallbackValue) const;
    void setSampleSpecificParameterValue(const juce::String& parameterId, float value);
    // Message thread only; copies one effect without changing the selected group.
    bool applySampleSpecificParameterToAll(const juce::String& parameterId, float value);

private:
    const PercussionSound* getRepresentativeSampleSound(int groupIndex) const noexcept;
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
    OttProcessor ottProcessor;
    std::atomic<float>* ottAmountParam = nullptr;
    BackgroundNoisePlayer backgroundNoise;
    std::atomic<float>* noiseSourceParam = nullptr;
    std::atomic<float>* noiseGainParam = nullptr;
    std::atomic<float>* noiseModulationParam = nullptr;
    std::atomic<float>* noiseTriggerParam = nullptr;
    std::atomic<float>* noiseReleaseParam = nullptr;
    std::atomic<float>* noiseStartParam = nullptr;
    std::atomic<float>* noiseEndParam = nullptr;
    const std::vector<PercussionSampleLibrary::SampleGroupInfo> noiseTriggerGroups {
        PercussionSampleLibrary::getEmbeddedSampleGroups()
    };
    NoiseSidechain noiseSidechain;
    SampleSpectrum sampleSpectrum;
    std::atomic<uint64_t> samplePlayheadSequenceCounter { 0 };
    std::atomic<bool> samplePlaybackBypassed { false };
    PercussionSynthesiser sampler;
    MidiNoteActivityState midiNoteActivity;
    std::vector<PercussionSampleLibrary::SampleGroupInfo> sampleGroups;
    std::atomic<int> selectedSampleGroupIndex { -1 };
    SampleSpecificParameterState sampleSpecificParameters;
    SampleSpecificRealtimeCache sampleSpecificCache;
    std::unique_ptr<WarpCachePrewarmer> warpCachePrewarmer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
