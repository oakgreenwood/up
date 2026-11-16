#pragma once

#include "VirtualMidiProtocol.h"

// Editor/message-thread owned. Only SHAKE strokes are sent; incoming host MIDI
// is never echoed. The DAW's returned MIDI is the sole playback/capture source.
class VirtualMidiClient
{
public:
    VirtualMidiClient();
    ~VirtualMidiClient();
    void poll(int port, bool active);
    bool trigger(int note, int velocity);
    bool isReady() const noexcept { return ready; }
    const juce::String& getError() const noexcept { return error; }
    void release();

private:
    bool send(VirtualMidi::Command, int note = 0, int velocity = 1);
    void discover(double now);
    juce::DatagramSocket socket;
    const uint64_t clientId;
    std::array<uint8_t, 16> secret {};
    uint32_t sequence = 0, lastAcknowledged = 0;
    int port = 0, serverPort = 0;
    bool bound = false, ready = false;
    double nextDiscovery = 0.0, nextClaim = 0.0, lastAccepted = 0.0, nextLaunch = 0.0;
    juce::String error;
    JUCE_DECLARE_NON_COPYABLE(VirtualMidiClient)
};
