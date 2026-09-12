#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

struct HostTempoUpdate
{
    bool transportRunning = false;
    double bpm = 153.0;
    bool bpmMoving = false;
    bool hostBpmAvailable = false;
};

class HostTempoTracker
{
public:
    HostTempoUpdate update(juce::AudioPlayHead* playHead, double nowSec);
    void resetMotion() noexcept;

    double getBpm() const noexcept;
    std::atomic<double>* getBpmAtomic() noexcept { return &hostBpmAtomic; }
    std::atomic<bool>* getMovingAtomic() noexcept { return &hostBpmMovingAtomic; }

private:
    static constexpr double bpmMotionEpsilon = 0.01;
    static constexpr double bpmMotionHoldSec = 0.25;

    std::atomic<double> hostBpmAtomic { 153.0 };
    std::atomic<bool> hostBpmMovingAtomic { false };

    double lastHostBpmForMotion = 0.0;
    double lastHostBpmChangeSec = 0.0;
    bool hasHostBpmForMotion = false;
};
