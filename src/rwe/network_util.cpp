#include "network_util.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

namespace rwe
{
    // CRC32 lookup table (polynomial 0xEDB88320, same as Boost.CRC / zlib)
    static uint32_t makeCrc32Entry(uint32_t index)
    {
        uint32_t crc = index;
        for (int j = 0; j < 8; ++j)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1u)));
        }
        return crc;
    }

    struct Crc32Table
    {
        uint32_t entries[256];
        Crc32Table()
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                entries[i] = makeCrc32Entry(i);
            }
        }
    };

    static const Crc32Table crc32Table;

    float ema(float val, float average, float alpha)
    {
        return (alpha * val) + ((1.0f - alpha) * average);
    }

    CommandSetPlan planCommandSets(
        unsigned int bufferedSets,
        unsigned int targetDepth,
        bool stalled,
        bool localSetWaiting)
    {
        auto pushLocalSet = bufferedSets <= targetDepth || (stalled && localSetWaiting);
        auto bufferedAfterLocalSet = pushLocalSet ? bufferedSets + 1 : bufferedSets;
        auto emptySetsToPush = bufferedAfterLocalSet < targetDepth ? targetDepth - bufferedAfterLocalSet : 0u;
        return {pushLocalSet, emptySetsToPush};
    }

    unsigned int estimateSustainableSpeedPermille(
        unsigned int chosenSpeedPermille,
        unsigned int ticksDispatched,
        unsigned int ticksLostToCap,
        float averageTickCostMillis)
    {
        auto capacity = chosenSpeedPermille;

        auto owed = ticksDispatched + ticksLostToCap;
        if (owed > 0 && ticksDispatched < owed)
        {
            auto managed = static_cast<unsigned int>(
                static_cast<unsigned long long>(chosenSpeedPermille) * ticksDispatched / owed);
            capacity = std::min(capacity, managed);
        }

        if (averageTickCostMillis > 0.0f)
        {
            auto byCost = static_cast<unsigned int>(static_cast<float>(SimMillisecondsPerTick) * 1000.0f / averageTickCostMillis);
            capacity = std::min(capacity, byCost);
        }

        return std::max(capacity, MinimumSustainableSpeedPermille);
    }

    float gateAdjustment(SceneTime averageSceneTime, SceneTime sceneTime)
    {
        auto lead = static_cast<int>(averageSceneTime.value) - static_cast<int>(sceneTime.value);
        auto factor = 1.0f + (FrameScheduler::GateGainPerTick * static_cast<float>(lead));
        return std::clamp(factor, FrameScheduler::MinGateFactor, FrameScheduler::MaxGateFactor);
    }

    unsigned int SpeedGovernor::update(Timestamp now, unsigned int chosenPermille, const std::vector<PeerCapacity>& peers)
    {
        auto target = chosenPermille;
        for (const auto& peer : peers)
        {
            target = std::min(target, peer.permille);
        }

        limiting.clear();
        if (target < chosenPermille)
        {
            for (const auto& peer : peers)
            {
                if (peer.permille == target)
                {
                    limiting.push_back(peer.playerId);
                }
            }
        }

        if (!lastUpdate)
        {
            // First frame: nothing has been recovered from yet, so the
            // ceiling, capped by the reports, applies at once.
            effective = target;
            lastUpdate = now;
        }
        else if (chosenPermille > lastChosen && target > effective)
        {
            // The player asked for more: that is explicit, not a peer
            // recovering, so it takes effect at once.
            effective = target;
            lastUpdate = now;
        }
        else if (target < effective)
        {
            // Drop at once, and start the recovery clock from here so the same
            // drop cannot be undone the next frame.
            effective = target;
            lastUpdate = now;
        }
        else if (target > effective)
        {
            auto elapsedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastUpdate).count();
            auto step = static_cast<unsigned int>(
                static_cast<long long>(RecoveryPermillePerSecond) * elapsedMillis / 1000);
            if (step > 0)
            {
                effective = std::min(target, effective + step);
                lastUpdate = now;
            }
        }

        lastChosen = chosenPermille;
        return effective;
    }

    FrameScheduler::FrameScheduler(
        unsigned int millisecondsBuffer,
        SceneTime averageSceneTime,
        SceneTime sceneTime,
        int maxTicksPerFrame)
        : millisecondsBuffer(millisecondsBuffer),
          gateFactor(gateAdjustment(averageSceneTime, sceneTime)),
          maxTicksPerFrame(maxTicksPerFrame)
    {
    }

    unsigned int FrameScheduler::tickCost() const
    {
        return static_cast<unsigned int>(static_cast<float>(SimMillisecondsPerTick) / gateFactor + 0.5f);
    }

    bool FrameScheduler::hasWork() const
    {
        return millisecondsBuffer >= tickCost()
            && dispatched < static_cast<unsigned int>(maxTicksPerFrame);
    }

    void FrameScheduler::next()
    {
        if (!hasWork())
        {
            drainBacklogForCap();
            return;
        }

        millisecondsBuffer -= tickCost();
        ++dispatched;
    }

    void FrameScheduler::discardBuffer()
    {
        millisecondsBuffer = 0;
    }

    FrameOutcome FrameScheduler::finish()
    {
        drainBacklogForCap();
        return {millisecondsBuffer, dispatched, lostToCap};
    }

    unsigned int FrameScheduler::ticksThisFrame() const
    {
        return dispatched;
    }

    void FrameScheduler::drainBacklogForCap()
    {
        if (capDrained || dispatched < static_cast<unsigned int>(maxTicksPerFrame))
        {
            return;
        }
        capDrained = true;
        lostToCap = millisecondsBuffer / tickCost();
        millisecondsBuffer = 0;
    }

    std::size_t chooseChatCountForPacket(
        std::size_t available,
        unsigned long long sizeLimit,
        const std::function<unsigned long long(std::size_t)>& sizeOf)
    {
        return longestPrefixThatFits(available, sizeLimit, sizeOf);
    }

    std::size_t longestPrefixThatFits(
        std::size_t available,
        unsigned long long sizeLimit,
        const std::function<unsigned long long(std::size_t)>& sizeOf)
    {
        if (available == 0 || sizeOf(available) <= sizeLimit)
        {
            return available;
        }

        // Invariant: `low` items fit (0 always counts as fitting, whether or
        // not it does -- see the chat case's last test) and `high` do not.
        std::size_t low = 0;
        std::size_t high = available;
        while (high - low > 1)
        {
            auto mid = low + ((high - low) / 2);
            if (sizeOf(mid) <= sizeLimit)
            {
                low = mid;
            }
            else
            {
                high = mid;
            }
        }
        return low;
    }

    void writeInt(char* sendBuffer, unsigned int crcResult)
    {
        sendBuffer[0] = crcResult & 0xffu;
        sendBuffer[1] = (crcResult >> 8u) & 0xffu;
        sendBuffer[2] = (crcResult >> 16u) & 0xffu;
        sendBuffer[3] = (crcResult >> 24u) & 0xffu;
    }
    unsigned int readInt(const char* buffer)
    {
        return (static_cast<unsigned int>(static_cast<unsigned char>(buffer[3])) << 24u)
            | (static_cast<unsigned int>(static_cast<unsigned char>(buffer[2])) << 16u)
            | (static_cast<unsigned int>(static_cast<unsigned char>(buffer[1])) << 8u)
            | (static_cast<unsigned int>(static_cast<unsigned char>(buffer[0])));
    }
    unsigned int computeCrc(const char* buffer, unsigned int size)
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (unsigned int i = 0; i < size; ++i)
        {
            crc = crc32Table.entries[(crc ^ static_cast<unsigned char>(buffer[i])) & 0xFFu] ^ (crc >> 8u);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    namespace
    {
        // "a.b.c.d" out of "[::ffff:a.b.c.d]:port", "a.b.c.d:port" and the
        // bare form; nothing if what is left is not an IPv4 address.
        std::optional<std::array<unsigned int, 4>> ipv4Octets(std::string text)
        {
            if (!text.empty() && text.front() == '[')
            {
                const auto close = text.find(']');
                if (close == std::string::npos)
                {
                    return std::nullopt;
                }
                text = text.substr(1, close - 1);
            }
            else if (std::count(text.begin(), text.end(), ':') == 1)
            {
                text = text.substr(0, text.find(':'));
            }

            const auto mapped = text.rfind(':');
            if (mapped != std::string::npos)
            {
                // Only the v4-mapped form carries a dotted quad after a colon.
                if (text.size() < 7 || text.compare(0, 7, "::ffff:") != 0 || mapped != 6)
                {
                    return std::nullopt;
                }
                text = text.substr(7);
            }

            std::array<unsigned int, 4> octets{};
            std::size_t at = 0;
            for (std::size_t i = 0; i < 4; ++i)
            {
                const auto end = text.find('.', at);
                const auto part = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                if (part.empty() || part.size() > 3
                    || !std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; }))
                {
                    return std::nullopt;
                }
                const auto value = static_cast<unsigned int>(std::atoi(part.c_str()));
                if (value > 255)
                {
                    return std::nullopt;
                }
                octets[i] = value;
                if (i < 3)
                {
                    if (end == std::string::npos)
                    {
                        return std::nullopt;
                    }
                    at = end + 1;
                }
                else if (end != std::string::npos)
                {
                    return std::nullopt;
                }
            }
            return octets;
        }

        std::string hostOnly(std::string text)
        {
            if (!text.empty() && text.front() == '[')
            {
                const auto close = text.find(']');
                return close == std::string::npos ? text : text.substr(1, close - 1);
            }
            if (std::count(text.begin(), text.end(), ':') == 1)
            {
                return text.substr(0, text.find(':'));
            }
            return text;
        }
    }

    bool sameCarrierBlock(const std::string& a, const std::string& b)
    {
        const auto left = ipv4Octets(a);
        const auto right = ipv4Octets(b);
        if (left && right)
        {
            return (*left)[0] == (*right)[0] && (*left)[1] == (*right)[1];
        }
        return !left && !right && hostOnly(a) == hostOnly(b);
    }
}
