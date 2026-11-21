#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

struct InputRoutingGuide
{
    juce::String title;
    juce::String steps;
    const char* imageResource = nullptr;
};

inline InputRoutingGuide getInputRoutingGuide(const juce::PluginHostType& host,
                                              juce::AudioProcessor::WrapperType wrapperType,
                                              const juce::String& pluginName)
{
    if (wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        return { "Input routing in Standalone",
                 "In Options > Audio/MIDI Settings, select your mic or interface input and uncheck Mute audio input.",
                 "standalone_png" };

    const auto inHost = [&pluginName] (const char* name, const char* steps, const char* imageResource = nullptr)
    {
        return InputRoutingGuide { juce::String("Input routing in ") + name,
                                   juce::String(steps).replace("{plugin}", pluginName),
                                   imageResource };
    };

    if (host.isAbletonLive())
        return inHost("Ableton Live",
            "On any audio track set Monitor to In ->\n"
            "Under Audio To, choose the track with {plugin}", "ableton_png");

    if (host.isLogic())
        return inHost("Logic Pro",
            "In the {plugin} window header, open Side Chain and choose your audio track or mic input.", "logic_png");

    if (host.isCubase() || host.isNuendo())
        return inHost(host.isNuendo() ? "Nuendo" : "Cubase",
            "Enable the sidechain in {plugin}. Under Set up Side-Chain Routing > Add Side-Chain Source,"
            " choose your audio track.", host.isNuendo() ? "nuendo_png" : "cubase_png");

    if (host.isBitwigStudio())
        return inHost("Bitwig Studio",
            "Click the sidechain icon on the {plugin} device and choose your audio track.", "bitwig_png");

    if (host.isReaper())
        return inHost("REAPER",
            "Set the {plugin} track to 4 channels and send your audio track from 1/2 to 3/4.\n"
            "In the plug-in pin connector, map 3/4 to the audio inputs. For a mono input, use channel 3.", "reaper_png");

    if (host.isFruityLoops())
        return inHost("FL Studio",
            "In the {plugin} Wrapper, open Processing > Connections. If an audio input appears,"
            " right-click its number and choose your source Mixer track.", "flstudio_png");

    const auto hostExecutable = juce::File(juce::PluginHostType::getHostPath()).getFileNameWithoutExtension();
    const bool isFenderStudioPro = hostExecutable.startsWithIgnoreCase("Fender Studio Pro");
    if (host.isStudioOne() || isFenderStudioPro)
        return inHost(isFenderStudioPro ? "Fender Studio Pro" : "Studio One",
            "Enable Sidechain in the {plugin} window. In the menu next to it, select Send for your audio channel.",
            isFenderStudioPro ? "fenderstudiopro_png" : "studioone_png");

    if (host.isSonar())
        return inHost("Cakewalk / Sonar",
            "In the {plugin} toolbar, open VST3 > Sidechain Sources and choose your audio track.", "cakewalk_png");

    if (host.isTracktionWaveform())
        return inHost("Waveform",
            "Choose your audio track from the sidechain menu at the top left of the {plugin} window.", "waveform_png");

    if (host.isArdour())
        return inHost("Ardour",
            "Right-click {plugin} in the mixer and open Pin Connections. Choose your audio track"
            " as the sidechain source and connect it to the plug-in's audio input pins.", "ardour_png");

    if (host.isReason())
        return inHost("Reason",
            "Press Tab and expand Optional Inputs & Outputs on {plugin}. If audio input jacks are available,"
            " patch your source output into them.", "reason_png");

    if (host.isMainStage())
        return inHost("MainStage",
            "If the {plugin} header shows a Side Chain menu, choose your mic input there.", "mainstage_png");

    if (host.isDigitalPerformer())
        return inHost("Digital Performer",
            "If the {plugin} Sidechain selector is available, choose your mic input or a bus fed by your audio track.",
            "digitalperformer_png");

    if (hostExecutable.equalsIgnoreCase("LUNA"))
        return inHost("LUNA",
            "If the {plugin} header shows Sidechain or SC, enable it and choose your audio track under Sources.",
            "luna_png");

    if (hostExecutable.startsWithIgnoreCase("Mixcraft"))
        return inHost("Mixcraft",
            "If the {plugin} window shows a Sidechain menu at the top left, choose your audio track there.", "mixcraft_png");

    constexpr auto genericSteps = "Route your audio track to the {plugin} instrument's sidechain."
                                  " If the instrument has no audio input, turn off AUDIO TRIGGER to play by touchpad.";
    auto guide = inHost(host.getHostDescription(), genericSteps);
    if (host.type == juce::PluginHostType::UnknownHost)
        guide.title = "Input routing";
    return guide;
}
