#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <rwe/game/SceneTime.h>
#include <rwe/rwe_time.h>
#include <rwe/sim/SimTicksPerSecond.h>

namespace rwe
{
    float ema(float val, float average, float alpha);

    /**
     * A peer's last reported scene time carried forward to now. The divisor
     * is a 16 ms frame, not the sim's own tick -- wrong since the simulation
     * moved to 30 Hz, and the subject of #354.
     */
    inline SceneTime projectSceneTime(SceneTime lastKnown, long elapsedTimeMillis)
    {
        return lastKnown + SceneTime(static_cast<unsigned int>(elapsedTimeMillis / 16));
    }

    template <typename Range>
    unsigned int estimateAverageSceneTimeStatic(SceneTime localSceneTime, Range sceneTimes, Timestamp time)
    {
        auto accum = localSceneTime.value;
        auto count = 1;
        for (const auto& lastKnownSceneTime : sceneTimes)
        {
            auto elapsedTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(time - lastKnownSceneTime.second).count();

            auto peerSceneTime = projectSceneTime(lastKnownSceneTime.first, elapsedTimeMillis);
            accum += peerSceneTime.value;
            count += 1;
        }
        return accum / count;
    }

    /**
     * What one iteration of the per-frame lockstep dispatch should do.
     */
    enum class FrameDispatch
    {
        // The drift gate held this tick back; the iteration's time is spent
        // regardless, so a skip costs the same 33 ms a tick would have.
        Skip,
        // Run a tick.
        Attempt,
    };

    /**
     * The command sets a frame should push for the local player before it
     * ticks: the local set if one is due, then empty padding up to the target
     * depth. Pushing a local set while stalled is what lets a drop command
     * escape a buffer that is full only because the tick waiting on it cannot
     * run.
     */
    struct CommandSetPlan
    {
        bool pushLocalSet;
        unsigned int emptySetsToPush;
    };

    CommandSetPlan planCommandSets(
        unsigned int bufferedSets,
        unsigned int targetDepth,
        bool stalled,
        bool localSetWaiting);

    /**
     * What a frame's dispatch did, for the network overlay to report (#355).
     */
    struct FrameOutcome
    {
        unsigned int millisecondsLeft;
        unsigned int ticksDispatched;
        unsigned int gateSkips;
        unsigned int ticksLostToCap;
    };

    /**
     * Decides the per-frame lockstep dispatch. It cannot run a tick itself --
     * only the scene can, because a tick touches the whole game -- so the
     * scene carries each decision out and feeds the scene time back: a tick
     * that cannot run does not advance the scene time, and the gate's
     * decision is a function of where the scene time is.
     */
    class FrameScheduler
    {
    public:
        // Tolerate a few ticks of drift either way before the gate acts, to
        // cope with noisiness in the estimation.
        static constexpr SceneTime frameTolerance{3};
        static constexpr SceneTime frameCheckInterval{5};

        FrameScheduler(
            unsigned int millisecondsBuffer,
            SceneTime averageSceneTime,
            int maxTicksPerFrame);

        /** The loop condition: buffer to spend and cap allowance left. */
        bool hasWork() const;

        /** Spend this iteration's 33 ms and decide. Call while hasWork(). */
        std::optional<FrameDispatch> next(SceneTime sceneTime);

        /**
         * Whether the catch-up tick follows an Attempt, at the scene time the
         * attempt reached. It shares the iteration's 33 ms and counts against
         * the cap, so it can reach the cap but never pass it.
         */
        bool extraTick(SceneTime sceneTime);

        /** Throw the rest of the buffer away: the clock-bounded replay case. */
        void discardBuffer();

        /** Final state, including the backlog the cap stopped. */
        FrameOutcome finish();

        unsigned int ticksThisFrame() const;

    private:
        bool gateAllowsTick(SceneTime sceneTime) const;
        SceneTime lowSceneTime() const;
        void drainBacklogForCap();

        unsigned int millisecondsBuffer;
        SceneTime averageSceneTime;
        int maxTicksPerFrame;
        unsigned int dispatched{0};
        unsigned int skips{0};
        unsigned int lostToCap{0};
        bool capDrained{false};
    };

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

    /**
     * The longest prefix of a stream, up to `available` items, whose packet
     * is no bigger than `sizeLimit`, found by halving rather than by trying
     * every length: sizeOf builds and measures a whole packet, and a command
     * stream can have hundreds of sets waiting on an ack. sizeOf must grow
     * with the count, as a packet does. 0 if not even one item fits.
     *
     * What a packet takes of each stream. Before this existed a packet took
     * all of them, and one that came to more than a datagram threw on the
     * network thread and stopped the game: a peer only had to stop acking,
     * or a player only had to order fifty units at once. Issue #75.
     */
    std::size_t longestPrefixThatFits(
        std::size_t available,
        unsigned long long sizeLimit,
        const std::function<unsigned long long(std::size_t)>& sizeOf);

    void writeInt(char* sendBuffer, unsigned int crcResult);

    unsigned int readInt(const char* buffer);

    unsigned int computeCrc(const char* buffer, unsigned int size);
}
