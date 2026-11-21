#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "PluginProcessor.h"
#include "UI/SampleEqualiserEditor.h"
#include "UI/SampleGroupSelector.h"
#include "UI/SampleWaveformEditor.h"
#include "UI/NoiseWaveformEditor.h"
#include "UI/OnboardingModal.h"
#include "UI/ShakeGesture.h"
#include "UI/AbletonMidiBridgeClient.h"
#include "Midi/VirtualMidiClient.h"

class CustomLookAndFeel;
#if JUCE_MAC
class TrackpadPlayView;
#endif

enum class ValueDragAxis { undecided, vertical, horizontal };

class KeyboardStepSlider final : public juce::Slider
{
public:
    bool keyPressed(const juce::KeyPress& key) override;
};

class ImageKnobSlider final : public juce::Slider
{
public:
    ImageKnobSlider()
    {
        setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
    }

    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    bool hitTest(int x, int y) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();
        const auto radius = 0.5f * juce::jmin(bounds.getWidth(), bounds.getHeight());
        return centre.getDistanceFrom({ static_cast<float>(x), static_cast<float>(y) }) <= radius;
    }

private:
    ValueDragAxis dragAxis = ValueDragAxis::undecided;
    juce::Point<float> dragStartPosition;
};

// A separate label keeps text editing independent of automated slider updates.
class NumericValueInput final : public juce::Label,
                                private juce::Slider::Listener
{
public:
    enum class Validation
    {
        signedDecimal,
        percentage,
        panorama
    };

    NumericValueInput(juce::Slider& controlledSlider,
                      const juce::String& name,
                      Validation validation = Validation::signedDecimal,
                      const juce::String& displaySuffix = {});
    ~NumericValueInput() override;

    void discardEdit();
    void refreshDisplay();
    bool editing() const noexcept { return isBeingEdited(); }
    bool dragging() const noexcept { return dragGesture.has_value(); }
    void setManualGestureParameter(juce::RangedAudioParameter* parameter) noexcept
    {
        manualGestureParameter = parameter;
    }
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void visibilityChanged() override;

private:
    juce::TextEditor* createEditorComponent() override;
    void textEditorReturnKeyPressed(juce::TextEditor& editor) override;
    void textEditorEscapeKeyPressed(juce::TextEditor& editor) override;
    void textEditorFocusLost(juce::TextEditor& editor) override;
    void sliderValueChanged(juce::Slider*) override;
    void commitEdit(const juce::String& text);
    void refreshValue();
    void endDrag();

    juce::Slider& slider;
    const Validation validation;
    const juce::String displaySuffix;
    juce::RangedAudioParameter* manualGestureParameter = nullptr;
    std::optional<juce::Slider::ScopedDragNotification> dragGesture;
    double dragStartValue = 0.0;
    ValueDragAxis dragAxis = ValueDragAxis::undecided;
    bool canDrag = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NumericValueInput)
};

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              public juce::FileDragAndDropTarget,
                                              private juce::Timer,
                                              private juce::AudioProcessorParameter::Listener
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void visibilityChanged() override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    // juce::Slider bitDepthSlider;
    // juce::Slider sampleRateSlider;
    ImageKnobSlider rzhavSlider;
    ImageKnobSlider sustainSlider;
    ImageKnobSlider ottSlider;
    juce::ComboBox noiseSourceSelector;
    juce::StringArray displayedNoiseNames;
    uint32_t displayedNoiseRevision = 0;
    KeyboardStepSlider noiseGainSlider;
    juce::Label noiseLabel;
    juce::Label noiseSourceLabel, noiseTriggerLabel, noiseFollowLabel;
    juce::ComboBox noiseTriggerSelector;
    KeyboardStepSlider noiseModulationSlider;
    juce::Label noiseModulationLabel;
    KeyboardStepSlider noiseReleaseSlider;
    juce::Label noiseReleaseLabel;
    ImageKnobSlider sampleGainSlider;
    ImageKnobSlider samplePunchSlider;
    ImageKnobSlider samplePitchSlider;
    ImageKnobSlider sampleFormantSlider;
    ImageKnobSlider sampleMonoSlider;
    ImageKnobSlider samplePanSlider;
    KeyboardStepSlider sampleAttackSlider;
    KeyboardStepSlider sampleReleaseSlider;
    KeyboardStepSlider sampleStartMsSlider;
    KeyboardStepSlider sampleEndMsSlider;
    KeyboardStepSlider sampleLoopBpmSlider;
    NumericValueInput sampleGainValueInput {
        sampleGainSlider, "Gain value", NumericValueInput::Validation::signedDecimal, " dB"
    };
    NumericValueInput samplePunchValueInput {
        samplePunchSlider, "Punch value", NumericValueInput::Validation::percentage, "%"
    };
    NumericValueInput samplePitchValueInput {
        samplePitchSlider, "Pitch value", NumericValueInput::Validation::signedDecimal, " st"
    };
    NumericValueInput sampleFormantValueInput {
        sampleFormantSlider, "Formant value", NumericValueInput::Validation::signedDecimal, " st"
    };
    NumericValueInput sampleMonoValueInput {
        sampleMonoSlider, "Mono value", NumericValueInput::Validation::percentage, "%"
    };
    NumericValueInput samplePanValueInput {
        samplePanSlider, "Panorama value", NumericValueInput::Validation::panorama
    };
    NumericValueInput sampleAttackValueInput {
        sampleAttackSlider, "Sample attack value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    NumericValueInput sampleReleaseValueInput {
        sampleReleaseSlider, "Sample release value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    NumericValueInput sampleStartValue {
        sampleStartMsSlider, "Sample start value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    NumericValueInput sampleEndValue {
        sampleEndMsSlider, "Sample end value", NumericValueInput::Validation::signedDecimal, " ms"
    };
    NumericValueInput sampleLoopBpmValueInput {
        sampleLoopBpmSlider, "Sample target BPM", NumericValueInput::Validation::signedDecimal, " BPM"
    };
    juce::Label sampleLoopBpmLabel;
    juce::Label sampleOriginalBpmLabel;
    juce::TextButton sampleTempoSyncButton { "TEMPO SYNC" };
    juce::TextButton sampleLoopModeButton { "PRESERVE PITCH" };
    juce::Label rzhavLabel;
    juce::Label sustainLabel;
    juce::Label ottLabel;
    juce::Label sampleGainLabel;
    juce::Label samplePunchLabel;
    juce::Label samplePitchLabel;
    juce::Label sampleFormantLabel;
    juce::Label sampleMonoLabel;
    juce::Label samplePanLabel;
    juce::Label sampleAttackLabel;
    juce::Label sampleReleaseLabel;
    juce::Label sampleStartLabel;
    juce::Label sampleEndLabel;
    juce::Label sampleBeatSnapLabel;
    juce::ComboBox sampleBeatSnapTarget;
    juce::TextButton sampleBeatSnapButton { "BEAT SNAP" };
    juce::TextButton applyToAllButton { "APPLY TO ALL" };
    juce::ComboBox applyToAllEffectSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rzhavAttachment;

    // std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bitDepthAttachment;
    // std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ottAttachment;
    std::unique_ptr<juce::ParameterAttachment> noiseSourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> noiseTriggerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseModulationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> noiseReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePunchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleFormantAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleMonoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> samplePanAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sampleLoopBpmAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sampleLoopModeAttachment;

private:
    struct SampleSpecificSliderBinding
    {
        juce::Slider* slider = nullptr;
        juce::String parameterId;
        juce::Label* label = nullptr;
    };

    struct SampleSpecificEditBinding
    {
        juce::RangedAudioParameter* parameter = nullptr;
        bool gestureInProgress = false;
        float lastGestureValue = 0.0f;
    };

    void bindSliderToParameter(juce::Slider& slider,
                               const juce::String& parameterId,
                               std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                               juce::Label* label = nullptr);
    void refreshSampleSpecificControls();
    void updateSampleTrimInputs();
    void commitSampleTrimInput(juce::Slider&, const char* parameterId);
    juce::RangedAudioParameter* getSelectedApplyToAllParameter() const;
    void selectApplyToAllEffect(juce::RangedAudioParameter& parameter);
    void refreshApplyToAllButton();
    void setSampleEffectsModalVisible(bool shouldBeVisible);
    void updateSampleEditorTabVisibility();
    void updateShakeMode(bool pollMidi = false);
    void resetShakeMidiInputCheck() noexcept;
    void watchShakeMidiInput(int note);
    void checkShakeMidiInput();
    void showShakeMidiInputWarning();
    void handleShakeMouseMove(const juce::MouseEvent&);
    void handleTouchpadHit(int velocity, std::optional<juce::Point<float>> touch, double pressedMs);
    bool triggerLiveHit(int velocity, bool alternate);
    void startRecordedMidiDrag();
    void insertRecordedMidi();
    void openNoiseFileChooser();
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    float getParameterDefaultValue(const juce::String& parameterId) const;
    void timerCallback() override;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;
    const bool abletonMidiInsertSupported;
    AbletonMidiBridgeClient abletonMidiBridge;
    juce::TextButton midiInsertButton { "ADD MIDI" };
    double midiInsertFeedbackUntilMs = 0.0;
    std::unique_ptr<juce::Component> sampleEffectsModalSurface;
    std::unique_ptr<juce::Button> sampleEffectsCloseButton;
    juce::TextButton effectsTabButton { "EFFECTS" };
    juce::TextButton sampleTabButton { "SAMPLE" };
    struct ShakeButton final : juce::TextButton
    {
        ShakeButton() : juce::TextButton("SHAKE") {}
        void mouseDown(const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu())
                return;
            juce::TextButton::mouseDown(event);
        }
    } shakeButton;
    juce::TextButton trackMidiButton { "TRACK MIDI" };
    juce::TextButton inputControlButton { "AUDIO TRIGGER" };
    juce::TextButton inputRoutingButton { "Input routing" };
    PluginUI::OnboardingModal onboardingModal;
    VirtualMidiClient virtualMidi;
    struct ShakeMidiInputCheck
    {
        void watch(uint32_t inputGeneration, double nowMs) noexcept
        {
            // A newer stroke must not postpone an unanswered check.
            if (deadlineMs <= 0.0 || inputGeneration != generation)
                *this = { inputGeneration, nowMs + 1000.0 };
        }

        bool timedOut(uint32_t inputGeneration, double nowMs) noexcept
        {
            if (inputGeneration != generation)
                deadlineMs = 0.0;
            return deadlineMs > 0.0 && nowMs >= deadlineMs;
        }

        uint32_t generation = 0;
        double deadlineMs = 0.0;
    };
    class InputControlMidiWorker final : private juce::Thread
    {
    public:
        explicit InputControlMidiWorker(AudioPluginAudioProcessor&);
        ~InputControlMidiWorker() override;
        void setActive(bool);
        struct Notices { bool launchHelper; bool missingInput; };
        Notices takeNotices();

    private:
        void run() override;

        AudioPluginAudioProcessor& processor;
        std::atomic<bool> active { false };
        juce::CriticalSection noticeLock;
        AudioPluginAudioProcessor::InputControlMidiTarget launchTarget, warningTarget;
    } inputControlMidi;
    struct MidiDragButton final : juce::TextButton
    {
        MidiDragButton() : juce::TextButton("DRAG MIDI") {}
        void mouseDown(const juce::MouseEvent& event) override
        {
            dragStarted = false;
            juce::TextButton::mouseDown(event);
        }
        void mouseDrag(const juce::MouseEvent& event) override
        {
            juce::TextButton::mouseDrag(event);
            if (!dragStarted && isEnabled() && event.getDistanceFromDragStart() >= 4 && onDrag)
            {
                dragStarted = true;
                onDrag();
            }
        }
        std::function<void()> onDrag;
        bool dragStarted = false;
    } midiDragButton;
    struct ShakeMouseListener final : juce::MouseListener
    {
        explicit ShakeMouseListener(AudioPluginAudioProcessorEditor& editor) : owner(editor) {}
        void mouseMove(const juce::MouseEvent& event) override { owner.handleShakeMouseMove(event); }
        void mouseDown(const juce::MouseEvent& event) override
        {
            if (event.eventComponent == &owner.onboardingModal)
                return;
            owner.shakeGesture.rebase();
            if (owner.livePlayGroup < 0 || owner.livePlaySurface == nullptr)
                return;
            const auto position = event.getEventRelativeTo(&owner).position;
            if (!owner.livePlaySurface->getBounds().toFloat().contains(position))
                owner.setLivePlayGroup(-1);
            else if (event.mods.isLeftButtonDown()
                     && !owner.trackMidiButton.getBounds().toFloat().contains(position)
                     && !owner.isInputControlOpen()
                     && (!owner.inputControlButton.isVisible()
                         || !owner.inputControlButton.getBounds().toFloat().contains(position)))
            {
                if (owner.isTouchpadPlayOpen())
                {
                    owner.handleTouchpadHit(100, std::nullopt, juce::Time::getMillisecondCounterHiRes());
                    return;
                }
                owner.shakeButton.setToggleState(!owner.shakeButton.getToggleState(),
                                                 juce::dontSendNotification);
                owner.shakeGesture.reset();
                owner.updateShakeMode();
            }
        }
        void mouseDrag(const juce::MouseEvent&) override { owner.shakeGesture.rebase(); }
        void mouseExit(const juce::MouseEvent& event) override
        {
            const auto position = event.getEventRelativeTo(&owner).position;
            if (!owner.getLocalBounds().toFloat().contains(position)
                || (owner.livePlayGroup >= 0 && owner.livePlaySurface != nullptr
                    && !owner.livePlaySurface->getBounds().toFloat().contains(position)))
                owner.shakeGesture.rebase();
        }
        AudioPluginAudioProcessorEditor& owner;
    } shakeMouseListener { *this };
    ShakeGesture shakeGesture;
    uint32_t observedShakeResetGeneration = 0;
    int liveMidiGroup = -1;
    int liveMidiInput = -1;
    std::array<int, 2> liveMidiNotes { -1, -1 };
    bool virtualMidiWasReady = false;
    bool liveMidiListening = false;
    bool liveMidiUsesInputControl = false;
    double nextTouchpadHitMs = 0.0;
    std::array<ShakeMidiInputCheck, 128> shakeMidiInputChecks {};
    bool shakeMidiInputMonitoring = false;
    bool shakeMidiInputWarningShown = false;
    SampleGroupSelector sampleGrid;
    int livePlayGroup = -1;
    std::unique_ptr<juce::Component> livePlaySurface, livePlayCloseButton;
#if JUCE_MAC
    std::unique_ptr<TrackpadPlayView> trackpadPlayView;
#endif
    juce::Label livePlayInstruction;
    struct ShakeStartButton final : juce::TextButton
    {
        ShakeStartButton() : juce::TextButton("CLICK TO START") {}
        void paintButton(juce::Graphics& g, bool, bool) override
        {
            g.setColour(juce::Colours::black);
            g.setFont(juce::Font(juce::FontOptions(22.0f)));
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    } shakeAreaStartButton;
    void setLivePlayGroup(int group);
    bool isNonDirectionalLivePlayOpen() const noexcept;
    bool isInputControlOpen() const noexcept;
    bool isTouchpadPlayOpen() const noexcept;
    void updateMainPanelVisibility();
    void selectSampleGroupForEditing(int groupIndex);
    SampleEqualiserEditor equaliserEditor;
    SampleWaveformEditor sampleWaveformEditor;
    NoiseWaveformEditor noiseWaveform;
    std::unique_ptr<juce::Component> noisePanelSurface;
    std::unique_ptr<juce::FileChooser> noiseFileChooser;
    std::unique_ptr<CustomLookAndFeel> customLNF;
    std::vector<SampleSpecificSliderBinding> sampleSpecificSliderBindings;
    std::vector<SampleSpecificEditBinding> sampleSpecificEditBindings;
    std::vector<juce::RangedAudioParameter*> applyToAllParameters;
    // Capture at editor construction: JUCE's message-thread query takes a mutex.
    const juce::Thread::ThreadID editorThreadId { juce::Thread::getCurrentThreadId() };
    juce::RangedAudioParameter* pendingApplyToAllParameter = nullptr;
    bool ignoreSampleSpecificEdits = false;
    bool ignoreSampleTrimInputChanges = false;
    bool sampleEffectsModalOpen = false;
    bool effectsTabSelected = true;
    bool noiseFileDragActive = false;
    int displayedSampleGroupIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
