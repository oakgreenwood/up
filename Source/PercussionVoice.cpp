#include "PercussionVoice.h"

#include "Parameters/PluginParameters.h"
#include "Playback/VelocityLayerGain.h"

#include <cmath>

static_assert(std::atomic<uint64_t>::is_always_lock_free);

namespace
{
    constexpr double warpLoopPitchDebounceSeconds = 0.08;
    constexpr double pitchRatioChangeEpsilon = 1e-6;

    bool isNeutralPitchRatio(double ratio) noexcept
    {
        return std::abs(ratio - 1.0) < 1e-5;
    }

    bool pitchRatiosDiffer(double a, double b) noexcept
    {
        return std::abs(a - b) > pitchRatioChangeEpsilon;
    }
}

PercussionVoice::PercussionVoice()
{
    adsr.setParameters({ 0.00f, 0.0f, 1.0f, 0.06f });
}

void PercussionVoice::setSustainParam(std::atomic<float>* p)
{
    sustainAmountParam = p;
}

bool PercussionVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<PercussionSound*>(sound) != nullptr;
}

void PercussionVoice::startNote(int midiNoteNumber,
                                float velocity,
                                juce::SynthesiserSound* sound,
                                int currentPitchWheelPosition)
{
    juce::ignoreUnused(midiNoteNumber, currentPitchWheelPosition);

    currentSound = dynamic_cast<PercussionSound*>(sound);
    playheadSequence = playheadSequenceCounter != nullptr
        ? playheadSequenceCounter->fetch_add(1, std::memory_order_relaxed) + 1 : 0;
    beginPlayback(velocity);
    publishPlayhead();
}

void PercussionVoice::stopNote(float /*velocity*/, bool allowTailOff)
{
    if (allowTailOff)
    {
        // Loops and warp-enabled samples are key-held; ordinary hits play as one-shots.
        if (metadata == nullptr || (!metadata->warp && !metadata->loop))
            return;

        adsr.noteOff();
        return;
    }

    adsr.reset();
    clearActivePlayback();
}

void PercussionVoice::pitchWheelMoved(int) {}
void PercussionVoice::controllerMoved(int, int) {}

void PercussionVoice::prepareRealtimeWarpResources(double playbackSampleRate, int samplesPerBlock)
{
    adsr.reset();
    clearActivePlayback();
    formantShifter.prepare(playbackSampleRate);
    formantColouration.prepare(playbackSampleRate);
    equaliser.prepare(playbackSampleRate);
    noiseTriggerDelay.prepare(FormantShifter::latencySamples
        + PsolaFormantShifter::latencyForSampleRate(playbackSampleRate));
    formantDrainSamples = FormantShifter::tailSamples
                          + PsolaFormantShifter::tailForSampleRate(playbackSampleRate);
    realtimeWarpPlayer.prepare(playbackSampleRate, 2, samplesPerBlock);
}

void PercussionVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                      int startSample,
                                      int numSamples)
{
    if (currentSound == nullptr || numSamples <= 0)
        return;

    const float formantRatio = getCurrentFormantRatio();
    formantShifter.setRatio(formantRatio);
    formantColouration.setRatio(formantRatio);
    sampleMonoAmount.setTargetValue(getCurrentSampleMonoAmount());
    samplePan.setTargetValue(getCurrentSamplePan());
    updateEqualiser();
    // A live edit during the existing formant drain also needs an EQ tail.
    if (sourceFinished && !equaliserTailReserved && equaliser.hasTail())
    {
        formantTailRemaining += static_cast<int>(SampleEqualiser::validSampleRate(getSampleRate())
                                                 * SampleEqualiser::tailSeconds);
        equaliserTailReserved = true;
    }
    const int spectrumNote = currentSound->getMidiRootNote();
    const bool stereoOutput = outputBuffer.getNumChannels() > 1;
    int rendered = 0;
    while (rendered < numSamples && currentSound != nullptr)
    {
        const bool draining = sourceFinished;
        const int count = juce::jmin(FormantShifter::hopSize, numSamples - rendered,
                                    draining ? formantTailRemaining : FormantShifter::hopSize);
        voiceScratch.clear();
        noiseHits.fill(0.0f);
        if (!draining)
            renderSourceBlock(voiceScratch, 0, count);

        auto* left = voiceScratch.getWritePointer(0);
        auto* right = voiceScratch.getWritePointer(1);
        for (int i = 0; i < count; ++i)
        {
            formantShifter.process(left[i], right[i]);
            // Retain the PSOLA option's EQ and saturation coloration.
            formantColouration.process(left[i], right[i]);
            // Narrow the final voice signal, including effect tails, before pan.
            const float monoAmount = sampleMonoAmount.getNextValue();
            if (monoAmount > 0.0f)
            {
                const float mid = 0.5f * left[i] + 0.5f * right[i];
                const float side = (0.5f * left[i] - 0.5f * right[i]) * (1.0f - monoAmount);
                left[i] = mid + side;
                right[i] = mid - side;
            }
            applySamplePan(left[i], right[i], stereoOutput);
            equaliser.process(left[i], right[i]);
            const float noiseHit = noiseTriggerDelay.process(noiseHits[static_cast<size_t>(i)]);
            if (noiseSidechain != nullptr && noiseHit > 0.0f)
                noiseSidechain->addHit(spectrumNote, startSample + rendered + i, noiseHit);
            if (sampleSpectrum != nullptr)
                sampleSpectrum->add(spectrumNote, startSample + rendered + i,
                                    left[i], stereoOutput ? right[i] : left[i]);
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.addSample(ch, startSample + rendered + i,
                    ch == 0 ? left[i] : (ch == 1 ? right[i] : 0.5f * (left[i] + right[i])));
        }
        rendered += count;
        if (draining)
        {
            formantTailRemaining -= count;
            if (formantTailRemaining <= 0)
                clearActivePlayback();
        }
    }
}

void PercussionVoice::finishSourcePlayback() noexcept
{
    playheadSnapshot.store(0, std::memory_order_relaxed);
    sourceFinished = true;
    equaliserTailReserved = equaliser.hasTail();
    formantTailRemaining = formantDrainSamples + (equaliserTailReserved
        ? static_cast<int>(SampleEqualiser::validSampleRate(getSampleRate()) * SampleEqualiser::tailSeconds) : 0);
}

void PercussionVoice::renderSourceBlock(juce::AudioBuffer<float>& outputBuffer,
                                      int startSample,
                                      int numSamples)
{
    if (currentSound == nullptr || numSamples <= 0)
        return;

    if (activeBuffer == nullptr)
    {
        activeBuffer = &currentSound->getAudioData();
        updateActivePlaybackRegion();
    }

    if (currentSound->isWarpEnabled() && sampleSpecificCache != nullptr)
    {
        const bool tapeModeNow = sampleSpecificCache->getLoopTapeModeForMidiNote(
            currentSound->getMidiRootNote());
        if (tapeModeNow != noteTapeMode)
        {
            const double sourceTime = getCurrentOriginalSourceTimeSec();
            const double playbackRate = juce::jmax(1.0, getSampleRate());
            if (tapeModeNow)
            {
                if (switchToOriginalPlaybackFromSourceTime(sourceTime, playbackRate, true))
                    noteTapeMode = true;
            }
            else
            {
                noteTapeMode = false;
                const double pitch = getCurrentSamplePitchRatio();
                const bool needsPitchWarp = !isNeutralPitchRatio(pitch);
                if (shouldTimeWarpForCurrentHost() || needsPitchWarp)
                {
                    const double targetBpm = getCurrentHostBpm();
                    if (!tryUseWarpCache(targetBpm, needsPitchWarp ? pitch : 1.0,
                                         sourceTime, true, true))
                    {
                        const auto& original = currentSound->getAudioData();
                        const double sourceRate = juce::jmax(1.0, currentSound->getSourceSampleRate());
                        const double lastFrame = juce::jmax(0, original.getNumSamples() - 1);
                        const double sourceFrame = std::isfinite(sourceTime)
                            ? juce::jlimit(0.0, lastFrame, sourceTime * sourceRate) : 0.0;
                        const int frame = static_cast<int>(std::floor(sourceFrame));
                        switchToRealtimeWarpFromOriginal(frame, sourceTime,
                            currentSound->warpTimeRatioForTarget(targetBpm),
                            sourceRate, playbackRate, juce::jlimit(1, 2, original.getNumChannels()),
                            pitch, true);
                    }
                }
            }
        }
    }

    double effectiveSamplePitchRatio = notePitchRatio;
    if (!currentSound->isOneShot())
    {
        effectiveSamplePitchRatio = updateWarpLoopPitchDebounce(numSamples);
        maybeSwitchToReadyWarpCache(effectiveSamplePitchRatio);
        maybeSwitchWarpCacheToRealtime(effectiveSamplePitchRatio);
        maybeSwitchLengthPreservedPitchToRealtime(effectiveSamplePitchRatio);
    }

    const auto& data = *activeBuffer;
    const int sourceNumSamples = data.getNumSamples();
    const int sourceNumChans = data.getNumChannels();
    const bool loopWhileHeld = (metadata != nullptr && metadata->loop && isKeyDown());

    if (sourceNumSamples <= 0 || sourceNumChans <= 0 || !adsr.isActive())
    {
        finishSourcePlayback();
        return;
    }

    if (loopWhileHeld)
        updateHeldLoopTrim();

    sampleGain.setTargetValue(getCurrentSampleGain());
    const float sustainAmount = getSustainAmount();
    const float punchAmount = getCurrentSamplePunchAmount();
    const bool hasTransientData = (metadata != nullptr && !metadata->transients.empty());
    const float sustainMakeupGain = SustainTailShaper::getMakeupGain(sustainAmount, hasTransientData);

    if (isRealtimeWarping && realtimeWarpPlayer.isReady() && hostBpmParam != nullptr)
    {
        const double hostBpm = getCurrentHostBpm();
        const auto result = realtimeWarpPlayer.render(outputBuffer,
                                                      startSample,
                                                      numSamples,
                                                      *currentSound,
                                                      data,
                                                      playbackState.activeSourceSampleRate,
                                                      juce::jmax(1.0, getSampleRate()),
                                                      hostBpm,
                                                      loopWhileHeld,
                                                      adsr,
                                                      playbackState.attackSeconds,
                                                      playbackState.releaseSeconds,
                                                      velocityGain,
                                                      sampleGain,
                                                      punchAmount,
                                                      metadata,
                                                      sustainAmount,
                                                      sustainMakeupGain,
                                                      effectiveSamplePitchRatio,
                                                      sustainShaper,
                                                      noteStartDeclicker,
                                                      noiseTrigger, noiseHits.data());

        if (result.finished)
            finishSourcePlayback();
        else
        {
            if (result.wrapped)
            {
                noteTrimStart = nextLoopTrimStart;
                noteTrimEndSeconds = nextLoopTrimEndSeconds;
                playbackState.regionStart = playbackState.nextRegionStart;
                playbackState.regionEnd = playbackState.nextRegionEnd;
            }
            publishPlayhead();
        }

        return;
    }

    updateSampleRendererPitchRatio();

    const auto result = sampleRenderer.render(outputBuffer,
                                             startSample,
                                             numSamples,
                                             data,
                                             playbackState,
                                             juce::jmax(1.0, getSampleRate()),
                                             loopWhileHeld,
                                             adsr,
                                             velocityGain,
                                             sampleGain,
                                             punchAmount,
                                             metadata,
                                             sustainAmount,
                                             sustainMakeupGain,
                                             sustainShaper,
                                             noteStartDeclicker,
                                             noiseTrigger, noiseHits.data());

    if (result.finished)
        finishSourcePlayback();
    else
    {
        if (result.wrapped)
        {
            noteTrimStart = nextLoopTrimStart;
            noteTrimEndSeconds = nextLoopTrimEndSeconds;
        }
        publishPlayhead();
    }
}

void PercussionVoice::publishPlayhead() noexcept
{
    if (currentSound == nullptr || sourceFinished || playheadSequence == 0)
        return;

    const auto& source = currentSound->getAudioData();
    const double sourceRate = currentSound->getSourceSampleRate();
    const double sourceTime = getCurrentOriginalSourceTimeSec();
    if (source.getNumSamples() <= 0 || !std::isfinite(sourceRate) || sourceRate <= 0.0
        || !std::isfinite(sourceTime))
    {
        playheadSnapshot.store(0, std::memory_order_relaxed);
        return;
    }

    const auto [first, last] = getTrimRegion(source.getNumSamples(), sourceRate);
    const double sourceFrame = sourceTime * sourceRate;
    if (last <= first || !std::isfinite(sourceFrame))
    {
        playheadSnapshot.store(0, std::memory_order_relaxed);
        return;
    }
    const auto progress = static_cast<uint64_t>(std::round(juce::jlimit(0.0, 1.0,
        (sourceFrame - first) / (last - first)) * 65534.0));
    const auto note = static_cast<uint64_t>(currentSound->getMidiRootNote() + 1);
    const uint64_t snapshot = ((playheadSequence & 0xffffffffffULL) << 24)
                            | ((note & 0xffULL) << 16) | progress;
    playheadSnapshot.store(snapshot, std::memory_order_relaxed);
}

void PercussionVoice::beginPlayback(float velocity)
{
    sourceFinished = false;
    equaliserTailReserved = false;
    formantTailRemaining = 0;
    const float formantRatio = getCurrentFormantRatio();
    formantShifter.reset(formantRatio);
    formantColouration.reset(formantRatio);
    updateEqualiser(true);
    equaliser.reset();
    activeWarpCache.reset();
    activeBuffer = nullptr;
    metadata = nullptr;
    playbackState = {};
    playbackState.activeSourceSampleRate = 44100.0;
    sustainShaper.setMetadata(nullptr);
    noteStartDeclicker.reset();
    // A subsequent realtime warp start resets its own engine. One-shots never
    // enter Rubber Band here (even when reusing a voice that previously warped).
    resetWarpFlags();

    velocityGain = VelocityLayerGain::calculate(velocity, currentSound);
    const double sampleRate = getSampleRate();
    sampleGain.reset(std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0, 0.01);
    sampleGain.setCurrentAndTargetValue(getCurrentSampleGain());
    sampleMonoAmount.reset(std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0, 0.01);
    sampleMonoAmount.setCurrentAndTargetValue(getCurrentSampleMonoAmount());
    const double panSampleRate = std::isfinite(sampleRate) && sampleRate >= 1000.0
        && sampleRate <= 768000.0 ? sampleRate : 44100.0;
    samplePan.reset(panSampleRate, 0.01);
    samplePan.setCurrentAndTargetValue(getCurrentSamplePan());
    lastPanPosition = 0.0f;
    panNormalization = 1.0;

    if (currentSound == nullptr)
    {
        clearActivePlayback();
        return;
    }

    metadata = currentSound->metadata.get();
    noiseTrigger.start(&currentSound->getNoiseTriggerPoints());
    noiseTriggerDelay.reset();
    sustainShaper.setMetadata(metadata);
    if (warpCachePrewarmer != nullptr && currentSound->isWarpEnabled())
        warpCachePrewarmer->recordPlayed(*currentSound);

    double sourceSampleRate = currentSound->getSourceSampleRate();
    double playbackSampleRate = getSampleRate();

    if (sourceSampleRate <= 0.0)
        sourceSampleRate = 44100.0;
    if (playbackSampleRate <= 0.0)
        playbackSampleRate = 44100.0;

    notePitchRatio = sampleSpecificCache != nullptr
        ? sampleSpecificCache->getPitchRatioForMidiNote(currentSound->getMidiRootNote()) : 1.0;
    noteTapeMode = currentSound->isWarpEnabled() && sampleSpecificCache != nullptr
        && sampleSpecificCache->getLoopTapeModeForMidiNote(currentSound->getMidiRootNote());
    noteTrimStart = sampleSpecificCache != nullptr
        ? sampleSpecificCache->getStartForMidiNote(currentSound->getMidiRootNote()) : 0.0f;
    noteTrimEndSeconds = sampleSpecificCache != nullptr
        ? sampleSpecificCache->getEndTrimSecondsForMidiNote(currentSound->getMidiRootNote()) : 0.0f;
    nextLoopTrimStart = noteTrimStart;
    nextLoopTrimEndSeconds = noteTrimEndSeconds;
    const double samplePitchRatio = notePitchRatio;
    resetWarpLoopPitchDebounce(samplePitchRatio);

    activeBuffer = &currentSound->getAudioData();
    playbackState.activeSourceSampleRate = sourceSampleRate;
    updateActivePlaybackRegion();
    playbackState.sourceSamplePosition = playbackState.regionStart;
    playbackState.pitchRatio = playbackState.activeSourceSampleRate / playbackSampleRate;
    playbackState.currentTimeRatio = 1.0;
    playbackState.usingWarpCache = false;

    const int midiNote = currentSound->getMidiRootNote();
    const float attackMs = sampleSpecificCache != nullptr
        ? sampleSpecificCache->getAttackMsForMidiNote(midiNote)
        : PluginParameters::sampleAttackMsDefault;
    const float releaseMs = sampleSpecificCache != nullptr
        ? sampleSpecificCache->getReleaseMsForMidiNote(midiNote)
        : PluginParameters::sampleReleaseMsDefault;
    adsr.reset();
    adsr.setSampleRate(playbackSampleRate);
    playbackState.attackSeconds = attackMs * 0.001;
    playbackState.releaseSeconds = releaseMs * 0.001;
    // Source-time fades follow trim boundaries; ADSR handles key release.
    adsr.setParameters({ 0.0f, 0.0f, 1.0f, releaseMs * 0.001f });
    adsr.noteOn();

    if (currentSound->isOneShot())
    {
        updateSampleRendererPitchRatio();
        return;
    }

    if (noteTapeMode)
    {
        updateSampleRendererPitchRatio();
        return;
    }

    const bool warpToggle = (warpEnabledParam != nullptr)
                         && warpEnabledParam->load(std::memory_order_relaxed);
    const bool bpmIsMoving = isTargetTempoMoving();
    const double hostBpm = getCurrentHostBpm();
    const double warpBaseBpm = currentSound->warpBaseBpmForTarget(hostBpm);
    const double bpmDiff = std::abs(hostBpm - warpBaseBpm);
    const bool nearOriginalBpm = (hostBpmParam != nullptr && bpmDiff < 0.1);
    const bool shouldTimeWarp = currentSound->isWarpEnabled()
                             && hostBpmParam != nullptr
                             && warpToggle
                             && !nearOriginalBpm;
    const bool shouldPreservePitchLength = currentSound->isWarpEnabled()
                                        && hostBpmParam != nullptr
                                        && warpToggle;
    const bool needsLengthPreservingPitch = shouldPreservePitchLength
                                         && !isNeutralPitchRatio(samplePitchRatio);
    const bool usePitchWarpCache = shouldUsePitchWarpCache(samplePitchRatio);
    const bool canUseOfflineWarpCache = (shouldTimeWarp && !needsLengthPreservingPitch)
                                     || usePitchWarpCache;
    const double offlineWarpCachePitchRatio = usePitchWarpCache ? samplePitchRatio : 1.0;

    isWarping = shouldTimeWarp || needsLengthPreservingPitch;

    if (shouldTimeWarp && bpmIsMoving)
        noteStartDeclicker.trigger();

    if (canUseOfflineWarpCache && metadata != nullptr && hostBpmParam != nullptr)
    {
        if (!tryUseWarpCache(hostBpm,
                             offlineWarpCachePitchRatio,
                             playbackState.sourceSamplePosition / sourceSampleRate,
                             true,
                             false))
        {
            isRealtimeWarping = true;
        }
    }
    else if (shouldTimeWarp || needsLengthPreservingPitch)
    {
        isRealtimeWarping = true;
    }

    if (isRealtimeWarping && activeBuffer != nullptr)
    {
        const int channels = juce::jlimit(1, 2, activeBuffer->getNumChannels());
        const double hostBpmAtNoteStart = getCurrentHostBpm();
        playbackState.currentTimeRatio = currentSound->warpTimeRatioForTarget(hostBpmAtNoteStart);

        if (!realtimeWarpPlayer.start(playbackState.regionStart,
                                      playbackState.sourceSamplePosition / sourceSampleRate,
                                      playbackState.regionStart,
                                      playbackState.regionEnd,
                                      playbackState.currentTimeRatio,
                                      playbackState.activeSourceSampleRate,
                                      playbackSampleRate,
                                      channels,
                                      samplePitchRatio))
            isRealtimeWarping = false;
    }
}

void PercussionVoice::clearActivePlayback()
{
    playheadSnapshot.store(0, std::memory_order_relaxed);
    playheadSequence = 0;
    sourceFinished = false;
    equaliserTailReserved = false;
    formantTailRemaining = 0;
    clearCurrentNote();

    currentSound = nullptr;
    noiseTrigger.start(nullptr);
    noiseTriggerDelay.reset();
    activeWarpCache.reset();
    activeBuffer = nullptr;
    metadata = nullptr;

    playbackState = {};
    sustainShaper.setMetadata(nullptr);
    noteStartDeclicker.reset();
    notePitchRatio = 1.0;
    noteTapeMode = false;
    noteTrimStart = 0.0f;
    noteTrimEndSeconds = 0.0f;
    nextLoopTrimStart = 0.0f;
    nextLoopTrimEndSeconds = 0.0f;
    resetWarpLoopPitchDebounce(1.0);
    resetWarpFlags();
}

void PercussionVoice::resetWarpFlags()
{
    isWarping = false;
    isRealtimeWarping = false;
    playbackState.usingWarpCache = false;
    playbackState.currentTimeRatio = 1.0;
}

void PercussionVoice::maybeSwitchToReadyWarpCache(double effectiveSamplePitchRatio)
{
    if (currentSound == nullptr
        || !currentSound->isWarpEnabled()
        || !shouldPreserveLengthForPitch())
    {
        return;
    }

    const bool hostBpmIsMoving = isTargetTempoMoving();
    if (hostBpmIsMoving)
        return;

    const bool needsLengthPreservingPitch = !isNeutralPitchRatio(effectiveSamplePitchRatio);
    const double cachePitchRatio = needsLengthPreservingPitch ? effectiveSamplePitchRatio : 1.0;
    const bool activeCacheMatches = playbackState.usingWarpCache
                                 && activeWarpCache
                                 && std::abs(activeWarpCache->bpm
                                           - PercussionSound::quantizeWarpBpm(getCurrentHostBpm())) <= 0.005
                                 && !pitchRatiosDiffer(activeWarpCache->pitchRatio,
                                      PercussionSound::quantizeWarpPitchRatio(cachePitchRatio));
    if (activeCacheMatches)
        return;

    if (!needsLengthPreservingPitch)
    {
        if (!shouldTimeWarpForCurrentHost())
            return;

        const bool activeCacheNeedsNeutralPitch = playbackState.usingWarpCache
                                              && activeWarpCache
                                              && pitchRatiosDiffer(activeWarpCache->pitchRatio, 1.0);
        if (!isRealtimeWarping && !activeCacheNeedsNeutralPitch)
            return;
    }

    tryUseWarpCache(getCurrentHostBpm(),
                    cachePitchRatio,
                    getCurrentOriginalSourceTimeSec(),
                    needsLengthPreservingPitch,
                    true);
}

void PercussionVoice::maybeSwitchWarpCacheToRealtime(double effectiveSamplePitchRatio)
{
    const double hostBpmNow = getCurrentHostBpm();
    const double quantizedHostBpm = PercussionSound::quantizeWarpBpm(hostBpmNow);
    const bool cacheBpmMismatch = (activeWarpCache == nullptr)
                               || (std::abs(activeWarpCache->bpm - quantizedHostBpm) > 0.005);

    if (!playbackState.usingWarpCache || isRealtimeWarping || !cacheBpmMismatch)
        return;

    const double cacheRatio = (activeWarpCache != nullptr)
                                ? activeWarpCache->timeRatio
                                : juce::jmax(1e-9, playbackState.currentTimeRatio);
    const double cacheSampleRate = juce::jmax(1e-9, playbackState.activeSourceSampleRate);
    const double warpedTimeSec = playbackState.sourceSamplePosition / cacheSampleRate;
    const double sourceTimeSec = warpedTimeSec / juce::jmax(1e-9, cacheRatio);

    const auto& originalData = currentSound->getAudioData();
    const int originalNumSamples = originalData.getNumSamples();
    if (originalNumSamples <= 0)
        return;

    const double sourceSampleRate = juce::jmax(1.0, currentSound->getSourceSampleRate());
    const int startSourceSample = juce::jlimit(0,
                                               juce::jmax(0, originalNumSamples - 1),
                                               (int) std::floor(sourceTimeSec * sourceSampleRate));
    const double playbackSampleRate = juce::jmax(1.0, getSampleRate());
    const int channels = juce::jlimit(1, 2, originalData.getNumChannels());
    const double timeRatio = currentSound->warpTimeRatioForTarget(hostBpmNow);

    switchToRealtimeWarpFromOriginal(startSourceSample,
                                     sourceTimeSec,
                                     timeRatio,
                                     sourceSampleRate,
                                     playbackSampleRate,
                                     channels,
                                     effectiveSamplePitchRatio,
                                     true);
}

void PercussionVoice::maybeSwitchLengthPreservedPitchToRealtime(double effectiveSamplePitchRatio)
{
    if (currentSound == nullptr
        || isRealtimeWarping
        || !shouldPreserveLengthForPitch())
    {
        return;
    }

    const bool pitchIsNeutral = isNeutralPitchRatio(effectiveSamplePitchRatio);
    const double targetCachePitchRatio = pitchIsNeutral ? 1.0 : effectiveSamplePitchRatio;
    const bool activeCacheMatchesPitch = playbackState.usingWarpCache
                                      && activeWarpCache
                                      && !pitchRatiosDiffer(activeWarpCache->pitchRatio,
                                                           PercussionSound::quantizeWarpPitchRatio(targetCachePitchRatio));

    if (pitchIsNeutral)
    {
        if (!playbackState.usingWarpCache || activeCacheMatchesPitch)
            return;

        if (!shouldTimeWarpForCurrentHost())
        {
            switchToOriginalPlaybackFromSourceTime(getCurrentOriginalSourceTimeSec(),
                                                   juce::jmax(1.0, getSampleRate()),
                                                   true);
            return;
        }
    }
    else if (activeCacheMatchesPitch && shouldUsePitchWarpCache(effectiveSamplePitchRatio))
    {
        return;
    }

    const double hostBpmNow = getCurrentHostBpm();
    const double playbackSampleRate = juce::jmax(1.0, getSampleRate());
    const double timeRatio = currentSound->warpTimeRatioForTarget(hostBpmNow);

    if (playbackState.usingWarpCache)
    {
        const double cacheRatio = (activeWarpCache != nullptr)
                                    ? activeWarpCache->timeRatio
                                    : juce::jmax(1e-9, playbackState.currentTimeRatio);
        const double cacheSampleRate = juce::jmax(1e-9, playbackState.activeSourceSampleRate);
        const double warpedTimeSec = playbackState.sourceSamplePosition / cacheSampleRate;
        const double sourceTimeSec = warpedTimeSec / juce::jmax(1e-9, cacheRatio);
        const auto& originalData = currentSound->getAudioData();
        const double sourceSampleRate = juce::jmax(1.0, currentSound->getSourceSampleRate());
        const int startSourceSample = juce::jlimit(0,
                                                   juce::jmax(0, originalData.getNumSamples() - 1),
                                                   (int) std::floor(sourceTimeSec * sourceSampleRate));

        switchToRealtimeWarpFromOriginal(startSourceSample,
                                         sourceTimeSec,
                                         timeRatio,
                                         sourceSampleRate,
                                         playbackSampleRate,
                                         juce::jlimit(1, 2, originalData.getNumChannels()),
                                         effectiveSamplePitchRatio,
                                         true);
        return;
    }

    const auto& originalData = currentSound->getAudioData();
    const double sourceSampleRate = juce::jmax(1.0, currentSound->getSourceSampleRate());
    const int startSourceSample = juce::jlimit(0,
                                               juce::jmax(0, originalData.getNumSamples() - 1),
                                               (int) std::floor(playbackState.sourceSamplePosition));
    const double sourceTimeSec = static_cast<double>(startSourceSample) / sourceSampleRate;

    switchToRealtimeWarpFromOriginal(startSourceSample,
                                     sourceTimeSec,
                                     timeRatio,
                                     sourceSampleRate,
                                     playbackSampleRate,
                                     juce::jlimit(1, 2, originalData.getNumChannels()),
                                     effectiveSamplePitchRatio,
                                     true);
}

bool PercussionVoice::tryUseWarpCache(double hostBpm,
                                      double samplePitchRatio,
                                      double sourceTimeSec,
                                      bool requestIfMissing,
                                      bool triggerDeclick)
{
    if (currentSound == nullptr)
        return false;

    auto cache = warpCachePrewarmer != nullptr
        ? warpCachePrewarmer->acquire(*currentSound, hostBpm, samplePitchRatio, requestIfMissing)
        : WarpCachePrewarmer::Lease {};
    if (!cache)
        return false;

    if (playbackState.usingWarpCache && activeWarpCache.get() == cache.get())
        return true;

    return switchToWarpCache(std::move(cache),
                             sourceTimeSec,
                             juce::jmax(1.0, getSampleRate()),
                             triggerDeclick);
}

bool PercussionVoice::switchToWarpCache(WarpCachePrewarmer::Lease cache,
                                        double sourceTimeSec,
                                        double playbackSampleRate,
                                        bool triggerDeclick)
{
    if (!cache || cache->buffer.getNumSamples() <= 0 || cache->buffer.getNumChannels() <= 0)
        return false;

    activeWarpCache = std::move(cache);
    activeBuffer = &activeWarpCache->buffer;
    updateActivePlaybackRegion();

    const double cacheSampleRate = juce::jmax(1.0, activeWarpCache->sourceSampleRate);
    const double cacheTimeSec = juce::jmax(0.0, sourceTimeSec)
                              * juce::jmax(1e-9, activeWarpCache->timeRatio);
    const double maxPosition = playbackState.regionEnd - 1;

    playbackState.activeSourceSampleRate = cacheSampleRate;
    playbackState.sourceSamplePosition = juce::jlimit((double) playbackState.regionStart,
                                                      maxPosition,
                                                      cacheTimeSec * cacheSampleRate);
    playbackState.pitchRatio = playbackState.activeSourceSampleRate / juce::jmax(1.0, playbackSampleRate);
    playbackState.currentTimeRatio = activeWarpCache->timeRatio;
    playbackState.usingWarpCache = true;
    isWarping = true;
    isRealtimeWarping = false;
    // The inactive realtime engine is reset by start() if it is needed again.
    // Resetting it here would clear/reconfigure Rubber Band on every cached hit.

    if (triggerDeclick)
        noteStartDeclicker.trigger();

    return true;
}

bool PercussionVoice::switchToOriginalPlaybackFromSourceTime(double sourceTimeSec,
                                                             double playbackSampleRate,
                                                             bool triggerDeclick)
{
    if (currentSound == nullptr || !std::isfinite(sourceTimeSec))
        return false;

    const auto& originalData = currentSound->getAudioData();
    if (originalData.getNumSamples() <= 0 || originalData.getNumChannels() <= 0)
        return false;

    const double sourceSampleRate = juce::jmax(1.0, currentSound->getSourceSampleRate());
    activeWarpCache.reset();
    activeBuffer = &originalData;
    updateActivePlaybackRegion();
    const double maxPosition = playbackState.regionEnd - 1;
    // RealtimeWarpPlayer::start() resets the engine before its next use.

    playbackState.activeSourceSampleRate = sourceSampleRate;
    playbackState.sourceSamplePosition = juce::jlimit((double) playbackState.regionStart,
                                                      maxPosition,
                                                      juce::jmax(0.0, sourceTimeSec) * sourceSampleRate);
    playbackState.pitchRatio = playbackState.activeSourceSampleRate / juce::jmax(1.0, playbackSampleRate);
    playbackState.currentTimeRatio = 1.0;
    playbackState.usingWarpCache = false;
    isWarping = false;
    isRealtimeWarping = false;

    if (triggerDeclick)
        noteStartDeclicker.trigger();

    return true;
}

bool PercussionVoice::switchToRealtimeWarpFromOriginal(int startSourceSample,
                                                       double sourceTimeSec,
                                                       double timeRatio,
                                                       double sourceSampleRate,
                                                       double playbackSampleRate,
                                                       int channels,
                                                       double samplePitchRatio,
                                                       bool triggerDeclick)
{
    if (currentSound == nullptr || !std::isfinite(sourceSampleRate)
        || sourceSampleRate <= 0.0 || !std::isfinite(sourceTimeSec))
        return false;

    const auto& originalData = currentSound->getAudioData();
    const auto [regionStart, regionEnd] = getTrimRegion(originalData.getNumSamples(), sourceSampleRate);
    if (regionEnd <= regionStart)
        return false;
    startSourceSample = juce::jlimit(regionStart, regionEnd - 1, startSourceSample);
    sourceTimeSec = juce::jlimit(regionStart / sourceSampleRate,
                                 (regionEnd - 1) / sourceSampleRate, sourceTimeSec);
    if (!realtimeWarpPlayer.start(startSourceSample,
                                  sourceTimeSec,
                                  regionStart,
                                  regionEnd,
                                  timeRatio,
                                  sourceSampleRate,
                                  playbackSampleRate,
                                  channels,
                                  samplePitchRatio))
    {
        return false;
    }

    activeWarpCache.reset();
    activeBuffer = &originalData;
    updateActivePlaybackRegion();
    playbackState.activeSourceSampleRate = sourceSampleRate;
    playbackState.sourceSamplePosition = static_cast<double>(startSourceSample);
    playbackState.pitchRatio = playbackState.activeSourceSampleRate / playbackSampleRate;
    playbackState.currentTimeRatio = timeRatio;
    playbackState.usingWarpCache = false;
    isWarping = true;
    isRealtimeWarping = true;

    if (triggerDeclick)
        noteStartDeclicker.trigger();

    return true;
}

double PercussionVoice::updateWarpLoopPitchDebounce(int numSamples)
{
    const double currentPitchRatio = getCurrentSamplePitchRatio();

    if (!shouldDebounceWarpLoopPitch())
    {
        resetWarpLoopPitchDebounce(currentPitchRatio);
        return currentPitchRatio;
    }

    if (pitchRatiosDiffer(currentPitchRatio, pendingWarpLoopPitchRatio))
    {
        pendingWarpLoopPitchRatio = currentPitchRatio;
        warpLoopPitchDebounceSamplesRemaining = getWarpLoopPitchDebounceSampleCount();
    }
    else if (warpLoopPitchDebounceSamplesRemaining > 0)
    {
        warpLoopPitchDebounceSamplesRemaining = juce::jmax(0,
                                                           warpLoopPitchDebounceSamplesRemaining
                                                            - juce::jmax(0, numSamples));

        if (warpLoopPitchDebounceSamplesRemaining == 0
            && pitchRatiosDiffer(appliedWarpLoopPitchRatio, pendingWarpLoopPitchRatio))
        {
            appliedWarpLoopPitchRatio = pendingWarpLoopPitchRatio;
            noteStartDeclicker.trigger();
        }
    }

    return appliedWarpLoopPitchRatio;
}

void PercussionVoice::resetWarpLoopPitchDebounce(double pitchRatio) noexcept
{
    appliedWarpLoopPitchRatio = pitchRatio;
    pendingWarpLoopPitchRatio = pitchRatio;
    warpLoopPitchDebounceSamplesRemaining = 0;
}

float PercussionVoice::getSustainAmount() const noexcept
{
    const float amount = (sustainAmountParam != nullptr)
                           ? sustainAmountParam->load(std::memory_order_relaxed)
                           : 0.0f;
    return std::isfinite(amount) ? juce::jlimit(0.0f, 1.0f, amount) : 0.0f;
}

float PercussionVoice::getCurrentFormantRatio() const noexcept
{
    return sampleSpecificCache != nullptr && currentSound != nullptr
        ? sampleSpecificCache->getFormantRatioForMidiNote(currentSound->getMidiRootNote()) : 1.0f;
}

float PercussionVoice::getCurrentSampleGain() const noexcept
{
    return sampleSpecificCache != nullptr && currentSound != nullptr
        ? sampleSpecificCache->getGainLinearForMidiNote(currentSound->getMidiRootNote()) : 1.0f;
}

float PercussionVoice::getCurrentSampleMonoAmount() const noexcept
{
    return sampleSpecificCache != nullptr && currentSound != nullptr
        ? sampleSpecificCache->getMonoAmountForMidiNote(currentSound->getMidiRootNote()) : 0.0f;
}

float PercussionVoice::getCurrentSamplePan() const noexcept
{
    return sampleSpecificCache != nullptr && currentSound != nullptr
        ? sampleSpecificCache->getPanForMidiNote(currentSound->getMidiRootNote()) / 50.0f : 0.0f;
}

void PercussionVoice::applySamplePan(float& left, float& right, bool stereoOutput) noexcept
{
    const float position = juce::jlimit(-1.0f, 1.0f, samplePan.getNextValue());
    // Advance smoothing on mono buses too, but don't pan away their only output.
    if (!stereoOutput || position == 0.0f)
        return;

    const double pan = position;
    if (position != lastPanPosition)
    {
        // Normalized crossfeed: for L == R, the two output powers sum to the
        // same value at every position. Steady pan needs no square root.
        panNormalization = 1.0 / std::sqrt(1.0 + pan * pan);
        lastPanPosition = position;
    }

    const double inputL = left;
    const double inputR = right;
    const float outputL = static_cast<float>(panNormalization
        * (pan < 0.0 ? inputL - pan * inputR : (1.0 - pan) * inputL));
    const float outputR = static_cast<float>(panNormalization
        * (pan > 0.0 ? inputR + pan * inputL : (1.0 + pan) * inputR));
    left = std::isfinite(outputL) ? outputL : 0.0f;
    right = std::isfinite(outputR) ? outputR : 0.0f;
}

float PercussionVoice::getCurrentSamplePunchAmount() const noexcept
{
    if (currentSound == nullptr || sampleSpecificCache == nullptr)
        return 0.0f;

    return sampleSpecificCache->getPunchAmountForMidiNote(currentSound->getMidiRootNote());
}

void PercussionVoice::updateSampleRendererPitchRatio() noexcept
{
    const double playbackSampleRate = juce::jmax(1.0, getSampleRate());
    double pitchRatio = 1.0;

    if (!shouldPreserveLengthForPitch())
        pitchRatio = getCurrentSamplePitchRatio();

    if (noteTapeMode && currentSound != nullptr)
        pitchRatio *= getCurrentHostBpm() / juce::jmax(1.0, currentSound->getOriginalBpm());

    playbackState.pitchRatio = (playbackState.activeSourceSampleRate / playbackSampleRate) * pitchRatio;
}

std::pair<int, int> PercussionVoice::getTrimRegion(int sourceSamples,
                                                   double framesPerOriginalSecond) const noexcept
{
    return getTrimRegion(sourceSamples, framesPerOriginalSecond,
                         noteTrimStart, noteTrimEndSeconds);
}

std::pair<int, int> PercussionVoice::getTrimRegion(int sourceSamples,
                                                   double framesPerOriginalSecond,
                                                   float trimStart,
                                                   float endTrimSeconds) const noexcept
{
    if (sourceSamples <= 0)
        return { 0, 0 };

    const double start = std::isfinite(trimStart) ? juce::jlimit(0.0, 1.0,
        static_cast<double>(trimStart)) : 0.0;
    const int first = static_cast<int>(juce::jlimit(0.0,
        static_cast<double>(sourceSamples - 1), std::floor(start * sourceSamples)));
    const double seconds = std::isfinite(endTrimSeconds)
        ? juce::jmax(0.0, static_cast<double>(endTrimSeconds)) : 0.0;
    const double rate = std::isfinite(framesPerOriginalSecond)
        ? juce::jmax(1.0, framesPerOriginalSecond) : 1.0;
    const double removedFrames = seconds * rate;
    const int removed = static_cast<int>(std::isfinite(removedFrames)
        ? juce::jlimit(0.0, static_cast<double>(sourceSamples), std::ceil(removedFrames))
        : static_cast<double>(sourceSamples));
    const int last = juce::jmax(first + 1, sourceSamples - removed);
    return { first, last };
}

void PercussionVoice::updateActivePlaybackRegion() noexcept
{
    double framesPerOriginalSecond = currentSound != nullptr
        ? currentSound->getSourceSampleRate() : 1.0;
    if (activeWarpCache != nullptr && activeBuffer == &activeWarpCache->buffer)
        framesPerOriginalSecond = activeWarpCache->sourceSampleRate * activeWarpCache->timeRatio;
    const auto [start, end] = getTrimRegion(activeBuffer != nullptr
        ? activeBuffer->getNumSamples() : 0, framesPerOriginalSecond);
    playbackState.regionStart = start;
    playbackState.regionEnd = end;
    playbackState.nextRegionStart = start;
    playbackState.nextRegionEnd = end;
}

void PercussionVoice::updateHeldLoopTrim() noexcept
{
    if (currentSound == nullptr || activeBuffer == nullptr || sampleSpecificCache == nullptr)
        return;

    const int note = currentSound->getMidiRootNote();
    nextLoopTrimStart = sampleSpecificCache->getStartForMidiNote(note);
    nextLoopTrimEndSeconds = sampleSpecificCache->getEndTrimSecondsForMidiNote(note);

    const double originalFramesPerSecond = activeWarpCache != nullptr
        && activeBuffer == &activeWarpCache->buffer
        ? activeWarpCache->sourceSampleRate * activeWarpCache->timeRatio
        : currentSound->getSourceSampleRate();
    const int sourceSamples = activeBuffer->getNumSamples();
    const auto [nextStart, nextEnd] = getTrimRegion(sourceSamples, originalFramesPerSecond,
                                                    nextLoopTrimStart, nextLoopTrimEndSeconds);
    playbackState.nextRegionStart = nextStart;
    playbackState.nextRegionEnd = nextEnd;

    // Keep the start of this pass. Move its end only while the new boundary is
    // still ahead of audible playback; otherwise adopt it at the next wrap.
    const int currentEnd = getTrimRegion(sourceSamples, originalFramesPerSecond,
                                         noteTrimStart, nextLoopTrimEndSeconds).second;
    const double position = isRealtimeWarping
        ? juce::jmax(realtimeWarpPlayer.getCurrentSourceTimeSec() * originalFramesPerSecond,
                     static_cast<double>(realtimeWarpPlayer.getFedSourceFrame()))
        : playbackState.sourceSamplePosition;
    if (currentEnd > position)
    {
        playbackState.regionEnd = currentEnd;
        noteTrimEndSeconds = nextLoopTrimEndSeconds;
    }

    if (isRealtimeWarping)
        realtimeWarpPlayer.setLoopTrim(nextStart, nextEnd, playbackState.regionEnd);
}

double PercussionVoice::getCurrentSamplePitchRatio() const noexcept
{
    if (currentSound != nullptr && currentSound->isOneShot())
        return notePitchRatio;

    if (currentSound == nullptr || sampleSpecificCache == nullptr)
        return 1.0;

    return sampleSpecificCache->getPitchRatioForMidiNote(currentSound->getMidiRootNote());
}

double PercussionVoice::getCurrentHostBpm() const noexcept
{
    if (currentSound != nullptr && currentSound->isWarpEnabled()
        && sampleSpecificCache != nullptr)
    {
        const float target = sampleSpecificCache->getLoopBpmForMidiNote(currentSound->getMidiRootNote());
        if (target >= 1.0f)
            return target;
    }
    if (hostBpmParam != nullptr)
        return juce::jmax(1.0, hostBpmParam->load(std::memory_order_relaxed));

    return (currentSound != nullptr) ? currentSound->getOriginalBpm() : 1.0;
}

bool PercussionVoice::isTargetTempoMoving() const noexcept
{
    if (currentSound != nullptr && currentSound->isWarpEnabled()
        && sampleSpecificCache != nullptr
        && sampleSpecificCache->getLoopBpmForMidiNote(currentSound->getMidiRootNote()) >= 1.0f)
        return false;
    return hostBpmMovingParam != nullptr
        && hostBpmMovingParam->load(std::memory_order_relaxed);
}

double PercussionVoice::getCurrentOriginalSourceTimeSec() const noexcept
{
    if (currentSound == nullptr)
        return 0.0;

    if (isRealtimeWarping)
        return realtimeWarpPlayer.getCurrentSourceTimeSec();

    const double activeSampleRate = juce::jmax(1.0, playbackState.activeSourceSampleRate);
    const double playbackTimeSec = playbackState.sourceSamplePosition / activeSampleRate;

    if (playbackState.usingWarpCache)
        return playbackTimeSec / juce::jmax(1e-9, playbackState.currentTimeRatio);

    return playbackTimeSec;
}

bool PercussionVoice::shouldTimeWarpForCurrentHost() const noexcept
{
    if (currentSound == nullptr
        || noteTapeMode
        || !currentSound->isWarpEnabled()
        || hostBpmParam == nullptr
        || warpEnabledParam == nullptr
        || !warpEnabledParam->load(std::memory_order_relaxed))
    {
        return false;
    }

    const double hostBpm = getCurrentHostBpm();
    const double warpBaseBpm = currentSound->warpBaseBpmForTarget(hostBpm);
    return std::abs(hostBpm - warpBaseBpm) >= 0.1;
}

bool PercussionVoice::shouldPreserveLengthForPitch() const noexcept
{
    return currentSound != nullptr
        && !noteTapeMode
        && currentSound->isWarpEnabled()
        && hostBpmParam != nullptr
        && warpEnabledParam != nullptr
        && warpEnabledParam->load(std::memory_order_relaxed);
}

bool PercussionVoice::shouldDebounceWarpLoopPitch() const noexcept
{
    return shouldPreserveLengthForPitch()
        && metadata != nullptr
        && metadata->loop;
}

bool PercussionVoice::shouldUsePitchWarpCache(double samplePitchRatio) const noexcept
{
    // Cache eligibility depends on warp, independently of looping/debounce.
    return currentSound != nullptr
        && currentSound->isWarpEnabled()
        && shouldPreserveLengthForPitch()
        && !isNeutralPitchRatio(samplePitchRatio);
}

int PercussionVoice::getWarpLoopPitchDebounceSampleCount() const noexcept
{
    const double sampleRate = juce::jmax(1.0, getSampleRate());
    return juce::jmax(1, (int) std::ceil(warpLoopPitchDebounceSeconds * sampleRate));
}

void PercussionVoice::updateEqualiser(bool immediate) noexcept
{
    if (currentSound == nullptr || sampleSpecificCache == nullptr)
        return;
    const int note = currentSound->getMidiRootNote();
    for (int band = 0; band < SampleEqualiser::bandCount; ++band)
        equaliser.setBand(band, sampleSpecificCache->getEqFrequencyForMidiNote(note, band),
                         sampleSpecificCache->getEqGainForMidiNote(note, band), immediate);
}
