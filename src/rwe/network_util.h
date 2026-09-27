#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <rwe/game/SceneTime.h>
#include <rwe/rwe_time.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <vector>

namespace rwe
{
    float ema(float val, float average, float alpha);

    /**
     * How a peer said it was running when it last reported: the speed it was
     * advancing at, and whether it had stopped. The defaults describe a peer
     * whose build predates the fields -- running at 1x, unpaused, not
     * stalled.
     */
    struct PeerRunState
    {
        unsigned int speedPermille{1000};
        bool paused{false};
        bool stalled{false};
    };

    /**
     * A peer's last reported scene time carried forward to now. The divisor
     * is the sim's own tick, not the 16 ms frame it used to be, which ran a
     * peer's projected time forward at twice real time since the simulation
     * moved to 30 Hz (#354).
     */
    inline SceneTime projectSceneTime(SceneTime lastKnown, long elapsedTimeMillis)
    {
        return lastKnown + SceneTime(static_cast<unsigned int>(elapsedTimeMillis / SimMillisecondsPerTick));
    }

    /**
     * The same, scaled by the speed the peer is running at. A paused or
     * stalled peer is not advancing at all, so its time stands still however
     * long ago it reported.
     */
    inline SceneTime projectSceneTime(SceneTime lastKnown, long elapsedTimeMillis, const PeerRunState& runState)
    {
        if (runState.paused || runState.stalled)
        {
            return lastKnown;
        }

        auto scaledMillis = (elapsedTimeMillis * static_cast<long>(runState.speedPermille)) / 1000;
        return lastKnown + SceneTime(static_cast<unsigned int>(scaledMillis / SimMillisecondsPerTick));
    }

    /** A peer's last report and how it said it was running when it made it. */
    struct PeerSceneTimeReport
    {
        SceneTime lastKnown;
        Timestamp reportedAt;
        PeerRunState runState;
    };

    template <typename Range>
    unsigned int estimateAverageSceneTimeStatic(SceneTime localSceneTime, Range reports, Timestamp time)
    {
        auto accum = localSceneTime.value;
        auto count = 1;
        for (const auto& report : reports)
        {
            auto elapsedTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(time - report.reportedAt).count();

            auto peerSceneTime = projectSceneTime(report.lastKnown, elapsedTimeMillis, report.runState);
            accum += peerSceneTime.value;
            count += 1;
        }
        return accum / count;
    }

    /**
     * The slowest the effective speed may fall: 0.1x, the floor of the
     * game's speed steps. A machine slower than this is not made playable by
     * slowing down further.
     */
    constexpr unsigned int MinimumSustainableSpeedPermille = 100;

    /**
     * The speed a machine looks able to sustain, in per mille of normal speed,
     * from the dispatch it has just managed. The lower of two independent
     * measures wins: how many of the ticks the clock owed actually ran -- a
     * tick lost to the per-frame cap is the machine being short of time -- and
     * what a tick cost against the budget a tick has. The first covers a frame
     * that fell behind; the second catches a machine only just failing, which
     * has not reached the cap yet. It never reports above `chosenSpeedPermille`,
     * which is all a peer needs to know: the chosen speed is the ceiling.
     */
    unsigned int estimateSustainableSpeedPermille(
        unsigned int chosenSpeedPermille,
        unsigned int ticksDispatched,
        unsigned int ticksLostToCap,
        float averageTickCostMillis);

    /** A peer and the speed its machine reported it can sustain, in per mille. */
    struct PeerCapacity
    {
        PlayerId playerId;
        unsigned int permille{1000};
    };

    /**
     * The proportional drift gate: how much of the accumulator's tick cost to
     * charge for a lead or lag against the average scene time. The gate is
     * `1 + k * (average - local)`, clamped to +/-25% -- a peer ahead of the
     * pack pays more per tick and so runs fewer, and one behind pays less and
     * catches up, smoothly and in proportion rather than by skipping every
     * fifth tick.
     */
    float gateAdjustment(SceneTime averageSceneTime, SceneTime sceneTime);

    /**
     * The effective game speed: the host's chosen speed, capped by every
     * peer's reported capacity, with hysteresis. A drop is immediate -- a
     * machine that cannot keep up needs no second warning -- and a recovery
     * climbs at a fixed rate, because one that has just caught up may merely
     * have been busy. Every peer computes the same answer from the same
     * reports, so the speed never has to travel as a command.
     */
    class SpeedGovernor
    {
    public:
        /** How far the effective speed may climb per second while recovering. */
        static constexpr unsigned int RecoveryPermillePerSecond = 100;

        /**
         * Recompute from the chosen ceiling and every machine's capacity,
         * including this peer's own. `now` is the clock the recovery rate is
         * measured against.
         */
        unsigned int update(Timestamp now, unsigned int chosenPermille, const std::vector<PeerCapacity>& peers);

        unsigned int effectivePermille() const { return effective; }

        /** The peers whose capacity is holding the effective speed down. */
        const std::vector<PlayerId>& limitingPeers() const { return limiting; }

    private:
        unsigned int effective{1000};
        std::optional<Timestamp> lastUpdate;
        unsigned int lastChosen{0};
        std::vector<PlayerId> limiting;
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
        unsigned int ticksLostToCap;
    };

    /**
     * Decides the per-frame lockstep dispatch. It cannot run a tick itself --
     * only the scene can, because a tick touches the whole game -- so the
     * scene asks it once per iteration and feeds the scene time back into a
     * freshly built scheduler each frame.
     */
    class FrameScheduler
    {
    public:
        // Each tick of lead against the average costs 3% more wall-clock time,
        // so a tick of estimation noise moves the tick rate by a fraction of a
        // percent and the +/-25% clamp is only reached nine ticks out. That is
        // a far gentler correction than the every-fifth skip it replaces.
        static constexpr float GateGainPerTick = 0.03f;
        static constexpr float MinGateFactor = 0.75f;
        static constexpr float MaxGateFactor = 1.25f;

        /**
         * `sceneTime` is this peer's scene time at the top of the frame, and
         * `averageSceneTime` the average it is being held to: together they
         * set the proportional gate's tick cost for the whole frame.
         */
        FrameScheduler(
            unsigned int millisecondsBuffer,
            SceneTime averageSceneTime,
            SceneTime sceneTime,
            int maxTicksPerFrame);

        /** The loop condition: buffer to spend and cap allowance left. */
        bool hasWork() const;

        /** Spend this iteration's adjusted tick cost and run a tick. Call while hasWork(). */
        void next();

        /** Throw the rest of the buffer away: the clock-bounded replay case. */
        void discardBuffer();

        /** Final state, including the backlog the cap stopped. */
        FrameOutcome finish();

        unsigned int ticksThisFrame() const;

    private:
        unsigned int tickCost() const;
        void drainBacklogForCap();

        unsigned int millisecondsBuffer;
        float gateFactor;
        int maxTicksPerFrame;
        unsigned int dispatched{0};
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
