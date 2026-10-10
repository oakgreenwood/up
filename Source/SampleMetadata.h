#pragma once

#include <juce_core/juce_core.h>
#include "BinaryData.h"

// One continuous region, either attack or sustain
struct ZoneInfo
{
    double startSec = 0.0;
    double endSec   = 0.0;
    bool   isAttack = false;  // true = "attack", false = "sustain"
};

// Metadata for a sample: zones parsed from your JSON
struct SampleMetadata
{
    double sampleRate = 48000.0;
    double lengthSec  = 0.0;
    bool hasTransientJson = false;
    bool warp = false;
    bool loop = false;
    double originalBpm = 0.0; // 0 when the JSON does not supply a BPM.
    bool ignoreTransientShaper = false;
    std::vector<double> transients;

    bool hasTransients() const noexcept { return !transients.empty(); }
};

// Given a BinaryData WAV resource name and its original source filename,
// find and load metadata for the exact sample file stem
// (e.g. "1_v1_n1.wav" -> "1_v1_n1.transients.json").
std::unique_ptr<SampleMetadata> loadMetadataForResource(const juce::String& wavResourceName,
                                                        const juce::String& wavOriginalFilename);
