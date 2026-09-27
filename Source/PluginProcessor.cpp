#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/PluginParameters.h"
#include "PercussionVoice.h"
#include "SampleLibrary/PercussionSampleLibrary.h"

#include <cmath>
#include <cstring>
#include <limits>

//==============================================================================
namespace
{
    constexpr int percussionVoiceCount = 8;
    constexpr double percussionOriginalBpm = 153.0;

    const juce::Identifier selectedSampleGroupIndexProperty { "selectedSampleGroupIndex" };
    const juce::Identifier selectedSampleGroupNoteIndexProperty { "selectedSampleGroupNoteIndex" };
    const juce::Identifier selectedSampleGroupPitchIndexProperty { "selectedSampleGroupPitchIndex" };
    const juce::Identifier noiseSourceNameProperty { "noiseSourceName" };
    const juce::Identifier noiseImportedIndexProperty { "noiseImportedIndex" };
    const juce::Identifier noiseUsesImportedProperty { "noiseUsesImportedSource" };
    const juce::Identifier noiseTriggerNoteProperty { "noiseTriggerNoteIndex" };
    const juce::Identifier noiseTriggerPitchProperty { "noiseTriggerPitchIndex" };

    int findSampleGroupIndexForKey(const std::vector<PercussionSampleLibrary::SampleGroupInfo>& groups,
                                   int noteIndex,
                                   int pitchIndex) noexcept
    {
        for (int i = 0; i < static_cast<int>(groups.size()); ++i)
        {
            const auto& group = groups[(size_t) i];
            if (group.noteIndex == noteIndex && group.pitchIndex == pitchIndex)
                return i;
        }

        return -1;
    }
}

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, PluginParameters::stateType, PluginParameters::createParameterLayout())
{
    DBG("=== AudioPluginAudioProcessor constructor ===");

    rzhavParam = parameters.getRawParameterValue(PluginParameters::rzhavchinaId);
    ottAmountParam = parameters.getRawParameterValue(PluginParameters::ottAmountId);
    noiseSourceParam = parameters.getRawParameterValue(PluginParameters::noiseSourceId);
    noiseGainParam = parameters.getRawParameterValue(PluginParameters::noiseGainDbId);
    noiseModulationParam = parameters.getRawParameterValue(PluginParameters::noiseModulationId);
    noiseTriggerParam = parameters.getRawParameterValue(PluginParameters::noiseTriggerId);
    noiseReleaseParam = parameters.getRawParameterValue(PluginParameters::noiseReleaseMsId);
    noiseStartParam = parameters.getRawParameterValue(PluginParameters::noiseStartId);
    noiseEndParam = parameters.getRawParameterValue(PluginParameters::noiseEndId);
    sustainShortenParam = parameters.getRawParameterValue(PluginParameters::sustainShortenId);
    warpParamRaw = parameters.getRawParameterValue(PluginParameters::warpEnabledId);

    sampler.clearVoices();
    addPercussionVoices();
    DBG("Added " << sampler.getNumVoices() << " sampler voices");

    PercussionSampleLibrary::loadEmbeddedSamples(sampler, percussionOriginalBpm, &sampleGroups);
    for (const auto& group : sampleGroups)
        midiNoteActivity.enableSampleNote(group.midiNote);
    clampSelectedSampleGroupIndex();
    rebuildSampleSpecificCache();
    warpCachePrewarmer = std::make_unique<WarpCachePrewarmer>(
        sampler, sampleSpecificCache, warpEnabledAtomic, *hostTempo.getBpmAtomic());
    updateVoiceSharedState();
    for (const auto& parameter : PluginParameters::sampleSpecificParameters)
        parameters.addParameterListener(parameter.id, this);
    DBG("=== Constructor done ===");
}

void AudioPluginAudioProcessor::addPercussionVoices()
{
    for (int i = 0; i < percussionVoiceCount; ++i)
    {
        auto* v = new PercussionVoice();
        v->setSustainParam(sustainShortenParam);
        v->setWarpEnabledParam(&warpEnabledAtomic);
        v->setHostBpmParam(hostTempo.getBpmAtomic());
        v->setHostBpmMovingParam(hostTempo.getMovingAtomic());
        v->setSampleSpecificCache(&sampleSpecificCache);
        sampler.addVoice(v);
    }
}

void AudioPluginAudioProcessor::updateVoiceSharedState()
{
    for (int i = 0; i < sampler.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<PercussionVoice*>(sampler.getVoice(i)))
        {
            v->setSustainParam(sustainShortenParam);
            v->setWarpEnabledParam(&warpEnabledAtomic);
            v->setHostBpmParam(hostTempo.getBpmAtomic());
            v->setHostBpmMovingParam(hostTempo.getMovingAtomic());
            v->setSampleSpecificCache(&sampleSpecificCache);
            v->setSampleSpectrum(&sampleSpectrum);
            v->setNoiseSidechain(&noiseSidechain);
            v->setWarpCachePrewarmer(warpCachePrewarmer.get());
        }
    }
}

const std::vector<PercussionSampleLibrary::SampleGroupInfo>& AudioPluginAudioProcessor::getSampleGroups() const noexcept
{
    return sampleGroups;
}

int AudioPluginAudioProcessor::getSelectedSampleGroupIndex() const noexcept
{
    return selectedSampleGroupIndex.load(std::memory_order_relaxed);
}

void AudioPluginAudioProcessor::setSelectedSampleGroupIndex(int groupIndex)
{
    if (sampleGroups.empty())
    {
        selectedSampleGroupIndex.store(-1, std::memory_order_relaxed);
        return;
    }

    selectedSampleGroupIndex.store(juce::jlimit(0,
                                                static_cast<int>(sampleGroups.size()) - 1,
                                                groupIndex),
                                   std::memory_order_relaxed);
    synchroniseSelectedSampleParameters();
}

float AudioPluginAudioProcessor::getMidiNoteActivityVelocity(int midiNote) const noexcept
{
    return midiNoteActivity.getVelocityForMidiNote(midiNote);
}

uint32_t AudioPluginAudioProcessor::getMidiNoteActivityGeneration(int midiNote) const noexcept
{
    return midiNoteActivity.getGenerationForMidiNote(midiNote);
}

int AudioPluginAudioProcessor::getLatestMidiNoteOnNote() const noexcept
{
    return midiNoteActivity.getLatestNoteOnMidiNote();
}

float AudioPluginAudioProcessor::getSampleSpecificParameterValue(const juce::String& parameterId,
                                                                 float fallbackValue) const
{
    const auto* definition = PluginParameters::findSampleSpecificParameter(parameterId);
    if (definition == nullptr)
        return fallbackValue;

    const int groupIndex = getSelectedSampleGroupIndex();
    if (groupIndex < 0 || groupIndex >= static_cast<int>(sampleGroups.size()))
        return fallbackValue;

    const auto& sampleGroup = sampleGroups[(size_t) groupIndex];

    // Atomics are authoritative, including automation received without an editor.
    return definition->readCache(sampleSpecificCache, sampleGroup.midiNote);
}

void AudioPluginAudioProcessor::setSampleSpecificParameterValue(const juce::String& parameterId,
                                                                float value)
{
    const auto* definition = PluginParameters::findSampleSpecificParameter(parameterId);
    if (definition == nullptr || !std::isfinite(value))
        return;

    const int groupIndex = getSelectedSampleGroupIndex();
    if (groupIndex < 0 || groupIndex >= static_cast<int>(sampleGroups.size()))
        return;

    const auto& sampleGroup = sampleGroups[(size_t) groupIndex];

    definition->writeCache(sampleSpecificCache, sampleGroup.midiNote, value);
    sampleSpecificParameters.setValue(parameterId, sampleGroup, groupIndex,
        definition->readCache(sampleSpecificCache, sampleGroup.midiNote));
}

bool AudioPluginAudioProcessor::applySampleSpecificParameterToAll(const juce::String& parameterId,
                                                                  float value)
{
    const auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    jassert(messageManager != nullptr && messageManager->isThisTheMessageThread());
    if (messageManager == nullptr || !messageManager->isThisTheMessageThread())
        return false;

    const auto* definition = PluginParameters::findSampleSpecificParameter(parameterId);
    auto* parameter = parameters.getParameter(parameterId);
    if (definition == nullptr || parameter == nullptr || sampleGroups.empty() || !std::isfinite(value))
        return false;

    // Snapshot every member before publication: multi-parameter effects copy
    // the complete selected curve through the same registry as scalar effects.
    const auto belongsToSelectedEffect = [definition] (const auto& member) noexcept
    {
        if (std::strcmp(member.id, definition->id) == 0)
            return true;
        return definition->effectId != nullptr && member.effectId != nullptr
            && std::strcmp(member.effectId, definition->effectId) == 0;
    };
    std::array<float, PluginParameters::sampleSpecificParameters.size()> values {};
    for (size_t i = 0; i < PluginParameters::sampleSpecificParameters.size(); ++i)
    {
        const auto& member = PluginParameters::sampleSpecificParameters[i];
        if (!belongsToSelectedEffect(member))
            continue;
        if (auto* memberParameter = parameters.getParameter(member.id))
        {
            const float source = std::strcmp(member.id, definition->id) == 0 ? value
                : getSampleSpecificParameterValue(member.id,
                    memberParameter->convertFrom0to1(memberParameter->getDefaultValue()));
            values[i] = memberParameter->convertFrom0to1(memberParameter->convertTo0to1(source));
        }
    }
    for (size_t i = 0; i < PluginParameters::sampleSpecificParameters.size(); ++i)
    {
        const auto& member = PluginParameters::sampleSpecificParameters[i];
        if (!belongsToSelectedEffect(member))
            continue;
        for (int groupIndex = 0; groupIndex < static_cast<int>(sampleGroups.size()); ++groupIndex)
        {
            const auto& group = sampleGroups[(size_t) groupIndex];
            member.writeCache(sampleSpecificCache, group.midiNote, values[i]);
            sampleSpecificParameters.setValue(member.id, group, groupIndex,
                member.readCache(sampleSpecificCache, group.midiNote));
        }
        if (auto* memberParameter = parameters.getParameter(member.id))
        {
            const float normalisedValue = memberParameter->convertTo0to1(values[i]);
            if (memberParameter->getValue() != normalisedValue)
            {
                memberParameter->beginChangeGesture();
                memberParameter->setValueNotifyingHost(normalisedValue);
                memberParameter->endChangeGesture();
            }
        }
    }
    // The selected parameters may already match, but other groups changed.
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged(true));
    return true;
}

void AudioPluginAudioProcessor::parameterChanged(const juce::String& parameterId, float value)
{
    // The host may call on audio: fixed lookups and atomic stores only. State-tree
    // updates happen in UI helpers or at save time, never in this callback.
    const int groupIndex = getSelectedSampleGroupIndex();
    if (groupIndex < 0 || groupIndex >= static_cast<int>(sampleGroups.size()) || !std::isfinite(value))
        return;

    if (const auto* definition = PluginParameters::findSampleSpecificParameter(parameterId))
        definition->writeCache(sampleSpecificCache, sampleGroups[(size_t) groupIndex].midiNote, value);
}

void AudioPluginAudioProcessor::synchroniseSelectedSampleParameters()
{
    for (const auto& definition : PluginParameters::sampleSpecificParameters)
        if (auto* parameter = parameters.getParameter(definition.id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(getSampleSpecificParameterValue(
                definition.id, parameter->convertFrom0to1(parameter->getDefaultValue()))));
}

void AudioPluginAudioProcessor::clampSelectedSampleGroupIndex() noexcept
{
    const int index = sampleGroups.empty() ? -1
        : juce::jlimit(0, (int) sampleGroups.size() - 1, getSelectedSampleGroupIndex());
    selectedSampleGroupIndex.store(index, std::memory_order_relaxed);
}

void AudioPluginAudioProcessor::rebuildSampleSpecificCache()
{
    sampleSpecificCache.reset();

    for (int groupIndex = 0; groupIndex < static_cast<int>(sampleGroups.size()); ++groupIndex)
    {
        const auto& sampleGroup = sampleGroups[(size_t) groupIndex];
        for (const auto& definition : PluginParameters::sampleSpecificParameters)
            if (auto* parameter = parameters.getParameter(definition.id))
                definition.writeCache(sampleSpecificCache, sampleGroup.midiNote,
                    sampleSpecificParameters.getValue(definition.id, sampleGroup, groupIndex,
                        parameter->convertFrom0to1(parameter->getDefaultValue())));
    }
}

void AudioPluginAudioProcessor::storeSampleSpecificCache()
{
    for (int i = 0; i < (int) sampleGroups.size(); ++i)
    {
        const auto& group = sampleGroups[(size_t) i];
        for (const auto& definition : PluginParameters::sampleSpecificParameters)
            sampleSpecificParameters.setValue(definition.id, group, i,
                definition.readCache(sampleSpecificCache, group.midiNote));
    }
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    for (const auto& parameter : PluginParameters::sampleSpecificParameters)
        parameters.removeParameterListener(parameter.id, this);
    suspendProcessing(true);
    sampler.allNotesOff(1, false);
    sampler.clearLayerMappings();
    sampler.clearVoices();
    warpCachePrewarmer.reset(); // Join the worker before freeing immutable source sounds.
    sampler.clearSounds();
}

//==============================================================================

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    setLatencySamples(FormantShifter::latencySamples + PsolaFormantShifter::latencyForSampleRate(sampleRate));
    sampler.setCurrentPlaybackSampleRate(sampleRate);
    sampleSpectrum.sampleRate.store(static_cast<float>(SampleEqualiser::validSampleRate(sampleRate)),
                                    std::memory_order_relaxed);
    rzhavProcessor.prepare(sampleRate);
    backgroundNoise.prepare(sampleRate);
    noiseSidechain.prepare(sampleRate);
    ottProcessor.prepare(sampleRate, ottAmountParam != nullptr
        ? ottAmountParam->load(std::memory_order_relaxed) : PluginParameters::ottAmountDefault);
    midiNoteActivity.reset();
    updateVoiceSharedState();

    for (int i = 0; i < sampler.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<PercussionVoice*>(sampler.getVoice(i)))
            v->prepareRealtimeWarpResources(sampleRate, samplesPerBlock);
}

void AudioPluginAudioProcessor::releaseResources()
{
    rzhavProcessor.reset();
    ottProcessor.reset();
    backgroundNoise.reset();
    noiseSidechain.reset();
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto tempo = hostTempo.update(getPlayHead(), nowSec);

    buffer.clear();

    if (warpParamRaw != nullptr)
        warpEnabledAtomic.store(warpParamRaw->load(std::memory_order_relaxed) >= 0.5f,
                                std::memory_order_relaxed);

    const bool warpEnabledNow = warpEnabledAtomic.load(std::memory_order_relaxed);
    if (warpCachePrewarmer != nullptr
        && warpCachePrewarmer->update(warpEnabledNow, tempo.transportRunning, tempo.hostBpmAvailable))
        hostTempo.resetMotion();

    const int selectedGroup = getSelectedSampleGroupIndex();
    const int spectrumNote = selectedGroup >= 0 && selectedGroup < static_cast<int>(sampleGroups.size())
        ? sampleGroups[(size_t) selectedGroup].midiNote : -1;
    sampleSpectrum.beginBlock(buffer.getNumSamples(), spectrumNote);

    const float rawTrigger = noiseTriggerParam->load(std::memory_order_relaxed);
    const int trigger = std::isfinite(rawTrigger) ? static_cast<int>(std::round(juce::jlimit(
        0.0f, static_cast<float>(noiseTriggerGroups.size()), rawTrigger))) : 0;
    const int triggerNote = trigger > 0 ? noiseTriggerGroups[static_cast<size_t>(trigger - 1)].midiNote : -1;
    noiseSidechain.setParameters(noiseModulationParam->load(std::memory_order_relaxed),
                                PluginParameters::noiseAttackMsDefault,
                                noiseReleaseParam->load(std::memory_order_relaxed),
                                PluginParameters::noiseRiseDbDefault);
    const float source = noiseSourceParam->load(std::memory_order_relaxed);
    const float noiseGain = noiseGainParam->load(std::memory_order_relaxed);
    // Observe gesture completion before reading its final parameter targets.
    // All chunks in this host block use the same gesture/trim snapshot.
    const auto noiseTrimGesture = backgroundNoise.getTrimGestureState();
    const float noiseStart = noiseStartParam->load(std::memory_order_relaxed);
    const float noiseEnd = noiseEndParam->load(std::memory_order_relaxed);
    const float rzhavAmount = rzhavParam->load(std::memory_order_relaxed);
    const float ottAmount = ottAmountParam->load(std::memory_order_relaxed);

    // Stream events exactly once. Passing the whole MIDI buffer to JUCE for
    // each chunk would also dispatch events belonging to subsequent chunks.
    auto midi = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();
    int lastMidiRenderPosition = 0;
    bool firstMidiInterval = true;
    const auto eventPosition = [&]() noexcept
    {
        const int position = juce::jlimit(0, buffer.getNumSamples(), (*midi).samplePosition);
        // Preserve JUCE's default 32-frame event coalescing, independently of
        // our scratch-buffer boundaries. The first rendered interval is exact.
        return position - lastMidiRenderPosition < (firstMidiInterval ? 1 : 32)
            ? lastMidiRenderPosition : position;
    };
    const auto dispatch = [&]
    {
        const auto metadata = *midi++;
        // Synthesiser ignores SysEx. Reject it before constructing an owning
        // MidiMessage, so long messages cannot allocate on the audio thread.
        if (metadata.numBytes > 0 && metadata.numBytes <= 3)
        {
            const auto message = metadata.getMessage();
            midiNoteActivity.handleMidiMessage(message);
            sampler.dispatchMidi(message);
        }
    };

    for (int start = 0; start < buffer.getNumSamples();)
    {
        const int count = juce::jmin(NoiseSidechain::capacity, buffer.getNumSamples() - start);
        const int end = start + count;
        noiseSidechain.beginBlock(start, count, triggerNote);
        int rendered = start;
        while (midi != midiEnd && eventPosition() < end)
        {
            const int position = eventPosition();
            sampler.renderAudioRange(buffer, rendered, position - rendered);
            rendered = juce::jmax(rendered, position);
            if (position > lastMidiRenderPosition)
            {
                firstMidiInterval = false;
                lastMidiRenderPosition = position;
            }
            dispatch();
        }
        sampler.renderAudioRange(buffer, rendered, end - rendered);
        const auto* modulation = noiseSidechain.finishBlock();
        // A non-owning view uses JUCE's inline channel-pointer storage (<=2
        // channels). It never allocates/resizes sample storage on audio.
        juce::AudioBuffer<float> chunk(buffer.getArrayOfWritePointers(),
                                       juce::jmin(2, buffer.getNumChannels()), start, count);
        rzhavProcessor.process(chunk, rzhavAmount);
        ottProcessor.process(chunk, ottAmount);
        backgroundNoise.process(chunk, source, noiseGain, modulation, noiseStart, noiseEnd, noiseTrimGesture);
        start = end;
    }
    // Preserve state updates for zero-length blocks and end-of-block events.
    while (midi != midiEnd)
        dispatch();
    sampleSpectrum.endBlock();
}

//==============================================================================

void AudioPluginAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer&)
{
    // This instrument has no input to delay/pass through. Keep bypass silent
    // without JUCE's default assertion that a bypassed processor has zero latency.
    buffer.clear();
    sampleSpectrum.beginBlock(0, -1);
    backgroundNoise.reset();
    noiseSidechain.reset();
}

bool AudioPluginAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor(*this);
}

//==============================================================================
// Required virtuals

const juce::String AudioPluginAudioProcessor::getName() const { return JucePlugin_Name; }
bool AudioPluginAudioProcessor::acceptsMidi() const { return true; }
bool AudioPluginAudioProcessor::producesMidi() const { return false; }
bool AudioPluginAudioProcessor::isMidiEffect() const { return false; }
double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    const float noiseGain = noiseGainParam->load(std::memory_order_relaxed);
    if (std::isfinite(noiseGain) && noiseGain > PluginParameters::noiseGainDbSilence)
        return std::numeric_limits<double>::infinity();
    const double hostRate = getSampleRate();
    const double rate = std::isfinite(hostRate) && hostRate >= 1000.0 && hostRate <= 768000.0
        ? hostRate : 44100.0;
    return (double) (FormantShifter::tailSamples + PsolaFormantShifter::tailForSampleRate(rate))
           / rate + SampleEqualiser::tailSeconds + OttProcessor::tailSeconds;
}

int AudioPluginAudioProcessor::getNumPrograms() { return 1; }
int AudioPluginAudioProcessor::getCurrentProgram() { return 0; }
void AudioPluginAudioProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
const juce::String AudioPluginAudioProcessor::getProgramName(int index) { juce::ignoreUnused(index); return "Default"; }
void AudioPluginAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================

void AudioPluginAudioProcessor::importNoiseFile(const juce::File& file)
{
    // Called by the editor. The worker owns all file access and decoding.
    const int sourceIndex = backgroundNoise.importFile(file);
    if (sourceIndex < 0)
        return;
    const auto setValue = [this] (const char* id, float value)
    {
        auto* parameter = parameters.getParameter(id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    };
    setValue(PluginParameters::noiseStartId, 0.0f);
    setValue(PluginParameters::noiseEndId, 1.0f);
    setValue(PluginParameters::noiseSourceId, static_cast<float>(sourceIndex));
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
}

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    storeSampleSpecificCache();
    auto state = parameters.copyState();
    const auto noiseNames = BackgroundNoisePlayer::getSourceNames();
    const auto noiseState = state.getChildWithProperty("id", PluginParameters::noiseSourceId);
    const int sourceIndex = juce::jlimit(0, noiseNames.size() - 1,
        static_cast<int>(noiseState.getProperty("value", 0)));
    state.setProperty(noiseUsesImportedProperty,
        sourceIndex >= BackgroundNoisePlayer::importedSourceIndex(), nullptr);
    state.setProperty(noiseImportedIndexProperty,
        sourceIndex - BackgroundNoisePlayer::importedSourceIndex(), nullptr);
    backgroundNoise.writeImportedState(state);
    state.setProperty(noiseSourceNameProperty,
                      noiseNames[sourceIndex], nullptr);
    const auto triggerState = state.getChildWithProperty("id", PluginParameters::noiseTriggerId);
    const int triggerIndex = static_cast<int>(triggerState.getProperty("value", 0)) - 1;
    const bool validTrigger = triggerIndex >= 0 && triggerIndex < static_cast<int>(noiseTriggerGroups.size());
    state.setProperty(noiseTriggerNoteProperty, validTrigger
        ? noiseTriggerGroups[static_cast<size_t>(triggerIndex)].noteIndex : -1, nullptr);
    state.setProperty(noiseTriggerPitchProperty, validTrigger
        ? noiseTriggerGroups[static_cast<size_t>(triggerIndex)].pitchIndex : -1, nullptr);
    const int selectedIndex = getSelectedSampleGroupIndex();
    state.setProperty(selectedSampleGroupIndexProperty, selectedIndex, nullptr);

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(sampleGroups.size()))
    {
        const auto& sampleGroup = sampleGroups[(size_t) selectedIndex];
        state.setProperty(selectedSampleGroupNoteIndexProperty, sampleGroup.noteIndex, nullptr);
        state.setProperty(selectedSampleGroupPitchIndexProperty, sampleGroup.pitchIndex, nullptr);
    }

    sampleSpecificParameters.writeToPluginState(state);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters.state.getType()))
        {
            auto restoredState = juce::ValueTree::fromXml(*xml);
            // APVTS retains the current value for absent parameters. Old
            // projects must instead restore the new global effect as bypassed.
            if (!restoredState.getChildWithProperty("id", PluginParameters::ottAmountId).isValid())
            {
                juce::ValueTree ottState("PARAM");
                ottState.setProperty("id", PluginParameters::ottAmountId, nullptr);
                ottState.setProperty("value", PluginParameters::ottAmountDefault, nullptr);
                restoredState.appendChild(ottState, nullptr);
            }
            // Old projects must load silent even into a previously used instance.
            const std::pair<const char*, float> noiseDefaults[] {
                { PluginParameters::noiseSourceId, 0.0f },
                { PluginParameters::noiseGainDbId, PluginParameters::noiseGainDbSilence },
                { PluginParameters::noiseModulationId, 0.0f },
                { PluginParameters::noiseTriggerId, 0.0f },
                { PluginParameters::noiseAttackMsId, PluginParameters::noiseAttackMsDefault },
                { PluginParameters::noiseReleaseMsId, PluginParameters::noiseReleaseMsDefault },
                { PluginParameters::noiseStartId, 0.0f },
                { PluginParameters::noiseEndId, 1.0f },
                { PluginParameters::noiseRiseDbId, PluginParameters::noiseRiseDbDefault }
            };
            for (const auto& [id, defaultValue] : noiseDefaults)
            {
                if (!restoredState.getChildWithProperty("id", id).isValid())
                {
                    juce::ValueTree noiseState("PARAM");
                    noiseState.setProperty("id", id, nullptr);
                    noiseState.setProperty("value", defaultValue, nullptr);
                    restoredState.appendChild(noiseState, nullptr);
                }
            }
            // Preserve source identity when files added in a later build reorder choices.
            if (restoredState.hasProperty(noiseSourceNameProperty))
            {
                const int noiseIndex = static_cast<bool>(restoredState.getProperty(noiseUsesImportedProperty, false))
                    ? BackgroundNoisePlayer::importedSourceIndex() + juce::jlimit(0,
                        BackgroundNoisePlayer::maximumCustomSources - 1,
                        static_cast<int>(restoredState.getProperty(noiseImportedIndexProperty, 0)))
                    : BackgroundNoisePlayer::getSourceNames().indexOf(
                        restoredState.getProperty(noiseSourceNameProperty).toString());
                auto sourceState = restoredState.getChildWithProperty("id", PluginParameters::noiseSourceId);
                sourceState.setProperty("value", juce::jmax(0, noiseIndex), nullptr);
                if (noiseIndex < 0)
                {
                    auto gainState = restoredState.getChildWithProperty("id", PluginParameters::noiseGainDbId);
                    gainState.setProperty("value", PluginParameters::noiseGainDbSilence, nullptr);
                }
            }
            if (restoredState.hasProperty(noiseTriggerNoteProperty))
            {
                const int note = static_cast<int>(restoredState.getProperty(noiseTriggerNoteProperty, -1));
                const int index = findSampleGroupIndexForKey(noiseTriggerGroups, note,
                    static_cast<int>(restoredState.getProperty(noiseTriggerPitchProperty, -1)));
                auto triggerState = restoredState.getChildWithProperty("id", PluginParameters::noiseTriggerId);
                triggerState.setProperty("value", index + 1, nullptr);
                if (note >= 0 && index < 0)
                {
                    auto depthState = restoredState.getChildWithProperty("id", PluginParameters::noiseModulationId);
                    depthState.setProperty("value", 0.0f, nullptr);
                }
            }
            backgroundNoise.restoreImportedState(restoredState);
            // The noise loader owns this large payload, not APVTS's live tree.
            restoredState.removeChild(restoredState.getChildWithName("ImportedNoise"), nullptr);
            parameters.replaceState(restoredState);

            int restoredSelectedIndex = findSampleGroupIndexForKey(
                sampleGroups,
                static_cast<int>(restoredState.getProperty(selectedSampleGroupNoteIndexProperty, -1)),
                static_cast<int>(restoredState.getProperty(selectedSampleGroupPitchIndexProperty, -1)));

            if (restoredSelectedIndex < 0)
                restoredSelectedIndex = static_cast<int>(restoredState.getProperty(
                    selectedSampleGroupIndexProperty,
                    getSelectedSampleGroupIndex()));

            selectedSampleGroupIndex.store(restoredSelectedIndex, std::memory_order_relaxed);
            clampSelectedSampleGroupIndex();

            sampleSpecificParameters.restoreFromPluginState(restoredState);
            rebuildSampleSpecificCache();
            synchroniseSelectedSampleParameters();

            if (warpParamRaw != nullptr)
            {
                const bool restoredWarpEnabled = warpParamRaw->load(std::memory_order_relaxed) >= 0.5f;
                warpEnabledAtomic.store(restoredWarpEnabled, std::memory_order_relaxed);
            }
            if (warpCachePrewarmer != nullptr)
                warpCachePrewarmer->requestStartupPreparation();
        }
    }
}

//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
