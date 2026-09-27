#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <algorithm>
#include <cmath>
#include "../Parameters/PluginParameters.h"

// Audio-thread-only hit capture and envelope. Voices contribute latency-aligned
// onset strengths; no cross-thread buffer handoff is needed.
class NoiseSidechain
{
public:
    static constexpr int capacity = 512;

    void prepare(double rate) noexcept
    {
        sampleRate = std::isfinite(rate) && rate >= 1000.0 && rate <= 768000.0 ? rate : 44100.0;
        amount.reset(sampleRate, 0.01);
        followRise.reset(sampleRate, 0.01);
        lastAttack = lastRelease = -1.0f;
        reset();
    }

    void reset() noexcept
    {
        amount.setCurrentAndTargetValue(0.0f);
        followRise.setCurrentAndTargetValue(1.0);
        lastRiseDb = -1.0f;
        lastFollowDepth = -1.0;
        followBoostShape = 0.0;
        lastRawDepth = -1.0;
        curvedDepth = 0.0;
        initialiseAmount = true;
        levelEnvelope = 0.0;
        pumpLevelEnvelope = pumpActivityEnvelope = 0.0;
        pumpReleaseStartLevel = pumpReleaseStartActivity = pumpTarget = 0.0;
        pumpReleasePhase = 1.0;
        attackRemaining = 0;
        hitTarget = 0.0;
        count = 0;
    }

    void setParameters(float depth, float attackMs, float releaseMs, float riseDb) noexcept
    {
        riseDb = std::isfinite(riseDb)
            ? juce::jlimit(0.0f, PluginParameters::noiseRiseDbMaximum, riseDb)
            : PluginParameters::noiseRiseDbDefault;
        if (riseDb != lastRiseDb)
        {
            lastRiseDb = riseDb;
            riseGainTarget = juce::Decibels::decibelsToGain(static_cast<double>(riseDb));
        }
        const float target = std::isfinite(depth) ? juce::jlimit(-1.0f, 1.0f, depth) : 0.0f;
        if (initialiseAmount)
        {
            // Restored full Follow must start silent without a brief neutral
            // blend while waiting for the first trigger (also after bypass).
            amount.setCurrentAndTargetValue(target);
            followRise.setCurrentAndTargetValue(riseGainTarget);
            initialiseAmount = false;
        }
        else
        {
            amount.setTargetValue(target);
            followRise.setTargetValue(riseGainTarget);
        }
        updateCoefficient(attackMs, lastAttack, attackCoefficient, PluginParameters::noiseAttackMsDefault);
        updateCoefficient(releaseMs, lastRelease, releaseCoefficient, 150.0f);
        attackSamples = juce::jmax(1, static_cast<int>(std::ceil(sampleRate * lastAttack * 0.001)));
        pumpReleaseStep = 1.0 / juce::jmax(1.0, sampleRate * lastRelease * 0.001);
    }

    void beginBlock(int start, int numSamples, int midiNote) noexcept
    {
        offset = start;
        count = juce::jlimit(0, capacity, numSamples);
        triggerNote = midiNote;
        std::fill_n(values.begin(), count, 0.0f);
    }

    void addHit(int note, int position, float strength) noexcept
    {
        const int frame = position - offset;
        if ((triggerNote >= 0 && note != triggerNote) || frame < 0 || frame >= count)
            return;
        if (!std::isfinite(strength) || strength <= 0.0f)
            return;
        auto& value = values[static_cast<size_t>(frame)];
        // Simultaneous voices share one pulse without adding an extra boost.
        value = juce::jmax(value, juce::jlimit(0.0f, 1.0f, strength));
    }

    const float* finishBlock() noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            const double hit = values[static_cast<size_t>(i)];
            if (hit > 0.0)
            {
                // Retrigger from the current envelope, never reset it to zero.
                // A quieter overlapping hit must not pull an attack downward.
                hitTarget = juce::jmax(hit, levelEnvelope,
                    attackRemaining > 0 ? hitTarget : 0.0);
                pumpTarget = juce::jmax(hit, pumpLevelEnvelope,
                    attackRemaining > 0 ? pumpTarget : 0.0);
                attackRemaining = attackSamples;
            }
            const bool attacking = attackRemaining > 0;
            follow(levelEnvelope, attacking ? hitTarget : 0.0);
            advancePump(attacking);
            if (attacking)
                --attackRemaining;
            const float depth = amount.getNextValue();
            const double rawDepth = juce::jlimit(0.0, 1.0, std::abs(static_cast<double>(depth)));
            if (rawDepth != lastRawDepth)
            {
                lastRawDepth = rawDepth;
                // Both sides build depth quickly near centre, then ease toward
                // full effect. Shape the smoothed value to preserve neutral.
                curvedDepth = std::sqrt(rawDepth);
            }
            const double mix = curvedDepth;
            const double riseGain = followRise.getNextValue();
            const double followDepth = juce::jmax(0.0, static_cast<double>(depth));
            if (followDepth != lastFollowDepth)
            {
                lastFollowDepth = followDepth;
                // Keep boost saturation at the original +15 slider position,
                // independently of the curved background/ducking depth.
                followBoostShape = std::sqrt(juce::jmin(1.0, followDepth / followBoostFullDepth));
            }
            const double baseGain = 1.0 - mix;
            const double triggeredPeak = 1.0 + (riseGain - 1.0) * followBoostShape;
            const double envelope = depth < 0.0f
                ? pumpLevelEnvelope + mix * (pumpActivityEnvelope - pumpLevelEnvelope) : levelEnvelope;
            values[static_cast<size_t>(i)] = static_cast<float>(juce::jlimit(
                0.0, depth < 0.0f ? 1.0 : riseGain, depth < 0.0f
                    ? 1.0 - mix * envelope
                    : baseGain + (triggeredPeak - baseGain) * envelope));
        }
        return values.data();
    }

private:
    static constexpr double followBoostFullDepth = 0.3;

    void advancePump(bool attacking) noexcept
    {
        if (attacking)
        {
            // Square the exponential remaining-distance factor: sharper
            // ducking than Follow, with continuity on every retrigger.
            const double coefficient = attackCoefficient * attackCoefficient;
            pumpActivityEnvelope = 1.0 + coefficient * (pumpActivityEnvelope - 1.0);
            pumpLevelEnvelope = pumpTarget + coefficient * (pumpLevelEnvelope - pumpTarget);
            if (std::abs(pumpActivityEnvelope - 1.0) < 0.00001)
                pumpActivityEnvelope = 1.0;
            if (std::abs(pumpLevelEnvelope - pumpTarget) < 0.00001)
                pumpLevelEnvelope = pumpTarget;
            pumpReleaseStartActivity = pumpActivityEnvelope;
            pumpReleaseStartLevel = pumpLevelEnvelope;
            pumpReleasePhase = 0.0;
        }
        else
        {
            // Noise gain returns as t^2 over Release: slow at first, then
            // accelerating. Live time edits change speed without resetting t.
            pumpReleasePhase = juce::jmin(1.0, pumpReleasePhase + pumpReleaseStep);
            const double remaining = 1.0 - pumpReleasePhase * pumpReleasePhase;
            pumpActivityEnvelope = pumpReleaseStartActivity * remaining;
            pumpLevelEnvelope = pumpReleaseStartLevel * remaining;
        }
    }

    void updateCoefficient(float value, float& previous, double& coefficient, float fallback) noexcept
    {
        value = std::isfinite(value) ? juce::jlimit(1.0f, 2000.0f, value) : fallback;
        if (value != previous)
        {
            previous = value;
            // The displayed time is the time to cover 99.9% of a step.
            coefficient = std::exp(std::log(0.001) / (sampleRate * value * 0.001));
        }
    }

    void follow(double& envelope, double target) noexcept
    {
        const double coefficient = target > envelope ? attackCoefficient : releaseCoefficient;
        envelope = target + coefficient * (envelope - target);
        if (std::abs(envelope - target) < 0.00001)
            envelope = target;
    }

    std::array<float, capacity> values {};
    juce::SmoothedValue<float> amount;
    juce::SmoothedValue<double> followRise;
    double riseGainTarget = 1.0, lastFollowDepth = -1.0, followBoostShape = 0.0;
    double lastRawDepth = -1.0, curvedDepth = 0.0;
    float lastRiseDb = -1.0f;
    double sampleRate = 44100.0, levelEnvelope = 0.0;
    double hitTarget = 0.0;
    double pumpLevelEnvelope = 0.0, pumpActivityEnvelope = 0.0, pumpTarget = 0.0;
    double pumpReleaseStartLevel = 0.0, pumpReleaseStartActivity = 0.0;
    double pumpReleasePhase = 1.0, pumpReleaseStep = 1.0;
    double attackCoefficient = 0.0, releaseCoefficient = 0.0;
    float lastAttack = -1.0f, lastRelease = -1.0f;
    int offset = 0, count = 0, triggerNote = -1;
    int attackSamples = 1, attackRemaining = 0;
    bool initialiseAmount = true;
};
