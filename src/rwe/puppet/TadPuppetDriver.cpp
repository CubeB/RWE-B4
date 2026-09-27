#include <rwe/puppet/TadPuppetDriver.h>
#include <cmath>
#include <rwe/io/tad/tad_events.h>
#include <rwe/pathfinding/UnitPath.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    namespace
    {
        uint32_t puppetKey(uint8_t sender, uint16_t index)
        {
            return (static_cast<uint32_t>(sender) << 16) | static_cast<uint32_t>(index);
        }
    }

    struct TadPuppetDriver::Impl
    {
        struct Puppet
        {
            std::string typeName;
            std::optional<UnitId> unit;
            bool placed{false};

            /** The demo's global id for this slot, so the id map can be kept in step. */
            uint16_t demoId{0};

            /** True while the stream has given a type but no position yet. */
            bool unplaced{false};

            /**
             * The last mover the stream sent before a position arrived, so a
             * unit first seen moving does not stand still until it next
             * changes its mind.
             */
            std::optional<std::variant<TadGroundPath, TadAirMover>> pendingMover;
        };

        GameSimulation& sim;
        uint16_t maxUnits;
        std::vector<std::string> unitLoadOrder;
        TadUnitStateLayout layout;

        std::unordered_map<uint8_t, PlayerId> senderToPlayer;
        std::unordered_map<unsigned int, uint8_t> senderOfBlock;
        std::unordered_map<unsigned int, uint8_t> blockOfSender;

        std::unordered_map<uint32_t, Puppet> puppets;
        std::unordered_map<uint16_t, uint32_t> keyOfId;

        std::optional<uint32_t> baseSerial;
        std::unordered_map<uint8_t, uint32_t> lastSerial;

        std::size_t initialFeatureCount{0};

        TadPuppetStats stats;

        Impl(GameSimulation& sim, uint16_t maxUnits, std::vector<std::string> unitLoadOrder)
            : sim(sim),
              maxUnits(maxUnits),
              unitLoadOrder(std::move(unitLoadOrder))
        {
            std::vector<bool> canFly;
            canFly.reserve(this->unitLoadOrder.size());
            for (const auto& name : this->unitLoadOrder)
            {
                auto it = sim.unitDefinitions.find(name);
                canFly.push_back(it != sim.unitDefinitions.end() && it->second.canFly);
            }
            layout = tadUnitStateLayout(canFly, maxUnits);
            initialFeatureCount = sim.features.slotCount();
        }

        std::optional<std::string> typeNameFor(uint16_t typeIndex) const
        {
            auto name = tadUnitNameForTypeIndex(unitLoadOrder, typeIndex);
            if (!name || sim.unitDefinitions.count(*name) == 0)
            {
                return std::nullopt;
            }
            return name;
        }

        void learnBlock(uint8_t sender, unsigned int block)
        {
            senderOfBlock.emplace(block, sender);
            blockOfSender.emplace(sender, block);
        }

        /**
         * The wire carries TA's world coordinates, whose origin is the map's
         * top left; RWE's world is centred on the origin. The half-extents
         * come off here, the inverse of what DemoRecorder puts on.
         */
        SimVector toSimPosition(const TadPosition& p) const
        {
            return sim.terrain.topLeftCoordinateToWorld(SimVector(
                simScalarFromFixed(p.x),
                simScalarFromFixed(p.y),
                simScalarFromFixed(p.z)));
        }

        Puppet& ensurePuppet(uint8_t sender, uint16_t index, const std::string& typeName)
        {
            auto key = puppetKey(sender, index);
            auto [it, inserted] = puppets.try_emplace(key);
            if (inserted)
            {
                it->second.typeName = typeName;
                it->second.unplaced = true;
                ++stats.unplacedUnits;
            }
            return it->second;
        }

        void markPlaced(Puppet& puppet)
        {
            if (puppet.unplaced)
            {
                puppet.unplaced = false;
                --stats.unplacedUnits;
            }
        }

        void dropPuppet(std::unordered_map<uint32_t, Puppet>::iterator it)
        {
            if (it->second.unplaced)
            {
                --stats.unplacedUnits;
            }
            if (it->second.demoId != 0)
            {
                keyOfId.erase(it->second.demoId);
            }
            puppets.erase(it);
        }

        /**
         * The RWE unit a puppet's demo id currently names, or nothing if the
         * slot has been freed and possibly handed to another unit. UnitIds are
         * recycled, so the type has to match too: a record for an id the sim
         * has already let go names no unit of ours.
         */
        std::optional<UnitId> liveUnitOf(const Puppet& puppet) const
        {
            if (!puppet.unit)
            {
                return std::nullopt;
            }
            auto ref = sim.tryGetUnitState(*puppet.unit);
            if (!ref || ref->get().unitType != puppet.typeName)
            {
                return std::nullopt;
            }
            return puppet.unit;
        }

        /**
         * Takes a flying unit back out of the occupied grid.
         *
         * The simulation puts every mobile unit into the grid when it spawns,
         * but a flying unit's per-tick move sets its position without touching
         * the grid, and the death sweep skips anything airborne. So the cells
         * it was given at spawn would stay pointed at it for ever, blocking
         * ground units and naming a freed id to any later blast. Airborne
         * units do not collide, so the cells are not needed.
         */
        void clearOccupiedCells(const UnitId& unitId, const UnitDefinition& definition)
        {
            auto ref = sim.tryGetUnitState(unitId);
            if (!ref)
            {
                return;
            }
            auto region = sim.occupiedGrid.tryToRegion(sim.computeFootprintRegion(ref->get().position, definition.movementCollisionInfo));
            if (!region)
            {
                return;
            }
            sim.occupiedGrid.forEach(*region, [&](auto& cell) {
                if (cell.mobileUnitId == unitId)
                {
                    cell.mobileUnitId = std::nullopt;
                }
            });
        }

        /** Moves a puppet's ground footprint to a snapped position. */
        void moveOccupiedCells(const UnitId& unitId, const SimVector& from, const SimVector& to, const UnitDefinition& definition)
        {
            auto oldRegion = sim.occupiedGrid.tryToRegion(sim.computeFootprintRegion(from, definition.movementCollisionInfo));
            auto newRegion = sim.occupiedGrid.tryToRegion(sim.computeFootprintRegion(to, definition.movementCollisionInfo));
            if (oldRegion)
            {
                sim.occupiedGrid.forEach(*oldRegion, [&](auto& cell) {
                    if (cell.mobileUnitId == unitId)
                    {
                        cell.mobileUnitId = std::nullopt;
                    }
                });
            }
            if (newRegion)
            {
                sim.occupiedGrid.forEach(*newRegion, [&](auto& cell) { cell.mobileUnitId = unitId; });
            }
        }



        void advanceTo(uint32_t serial)
        {
            if (!baseSerial)
            {
                baseSerial = serial;
            }

            // The serial is a sender-local clock; align it to the simulation's
            // own tick count on the first record so a demo that does not start
            // at zero is not replayed with a gap in front of it.
            auto target = serial >= *baseSerial ? serial - *baseSerial : 0u;
            while (sim.gameTime.value < target)
            {
                sim.tick();
                ++stats.ticksPlayed;
            }
        }

        void applyBuildStarted(uint8_t sender, const TadBytes& subPacket)
        {
            auto e = tadDecodeBuildStarted(subPacket);
            if (!e)
            {
                return;
            }

            auto block = tadOwnerBlockOfUnitId(e->unitId, maxUnits);
            if (!block)
            {
                ++stats.recordsDroppedBadId;
                return;
            }
            learnBlock(sender, *block);

            auto key = puppetKey(sender, static_cast<uint16_t>((e->unitId - 1) % maxUnits));
            if (puppets.count(key) != 0)
            {
                // The corpus has duplicate 0x09s; the first one wins.
                return;
            }

            auto typeName = typeNameFor(e->typeIndex);
            if (!typeName)
            {
                ++stats.recordsDroppedBadType;
                return;
            }

            auto player = senderToPlayer.find(sender);
            if (player == senderToPlayer.end())
            {
                return;
            }

            auto& puppet = ensurePuppet(sender, static_cast<uint16_t>((e->unitId - 1) % maxUnits), *typeName);
            auto unitId = sim.trySpawnUnit(
                *typeName,
                player->second,
                toSimPosition(e->position),
                SimAngle(static_cast<uint16_t>(e->rotation.y)));
            if (!unitId)
            {
                ++stats.spawnsRefused;
                return;
            }

            puppet.unit = *unitId;
            puppet.placed = true;
            puppet.demoId = e->unitId;
            markPlaced(puppet);
            keyOfId[e->unitId] = key;
            if (const auto& definition = sim.unitDefinitions.at(*typeName); definition.canFly)
            {
                clearOccupiedCells(*unitId, definition);
            }
            ++stats.unitsSpawned;
        }

        void applyBuildFinished(uint8_t, const TadBytes& subPacket)
        {
            auto e = tadDecodeBuildFinished(subPacket);
            if (!e)
            {
                return;
            }

            auto key = keyOfId.find(e->unitId);
            if (key == keyOfId.end())
            {
                ++stats.recordsDroppedUnknownUnit;
                return;
            }
            auto puppetIt = puppets.find(key->second);
            if (puppetIt == puppets.end())
            {
                return;
            }
            auto live = liveUnitOf(puppetIt->second);
            if (!live)
            {
                dropPuppet(puppetIt);
                return;
            }

            auto& unit = sim.getUnitState(*live);
            unit.finishBuilding(sim.unitDefinitions.at(unit.unitType));
            ++stats.unitsFinished;
        }

        void applyDeath(const TadBytes& subPacket)
        {
            auto e = tadDecodeDeath(subPacket);
            if (!e)
            {
                return;
            }

            auto block = tadOwnerBlockOfUnitId(e->unitId, maxUnits);
            if (!block)
            {
                ++stats.recordsDroppedBadId;
                return;
            }
            if (senderOfBlock.find(*block) == senderOfBlock.end())
            {
                ++stats.recordsDroppedBadBlock;
                return;
            }

            auto key = keyOfId.find(e->unitId);
            if (key == keyOfId.end())
            {
                ++stats.recordsDroppedUnknownUnit;
                return;
            }
            auto puppetIt = puppets.find(key->second);
            if (puppetIt == puppets.end())
            {
                return;
            }
            auto live = liveUnitOf(puppetIt->second);
            if (!live)
            {
                dropPuppet(puppetIt);
                ++stats.recordsDroppedUnknownUnit;
                return;
            }

            auto unitId = *live;
            sim.killUnit(unitId);

            // The owner's record, not the local `Killed` script, says what is
            // left behind: level 0 is nothing, and level n walks the corpse's
            // featuredead chain n-1 steps down. The sim spawns it at the end of
            // the tick from exactly this.
            auto& unit = sim.getUnitState(unitId);
            auto level = e->corpseLevel();
            if (auto* dead = std::get_if<UnitState::LifeStateDead>(&unit.lifeState))
            {
                dead->leaveCorpse = level > 0;
                dead->corpseLevel = level;
            }

            ++stats.unitsKilled;
            dropPuppet(puppetIt);
        }

        void applyDamage(const TadBytes& subPacket)
        {
            auto e = tadDecodeDamage(subPacket);
            if (!e)
            {
                return;
            }

            auto key = keyOfId.find(e->victimId);
            if (key == keyOfId.end())
            {
                ++stats.recordsDroppedUnknownUnit;
                return;
            }
            auto puppetIt = puppets.find(key->second);
            if (puppetIt == puppets.end())
            {
                return;
            }
            auto live = liveUnitOf(puppetIt->second);
            if (!live)
            {
                dropPuppet(puppetIt);
                return;
            }

            applyRemoteDamage(sim, *live, e->damage);
        }

        void applyGroundMover(UnitState& unit, const TadGroundPath& ground)
        {
            if (ground.waypoints.empty())
            {
                unit.navigationState.state = NavigationStateIdle();
                unit.navigationState.desiredDestination = std::nullopt;
                return;
            }

            // The follower steers along the segment from wp[current-1] to
            // wp[current], so the front of the path is where the unit stands
            // now and the replicated waypoints follow it.
            UnitPath path;
            path.waypoints.push_back(unit.position);
            for (const auto& wp : ground.waypoints)
            {
                SimVector p = sim.terrain.topLeftCoordinateToWorld(
                    SimVector(SimScalar(static_cast<float>(wp.x)), 0_ss, SimScalar(static_cast<float>(wp.z))));
                p.y = sim.terrain.getHeightAt(p.x, p.z);
                path.waypoints.push_back(p);
            }

            auto destination = path.waypoints.back();

            NavigationStateMoving moving;
            moving.movementGoal = destination;
            moving.pathDestination = destination;
            moving.path = PathFollowingInfo(std::move(path), sim.gameTime);
            moving.pathRequested = false;
            moving.pathIsStandIn = false;
            moving.wantsPath = false;
            unit.navigationState.state = std::move(moving);
            unit.navigationState.desiredDestination = destination;
        }

        void applyAirMover(UnitState& unit, const TadAirMover& air)
        {
            std::optional<SimVector> goal;
            if (const auto* move = std::get_if<TadMoveGoal>(&air.goal))
            {
                if (move->position)
                {
                    goal = toSimPosition(*move->position);
                }
            }
            else if (const auto* moving = std::get_if<TadMovingGoal>(&air.goal))
            {
                goal = toSimPosition(moving->position);
            }

            unit.navigationState.desiredDestination = goal;
            unit.navigationState.state = NavigationStateIdle();
        }

        void applyMover(UnitState& unit, const std::variant<TadGroundPath, TadAirMover>& mover)
        {
            if (const auto* ground = std::get_if<TadGroundPath>(&mover))
            {
                applyGroundMover(unit, *ground);
            }
            else if (const auto* air = std::get_if<TadAirMover>(&mover))
            {
                applyAirMover(unit, *air);
            }
        }

        void applyUnitState(uint8_t sender, const TadBytes& subPacket)
        {
            auto state = tadDecodeUnitState(subPacket, layout);
            if (!state)
            {
                ++stats.recordsDroppedBadType;
                return;
            }

            for (const auto& update : state->updates)
            {
                auto typeName = typeNameFor(update.typeIndex);
                if (!typeName)
                {
                    ++stats.recordsDroppedBadType;
                    continue;
                }

                auto& puppet = ensurePuppet(sender, update.index, *typeName);
                auto live = liveUnitOf(puppet);
                if (!live)
                {
                    // No position yet, or the unit this slot named has left.
                    // There is nothing to steer; remember the mover and apply
                    // it when a full-state record places the unit.
                    puppet.pendingMover = update.mover;
                    continue;
                }
                applyMover(sim.getUnitState(*live), update.mover);
            }

            if (!state->sync)
            {
                return;
            }
            applySync(sender, *state->sync);
        }

        std::optional<UnitId> carrierUnit(const TadCarried& carried) const
        {
            auto key = keyOfId.find(carried.carrierId);
            if (key == keyOfId.end())
            {
                return std::nullopt;
            }
            auto it = puppets.find(key->second);
            if (it == puppets.end())
            {
                return std::nullopt;
            }
            return liveUnitOf(it->second);
        }

        void applySync(uint8_t sender, const TadUnitSync& sync)
        {
            auto player = senderToPlayer.find(sender);
            if (player == senderToPlayer.end())
            {
                return;
            }

            auto index = static_cast<uint16_t>(sync.index % maxUnits);

            if (sync.typeIndex == 0)
            {
                // An empty slot: forget anything we had there rather than
                // pretending the slot holds a unit the owner does not have.
                // A slot whose unit is still live is left alone, because the
                // recorder writes the empty sync before the death record that
                // follows it in the same packet and the death needs its id.
                if (auto it = puppets.find(puppetKey(sender, index)); it != puppets.end() && !liveUnitOf(it->second))
                {
                    dropPuppet(it);
                }
                return;
            }

            auto typeName = typeNameFor(sync.typeIndex);
            if (!typeName)
            {
                ++stats.recordsDroppedBadType;
                return;
            }

            auto key = puppetKey(sender, index);
            auto& puppet = ensurePuppet(sender, index, *typeName);

            auto live = liveUnitOf(puppet);
            if (!live)
            {
                // The unit this slot named has left the simulation; the record
                // is about whatever the owner has there now, so place it afresh.
                puppet.unit = std::nullopt;
            }

            if (!live)
            {
                // A unit that existed before the recording began: first seen
                // in this full-state record, so spawn it complete here.
                SimVector position(0_ss, 0_ss, 0_ss);
                if (sync.carried)
                {
                    auto carrier = carrierUnit(*sync.carried);
                    if (!carrier)
                    {
                        // No position and no carrier to borrow one from; the
                        // stream has not told us where it is, so do not invent.
                        return;
                    }
                    position = sim.getUnitState(*carrier).position;
                }
                else
                {
                    position = toSimPosition(sync.position);
                }

                auto unitId = sim.trySpawnCompletedUnit(
                    *typeName,
                    player->second,
                    position,
                    SimAngle(static_cast<uint16_t>(sync.rotation.y)));
                if (!unitId)
                {
                    ++stats.spawnsRefused;
                    return;
                }
                puppet.unit = *unitId;
                puppet.placed = !sync.carried;
                markPlaced(puppet);
                if (const auto& definition = sim.unitDefinitions.at(*typeName); definition.canFly)
                {
                    clearOccupiedCells(*unitId, definition);
                }
                if (puppet.pendingMover)
                {
                    applyMover(sim.getUnitState(*unitId), *puppet.pendingMover);
                }
                if (auto block = blockOfSender.find(sender); block != blockOfSender.end())
                {
                    puppet.demoId = tadUnitIdOfIndex(block->second, index, maxUnits);
                    keyOfId[puppet.demoId] = key;
                }
                return;
            }

            auto& unit = sim.getUnitState(*live);
            auto& definition = sim.unitDefinitions.at(unit.unitType);

            if (sync.carried)
            {
                // No position on the wire, so nothing to measure or snap; the
                // unit's carrier decides where it is.
                if (auto carrier = carrierUnit(*sync.carried))
                {
                    auto carrierPosition = sim.getUnitState(*carrier).position;
                    if (definition.isMobile && !definition.canFly)
                    {
                        moveOccupiedCells(*live, unit.position, carrierPosition, definition);
                    }
                    unit.position = carrierPosition;
                }
                return;
            }

            auto recorded = toSimPosition(sync.position);
            if (puppet.placed)
            {
                auto dx = static_cast<double>((unit.position.x - recorded.x).value);
                auto dz = static_cast<double>((unit.position.z - recorded.z).value);
                auto distance = std::sqrt(dx * dx + dz * dz);
                auto& drift = definition.canFly ? stats.airDrift : stats.groundDrift;
                ++drift.samples;
                drift.distances.push_back(distance);
            }

            // Keep the occupied grid with the snapped position, or a ground
            // unit's cells would stay where it was and a later spawn would be
            // refused by a footprint nobody stands on any more. A building
            // keeps its place in the grid as a building, and it does not move,
            // so its position is left where it was placed: snapping it would
            // leave its building cells behind, and the removal at the end of
            // its life would not clear cells it no longer stands on.
            if (definition.isMobile)
            {
                if (!definition.canFly)
                {
                    moveOccupiedCells(*live, unit.position, recorded, definition);
                }
                unit.previousPosition = recorded;
                unit.position = recorded;
            }
            unit.previousRotation = SimAngle(static_cast<uint16_t>(sync.rotation.y));
            unit.rotation = SimAngle(static_cast<uint16_t>(sync.rotation.y));

            if (sync.buildProgress == 0)
            {
                if (unit.isBeingBuilt(definition))
                {
                    unit.finishBuilding(definition);
                }
            }
            else if (unit.isBeingBuilt(definition))
            {
                auto remaining = static_cast<unsigned int>(sync.buildProgress - 1);
                unit.buildTimeCompleted = definition.buildTime - (definition.buildTime * remaining) / 254u;
                if (unit.buildTimeCompleted > definition.buildTime)
                {
                    unit.buildTimeCompleted = definition.buildTime;
                }
            }

            unit.hitPoints = sync.health;
            puppet.placed = true;
        }
    };

    TadPuppetDriver::TadPuppetDriver(
        GameSimulation& simulation,
        uint16_t maxUnits,
        std::vector<std::string> unitLoadOrder)
        : impl(std::make_unique<Impl>(simulation, maxUnits, std::move(unitLoadOrder)))
    {
    }

    TadPuppetDriver::~TadPuppetDriver() = default;

    void TadPuppetDriver::addPlayer(uint8_t sender, PlayerId player)
    {
        impl->senderToPlayer[sender] = player;
        impl->sim.getPlayer(player).simulation = PlayerSimulation::Remote;
    }

    void TadPuppetDriver::onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets)
    {
        ++impl->stats.packets;

        std::optional<uint32_t> serial;
        for (const auto& subPacket : subPackets)
        {
            if (subPacket.size() >= 7 && static_cast<TadSubPacketCode>(subPacket[0]) == TadSubPacketCode::UnitStatAndMove)
            {
                if (auto state = tadDecodeUnitState(subPacket, impl->layout))
                {
                    serial = state->tick;
                    break;
                }
            }
        }

        if (!serial)
        {
            auto last = impl->lastSerial.find(packet.sender);
            if (last != impl->lastSerial.end())
            {
                serial = last->second;
            }
            else
            {
                ++impl->stats.packetsWithoutClock;
            }
        }

        if (serial)
        {
            impl->advanceTo(*serial);
            impl->lastSerial[packet.sender] = *serial;
        }

        for (const auto& subPacket : subPackets)
        {
            if (subPacket.empty())
            {
                continue;
            }

            switch (static_cast<TadSubPacketCode>(subPacket[0]))
            {
                case TadSubPacketCode::UnitBuildStarted:
                    impl->applyBuildStarted(packet.sender, subPacket);
                    break;
                case TadSubPacketCode::UnitBuildFinished:
                    impl->applyBuildFinished(packet.sender, subPacket);
                    break;
                case TadSubPacketCode::UnitKilled:
                    impl->applyDeath(subPacket);
                    break;
                case TadSubPacketCode::UnitTakeDamage:
                    impl->applyDamage(subPacket);
                    break;
                case TadSubPacketCode::UnitStatAndMove:
                    impl->applyUnitState(packet.sender, subPacket);
                    break;
                default:
                    break;
            }
        }

        if (impl->sim.features.slotCount() > impl->initialFeatureCount)
        {
            impl->stats.wrecksLeft = impl->sim.features.slotCount() - impl->initialFeatureCount;
        }
    }

    const TadPuppetStats& TadPuppetDriver::stats() const
    {
        return impl->stats;
    }
}
