#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <cstring>

// Shared only by message/worker threads. No socket or filesystem API is used by render.
namespace VirtualMidi
{
constexpr int portCount = 1;
constexpr int packetBytes = 64;
constexpr int protocolVersion = 2;
constexpr double leaseMs = 2000.0;
enum class Command : uint8_t { claim = 1, trigger, release, accepted, busy, unavailable };

inline juce::String portName() { return "ULICHPERC MIDI"; }

inline juce::File directory()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile("Application Support");
   #endif
    return base.getChildFile("shroomnoise/ulichpercs/VirtualMidi");
}

inline juce::File helperFile()
{
   #if JUCE_MAC
    return directory().getChildFile("ULICHPERC MIDI.app");
   #elif JUCE_WINDOWS
    return directory().getChildFile("ULICHPERC MIDI.exe");
   #else
    return directory().getChildFile("ULICHPERC MIDI");
   #endif
}

struct Packet
{
    Command command = Command::claim;
    int port = 1, note = 0, velocity = 1;
    uint32_t sequence = 0;
    uint64_t client = 0, sentMs = 0;
    std::array<uint8_t, 16> secret {};

    std::array<uint8_t, packetBytes> encode() const noexcept
    {
        std::array<uint8_t, packetBytes> data {};
        std::memcpy(data.data(), "ULVM", 4);
        data[4] = protocolVersion;
        data[5] = static_cast<uint8_t>(command);
        data[6] = static_cast<uint8_t>(port);
        data[7] = static_cast<uint8_t>(note);
        data[8] = static_cast<uint8_t>(velocity);
        const auto write = [&data](int start, uint64_t value, int count)
        {
            for (int i = 0; i < count; ++i)
                data[static_cast<size_t>(start + i)] = static_cast<uint8_t>(value >> (i * 8));
        };
        write(10, sequence, 4);
        write(14, client, 8);
        std::memcpy(data.data() + 22, secret.data(), secret.size());
        write(38, sentMs, 8);
        return data;
    }

    static bool decode(const uint8_t* data, int bytes, Packet& packet) noexcept
    {
        if (bytes != packetBytes || std::memcmp(data, "ULVM", 4) != 0
            || data[4] != protocolVersion || data[5] < 1 || data[5] > 6
            || data[6] < 1 || data[6] > portCount || data[7] > 127 || data[8] > 127)
            return false;
        const auto read = [data](int start, int count)
        {
            uint64_t value = 0;
            for (int i = 0; i < count; ++i)
                value |= static_cast<uint64_t>(data[start + i]) << (i * 8);
            return value;
        };
        packet.command = static_cast<Command>(data[5]);
        packet.port = data[6];
        packet.note = data[7];
        packet.velocity = data[8];
        packet.sequence = static_cast<uint32_t>(read(10, 4));
        packet.client = read(14, 8);
        std::memcpy(packet.secret.data(), data + 22, packet.secret.size());
        packet.sentMs = read(38, 8);
        return packet.client != 0;
    }
};
}
