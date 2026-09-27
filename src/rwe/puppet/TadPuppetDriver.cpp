#include <rwe/puppet/TadPuppetDriver.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <rwe/cob/CobEnvironment.h>
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

            /**
             * A moving goal's per-tick velocity, which the original's resolver
             * adds to the goal position every tick. Advanced in `advanceTo` so
             * an aircraft chasing a running target is not left behind.
             */
            std::optional<SimVector> airGoalVelocity;
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

        /** When set, records are queued by tick and the scene owns the clock. */
        bool externalClock{false};

        struct QueuedPacket
        {
            uint8_t sender;
            std::vector<TadBytes> subPackets;
        };

        /** Records waiting for their tick, in the order the stream gave them. */
        std::map<uint32_t, std::vector<QueuedPacket>> pending;

        /** The highest tick a record has been queued for. */
        std::optional<uint32_t> lastSeenTick;

        std::vector<TadChatLine> chatLines;

        /** The latest 0x19 value waiting to be taken. */
        std::optional<uint16_t> speedChange;

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

        /**
         * Names a puppet by the global id its slot stands for, once its
         * sender's block is known.
         *
         * A unit is often first seen in a 0x2c update, which carries its
         * block-relative index and no global id; its damage and death records
         * carry the global id, so without this the records for a unit that has
         * not yet had its full-state turn read as unknown. The block is
         * learned from the sender's own 0x09, so the arithmetic is exact.
         */
        void assignDemoId(uint8_t sender, uint16_t index, Puppet& puppet)
        {
            if (puppet.demoId != 0)
            {
                return;
            }
            auto block = blockOfSender.find(sender);
            if (block == blockOfSender.end())
            {
                return;
            }
            puppet.demoId = tadUnitIdOfIndex(block->second, index, maxUnits);
            keyOfId[puppet.demoId] = puppetKey(sender, index);
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
         * Puts a freshly spawned aircraft into flight.
         *
         * A unit's physics defaults to the ground variant and its air variant
         * starts in the takeoff state, which the remote path does not steer --
         * it only sets a target on the flying state -- and the ground-to-air
         * transition belongs to the local behavior this unit does not run.
         * Left alone, a puppet aircraft is a ground unit that never moves. This
         * is `UnitBehaviorService::transitionFromGroundToAir` with the state
         * already flying, and a landed entry is put here too: it has no target,
         * so it decelerates where it stands.
         */
        void makeAirborne(const UnitId& unitId, const UnitDefinition& definition)
        {
            auto ref = sim.tryGetUnitState(unitId);
            if (!ref)
            {
                return;
            }
            auto& unit = ref->get();
            unit.physics = UnitPhysicsInfoAir();
            if (auto* air = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
            {
                air->movementState = AirMovementStateFlying();
            }
            clearOccupiedCells(unitId, definition);
            sim.flyingUnitsSet.insert(unitId);
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

        /**
         * Spawns a puppet where its owner recorded it, whether or not the
         * simulation's own placement would allow it.
         *
         * The owner already resolved placement, so a factory's build pad or a
         * peer's footprint must not refuse the unit: the recorded position is
         * authoritative, the same way a remote unit's step is never refused by
         * a stale peer. The blocking cells are cleared and the new unit claims
         * them through the ordinary spawn; a wreck in the way is cleared only
         * after a first attempt without one, so an overlap that is not a wreck
         * does not orphan a feature.
         */
        std::optional<UnitId> placeUnit(
            const std::string& typeName,
            PlayerId player,
            const SimVector& position,
            SimAngle rotation,
            bool completed)
        {
            const auto& definition = sim.unitDefinitions.at(typeName);
            auto region = sim.occupiedGrid.tryToRegion(sim.computeFootprintRegion(position, definition.movementCollisionInfo));
            if (!region)
            {
                return std::nullopt;
            }

            sim.occupiedGrid.forEach(*region, [](auto& cell) {
                cell.mobileUnitId = std::nullopt;
                cell.buildingInfo = std::nullopt;
            });

            auto spawn = [&]() -> std::optional<UnitId> {
                return completed
                    ? sim.trySpawnCompletedUnit(typeName, player, position, rotation)
                    : sim.trySpawnUnit(typeName, player, position, rotation);
            };

            if (auto id = spawn())
            {
                return id;
            }

            sim.occupiedGrid.forEach(*region, [](auto& cell) { cell.featureId = std::nullopt; });
            return spawn();
        }



        /**
         * Moves every running air goal on by one tick.
         *
         * A moving goal is a point the original integrates itself when it is
         * resolved (0x44EA60), x and z only, and never touches the height. The
         * driver applies one record and then ticks the simulation, so the
         * advance has to happen here, between the records.
         */
        void advanceAirGoals()
        {
            for (auto& [key, puppet] : puppets)
            {
                if (!puppet.airGoalVelocity)
                {
                    continue;
                }
                auto live = liveUnitOf(puppet);
                if (!live)
                {
                    continue;
                }
                auto& destination = sim.getUnitState(*live).navigationState.desiredDestination;
                if (destination)
                {
                    if (auto* target = std::get_if<SimVector>(&*destination))
                    {
                        target->x = target->x + puppet.airGoalVelocity->x;
                        target->z = target->z + puppet.airGoalVelocity->z;
                    }
                }
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
                advanceAirGoals();
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
            auto unitId = placeUnit(
                *typeName,
                player->second,
                toSimPosition(e->position),
                SimAngle(static_cast<uint16_t>(e->rotation.y)),
                false);
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
                makeAirborne(*unitId, definition);
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
                // A slot the simulation has already let go is forgotten. One
                // that has not been placed yet is left for its full-state
                // record, which carries the build progress that finishes it.
                if (puppetIt->second.unit)
                {
                    dropPuppet(puppetIt);
                }
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
                ++stats.deathsDroppedNotLive;
                return;
            }
            auto live = liveUnitOf(puppetIt->second);
            if (!live)
            {
                // The owner declares a death for a unit we have not placed yet
                // or have already let go; either way it never stands here.
                ++stats.deathsDroppedNotLive;
                dropPuppet(puppetIt);
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
                // Damage to a slot not placed yet is dropped: the full-state
                // record snaps its health anyway, and `applyRemoteDamage`
                // needs a unit that exists. A slot already let go is forgotten.
                if (puppetIt->second.unit)
                {
                    dropPuppet(puppetIt);
                }
                return;
            }

            applyRemoteDamage(sim, *live, e->damage);
        }

        /**
         * Spawns the round a recorded 0x0d names, from the recorded origin
         * toward the recorded target, for its looks only.
         *
         * The weapon is the shooter's own slot, so the model and flight match
         * what the unit fired. Every demo player is Remote, and the damage path
         * refuses a Remote victim, so this can take nothing off any unit;
         * damage reaches the simulation only through the owner's 0x0b. A shot
         * whose shooter or slot cannot be resolved is counted and dropped.
         */
        void applyShot(const TadBytes& subPacket)
        {
            auto e = tadDecodeShot(subPacket);
            if (!e)
            {
                return;
            }

            auto key = keyOfId.find(e->shooterId);
            if (key == keyOfId.end())
            {
                ++stats.shotsDropped;
                return;
            }
            auto it = puppets.find(key->second);
            if (it == puppets.end())
            {
                ++stats.shotsDropped;
                return;
            }
            auto live = liveUnitOf(it->second);
            if (!live)
            {
                ++stats.shotsDropped;
                return;
            }

            auto& unit = sim.getUnitState(*live);
            if (e->weaponSlot >= unit.weapons.size() || !unit.weapons[e->weaponSlot])
            {
                ++stats.shotsDropped;
                return;
            }
            const auto& weapon = *unit.weapons[e->weaponSlot];
            if (sim.weaponDefinitions.find(weapon.weaponType) == sim.weaponDefinitions.end())
            {
                ++stats.shotsDropped;
                return;
            }

            auto origin = toSimPosition(e->origin);
            auto target = toSimPosition(e->target);
            auto delta = target - origin;
            auto length = delta.length();
            if (length <= 0_ss)
            {
                ++stats.shotsDropped;
                return;
            }

            sim.spawnProjectile(ProjectileSpawn{
                .owner = unit.owner,
                .weapon = &weapon,
                .position = origin,
                .direction = delta.normalizedOr(UnitState::toDirection(unit.rotation)),
                .distanceToTarget = length,
                .attacker = *live,
                .targetPosition = target,
            });
            ++stats.shotsSpawned;
        }

        /**
         * Runs a recorded 0x10 on the puppet's own COB environment, so the
         * animation, activation or build arm the owner's script asked for
         * plays here. The index is into the unit's own script table, which is
         * the order RWE parsed out of the same .cob, and both it and the
         * argument count are bounded before use.
         */
        void applyScriptCall(const TadBytes& subPacket)
        {
            auto e = tadDecodeScriptCall(subPacket);
            if (!e)
            {
                return;
            }

            auto key = keyOfId.find(e->unitId);
            if (key == keyOfId.end())
            {
                ++stats.scriptCallsDropped;
                return;
            }
            auto it = puppets.find(key->second);
            if (it == puppets.end())
            {
                ++stats.scriptCallsDropped;
                return;
            }
            auto live = liveUnitOf(it->second);
            if (!live)
            {
                ++stats.scriptCallsDropped;
                return;
            }

            auto& unit = sim.getUnitState(*live);
            if (!unit.cobEnvironment)
            {
                ++stats.scriptCallsDropped;
                return;
            }
            const auto* script = unit.cobEnvironment->script();
            if (script == nullptr || e->scriptIndex >= script->functions.size())
            {
                ++stats.scriptCallsDropped;
                return;
            }

            auto count = std::min<unsigned int>(e->argCount, 4u);
            std::vector<int> params(e->args, e->args + count);
            unit.cobEnvironment->createThread(e->scriptIndex, params);
            ++stats.scriptCallsRun;
        }

        /** Writes a recorded 0x28 onto the sender's own player, which is whose state it is. */
        void applyResourceStats(uint8_t sender, const TadBytes& subPacket)
        {
            auto e = tadDecodeResourceStats(subPacket);
            if (!e)
            {
                return;
            }
            auto player = senderToPlayer.find(sender);
            if (player == senderToPlayer.end())
            {
                return;
            }

            auto& p = sim.getPlayer(player->second);
            p.metal = Metal(std::max(0.0f, e->metalStored));
            p.energy = Energy(std::max(0.0f, e->energyStored));
            p.maxMetal = Metal(std::max(0.0f, e->metalStorage));
            p.maxEnergy = Energy(std::max(0.0f, e->energyStorage));
        }

        /** Reads a recorded 0x19. The value is stored, not interpreted: what a pause looks like is not settled. */
        void applySpeed(const TadBytes& subPacket)
        {
            auto e = tadDecodeSpeed(subPacket);
            if (!e)
            {
                return;
            }
            ++stats.speedChanges;
            speedChange = e->value;
        }

        void applyChat(uint8_t sender, const TadBytes& subPacket, bool ally)
        {
            std::string text;
            if (ally)
            {
                auto e = tadDecodeAllyChat(subPacket);
                if (!e)
                {
                    return;
                }
                text = std::move(e->text);
            }
            else
            {
                auto e = tadDecodeChat(subPacket);
                if (!e)
                {
                    return;
                }
                text = std::move(e->text);
            }

            if (text.empty())
            {
                return;
            }
            auto player = senderToPlayer.find(sender);
            if (player == senderToPlayer.end())
            {
                return;
            }

            if (ally)
            {
                ++stats.allyChatLines;
            }
            else
            {
                ++stats.chatLines;
            }
            chatLines.push_back(TadChatLine{player->second, ally, std::move(text)});
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

        void applyAirMover(Puppet& puppet, UnitState& unit, const TadAirMover& air)
        {
            puppet.airGoalVelocity = std::nullopt;

            // Mode 1 is landed: a parked aircraft is not flying anywhere, so it
            // gets no target and decelerates where it stands. Mode 2 is flying.
            if (air.movementMode == 1)
            {
                unit.navigationState.desiredDestination = std::nullopt;
                unit.navigationState.state = NavigationStateIdle();
                return;
            }

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
                // The resolver advances the goal by its velocity every tick,
                // x and z only; the height is never touched.
                puppet.airGoalVelocity = SimVector(
                    simScalarFromFixed(moving->velocity.x),
                    0_ss,
                    simScalarFromFixed(moving->velocity.z));
            }

            unit.navigationState.desiredDestination = goal;
            unit.navigationState.state = NavigationStateIdle();
        }

        void applyMover(Puppet& puppet, UnitState& unit, const std::variant<TadGroundPath, TadAirMover>& mover)
        {
            if (const auto* ground = std::get_if<TadGroundPath>(&mover))
            {
                applyGroundMover(unit, *ground);
            }
            else if (const auto* air = std::get_if<TadAirMover>(&mover))
            {
                applyAirMover(puppet, unit, *air);
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
                assignDemoId(sender, update.index, puppet);
                auto live = liveUnitOf(puppet);
                if (!live)
                {
                    // No position yet, or the unit this slot named has left.
                    // There is nothing to steer; remember the mover and apply
                    // it when a full-state record places the unit.
                    puppet.pendingMover = update.mover;
                    continue;
                }
                applyMover(puppet, sim.getUnitState(*live), update.mover);
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

                auto unitId = placeUnit(
                    *typeName,
                    player->second,
                    position,
                    SimAngle(static_cast<uint16_t>(sync.rotation.y)),
                    true);
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
                    makeAirborne(*unitId, definition);
                }
                if (puppet.pendingMover)
                {
                    applyMover(puppet, sim.getUnitState(*unitId), *puppet.pendingMover);
                }
                assignDemoId(sender, index, puppet);
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

        void applySubPackets(uint8_t sender, const std::vector<TadBytes>& subPackets)
        {
            for (const auto& subPacket : subPackets)
            {
                if (subPacket.empty())
                {
                    continue;
                }

                switch (static_cast<TadSubPacketCode>(subPacket[0]))
                {
                    case TadSubPacketCode::UnitBuildStarted:
                        applyBuildStarted(sender, subPacket);
                        break;
                    case TadSubPacketCode::UnitBuildFinished:
                        applyBuildFinished(sender, subPacket);
                        break;
                    case TadSubPacketCode::UnitKilled:
                        applyDeath(subPacket);
                        break;
                    case TadSubPacketCode::UnitTakeDamage:
                        applyDamage(subPacket);
                        break;
                    case TadSubPacketCode::WeaponFired:
                        applyShot(subPacket);
                        break;
                    case TadSubPacketCode::UnitStartScript:
                        applyScriptCall(subPacket);
                        break;
                    case TadSubPacketCode::PlayerResourceInfo:
                        applyResourceStats(sender, subPacket);
                        break;
                    case TadSubPacketCode::Speed:
                        applySpeed(subPacket);
                        break;
                    case TadSubPacketCode::Chat:
                        applyChat(sender, subPacket, false);
                        break;
                    case TadSubPacketCode::AllyChat:
                        applyChat(sender, subPacket, true);
                        break;
                    case TadSubPacketCode::UnitStatAndMove:
                        applyUnitState(sender, subPacket);
                        break;
                    default:
                        break;
                }
            }
        }

        void updateWreckCount()
        {
            if (sim.features.slotCount() > initialFeatureCount)
            {
                stats.wrecksLeft = sim.features.slotCount() - initialFeatureCount;
            }
        }

        /**
         * The sender's current serial, from its own 0x2c or, for a record that
         * carries no clock of its own, the last one it sent. Counts a packet
         * that has neither.
         */
        std::optional<uint32_t> resolveSerial(uint8_t sender, const std::vector<TadBytes>& subPackets)
        {
            for (const auto& subPacket : subPackets)
            {
                if (subPacket.size() >= 7 && static_cast<TadSubPacketCode>(subPacket[0]) == TadSubPacketCode::UnitStatAndMove)
                {
                    if (auto state = tadDecodeUnitState(subPacket, layout))
                    {
                        return state->tick;
                    }
                }
            }

            auto last = lastSerial.find(sender);
            if (last != lastSerial.end())
            {
                return last->second;
            }

            ++stats.packetsWithoutClock;
            return std::nullopt;
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

        auto serial = impl->resolveSerial(packet.sender, subPackets);
        if (!serial)
        {
            if (impl->externalClock)
            {
                impl->pending[impl->lastSeenTick.value_or(0)].push_back(Impl::QueuedPacket{packet.sender, subPackets});
            }
            else
            {
                impl->applySubPackets(packet.sender, subPackets);
                impl->updateWreckCount();
            }
            return;
        }

        if (!impl->baseSerial)
        {
            impl->baseSerial = *serial;
        }
        auto tick = *serial >= *impl->baseSerial ? *serial - *impl->baseSerial : 0u;
        impl->lastSerial[packet.sender] = *serial;
        impl->lastSeenTick = std::max(impl->lastSeenTick.value_or(0u), tick);

        if (impl->externalClock)
        {
            impl->pending[tick].push_back(Impl::QueuedPacket{packet.sender, subPackets});
            return;
        }

        impl->advanceTo(*serial);
        impl->applySubPackets(packet.sender, subPackets);
        impl->updateWreckCount();
    }

    void TadPuppetDriver::setExternalClock(bool external)
    {
        impl->externalClock = external;
    }

    void TadPuppetDriver::applyTick(uint32_t tick)
    {
        for (auto it = impl->pending.begin(); it != impl->pending.end() && it->first <= tick;)
        {
            for (const auto& queued : it->second)
            {
                impl->applySubPackets(queued.sender, queued.subPackets);
            }
            it = impl->pending.erase(it);
        }
        impl->updateWreckCount();
        impl->advanceAirGoals();
    }

    std::optional<uint32_t> TadPuppetDriver::lastTick() const
    {
        return impl->lastSeenTick;
    }

    std::vector<TadChatLine> TadPuppetDriver::takeChat()
    {
        return std::exchange(impl->chatLines, {});
    }

    std::optional<uint16_t> TadPuppetDriver::takeSpeedChange()
    {
        return std::exchange(impl->speedChange, std::nullopt);
    }

    const TadPuppetStats& TadPuppetDriver::stats() const
    {
        return impl->stats;
    }
}
