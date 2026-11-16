#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <optional>

// Message-thread motion detector. One hit per stroke, with a six-pixel
// reversal threshold and a slowly adapting direction to tolerate curved paths.
class ShakeGesture
{
public:
    void reset() noexcept { *this = {}; }
    bool isDownStroke() const noexcept { return downStroke; }
    void rebase() noexcept { hasPosition = false; travel = reverseTravel = {}; }

    std::optional<int> move(juce::Point<float> position, double timeMs, float width) noexcept
    {
        if (lastHitMs >= 0.0 && timeMs - lastHitMs >= 4000.0)
            reset();

        if (!hasPosition || timeMs - lastMoveMs > 100.0)
        {
            lastPosition = position;
            lastMoveMs = timeMs;
            hasPosition = true;
            travel = reverseTravel = {};
            return {};
        }

        const auto delta = position - lastPosition;
        const double elapsed = timeMs - lastMoveMs;
        lastPosition = position;
        lastMoveMs = timeMs;
        const float distance = delta.getDistanceFromOrigin();
        if (elapsed <= 0.0 || distance <= 0.0f)
            return {};

        const auto unit = delta / distance;
        bool hit = false;
        if (!hasDirection)
        {
            travel += delta;
            if (travel.getDistanceFromOrigin() >= 3.0f)
            {
                direction = travel / travel.getDistanceFromOrigin();
                hasDirection = hit = true;
            }
        }
        else
        {
            const float alignment = unit.x * direction.x + unit.y * direction.y;
            if (alignment < -0.35f)
            {
                reverseTravel += delta;
                const float reverseDistance = reverseTravel.getDistanceFromOrigin();
                if (reverseDistance >= 6.0f && timeMs - lastHitMs >= 12.0)
                {
                    direction = reverseTravel / reverseDistance;
                    hit = true;
                }
            }
            else
            {
                reverseTravel = {};
                if (alignment > 0.0f)
                {
                    const auto blended = direction * 0.85f + unit * 0.15f;
                    direction = blended / blended.getDistanceFromOrigin();
                }
            }
        }

        if (!hit)
            return {};

        travel = reverseTravel = {};
        lastHitMs = timeMs;
        downStroke = hasHit ? !downStroke : false;
        hasHit = true;
        // The first usable displacement supplies velocity, without waiting for
        // a timer tick or for the stroke to finish. Scale for editor width.
        const double speed = distance * 1000.0 / elapsed;
        const double normalized = juce::jlimit(0.0, 1.0, speed / (2.0 * juce::jmax(1.0f, width)));
        return juce::jlimit(1, 127, static_cast<int>(std::round(1.0 + 126.0 * std::sqrt(normalized))));
    }

private:
    juce::Point<float> lastPosition, direction, travel, reverseTravel;
    double lastMoveMs = 0.0;
    double lastHitMs = -1.0;
    bool hasPosition = false;
    bool hasDirection = false;
    bool hasHit = false;
    bool downStroke = false;
};
