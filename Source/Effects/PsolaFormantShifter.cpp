#include "PsolaFormantShifter.h"

#include <algorithm>
#include <cmath>

double PsolaFormantShifter::safeSampleRate(double rate) noexcept
{
    return std::isfinite(rate) && rate >= 1000.0 && rate <= 768000.0 ? rate : 44100.0;
}

int PsolaFormantShifter::latencyForSampleRate(double rate) noexcept
{
    return 4 * (int) std::ceil(safeSampleRate(rate) / 60.0) + 1;
}

int PsolaFormantShifter::tailForSampleRate(double rate) noexcept
{
    return latencyForSampleRate(rate) + 3 * (int) std::ceil(safeSampleRate(rate) / 60.0) + 2;
}

PsolaFormantShifter::PsolaFormantShifter()
{
    for (int i = 0; i <= windowTableSize; ++i)
        windowTable[(size_t) i] = (float) (0.5 + 0.5 * std::cos(
            juce::MathConstants<double>::pi * (double) i / windowTableSize));
    prepare(44100.0);
}

void PsolaFormantShifter::prepare(double sampleRate)
{
    const double rate = safeSampleRate(sampleRate);
    maxPeriod = (int) std::ceil(rate / 60.0);
    minPeriod = juce::jmax(2, (int) std::floor(rate / 1000.0));
    latency = latencyForSampleRate(rate);
    size_t capacity = 1;
    while (capacity < (size_t) (8 * maxPeriod + 16))
        capacity *= 2;
    input.assign(capacity, Stereo {});
    correction.assign(capacity, Stereo {});
    ringMask = capacity - 1;

    decimation = juce::jmax(1, (int) std::ceil(rate / 8000.0));
    const double analysisRate = rate / decimation;
    minLag = juce::jmax(2, (int) std::floor(analysisRate / 1000.0));
    maxLag = juce::jlimit(minLag + 2, maximumLag, (int) std::ceil(analysisRate / 60.0) + 1);
    analysisHop = juce::jmax(1, (int) std::round(analysisRate * 0.01));
    filterCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi
                                      * juce::jmin(1800.0, rate * 0.2) / rate);
    shift.reset(rate, 0.02);
    voicing.reset(rate, 0.02);
    reset(1.0f);
}

void PsolaFormantShifter::reset(float ratio) noexcept
{
    std::fill(input.begin(), input.end(), Stereo {});
    std::fill(correction.begin(), correction.end(), Stereo {});
    analysis.fill(Stereo {});
    filter1.fill(0.0);
    filter2.fill(0.0);
    decimationSum.fill(0.0);
    decimationCount = analysisPosition = analysisCount = analysisCountdown = 0;
    sampleIndex = lastMark = nextAttempt = 0;
    nextMark = periodSamples = 0.0;
    referenceChannel = 0;
    pitchReady = marksReady = hasScheduledGrain = false;
    shift.setCurrentAndTargetValue(std::isfinite(ratio) ? juce::jlimit(0.5f, 2.0f, ratio) : 1.0f);
    voicing.setCurrentAndTargetValue(0.0f);
}

void PsolaFormantShifter::setRatio(float ratio) noexcept
{
    shift.setTargetValue(std::isfinite(ratio) ? juce::jlimit(0.5f, 2.0f, ratio) : 1.0f);
}

void PsolaFormantShifter::process(float& left, float& right) noexcept
{
    const auto index = (size_t) sampleIndex & ringMask;
    input[index] = { std::isfinite(left) ? left : 0.0f, std::isfinite(right) ? right : 0.0f };
    const float ratio = shift.getNextValue();

    // Only the detector is downsampled. Double filter state prevents overflow
    // or persistent non-finite feedback even for extreme finite float input.
    for (size_t ch = 0; ch < 2; ++ch)
    {
        filter1[ch] += filterCoefficient * ((double) input[index][ch] - filter1[ch]);
        filter2[ch] += filterCoefficient * (filter1[ch] - filter2[ch]);
        decimationSum[ch] += filter2[ch];
    }
    if (++decimationCount == decimation)
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            analysis[(size_t) analysisPosition][ch] = (float) (decimationSum[ch] / decimation);
            decimationSum[ch] = 0.0;
        }
        decimationCount = 0;
        analysisPosition = (analysisPosition + 1) % analysisCapacity;
        analysisCount = juce::jmin(analysisCount + 1, analysisCapacity);
        if (--analysisCountdown <= 0)
        {
            analysisCountdown = analysisHop;
            if (ratio != 1.0f)
                analysePitch();
        }
    }
    if (ratio == 1.0f)
    {
        pitchReady = marksReady = false;
        voicing.setTargetValue(0.0f);
    }
    const float confidence = voicing.getNextValue();
    if (pitchReady && ratio != 1.0f)
        scheduleGrain(ratio, confidence);

    const float outL = read(0, sampleIndex - latency) + correction[index][0];
    const float outR = read(1, sampleIndex - latency) + correction[index][1];
    correction[index] = {};
    left = std::isfinite(outL) ? outL : 0.0f;
    right = std::isfinite(outR) ? outR : 0.0f;
    ++sampleIndex;
}

void PsolaFormantShifter::analysePitch() noexcept
{
    const int count = comparisonSamples + maxLag + 1;
    if (analysisCount < count)
        return;

    std::array<double, 2> energies {};
    for (int i = 0; i < count; ++i)
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const double value = analysis[(size_t) ((analysisPosition - 1 - i + analysisCapacity) % analysisCapacity)][ch];
            energies[ch] += value * value;
        }
    const int channel = energies[(size_t) (1 - referenceChannel)] > 2.0 * energies[(size_t) referenceChannel]
        ? 1 - referenceChannel : referenceChannel;
    if (channel != referenceChannel)
        marksReady = false;
    referenceChannel = channel;
    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const float value = analysis[(size_t) ((analysisPosition - 1 - i + analysisCapacity) % analysisCapacity)][(size_t) channel];
        normalisedAnalysis[(size_t) i] = value;
        peak = juce::jmax(peak, std::abs(value));
    }
    if (!std::isfinite(peak) || peak < 1.0e-7f)
    {
        pitchReady = marksReady = false;
        voicing.setTargetValue(0.0f);
        return;
    }
    for (int i = 0; i < count; ++i)
        normalisedAnalysis[(size_t) i] /= peak;

    // YIN cumulative-mean normalized difference, evaluated on a bounded 8 kHz
    // analysis stream. The first sufficiently deep local minimum avoids the
    // repeated-period/octave-down choices of a global autocorrelation maximum.
    differences[0] = 1.0;
    double cumulative = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double difference = 0.0;
        for (int i = 0; i < comparisonSamples; ++i)
        {
            const double delta = (double) normalisedAnalysis[(size_t) i]
                                 - normalisedAnalysis[(size_t) (i + lag)];
            difference += delta * delta;
        }
        cumulative += difference;
        differences[(size_t) lag] = cumulative > 1.0e-12 ? difference * lag / cumulative : 1.0;
    }
    int best = -1;
    for (int lag = minLag; lag < maxLag; ++lag)
        if (differences[(size_t) lag] < 0.2
            && differences[(size_t) lag] <= differences[(size_t) (lag - 1)]
            && differences[(size_t) lag] < differences[(size_t) (lag + 1)])
        {
            best = lag;
            break;
        }
    if (best < 0)
    {
        pitchReady = marksReady = false;
        voicing.setTargetValue(0.0f);
        return;
    }
    const double before = differences[(size_t) (best - 1)];
    const double here = differences[(size_t) best];
    const double after = differences[(size_t) (best + 1)];
    const double curvature = before - 2.0 * here + after;
    const double fraction = curvature > 1.0e-12
        ? juce::jlimit(-0.5, 0.5, 0.5 * (before - after) / curvature) : 0.0;
    const double detectedPeriod = juce::jlimit((double) minPeriod, (double) maxPeriod,
                                               ((double) best + fraction) * decimation);
    if (!pitchReady || detectedPeriod < periodSamples * 0.75 || detectedPeriod > periodSamples * 1.33)
    {
        periodSamples = detectedPeriod;
        marksReady = false;
    }
    else
        periodSamples += 0.35 * (detectedPeriod - periodSamples);
    pitchReady = true;
    voicing.setTargetValue((float) juce::jlimit(0.0, 1.0, (0.2 - here) / 0.15));
}

void PsolaFormantShifter::scheduleGrain(float ratio, float confidence) noexcept
{
    if (sampleIndex < nextAttempt || confidence <= 0.0f)
        return;
    const bool first = !marksReady;
    const double searchRadius = periodSamples * (first ? 0.5 : 0.2);
    if (first)
        nextMark = (double) sampleIndex - maxPeriod - searchRadius - 1.0;
    if (nextMark + searchRadius + maxPeriod > (double) sampleIndex)
        return;

    // Back off after both successes and failed phase locks. Without this bound,
    // a false pitch detection could reseed a full grain on every input sample.
    nextAttempt = sampleIndex + juce::jmax(1, (int) std::ceil(periodSamples * 0.5));
    const double mark = findPitchMark(nextMark, searchRadius, first);
    if (mark < 0.0 || (hasScheduledGrain && mark < (double) lastMark + 0.5 * periodSamples))
    {
        marksReady = false;
        return;
    }
    // At most one grain per input frame. Tracking advances by at least 0.8 of
    // a period; reseeding must advance by at least 0.5. No pitch-jump catch-up loop.
    addGrain((int64_t) mark, periodSamples, ratio, confidence);
    lastMark = (int64_t) mark;
    nextMark = mark + periodSamples;
    marksReady = true;
    hasScheduledGrain = true;
}

double PsolaFormantShifter::findPitchMark(double predicted, double radius, bool first) const noexcept
{
    const int64_t begin = juce::jmax<int64_t>(0, (int64_t) std::ceil(predicted - radius));
    const int64_t end = (int64_t) std::floor(predicted + radius);
    if (end < begin)
        return -1.0;
    double bestScore = -2.0;
    int64_t bestMark = begin;
    // Seed on an amplitude extremum, then lock successive marks by comparing
    // a short waveform around the previous pulse (same polarity and channel).
    const int step = first ? 1 : juce::jmax(1, (int) std::ceil(periodSamples / 64.0));
    for (int64_t candidate = begin; candidate <= end; candidate += step)
    {
        double score = std::abs((double) read(referenceChannel, candidate));
        if (!first)
        {
            double cross = 0.0, sourceEnergy = 0.0, targetEnergy = 0.0;
            for (int i = 0; i < 32; ++i)
            {
                const double offset = ((double) i / 31.0 - 0.5) * periodSamples * 0.5;
                const double a = interpolate(referenceChannel, (double) lastMark + offset);
                const double b = interpolate(referenceChannel, (double) candidate + offset);
                cross += a * b;
                sourceEnergy += a * a;
                targetEnergy += b * b;
            }
            score = sourceEnergy > 1.0e-14 && targetEnergy > 1.0e-14
                ? cross / std::sqrt(sourceEnergy * targetEnergy) : -1.0;
        }
        if (score > bestScore)
        {
            bestScore = score;
            bestMark = candidate;
        }
    }
    return !first && bestScore < 0.5 ? -1.0 : (double) bestMark;
}

void PsolaFormantShifter::addGrain(int64_t mark, double period, float ratio, float confidence) noexcept
{
    const double wetRadius = period / ratio;
    const int radius = (int) std::ceil(juce::jmax(period, wetRadius));
    const int64_t centre = mark + latency;
    const float gain = std::sqrt(ratio);
    for (int offset = -radius; offset <= radius; ++offset)
    {
        const int64_t destination = centre + offset;
        if (destination < sampleIndex || destination - sampleIndex >= (int64_t) input.size())
            continue;
        const float dryWindow = window((double) offset / period);
        const float wetWindow = window((double) offset / wetRadius);
        const auto slot = (size_t) destination & ringMask;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float wet = wetWindow > 0.0f
                ? interpolate(ch, (double) mark + (double) offset * ratio) : 0.0f;
            const float dry = dryWindow > 0.0f ? read(ch, mark + offset) : 0.0f;
            // Keep the window on each resampled grain. Dividing by the sum of
            // compressed windows would erase the pitch-synchronous pulse shape.
            const double value = (double) correction[slot][(size_t) ch] + confidence
                * ((double) gain * wet * wetWindow - (double) dry * dryWindow);
            const float output = (float) value;
            correction[slot][(size_t) ch] = std::isfinite(output) ? output : 0.0f;
        }
    }
}

float PsolaFormantShifter::read(int channel, int64_t index) const noexcept
{
    return index >= 0 && index <= sampleIndex && sampleIndex - index < (int64_t) input.size()
        ? input[(size_t) index & ringMask][(size_t) channel] : 0.0f;
}

float PsolaFormantShifter::interpolate(int channel, double index) const noexcept
{
    if (index < 0.0)
        return 0.0f;
    const auto lower = (int64_t) std::floor(index);
    const double fraction = index - (double) lower;
    // Double arithmetic also avoids overflow in the difference of finite floats.
    return (float) ((1.0 - fraction) * read(channel, lower) + fraction * read(channel, lower + 1));
}

float PsolaFormantShifter::window(double distance) const noexcept
{
    const double position = std::abs(distance) * windowTableSize;
    if (position >= windowTableSize)
        return 0.0f;
    const int lower = (int) position;
    const float fraction = (float) (position - lower);
    return windowTable[(size_t) lower]
        + fraction * (windowTable[(size_t) (lower + 1)] - windowTable[(size_t) lower]);
}
