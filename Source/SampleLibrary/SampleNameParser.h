#pragma once

#include <juce_core/juce_core.h>

enum class SampleDirection
{
    none,
    up,
    down
};

struct ParsedSampleName
{
    int noteIndex = 0;
    int velocityGroupIndex = 1;
    int variationIndex = 1;
    int pitchIndex = 1;
    SampleDirection direction = SampleDirection::none;
};

namespace SampleNameParser
{
    juce::String getFileNameStem(juce::String pathOrName);
    bool parseSampleName(const juce::String& sampleIdentifier, ParsedSampleName& out);
}
