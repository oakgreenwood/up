#include "SampleMetadata.h"
#include <cmath>

namespace
{
    juce::String getFileName(juce::String pathOrName)
    {
        pathOrName = pathOrName.trim().replaceCharacter('\\', '/');
        return pathOrName.fromLastOccurrenceOf("/", false, false);
    }

    juce::String getFileNameStem(const juce::String& pathOrName)
    {
        auto fileName = getFileName(pathOrName);

        if (fileName.containsChar('.'))
            fileName = fileName.upToLastOccurrenceOf(".", false, false);

        return fileName;
    }

    juce::String getResourceBaseName(const juce::String& wavResourceName)
    {
        if (wavResourceName.endsWithIgnoreCase("_wav"))
            return wavResourceName.upToLastOccurrenceOf("_wav", false, false);

        return wavResourceName;
    }

    juce::String getSampleStemForMetadata(const juce::String& wavResourceName,
                                          const juce::String& wavOriginalFilename)
    {
        if (wavOriginalFilename.isNotEmpty())
            return getFileNameStem(wavOriginalFilename);

        auto stem = getResourceBaseName(wavResourceName);

        while (stem.startsWithChar('_'))
            stem = stem.substring(1);

        if (stem.startsWithIgnoreCase("samples_"))
            stem = stem.substring(8);

        return stem;
    }

    void addUnique(juce::StringArray& values, const juce::String& value)
    {
        if (value.isNotEmpty() && !values.contains(value))
            values.add(value);
    }

    juce::String findResourceNameForOriginalFilename(const juce::String& expectedFileName)
    {
        juce::String caseInsensitiveMatch;

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const juce::String originalFileName = getFileName(BinaryData::originalFilenames[i]);

            if (originalFileName == expectedFileName)
                return BinaryData::namedResourceList[i];

            if (caseInsensitiveMatch.isEmpty()
                && originalFileName.equalsIgnoreCase(expectedFileName))
            {
                caseInsensitiveMatch = BinaryData::namedResourceList[i];
            }
        }

        return caseInsensitiveMatch;
    }

    const void* getMetadataResourceData(const juce::String& wavResourceName,
                                        const juce::String& wavOriginalFilename,
                                        juce::String& jsonResName,
                                        juce::String& expectedJsonFileName,
                                        int& size)
    {
        const auto sampleStem = getSampleStemForMetadata(wavResourceName, wavOriginalFilename);
        expectedJsonFileName = sampleStem + ".transients.json";

        jsonResName = findResourceNameForOriginalFilename(expectedJsonFileName);
        if (jsonResName.isNotEmpty())
            return BinaryData::getNamedResource(jsonResName.toRawUTF8(), size);

        const auto resourceBaseName = getResourceBaseName(wavResourceName);

        juce::StringArray candidateResourceNames;
        addUnique(candidateResourceNames, sampleStem + "_transients_json");
        addUnique(candidateResourceNames, "_" + sampleStem + "_transients_json");
        addUnique(candidateResourceNames, "transients_" + sampleStem + "_transients_json");
        addUnique(candidateResourceNames, resourceBaseName + "_transients_json");

        for (int i = 0; i < candidateResourceNames.size(); ++i)
        {
            const auto& candidate = candidateResourceNames[i];

            if (const void* data = BinaryData::getNamedResource(candidate.toRawUTF8(), size))
            {
                jsonResName = candidate;
                return data;
            }
        }

        jsonResName = expectedJsonFileName;
        size = 0;
        return nullptr;
    }
}

std::unique_ptr<SampleMetadata> loadMetadataForResource(const juce::String& wavResourceName,
                                                        const juce::String& wavOriginalFilename)
{
    auto makeFallbackMetadata = [&wavResourceName](bool hasJsonFile, const juce::String& reason)
    {
        auto fallback = std::make_unique<SampleMetadata>();
        fallback->sampleRate = 48000.0;
        fallback->lengthSec = 0.0;
        fallback->hasTransientJson = hasJsonFile;
        fallback->warp = false;
        fallback->loop = false;
        fallback->ignoreTransientShaper = false;
        fallback->transients.push_back(0.0);
        DBG("  Using fallback metadata for " << wavResourceName
            << ": " << reason
            << " (sampleRate=48000, transient[0]=0.0)");
        return fallback;
    };

    int size = 0;
    juce::String jsonResName;
    juce::String expectedJsonFileName;
    const void* data = getMetadataResourceData(wavResourceName,
                                               wavOriginalFilename,
                                               jsonResName,
                                               expectedJsonFileName,
                                               size);

    DBG("Trying to load metadata: wavResource=" << wavResourceName
        << ", wavFile=" << wavOriginalFilename
        << ", expectedJsonFile=" << expectedJsonFileName
        << ", jsonResName=" << jsonResName);

    if (data == nullptr || size <= 0)
    {
        return makeFallbackMetadata(false, "no JSON resource for " + expectedJsonFileName);
    }

    juce::String jsonText = juce::String::fromUTF8(static_cast<const char*>(data), size);

    auto jsonVar = juce::JSON::parse(jsonText);
    if (jsonVar.isVoid() || !jsonVar.isObject())
    {
        return makeFallbackMetadata(true, "JSON parse failed for " + jsonResName);
    }

    auto& obj = *jsonVar.getDynamicObject();
    auto meta = std::make_unique<SampleMetadata>();
    meta->hasTransientJson = true;

    // JUCE 7 style: no default in getProperty
    if (obj.hasProperty("sampleRate"))
        meta->sampleRate = (double) obj.getProperty("sampleRate");
    if (meta->sampleRate <= 0.0)
        meta->sampleRate = 48000.0;

    meta->warp = obj.hasProperty("warp")
                   ? (bool) obj.getProperty("warp")
                   : false;

    meta->loop = obj.hasProperty("loop")
                   ? (bool) obj.getProperty("loop")
                   : false;
    if (obj.hasProperty("bpm"))
    {
        const double bpm = static_cast<double>(obj.getProperty("bpm"));
        if (std::isfinite(bpm) && bpm >= 1.0 && bpm <= 300.0)
            meta->originalBpm = bpm;
    }

    meta->ignoreTransientShaper = obj.hasProperty("ignoreTransientShaper")
                                      ? (bool) obj.getProperty("ignoreTransientShaper")
                                      : false;

    auto tv = obj.getProperty("transients");
    if (tv.isArray())
    {
        for (auto& v : *tv.getArray())
            meta->transients.push_back((double)v);

        std::sort(meta->transients.begin(), meta->transients.end());
    }

    if (!meta->hasTransients())
    {
        meta->transients.push_back(0.0);
        DBG("  JSON had no valid transients, inserted default transient 0.0 for " << jsonResName);
    }

    DBG("  Loaded metadata OK for " << jsonResName
        << " (transients=" << meta->transients.size()
        << ", loop=" << (meta->loop ? "true" : "false")
        << ", ignoreTransientShaper=" << (meta->ignoreTransientShaper ? "true" : "false") << ")");
    return meta;
}
