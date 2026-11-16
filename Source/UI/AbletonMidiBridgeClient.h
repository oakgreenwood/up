#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

// Message-thread-only legacy client for a separate Live clip-insertion script.
// The existing MIDI export supplies an immutable snapshot; no work runs on audio.
class AbletonMidiBridgeClient
{
public:
    AbletonMidiBridgeClient();
    ~AbletonMidiBridgeClient();

    static constexpr bool isSupportedPlatform() noexcept
    {
       #if JUCE_MAC || JUCE_WINDOWS
        return true;
       #else
        return false;
       #endif
    }

    juce::Result insert(const juce::File& midiFile, double arrangementStart,
                        juce::AudioProcessorValueTreeState&);
    std::optional<juce::Result> poll();
    bool isPending() const noexcept { return requestId.isNotEmpty(); }

private:
    void clearRequest();

    juce::File directory;
    juce::String requestId;
    double deadlineMs = 0.0;
    double nextPollMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE(AbletonMidiBridgeClient)
};
