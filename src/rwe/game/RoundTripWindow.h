#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace rwe
{
    /**
     * The most recent round-trip samples to one peer.
     *
     * The average the command buffer is sized from is a moving average, which
     * is built to smooth spikes away; this keeps them, so that a connection
     * that swings between 50 ms and 400 ms can be told apart from a steady one
     * with the same average. For display only -- nothing the game decides
     * reads it.
     */
    class RoundTripWindow
    {
    public:
        /** About five seconds, at the network service's 100 ms send rate. */
        static constexpr std::size_t Capacity = 50;

        void add(float millis)
        {
            samples[next] = millis;
            next = (next + 1) % Capacity;
            count = std::min(count + 1, Capacity);
        }

        std::size_t size() const { return count; }

        /** Zero while there are no samples. */
        float latest() const
        {
            return count == 0 ? 0.0f : samples[(next + Capacity - 1) % Capacity];
        }

        /** Zero while there are no samples. */
        float min() const
        {
            return count == 0 ? 0.0f : *std::min_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
        }

        /** Zero while there are no samples. */
        float max() const
        {
            return count == 0 ? 0.0f : *std::max_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
        }

    private:
        std::array<float, Capacity> samples{};
        std::size_t next{0};
        std::size_t count{0};
    };
}
