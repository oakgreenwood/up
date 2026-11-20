#pragma once

#include <functional>
#include <optional>
#include <juce_gui_extra/juce_gui_extra.h>

// AppKit view over the Live Play surface. The JUCE button below its cutout
// still receives its own mouse events.
class TrackpadPlayView final : public juce::NSViewComponent, private juce::Timer
{
public:
    using Touch = std::optional<juce::Point<float>>;
    explicit TrackpadPlayView(std::function<void(int, Touch, double)> onHit);
    ~TrackpadPlayView() override;
    void setPassThroughBounds(juce::Rectangle<int> bounds) { passThroughBounds = bounds.toFloat(); }
#if JUCE_DEBUG
    static void logMidiHit(int note, int velocity, bool trackMidi, bool accepted);
#endif

private:
    struct ViewClass;
    void timerCallback() override;
    void visibilityChanged() override;
    void clearContacts();
    void finishHit(const char* reason);

    const std::function<void(int, Touch, double)> hit;
    juce::Rectangle<float> passThroughBounds;
    Touch contact, endedContact, pendingContact;
    double endedAt = 0.0;
    double mouseDownTimestamp = 0.0;
    double mouseDownReceivedMs = 0.0;
    float peakPressure = 0.0f;
};
