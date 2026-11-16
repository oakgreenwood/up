#include "HostTempoTracker.h"

#include <cmath>

HostTempoUpdate HostTempoTracker::update(juce::AudioPlayHead* playHead, double nowSec)
{
    bool hostTransportRunning = false;
    bool hostBpmAvailable = false;
    bool hostTransportStarted = false;
    bool hostPositionAvailable = false;
    bool hostRecording = false;
    bool hostPlaying = false;
    bool hostPpqAvailable = false;
    double ppqPosition = 0.0;

    if (playHead != nullptr)
    {
        if (const auto pos = playHead->getPosition())
        {
            hostPositionAvailable = true;
            hostRecording = pos->getIsRecording();
            hostPlaying = pos->getIsPlaying();
            if (const auto ppq = pos->getPpqPosition(); ppq && std::isfinite(*ppq))
            {
                hostPpqAvailable = true;
                ppqPosition = *ppq;
            }
            if (const auto bpm = pos->getBpm(); bpm && std::isfinite(*bpm) && *bpm >= 1.0)
            {
                hostBpmAtomic.store(*bpm, std::memory_order_relaxed);
                hostBpmAvailable = true;
            }
        }
    }

    if (hostPositionAvailable)
    {
        hostTransportRunning = hostPlaying || hostRecording;
        hostTransportStarted = hostTransportRunning && !wasTransportRunning;
        wasTransportRunning = hostTransportRunning;
    }

    const double hostBpmNow = getBpm();
    const bool hostBpmMoved = (!hasHostBpmForMotion)
                           || (std::abs(hostBpmNow - lastHostBpmForMotion) > bpmMotionEpsilon);
    if (hostBpmMoved)
    {
        hasHostBpmForMotion = true;
        lastHostBpmForMotion = hostBpmNow;
        lastHostBpmChangeSec = nowSec;
    }

    const bool isMoving = hasHostBpmForMotion
                       && ((nowSec - lastHostBpmChangeSec) <= bpmMotionHoldSec);
    hostBpmMovingAtomic.store(isMoving, std::memory_order_relaxed);

    return { hostTransportRunning, hostBpmNow, isMoving, hostBpmAvailable, hostTransportStarted,
             hostPositionAvailable, hostRecording, hostPlaying, hostPpqAvailable, ppqPosition };
}

void HostTempoTracker::resetMotion() noexcept
{
    hostBpmMovingAtomic.store(false, std::memory_order_relaxed);
    hasHostBpmForMotion = false;
    lastHostBpmForMotion = 0.0;
    lastHostBpmChangeSec = 0.0;
}

double HostTempoTracker::getBpm() const noexcept
{
    return juce::jmax(1.0, hostBpmAtomic.load(std::memory_order_relaxed));
}
