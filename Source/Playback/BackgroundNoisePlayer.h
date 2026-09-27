#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <condition_variable>
#include <mutex>

// Embedded sources live until teardown. Imported sources use immutable pinned
// slots; only the loader replaces/frees them. Playback state is audio-only.
class BackgroundNoisePlayer : private juce::Thread
{
public:
    BackgroundNoisePlayer();
    ~BackgroundNoisePlayer() override;
    static juce::StringArray getSourceNames();
    static int importedSourceIndex() noexcept;
    // Non-realtime APIs: import/UI/state only. Save may wait for pending decoding.
    static bool canImportFile(const juce::File&);
    void importFile(const juce::File&);
    void writeImportedState(juce::ValueTree&);
    void restoreImportedState(const juce::ValueTree&);
    juce::String getImportedName() const;
    juce::String takeImportError();
    uint32_t getImportedRevision() const noexcept { return importedRevision.load(); }

    struct Waveform
    {
        static constexpr int bins = 1024;
        std::array<float, bins> minimum {}, maximum {};
        int samples = 0;
        float peak = 0.0f;
    };
    struct Region { int start = 0, end = 0; }; // End is exclusive.
    static Region resolveRegion(int samples, float start, float end) noexcept;
    // Message thread: copies only the small summary, never borrows replaceable audio.
    bool copyWaveform(int sourceIndex, Waveform&) const;
    // Normalized source position, or -1 when the displayed source is stopped.
    float getPlayheadPosition(int sourceIndex) const noexcept;
    // Message-thread gesture handoff; does not change saved gain or trim values.
    void setTrimActive(bool active) noexcept;
    uint32_t getTrimGestureState() const noexcept
    { return trimGestureState.load(std::memory_order_acquire); }

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& output, float sourceIndex, float gainDb,
                 const float* modulation, float start, float end, uint32_t gestureState) noexcept;

private:
    struct Source
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        Waveform waveform;
        juce::String name, encodedWav;
    };

    static void prepareWaveform(Source&);
    std::unique_ptr<Source> decode(juce::AudioFormatReader&, uint32_t generation);
    void run() override;
    int acquireImported() noexcept;
    void releaseImported(int slot) noexcept;
    struct ImportedSlot
    {
        std::atomic<int> readers { 0 }; // -1 reserves a slot for the loader.
        std::unique_ptr<Source> source;
    };
    std::array<ImportedSlot, 3> importedSlots;
    std::atomic<int> publishedImported { -1 };
    std::atomic<uint32_t> importGeneration { 0 }, importedRevision { 0 };
    mutable std::mutex importMutex; // Never used by render/reset/prepare.
    std::condition_variable importWake, importFinished;
    juce::File requestedFile;
    juce::String requestedName, requestedWav, importError;
    bool importPending = false, stopping = false;

    std::vector<Source> sources;
    juce::SmoothedValue<float> gain;
    juce::SmoothedValue<float> regionFade;
    // One coherent audio-to-UI snapshot: source/slot identity and normalized
    // position. Zero means stopped. No audio buffers are shared with the UI.
    std::atomic<uint64_t> playheadSnapshot { 0 };
    // Odd means dragging; changing the counter also catches gestures completed
    // between callbacks. The UI writes the atomic; audio owns the other fields.
    std::atomic<uint32_t> trimGestureState { 0 };
    uint32_t lastTrimGestureState = 0;
    bool trimStopPending = false;
    double outputSampleRate = 44100.0;
    double position = 0.0;
    int activeSource = -1;
    int activeImportedSlot = -1;
    const Source* activeData = nullptr; // Borrowed while its slot is pinned.
    Region activeRegion;
    int crossfadeFrames = 0;
};
