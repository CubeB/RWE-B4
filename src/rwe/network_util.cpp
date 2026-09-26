#include "network_util.h"
#include <cstdint>

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

    FrameScheduler::FrameScheduler(
        unsigned int millisecondsBuffer,
        SceneTime averageSceneTime,
        int maxTicksPerFrame)
        : millisecondsBuffer(millisecondsBuffer),
          averageSceneTime(averageSceneTime),
          maxTicksPerFrame(maxTicksPerFrame)
    {
    }

    bool FrameScheduler::hasWork() const
    {
        return millisecondsBuffer >= static_cast<unsigned int>(SimMillisecondsPerTick)
            && dispatched < static_cast<unsigned int>(maxTicksPerFrame);
    }

    std::optional<FrameDispatch> FrameScheduler::next(SceneTime sceneTime)
    {
        if (!hasWork())
        {
            drainBacklogForCap();
            return std::nullopt;
        }

        millisecondsBuffer -= static_cast<unsigned int>(SimMillisecondsPerTick);

        if (!gateAllowsTick(sceneTime))
        {
            ++skips;
            return FrameDispatch::Skip;
        }

        ++dispatched;
        return FrameDispatch::Attempt;
    }

    bool FrameScheduler::extraTick(SceneTime sceneTime)
    {
        if (dispatched < static_cast<unsigned int>(maxTicksPerFrame)
            && sceneTime % frameCheckInterval == SceneTime(0)
            && sceneTime < lowSceneTime())
        {
            ++dispatched;
            return true;
        }
        return false;
    }

    void FrameScheduler::discardBuffer()
    {
        millisecondsBuffer = 0;
    }

    FrameOutcome FrameScheduler::finish()
    {
        drainBacklogForCap();
        return {millisecondsBuffer, dispatched, skips, lostToCap};
    }

    unsigned int FrameScheduler::ticksThisFrame() const
    {
        return dispatched;
    }

    bool FrameScheduler::gateAllowsTick(SceneTime sceneTime) const
    {
        auto highSceneTime = averageSceneTime + frameTolerance;
        return sceneTime % frameCheckInterval != SceneTime(0) || sceneTime <= highSceneTime;
    }

    SceneTime FrameScheduler::lowSceneTime() const
    {
        return averageSceneTime <= frameTolerance ? SceneTime(0) : averageSceneTime - frameTolerance;
    }

    void FrameScheduler::drainBacklogForCap()
    {
        if (capDrained || dispatched < static_cast<unsigned int>(maxTicksPerFrame))
        {
            return;
        }
        capDrained = true;
        lostToCap = millisecondsBuffer / static_cast<unsigned int>(SimMillisecondsPerTick);
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

}
