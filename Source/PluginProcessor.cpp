#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/PluginParameters.h"
#include "PercussionVoice.h"
#include "SampleLibrary/PercussionSampleLibrary.h"

#include <cmath>

//==============================================================================
namespace
{
    constexpr int percussionVoiceCount = OneShotPitchCache::maximumVoices;
    constexpr double percussionOriginalBpm = 153.0;

    const juce::Identifier selectedSampleGroupIndexProperty { "selectedSampleGroupIndex" };
    const juce::Identifier selectedSampleGroupNoteIndexProperty { "selectedSampleGroupNoteIndex" };
    const juce::Identifier selectedSampleGroupPitchIndexProperty { "selectedSampleGroupPitchIndex" };

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
    sustainShortenParam = parameters.getRawParameterValue(PluginParameters::sustainShortenId);
    warpParamRaw = parameters.getRawParameterValue(PluginParameters::warpEnabledId);

    sampler.clearVoices();
    addPercussionVoices();
    DBG("Added " << sampler.getNumVoices() << " sampler voices");

    PercussionSampleLibrary::loadEmbeddedSamples(sampler, percussionOriginalBpm, &sampleGroups);
    clampSelectedSampleGroupIndex();
    rebuildSampleSpecificCache();
    warpCachePrewarmer = std::make_unique<WarpCachePrewarmer>(
        sampler, sampleSpecificCache, warpEnabledAtomic, *hostTempo.getBpmAtomic());
    oneShotPitchCache = std::make_unique<OneShotPitchCache>(sampler, sampleSpecificCache);
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
            v->setOneShotPitchCache(oneShotPitchCache.get());
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

    const float normalisedValue = parameter->convertTo0to1(value);
    const float sampleValue = parameter->convertFrom0to1(normalisedValue);
    for (int groupIndex = 0; groupIndex < static_cast<int>(sampleGroups.size()); ++groupIndex)
    {
        const auto& group = sampleGroups[(size_t) groupIndex];
        definition->writeCache(sampleSpecificCache, group.midiNote, sampleValue);
        sampleSpecificParameters.setValue(parameterId, group, groupIndex,
            definition->readCache(sampleSpecificCache, group.midiNote));
    }

    // The selected parameter may already match, but the other groups still changed.
    if (parameter->getValue() != normalisedValue)
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(normalisedValue);
        parameter->endChangeGesture();
    }
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

bool AudioPluginAudioProcessor::selectedSampleSupportsPitchMode() const noexcept
{
    const int groupIndex = getSelectedSampleGroupIndex();
    return oneShotPitchCache != nullptr && groupIndex >= 0 && groupIndex < (int) sampleGroups.size()
        && oneShotPitchCache->supportsMidiNote(sampleGroups[(size_t) groupIndex].midiNote);
}

OneShotPitchCache::Status AudioPluginAudioProcessor::getSelectedSamplePitchStatus() const noexcept
{
    const int groupIndex = getSelectedSampleGroupIndex();
    if (oneShotPitchCache != nullptr && groupIndex >= 0 && groupIndex < (int) sampleGroups.size())
        return oneShotPitchCache->getStatus(sampleGroups[(size_t) groupIndex].midiNote);
    return OneShotPitchCache::Status::ready;
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
    oneShotPitchCache.reset();
    warpCachePrewarmer.reset(); // Join workers before freeing immutable source sounds.
    sampler.clearSounds();
}

//==============================================================================

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (oneShotPitchCache != nullptr)
        oneShotPitchCache->setPlaybackSampleRate(sampleRate);
    sampler.setCurrentPlaybackSampleRate(sampleRate);
    rzhavProcessor.prepare(sampleRate);
    midiNoteActivity.reset();
    updateVoiceSharedState();

    for (int i = 0; i < sampler.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<PercussionVoice*>(sampler.getVoice(i)))
            v->prepareRealtimeWarpResources(sampleRate, samplesPerBlock);
}

void AudioPluginAudioProcessor::releaseResources()
{
    rzhavProcessor.reset();
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

    for (const auto metadata : midiMessages)
        midiNoteActivity.handleMidiMessage(metadata.getMessage());

    sampler.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    float rzhavAmount = 0.0f;
    if (rzhavParam != nullptr)
        rzhavAmount = rzhavParam->load(std::memory_order_relaxed);

    rzhavProcessor.process(buffer, rzhavAmount);
}

//==============================================================================

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
double AudioPluginAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int AudioPluginAudioProcessor::getNumPrograms() { return 1; }
int AudioPluginAudioProcessor::getCurrentProgram() { return 0; }
void AudioPluginAudioProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
const juce::String AudioPluginAudioProcessor::getProgramName(int index) { juce::ignoreUnused(index); return "Default"; }
void AudioPluginAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    storeSampleSpecificCache();
    auto state = parameters.copyState();
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
