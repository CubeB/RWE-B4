#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_util.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * How far a puppet was from where its owner's full-state record said it
     * was, in world units, sampled once per full-state record before the snap.
     *
     * The distribution is the regression guard: it says how well RWE's own
     * movement code dead-reckons along a replicated path between the records
     * that correct it.
     */
    struct TadPuppetDrift
    {
        unsigned int samples{0};
        std::vector<double> distances;
    };

    /** Everything a playback did, for the tool and the tests to report. */
    struct TadPuppetStats
    {
        uint64_t packets{0};
        uint64_t ticksPlayed{0};

        uint64_t unitsSpawned{0};
        uint64_t unitsFinished{0};
        uint64_t unitsKilled{0};
        uint64_t wrecksLeft{0};

        /** Units first seen in a mover record, which carries no position. */
        uint64_t unplacedUnits{0};

        uint64_t recordsDroppedBadId{0};
        uint64_t recordsDroppedBadType{0};
        uint64_t recordsDroppedBadBlock{0};
        uint64_t recordsDroppedUnknownUnit{0};
        uint64_t spawnsRefused{0};

        /** A packet whose sender had no serial yet, applied at the current tick. */
        uint64_t packetsWithoutClock{0};

        /** A recorded 0x0d turned into a display round, and one that could not be. */
        uint64_t shotsSpawned{0};
        uint64_t shotsDropped{0};

        /** A recorded 0x10 run on the puppet's own COB, and one that named nothing runnable. */
        uint64_t scriptCallsRun{0};
        uint64_t scriptCallsDropped{0};

        uint64_t chatLines{0};
        uint64_t allyChatLines{0};

        TadPuppetDrift groundDrift;
        TadPuppetDrift airDrift;
    };

    /** One chat line from the stream, for the scene to print and a tool to count. */
    struct TadChatLine
    {
        PlayerId player;
        bool ally{false};
        std::string text;
    };

    /**
     * Drives a simulation straight from a TA demo's packet stream.
     *
     * A demo is state and effects, not orders, so it cannot be fed to the
     * simulation as commands. Instead the driver stamps the demo's unit ids
     * onto RWE units it creates and steers each one along the path or toward
     * the goal the owner sent, correcting it at each full-state record -- the
     * same dead reckoning a receiving TA does. Every demo player is Remote, so
     * no local decision ever contends with the recorded ones.
     *
     * It owns no simulation state and touches no graphics: the scene and the
     * headless tool both use it. The tick clock is the 0x2c serial, never
     * Packet::time.
     */
    class TadPuppetDriver
    {
    public:
        TadPuppetDriver(
            GameSimulation& simulation,
            uint16_t maxUnits,
            std::vector<std::string> unitLoadOrder);

        ~TadPuppetDriver();

        TadPuppetDriver(const TadPuppetDriver&) = delete;
        TadPuppetDriver& operator=(const TadPuppetDriver&) = delete;

        /** Records the RWE player standing in for a demo sender, and marks it Remote. */
        void addPlayer(uint8_t sender, PlayerId player);

        /** Applies one packet's subpackets, advancing the clock to its serial. */
        void onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets);

        /**
         * Hands the clock to the caller instead of ticking the simulation from
         * `onPacket`. A packet's records are queued for the tick its serial
         * names, and `applyTick` applies them; the scene drives the ticks and
         * the driver never calls `GameSimulation::tick` itself. The headless
         * tool leaves this off, so it owns the clock.
         */
        void setExternalClock(bool external);

        /**
         * The external clock's tick: applies every queued record that belongs
         * to this tick, then advances moving air goals. Call once before each
         * simulation tick.
         */
        void applyTick(uint32_t tick);

        /** The highest tick the stream has named, once anything has a serial. */
        std::optional<uint32_t> lastTick() const;

        /** Takes the chat lines decoded since the last call. */
        std::vector<TadChatLine> takeChat();

        const TadPuppetStats& stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
