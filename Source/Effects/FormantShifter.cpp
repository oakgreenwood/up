#include "FormantShifter.h"

#include <cmath>

FormantShifter::FormantShifter()
{
    for (int i = 0; i < frameSize; ++i)
        window[(size_t) i] = std::sqrt(0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * (float) i / (float) frameSize));
    prepare(44100.0);
}

void FormantShifter::prepare(double sampleRate) noexcept
{
    const double rate = std::isfinite(sampleRate) && sampleRate >= 1000.0 && sampleRate <= 768000.0
        ? sampleRate : 44100.0;
    // Fit a vocal-tract-like resonance model below 8 kHz. Sampling the power
    // spectrum onto this fixed frequency grid keeps model order/cost constant
    // at high host rates, without resampling or changing the audio's pitch.
    const double modelRate = juce::jmin(rate, 16000.0);
    modelBinsPerOutputBin = (float) (rate / modelRate);
    const double emphasis = std::exp(-juce::MathConstants<double>::twoPi * 50.0 / modelRate);
    for (int i = 0; i < binCount; ++i)
    {
        analysisBins[(size_t) i] = (float) ((double) i * modelRate / rate);
        const double response = 1.0 + emphasis * emphasis - 2.0 * emphasis * std::cos(
            juce::MathConstants<double>::twoPi * (double) i / frameSize);
        preEmphasisPower[(size_t) i] = (float) response;
        deEmphasisLog[(size_t) i] = (float) (-0.5 * std::log(response));
    }
    // Widen poles by 50 Hz to stop the model following individual harmonics
    // too closely. This is applied to the model, never as an audio IIR filter.
    for (int i = 0; i <= modelOrder; ++i)
        bandwidthExpansion[(size_t) i] = std::exp(
            -juce::MathConstants<double>::pi * 50.0 * (double) i / modelRate);
    envelopeMemory = (float) std::exp(-(double) hopSize / (rate * 0.004));
    shift.reset(rate / hopSize, 0.02);
    saturationMix.reset(rate, 0.02);
    lowerFormantGain.reset(rate, 0.02);
    reset(1.0f);
}

void FormantShifter::reset(float ratio) noexcept
{
    for (auto& channel : input)
        channel.fill(0.0f);
    for (auto& channel : correction)
        channel.fill(0.0f);
    position = 0;
    hopPosition = 0;
    envelopeReady = false;
    driveReady = false;
    ratio = std::isfinite(ratio) ? juce::jlimit(0.5f, 2.0f, ratio) : 1.0f;
    shift.setCurrentAndTargetValue(ratio);
    saturationMix.setCurrentAndTargetValue(saturationAmount(ratio));
    lowerFormantGain.setCurrentAndTargetValue(lowerFormantGainForRatio(ratio));
}

void FormantShifter::setRatio(float ratio) noexcept
{
    ratio = std::isfinite(ratio) ? juce::jlimit(0.5f, 2.0f, ratio) : 1.0f;
    if (ratio == shift.getTargetValue())
        return;
    shift.setTargetValue(ratio);
    saturationMix.setTargetValue(saturationAmount(ratio));
    lowerFormantGain.setTargetValue(lowerFormantGainForRatio(ratio));
}

void FormantShifter::process(float& left, float& right) noexcept
{
    const std::array<float, 2> incoming { left, right };
    std::array<float, 2> outgoing {};
    for (size_t ch = 0; ch < input.size(); ++ch)
    {
        // A separate delayed dry path makes neutral formant exactly transparent.
        outgoing[ch] = input[ch][(size_t) position] + correction[ch][(size_t) position];
        correction[ch][(size_t) position] = 0.0f;
        input[ch][(size_t) position] = std::isfinite(incoming[ch]) ? incoming[ch] : 0.0f;
    }
    left = std::isfinite(outgoing[0]) ? outgoing[0] : 0.0f;
    right = std::isfinite(outgoing[1]) ? outgoing[1] : 0.0f;
    applySaturation(left, right);
    // Linked post-effect makeup gain, independent of the sample Gain knob.
    const float gain = lowerFormantGain.getNextValue();
    if (gain != 1.0f)
    {
        left *= gain;
        right *= gain;
        left = std::isfinite(left) ? left : 0.0f;
        right = std::isfinite(right) ? right : 0.0f;
    }
    position = (position + 1) % frameSize;
    if (++hopPosition == hopSize)
    {
        hopPosition = 0;
        processFrame();
    }
}

void FormantShifter::processFrame() noexcept
{
    const float ratio = shift.getNextValue();
    if (ratio == 1.0f)
    {
        envelopeReady = false;
        return;
    }

    for (size_t ch = 0; ch < input.size(); ++ch)
    {
        for (int i = 0; i < frameSize; ++i)
            scratch[(size_t) i] = { input[ch][(size_t) ((position + i) % frameSize)]
                                      * window[(size_t) i], 0.0f };
        fft.perform(scratch.data(), spectra[ch].data(), false);
    }

    // Link stereo power (not summed audio) so opposite-polarity channels do not
    // cancel in the analysis. Normalize before squaring to bound the LPC input.
    float peak = 0.0f;
    for (int i = 0; i < binCount; ++i)
    {
        const float left = std::abs(spectra[0][(size_t) i]);
        const float right = std::abs(spectra[1][(size_t) i]);
        if (!std::isfinite(left) || !std::isfinite(right))
        {
            envelopeReady = false;
            return;
        }
        peak = juce::jmax(peak, left, right);
    }
    if (peak < 1.0e-10f || !analyseEnvelope(peak))
    {
        envelopeReady = false;
        return;
    }

    constexpr int nyquist = frameSize / 2;
    constexpr float maxLogGain = 2.763102112f; // +/-24 dB, bounds resonant boosts/nulls.
    constexpr float eqStrength = 0.95f;
    for (int i = 0; i <= nyquist; ++i)
    {
        const float targetBin = (float) i * modelBinsPerOutputBin;
        const float sourceBin = targetBin / ratio;
        const float bandWeight = modelBandWeight(targetBin) * modelBandWeight(sourceBin);
        float logGain = 0.0f;
        if (bandWeight > 0.0f)
        {
            // A little extra resonance contrast gives the shift more throat and
            // vowel character. Resonance frequencies still follow the knob ratio.
            logGain = 1.15f * bandWeight * (envelopeAt(sourceBin) - envelopeAt(targetBin));
        }
        // Apply 95% of the original EQ boost/cut in dB after limiting, including
        // capped bins (now at most +/-22.8 dB).
        // Subtract unity so overlap-add contains only the effect correction.
        const float gain = std::isfinite(logGain) && logGain != 0.0f
            ? std::exp(eqStrength * juce::jlimit(-maxLogGain, maxLogGain, logGain)) - 1.0f : 0.0f;
        for (auto& spectrum : spectra)
        {
            spectrum[(size_t) i] *= gain;
            if (i > 0 && i < nyquist)
                spectrum[(size_t) (frameSize - i)] *= gain;
        }
    }

    for (size_t ch = 0; ch < input.size(); ++ch)
    {
        fft.perform(spectra[ch].data(), scratch.data(), true);
        for (int i = 0; i < frameSize; ++i)
        {
            // Four overlapping periodic sqrt-Hann windows sum to two.
            const float value = scratch[(size_t) i].real() * window[(size_t) i] * 0.5f;
            if (std::isfinite(value))
                correction[ch][(size_t) ((position + i) % frameSize)] += value;
        }
    }
}

bool FormantShifter::analyseEnvelope(float peak) noexcept
{
    for (int i = 0; i < binCount; ++i)
        power[(size_t) i] = 0.5f * (std::norm(spectra[0][(size_t) i] / peak)
                                   + std::norm(spectra[1][(size_t) i] / peak));

    for (int i = 0; i < binCount; ++i)
    {
        const float bin = analysisBins[(size_t) i];
        const int lower = (int) bin;
        const int upper = juce::jmin(lower + 1, binCount - 1);
        const float fraction = bin - (float) lower;
        const float value = (power[(size_t) lower]
                            + fraction * (power[(size_t) upper] - power[(size_t) lower]))
                           * preEmphasisPower[(size_t) i];
        scratch[(size_t) i] = { value, 0.0f };
        if (i > 0 && i < frameSize / 2)
            scratch[(size_t) (frameSize - i)] = { value, 0.0f };
    }
    fft.perform(scratch.data(), autocorrelation.data(), true);
    const double energy = autocorrelation[0].real();
    if (!std::isfinite(energy) || energy < 1.0e-12)
        return false;

    // Regularized Levinson-Durbin: fixed order, double-precision recursion,
    // bounded reflection coefficients and an error floor for tonal/percussive
    // frames. Degenerate frames never feed an unstable synthesis filter.
    std::array<double, modelOrder + 1> correlations {}, coefficients {}, previous {};
    for (int i = 1; i <= modelOrder; ++i)
    {
        correlations[(size_t) i] = (double) autocorrelation[(size_t) i].real() / energy;
        if (!std::isfinite(correlations[(size_t) i]))
            return false;
    }
    coefficients[0] = 1.0;
    double error = 1.001; // Diagonal loading, relative to the frame's energy.
    for (int order = 1; order <= modelOrder; ++order)
    {
        double residual = correlations[(size_t) order];
        for (int j = 1; j < order; ++j)
            residual += coefficients[(size_t) j] * correlations[(size_t) (order - j)];
        if (!std::isfinite(residual))
            return false;
        const double reflection = juce::jlimit(-0.98, 0.98, -residual / error);
        previous = coefficients;
        for (int j = 1; j < order; ++j)
            coefficients[(size_t) j] = previous[(size_t) j]
                                       + reflection * previous[(size_t) (order - j)];
        coefficients[(size_t) order] = reflection;
        error *= 1.0 - reflection * reflection;
        if (error < 1.0e-6)
            break;
    }

    scratch.fill({ 0.0f, 0.0f });
    for (int i = 0; i <= modelOrder; ++i)
        scratch[(size_t) i] = { (float) (coefficients[(size_t) i] * bandwidthExpansion[(size_t) i]), 0.0f };
    fft.perform(scratch.data(), modelResponse.data(), false);
    for (int i = 0; i < binCount; ++i)
    {
        const float denominator = std::norm(modelResponse[(size_t) i]);
        if (!std::isfinite(denominator))
            return false;
        // Prediction gain cancels in the shifted/original envelope ratio.
        const float value = -0.5f * std::log(juce::jmax(1.0e-9f, denominator))
                            + deEmphasisLog[(size_t) i];
        logEnvelope[(size_t) i] = envelopeReady
            ? value + envelopeMemory * (logEnvelope[(size_t) i] - value) : value;
    }
    envelopeReady = true;
    return true;
}

float FormantShifter::envelopeAt(float bin) const noexcept
{
    const int lower = (int) bin;
    const int upper = juce::jmin(lower + 1, binCount - 1);
    return logEnvelope[(size_t) lower]
           + (bin - (float) lower) * (logEnvelope[(size_t) upper] - logEnvelope[(size_t) lower]);
}

float FormantShifter::modelBandWeight(float bin) noexcept
{
    // Fade to unity in the upper quarter of the model band rather than inventing
    // an envelope above the analysis ceiling or sharply cutting cymbal air.
    constexpr float ceiling = (float) (binCount - 1);
    const float fade = juce::jlimit(0.0f, 1.0f, (ceiling - bin) / (ceiling * 0.25f));
    return fade * fade * (3.0f - 2.0f * fade);
}

float FormantShifter::saturationAmount(float ratio) noexcept
{
    return 0.22f * juce::jlimit(0.0f, 1.0f, std::abs(std::log2(ratio))); // 15% more blend than 0.216.
}

float FormantShifter::lowerFormantGainForRatio(float ratio) noexcept
{
    // Callers sanitize ratio to 0.5..2. Map negative semitones linearly in dB:
    // 0 st -> 0 dB, -6 st -> +1.5 dB, -12 st -> +3 dB. Positive shifts stay unity.
    if (ratio >= 1.0f)
        return 1.0f;
    const float boostDb = 3.0f * juce::jlimit(0.0f, 1.0f, -std::log2(ratio));
    return std::pow(10.0f, boostDb / 20.0f);
}

void FormantShifter::applySaturation(float& left, float& right) noexcept
{
    const float mix = saturationMix.getNextValue();
    if (mix <= 0.0f)
    {
        driveReady = false;
        return;
    }

    constexpr double drive = 1.8;
    constexpr double bias = 0.15;
    constexpr double biasRoot = 1.0111874208078342; // sqrt(1 + bias^2)
    constexpr double biasOutput = bias / biasRoot;
    constexpr double normalisation = biasRoot * biasRoot * biasRoot / drive;
    std::array<float, 2> frame { left, right };
    for (size_t ch = 0; ch < frame.size(); ++ch)
    {
        const double x = drive * (double) frame[ch] + bias;
        const double root = std::sqrt(1.0 + x * x);
        if (!driveReady)
        {
            previousDriveInput[ch] = x;
            previousDriveRoot[ch] = root;
        }
        // First-order antiderivative antialiasing of x/sqrt(1+x*x). Rationalize
        // the primitive's divided difference: this remains well conditioned
        // for equal/near-equal samples without logs, oversampling, or a branch.
        const double average = (x + previousDriveInput[ch]) / (root + previousDriveRoot[ch]);
        const double shaped = (average - biasOutput) * normalisation;
        previousDriveInput[ch] = x;
        previousDriveRoot[ch] = root;
        frame[ch] += mix * ((float) shaped - frame[ch]);
    }
    driveReady = true;
    left = frame[0];
    right = frame[1];
}
