#include "HostTempoTracker.h"

#include <cmath>

HostTempoUpdate HostTempoTracker::update(juce::AudioPlayHead* playHead, double nowSec)
{
    bool hostTransportRunning = false;
    bool hostBpmAvailable = false;

    if (playHead != nullptr)
    {
        if (const auto pos = playHead->getPosition())
        {
            hostTransportRunning = pos->getIsPlaying() || pos->getIsRecording();
            if (const auto bpm = pos->getBpm(); bpm && std::isfinite(*bpm) && *bpm >= 1.0)
            {
                hostBpmAtomic.store(*bpm, std::memory_order_relaxed);
                hostBpmAvailable = true;
            }
        }
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

    return { hostTransportRunning, hostBpmNow, isMoving, hostBpmAvailable };
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
