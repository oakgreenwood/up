#pragma once

#include "PercussionSynthesiser.h"

#include <vector>

namespace PercussionSampleLibrary
{
    struct SampleGroupInfo
    {
        int noteIndex = 0;
        int pitchIndex = 1;
        int mappedNoteIndex = 0;
        int midiNote = 0;
    };

    // Constructor/message thread only; inventories names without decoding audio.
    std::vector<SampleGroupInfo> getEmbeddedSampleGroups();

    void loadEmbeddedSamples(PercussionSynthesiser& sampler,
                             double originalBpm,
                             std::vector<SampleGroupInfo>* loadedSampleGroups = nullptr);
}
