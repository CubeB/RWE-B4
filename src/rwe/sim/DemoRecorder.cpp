#include "DemoRecorder.h"

#include <algorithm>
#include <cmath>
#include <rwe/io/tad/TadWriter.h>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/match.h>
#include <rwe/util/rwe_string.h>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>

namespace rwe
{
    namespace
    {
        /**
         * The cap the original's update loop applies (`0x48B7F6`): entries
         * stop once the packet has reached 512 bytes and the rest wait for a
         * later tick. It is checked against the 0x2c alone rather than the
         * whole packet, and the full-state record that follows is exempt, as
         * it is in the original.
         */
        const std::size_t MaxUnitStateSubPacketBytes = 512;

        /**
         * One 0x28 every this many ticks, which is the corpus's sampling
         * interval: 47,880 of its 56,535 gaps are exactly 120, and the samples
         * land on a multiple of thirty -- a settle -- for 99.7% of them.
         */
        const uint32_t ResourceSampleTicks = 120;

        /**
         * The one 0x19 a sender emits at the start of a game, which is what
         * the corpus's first record reads: demo 14724 tick 0, bytes
         * `19 00 01`, a 16-bit 256 -- normal speed in the 8.8 fixed point the
         * field is written in. RWE has no pause and no speed setting, so this
         * initial record is all there is to say; a real stream's later records
         * are pauses and speed changes RWE does not have.
         */
        const uint16_t NormalSpeedValue = 256;

        /** What a ground or air mover serialiser will write, as the recorder stores it. */
        using Mover = std::variant<TadGroundPath, TadAirMover>;

        bool sameMover(const Mover& a, const Mover& b)
        {
            if (a.index() != b.index())
            {
                return false;
            }

            if (const auto* groundA = std::get_if<TadGroundPath>(&a))
            {
                const auto& groundB = std::get<TadGroundPath>(b);
                return groundA->blocked == groundB.blocked && groundA->waypoints == groundB.waypoints;
            }

            const auto& airA = std::get<TadAirMover>(a);
            const auto& airB = std::get<TadAirMover>(b);
            // The goal is always the empty kind in this milestone: air goals
            // are approximated to kind 0/mode and refined with the air work.
            return airA.movementMode == airB.movementMode && airA.goal.index() == airB.goal.index();
        }

        TadPosition toTadPosition(const SimVector& position)
        {
            return TadPosition{
                simScalarToFixed(position.x),
                simScalarToFixed(position.y),
                simScalarToFixed(position.z)};
        }

        /**
         * The corpus's rotation is (pitch, yaw, roll) and RWE keeps only the
         * yaw; terrain-following pitch and roll are presentation. The encoder
         * writes the record as y, z, x, which is why the yaw goes in y here.
         */
        TadRotation toTadRotation(const UnitState& unit)
        {
            return TadRotation{0, static_cast<int16_t>(unit.rotation.value), 0};
        }

        /**
         * A shot's launch attitude from the direction it actually left on:
         * yaw and elevation, roll left zero. The corpus's 0x0d rotation triple
         * tracks the aim line's bearing and elevation, and its departure from
         * the aim line is a real aiming error, so it is the post-scatter
         * direction that belongs here and not the mount's pre-scatter aim.
         */
        TadRotation launchRotation(const SimVector& direction)
        {
            auto yaw = atan2(direction.x, direction.z);
            auto pitch = atan2(direction.y, hypot(direction.x, direction.z));
            return TadRotation{static_cast<int16_t>(pitch.value), static_cast<int16_t>(yaw.value), 0};
        }

        SimVector airVelocity(const UnitPhysicsInfoAir& air)
        {
            return match(
                air.movementState,
                [](const AirMovementStateFlying& s) { return s.currentVelocity; },
                [](const AirMovementStateTakingOff& s) { return s.currentVelocity; },
                [](const AirMovementStateLanding&) { return SimVector(0_ss, 0_ss, 0_ss); },
                [](const AirMovementStateAttackRun& s) { return s.currentVelocity; },
                [](const AirMovementStateHoverAttack& s) { return s.currentVelocity; },
                [](const AirMovementStateDogfight& s) { return s.currentVelocity; });
        }

        SimScalar speedOf(const UnitState& unit)
        {
            return match(
                unit.physics,
                [](const UnitPhysicsInfoGround& ground) { return ground.currentSpeed; },
                [](const UnitPhysicsInfoAir& air) {
                    auto v = airVelocity(air);
                    return rweSqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                });
        }

        /**
         * Whether the type has a mover, which is what decides whether a
         * full-state record carries a speed. The receiver knows from its own
         * copy of the unit, and the closest question RWE's definitions answer
         * is whether the type moves at all: `isMobile` or `canFly`, or any
         * `MaxVelocity`, which the corpus's immobile pads still carry.
         */
        bool hasMover(const UnitDefinition& definition)
        {
            return definition.isMobile || definition.canFly || definition.maxVelocity.value > 0.0f;
        }

        std::vector<bool> canFlyFlags(const GameSimulation& simulation, const std::vector<std::string>& loadOrder)
        {
            std::vector<bool> flags;
            flags.reserve(loadOrder.size());
            for (const auto& name : loadOrder)
            {
                auto it = simulation.unitDefinitions.find(name);
                flags.push_back(it != simulation.unitDefinitions.end() && it->second.canFly);
            }
            return flags;
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
        struct UnitRecord
        {
            PlayerId owner;
            uint16_t typeIndex{0};

            /** The mover state last put on the wire, so a change can be told. */
            Mover lastMover{TadGroundPath{false, {}}};
            bool moverSent{false};
        };

        /** The M3 slots: M4 fills the projectile and settle halves. */
        struct TickRecords
        {
            std::vector<TadBytes> unitPass;
            std::vector<TadBytes> projectilePass;
            std::vector<TadBytes> settle;
        };

        Impl(const std::filesystem::path& path, const GameSimulation& simulation, DemoRecorderSettings settings)
            : writer(path, makeWriterSettings(simulation, settings)),
              settings(std::move(settings)),
              ids(this->settings.maxUnits),
              layout(tadUnitStateLayout(canFlyFlags(simulation, this->settings.unitLoadOrder), this->settings.maxUnits))
        {
            init(simulation);
        }

        Impl(std::ostream& stream, const GameSimulation& simulation, DemoRecorderSettings settings)
            : writer(stream, makeWriterSettings(simulation, settings)),
              settings(std::move(settings)),
              ids(this->settings.maxUnits),
              layout(tadUnitStateLayout(canFlyFlags(simulation, this->settings.unitLoadOrder), this->settings.maxUnits))
        {
            init(simulation);
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

            for (std::size_t i = 0; i < settings.unitLoadOrder.size(); ++i)
            {
                // Numbered from one: the load order is stored 0-based, the wire
                // is not, and a missing name here would make every type index
                // after it wrong rather than merely absent.
                typeIndexOfName.emplace(settings.unitLoadOrder[i], static_cast<uint16_t>(i + 1));
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
            if (records.count(unit) != 0)
            {
                return;
            }

            const auto& state = simulation.getUnitState(unit);
            auto typeIt = typeIndexOfName.find(state.unitType);
            if (typeIt == typeIndexOfName.end())
            {
                throw std::runtime_error(
                    "DemoRecorder: unit type " + state.unitType + " is not in the data set's unit load order");
            }

            if (!ids.allocate(state.owner, unit))
            {
                throw std::runtime_error(
                    "DemoRecorder: player " + std::to_string(state.owner.value) + "'s unit block is full (maxUnits="
                    + std::to_string(settings.maxUnits) + "); this game cannot be recorded as a demo");
            }

            UnitRecord record;
            record.owner = state.owner;
            record.typeIndex = typeIt->second;
            records.emplace(unit, std::move(record));
        }

        void unitRemoved(UnitId unit)
        {
            records.erase(unit);
            builderOf.erase(unit);
            ids.release(unit);
        }

        /** The DirectPlay id the header gave a player, for a 0x0c's killer. */
        std::optional<uint32_t> dplayIdOf(PlayerId player) const
        {
            for (const auto& [candidate, sender] : playerOrder)
            {
                if (candidate == player)
                {
                    return static_cast<uint32_t>(sender) + 1;
                }
            }
            return std::nullopt;
        }

        void shotFired(
            UnitId shooter,
            unsigned int weaponSlot,
            std::optional<UnitId> targetUnit,
            const SimVector& origin,
            const SimVector& aimPoint,
            const SimVector& direction)
        {
            auto recordIt = records.find(shooter);
            auto shooterId = ids.idOf(shooter);
            if (recordIt == records.end() || !shooterId)
            {
                return;
            }

            uint16_t targetId = 0;
            if (targetUnit)
            {
                targetId = ids.idOf(*targetUnit).value_or(0);
            }

            tickRecords[recordIt->second.owner].unitPass.push_back(tadEncodeShot(TadShot{
                toTadPosition(origin),
                toTadPosition(aimPoint),
                launchRotation(direction),
                targetId,
                *shooterId,
                static_cast<uint8_t>(weaponSlot)}));
        }

        void damageApplied(UnitId victim, std::optional<UnitId> attacker, unsigned int damage)
        {
            auto victimRecordIt = records.find(victim);
            auto victimId = ids.idOf(victim);
            if (victimRecordIt == records.end() || !victimId)
            {
                return;
            }

            // The attacker's owner sends it, so a shot and the damage it caused
            // share one tick clock. With no attacker to name, the victim's
            // owner is the only peer left.
            auto sender = victimRecordIt->second.owner;
            uint16_t attackerId = 0;
            if (attacker)
            {
                if (auto attackerRecordIt = records.find(*attacker); attackerRecordIt != records.end())
                {
                    sender = attackerRecordIt->second.owner;
                    attackerId = ids.idOf(*attacker).value_or(0);
                }
            }

            tickRecords[sender].projectilePass.push_back(tadEncodeDamage(TadDamage{
                *victimId,
                attackerId,
                static_cast<uint16_t>(std::min(damage, 0xffffu)),
                // Not remaining health and not identified (tad_events.h); RWE
                // has nothing to derive it from and writes the zero a fresh
                // record would carry.
                0}));
        }

        void unitDied(UnitId unit, std::optional<UnitId> killer, unsigned int severity, unsigned int cause, unsigned int corpseLevel)
        {
            auto recordIt = records.find(unit);
            auto unitId = ids.idOf(unit);
            if (recordIt == records.end() || !unitId)
            {
                return;
            }

            uint16_t killerId = 0;
            uint32_t killerDplayId = 0xffffffffu;
            if (killer)
            {
                killerId = ids.idOf(*killer).value_or(0);
                if (auto killerRecordIt = records.find(*killer); killerRecordIt != records.end())
                {
                    if (auto dplay = dplayIdOf(killerRecordIt->second.owner))
                    {
                        killerDplayId = *dplay;
                    }
                }
            }

            tickRecords[recordIt->second.owner].projectilePass.push_back(tadEncodeDeath(TadDeath{
                *unitId,
                killerDplayId,
                killerId,
                static_cast<uint8_t>(std::min(severity, 255u)),
                static_cast<uint8_t>(((cause & 0xfu) << 4) | (corpseLevel & 0xfu))}));
        }

        void unitCaptured(UnitId unit, PlayerId newOwner)
        {
            auto recordIt = records.find(unit);
            if (recordIt == records.end())
            {
                return;
            }

            auto previousOwner = recordIt->second.owner;
            if (previousOwner == newOwner)
            {
                return;
            }

            // Cause 4, severity 0, level 0: the original's own owner-change
            // record, and the corpus's 0x0c section says every cause-4 death
            // reads exactly that.
            if (auto oldId = ids.idOf(unit))
            {
                tickRecords[previousOwner].projectilePass.push_back(tadEncodeDeath(TadDeath{
                    *oldId,
                    0xffffffffu,
                    0,
                    0,
                    4u << 4u}));
            }

            // The new owner's block seats it under a new id. It has no 0x09 --
            // a capture is not a nanoframe -- so a receiver learns of it from
            // the new block's next 0x2c, which is why the mover is marked
            // unsent: that record is the only one that will carry its type.
            ids.release(unit);
            if (!ids.allocate(newOwner, unit))
            {
                throw std::runtime_error(
                    "DemoRecorder: player " + std::to_string(newOwner.value) + "'s unit block is full (maxUnits="
                    + std::to_string(settings.maxUnits) + "); this game cannot be recorded as a demo");
            }
            recordIt->second.owner = newOwner;
            recordIt->second.moverSent = false;
        }

        void buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit)
        {
            auto builderId = ids.idOf(builder);
            auto unitId = ids.idOf(unit);
            auto recordIt = records.find(unit);
            if (!builderId || !unitId || recordIt == records.end())
            {
                throw std::runtime_error("DemoRecorder: a build started for a unit the recorder has not been told about");
            }

            const auto& state = simulation.getUnitState(unit);
            tickRecords[recordIt->second.owner].unitPass.push_back(tadEncodeBuildStarted(TadBuildStarted{
                recordIt->second.typeIndex,
                *unitId,
                toTadPosition(state.position),
                toTadRotation(state)}));

            // The 0x09 names the frame and not its builder; the 0x12 names
            // both, so this is the only place the pairing can be learned.
            builderOf[unit] = *builderId;
        }

        /**
         * Completion is read off each frame's own build progress rather than
         * off UnitCompleteEvent, and for the same reason the builder is
         * remembered: the event names the unit and not who finished it. A
         * frame that was never a 0x09 (one standing when recording began)
         * has no entry here and completes silently.
         */
        void emitBuildFinished(const GameSimulation& simulation)
        {
            // Walked in unit order rather than in `builderOf`'s hash order, so
            // two frames completing on one tick go out in the order the unit
            // pass would have produced them.
            for (auto& [unitId, unit] : simulation.units)
            {
                auto it = builderOf.find(unitId);
                if (it == builderOf.end())
                {
                    continue;
                }

                if (unit.isBeingBuilt(simulation.unitDefinitions.at(unit.unitType)))
                {
                    continue;
                }

                if (auto id = ids.idOf(unitId))
                {
                    tickRecords[unit.owner].unitPass.push_back(
                        tadEncodeBuildFinished(TadBuildFinished{*id, it->second}));
                }
                builderOf.erase(it);
            }
        }

        /**
         * What "the mover changed" means here, since the original reads a
         * dirty bit RWE does not keep: a unit's next three waypoints and its
         * blocked flag, or an aircraft's mode, sent when any of that differs
         * from what was last put on the wire. A unit that has never been sent
         * counts as changed, so every unit gets one entry and a unit that
         * stopped gets an empty path. A change deferred by the 512-byte cap
         * stays pending, because only what went out is marked sent.
         */
        Mover moverFor(const GameSimulation& simulation, UnitId unitId, const UnitState& unit) const
        {
            const auto& record = records.at(unitId);
            bool canFly = layout.canFly[record.typeIndex];

            if (canFly)
            {
                TadAirMover air;
                air.goal = std::monostate{};
                air.movementMode = simulation.flyingUnitsSet.count(unitId) != 0 ? 2 : 1;
                return air;
            }

            TadGroundPath path;
            path.blocked = unit.inCollision;

            // `currentWaypoint` is the corner being headed for and the one
            // behind it is where the unit came from -- the original's wp[1] and
            // wp[0] (TOTALA-EXE.md section 102). The wire carries wp[1] onward,
            // which is why the walk starts at the iterator itself.
            if (const auto* moving = std::get_if<NavigationStateMoving>(&unit.navigationState.state))
            {
                if (moving->path)
                {
                    auto it = moving->path->currentWaypoint;
                    auto end = moving->path->path.waypoints.end();
                    for (int i = 0; i < 3 && it != end; ++i, ++it)
                    {
                        path.waypoints.push_back(TadWaypoint{
                            static_cast<int16_t>(std::lround(simScalarToFloat(it->x))),
                            static_cast<int16_t>(std::lround(simScalarToFloat(it->z)))});
                    }
                }
            }

            return path;
        }

        void fillSync(const GameSimulation& simulation, UnitId unitId, const UnitState& unit, TadUnitSync& sync) const
        {
            const auto& definition = simulation.unitDefinitions.at(unit.unitType);
            sync.typeIndex = records.at(unitId).typeIndex;
            sync.health = static_cast<uint16_t>(std::min(unit.hitPoints, 0xffffu));

            // 0 once complete, else 1 + trunc(254 * remaining), and remaining
            // is the original's countdown, which RWE stores as the fraction
            // completed.
            sync.buildProgress = 0;
            if (unit.isBeingBuilt(definition))
            {
                auto complete = std::clamp(unit.getPreciseCompletePercent(definition), 0.0f, 1.0f);
                sync.buildProgress = static_cast<uint8_t>(1 + static_cast<unsigned int>(254.0f * (1.0f - complete)));
            }

            uint8_t flags = 0;
            if (unit.armored)
            {
                flags |= 0x02;
            }
            if (unit.isParalyzed(simulation.gameTime))
            {
                flags |= 0x10;
            }
            sync.flags10E = flags;

            sync.motionState = simulation.flyingUnitsSet.count(unitId) != 0
                ? 2
                : (definition.isMobile || definition.canFly ? 1 : 0);

            sync.carried = std::nullopt;
            if (unit.carriedBy)
            {
                if (auto carrierId = ids.idOf(*unit.carriedBy))
                {
                    // The wire carries a model piece index where RWE keeps a
                    // piece name; zero is the transport's own origin, which is
                    // what an empty name means here too.
                    int8_t piece = 0;
                    if (auto carrier = simulation.tryGetUnitState(*unit.carriedBy))
                    {
                        const auto& carrierUnit = carrier->get();
                        auto pieceIt = carrierUnit.pieceNameToIndices.find(unit.carriedPiece);
                        if (pieceIt != carrierUnit.pieceNameToIndices.end())
                        {
                            piece = static_cast<int8_t>(pieceIt->second);
                        }
                    }
                    sync.carried = TadCarried{*carrierId, piece};
                }
            }

            sync.position = toTadPosition(unit.position);
            sync.rotation = toTadRotation(unit);

            sync.speed = std::nullopt;
            if (hasMover(definition))
            {
                sync.speed = simScalarToFixed(speedOf(unit));
            }
        }

        /**
         * A sender's own resource state for the 0x28: the four settled slots
         * exactly, and the two cumulative triples from the produced and excess
         * figures the end-of-game chart reads.
         *
         * WHICH TRIPLE SLOT IS WHICH is not settled anywhere -- the corpus's
         * six floats are named only by position (TadResourceStats) -- so the
         * first slot is the produced total, the second what the cap threw
         * away, and the third the difference, which is what was actually put
         * to use. No L2 tool reads them; the JSON dump carries them for the
         * argument that would name them.
         */
        TadResourceStats resourceStatsFor(const GameSimulation& simulation, PlayerId player) const
        {
            const auto& info = simulation.getPlayer(player);
            TadResourceStats stats{};
            stats.metalStored = info.metal.value;
            stats.energyStored = info.energy.value;
            stats.metalStorage = info.maxMetal.value;
            stats.energyStorage = info.maxEnergy.value;
            stats.energyCounters[0] = info.energyProduced.value;
            stats.energyCounters[1] = info.energyExcess.value;
            stats.energyCounters[2] = info.energyProduced.value - info.energyExcess.value;
            stats.metalCounters[0] = info.metalProduced.value;
            stats.metalCounters[1] = info.metalExcess.value;
            stats.metalCounters[2] = info.metalProduced.value - info.metalExcess.value;
            return stats;
        }

        void endOfTick(const GameSimulation& simulation)
        {
            emitBuildFinished(simulation);

            const auto tick = static_cast<uint32_t>(simulation.gameTime.value);
            const auto syncIndex = static_cast<uint16_t>(tick % settings.maxUnits);
            const bool sampleResources = tick != 0 && tick % ResourceSampleTicks == 0;

            for (const auto& [player, sender] : playerOrder)
            {
                auto& tickRecordsForPlayer = tickRecords[player];

                TadUnitState state;
                state.tick = tick;

                struct Candidate
                {
                    UnitId unit;
                    Mover mover;
                };
                std::vector<Candidate> candidates;
                for (auto& [unitId, unit] : simulation.units)
                {
                    if (unit.owner != player)
                    {
                        continue;
                    }

                    auto recordIt = records.find(unitId);
                    if (recordIt == records.end())
                    {
                        continue;
                    }

                    auto mover = moverFor(simulation, unitId, unit);
                    if (recordIt->second.moverSent && sameMover(recordIt->second.lastMover, mover))
                    {
                        continue;
                    }
                    candidates.push_back(Candidate{unitId, std::move(mover)});
                }

                std::size_t included = 0;
                for (; included < candidates.size(); ++included)
                {
                    auto index = ids.indexOf(candidates[included].unit);
                    state.updates.push_back(TadUnitUpdate{
                        index.value_or(0),
                        records.at(candidates[included].unit).typeIndex,
                        candidates[included].mover});
                    if (tadEncodeUnitState(state, layout).size() > MaxUnitStateSubPacketBytes)
                    {
                        state.updates.pop_back();
                        break;
                    }
                }
                for (std::size_t i = 0; i < included; ++i)
                {
                    auto& record = records.at(candidates[i].unit);
                    record.lastMover = std::move(candidates[i].mover);
                    record.moverSent = true;
                }

                TadUnitSync sync;
                sync.index = syncIndex;
                sync.typeIndex = 0;
                for (auto& [unitId, unit] : simulation.units)
                {
                    if (unit.owner != player)
                    {
                        continue;
                    }
                    auto index = ids.indexOf(unitId);
                    if (index && *index == syncIndex)
                    {
                        fillSync(simulation, unitId, unit, sync);
                        break;
                    }
                }
                state.sync = sync;

                auto stateBytes = tadEncodeUnitState(state, layout);
                if (stateBytes.empty())
                {
                    throw std::runtime_error("DemoRecorder: a unit state record would not encode");
                }

                if (sampleResources)
                {
                    tickRecordsForPlayer.settle.push_back(tadEncodeResourceStats(resourceStatsFor(simulation, player)));
                }

                std::vector<TadBytes> payload;
                payload.reserve(tickRecordsForPlayer.unitPass.size() + tickRecordsForPlayer.projectilePass.size() + tickRecordsForPlayer.settle.size() + 2);
                // The initial speed record goes out before everything else,
                // which is where the corpus's is: its game-start 0x19s sit in
                // packets with no 0x2c at all, so a consumer stamps them tick 0.
                if (!speedSent)
                {
                    payload.push_back(tadEncodeSpeed(TadSpeed{NormalSpeedValue}));
                }
                payload.insert(payload.end(), tickRecordsForPlayer.unitPass.begin(), tickRecordsForPlayer.unitPass.end());
                payload.push_back(std::move(stateBytes));
                payload.insert(payload.end(), tickRecordsForPlayer.projectilePass.begin(), tickRecordsForPlayer.projectilePass.end());
                payload.insert(payload.end(), tickRecordsForPlayer.settle.begin(), tickRecordsForPlayer.settle.end());

                writer.writePacket(static_cast<uint16_t>(SimMillisecondsPerTick), sender, payload);
            }

            speedSent = true;
            tickRecords.clear();
        }

        /**
         * Declared before `settings` because the writer's settings come from
         * the constructor's parameter, not the member, and the member's is
         * moved into place after. Reversing the two would let the move empty
         * the map name before the writer read it.
         */
        TadWriter writer;

        DemoRecorderSettings settings;

        DemoIdAllocator ids;
        TadUnitStateLayout layout;

        std::unordered_map<std::string, uint16_t> typeIndexOfName;
        std::unordered_map<UnitId, UnitRecord> records;

        /** Built unit -> the demo id of its builder, for the 0x12. */
        std::unordered_map<UnitId, uint16_t> builderOf;

        std::vector<std::pair<PlayerId, uint8_t>> playerOrder;
        std::unordered_map<PlayerId, TickRecords> tickRecords;

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

    void DemoRecorder::damageApplied(const GameSimulation& /*simulation*/, UnitId victim, std::optional<UnitId> attacker, unsigned int damage)
    {
        impl->damageApplied(victim, attacker, damage);
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

    void DemoRecorder::unitCaptured(const GameSimulation& /*simulation*/, UnitId unit, PlayerId newOwner)
    {
        impl->unitCaptured(unit, newOwner);
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
