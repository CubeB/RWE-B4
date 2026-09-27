#include "DemoRecorder.h"

#include <rwe/io/tad/TadWriter.h>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/TaWireTape.h>
#include <rwe/util/rwe_string.h>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace rwe
{
    namespace
    {
        /**
         * The one 0x19 a sender emits at the start of a game, which is what
         * the corpus's first record reads: demo 14724 tick 0, bytes
         * `19 00 01`, a 16-bit 256 -- normal speed in the 8.8 fixed point the
         * field is written in. RWE has no pause and no speed setting, so this
         * initial record is all there is to say; a real stream's later records
         * are pauses and speed changes RWE does not have.
         */
        const uint16_t NormalSpeedValue = 256;

        /**
         * Every player in the game, which is every block a demo's tape
         * describes: a demo is a wire tap of all senders, not a seat.
         */
        std::vector<PlayerId> everyPlayer(const GameSimulation& simulation)
        {
            std::vector<PlayerId> players;
            players.reserve(simulation.players.size());
            for (std::size_t i = 0; i < simulation.players.size(); ++i)
            {
                players.push_back(PlayerId(static_cast<unsigned int>(i)));
            }
            return players;
        }

        TadWriterSettings makeWriterSettings(const GameSimulation& simulation, const DemoRecorderSettings& settings)
        {
            if (simulation.players.size() > 255)
            {
                throw std::runtime_error("DemoRecorder: too many players for a demo header");
            }

            TadWriterSettings writerSettings;
            writerSettings.version = 5;
            writerSettings.maxUnits = settings.maxUnits;
            writerSettings.mapName = settings.mapName;
            writerSettings.recorderVersion = settings.recorderVersion;
            writerSettings.date = settings.date;

            writerSettings.players.reserve(simulation.players.size());
            writerSettings.dplayIds.reserve(simulation.players.size());
            for (std::size_t i = 0; i < simulation.players.size(); ++i)
            {
                const auto& player = simulation.players[i];
                writerSettings.players.push_back(TadPlayer{
                    static_cast<uint8_t>(player.color.value),
                    static_cast<int8_t>(toUpper(player.side) == "CORE" ? 1 : 0),
                    static_cast<uint8_t>(i + 1),
                    player.name.value_or("Player " + std::to_string(i + 1))});

                // RWE has no DirectPlay id of its own, and the id is only
                // needed so a 0x0c's killerDplayId can name a player, so they
                // run from one and are as synthetic as the ids themselves.
                writerSettings.dplayIds.push_back(static_cast<uint32_t>(i + 1));
            }
            writerSettings.numPlayers = static_cast<uint8_t>(writerSettings.players.size());
            return writerSettings;
        }
    }

    // ---------------------------------------------------------------------
    // DemoIdAllocator
    // ---------------------------------------------------------------------

    struct DemoIdAllocator::Impl
    {
        struct Block
        {
            /** The block number the id arithmetic uses; not the player number. */
            unsigned int number{0};

            /** Freed slots, lowest-first so recycling is deterministic. */
            std::set<uint16_t> freed;

            uint16_t nextIndex{0};
            std::size_t used{0};
        };

        explicit Impl(uint16_t maxUnits) : maxUnits(maxUnits) {}

        uint16_t maxUnits;
        std::unordered_map<PlayerId, Block> blocks;
        std::unordered_map<UnitId, PlayerId> owners;
        std::unordered_map<UnitId, uint16_t> ids;
    };

    DemoIdAllocator::DemoIdAllocator(uint16_t maxUnits)
        : impl(std::make_unique<Impl>(maxUnits))
    {
    }

    DemoIdAllocator::~DemoIdAllocator() = default;

    uint16_t DemoIdAllocator::maxUnits() const
    {
        return impl->maxUnits;
    }

    std::optional<uint16_t> DemoIdAllocator::allocate(PlayerId owner, UnitId unit)
    {
        if (auto existing = idOf(unit))
        {
            return existing;
        }

        if (impl->maxUnits == 0)
        {
            return std::nullopt;
        }

        auto [blockIt, inserted] = impl->blocks.try_emplace(owner);
        auto& block = blockIt->second;
        if (inserted)
        {
            block.number = static_cast<unsigned int>(impl->blocks.size() - 1);
        }

        // A block whose base does not fit sixteen bits cannot be represented,
        // which is the same refusal as a full block. The ids a block can
        // reach top out at (number + 1) * maxUnits.
        if (block.number >= 0xFFFFu / impl->maxUnits)
        {
            return std::nullopt;
        }

        uint16_t index;
        if (!block.freed.empty())
        {
            index = *block.freed.begin();
            block.freed.erase(block.freed.begin());
        }
        else
        {
            if (block.nextIndex >= impl->maxUnits)
            {
                return std::nullopt;
            }
            index = block.nextIndex++;
        }

        ++block.used;
        auto id = tadUnitIdOfIndex(block.number, index, impl->maxUnits);
        impl->ids.emplace(unit, id);
        impl->owners.emplace(unit, owner);
        return id;
    }

    std::optional<uint16_t> DemoIdAllocator::idOf(UnitId unit) const
    {
        auto it = impl->ids.find(unit);
        if (it == impl->ids.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<uint16_t> DemoIdAllocator::indexOf(UnitId unit) const
    {
        auto id = idOf(unit);
        if (!id)
        {
            return std::nullopt;
        }
        return static_cast<uint16_t>((*id - 1) % impl->maxUnits);
    }

    std::optional<PlayerId> DemoIdAllocator::ownerOf(UnitId unit) const
    {
        auto it = impl->owners.find(unit);
        if (it == impl->owners.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::size_t DemoIdAllocator::usedBy(PlayerId owner) const
    {
        auto it = impl->blocks.find(owner);
        if (it == impl->blocks.end())
        {
            return 0;
        }
        return it->second.used;
    }

    void DemoIdAllocator::release(UnitId unit)
    {
        auto ownerIt = impl->owners.find(unit);
        auto idIt = impl->ids.find(unit);
        if (ownerIt == impl->owners.end() || idIt == impl->ids.end())
        {
            return;
        }

        auto index = static_cast<uint16_t>((idIt->second - 1) % impl->maxUnits);
        auto blockIt = impl->blocks.find(ownerIt->second);
        if (blockIt != impl->blocks.end())
        {
            blockIt->second.freed.insert(index);
            --blockIt->second.used;
        }

        impl->owners.erase(ownerIt);
        impl->ids.erase(idIt);
    }

    // ---------------------------------------------------------------------
    // DemoRecorder
    // ---------------------------------------------------------------------

    struct DemoRecorder::Impl
    {
        Impl(const std::filesystem::path& path, const GameSimulation& simulation, DemoRecorderSettings settings)
            : writer(path, makeWriterSettings(simulation, settings)),
              settings(std::move(settings)),
              tape(simulation, tapeSettings(), everyPlayer(simulation))
        {
            init(simulation);
        }

        Impl(std::ostream& stream, const GameSimulation& simulation, DemoRecorderSettings settings)
            : writer(stream, makeWriterSettings(simulation, settings)),
              settings(std::move(settings)),
              tape(simulation, tapeSettings(), everyPlayer(simulation))
        {
            init(simulation);
        }

        /**
         * Read off the member rather than the constructor's parameter, which
         * the second initialiser has already moved from: same ordering trap as
         * the writer's settings, and the same reason the declaration order
         * below matters.
         */
        TaWireTapeSettings tapeSettings() const
        {
            return TaWireTapeSettings{settings.maxUnits, settings.unitLoadOrder};
        }

        void init(const GameSimulation& simulation)
        {
            if (settings.maxUnits == 0)
            {
                throw std::runtime_error("DemoRecorder: maxUnits must be at least one");
            }
            if (settings.unitLoadOrder.empty())
            {
                throw std::runtime_error("DemoRecorder: the data set's unit load order is empty; there is nothing to index types by");
            }

            for (std::size_t i = 0; i < simulation.players.size(); ++i)
            {
                playerOrder.push_back({PlayerId(static_cast<unsigned int>(i)), static_cast<uint8_t>(i)});
            }

            writer.writeHeader();
            writer.writeExtraSectors();
            writer.writePlayers();
            writer.writePlayerStatuses();
            writer.writeUnitTable(settings.unitLoadOrder.size());

            // Whatever is already standing gets an id too: a start-position
            // commander and anything a save or a replay has restored were
            // never built under this recording and get no 0x09, but they are
            // on the map and the round-robin has to have a slot for them.
            for (auto& entry : simulation.units)
            {
                unitCreated(simulation, entry.first);
            }
        }

        void unitCreated(const GameSimulation& simulation, UnitId unit)
        {
            if (auto refusal = tape.unitCreated(simulation, unit); refusal != TaWireRefusal::None)
            {
                if (refusal == TaWireRefusal::UnknownType)
                {
                    throw std::runtime_error(
                        "DemoRecorder: unit type " + simulation.getUnitState(unit).unitType
                        + " is not in the data set's unit load order");
                }
                throw std::runtime_error(
                    "DemoRecorder: player " + std::to_string(simulation.getUnitState(unit).owner.value) + "'s unit block is full (maxUnits="
                    + std::to_string(settings.maxUnits) + "); this game cannot be recorded as a demo");
            }
        }

        void unitRemoved(UnitId unit)
        {
            tape.unitRemoved(unit);
        }

        void buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit)
        {
            if (!tape.buildStarted(simulation, builder, unit))
            {
                throw std::runtime_error("DemoRecorder: a build started for a unit the recorder has not been told about");
            }
        }

        void shotFired(
            UnitId shooter,
            unsigned int weaponSlot,
            std::optional<UnitId> targetUnit,
            const SimVector& origin,
            const SimVector& aimPoint,
            const SimVector& direction)
        {
            tape.shotFired(shooter, weaponSlot, targetUnit, origin, aimPoint, direction);
        }

        void damageApplied(UnitId victim, std::optional<UnitId> attacker, unsigned int damage, std::optional<PlayerId> sourceOwner)
        {
            tape.damageApplied(victim, attacker, damage, sourceOwner);
        }

        void unitDied(UnitId unit, std::optional<UnitId> killer, unsigned int severity, unsigned int cause, unsigned int corpseLevel)
        {
            tape.unitDied(unit, killer, severity, cause, corpseLevel);
        }

        void unitCaptured(const GameSimulation& simulation, UnitId unit, PlayerId newOwner)
        {
            if (auto refusal = tape.unitCaptured(simulation, unit, newOwner); refusal != TaWireRefusal::None)
            {
                throw std::runtime_error(
                    "DemoRecorder: player " + std::to_string(newOwner.value) + "'s unit block is full (maxUnits="
                    + std::to_string(settings.maxUnits) + "); this game cannot be recorded as a demo");
            }
        }

        void endOfTick(const GameSimulation& simulation)
        {
            for (const auto& [player, sender] : playerOrder)
            {
                auto records = tape.endOfTick(simulation, player);

                if (records.unitState.empty())
                {
                    throw std::runtime_error("DemoRecorder: a unit state record would not encode");
                }

                std::vector<TadBytes> payload;
                payload.reserve(records.unitPass.size() + records.projectilePass.size() + records.settle.size() + 2);
                // The initial speed record goes out before everything else,
                // which is where the corpus's is: its game-start 0x19s sit in
                // packets with no 0x2c at all, so a consumer stamps them tick 0.
                if (!speedSent)
                {
                    payload.push_back(tadEncodeSpeed(TadSpeed{NormalSpeedValue}));
                }
                payload.insert(payload.end(), records.unitPass.begin(), records.unitPass.end());
                payload.push_back(std::move(records.unitState));
                payload.insert(payload.end(), records.projectilePass.begin(), records.projectilePass.end());
                payload.insert(payload.end(), records.settle.begin(), records.settle.end());

                writer.writePacket(static_cast<uint16_t>(SimMillisecondsPerTick), sender, payload);
            }

            speedSent = true;
        }

        /**
         * Declared before `settings` because the writer's settings come from
         * the constructor's parameter, not the member, and the member's is
         * moved into place after. Reversing the two would let the move empty
         * the map name before the writer read it, and the tape's own settings
         * the same way.
         */
        TadWriter writer;

        DemoRecorderSettings settings;

        TaWireTape tape;

        std::vector<std::pair<PlayerId, uint8_t>> playerOrder;

        /** The game-start 0x19 goes out once, in each sender's first packet. */
        bool speedSent{false};
    };

    DemoRecorder::DemoRecorder(const std::filesystem::path& path, const GameSimulation& simulation, DemoRecorderSettings settings)
        : impl(std::make_unique<Impl>(path, simulation, std::move(settings)))
    {
    }

    DemoRecorder::DemoRecorder(std::ostream& stream, const GameSimulation& simulation, DemoRecorderSettings settings)
        : impl(std::make_unique<Impl>(stream, simulation, std::move(settings)))
    {
    }

    DemoRecorder::~DemoRecorder() = default;

    void DemoRecorder::unitCreated(const GameSimulation& simulation, UnitId unit)
    {
        impl->unitCreated(simulation, unit);
    }

    void DemoRecorder::unitRemoved(UnitId unit)
    {
        impl->unitRemoved(unit);
    }

    void DemoRecorder::buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit)
    {
        impl->buildStarted(simulation, builder, unit);
    }

    void DemoRecorder::shotFired(
        const GameSimulation& /*simulation*/,
        UnitId shooter,
        unsigned int weaponSlot,
        std::optional<UnitId> targetUnit,
        const SimVector& origin,
        const SimVector& aimPoint,
        const SimVector& direction)
    {
        impl->shotFired(shooter, weaponSlot, targetUnit, origin, aimPoint, direction);
    }

    void DemoRecorder::damageApplied(
        const GameSimulation& /*simulation*/,
        UnitId victim,
        std::optional<UnitId> attacker,
        unsigned int damage,
        std::optional<PlayerId> sourceOwner)
    {
        impl->damageApplied(victim, attacker, damage, sourceOwner);
    }

    void DemoRecorder::unitDied(
        const GameSimulation& /*simulation*/,
        UnitId unit,
        std::optional<UnitId> killer,
        unsigned int severity,
        unsigned int cause,
        unsigned int corpseLevel)
    {
        impl->unitDied(unit, killer, severity, cause, corpseLevel);
    }

    void DemoRecorder::unitCaptured(const GameSimulation& simulation, UnitId unit, PlayerId newOwner)
    {
        impl->unitCaptured(simulation, unit, newOwner);
    }

    void DemoRecorder::endOfTick(const GameSimulation& simulation)
    {
        impl->endOfTick(simulation);
    }

    void DemoRecorder::close()
    {
        impl->writer.close();
    }
}
