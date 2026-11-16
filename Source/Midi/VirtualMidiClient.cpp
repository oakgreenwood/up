#include "VirtualMidiClient.h"

VirtualMidiClient::VirtualMidiClient()
    : clientId(static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64()) | 1ULL)
{
}

VirtualMidiClient::~VirtualMidiClient() { release(); }

void VirtualMidiClient::release()
{
    if (port > 0 && serverPort > 0)
        send(VirtualMidi::Command::release);
    port = 0;
    ready = false;
    lastAccepted = 0.0;
    lastAcknowledged = sequence;
}

void VirtualMidiClient::discover(double now)
{
    nextDiscovery = now + 1000.0;
    const auto file = VirtualMidi::directory().getChildFile("ready.json");
    const auto status = file.existsAsFile() && file.getSize() <= 8192
        ? juce::JSON::parse(file.loadFileAsString()) : juce::var();
    const auto age = juce::Time::currentTimeMillis() - static_cast<juce::int64>(status["updated_ms"]);
    const auto key = status["secret"].toString();
    const int discoveredPort = static_cast<int>(status["port"]);
    bool validKey = key.length() == 32;
    for (const auto c : key)
        validKey = validKey && juce::CharacterFunctions::getHexDigitValue(c) >= 0;
    if (static_cast<int>(status["protocol"]) != VirtualMidi::protocolVersion
        || discoveredPort <= 0 || discoveredPort > 65535 || age < 0 || age > 4000 || !validKey)
    {
        serverPort = 0;
        ready = false;
        error = "Install/start the ULICHPERC MIDI component, then enable its input in the DAW.";
        if (now >= nextLaunch && VirtualMidi::helperFile().exists())
        {
            nextLaunch = now + 10000.0;
            VirtualMidi::helperFile().startAsProcess();
        }
        return;
    }
    const juce::Uuid uuid(key);
    std::array<uint8_t, 16> discoveredSecret {};
    std::memcpy(discoveredSecret.data(), uuid.getRawData(), discoveredSecret.size());
    if (serverPort != discoveredPort || secret != discoveredSecret)
    {
        ready = false;
        lastAccepted = 0.0;
        nextClaim = 0.0;
    }
    serverPort = discoveredPort;
    secret = discoveredSecret;
    if ((static_cast<int>(status["available"]) & (1 << (port - 1))) == 0)
    {
        ready = false;
        error = status["error"].toString();
        if (error.isEmpty())
            error = "The MIDI component could not create this input. Check its runtime installation.";
    }
}

void VirtualMidiClient::poll(int requestedPort, bool active)
{
    requestedPort = active ? juce::jlimit(0, VirtualMidi::portCount, requestedPort) : 0;
    if (port != requestedPort)
    {
        release();
        port = requestedPort;
        nextDiscovery = nextClaim = 0.0;
    }
    if (port == 0)
        return;
    if (!bound)
        bound = socket.bindToPort(0, "127.0.0.1");
    if (!bound)
    {
        error = "The local MIDI connection could not be opened.";
        return;
    }
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now >= nextDiscovery)
        discover(now);
    if (serverPort == 0)
        return;
    for (int i = 0; i < 16 && socket.waitUntilReady(true, 0) > 0; ++i)
    {
        std::array<uint8_t, VirtualMidi::packetBytes + 1> data {};
        juce::String address;
        int sender = 0;
        const auto bytes = socket.read(data.data(), static_cast<int>(data.size()), false, address, sender);
        VirtualMidi::Packet reply;
        if (address != "127.0.0.1" || sender != serverPort
            || !VirtualMidi::Packet::decode(data.data(), bytes, reply)
            || reply.client != clientId || reply.port != port || reply.secret != secret)
            continue;
        const auto distance = reply.sequence - lastAcknowledged;
        const auto wallNow = static_cast<uint64_t>(juce::Time::currentTimeMillis());
        if (distance == 0 || distance >= 0x80000000U
            || reply.sentMs > wallNow + 100 || reply.sentMs + 1000 < wallNow
            || (reply.command != VirtualMidi::Command::accepted
                && reply.command != VirtualMidi::Command::busy
                && reply.command != VirtualMidi::Command::unavailable))
            continue;
        lastAcknowledged = reply.sequence;
        ready = reply.command == VirtualMidi::Command::accepted;
        if (ready)
        {
            lastAccepted = now;
            error.clear();
        }
        else if (reply.command == VirtualMidi::Command::busy)
            error = "ULICHPERC MIDI is in use by another instance. Turn off SHAKE in that instance first.";
        else
            error = "This MIDI input is unavailable. Check the component's MIDI runtime.";
    }
    if (now - lastAccepted > 1000.0)
        ready = false;
    if (now >= nextClaim)
    {
        nextClaim = now + 250.0;
        send(VirtualMidi::Command::claim);
    }
}

bool VirtualMidiClient::send(VirtualMidi::Command command, int note, int velocity)
{
    if (!bound || serverPort == 0 || port <= 0)
        return false;
    VirtualMidi::Packet packet;
    packet.command = command;
    packet.port = port;
    packet.note = juce::jlimit(0, 127, note);
    packet.velocity = juce::jlimit(1, 127, velocity);
    packet.sequence = ++sequence;
    packet.client = clientId;
    packet.secret = secret;
    packet.sentMs = static_cast<uint64_t>(juce::Time::currentTimeMillis());
    const auto data = packet.encode();
    if (socket.waitUntilReady(false, 0) <= 0)
        return false;
    return socket.write("127.0.0.1", serverPort, data.data(), static_cast<int>(data.size()))
        == static_cast<int>(data.size());
}

bool VirtualMidiClient::trigger(int note, int velocity)
{
    return ready && note >= 0 && note <= 127 && send(VirtualMidi::Command::trigger, note, velocity);
}
