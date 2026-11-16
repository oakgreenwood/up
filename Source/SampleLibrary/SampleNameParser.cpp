#include "SampleNameParser.h"

namespace SampleNameParser
{
    juce::String getFileNameStem(juce::String pathOrName)
    {
        pathOrName = pathOrName.trim().replaceCharacter('\\', '/');
        pathOrName = pathOrName.fromLastOccurrenceOf("/", false, false);

        if (pathOrName.containsChar('.'))
            pathOrName = pathOrName.upToLastOccurrenceOf(".", false, false);

        return pathOrName;
    }

    bool parseSampleName(const juce::String& sampleIdentifier, ParsedSampleName& out)
    {
        juce::String tokenName = getFileNameStem(sampleIdentifier);

        while (tokenName.startsWithChar('_'))
            tokenName = tokenName.substring(1);

        const int samplesPrefixAt = tokenName.lastIndexOfIgnoreCase("samples_");
        if (samplesPrefixAt >= 0)
            tokenName = tokenName.substring(samplesPrefixAt + 8);

        if (tokenName.startsWithIgnoreCase("samples_"))
            tokenName = tokenName.substring(8);

        juce::StringArray tokens;
        tokens.addTokens(tokenName, "_", "");
        tokens.removeEmptyStrings();

        if (tokens.isEmpty())
            return false;

        int firstNumericToken = -1;
        for (int i = 0; i < tokens.size(); ++i)
        {
            if (tokens[i].containsOnly("0123456789"))
            {
                firstNumericToken = i;
                break;
            }
        }

        if (firstNumericToken < 0)
            return false;

        const int noteIdx = tokens[firstNumericToken].getIntValue();
        if (noteIdx <= 0)
            return false;

        bool hasVelocityToken = false;
        bool hasVariationToken = false;
        bool hasPitchToken = false;
        int velocityGroup = 1;
        int variation = 1;
        int pitch = 1;
        SampleDirection direction = SampleDirection::none;

        for (int i = firstNumericToken + 1; i < tokens.size(); ++i)
        {
            const auto t = tokens[i].trim();
            if (t.equalsIgnoreCase("u") || t.equalsIgnoreCase("d"))
            {
                if (direction != SampleDirection::none || i != tokens.size() - 1)
                    return false;
                direction = t.equalsIgnoreCase("u") ? SampleDirection::up : SampleDirection::down;
                continue;
            }

            if (t.length() < 2)
                return false;

            const juce::juce_wchar prefix = juce::CharacterFunctions::toLowerCase(t[0]);
            const auto numericPart = t.substring(1);

            if (!numericPart.containsOnly("0123456789"))
                return false;

            const int value = numericPart.getIntValue();
            if (value <= 0)
                return false;

            if (prefix == 'v')
            {
                if (hasVelocityToken)
                    return false;
                hasVelocityToken = true;
                velocityGroup = value;
            }
            else if (prefix == 'n')
            {
                if (hasVariationToken)
                    return false;
                hasVariationToken = true;
                variation = value;
            }
            else if (prefix == 'p')
            {
                if (hasPitchToken)
                    return false;
                hasPitchToken = true;
                pitch = value;
            }
            else
            {
                return false;
            }
        }

        out.noteIndex = noteIdx;
        out.velocityGroupIndex = velocityGroup;
        out.variationIndex = variation;
        out.pitchIndex = pitch;
        out.direction = direction;
        return true;
    }
}
