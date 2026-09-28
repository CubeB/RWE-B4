#pragma once

#include <cstdint>
#include <functional>
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

        /**
         * A 0x0b for a unit this machine owns, handed to the incoming-damage
         * handler instead of being dropped as unknown.
         */
        uint64_t damageHandedOff{0};

        /**
         * A death for a unit we hold no live puppet for: one never placed, one
         * already let go, or one whose slot now holds a different type because
         * the owner re-used it. Counted, because a loss that increments nothing
         * is a hole in the instrument rather than a fact about the stream.
         */
        uint64_t deathsDroppedNotLive{0};

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

        /** A recorded 0x19 game-speed setting. */
        uint64_t speedChanges{0};

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
         * Queues one packet's subpackets for a tick the caller has already
         * worked out, and applies them when `applyTick` reaches it. A live
         * receiver is that caller: it holds a packet until the tick its 0x2c
         * names, and it is the only thing that can say where a record with no
         * serial of its own belongs, which is the tick of the packet its
         * sender sent before it.
         *
         * Requires the external clock. The tick is the caller's, so this
         * neither reads nor keeps a clock of its own -- a record handed over
         * here lands on the tick it is given whatever serial the packet
         * carries.
         */
        void onPacketAt(const TadPacket& packet, const std::vector<TadBytes>& subPackets, uint32_t tick);

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

        /**
         * Takes the most recent recorded 0x19 game-speed value since the last
         * call, or nothing. The value is TA's own: 256 is normal speed and the
         * high byte is the speed level, but the corpus does not settle what a
         * pause looks like, so the caller reads it rather than the driver.
         * Asked after `applyTick`, where the value belongs to the tick about
         * to run.
         */
        std::optional<uint16_t> takeSpeedChange();

        /**
         * The peer's own wire id for one of its units, which is what a hit on
         * that unit has to name: a 0x0b carries the victim's id in the
         * *victim's owner's* block, and only that owner allocates it. Nothing
         * for a unit this driver does not puppet, which is every unit this
         * machine owns.
         */
        std::optional<uint16_t> wireIdOf(UnitId unit) const;

        /**
         * The RWE unit the peer's own id names, or nothing where that slot is
         * empty, has been let go, or belongs to another owner. The inverse of
         * `wireIdOf`.
         */
        std::optional<UnitId> unitOfWireId(uint16_t wireId) const;

        /**
         * Where a 0x0b naming a unit this driver does not puppet is offered.
         *
         * TA's damage records are sent by the attacker, so a record naming
         * one of *our* units arrives from the peer alongside the ones naming
         * units it owns, and the driver puppets only those. The handler is
         * given the victim's wire id and the attacker's `UnitId` where the
         * driver holds one, and decides which of them are units of ours; it
         * is called where the packet is delivered, so in the sender's order.
         */
        using IncomingDamageHandler = std::function<void(uint16_t victimId, std::optional<UnitId> attacker, unsigned int damage)>;

        void setIncomingDamageHandler(IncomingDamageHandler handler);

        const TadPuppetStats& stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
