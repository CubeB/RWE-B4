#include "NetworkHistory.h"

#include <algorithm>
#include <cmath>

namespace rwe
{
    void SampleRing::push(float value)
    {
        samples[next] = value;
        next = (next + 1) % Capacity;
        count = std::min(count + 1, Capacity);
    }

    float SampleRing::latest() const
    {
        return count == 0 ? 0.0f : samples[(next + Capacity - 1) % Capacity];
    }

    float SampleRing::max() const
    {
        return count == 0 ? 0.0f : *std::max_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
    }

    NetworkHistory::PeerSeries& NetworkHistory::seriesFor(PlayerId playerId)
    {
        auto it = std::find_if(peerSeries.begin(), peerSeries.end(), [&](const auto& s) { return s.playerId == playerId; });
        if (it != peerSeries.end())
        {
            return *it;
        }
        return peerSeries.emplace_back(playerId);
    }

    void NetworkHistory::observe(Timestamp now, unsigned int ticksRun, std::chrono::milliseconds stalledSoFar, const std::vector<PeerSample>& peers)
    {
        if (!intervalStart)
        {
            intervalStart = now;
            ticksAtStart = ticksRun;
            stalledAtStart = stalledSoFar;
        }

        for (const auto& peer : peers)
        {
            auto& series = seriesFor(peer.playerId);
            if (!series.worst)
            {
                series.worst = peer;
                continue;
            }

            auto& w = *series.worst;
            w.latestRoundTripMillis = std::max(w.latestRoundTripMillis, peer.latestRoundTripMillis);
            w.buffered = std::min(w.buffered, peer.buffered);
            w.quietMillis = std::max(w.quietMillis, peer.quietMillis);
            w.unackedCommandSets = std::max(w.unackedCommandSets, peer.unackedCommandSets);
            if (peer.tickLead && (!w.tickLead || std::abs(*peer.tickLead) > std::abs(*w.tickLead)))
            {
                w.tickLead = peer.tickLead;
            }
        }

        auto elapsed = now - *intervalStart;
        if (elapsed < Interval)
        {
            return;
        }

        auto seconds = std::chrono::duration<float>(elapsed).count();
        ticksPerSecond.push(static_cast<float>(ticksRun - ticksAtStart) / seconds);
        stalledMillis.push(static_cast<float>((stalledSoFar - stalledAtStart).count()));

        for (auto& series : peerSeries)
        {
            series.present = series.worst.has_value();
            if (!series.worst)
            {
                continue;
            }

            const auto& w = *series.worst;
            series.roundTrip.push(w.latestRoundTripMillis);
            series.buffered.push(static_cast<float>(w.buffered));
            series.quiet.push(w.quietMillis);
            series.unacked.push(w.unackedCommandSets);
            series.tickLead.push(w.tickLead.value_or(0.0f));
            series.worst = std::nullopt;
        }

        intervalStart = now;
        ticksAtStart = ticksRun;
        stalledAtStart = stalledSoFar;
    }
}
