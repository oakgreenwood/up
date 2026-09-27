#include "PercussionSampleLibrary.h"

#include "BinaryData.h"
#include "PercussionSound.h"
#include "SampleNameParser.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <utility>
#include <vector>

namespace
{
    struct ParsedSampleResource
    {
        juce::String resourceName;
        juce::String originalFilename;
        juce::String sampleIdentifier;
        juce::String cleanName;
        ParsedSampleName parsed;
    };

    bool isWavResource(const juce::String& resourceName, const juce::String& originalFilename)
    {
        if (originalFilename.isNotEmpty())
        {
            if (originalFilename.toLowerCase().endsWith(".wav"))
                return true;
        }

        return resourceName.endsWithIgnoreCase("_wav");
    }

    int countExtraPitchSlotsBefore(int noteIndex,
                                   const std::map<int, int>& maxPitchIndexByNoteIndex) noexcept
    {
        int extraSlots = 0;

        for (const auto& entry : maxPitchIndexByNoteIndex)
        {
            if (entry.first >= noteIndex)
                break;

            extraSlots += juce::jmax(1, entry.second) - 1;
        }

        return extraSlots;
    }

    int getMappedNoteIndex(const ParsedSampleName& parsed,
                           const std::map<int, int>& maxPitchIndexByNoteIndex) noexcept
    {
        return parsed.noteIndex
             + countExtraPitchSlotsBefore(parsed.noteIndex, maxPitchIndexByNoteIndex)
             + parsed.pitchIndex
             - 1;
    }

    std::vector<ParsedSampleResource> collectSampleResources(std::map<int, int>& maxPitchIndexByNoteIndex)
    {
        std::vector<ParsedSampleResource> sampleResources;

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String resourceName(BinaryData::namedResourceList[i]);
            const juce::String originalFilename(BinaryData::originalFilenames[i]);
            DBG("Checking resource: " << resourceName << " (source file: " << originalFilename << ")");

            if (!isWavResource(resourceName, originalFilename))
            {
                DBG("  Skipped (not a wav resource)");
                continue;
            }

            const juce::String sampleIdentifier = originalFilename.isNotEmpty() ? originalFilename : resourceName;
            juce::String cleanName = SampleNameParser::getFileNameStem(sampleIdentifier);
            if (cleanName.isEmpty())
                cleanName = resourceName.upToLastOccurrenceOf("_wav", false, false);

            ParsedSampleName parsed;
            if (!SampleNameParser::parseSampleName(sampleIdentifier, parsed)
                && !SampleNameParser::parseSampleName(resourceName, parsed))
            {
                DBG("  Skipped: invalid sample name pattern from '" << sampleIdentifier
                    << "' / '" << resourceName << "' (expected note[_vX][_nY][_pZ])");
                continue;
            }

            ParsedSampleResource sampleResource;
            sampleResource.resourceName = resourceName;
            sampleResource.originalFilename = originalFilename;
            sampleResource.sampleIdentifier = sampleIdentifier;
            sampleResource.cleanName = cleanName;
            sampleResource.parsed = parsed;
            sampleResources.push_back(std::move(sampleResource));

            auto& maxPitchIndex = maxPitchIndexByNoteIndex[parsed.noteIndex];
            maxPitchIndex = juce::jmax(maxPitchIndex, parsed.pitchIndex);
        }

        return sampleResources;
    }

}

namespace PercussionSampleLibrary
{
    std::vector<SampleGroupInfo> getEmbeddedSampleGroups()
    {
        std::map<int, int> maxPitchIndexByNoteIndex;
        const auto resources = collectSampleResources(maxPitchIndexByNoteIndex);
        std::map<int, SampleGroupInfo> groups;
        for (const auto& resource : resources)
        {
            const int mapped = getMappedNoteIndex(resource.parsed, maxPitchIndexByNoteIndex);
            const int note = 48 + mapped - 1;
            if (note >= 0 && note < 128)
                groups.emplace(mapped, SampleGroupInfo {
                    resource.parsed.noteIndex, resource.parsed.pitchIndex, mapped, note });
        }
        std::vector<SampleGroupInfo> result;
        for (const auto& group : groups)
            result.push_back(group.second);
        return result;
    }

    void loadEmbeddedSamples(PercussionSynthesiser& sampler,
                             double originalBpm,
                             std::vector<SampleGroupInfo>* loadedSampleGroups)
    {
        if (loadedSampleGroups != nullptr)
            loadedSampleGroups->clear();

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        DBG("BinaryData::namedResourceListSize = " << BinaryData::namedResourceListSize);
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            DBG("  Resource[" << i << "]: " << BinaryData::namedResourceList[i]);

        std::map<int, int> maxPitchIndexByNoteIndex;
        const auto sampleResources = collectSampleResources(maxPitchIndexByNoteIndex);

        std::map<int, SampleGroupInfo> sampleGroupsByMappedNoteIndex;

        for (const auto& sampleResource : sampleResources)
        {
            const auto& resourceName = sampleResource.resourceName;
            const auto& parsed = sampleResource.parsed;

            int dataSize = 0;
            const void* data = BinaryData::getNamedResource(resourceName.toRawUTF8(), dataSize);
            if (data == nullptr || dataSize <= 0)
            {
                DBG("  ERROR: getNamedResource failed for " << resourceName);
                continue;
            }

            std::unique_ptr<juce::MemoryInputStream> stream(
                new juce::MemoryInputStream(data, static_cast<size_t>(dataSize), false));

            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(stream)));
            if (!reader)
            {
                DBG("  ERROR: AudioFormatReader creation failed for " << resourceName);
                continue;
            }

            const int mappedNoteIndex = getMappedNoteIndex(parsed, maxPitchIndexByNoteIndex);
            const int midiNote = 48 + mappedNoteIndex - 1;
            if (midiNote < 0 || midiNote > 127)
            {
                DBG("  Skipped: note out of MIDI range, noteIndex=" << parsed.noteIndex
                    << ", pitch=" << parsed.pitchIndex
                    << ", mappedNoteIndex=" << mappedNoteIndex
                    << ", midiNote=" << midiNote);
                continue;
            }

            DBG("  Parsed noteIndex=" << parsed.noteIndex
                << ", vGroup=" << parsed.velocityGroupIndex
                << ", variation=" << parsed.variationIndex
                << ", pitch=" << parsed.pitchIndex
                << ", mappedNoteIndex=" << mappedNoteIndex
                << ", midiNote=" << midiNote);

            SampleGroupInfo sampleGroup;
            sampleGroup.noteIndex = parsed.noteIndex;
            sampleGroup.pitchIndex = parsed.pitchIndex;
            sampleGroup.mappedNoteIndex = mappedNoteIndex;
            sampleGroup.midiNote = midiNote;
            sampleGroupsByMappedNoteIndex.emplace(mappedNoteIndex, sampleGroup);

            auto* sound = new PercussionSound(
                sampleResource.cleanName,
                *reader,
                juce::BigInteger().setRange(midiNote, 1, true),
                midiNote,
                0.001,
                0.05,
                reader->lengthInSamples / reader->sampleRate,
                resourceName,
                sampleResource.originalFilename,
                originalBpm);

            sampler.addSound(sound);
            sampler.registerLayeredSound(sound,
                                         midiNote,
                                         parsed.velocityGroupIndex,
                                         parsed.variationIndex);
            DBG("  Added PercussionSound for " << sampleResource.cleanName);
        }

        sampler.finalizeLayerMappings();

        if (loadedSampleGroups != nullptr)
        {
            loadedSampleGroups->reserve(sampleGroupsByMappedNoteIndex.size());

            for (const auto& entry : sampleGroupsByMappedNoteIndex)
                loadedSampleGroups->push_back(entry.second);
        }

        DBG("Total sampler sounds loaded: " << sampler.getNumSounds());
    }
}
