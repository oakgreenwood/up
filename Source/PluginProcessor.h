#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include "Effects/RzhavProcessor.h"
#include "Effects/OttProcessor.h"
#include "Effects/SampleSpectrum.h"
#include "Effects/NoiseSidechain.h"
#include "Playback/BackgroundNoisePlayer.h"
#include "Playback/InputTrigger.h"
#include "Midi/MidiNoteActivityState.h"
#include "Midi/ShakeMidiState.h"
#include "Midi/ShakeMidiTake.h"
#include "PercussionSynthesiser.h"
#include "Parameters/SampleSpecificParameterState.h"
#include "Parameters/SampleSpecificRealtimeCache.h"
#include "SampleLibrary/PercussionSampleLibrary.h"
#include "Tempo/HostTempoTracker.h"
#include "Tempo/HostRecordingCallbackReceiver.h"
#include "Warp/WarpCachePrewarmer.h"

//==============================================================================
class AudioPluginAudioProcessor final : public juce::AudioProcessor,
                                        public Ulichperc::HostRecordingCallbackReceiver,
                                        private juce::VST3ClientExtensions,
                                        private juce::AudioProcessorValueTreeState::Listener,
                                        private juce::Timer
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
    uint32_t getIncomingNoteOnGeneration(int note) const noexcept
    {
        return note >= 0 && note < 128
            ? incomingNoteOnGenerations[static_cast<size_t>(note)].load(std::memory_order_relaxed) : 0;
    }
    bool isSampleGroupDirectional(int groupIndex) const noexcept;
    // Message-thread-only live-play control.
    void setLivePlayNotes(int primaryNote, int alternateNote = -1) noexcept
    { shakeMidi.setTarget(primaryNote, alternateNote); }
    void setInputControlTarget(int note, bool trackMidi = false) noexcept;
#if JUCE_DEBUG
    struct InputControlTraceEvent
    {
        enum class Kind { onset, suppressedRetrigger, localNoteOn, incomingNoteOn };
        Kind kind;
        uint64_t frame;
        double sampleRate;
        float peak;
        int note, velocity, channel;
        bool trackMidi;
    };
    // Message thread only. All formatting and logging happen in the editor.
    template <typename Callback>
    uint32_t drainInputControlTrace(Callback&& callback)
    {
        const auto read = inputControlTraceFifo.read(static_cast<int>(inputControlTrace.size()));
        read.forEach([&] (int index) { callback(inputControlTrace[static_cast<size_t>(index)]); });
        return inputControlTraceDropped.exchange(0, std::memory_order_relaxed);
    }
#endif
    template <typename Callback>
    void forEachInputControlVelocity(Callback&& callback)
    {
        const auto read = inputControlFifo.read(static_cast<int>(inputControlHits.size()));
        const auto target = inputControlTarget.load(std::memory_order_relaxed);
        const auto reset = inputControlResetGeneration.load(std::memory_order_relaxed);
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        read.forEach([&] (int index)
        {
            const auto& hit = inputControlHits[static_cast<size_t>(index)];
            if (hit.target == target && hit.reset == reset && nowMs - hit.timeMs <= 250.0)
                callback(hit.velocity);
        });
    }
    bool triggerLivePlay(int velocity, bool alternate) noexcept { return shakeMidi.trigger(velocity, alternate); }
    uint32_t getShakeResetGeneration() const noexcept { return shakeMidi.getResetGeneration(); }
    // TRACK MIDI: 0 = internal SHAKE, 1 = ULICHPERC MIDI / returned DAW input.
    int getVirtualMidiInput() const noexcept { return virtualMidiInput.load(std::memory_order_relaxed); }
    void setVirtualMidiInput(int input);
    bool isHostRecording() const noexcept { return hostRecording.load(std::memory_order_relaxed); }
    bool isShakeUsingVirtualMidi() const noexcept
    { return shakeVirtualMidiActive.load(std::memory_order_acquire) || getVirtualMidiInput() > 0; }
    void setShakeVirtualMidiActive(bool active) noexcept
    { shakeVirtualMidiActive.store(active, std::memory_order_release); }
    void setHostRecordingCallbackAvailable(bool available) noexcept override
    {
        hostRecordingCallback.store(available ? Ulichperc::HostRecordingCallbackAvailability::available
                                             : Ulichperc::HostRecordingCallbackAvailability::unavailable,
                                    std::memory_order_relaxed);
    }
    bool needsManualShakeMode() const noexcept
    {
        return hostRecordingCallback.load(std::memory_order_relaxed)
            == Ulichperc::HostRecordingCallbackAvailability::unavailable;
    }
    void requestShakeSequenceReset() noexcept { shakeSequenceResetRequested.store(true, std::memory_order_release); }
    bool isPlaybackBypassed() const noexcept { return samplePlaybackBypassed.load(std::memory_order_relaxed); }
    bool hasRecordedShakeMidi() const noexcept { return shakeMidiTake.hasTake(); }
    ShakeMidiTake::Status getRecordedMidiStatus() const noexcept { return shakeMidiTake.getStatus(); }
    // Message thread only; writes a snapshot of the current take's published notes.
    juce::File createRecordedShakeMidiFile(double* arrangementStart = nullptr) const
    { return shakeMidiTake.createMidiFile(arrangementStart); }

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
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    bool getPluginHasMainInput() const override { return false; }
    void collectInputTriggers(const juce::AudioBuffer<float>&, int channels, uint64_t target, double nowMs) noexcept;
#if JUCE_DEBUG
    void traceInputControl(InputControlTraceEvent::Kind, int position, int note, int velocity,
                           int channel, bool trackMidi, float peak = 0.0f) noexcept;
#endif
    const PercussionSound* getRepresentativeSampleSound(int groupIndex) const noexcept;
    void addPercussionVoices();
    void updateVoiceSharedState();
    void refreshFormantBypassTarget() noexcept;
    void reportFormantLatency();
    void timerCallback() override { reportFormantLatency(); }
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
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    std::array<std::atomic<uint32_t>, 128> incomingNoteOnGenerations;
    ShakeMidiState shakeMidi;
    ShakeMidiTake shakeMidiTake;
    InputTrigger inputTrigger;
    uint64_t observedInputControlTarget = 0;
    static constexpr uint64_t inputControlTrackMidiBit = 0x100;
    struct InputControlHit { uint64_t target; uint32_t reset; int velocity; double timeMs; };
    std::array<InputControlHit, ShakeMidiState::maxTriggersPerBlock + 1> inputControlHits {};
    juce::AbstractFifo inputControlFifo { static_cast<int>(inputControlHits.size()) };
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<uint64_t> inputControlTarget { 0 };
    std::atomic<uint32_t> inputControlResetGeneration { 0 };
#if JUCE_DEBUG
    uint64_t inputControlTraceFrame = 0;
    std::array<InputControlTraceEvent, 129> inputControlTrace {};
    juce::AbstractFifo inputControlTraceFifo { static_cast<int>(inputControlTrace.size()) };
    std::atomic<uint32_t> inputControlTraceDropped { 0 };
#endif
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<int>::is_always_lock_free);
    std::atomic<int> virtualMidiInput { 0 };
    std::atomic<bool> hostRecording { false };
    std::atomic<bool> shakeVirtualMidiActive { false };
    static_assert(std::atomic<Ulichperc::HostRecordingCallbackAvailability>::is_always_lock_free);
    std::atomic<Ulichperc::HostRecordingCallbackAvailability> hostRecordingCallback {
        Ulichperc::HostRecordingCallbackAvailability::unknown
    };
    std::atomic<bool> shakeSequenceResetRequested { false };
    std::vector<PercussionSampleLibrary::SampleGroupInfo> sampleGroups;
    std::atomic<int> selectedSampleGroupIndex { -1 };
    SampleSpecificParameterState sampleSpecificParameters;
    SampleSpecificRealtimeCache sampleSpecificCache;
    int preparedFormantLatency = 0;
    uint32_t observedFormantBypassRevision = 0;
    bool formantRequired = false;
    std::atomic<int> activeFormantLatency { 0 };
    std::atomic_flag reportingFormantLatency = ATOMIC_FLAG_INIT;
    std::unique_ptr<WarpCachePrewarmer> warpCachePrewarmer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
