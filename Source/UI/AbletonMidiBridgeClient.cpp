#include "AbletonMidiBridgeClient.h"
#include "Parameters/PluginParameters.h"
#include "Midi/ShakeMidiTake.h"

#include <cmath>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace
{
    constexpr int protocolVersion = 1;

    juce::var readJson(const juce::File& file, juce::int64 maximumBytes)
    {
        return file.existsAsFile() && file.getSize() <= maximumBytes
            ? juce::JSON::parse(file.loadFileAsString()) : juce::var();
    }

    juce::String processId()
    {
       #if JUCE_WINDOWS
        return juce::String(static_cast<juce::int64>(GetCurrentProcessId()));
       #else
        return juce::String(static_cast<int>(getpid()));
       #endif
    }
}

AbletonMidiBridgeClient::AbletonMidiBridgeClient()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile("Application Support");
   #endif
    directory = base.getChildFile("shroomnoise/ulichpercs/AbletonBridge")
                    .getChildFile(processId());
}

AbletonMidiBridgeClient::~AbletonMidiBridgeClient()
{
    clearRequest();
}

juce::Result AbletonMidiBridgeClient::insert(const juce::File& midiFile, double arrangementStart,
                                           juce::AudioProcessorValueTreeState& parameters)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (isPending())
        return juce::Result::fail("A MIDI insertion is already pending.");
    if (!std::isfinite(arrangementStart) || arrangementStart < 0.0)
        return juce::Result::fail("The host did not provide a supported Arrangement start for this take. "
                                  "Record a new take or use DRAG MIDI.");

    const auto ready = readJson(directory.getChildFile("ready.json"), 4096);
    const auto now = juce::Time::currentTimeMillis();
    const auto age = now - static_cast<juce::int64>(ready["updated_ms"]);
    const auto session = ready["session"].toString();
    if (static_cast<int>(ready["protocol"]) != protocolVersion || session.isEmpty()
        || age < 0 || age > 4000)
        return juce::Result::fail(
            "Install the supplied ULICHPERC folder in Remote Scripts inside Live's User Library. "
            "Restart Live, then select ULICHPERC under Control Surface in Live's MIDI settings "
            "(Input and Output: None). This one-time setup lets Live load the bridge automatically.");

    auto* link = parameters.getParameter(PluginParameters::abletonLinkId);
    if (link == nullptr || !midiFile.existsAsFile() || midiFile.getSize() > 2 * 1024 * 1024)
        return juce::Result::fail("The captured MIDI take could not be exported.");
    auto input = midiFile.createInputStream();
    juce::MidiFile midi;
    if (!input || !midi.readFrom(*input) || midi.getNumTracks() != 1 || midi.getTimeFormat() <= 0)
        return juce::Result::fail("The captured MIDI file could not be read.");

    const auto* track = midi.getTrack(0);
    const double ticksPerBeat = midi.getTimeFormat();
    const double length = track->getEndTime() / ticksPerBeat;
    if (!std::isfinite(length) || length <= 0.0 || length > ShakeMidiTake::maxTakeBeats + 1.0)
        return juce::Result::fail("The captured MIDI take has an invalid length.");

    juce::Array<juce::var> notes;
    for (int i = 0; i < track->getNumEvents(); ++i)
    {
        const auto* event = track->getEventPointer(i);
        if (!event->message.isNoteOn())
            continue;
        if (notes.size() >= ShakeMidiTake::maxNotes || event->noteOffObject == nullptr)
            return juce::Result::fail("The captured MIDI take has invalid note data.");
        const double start = event->message.getTimeStamp() / ticksPerBeat;
        const double duration = (event->noteOffObject->message.getTimeStamp()
                                 - event->message.getTimeStamp()) / ticksPerBeat;
        if (!std::isfinite(start) || !std::isfinite(duration) || start < 0.0 || duration <= 0.0)
            return juce::Result::fail("The captured MIDI take has invalid note timing.");
        notes.add(juce::var(juce::Array<juce::var> {
            event->message.getNoteNumber(), start, duration,
            static_cast<int>(event->message.getVelocity()), event->message.getChannel() }));
    }
    if (notes.isEmpty())
        return juce::Result::fail("There are no captured notes to add.");

    const int token = juce::Random::getSystemRandom().nextInt(PluginParameters::abletonLinkMaximum) + 1;
    link->beginChangeGesture();
    link->setValueNotifyingHost(link->convertTo0to1(static_cast<float>(token)));
    link->endChangeGesture();

    const auto id = juce::Uuid().toString();
    juce::var request(new juce::DynamicObject());
    auto* object = request.getDynamicObject();
    object->setProperty("protocol", protocolVersion);
    object->setProperty("id", id);
    object->setProperty("session", session);
    object->setProperty("token", token);
    object->setProperty("created_ms", now);
    object->setProperty("expires_ms", now + 15000);
    object->setProperty("length", length);
    object->setProperty("position", arrangementStart);
    object->setProperty("notes", juce::var(notes));

    const auto target = directory.getChildFile("request-" + id + ".json");
    juce::TemporaryFile temporary(target);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(request, true))
        || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("The MIDI insertion request could not be saved.");

    requestId = id;
    deadlineMs = juce::Time::getMillisecondCounterHiRes() + 15000.0;
    nextPollMs = 0.0;
    return juce::Result::ok();
}

std::optional<juce::Result> AbletonMidiBridgeClient::poll()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (!isPending() || now < nextPollMs)
        return {};
    nextPollMs = now + 200.0;
    const auto reply = readJson(directory.getChildFile("response-" + requestId + ".json"), 8192);
    if (reply["id"].toString() == requestId
        && static_cast<int>(reply["protocol"]) == protocolVersion)
    {
        const bool success = static_cast<bool>(reply["ok"]);
        const auto message = reply["message"].toString();
        clearRequest();
        return success ? juce::Result::ok() : juce::Result::fail(message);
    }
    if (now >= deadlineMs)
    {
        clearRequest();
        return juce::Result::fail("Live did not acknowledge the insertion. Check the track before retrying.");
    }
    return {};
}

void AbletonMidiBridgeClient::clearRequest()
{
    if (requestId.isEmpty())
        return;
    directory.getChildFile("request-" + requestId + ".json").deleteFile();
    directory.getChildFile("response-" + requestId + ".json").deleteFile();
    requestId.clear();
}
