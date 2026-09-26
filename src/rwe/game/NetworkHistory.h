#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <rwe/rwe_time.h>
#include <rwe/sim/PlayerId.h>
#include <utility>
#include <vector>

namespace rwe
{
    /** A fixed-length history of one figure, laid out for ImGui::PlotLines. */
    class SampleRing
    {
    public:
        static constexpr std::size_t Capacity = 120;

        void push(float value);

        std::size_t size() const { return count; }
        const float* data() const { return samples.data(); }

        /** Where the oldest sample sits in data(), which is what PlotLines' values_offset wants. */
        std::size_t offset() const { return count < Capacity ? 0 : next; }

        /** Zero while empty. */
        float latest() const;

        /** Zero while empty. */
        float max() const;

    private:
        std::array<float, Capacity> samples{};
        std::size_t next{0};
        std::size_t count{0};
    };

    /** One peer as seen on one frame. */
    struct PeerSample
    {
        PlayerId playerId;
        float latestRoundTripMillis;
        unsigned int buffered;
        float quietMillis;
        float unackedCommandSets;
        std::optional<float> tickLead;
    };

    /**
     * The network overlay's graphs: the lockstep's figures, gathered every
     * frame and kept as one sample per Interval.
     *
     * Each sample is the worst the interval saw -- the highest round trip and
     * silence, the fewest commands buffered -- because a figure that dips to
     * zero for one frame is the one that stalls the game, and sampling the
     * frame the interval happens to end on would miss it. Wall-clock figures
     * about this machine; the simulation never reads them.
     */
    class NetworkHistory
    {
    public:
        /** Four samples a second is slow enough to read; 120 of them is thirty seconds. */
        static constexpr std::chrono::milliseconds Interval{250};

        struct PeerSeries
        {
            PlayerId playerId;

            /** Whether the peer was in the most recent sample; a dropped peer's history stays. */
            bool present{false};

            SampleRing roundTrip;
            SampleRing buffered;
            SampleRing quiet;
            SampleRing unacked;
            SampleRing tickLead;

            std::optional<PeerSample> worst;

            explicit PeerSeries(PlayerId playerId) : playerId(playerId) {}
        };

        /**
         * `ticksRun` and `stalledSoFar` are running totals; the history keeps
         * how much each moved per interval.
         */
        void observe(Timestamp now, unsigned int ticksRun, std::chrono::milliseconds stalledSoFar, const std::vector<PeerSample>& peers);

        /** Ticks a second, per interval. */
        const SampleRing& tickRate() const { return ticksPerSecond; }

        /** Milliseconds spent stalled, per interval. */
        const SampleRing& stalled() const { return stalledMillis; }

        const std::vector<PeerSeries>& peers() const { return peerSeries; }

    private:
        PeerSeries& seriesFor(PlayerId playerId);

        std::optional<Timestamp> intervalStart;
        unsigned int ticksAtStart{0};
        std::chrono::milliseconds stalledAtStart{0};

        SampleRing ticksPerSecond;
        SampleRing stalledMillis;
        std::vector<PeerSeries> peerSeries;
    };
}
