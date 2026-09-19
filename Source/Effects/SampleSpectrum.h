#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <algorithm>

// Audio owns capture/assembly; the message thread owns FFT/display. No shared
// buffer is overwritten until the single consumer releases its queue slot.
class SampleSpectrum final
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;
    static constexpr int captureCapacity = 32768;
    struct Frame
    {
        std::array<float, fftSize> left {}, right {};
        int note = -1;
        float sampleRate = 44100.0f;
    };
    std::atomic<bool> enabled { false };
    std::atomic<float> sampleRate { 44100.0f };

    void beginBlock(int count, int note) noexcept
    {
        const bool capturing = enabled.load(std::memory_order_relaxed)
            && note >= 0 && count >= 0 && count <= captureCapacity;
        captureCount = capturing ? count : 0;
        const float rate = sampleRate.load(std::memory_order_relaxed);
        if (note != assembly.note || rate != assembly.sampleRate || !capturing)
        {
            assembly.note = note;
            assembly.sampleRate = rate;
            filled = 0;
            sinceLastFrame = 0;
            windowFull = false;
        }
        std::fill_n(left.begin(), captureCount, 0.0f);
        std::fill_n(right.begin(), captureCount, 0.0f);
    }

    void add(int note, int position, float l, float r) noexcept
    {
        if (note == assembly.note && position >= 0 && position < captureCount)
        {
            left[static_cast<size_t>(position)] += l;
            right[static_cast<size_t>(position)] += r;
        }
    }

    void endBlock() noexcept
    {
        for (int i = 0; i < captureCount; ++i)
        {
            assembly.left[static_cast<size_t>(filled)] = left[static_cast<size_t>(i)];
            assembly.right[static_cast<size_t>(filled++)] = right[static_cast<size_t>(i)];
            if (filled == fftSize)
            {
                filled = 0;
                windowFull = true;
            }
            if (++sinceLastFrame == hopSize)
            {
                sinceLastFrame = 0;
                if (!windowFull)
                    continue;
                const auto w = written.load(std::memory_order_relaxed);
                // Full queue: drop this complete frame, never wait or overwrite.
                if (w - read.load(std::memory_order_acquire) < queueSize)
                {
                    auto& frame = frames[w % queueSize];
                    // Copy the rolling window oldest-first into a free slot.
                    // Every packet is independent, so dropping one is harmless.
                    const auto split = static_cast<size_t>(filled);
                    const auto firstCount = static_cast<size_t>(fftSize - filled);
                    std::copy_n(assembly.left.begin() + filled, firstCount, frame.left.begin());
                    std::copy_n(assembly.left.begin(), split, frame.left.begin() + firstCount);
                    std::copy_n(assembly.right.begin() + filled, firstCount, frame.right.begin());
                    std::copy_n(assembly.right.begin(), split, frame.right.begin() + firstCount);
                    frame.note = assembly.note;
                    frame.sampleRate = assembly.sampleRate;
                    written.store(w + 1, std::memory_order_release);
                }
            }
        }
    }

    bool pop(Frame& result) noexcept
    {
        const auto r = read.load(std::memory_order_relaxed);
        if (r == written.load(std::memory_order_acquire))
            return false;
        result = frames[r % queueSize];
        read.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static constexpr uint32_t queueSize = 4;
    std::array<float, captureCapacity> left {}, right {};
    Frame assembly;
    std::array<Frame, queueSize> frames;
    std::atomic<uint32_t> written { 0 }, read { 0 };
    int filled = 0, captureCount = 0;
    int sinceLastFrame = 0;
    bool windowFull = false;
};
