#include "TaWireTape.h"

#include <algorithm>
#include <cmath>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/match.h>
#include <rwe/util/rwe_string.h>
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

        /** What a ground or air mover serialiser will write, as the tape stores it. */
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

        /**
         * RWE's world is centred on the origin; TA's starts at the map's top
         * left. The wire carries TA's, so the half-extents go back on here and
         * come off again in the puppet driver.
         */
        TadPosition toTadPosition(const SimVector& position, SimScalar halfWidth, SimScalar halfHeight)
        {
            return TadPosition{
                simScalarToFixed(position.x + halfWidth),
                simScalarToFixed(position.y),
                simScalarToFixed(position.z + halfHeight)};
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
    }

    struct TaWireTape::Impl
    {
        struct UnitRecord
        {
            PlayerId owner;
            uint16_t typeIndex{0};

            /** The mover state last put on the wire, so a change can be told. */
            Mover lastMover{TadGroundPath{false, {}}};
            bool moverSent{false};
        };

        struct TickRecords
        {
            std::vector<TadBytes> unitPass;
            std::vector<TadBytes> projectilePass;
            std::vector<TadBytes> settle;
        };

        Impl(
            const GameSimulation& simulation,
            TaWireTapeSettings settings,
            std::vector<PlayerId> describedPlayers,
            TaPeerIds peerIds)
            : settings(std::move(settings)),
              ids(this->settings.maxUnits),
              peerIds(std::move(peerIds)),
              described(std::move(describedPlayers)),
              layout(tadUnitStateLayout(canFlyFlags(simulation, this->settings.unitLoadOrder), this->settings.maxUnits))
        {
            if (this->settings.maxUnits == 0)
            {
                throw std::runtime_error("TaWireTape: maxUnits must be at least one");
            }
            if (this->settings.unitLoadOrder.empty())
            {
                throw std::runtime_error("TaWireTape: the data set's unit load order is empty; there is nothing to index types by");
            }

            playerCount = simulation.players.size();

            for (std::size_t i = 0; i < this->settings.unitLoadOrder.size(); ++i)
            {
                // Numbered from one: the load order is stored 0-based, the wire
                // is not, and a missing name here would make every type index
                // after it wrong rather than merely absent.
                typeIndexOfName.emplace(this->settings.unitLoadOrder[i], static_cast<uint16_t>(i + 1));
            }

            halfWidth = simulation.terrain.getWidthInWorldUnits() / 2_ss;
            halfHeight = simulation.terrain.getHeightInWorldUnits() / 2_ss;

            for (const auto& entry : simulation.units)
            {
                unitCreated(simulation, entry.first);
            }
        }

        bool describes(PlayerId player) const
        {
            return std::find(described.begin(), described.end(), player) != described.end();
        }

        /**
         * The id out of our own allocator, or the peer's where it is not ours
         * to seat. A unit that has just left answers from the id it held: a
         * death is recorded at the moment the unit dies and the sweep that
         * frees its slot runs later in the same tick, so the record naming it
         * is the one thing that still knows what it was called.
         */
        std::optional<uint16_t> idOf(UnitId unit) const
        {
            if (auto id = ids.idOf(unit))
            {
                return id;
            }
            if (auto released = releasedIds.find(unit); released != releasedIds.end())
            {
                return released->second;
            }
            if (peerIds.unitId)
            {
                return peerIds.unitId(unit);
            }
            return std::nullopt;
        }

        /**
         * The DirectPlay id a 0x0c's killer is named by. RWE has none of its
         * own, so a player this tape describes is numbered from one -- as
         * synthetic as the ids themselves -- and anyone else is the peer's to
         * name or nobody's.
         */
        std::optional<uint32_t> dplayIdOf(PlayerId player) const
        {
            if (player.value >= playerCount)
            {
                return std::nullopt;
            }
            if (describes(player))
            {
                return static_cast<uint32_t>(player.value) + 1;
            }
            if (peerIds.dplayId)
            {
                return peerIds.dplayId(player);
            }
            return std::nullopt;
        }

        TaWireRefusal unitCreated(const GameSimulation& simulation, UnitId unit)
        {
            if (records.count(unit) != 0)
            {
                return TaWireRefusal::None;
            }

            const auto& state = simulation.getUnitState(unit);
            auto typeIt = typeIndexOfName.find(state.unitType);
            if (typeIt == typeIndexOfName.end())
            {
                return TaWireRefusal::UnknownType;
            }

            if (describes(state.owner) && !ids.allocate(state.owner, unit))
            {
                return TaWireRefusal::BlockFull;
            }

            UnitRecord record;
            record.owner = state.owner;
            record.typeIndex = typeIt->second;
            records.emplace(unit, std::move(record));
            // The id is the UnitId's now, not the one it was called by.
            releasedIds.erase(unit);
            return TaWireRefusal::None;
        }

        void unitRemoved(UnitId unit)
        {
            records.erase(unit);
            builderOf.erase(unit);
            if (auto id = ids.idOf(unit))
            {
                releasedIds[unit] = *id;
            }
            ids.release(unit);
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
            auto shooterId = idOf(shooter);
            if (recordIt == records.end() || !shooterId)
            {
                return;
            }

            uint16_t targetId = 0;
            if (targetUnit)
            {
                targetId = idOf(*targetUnit).value_or(0);
            }

            tickRecords[recordIt->second.owner].unitPass.push_back(tadEncodeShot(TadShot{
                toTadPosition(origin, halfWidth, halfHeight),
                toTadPosition(aimPoint, halfWidth, halfHeight),
                launchRotation(direction),
                targetId,
                *shooterId,
                static_cast<uint8_t>(weaponSlot)}));
        }

        void damageApplied(UnitId victim, std::optional<UnitId> attacker, unsigned int damage, std::optional<PlayerId> sourceOwner)
        {
            auto victimRecordIt = records.find(victim);
            auto victimId = idOf(victim);
            if (victimRecordIt == records.end() || !victimId)
            {
                return;
            }

            auto sender = sourceOwner.value_or(victimRecordIt->second.owner);
            uint16_t attackerId = 0;
            if (attacker)
            {
                auto attackerRecordIt = records.find(*attacker);
                if (attackerRecordIt != records.end())
                {
                    sender = attackerRecordIt->second.owner;
                    attackerId = idOf(*attacker).value_or(0);
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
            auto unitId = idOf(unit);
            if (recordIt == records.end() || !unitId)
            {
                return;
            }

            uint16_t killerId = 0;
            uint32_t killerDplayId = 0xffffffffu;
            if (killer)
            {
                killerId = idOf(*killer).value_or(0);
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

        TaWireRefusal unitCaptured(UnitId unit, PlayerId newOwner)
        {
            auto recordIt = records.find(unit);
            if (recordIt == records.end())
            {
                return TaWireRefusal::None;
            }

            auto previousOwner = recordIt->second.owner;
            if (previousOwner == newOwner)
            {
                return TaWireRefusal::None;
            }

            bool wasOurs = describes(previousOwner);
            bool isOurs = describes(newOwner);

            // The old id is the only thing the peer can delete the unit with,
            // and the original's owner change is a death: cause 4, severity 0,
            // level 0, which the corpus's 0x0c section says every cause-4 death
            // reads exactly.
            if (wasOurs)
            {
                if (auto oldId = ids.idOf(unit))
                {
                    tickRecords[previousOwner].projectilePass.push_back(tadEncodeDeath(TadDeath{
                        *oldId,
                        0xffffffffu,
                        0,
                        0,
                        4u << 4u}));
                }
            }

            // The new block seats it under a fresh id. It has no 0x09 -- a
            // capture is not a nanoframe -- so a receiver learns of it from
            // the new block's next 0x2c, which is why the mover is marked
            // unsent: that record is the only one that will carry its type.
            ids.release(unit);
            if (isOurs && !ids.allocate(newOwner, unit))
            {
                return TaWireRefusal::BlockFull;
            }

            recordIt->second.owner = newOwner;
            recordIt->second.moverSent = false;
            return TaWireRefusal::None;
        }

        bool buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit)
        {
            auto builderId = idOf(builder);
            auto unitId = idOf(unit);
            auto recordIt = records.find(unit);
            if (!builderId || !unitId || recordIt == records.end())
            {
                return false;
            }

            const auto& state = simulation.getUnitState(unit);
            tickRecords[recordIt->second.owner].unitPass.push_back(tadEncodeBuildStarted(TadBuildStarted{
                recordIt->second.typeIndex,
                *unitId,
                toTadPosition(state.position, halfWidth, halfHeight),
                toTadRotation(state)}));

            // The 0x09 names the frame and not its builder; the 0x12 names
            // both, so this is the only place the pairing can be learned.
            builderOf[unit] = *builderId;
            return true;
        }

        /**
         * Completion is read off each frame's own build progress rather than
         * off UnitCompleteEvent, and for the same reason the builder is
         * remembered: the event names the unit and not who finished it. A
         * frame that was never a 0x09 (one standing when the tape was made)
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
                            static_cast<int16_t>(std::lround(simScalarToFloat(it->x) + simScalarToFloat(halfWidth))),
                            static_cast<int16_t>(std::lround(simScalarToFloat(it->z) + simScalarToFloat(halfHeight)))});
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
                if (auto carrierId = idOf(*unit.carriedBy))
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

            sync.position = toTadPosition(unit.position, halfWidth, halfHeight);
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

        /**
         * The waypoint entries and the full-state record, in the original's
         * own order: the entries for whatever changed until the 0x2c reaches
         * 512 bytes, and then the record for slot `tick % maxUnits` whether or
         * not anything sits in it. That record is what keeps a peer's copy of
         * the unit alive, so a slot this tape describes must never go out empty
         * while a unit is in it -- an empty record is a deletion.
         */
        TaTickRecords endOfTick(const GameSimulation& simulation, PlayerId player)
        {
            emitBuildFinished(simulation);

            TaTickRecords out;
            auto queued = tickRecords.find(player);
            if (queued != tickRecords.end())
            {
                out.unitPass = std::move(queued->second.unitPass);
                out.projectilePass = std::move(queued->second.projectilePass);
                out.settle = std::move(queued->second.settle);
                tickRecords.erase(queued);
            }

            const auto tick = static_cast<uint32_t>(simulation.gameTime.value);
            const auto syncIndex = static_cast<uint16_t>(tick % settings.maxUnits);

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

            out.unitState = tadEncodeUnitState(state, layout);

            // A death recorded this tick has been sent by now, and a UnitId is
            // recycled, so nothing may still answer for what it used to be.
            releasedIds.clear();

            if (tick != 0 && tick % ResourceSampleTicks == 0)
            {
                out.settle.push_back(tadEncodeResourceStats(resourceStatsFor(simulation, player)));
            }

            return out;
        }

        TaWireTapeSettings settings;
        DemoIdAllocator ids;
        TadUnitStateLayout layout;

        TaPeerIds peerIds;
        std::vector<PlayerId> described;
        std::size_t playerCount{0};

        SimScalar halfWidth{0_ss};
        SimScalar halfHeight{0_ss};

        std::unordered_map<std::string, uint16_t> typeIndexOfName;
        std::unordered_map<UnitId, UnitRecord> records;

        /** Built unit -> the wire id of its builder, for the 0x12. */
        std::unordered_map<UnitId, uint16_t> builderOf;

        /** The id a unit held on the tick it left, for the record that still names it. */
        std::unordered_map<UnitId, uint16_t> releasedIds;

        std::unordered_map<PlayerId, TickRecords> tickRecords;
    };

    TaWireTape::TaWireTape(
        const GameSimulation& simulation,
        TaWireTapeSettings settings,
        std::vector<PlayerId> describedPlayers,
        TaPeerIds peerIds)
        : impl(std::make_unique<Impl>(simulation, std::move(settings), std::move(describedPlayers), std::move(peerIds)))
    {
    }

    TaWireTape::~TaWireTape() = default;

    bool TaWireTape::describes(PlayerId player) const
    {
        return impl->describes(player);
    }

    std::optional<uint16_t> TaWireTape::idOf(UnitId unit) const
    {
        return impl->idOf(unit);
    }

    std::optional<uint32_t> TaWireTape::dplayIdOf(PlayerId player) const
    {
        return impl->dplayIdOf(player);
    }

    uint16_t TaWireTape::maxUnits() const
    {
        return impl->settings.maxUnits;
    }

    TaWireRefusal TaWireTape::unitCreated(const GameSimulation& simulation, UnitId unit)
    {
        return impl->unitCreated(simulation, unit);
    }

    void TaWireTape::unitRemoved(UnitId unit)
    {
        impl->unitRemoved(unit);
    }

    bool TaWireTape::buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit)
    {
        return impl->buildStarted(simulation, builder, unit);
    }

    void TaWireTape::shotFired(
        UnitId shooter,
        unsigned int weaponSlot,
        std::optional<UnitId> targetUnit,
        const SimVector& origin,
        const SimVector& aimPoint,
        const SimVector& direction)
    {
        impl->shotFired(shooter, weaponSlot, targetUnit, origin, aimPoint, direction);
    }

    void TaWireTape::damageApplied(UnitId victim, std::optional<UnitId> attacker, unsigned int damage, std::optional<PlayerId> sourceOwner)
    {
        impl->damageApplied(victim, attacker, damage, sourceOwner);
    }

    void TaWireTape::unitDied(UnitId unit, std::optional<UnitId> killer, unsigned int severity, unsigned int cause, unsigned int corpseLevel)
    {
        impl->unitDied(unit, killer, severity, cause, corpseLevel);
    }

    TaWireRefusal TaWireTape::unitCaptured(UnitId unit, PlayerId newOwner)
    {
        return impl->unitCaptured(unit, newOwner);
    }

    TaTickRecords TaWireTape::endOfTick(const GameSimulation& simulation, PlayerId player)
    {
        return impl->endOfTick(simulation, player);
    }
}
