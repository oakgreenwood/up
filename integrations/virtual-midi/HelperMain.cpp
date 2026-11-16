#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Midi/VirtualMidiProtocol.h"
#include <atomic>

#if !JUCE_WINDOWS
 #include <sys/stat.h>
#endif

// One process per login session, independent of plugin/editor lifetime. All OS
// MIDI, IPC and deadlines run on this helper's worker. Its message thread
// publishes discovery files so disk access cannot delay note-offs.
class VirtualMidiService final : private juce::Thread
{
public:
    VirtualMidiService() : juce::Thread("ULICHPERC virtual MIDI") {}
    ~VirtualMidiService() override
    {
        stopThread(-1);
        VirtualMidi::directory().getChildFile("ready.json").deleteFile();
    }
    void start() { startThread(); }

    void publish()
    {
        if (!initialised.load(std::memory_order_acquire))
            return;
        juce::var status(new juce::DynamicObject());
        auto& object = *status.getDynamicObject();
        object.setProperty("protocol", VirtualMidi::protocolVersion);
        object.setProperty("port", serverPort);
        object.setProperty("secret", secretId.toString());
        object.setProperty("updated_ms", juce::Time::currentTimeMillis());
        object.setProperty("available", available);
        object.setProperty("error", startupError);
        const auto file = VirtualMidi::directory().getChildFile("ready.json");
        juce::TemporaryFile temporary(file);
        if (temporary.getFile().replaceWithText(juce::JSON::toString(status)))
        {
           #if !JUCE_WINDOWS
            if (::chmod(temporary.getFile().getFullPathName().toRawUTF8(), 0600) != 0)
                return;
           #endif
            temporary.overwriteTargetFileWithTemporary();
        }
    }

private:
    struct Port
    {
        std::unique_ptr<juce::MidiOutput> output;
        uint64_t owner = 0, lastOwner = 0;
        uint32_t lastSequence = 0;
        double lastSeen = 0.0, noteOffAt = 0.0;
        int heldNote = -1;

        void stopNote()
        {
            if (output && heldNote >= 0)
                output->sendMessageNow(juce::MidiMessage::noteOff(1, heldNote));
            heldNote = -1;
        }

        void release()
        {
            stopNote();
            owner = 0;
        }
    };

    void receive(double now)
    {
        std::array<uint8_t, VirtualMidi::packetBytes + 1> bytes {};
        juce::String address;
        int sourcePort = 0;
        const auto size = socket.read(bytes.data(), static_cast<int>(bytes.size()), false, address, sourcePort);
        VirtualMidi::Packet packet;
        if (address != "127.0.0.1" || sourcePort <= 0
            || !VirtualMidi::Packet::decode(bytes.data(), size, packet) || packet.secret != secret)
            return;
        const auto wallNow = juce::Time::currentTimeMillis();
        if (packet.sentMs > static_cast<uint64_t>(wallNow + 100)
            || packet.sentMs + 250 < static_cast<uint64_t>(wallNow))
            return; // Never replay old strokes after a stalled UI or queued datagram.
        auto& port = ports[static_cast<size_t>(packet.port - 1)];
        if (packet.command == VirtualMidi::Command::claim)
        {
            if (!port.output)
                packet.command = VirtualMidi::Command::unavailable;
            else if (port.owner != 0 && port.owner != packet.client)
                packet.command = VirtualMidi::Command::busy;
            else
            {
                if (port.owner == 0 && port.lastOwner != packet.client)
                    port.lastSequence = packet.sequence - 1;
                // Claim heartbeats cannot roll the stroke sequence backwards.
                if (isNewer(packet.sequence, port.lastSequence))
                {
                    port.lastSequence = packet.sequence;
                    port.lastSeen = now;
                    port.owner = packet.client;
                    port.lastOwner = packet.client;
                }
                packet.command = port.owner == packet.client
                    ? VirtualMidi::Command::accepted : VirtualMidi::Command::busy;
            }
            const auto reply = packet.encode();
            if (socket.waitUntilReady(false, 0) > 0)
                socket.write(address, sourcePort, reply.data(), static_cast<int>(reply.size()));
            return;
        }
        if (port.owner != packet.client || !isNewer(packet.sequence, port.lastSequence))
            return;
        port.lastSequence = packet.sequence;
        if (packet.command == VirtualMidi::Command::release)
            port.release();
        else if (packet.command == VirtualMidi::Command::trigger && packet.velocity > 0)
        {
            port.stopNote();
            port.output->sendMessageNow(juce::MidiMessage::noteOn(1, packet.note,
                                        static_cast<juce::uint8>(packet.velocity)));
            port.heldNote = packet.note;
            port.noteOffAt = now + 30.0;
            port.lastSeen = now;
        }
    }

    static bool isNewer(uint32_t value, uint32_t previous) noexcept
    {
        const auto distance = value - previous;
        return distance != 0 && distance < 0x80000000U;
    }

    void run() override
    {
        const auto directory = VirtualMidi::directory();
        if (directory.createDirectory().failed() || !socket.bindToPort(0, "127.0.0.1"))
            return;
       #if !JUCE_WINDOWS
        if (::chmod(directory.getFullPathName().toRawUTF8(), 0700) != 0)
            return;
       #endif
        serverPort = socket.getBoundPort();
        const auto* rawSecret = secretId.getRawData();
        std::memcpy(secret.data(), rawSecret, secret.size());
        for (int i = 0; i < VirtualMidi::portCount && !threadShouldExit(); ++i)
        {
            try
            {
                ports[static_cast<size_t>(i)].output = juce::MidiOutput::createNewDevice(VirtualMidi::portName());
            }
            catch (...)
            {
                // Platform/SDK failures are reported through discovery instead
                // of repeatedly crashing a login-started background process.
                break;
            }
            if (ports[static_cast<size_t>(i)].output)
                available |= 1 << i;
        }
        for (const auto& port : ports)
            if (!port.output)
            {
               #if JUCE_WINDOWS
                startupError = "A virtual MIDI input could not be created. This component requires Windows 11 "
                               "and the Windows MIDI Services runtime compatible with its JUCE build.";
               #else
                startupError = "A virtual MIDI input could not be created. Check the system MIDI service "
                               "and restart the ULICHPERC MIDI component.";
               #endif
                break;
            }
        // All published fields are immutable after this release, until join.
        initialised.store(true, std::memory_order_release);
        while (!threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            // Deadlines run even under continuous/bad IPC traffic.
            for (auto& port : ports)
            {
                if (port.heldNote >= 0 && now >= port.noteOffAt)
                    port.stopNote();
                if (port.owner != 0 && now - port.lastSeen >= VirtualMidi::leaseMs)
                    port.release();
            }
            if (socket.waitUntilReady(true, 2) > 0)
                for (int i = 0; i < 32 && socket.waitUntilReady(true, 0) > 0; ++i)
                    receive(juce::Time::getMillisecondCounterHiRes());
        }
        for (auto& port : ports)
            port.release();
        // Outputs are reclaimed on this worker, never in a DAW audio callback.
        for (auto& port : ports)
            port.output.reset();
    }

    juce::DatagramSocket socket;
    const juce::Uuid secretId;
    std::array<uint8_t, 16> secret {};
    std::array<Port, VirtualMidi::portCount> ports;
    juce::String startupError;
    int serverPort = 0, available = 0;
    std::atomic<bool> initialised { false };
};

class VirtualMidiApplication final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "ULICHPERC MIDI"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override
    {
        service = std::make_unique<VirtualMidiService>();
        service->start();
        startTimer(500);
    }
    void shutdown() override { stopTimer(); service.reset(); }
    void anotherInstanceStarted(const juce::String&) override {}
    void systemRequestedQuit() override { quit(); }
private:
    void timerCallback() override { service->publish(); }
    std::unique_ptr<VirtualMidiService> service;
};

START_JUCE_APPLICATION(VirtualMidiApplication)
