#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Prepared once with the sound; seconds refer to the original, unwarped audio.
struct NoiseTriggerPoint
{
    double timeSeconds = 0.0;
    float peak = 0.0f;
};

class NoiseTriggerTracker
{
public:
    void start(const std::vector<NoiseTriggerPoint>* preparedPoints) noexcept
    {
        points = preparedPoints;
        restartLoop();
    }

    void restartLoop() noexcept { nextIndex = 0; }

    float advance(double sourceTimeSeconds, float gain) noexcept
    {
        if (points == nullptr || nextIndex >= points->size()
            || !std::isfinite(sourceTimeSeconds)
            || sourceTimeSeconds < (*points)[nextIndex].timeSeconds)
            return 0.0f;

        // Usually one marker. A seek or extreme rate can cross many: binary
        // search bounds work and coalesces them into the latest hit this frame.
        const auto end = std::upper_bound(points->begin() + nextIndex, points->end(),
            sourceTimeSeconds, [](double time, const NoiseTriggerPoint& point)
            { return time < point.timeSeconds; });
        nextIndex = static_cast<size_t>(end - points->begin());
        const double level = static_cast<double>((end - 1)->peak) * gain;
        if (!std::isfinite(level) || level <= 0.0001)
            return 0.0f;

        return static_cast<float>(0.8 + 0.2 * std::min(1.0, 4.0 * level));
    }

private:
    const std::vector<NoiseTriggerPoint>* points = nullptr;
    size_t nextIndex = 0;
};

// Audio-owned event delay matching the existing voice effects' latency.
// Storage is allocated only during preparation. Reset does not clear the ring.
class NoiseTriggerDelay
{
public:
    void prepare(int latencySamples)
    {
        ring.assign(static_cast<size_t>(std::max(1, latencySamples)), 0.0f);
        reset();
    }

    void reset() noexcept { position = filled = 0; }

    float process(float hit) noexcept
    {
        if (ring.empty())
            return hit;
        const float delayed = filled == ring.size() ? ring[position] : 0.0f;
        ring[position] = hit;
        if (filled < ring.size())
            ++filled;
        if (++position == ring.size())
            position = 0;
        return delayed;
    }

private:
    std::vector<float> ring;
    size_t position = 0, filled = 0;
};
