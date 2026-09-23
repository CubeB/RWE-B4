#pragma once

#include <cstddef>
#include <functional>
#include <rwe/game/SceneTime.h>
#include <rwe/rwe_time.h>

namespace rwe
{
    float ema(float val, float average, float alpha);

    template <typename Range>
    unsigned int estimateAverageSceneTimeStatic(SceneTime localSceneTime, Range sceneTimes, Timestamp time)
    {
        auto accum = localSceneTime.value;
        auto count = 1;
        for (const auto& lastKnownSceneTime : sceneTimes)
        {
            auto elapsedTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(time - lastKnownSceneTime.second).count();
            auto extraFrames = elapsedTimeMillis / 16;

            auto peerSceneTime = lastKnownSceneTime.first.value + extraFrames;
            accum += peerSceneTime;
            count += 1;
        }
        return accum / count;
    }

    /**
     * How many chat lines from the front of a send buffer a packet can carry:
     * as many as fit, shedding from the tail until they do.
     *
     * sizeOf says how big the packet would be carrying that many. Shedding
     * from the tail is what makes this safe -- what is left is still a prefix
     * of the stream, so the peer takes it in order and the rest is resent --
     * and chat is what sheds because the commands cannot: the game stops
     * without them. Returns 0 rather than failing when even none of it fits,
     * leaving the caller to decide what an oversized packet means.
     */
    std::size_t chooseChatCountForPacket(
        std::size_t available,
        unsigned long long sizeLimit,
        const std::function<unsigned long long(std::size_t)>& sizeOf);

    void writeInt(char* sendBuffer, unsigned int crcResult);

    unsigned int readInt(const char* buffer);

    unsigned int computeCrc(const char* buffer, unsigned int size);
}
