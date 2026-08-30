#include "UnitBehaviorService.h"
#include <rwe/util/SimpleLogger.h>
#include <rwe/cob/CobExecutionContext.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/cob/cob_util.h>
#include <rwe/sim/cob.h>
#include <rwe/sim/movement.h>
#include <rwe/util/Index.h>
#include <rwe/util/match.h>

namespace rwe
{
    SimScalar getTargetAltitude(const MapTerrain& terrain, SimScalar x, SimScalar z, const UnitDefinition& unitDefinition)
    {
        return rweMax(terrain.getHeightAt(x, z), terrain.getSeaLevel()) + unitDefinition.cruiseAltitude;
    }

    UnitBehaviorService::UnitBehaviorService(GameSimulation* sim)
        : sim(sim)
    {
    }

    void UnitBehaviorService::onCreate(UnitId unitId)
    {
        auto& unit = sim->getUnitState(unitId);
        const auto& unitDefinition = sim->unitDefinitions.at(unit.unitType);

        unit.cobEnvironment->createThread("Create", std::vector<int>());

        // set speed for metal extractors
        if (unitDefinition.extractsMetal != Metal(0))
        {
            auto footprint = sim->computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            auto metalValue = sim->metalGrid.accumulate(sim->metalGrid.clipRegion(footprint), 0u, std::plus<>());
            unit.cobEnvironment->createThread("SetSpeed", {static_cast<int>(metalValue)});
        }

        runUnitCobScripts(*sim, unitId);

        // measure z distances for ballistics
        for (int i = 0; i < getSize(unit.weapons); ++i)
        {
            auto& weapon = unit.weapons[i];
            if (!weapon)
            {
                continue;
            }
            auto localAimingPoint = getLocalAimingPoint(unitId, i);
            auto localFiringPoint = getLocalFiringPoint(unitId, i);
            weapon->ballisticZOffset = localFiringPoint.z - localAimingPoint.z;
        }
    }

    void UnitBehaviorService::updateWind(SimScalar windGenerationFactor, SimAngle windDirection)
    {
        int cobWindSpeed = static_cast<int>(toCobSpeed(windGenerationFactor).value);
        int cobWindDirection = toCobAngle(windDirection).value;

        for (auto& [id, unit] : sim->units)
        {
            const auto& unitDefinition = sim->unitDefinitions.at(unit.unitType);
            if (unitDefinition.windGenerator != Energy(0))
            {
                unit.cobEnvironment->createThread("SetSpeed", {cobWindSpeed});
                unit.cobEnvironment->createThread("SetDirection", {cobWindDirection});
            }
        }
    }

    void UnitBehaviorService::update(UnitId unitId)
    {
        auto unitInfo = sim->getUnitInfo(unitId);

        // A unit in a transport's grip just rides along (see updateCarriedUnits).
        if (unitInfo.state->carriedBy)
        {
            return;
        }

        // Clear steering targets.
        match(
            unitInfo.state->physics,
            [&](UnitPhysicsInfoGround& p) {
                p.steeringInfo = SteeringInfo{
                    unitInfo.state->rotation,
                    0_ss,
                };
            },
            [&](UnitPhysicsInfoAir& p) {
                match(
                    p.movementState,
                    [&](AirMovementStateFlying& s) {
                        s.targetPosition = unitInfo.state->position;
                    },
                    [&](const AirMovementStateTakingOff&) {
                        // do nothing
                    },
                    [&](const AirMovementStateLanding&) {
                        // do nothing
                    },
                    [&](const AirMovementStateAttackRun&) {
                        // Attack run drives its own steering inside handleAttackOrder.
                        // No clearing here, otherwise we'd erase the target each tick.
                    });
            });

        // clear navigation targets
        unitInfo.state->navigationState.desiredDestination = std::nullopt;

        // Run unit and weapon AI
        if (!unitInfo.state->isBeingBuilt(*unitInfo.definition))
        {
            // check our build queue
            if (!unitInfo.state->buildQueue.empty())
            {
                auto& entry = unitInfo.state->buildQueue.front();
                if (handleBuild(unitInfo, entry.first))
                {
                    if (entry.second > 1)
                    {
                        --entry.second;
                    }
                    else
                    {
                        unitInfo.state->buildQueue.pop_front();
                    }
                }
            }
            else
            {
                clearBuild(unitInfo);
            }

            // check our orders
            if (!unitInfo.state->orders.empty())
            {
                const auto& order = unitInfo.state->orders.front();

                // process move orders
                if (handleOrder(unitInfo, order))
                {
                    unitInfo.state->orders.pop_front();
                    unitInfo.state->buildOrderUnitId = std::nullopt;
                }
            }
            else if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics); airPhysics != nullptr)
            {
                match(
                    airPhysics->movementState,
                    [&](AirMovementStateFlying& m) {
                        if (navigateTo(unitInfo, NavigationGoalLandingLocation()))
                        {
                            m.shouldLand = true;
                        }
                    },
                    [&](const AirMovementStateLanding&) {
                        // do nothing
                    },
                    [&](const AirMovementStateTakingOff&) {
                        // do nothing
                    },
                    [&](const AirMovementStateAttackRun&) {
                        // Order list is now empty (e.g. player issued Stop),
                        // but the AttackRun handler is no longer driving us.
                        // Drop back to Flying so the idle/landing path can
                        // run; without this the aircraft freezes mid-air.
                        airPhysics->movementState = AirMovementStateFlying();
                        unitInfo.state->clearWeaponTargets();
                    });
            }
            else
            {
                changeState(*unitInfo.state, UnitBehaviorStateIdle());
            }

            // A spray that is already running follows its nozzle. Without this
            // the emission point is only refreshed on the ticks where work is
            // actually done, so an aircraft circling its job leaves the stream
            // hanging in the air where it started.
            match(
                unitInfo.state->behaviourState,
                [&](UnitBehaviorStateBuilding& s) {
                    if (s.nanoParticleOrigin)
                    {
                        s.nanoParticleOrigin = getNanoPoint(unitInfo.id);
                    }
                },
                [&](UnitBehaviorStateReclaiming& s) {
                    if (s.nanoParticleOrigin)
                    {
                        s.nanoParticleOrigin = getNanoPoint(unitInfo.id);
                    }
                },
                [&](const auto&) {});

            for (Index i = 0; i < getSize(unitInfo.state->weapons); ++i)
            {
                updateWeapon(unitId, i);
            }
        }

        if (unitInfo.definition->isMobile)
        {
            updateNavigation(unitInfo);

            applyUnitSteering(unitInfo);

            auto previouslyWasMoving = !areCloserThan(unitInfo.state->previousPosition, unitInfo.state->position, 0.1_ssf);

            updateUnitPosition(unitInfo);

            auto currentlyIsMoving = !areCloserThan(unitInfo.state->previousPosition, unitInfo.state->position, 0.1_ssf);

            if (currentlyIsMoving && !previouslyWasMoving)
            {
                unitInfo.state->cobEnvironment->createThread("StartMoving");
            }
            else if (!currentlyIsMoving && previouslyWasMoving)
            {
                unitInfo.state->cobEnvironment->createThread("StopMoving");
            }

            // do physics transitions
            match(
                unitInfo.state->physics,
                [&](const UnitPhysicsInfoGround& p) {
                    if (p.steeringInfo.shouldTakeOff)
                    {
                        transitionFromGroundToAir(unitInfo);
                    }
                },
                [&](UnitPhysicsInfoAir& p) {
                    match(
                        p.movementState,
                        [&](const AirMovementStateTakingOff& m) {
                            auto targetHeight = getTargetAltitude(sim->terrain, unitInfo.state->position.x, unitInfo.state->position.z, *unitInfo.definition);
                            if (unitInfo.state->position.y == targetHeight)
                            {
                                // Keep the heading and speed built up during the climb.
                                AirMovementStateFlying flying;
                                flying.targetPosition = m.targetPosition;
                                flying.currentVelocity = m.currentVelocity;
                                p.movementState = flying;
                            }
                        },
                        [&](AirMovementStateLanding& m) {
                            if (m.shouldAbort)
                            {
                                unitInfo.state->activate();
                                p.movementState = AirMovementStateFlying();
                            }
                            else
                            {
                                auto targetHeight = sim->terrain.getHeightAt(unitInfo.state->position.x, unitInfo.state->position.z);
                                if (unitInfo.state->position.y == targetHeight)
                                {
                                    if (!tryTransitionFromAirToGround(unitInfo))
                                    {
                                        // Something took the spot while we were
                                        // coming down. Climb away and look for
                                        // another one: forgetting the landing
                                        // spot makes the next idle tick search
                                        // afresh, and this one is now occupied
                                        // so it will not be picked again.
                                        m.landingFailed = true;
                                        unitInfo.state->activate();
                                        unitInfo.state->navigationState.state = NavigationStateIdle();
                                        p.movementState = AirMovementStateFlying();
                                    }
                                }
                            }
                        },
                        [&](const AirMovementStateFlying& m) {
                            if (m.shouldLand)
                            {
                                // Never touch down on water, whatever the
                                // navigation thinks: better to keep flying and
                                // look for somewhere else than to sink.
                                auto ground = sim->terrain.getHeightAt(unitInfo.state->position.x, unitInfo.state->position.z);
                                if (ground < sim->terrain.getSeaLevel())
                                {
                                    unitInfo.state->navigationState.state = NavigationStateIdle();
                                    return;
                                }
                                p.movementState = AirMovementStateLanding();
                                unitInfo.state->deactivate();
                            }
                        },
                        [&](const AirMovementStateAttackRun&) {
                            // Attack run does not transition out via this dispatcher.
                            // Termination back to Flying is handled in handleAttackOrder.
                        });
                });
        }
    }

    SimVector UnitBehaviorService::getUnitPositionWithCache(UnitState& s, UnitId unitId)
    {
        if (s.navigationState.unitPositionCache && s.navigationState.unitPositionCache->unitId == unitId)
        {
            const auto& pos = s.navigationState.unitPositionCache->position;
            const auto& time = s.navigationState.unitPositionCache->cachedAtTime;
            if (sim->gameTime - time < GameTime(SimTicksPerSecond))
            {
                return pos;
            }
        }

        const auto& pos = sim->getUnitState(unitId).position;
        s.navigationState.unitPositionCache = UnitPositionCache{
            unitId,
            pos,
            sim->gameTime,
        };

        return pos;
    }


    void UnitBehaviorService::updateNavigation(UnitInfo unitInfo)
    {
        const auto& goal = unitInfo.state->navigationState.desiredDestination;

        if (!goal)
        {
            unitInfo.state->navigationState.state = NavigationStateIdle();
            return;
        }

        auto resolvedGoal = match(
            *goal,
            [&](const NavigationGoalLandingLocation&) {
                const auto llState = std::get_if<NavigationStateMovingToLandingSpot>(&unitInfo.state->navigationState.state);
                if (llState)
                {
                    return std::make_optional<MovingStateGoal>(llState->landingLocation);
                }
                else
                {
                    auto landingLocation = findLandingLocation(*sim, unitInfo);
                    if (!landingLocation)
                    {
                        return std::optional<MovingStateGoal>();
                    }
                    unitInfo.state->navigationState.state = NavigationStateMovingToLandingSpot{*landingLocation};
                    return std::make_optional<MovingStateGoal>(*landingLocation);
                }
            },
            [&](const SimVector& v) {
                return std::make_optional<MovingStateGoal>(v);
            },
            [&](const DiscreteRect& r) {
                return std::make_optional<MovingStateGoal>(r);
            },
            [&](const UnitId& u) {
                return std::make_optional<MovingStateGoal>(u);
            },
            [&](const FeatureId& f) {
                // Unlike units, features can't change position so we'll just resolve position here.
                const auto& feature = sim->getFeature(f);
                return std::make_optional<MovingStateGoal>(feature.position);
            });

        if (!resolvedGoal)
        {
            unitInfo.state->navigationState.state = NavigationStateIdle();
            return;
        }

        moveTo(unitInfo, *resolvedGoal);
    }

    bool followPath(UnitInfo unitInfo, UnitPhysicsInfoGround& physics, PathFollowingInfo& path)
    {
        const auto& destination = *path.currentWaypoint;
        SimVector xzPosition(unitInfo.state->position.x, 0_ss, unitInfo.state->position.z);
        SimVector xzDestination(destination.x, 0_ss, destination.z);
        auto distanceSquared = xzPosition.distanceSquared(xzDestination);

        auto isFinalDestination = path.currentWaypoint == (path.path.waypoints.end() - 1);

        if (isFinalDestination)
        {
            if (distanceSquared < (8_ss * 8_ss))
            {
                return true;
            }

            physics.steeringInfo = arrive(*unitInfo.state, *unitInfo.definition, physics, destination);
            return false;
        }

        if (distanceSquared < (16_ss * 16_ss))
        {
            ++path.currentWaypoint;
            return false;
        }

        physics.steeringInfo = seek(*unitInfo.state, *unitInfo.definition, destination);
        return false;
    }

    void UnitBehaviorService::updateWeapon(UnitId id, unsigned int weaponIndex)
    {
        auto& unit = sim->getUnitState(id);
        auto& weapon = unit.weapons[weaponIndex];
        if (!weapon)
        {
            return;
        }

        const auto& weaponDefinition = sim->weaponDefinitions.at(weapon->weaponType);

        if (auto idleState = std::get_if<UnitWeaponStateIdle>(&weapon->state); idleState != nullptr)
        {
            // attempt to acquire a target
            if (!weaponDefinition.commandFire && unit.fireOrders == UnitFireOrders::FireAtWill)
            {
                for (const auto& entry : sim->units)
                {
                    auto otherUnitId = entry.first;
                    const auto& otherUnit = entry.second;

                    if (otherUnit.isDead())
                    {
                        continue;
                    }

                    if (otherUnit.isOwnedBy(unit.owner))
                    {
                        continue;
                    }

                    if (unit.position.distanceSquared(otherUnit.position) > weaponDefinition.maxRange * weaponDefinition.maxRange)
                    {
                        continue;
                    }

                    // Only what the owner can see or has on radar is fair game,
                    // and a torpedo cannot reach something standing on land.
                    if (!sim->canDetectUnit(unit.owner, otherUnitId) || !weaponCanHitUnit(weaponDefinition, otherUnit))
                    {
                        continue;
                    }

                    weapon->state = UnitWeaponStateAttacking(otherUnitId);
                    break;
                }
            }
        }
        else if (auto aimingState = std::get_if<UnitWeaponStateAttacking>(&weapon->state); aimingState != nullptr)
        {
            if (std::holds_alternative<UnitWeaponStateAttacking::FireInfo>(aimingState->attackInfo))
            {
                tryFireWeapon(id, weaponIndex);
                return;
            }

            // If we are not fire-at-will, the target is a unit,
            // and we don't have an explicit order to attack that unit,
            // drop the target.
            // This can happen if we acquired the target ourselves while in fire-at-will,
            // but then the player switched us to another firing mode.
            if (unit.fireOrders != UnitFireOrders::FireAtWill)
            {
                if (auto targetUnit = std::get_if<UnitId>(&aimingState->target); targetUnit != nullptr)
                {
                    if (unit.orders.empty())
                    {
                        unit.clearWeaponTarget(weaponIndex);
                        return;
                    }
                    else if (auto attackOrder = std::get_if<AttackOrder>(&unit.orders.front()); attackOrder != nullptr)
                    {
                        if (auto attackTarget = std::get_if<UnitId>(&attackOrder->target); attackTarget == nullptr || *attackTarget != *targetUnit)
                        {
                            unit.clearWeaponTarget(weaponIndex);
                            return;
                        }
                    }
                }
            }

            auto targetPosition = getTargetPosition(aimingState->target);

            if (!targetPosition || unit.position.distanceSquared(*targetPosition) > weaponDefinition.maxRange * weaponDefinition.maxRange)
            {
                unit.clearWeaponTarget(weaponIndex);
            }
            else if (std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType))
            {
                // Bombsight semantics: we ignore the COB AimWeapon dance and
                // fire directly from the IdleInfo state when the predicted
                // ballistic-impact point is within the release window of the
                // target. This avoids the degenerate aim case where the
                // bomber is overhead (vertical XZ vector ~0) which would
                // confuse computeBallisticHeadingAndPitch.
                if (std::holds_alternative<UnitWeaponStateAttacking::IdleInfo>(aimingState->attackInfo)
                    && sim->gameTime >= weapon->readyTime)
                {
                    auto bomberVelocity = SimVector(0_ss, 0_ss, 0_ss);
                    AirMovementStateAttackRun* runState = nullptr;
                    if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
                    {
                        match(
                            airPhysics->movementState,
                            [&](AirMovementStateAttackRun& m) { bomberVelocity = m.currentVelocity; runState = &m; },
                            [&](const AirMovementStateFlying& m) { bomberVelocity = m.currentVelocity; },
                            [&](const AirMovementStateTakingOff& m) { bomberVelocity = m.currentVelocity; },
                            [&](const AirMovementStateLanding&) {});
                    }

                    // Use the weapon's damageRadius as the release tolerance.
                    // damageRadius is half the TA areaOfEffect (see
                    // LoadingScene_util.cpp), which is roughly the splash
                    // radius — a reasonable bombsight gate.
                    auto releaseRadius = rweMax(weaponDefinition.damageRadius, 16_ss);

                    // Once the sight opens, a run lets go of a stick of three
                    // bombs as fast as the weapon reloads, then holds until
                    // the next pass.
                    const unsigned int bombsPerRun = 3;
                    bool stickStarted = runState && runState->bombsDroppedThisPass > 0;
                    bool stickFinished = runState && runState->bombsDroppedThisPass >= bombsPerRun;
                    if (!stickFinished && (stickStarted || bombsightInReleaseWindow(unit.position, bomberVelocity, *targetPosition, releaseRadius)))
                    {
                        if (runState)
                        {
                            ++runState->bombsDroppedThisPass;
                        }
                        // Heading/pitch are unused for bombs (we'll ignore
                        // them in tryFireWeapon and use the inherited
                        // velocity instead). Fill with zero for determinism.
                        aimingState->attackInfo = UnitWeaponStateAttacking::FireInfo{SimAngle(0), SimAngle(0), *targetPosition, std::nullopt, 0, GameTime(0)};
                        tryFireWeapon(id, weaponIndex);
                    }
                }
            }
            else if (std::holds_alternative<UnitWeaponStateAttacking::IdleInfo>(aimingState->attackInfo))
            {
                auto aimFromPosition = getAimingPoint(id, weaponIndex);

                auto headingAndPitch = computeHeadingAndPitch(unit.rotation, aimFromPosition, *targetPosition, weaponDefinition.velocity, (112_ss / (30_ss * 30_ss)), weapon->ballisticZOffset, weaponDefinition.physicsType);
                auto heading = headingAndPitch.first;
                auto pitch = headingAndPitch.second;

                auto threadId = unit.cobEnvironment->createThread(getAimScriptName(weaponIndex), {toCobAngle(heading).value, toCobAngle(pitch).value});

                if (threadId)
                {
                    aimingState->attackInfo = UnitWeaponStateAttacking::AimInfo{*threadId, heading, pitch};
                }
                else
                {
                    // We couldn't launch an aiming script (there isn't one),
                    // just go straight to firing.
                    if (sim->gameTime >= weapon->readyTime)
                    {
                        aimingState->attackInfo = UnitWeaponStateAttacking::FireInfo{heading, pitch, *targetPosition, std::nullopt, 0, GameTime(0)};
                        tryFireWeapon(id, weaponIndex);
                    }
                }
            }
            else if (auto aimInfo = std::get_if<UnitWeaponStateAttacking::AimInfo>(&aimingState->attackInfo))
            {
                auto returnValue = unit.cobEnvironment->tryReapThread(aimInfo->thread);
                if (returnValue)
                {
                    // we successfully reaped, clear the thread.
                    aimingState->attackInfo = UnitWeaponStateAttacking::IdleInfo{};

                    if (*returnValue)
                    {
                        // aiming was successful, check the target again for drift
                        auto aimFromPosition = getAimingPoint(id, weaponIndex);

                        auto headingAndPitch = computeHeadingAndPitch(unit.rotation, aimFromPosition, *targetPosition, weaponDefinition.velocity, (112_ss / (30_ss * 30_ss)), weapon->ballisticZOffset, weaponDefinition.physicsType);
                        auto heading = headingAndPitch.first;
                        auto pitch = headingAndPitch.second;

                        // if the target is close enough, try to fire
                        if (angleBetweenIsLessOrEqual(heading, aimInfo->lastHeading, weaponDefinition.tolerance) && angleBetweenIsLessOrEqual(pitch, aimInfo->lastPitch, weaponDefinition.pitchTolerance))
                        {

                            if (sim->gameTime >= weapon->readyTime)
                            {
                                aimingState->attackInfo = UnitWeaponStateAttacking::FireInfo{heading, pitch, *targetPosition, std::nullopt, 0, GameTime(0)};
                                tryFireWeapon(id, weaponIndex);
                            }
                        }
                    }
                }
            }
        }
    }

    SimVector UnitBehaviorService::changeDirectionByRandomAngle(const SimVector& direction, SimAngle maxAngle)
    {
        std::uniform_int_distribution dist(SimAngle(0).value, maxAngle.value);
        std::uniform_int_distribution dist2(0, 1);
        auto& rng = sim->rng;
        auto angle = SimAngle(dist(rng));
        if (dist2(rng))
        {
            angle = SimAngle(0) - angle;
        }

        return rotateDirectionXZ(direction, angle);
    }

    void UnitBehaviorService::tryFireWeapon(UnitId id, unsigned int weaponIndex)
    {
        auto& unit = sim->getUnitState(id);
        auto& weapon = unit.weapons[weaponIndex];

        if (!weapon)
        {
            return;
        }

        const auto& weaponDefinition = sim->weaponDefinitions.at(weapon->weaponType);

        auto attackInfo = std::get_if<UnitWeaponStateAttacking>(&weapon->state);
        if (!attackInfo)
        {
            return;
        }

        auto fireInfo = std::get_if<UnitWeaponStateAttacking::FireInfo>(&attackInfo->attackInfo);
        if (!fireInfo)
        {
            return;
        }

        // wait for burst reload
        auto gameTime = sim->gameTime;
        if (gameTime < fireInfo->readyTime)
        {
            return;
        }

        // spawn a projectile from the firing point
        if (!fireInfo->firingPiece)
        {
            auto scriptName = getQueryScriptName(weaponIndex);
            fireInfo->firingPiece = runCobQuery(id, scriptName).value_or(0);
        }

        auto firingPoint = unit.getTransform() * getPieceLocalPosition(id, *fireInfo->firingPiece);

        // Torpedoes run just under the surface from the moment they leave the tube.
        if (weaponDefinition.waterWeapon)
        {
            firingPoint.y = rweMin(firingPoint.y, sim->terrain.getSeaLevel() + SimScalar(0.25f));
        }

        bool isBomb = std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType);

        auto direction = match(
            weaponDefinition.physicsType,
            [&](const ProjectilePhysicsTypeLineOfSight&) {
                // Fallback to the unit's facing when firing piece coincides
                // with target position (e.g. bomber attacking ground directly
                // beneath itself). Without the guard, normalize throws.
                return (fireInfo->targetPosition - firingPoint).normalizedOr(UnitState::toDirection(unit.rotation));
            },
            [&](const ProjectilePhysicsTypeTracking&) {
                return (fireInfo->targetPosition - firingPoint).normalizedOr(UnitState::toDirection(unit.rotation));
            },
            [&](const ProjectilePhysicsTypeBallistic&) {
                return toDirection(fireInfo->heading + unit.rotation, -fireInfo->pitch);
            },
            [&](const ProjectilePhysicsTypeBomb&) {
                // Bombs have no launch direction — they inherit the
                // aircraft's velocity at release. Returning a zero vector
                // means projectile.velocity (= direction * weaponVelocity)
                // contributes nothing; the inheritedVelocity argument
                // below carries the bomber's motion.
                return SimVector(0_ss, 0_ss, 0_ss);
            });


        if (weaponDefinition.sprayAngle != SimAngle(0) && !isBomb)
        {
            // Bombs don't spray — release is deterministic from the bombsight.
            direction = changeDirectionByRandomAngle(direction, weaponDefinition.sprayAngle);
        }

        if (weaponDefinition.waterWeapon)
        {
            // Level running: a torpedo neither dives to the seabed nor leaps out of the water.
            direction.y = 0_ss;
            direction = direction.normalizedOr(UnitState::toDirection(unit.rotation));
        }

        std::optional<SimVector> inheritedVelocity;
        if (isBomb)
        {
            // Bombs inherit the aircraft's per-tick velocity at release.
            // Without this, the bomb would fall straight down from the
            // firing piece while the bomber has already moved past, causing
            // visible misses behind the target.
            SimVector bomberVelocity(0_ss, 0_ss, 0_ss);
            if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
            {
                match(
                    airPhysics->movementState,
                    [&](const AirMovementStateAttackRun& m) { bomberVelocity = m.currentVelocity; },
                    [&](const AirMovementStateFlying& m) { bomberVelocity = m.currentVelocity; },
                    [&](const AirMovementStateTakingOff&) {},
                    [&](const AirMovementStateLanding&) {});
            }
            inheritedVelocity = bomberVelocity;
        }

        auto targetUnit = std::get_if<UnitId>(&attackInfo->target);
        auto targetUnitOption = targetUnit == nullptr ? std::optional<UnitId>() : std::make_optional(*targetUnit);
        sim->spawnProjectile(unit.owner, *weapon, firingPoint, direction, (fireInfo->targetPosition - firingPoint).length(), targetUnitOption, id, inheritedVelocity);

        sim->events.push_back(FireWeaponEvent{weapon->weaponType, fireInfo->burstsFired, firingPoint});

        sim->addResourceDelta(id, -weaponDefinition.energyPerShot, Metal(0));

        // If we just started the burst, set the reload timer
        if (fireInfo->burstsFired == 0)
        {
            unit.cobEnvironment->createThread(getFireScriptName(weaponIndex));
            weapon->readyTime = gameTime + deltaSecondsToTicks(weaponDefinition.reloadTime);
        }

        // Recoil: let the script rock the unit away from the shot. RockUnit takes
        // the push direction in the unit's own frame, scaled the way TA-derived
        // engines do (about 500 per unit of direction).
        if (!isBomb)
        {
            // Scripts assume rotation 0 faces -z (see the XZAtan note in cob.cpp),
            // whereas the sim's rotation 0 faces +z; hence the half turn.
            auto localHeading = UnitState::toRotation(direction) - unit.rotation + HalfTurn;
            auto recoil = UnitState::toDirection(localHeading) * -1_ss;
            unit.cobEnvironment->createThread("RockUnit", {static_cast<int>(recoil.z.value * 500.0f), static_cast<int>(recoil.x.value * 500.0f)});
        }

        ++fireInfo->burstsFired;
        fireInfo->readyTime = gameTime + deltaSecondsToTicks(weaponDefinition.burstInterval);
        if (fireInfo->burstsFired >= weaponDefinition.burst)
        {
            // we finished our burst, we are reloading now
            attackInfo->attackInfo = UnitWeaponStateAttacking::IdleInfo{};
        }
    }

    void UnitBehaviorService::applyUnitSteering(UnitInfo unitInfo)
    {
        updateUnitRotation(unitInfo);
        updateUnitSpeed(unitInfo);
    }

    void UnitBehaviorService::updateUnitRotation(UnitInfo unitInfo)
    {
        auto turnRateThisFrame = SimAngle(unitInfo.definition->turnRate.value);
        unitInfo.state->previousRotation = unitInfo.state->rotation;

        match(
            unitInfo.state->physics,
            [&](const UnitPhysicsInfoGround& p) {
                unitInfo.state->rotation = turnTowards(unitInfo.state->rotation, p.steeringInfo.targetAngle, turnRateThisFrame);
            },
            [&](const UnitPhysicsInfoAir& p) {
                match(
                    p.movementState,
                    [&](const AirMovementStateTakingOff& m) {
                        // Already turning towards where it is going while it climbs.
                        if (!m.targetPosition)
                        {
                            return;
                        }
                        auto direction = *m.targetPosition - unitInfo.state->position;
                        direction.y = 0_ss;
                        if (direction.lengthSquared() == 0_ss)
                        {
                            return;
                        }
                        unitInfo.state->rotation = turnTowards(unitInfo.state->rotation, UnitState::toRotation(direction), turnRateThisFrame);
                    },
                    [&](const AirMovementStateLanding&) {
                        // do nothing
                    },
                    [&](const AirMovementStateFlying& m) {
                        if (!m.targetPosition)
                        {
                            // keep rotation as-is if not trying to go anywhere
                            return;
                        }
                        auto direction = *m.targetPosition - unitInfo.state->position;
                        auto targetAngle = UnitState::toRotation(direction);
                        unitInfo.state->rotation = turnTowards(unitInfo.state->rotation, targetAngle, turnRateThisFrame);
                    },
                    [&](const AirMovementStateAttackRun& m) {
                        // The nose follows the flight path: the run's velocity
                        // is already turn-rate limited, so pointing the model
                        // along it keeps what you see and where the bombs go
                        // in agreement.
                        SimVector heading(m.currentVelocity.x, 0_ss, m.currentVelocity.z);
                        if (heading.lengthSquared() == 0_ss)
                        {
                            heading = m.lastKnownTargetPos - unitInfo.state->position;
                            heading.y = 0_ss;
                        }
                        if (heading.lengthSquared() == 0_ss)
                        {
                            return;
                        }
                        auto targetAngle = UnitState::toRotation(heading);
                        unitInfo.state->rotation = turnTowards(unitInfo.state->rotation, targetAngle, turnRateThisFrame);
                    });
            });
    }

    void UnitBehaviorService::updateUnitSpeed(UnitInfo unitInfo)
    {
        match(
            unitInfo.state->physics,
            [&](UnitPhysicsInfoGround& p) {
                p.currentSpeed = computeNewGroundUnitSpeed(sim->terrain, *unitInfo.state, *unitInfo.definition, p, sim->getAdHocMovementClass(unitInfo.definition->movementCollisionInfo).maxSlope);
            },
            [&](UnitPhysicsInfoAir& p) {
                match(
                    p.movementState,
                    [&](AirMovementStateFlying& m) {
                        m.currentVelocity = computeNewAirUnitVelocity(*unitInfo.state, *unitInfo.definition, m);
                    },
                    [&](AirMovementStateTakingOff& m) {
                        // Gather speed towards the destination on the way up, at
                        // most half pace until it reaches cruise height.
                        AirMovementStateFlying asFlying;
                        asFlying.targetPosition = m.targetPosition;
                        asFlying.currentVelocity = m.currentVelocity;
                        auto velocity = computeNewAirUnitVelocity(*unitInfo.state, *unitInfo.definition, asFlying);
                        velocity.y = 0_ss;
                        auto limit = unitInfo.definition->maxVelocity / 2_ss;
                        if (velocity.lengthSquared() > limit * limit)
                        {
                            velocity = velocity.normalized() * limit;
                        }
                        m.currentVelocity = velocity;
                    },
                    [&](const AirMovementStateLanding&) {
                        // do nothing
                    },
                    [&](AirMovementStateAttackRun& m) {
                        m.currentVelocity = computeNewAttackRunVelocity(*unitInfo.state, *unitInfo.definition, m);
                    });
            });
    }

    void UnitBehaviorService::updateGroundUnitPosition(UnitInfo unitInfo, const UnitPhysicsInfoGround& physics)
    {
        auto direction = UnitState::toDirection(unitInfo.state->rotation);

        if (physics.currentSpeed > 0_ss)
        {
            auto newPosition = unitInfo.state->position + (direction * physics.currentSpeed);
            newPosition.y = sim->terrain.getHeightAt(newPosition.x, newPosition.z);
            if (unitInfo.definition->floater || unitInfo.definition->canHover)
            {
                newPosition.y = rweMax(newPosition.y, sim->terrain.getSeaLevel());
            }

            if (!tryApplyMovementToPosition(unitInfo, newPosition))
            {
                unitInfo.state->inCollision = true;

                // if we failed to move, try in each axis separately
                // to see if we can complete a "partial" movement
                const SimVector maskX(0_ss, 1_ss, 1_ss);
                const SimVector maskZ(1_ss, 1_ss, 0_ss);

                SimVector newPos1;
                SimVector newPos2;
                if (direction.x > direction.z)
                {
                    newPos1 = unitInfo.state->position + (direction * maskZ * physics.currentSpeed);
                    newPos2 = unitInfo.state->position + (direction * maskX * physics.currentSpeed);
                }
                else
                {
                    newPos1 = unitInfo.state->position + (direction * maskX * physics.currentSpeed);
                    newPos2 = unitInfo.state->position + (direction * maskZ * physics.currentSpeed);
                }
                newPos1.y = sim->terrain.getHeightAt(newPos1.x, newPos1.z);
                newPos2.y = sim->terrain.getHeightAt(newPos2.x, newPos2.z);

                if (unitInfo.definition->floater || unitInfo.definition->canHover)
                {
                    newPos1.y = rweMax(newPos1.y, sim->terrain.getSeaLevel());
                    newPos2.y = rweMax(newPos2.y, sim->terrain.getSeaLevel());
                }

                if (!tryApplyMovementToPosition(unitInfo, newPos1))
                {
                    tryApplyMovementToPosition(unitInfo, newPos2);
                }
            }
        }
    }

    void UnitBehaviorService::updateUnitPosition(UnitInfo unitInfo)
    {
        unitInfo.state->previousPosition = unitInfo.state->position;
        unitInfo.state->inCollision = false;

        match(
            unitInfo.state->physics,
            [&](const UnitPhysicsInfoGround& p) {
                updateGroundUnitPosition(unitInfo, p);
            },
            [&](const UnitPhysicsInfoAir& p) {
                match(
                    p.movementState,
                    [&](const AirMovementStateFlying& m) {
                        auto newPosition = unitInfo.state->position + m.currentVelocity;
                        tryApplyMovementToPosition(unitInfo, newPosition);
                    },
                    [&](const AirMovementStateTakingOff& m) {
                        // Move off along the ground track while climbing.
                        auto newPosition = unitInfo.state->position + m.currentVelocity;
                        tryApplyMovementToPosition(unitInfo, newPosition);
                        climbToCruiseAltitude(unitInfo);
                    },
                    [&](const AirMovementStateLanding&) {
                        descendToGroundLevel(unitInfo);
                    },
                    [&](const AirMovementStateAttackRun& m) {
                        // Move along the attack-run velocity, then converge
                        // towards cruise altitude. Cap the per-tick altitude
                        // delta to the unit's maxVelocity so rising terrain
                        // doesn't teleport the aircraft up — TA aircraft
                        // climb/dive smoothly to follow terrain.
                        auto newPosition = unitInfo.state->position + m.currentVelocity;
                        auto targetAltitude = getTargetAltitude(sim->terrain, newPosition.x, newPosition.z, *unitInfo.definition);
                        auto maxAltDelta = unitInfo.definition->maxVelocity;
                        auto altDelta = targetAltitude - newPosition.y;
                        altDelta = rweMax(-maxAltDelta, rweMin(altDelta, maxAltDelta));
                        newPosition.y = newPosition.y + altDelta;
                        tryApplyMovementToPosition(unitInfo, newPosition);
                    });
            });
    }

    bool UnitBehaviorService::tryApplyMovementToPosition(UnitInfo unitInfo, const SimVector& newPosition)
    {
        // No collision for flying units.
        if (isFlying(unitInfo.state->physics))
        {
            unitInfo.state->position = newPosition;
            return true;
        }

        // check for collision at the new position
        auto newFootprintRegion = sim->computeFootprintRegion(newPosition, unitInfo.definition->movementCollisionInfo);

        if (sim->isCollisionAt(newFootprintRegion, unitInfo.id))
        {
            return false;
        }

        // Unlike for pathfinding, TA doesn't care about the unit's actual movement class for collision checks,
        // it only cares about the attributes defined directly on the unitInfo.state->
        // Jam these into an ad-hoc movement class to pass into our walkability check.
        if (!isGridPointWalkable(sim->terrain, sim->getAdHocMovementClass(unitInfo.definition->movementCollisionInfo), newFootprintRegion.x, newFootprintRegion.y))
        {
            return false;
        }

        // we passed all collision checks, update accordingly
        auto footprintRegion = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
        sim->moveUnitOccupiedArea(footprintRegion, newFootprintRegion, unitInfo.id);

        auto seaLevel = sim->terrain.getSeaLevel();
        auto oldTerrainHeight = sim->terrain.getHeightAt(unitInfo.state->position.x, unitInfo.state->position.z);
        auto oldPosBelowSea = oldTerrainHeight < seaLevel;

        unitInfo.state->position = newPosition;

        auto newTerrainHeight = sim->terrain.getHeightAt(unitInfo.state->position.x, unitInfo.state->position.z);
        auto newPosBelowSea = newTerrainHeight < seaLevel;

        if (oldPosBelowSea && !newPosBelowSea)
        {
            unitInfo.state->cobEnvironment->createThread("setSFXoccupy", std::vector<int>{4});
        }
        else if (!oldPosBelowSea && newPosBelowSea)
        {
            unitInfo.state->cobEnvironment->createThread("setSFXoccupy", std::vector<int>{2});
        }

        return true;
    }

    std::optional<int> UnitBehaviorService::runCobQuery(UnitId id, const std::string& name)
    {
        auto& unit = sim->getUnitState(id);
        auto thread = unit.cobEnvironment->createNonScheduledThread(name, {0});
        if (!thread)
        {
            return std::nullopt;
        }
        CobExecutionContext context(unit.cobEnvironment.get(), &*thread);
        auto status = context.execute();
        if (std::get_if<CobEnvironment::FinishedStatus>(&status) == nullptr)
        {
            throw std::runtime_error("Synchronous cob query thread blocked before completion");
        }

        auto result = thread->returnLocals[0];
        return result;
    }

    SimVector UnitBehaviorService::getAimingPoint(UnitId id, unsigned int weaponIndex)
    {
        const auto& unit = sim->getUnitState(id);
        return unit.getTransform() * getLocalAimingPoint(id, weaponIndex);
    }

    SimVector UnitBehaviorService::getLocalAimingPoint(UnitId id, unsigned int weaponIndex)
    {
        auto scriptName = getAimFromScriptName(weaponIndex);
        auto pieceId = runCobQuery(id, scriptName);
        if (!pieceId)
        {
            return getLocalFiringPoint(id, weaponIndex);
        }

        return getPieceLocalPosition(id, *pieceId);
    }

    SimVector UnitBehaviorService::getLocalFiringPoint(UnitId id, unsigned int weaponIndex)
    {

        auto scriptName = getQueryScriptName(weaponIndex);
        auto pieceId = runCobQuery(id, scriptName);
        if (!pieceId)
        {
            return SimVector(0_ss, 0_ss, 0_ss);
        }

        return getPieceLocalPosition(id, *pieceId);
    }

    SimVector UnitBehaviorService::getSweetSpot(UnitId id)
    {
        auto pieceId = runCobQuery(id, "SweetSpot");
        if (!pieceId)
        {
            return sim->getUnitState(id).position;
        }

        return getPiecePosition(id, *pieceId);
    }

    std::optional<SimVector> UnitBehaviorService::tryGetSweetSpot(UnitId id)
    {
        if (!sim->unitExists(id))
        {
            return std::nullopt;
        }

        return getSweetSpot(id);
    }

    bool UnitBehaviorService::handleOrder(UnitInfo unitInfo, const UnitOrder& order)
    {
        // If a non-attack order arrives while the aircraft is mid-AttackRun
        // (e.g. the player issued Move or the previous attack got replaced),
        // drop the AttackRun state so the new order can drive Flying-mode
        // steering. Otherwise the aircraft freezes — the order dispatchers for
        // non-attack orders don't know how to drive an AttackRun-state unit.
        if (!std::holds_alternative<AttackOrder>(order))
        {
            if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics))
            {
                if (std::holds_alternative<AirMovementStateAttackRun>(airPhysics->movementState))
                {
                    airPhysics->movementState = AirMovementStateFlying();
                    unitInfo.state->clearWeaponTargets();
                }
            }
        }

        return match(
            order,
            [&](const MoveOrder& o) {
                return handleMoveOrder(unitInfo, o);
            },
            [&](const AttackOrder& o) {
                return handleAttackOrder(unitInfo, o);
            },
            [&](const BuildOrder& o) {
                return handleBuildOrder(unitInfo, o);
            },
            [&](const BuggerOffOrder& o) {
                return handleBuggerOffOrder(unitInfo, o);
            },
            [&](const CompleteBuildOrder& o) {
                return handleCompleteBuildOrder(unitInfo, o);
            },
            [&](const GuardOrder& o) {
                return handleGuardOrder(unitInfo, o);
            },
            [&](const ReclaimOrder& o) {
                return handleReclaimOrder(unitInfo, o);
            },
            [&](const RepairOrder& o) {
                return handleRepairOrder(unitInfo, o);
            },
            [&](const PatrolOrder& o) {
                return handlePatrolOrder(unitInfo, o);
            },
            [&](const CaptureOrder& o) {
                return handleCaptureOrder(unitInfo, o);
            },
            [&](const LoadOrder& o) {
                return handleLoadOrder(unitInfo, o);
            },
            [&](const UnloadOrder& o) {
                return handleUnloadOrder(unitInfo, o);
            });
    }

    bool UnitBehaviorService::withinBuildReach(UnitInfo unitInfo, const UnitState& target) const
    {
        const auto& targetDefinition = sim->unitDefinitions.at(target.unitType);
        auto rect = sim->computeFootprintRegion(target.position, targetDefinition.movementCollisionInfo);
        auto corner = sim->terrain.heightmapIndexToWorldCorner(rect.x, rect.y);
        auto minX = corner.x;
        auto minZ = corner.z;
        auto maxX = corner.x + (SimScalar(static_cast<float>(rect.width)) * MapTerrain::HeightTileWidthInWorldUnits);
        auto maxZ = corner.z + (SimScalar(static_cast<float>(rect.height)) * MapTerrain::HeightTileHeightInWorldUnits);

        const auto& position = unitInfo.state->position;
        auto nearestX = rweMax(minX, rweMin(maxX, position.x));
        auto nearestZ = rweMax(minZ, rweMin(maxZ, position.z));
        auto dx = position.x - nearestX;
        auto dz = position.z - nearestZ;
        auto reach = unitInfo.definition->buildDistance;
        return (dx * dx) + (dz * dz) <= reach * reach;
    }

    void UnitBehaviorService::interruptCurrentTask(UnitId unitId)
    {
        auto unitRef = sim->tryGetUnitState(unitId);
        if (!unitRef)
        {
            return;
        }
        auto& unit = unitRef->get();
        changeState(unit, UnitBehaviorStateIdle());

        // An aircraft part-way through setting down breaks off and climbs
        // away: whatever it has just been told to do outranks landing.
        if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
        {
            if (auto landing = std::get_if<AirMovementStateLanding>(&airPhysics->movementState))
            {
                landing->shouldAbort = true;
            }
        }
    }

    namespace
    {
        // How far above the ground a hovering air transport holds while it lifts or sets down.
        constexpr float HoverClearance = 6.0f;

        // How far a sea transport's crane reaches to a unit on the shore.
        // TA's data does not say; the Hulk's boom animation covers about this.
        const SimScalar CraneReach = 200_ss;
    }

    bool UnitBehaviorService::hoverTowards(UnitInfo unitInfo, const SimVector& point)
    {
        // Steers an air transport to a point that may be below cruise height;
        // true once it is hovering there.
        //
        // The tolerance is generous on purpose. An aircraft carries its speed
        // into the hover and drifts past the spot before settling, so demanding
        // that it be within a few units of the point had the Atlas circling and
        // overshooting for ten seconds before it would let go of its cargo.
        auto dx = unitInfo.state->position.x - point.x;
        auto dz = unitInfo.state->position.z - point.z;
        auto flatTolerance = rweMax(24_ss, unitInfo.definition->maxVelocity * 4_ss);
        auto heightTolerance = 24_ss;
        auto arrived = ((dx * dx) + (dz * dz)) <= (flatTolerance * flatTolerance)
            && rweAbs(unitInfo.state->position.y - point.y) <= heightTolerance;

        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr)
        {
            return arrived;
        }
        if (auto flying = std::get_if<AirMovementStateFlying>(&airPhysics->movementState))
        {
            flying->targetPosition = point;
        }
        return arrived;
    }

    bool UnitBehaviorService::prepareBuilderForWork(UnitInfo unitInfo, const SimVector& workPosition)
    {
        if (!unitInfo.definition->canFly)
        {
            return true;
        }

        // Sitting on its pad a construction aircraft cannot reach anything:
        // it has to be flying to use its fabricator. Get it up first.
        if (auto groundPhysics = std::get_if<UnitPhysicsInfoGround>(&unitInfo.state->physics))
        {
            groundPhysics->steeringInfo.shouldTakeOff = true;
            return false;
        }

        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr)
        {
            return true;
        }

        if (auto landing = std::get_if<AirMovementStateLanding>(&airPhysics->movementState))
        {
            landing->shouldAbort = true;
            return false;
        }
        if (std::holds_alternative<AirMovementStateTakingOff>(airPhysics->movementState))
        {
            // Still climbing out.
            return false;
        }

        if (auto flying = std::get_if<AirMovementStateFlying>(&airPhysics->movementState))
        {
            // Fly a slow circuit over the job rather than hanging motionless
            // above it, the way TA's construction aircraft do. Steering at a
            // point a little way round the circle keeps it moving: by the time
            // it gets there the point has moved on again. A sixteenth of a
            // turn keeps that point close, so the aircraft is always braking
            // towards it and the circuit stays slow and tight.
            auto radius = rweMax(24_ss, unitInfo.definition->buildDistance * 0.45_ssf);
            SimVector fromCentre(unitInfo.state->position.x - workPosition.x, 0_ss, unitInfo.state->position.z - workPosition.z);
            auto bearing = fromCentre.lengthSquared() > 0_ss
                ? UnitState::toRotation(fromCentre)
                : unitInfo.state->rotation;
            auto lead = bearing + SimAngle(4096);
            auto orbitPoint = workPosition + (UnitState::toDirection(lead) * radius);
            orbitPoint.y = getTargetAltitude(sim->terrain, orbitPoint.x, orbitPoint.z, *unitInfo.definition);
            flying->targetPosition = orbitPoint;
        }

        return true;
    }

    bool UnitBehaviorService::handleLoadOrder(UnitInfo unitInfo, const LoadOrder& loadOrder)
    {
        if (!unitInfo.definition->isTransport())
        {
            return true;
        }

        auto targetRef = sim->tryGetUnitState(loadOrder.target);
        if (targetRef && targetRef->get().carriedBy == unitInfo.id)
        {
            // Aboard (the script's attach-unit got there first). A crane that
            // is still stowing it is not ready for the next one, though.
            if (unitInfo.state->cobEnvironment->isThreadRunning("TransportPickup"))
            {
                return false;
            }
            unitInfo.state->transportScriptTarget = std::nullopt;
            return true;
        }
        if (!targetRef || !targetRef->get().isAlive() || !targetRef->get().isOwnedBy(unitInfo.state->owner) || targetRef->get().carriedBy || loadOrder.target == unitInfo.id)
        {
            unitInfo.state->transportScriptTarget = std::nullopt;
            return true;
        }
        auto& target = targetRef->get();
        const auto& targetDefinition = sim->unitDefinitions.at(target.unitType);

        // Only ground units ride; a transport that is full, or too small for
        // the unit's footprint, gives up.
        auto [footprintX, footprintZ] = sim->getFootprintXZ(targetDefinition.movementCollisionInfo);
        if (!targetDefinition.isMobile || targetDefinition.canFly || targetDefinition.isTransport()
            || unitInfo.state->carriedUnits.size() >= unitInfo.definition->effectiveTransportCapacity()
            || (unitInfo.definition->transportSize > 0 && std::max(footprintX, footprintZ) > unitInfo.definition->transportSize))
        {
            return true;
        }

        bool isShip = unitInfo.definition->floater;
        auto dx = unitInfo.state->position.x - target.position.x;
        auto dz = unitInfo.state->position.z - target.position.z;
        auto flatDistanceSquared = (dx * dx) + (dz * dz);

        // An air transport hovers over the unit; a ship's crane reaches the shore.
        auto pickupRange = isShip ? CraneReach : 24_ss + SimScalar(static_cast<float>(std::max(footprintX, footprintZ))) * MapTerrain::HeightTileWidthInWorldUnits;
        if (flatDistanceSquared > pickupRange * pickupRange)
        {
            navigateTo(unitInfo, loadOrder.target);

            // A ship cannot come ashore, so the unit walks down to the water's
            // edge to meet it. Its own path stops at the last point it can
            // actually reach, which is the shore.
            //
            // The meeting point is measured out from the ship towards the
            // unit, not the other way about: working from the unit's end sent
            // it marching away from the transport, and then further away again
            // each time it arrived.
            if (isShip && target.orders.empty() && !target.carriedBy)
            {
                SimVector towardsTarget(-dx, 0_ss, -dz);
                auto direction = towardsTarget.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));
                auto meetingPoint = unitInfo.state->position + (direction * (pickupRange * 0.75_ssf));
                meetingPoint.y = sim->terrain.getHeightAt(meetingPoint.x, meetingPoint.z);
                target.addOrder(createMoveOrder(meetingPoint));
            }
            return false;
        }

        auto targetHeight = simScalarToFloat(sim->unitModelDefinitions.at(targetDefinition.objectName).height);

        if (isShip)
        {
            // The ship's own script does the loading: TransportPickup(unit)
            // opens the doors, swings the crane out, fastens the unit with
            // attach-unit (which takes it aboard) and stows it on deck.
            if (unitInfo.state->transportScriptTarget != loadOrder.target)
            {
                // The crane handles one unit at a time: wait for the last
                // pickup to finish stowing before reaching for the next.
                if (unitInfo.state->cobEnvironment->isThreadRunning("TransportPickup"))
                {
                    return false;
                }
                unitInfo.state->transportScriptTarget = loadOrder.target;
                unitInfo.state->transportScriptStartedAt = sim->gameTime;
                auto thread = unitInfo.state->cobEnvironment->createThread("TransportPickup", {static_cast<int>(loadOrder.target.value)});
                LOG_DEBUG << "Transport " << unitInfo.id.value << " TransportPickup(" << loadOrder.target.value << ") " << (thread ? "started" : "not in script");
                return false;
            }
            if (target.carriedBy == unitInfo.id)
            {
                // Hooked on, but the boom is still swinging it into the hold
                // and the doors have yet to close. Not finished until the
                // script is.
                if (unitInfo.state->cobEnvironment->isThreadRunning("TransportPickup"))
                {
                    return false;
                }
                unitInfo.state->transportScriptTarget = std::nullopt;
                return true;
            }
            // A script that never attaches: take the unit aboard ourselves after a while.
            if (sim->gameTime >= unitInfo.state->transportScriptStartedAt + GameTime(10u * static_cast<unsigned int>(SimTicksPerSecond)))
            {
                sim->loadUnitIntoTransport(unitInfo.id, loadOrder.target, std::string());
                unitInfo.state->transportScriptTarget = std::nullopt;
                return true;
            }
            return false;
        }

        // An air transport drops down until it hovers just above the unit before lifting it.
        if (unitInfo.definition->canFly)
        {
            SimVector hoverPoint(target.position.x, target.position.y + SimScalar(targetHeight + HoverClearance), target.position.z);
            if (!hoverTowards(unitInfo, hoverPoint))
            {
                return false;
            }
        }

        // The script says which piece the unit hangs from and does its own animation.
        std::string piece;
        if (auto pieceId = runCobQuery(unitInfo.id, "QueryTransport"))
        {
            const auto& pieces = unitInfo.state->cobEnvironment->_script->pieces;
            if (*pieceId >= 0 && static_cast<std::size_t>(*pieceId) < pieces.size())
            {
                piece = pieces[static_cast<std::size_t>(*pieceId)];
            }
        }

        if (sim->loadUnitIntoTransport(unitInfo.id, loadOrder.target, piece))
        {
            // Air transports (Atlas) animate their grip with BeginTransport(height).
            unitInfo.state->cobEnvironment->createThread("BeginTransport", {static_cast<int>(targetHeight)});
        }
        return true;
    }

    bool UnitBehaviorService::handleUnloadOrder(UnitInfo unitInfo, const UnloadOrder& unloadOrder)
    {
        LOG_DEBUG << "Transport " << unitInfo.id.value << " unload order: carrying " << unitInfo.state->carriedUnits.size() << ", floater " << unitInfo.definition->floater;
        if (unitInfo.state->carriedUnits.empty())
        {
            unitInfo.state->transportScriptTarget = std::nullopt;
            return true;
        }

        auto dx = unitInfo.state->position.x - unloadOrder.destination.x;
        auto dz = unitInfo.state->position.z - unloadOrder.destination.z;
        // An aircraft needs room to stop, so it starts its descent from
        // further out than a ship's crane needs.
        auto dropRange = unitInfo.definition->floater
            ? CraneReach
            : rweMax(32_ss, unitInfo.definition->maxVelocity * 8_ss);
        if ((dx * dx) + (dz * dz) > dropRange * dropRange)
        {
            navigateTo(unitInfo, unloadOrder.destination);
            return false;
        }

        if (unitInfo.definition->floater)
        {
            // TransportDrop(unit, xz) swings one unit ashore and lets go with
            // drop-unit, which sets it down where it hangs. One order sets
            // down one unit, and the crane is not free until it has stowed.
            const auto& carried = unitInfo.state->carriedUnits;
            auto current = unitInfo.state->transportScriptTarget;
            bool stillAboard = current && std::find(carried.begin(), carried.end(), *current) != carried.end();

            if (current && !stillAboard)
            {
                // It is ashore. Wait for the boom to come back in, then the order is done.
                if (unitInfo.state->cobEnvironment->isThreadRunning("TransportDrop"))
                {
                    return false;
                }
                unitInfo.state->transportScriptTarget = std::nullopt;
                unitInfo.state->cobEnvironment->createThread("EndTransport");
                return true;
            }

            if (!current)
            {
                if (unitInfo.state->cobEnvironment->isThreadRunning("TransportDrop"))
                {
                    return false;
                }
                auto next = carried.front();
                unitInfo.state->transportScriptTarget = next;
                unitInfo.state->transportScriptStartedAt = sim->gameTime;
                // TransportDrop(unit, xz) takes the drop point as a packed x/z pair.
                const auto& d = unloadOrder.destination;
                auto packed = static_cast<int>(cobPackCoords(CobPosition::fromFloat(simScalarToFloat(d.x)), CobPosition::fromFloat(simScalarToFloat(d.z))));
                auto thread = unitInfo.state->cobEnvironment->createThread("TransportDrop", {static_cast<int>(next.value), packed});
                LOG_DEBUG << "Transport " << unitInfo.id.value << " TransportDrop(" << next.value << ") " << (thread ? "started" : "not in script");
                return false;
            }

            if (sim->gameTime >= unitInfo.state->transportScriptStartedAt + GameTime(10u * static_cast<unsigned int>(SimTicksPerSecond)))
            {
                // The script never let go: set it down ourselves.
                sim->unloadUnitFromTransport(unitInfo.id, *current, unloadOrder.destination);
                unitInfo.state->transportScriptTarget = std::nullopt;
                return true;
            }
            return false;
        }

        // An air transport settles low over the spot before letting go.
        if (unitInfo.definition->canFly)
        {
            float tallest = 0.0f;
            for (auto carriedId : unitInfo.state->carriedUnits)
            {
                if (auto carried = sim->tryGetUnitState(carriedId))
                {
                    tallest = std::max(tallest, simScalarToFloat(sim->unitModelDefinitions.at(sim->unitDefinitions.at(carried->get().unitType).objectName).height));
                }
            }
            auto ground = sim->terrain.getHeightAt(unloadOrder.destination.x, unloadOrder.destination.z);
            SimVector hoverPoint(unloadOrder.destination.x, ground + SimScalar(tallest + HoverClearance), unloadOrder.destination.z);
            if (!hoverTowards(unitInfo, hoverPoint))
            {
                return false;
            }
        }

        // One unload order sets down one unit; the rest stay aboard until
        // they are ordered out in turn.
        auto carriedId = unitInfo.state->carriedUnits.front();
        if (sim->unloadUnitFromTransport(unitInfo.id, carriedId, unloadOrder.destination))
        {
            unitInfo.state->cobEnvironment->createThread("EndTransport");
        }
        // If it found no room it stays aboard; the order is done either way.
        return true;
    }

    bool UnitBehaviorService::handleMoveOrder(UnitInfo unitInfo, const MoveOrder& moveOrder)
    {
        if (!unitInfo.definition->isMobile)
        {
            return false;
        }

        if (navigateTo(unitInfo, moveOrder.destination))
        {
            // Only report in on arriving at the last waypoint. A unit given a
            // string of shift-queued moves is still on its way until the queue
            // runs out, and announcing every leg of the journey is noise.
            // (This order is still at the front; the caller pops it.)
            if (unitInfo.state->orders.size() == 1)
            {
                sim->events.push_back(UnitArrivedEvent{unitInfo.id});
            }
            return true;
        }

        return false;
    }

    bool UnitBehaviorService::handleAttackOrder(UnitInfo unitInfo, const AttackOrder& attackOrder)
    {
        return attackTarget(unitInfo, attackOrder.target);
    }

    NavigationGoal attackTargetToNavigationGoal(const AttackTarget& target)
    {
        return match(
            target,
            [&](const UnitId& t) -> NavigationGoal {
                return t;
            },
            [&](const SimVector& t) -> NavigationGoal {
                return t;
            });
    }

    bool UnitBehaviorService::weaponCanHitUnit(const WeaponDefinition& weaponDefinition, const UnitState& target) const
    {
        if (!weaponDefinition.waterWeapon)
        {
            return true;
        }
        return target.position.y <= sim->terrain.getSeaLevel();
    }

    bool UnitBehaviorService::attackTarget(UnitInfo unitInfo, const AttackTarget& target)
    {
        if (!unitInfo.state->weapons[0])
        {
            return true;
        }

        // A water weapon ordered at a unit on dry land has nothing to do.
        if (auto targetUnitId = std::get_if<UnitId>(&target))
        {
            auto targetUnit = sim->tryGetUnitState(*targetUnitId);
            const auto& weaponDefinition = sim->weaponDefinitions.at(unitInfo.state->weapons[0]->weaponType);
            if (targetUnit && !weaponCanHitUnit(weaponDefinition, targetUnit->get()))
            {
                return true;
            }
        }

        // Aircraft execute attack runs rather than the ground-unit
        // approach/aim/fire pattern.
        if (unitInfo.definition->canFly)
        {
            return attackTargetAir(unitInfo, target);
        }

        const auto& weaponDefinition = sim->weaponDefinitions.at(unitInfo.state->weapons[0]->weaponType);

        auto targetPosition = getTargetPosition(target);
        if (!targetPosition)
        {
            // target has gone away, throw away this order
            return true;
        }

        auto maxRangeSquared = weaponDefinition.maxRange * weaponDefinition.maxRange;
        if (unitInfo.state->position.distanceSquared(*targetPosition) > maxRangeSquared)
        {
            navigateTo(unitInfo, attackTargetToNavigationGoal(target));
        }
        else
        {
            // we're in range, aim weapons
            for (unsigned int i = 0; i < 2; ++i)
            {
                match(
                    target,
                    [&](const UnitId& u) { unitInfo.state->setWeaponTarget(i, u); },
                    [&](const SimVector& v) { unitInfo.state->setWeaponTarget(i, v); });
            }
        }

        return false;
    }

    bool UnitBehaviorService::attackTargetAir(UnitInfo unitInfo, const AttackTarget& target)
    {
        // Resolve the target position. If the target has gone away, drop the order.
        auto targetPosition = getTargetPosition(target);
        if (!targetPosition)
        {
            unitInfo.state->clearWeaponTargets();
            // Reset to plain Flying so the aircraft will drift back to base on idle.
            if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics))
            {
                if (std::holds_alternative<AirMovementStateAttackRun>(airPhysics->movementState))
                {
                    airPhysics->movementState = AirMovementStateFlying();
                }
            }
            return true;
        }

        // If we're not airborne yet, request takeoff and wait.
        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr)
        {
            auto groundPhysics = std::get_if<UnitPhysicsInfoGround>(&unitInfo.state->physics);
            if (groundPhysics != nullptr)
            {
                groundPhysics->steeringInfo.shouldTakeOff = true;
            }
            return false;
        }

        // If we're taking off or landing, wait until the transition completes
        // (the air-state machine will end up in Flying). Don't fire yet.
        if (std::holds_alternative<AirMovementStateTakingOff>(airPhysics->movementState))
        {
            // Start moving towards the target while still climbing out.
            navigateTo(unitInfo, *targetPosition);
            unitInfo.state->clearWeaponTargets();
            return false;
        }
        if (auto landing = std::get_if<AirMovementStateLanding>(&airPhysics->movementState))
        {
            // Break off the landing and climb away: a new target outranks
            // setting down. The state machine puts us back into Flying.
            landing->shouldAbort = true;
            unitInfo.state->clearWeaponTargets();
            return false;
        }

        const auto& weaponDefinition = sim->weaponDefinitions.at(unitInfo.state->weapons[0]->weaponType);

        // If we're already in an AttackRun but the player retargeted, drop
        // the run so the next branch re-initialises Approaching cleanly
        // against the new target. Without this the aircraft pings between
        // Engaging and Departing because runOutDirection is stale relative
        // to the new target — observable as ~2-second oscillation in place.
        if (auto attackRun = std::get_if<AirMovementStateAttackRun>(&airPhysics->movementState))
        {
            if (attackRun->target != target)
            {
                AirMovementStateFlying flying;
                flying.currentVelocity = attackRun->currentVelocity;
                airPhysics->movementState = flying;
            }
        }

        // If we're currently in plain Flying, kick off an attack run.
        if (auto flying = std::get_if<AirMovementStateFlying>(&airPhysics->movementState))
        {
            AirMovementStateAttackRun runState(target);
            // Cache target position at cruise altitude so steering doesn't dive.
            auto altitude = getTargetAltitude(sim->terrain, targetPosition->x, targetPosition->z, *unitInfo.definition);
            runState.lastKnownTargetPos = SimVector(targetPosition->x, altitude, targetPosition->z);
            runState.runOutDirection = UnitState::toDirection(unitInfo.state->rotation);
            runState.runOutDistance = defaultAttackRunOutDistance(*unitInfo.definition, weaponDefinition.maxRange);
            runState.phase = AirMovementStateAttackRun::Phase::Approaching;
            // Carry the speed it already had: an aircraft that turns to attack
            // does not come to a halt first.
            runState.currentVelocity = flying->currentVelocity;
            airPhysics->movementState = runState;
        }

        auto attackRun = std::get_if<AirMovementStateAttackRun>(&airPhysics->movementState);
        if (attackRun == nullptr)
        {
            // Defensive: nothing to do.
            return false;
        }

        // Refresh cached target position (target may move).
        auto altitude = getTargetAltitude(sim->terrain, targetPosition->x, targetPosition->z, *unitInfo.definition);
        attackRun->lastKnownTargetPos = SimVector(targetPosition->x, altitude, targetPosition->z);
        attackRun->target = target;

        auto previousPhase = attackRun->phase;

        // If we just transitioned into Engaging, capture the run-out direction
        // before stepping. Approaching's phase change to Engaging happens in
        // stepAttackRunPhase.
        auto geometry = computeAttackRunGeometry(*unitInfo.definition, weaponDefinition.maxRange);
        SimVector heading = attackRun->currentVelocity;
        heading.y = 0_ss;
        if (heading.lengthSquared() == 0_ss)
        {
            heading = UnitState::toDirection(unitInfo.state->rotation);
        }
        bool weaponsHot = stepAttackRunPhase(unitInfo.state->position, heading, *targetPosition, geometry, *attackRun);

        // Never run out past the edge of the map: turn back early instead.
        if (attackRun->phase == AirMovementStateAttackRun::Phase::Departing)
        {
            const auto& heights = sim->terrain.getHeightMap();
            auto corner = sim->terrain.heightmapIndexToWorldCorner(0, 0);
            auto margin = 64_ss;
            auto minX = corner.x + margin;
            auto minZ = corner.z + margin;
            auto maxX = corner.x + (SimScalar(static_cast<float>(heights.getWidth())) * MapTerrain::HeightTileWidthInWorldUnits) - margin;
            auto maxZ = corner.z + (SimScalar(static_cast<float>(heights.getHeight())) * MapTerrain::HeightTileHeightInWorldUnits) - margin;
            const auto& p = unitInfo.state->position;
            if (p.x < minX || p.x > maxX || p.z < minZ || p.z > maxZ)
            {
                attackRun->phase = AirMovementStateAttackRun::Phase::Approaching;
            }
        }

        if (previousPhase == AirMovementStateAttackRun::Phase::Approaching
            && attackRun->phase == AirMovementStateAttackRun::Phase::Engaging)
        {
            // Capture the engagement heading: prefer the direction we're
            // already moving in so the line-up stays smooth. If we're
            // not moving yet, fall back to the line through the target.
            SimVector heading = attackRun->currentVelocity;
            heading.y = 0_ss;
            if (heading.lengthSquared() == 0_ss)
            {
                heading = SimVector(targetPosition->x - unitInfo.state->position.x, 0_ss, targetPosition->z - unitInfo.state->position.z);
            }
            attackRun->runOutDirection = heading.normalizedOr(UnitState::toDirection(unitInfo.state->rotation));
        }

        if (weaponsHot)
        {
            // Weapons hot: have all weapons fire on the target.
            for (unsigned int i = 0; i < 2; ++i)
            {
                match(
                    target,
                    [&](const UnitId& u) { unitInfo.state->setWeaponTarget(i, u); },
                    [&](const SimVector& v) { unitInfo.state->setWeaponTarget(i, v); });
            }
        }
        else
        {
            unitInfo.state->clearWeaponTargets();
        }

        return false;
    }

    bool UnitBehaviorService::handleBuildOrder(UnitInfo unitInfo, const BuildOrder& buildOrder)
    {
        return buildUnit(unitInfo, buildOrder.unitType, buildOrder.position);
    }

    bool UnitBehaviorService::handleBuggerOffOrder(UnitInfo unitInfo, const BuggerOffOrder& buggerOffOrder)
    {
        auto [footprintX, footprintZ] = sim->getFootprintXZ(unitInfo.definition->movementCollisionInfo);
        return navigateTo(unitInfo, buggerOffOrder.rect.expand((footprintX * 3) - 4, (footprintZ * 3) - 4));
    }

    bool UnitBehaviorService::handleCompleteBuildOrder(UnitInfo unitInfo, const rwe::CompleteBuildOrder& buildOrder)
    {
        return buildExistingUnit(unitInfo, buildOrder.target);
    }

    bool UnitBehaviorService::handleGuardOrder(UnitInfo unitInfo, const GuardOrder& guardOrder)
    {
        auto target = sim->tryGetUnitState(guardOrder.target);
        // TODO: real allied check here
        if (!target || !target->get().isOwnedBy(unitInfo.state->owner))
        {
            // unit is dead or a traitor, abandon order
            return true;
        }
        auto& targetUnit = target->get();


        // assist building
        if (auto bs = std::get_if<UnitBehaviorStateBuilding>(&targetUnit.behaviourState); unitInfo.definition->builder && bs)
        {
            buildExistingUnit(unitInfo, bs->targetUnit);
            return false;
        }

        // assist factory building
        if (auto fs = std::get_if<FactoryBehaviorStateBuilding>(&targetUnit.factoryState); unitInfo.definition->builder && fs)
        {
            if (fs->targetUnit)
            {
                buildExistingUnit(unitInfo, fs->targetUnit->first);
                return false;
            }
        }

        // stay close
        if (unitInfo.definition->canMove && unitInfo.state->position.distanceSquared(targetUnit.position) > SimScalar(200 * 200))
        {
            navigateTo(unitInfo, guardOrder.target);
            return false;
        }

        return false;
    }

    bool UnitBehaviorService::handleReclaimOrder(UnitInfo unitInfo, const ReclaimOrder& reclaimOrder)
    {
        if (auto targetUnit = std::get_if<UnitId>(&reclaimOrder.target); targetUnit != nullptr && *targetUnit == unitInfo.id)
        {
            // A unit cannot reclaim itself.
            return true;
        }

        return reclaimTarget(unitInfo, reclaimOrder.target);
    }

    bool UnitBehaviorService::handleRepairOrder(UnitInfo unitInfo, const RepairOrder& repairOrder)
    {
        if (repairOrder.target == unitInfo.id)
        {
            // A unit cannot repair itself.
            return true;
        }

        auto targetRef = sim->tryGetUnitState(repairOrder.target);
        if (!targetRef || targetRef->get().isDead())
        {
            return true;
        }
        const auto& target = targetRef->get();
        const auto& targetDefinition = sim->unitDefinitions.at(target.unitType);

        if (target.isBeingBuilt(targetDefinition))
        {
            // Repairing an unfinished unit means finishing its construction.
            return buildExistingUnit(unitInfo, repairOrder.target);
        }

        if (target.hitPoints >= targetDefinition.maxHitPoints)
        {
            // Nothing to repair.
            return true;
        }

        return repairExistingUnit(unitInfo, repairOrder.target);
    }

    std::optional<UnitId> UnitBehaviorService::findEnemyInWeaponRange(UnitInfo unitInfo) const
    {
        const auto& weapon = unitInfo.state->weapons[0];
        if (!weapon)
        {
            return std::nullopt;
        }
        const auto& weaponDefinition = sim->weaponDefinitions.at(weapon->weaponType);
        auto maxRangeSquared = weaponDefinition.maxRange * weaponDefinition.maxRange;

        std::optional<UnitId> best;
        std::optional<SimScalar> bestDistanceSquared;
        for (const auto& [otherId, other] : sim->units)
        {
            if (otherId == unitInfo.id || other.isDead() || other.isOwnedBy(unitInfo.state->owner))
            {
                continue;
            }

            auto distanceSquared = unitInfo.state->position.distanceSquared(other.position);
            if (distanceSquared > maxRangeSquared)
            {
                continue;
            }
            if (!bestDistanceSquared || distanceSquared < *bestDistanceSquared)
            {
                best = otherId;
                bestDistanceSquared = distanceSquared;
            }
        }
        return best;
    }

    bool UnitBehaviorService::handlePatrolOrder(UnitInfo unitInfo, const PatrolOrder& patrolOrder)
    {
        if (!unitInfo.definition->isMobile)
        {
            // Static units cannot patrol; drop the order.
            return true;
        }

        // Engage anything hostile in weapon range before carrying on.
        if (unitInfo.state->fireOrders != UnitFireOrders::HoldFire)
        {
            if (auto enemy = findEnemyInWeaponRange(unitInfo))
            {
                attackTarget(unitInfo, AttackTarget(*enemy));
                return false;
            }
        }

        if (navigateTo(unitInfo, patrolOrder.destination))
        {
            // Reached this waypoint: send it to the back so the route loops.
            // Copy first: the caller pops the front after we return true.
            auto destination = patrolOrder.destination;
            unitInfo.state->orders.push_back(PatrolOrder(destination));
            // No arrival report: a patrol never finishes, and TA does not
            // have the unit announce itself on every lap of its circuit.
            return true;
        }

        return false;
    }

    bool UnitBehaviorService::handleCaptureOrder(UnitInfo unitInfo, const CaptureOrder& captureOrder)
    {
        if (!unitInfo.definition->canCapture)
        {
            return true;
        }

        auto targetRef = sim->tryGetUnitState(captureOrder.target);
        if (!targetRef || targetRef->get().isDead() || targetRef->get().isOwnedBy(unitInfo.state->owner))
        {
            return true;
        }

        return captureExistingUnit(unitInfo, captureOrder.target);
    }

    bool UnitBehaviorService::captureExistingUnit(UnitInfo unitInfo, UnitId targetUnitId)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);
        if (!targetUnitRef || targetUnitRef->get().isDead() || targetUnitRef->get().isOwnedBy(unitInfo.state->owner))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();

        // Same reach as building; see the FIXME in buildExistingUnit.
        if (unitInfo.state->position.distanceSquared(targetUnit.position) > (unitInfo.definition->buildDistance * unitInfo.definition->buildDistance))
        {
            navigateTo(unitInfo, targetUnitId);
            return false;
        }

        return deployCaptureArm(unitInfo, targetUnitId);
    }

    bool UnitBehaviorService::deployCaptureArm(UnitInfo unitInfo, UnitId targetUnitId)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);
        if (!targetUnitRef || targetUnitRef->get().isDead() || targetUnitRef->get().isOwnedBy(unitInfo.state->owner))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();

        // Capturing reuses the building state for the nanolathe effect and
        // the StartBuilding/StopBuilding script hooks.
        return match(
            unitInfo.state->behaviourState,
            [&](UnitBehaviorStateBuilding& buildingState) {
                if (targetUnitId != buildingState.targetUnit)
                {
                    changeState(*unitInfo.state, UnitBehaviorStateIdle());
                    return captureExistingUnit(unitInfo, targetUnitId);
                }

                if (!unitInfo.state->inBuildStance)
                {
                    return false;
                }

                buildingState.nanoParticleOrigin = getNanoPoint(unitInfo.id);

                auto finished = sim->captureUnit(targetUnitId, unitInfo.state->owner, unitInfo.definition->workerTimePerTick);
                if (finished)
                {
                    changeState(*unitInfo.state, UnitBehaviorStateIdle());
                }
                return finished;
            },
            [&](const auto&) {
                // An arm still being stowed by a StopBuilding thread sets
                // INBUILDSTANCE back to zero, and if it gets there after
                // StartBuilding has set it the builder ends up with its arm
                // raised and nothing else happening. Let the stow finish.
                if (unitInfo.state->cobEnvironment->isThreadRunning("StopBuilding"))
                {
                    return false;
                }

                auto nanoFromPosition = getNanoPoint(unitInfo.id);
                auto headingAndPitch = computeLineOfSightHeadingAndPitch(unitInfo.state->rotation, nanoFromPosition, targetUnit.position);
                auto heading = headingAndPitch.first;
                auto pitch = headingAndPitch.second;

                changeState(*unitInfo.state, UnitBehaviorStateBuilding{targetUnitId, std::nullopt});
                unitInfo.state->cobEnvironment->createThread("StartBuilding", {toCobAngle(heading).value, toCobAngle(pitch).value});
                return false;
            });
    }

    bool UnitBehaviorService::repairExistingUnit(UnitInfo unitInfo, UnitId targetUnitId)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);
        if (!targetUnitRef || targetUnitRef->get().isDead())
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();

        // Same reach as building; see the FIXME in buildExistingUnit.
        if (unitInfo.state->position.distanceSquared(targetUnit.position) > (unitInfo.definition->buildDistance * unitInfo.definition->buildDistance))
        {
            navigateTo(unitInfo, targetUnitId);
            return false;
        }

        return deployRepairArm(unitInfo, targetUnitId);
    }

    bool UnitBehaviorService::handleBuild(UnitInfo unitInfo, const std::string& unitType)
    {
        return match(
            unitInfo.state->factoryState,
            [&](const FactoryBehaviorStateIdle&) {
                sim->activateUnit(unitInfo.id);
                unitInfo.state->factoryState = FactoryBehaviorStateBuilding();
                return false;
            },
            [&](FactoryBehaviorStateCreatingUnit& state) {
                return match(
                    state.status,
                    [&](const UnitCreationStatusPending&) {
                        return false;
                    },
                    [&](const UnitCreationStatusDone& s) {
                        unitInfo.state->cobEnvironment->createThread("StartBuilding");
                        unitInfo.state->factoryState = FactoryBehaviorStateBuilding{std::make_pair(s.unitId, std::optional<SimVector>())};
                        return false;
                    },
                    [&](const UnitCreationStatusFailed&) {
                        unitInfo.state->factoryState = FactoryBehaviorStateBuilding();
                        return false;
                    });
            },
            [&](FactoryBehaviorStateBuilding& state) {
                if (!unitInfo.state->inBuildStance)
                {
                    return false;
                }

                auto buildPieceInfo = getBuildPieceInfo(unitInfo.id);
                // buildPieceInfo.position.y = sim->terrain.getHeightAt(buildPieceInfo.position.x, buildPieceInfo.position.z);
                if (!state.targetUnit)
                {
                    unitInfo.state->factoryState = FactoryBehaviorStateCreatingUnit{unitType, unitInfo.state->owner, buildPieceInfo.position, buildPieceInfo.rotation};
                    sim->unitCreationRequests.push_back(unitInfo.id);
                    return false;
                }

                auto targetUnitOption = sim->tryGetUnitState(state.targetUnit->first);
                if (!targetUnitOption)
                {
                    unitInfo.state->factoryState = FactoryBehaviorStateCreatingUnit{unitType, unitInfo.state->owner, buildPieceInfo.position, buildPieceInfo.rotation};
                    sim->unitCreationRequests.push_back(unitInfo.id);
                    return false;
                }

                auto& targetUnit = targetUnitOption->get();
                const auto& targetUnitDefinition = sim->unitDefinitions.at(targetUnit.unitType);

                if (targetUnit.unitType != unitType)
                {
                    if (targetUnit.isBeingBuilt(targetUnitDefinition) && !targetUnit.isDead())
                    {
                        sim->quietlyKillUnit(state.targetUnit->first);
                    }
                    state.targetUnit = std::nullopt;
                    return false;
                }

                if (targetUnit.isDead())
                {
                    unitInfo.state->cobEnvironment->createThread("StopBuilding");
                    sim->deactivateUnit(unitInfo.id);
                    unitInfo.state->factoryState = FactoryBehaviorStateIdle();
                    return true;
                }

                if (!targetUnit.isBeingBuilt(targetUnitDefinition))
                {
                    if (unitInfo.state->orders.empty())
                    {
                        auto footprintRect = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
                        targetUnit.addOrder(BuggerOffOrder(footprintRect));
                    }
                    else
                    {
                        targetUnit.replaceOrders(unitInfo.state->orders);
                    }
                    unitInfo.state->cobEnvironment->createThread("StopBuilding");
                    sim->deactivateUnit(unitInfo.id);
                    unitInfo.state->factoryState = FactoryBehaviorStateIdle();
                    return true;
                }

                if (targetUnitDefinition.floater || targetUnitDefinition.canHover)
                {
                    buildPieceInfo.position.y = rweMax(buildPieceInfo.position.y, sim->terrain.getSeaLevel());
                }

                tryApplyMovementToPosition(sim->getUnitInfo(state.targetUnit->first), buildPieceInfo.position);
                targetUnit.rotation = buildPieceInfo.rotation;

                auto costs = targetUnit.getBuildCostInfo(targetUnitDefinition, unitInfo.definition->workerTimePerTick);
                auto gotResources = sim->addResourceDelta(
                    unitInfo.id,
                    -Energy(targetUnitDefinition.buildCostEnergy.value * static_cast<float>(unitInfo.definition->workerTimePerTick) / static_cast<float>(targetUnitDefinition.buildTime)),
                    -Metal(targetUnitDefinition.buildCostMetal.value * static_cast<float>(unitInfo.definition->workerTimePerTick) / static_cast<float>(targetUnitDefinition.buildTime)),
                    -costs.energyCost,
                    -costs.metalCost);

                if (!gotResources)
                {
                    // we don't have resources available to build -- wait
                    state.targetUnit->second = std::nullopt;
                    return false;
                }
                state.targetUnit->second = getNanoPoint(unitInfo.id);

                if (targetUnit.addBuildProgress(targetUnitDefinition, unitInfo.definition->workerTimePerTick))
                {
                    sim->events.push_back(UnitCompleteEvent{state.targetUnit->first});

                    if (targetUnitDefinition.activateWhenBuilt)
                    {
                        sim->activateUnit(state.targetUnit->first);
                    }
                }

                return false;
            });
    }

    void UnitBehaviorService::clearBuild(UnitInfo unitInfo)
    {
        match(
            unitInfo.state->factoryState,
            [&](const FactoryBehaviorStateIdle&) {
                // do nothing
            },
            [&](const FactoryBehaviorStateCreatingUnit& state) {
                match(
                    state.status,
                    [&](const UnitCreationStatusDone& d) {
                        sim->quietlyKillUnit(d.unitId);
                    },
                    [&](const auto&) {
                        // do nothing
                    });
                sim->deactivateUnit(unitInfo.id);
                unitInfo.state->factoryState = FactoryBehaviorStateIdle();
            },
            [&](const FactoryBehaviorStateBuilding& state) {
                if (state.targetUnit)
                {
                    sim->quietlyKillUnit(state.targetUnit->first);
                    unitInfo.state->cobEnvironment->createThread("StopBuilding");
                }
                sim->deactivateUnit(unitInfo.id);
                unitInfo.state->factoryState = FactoryBehaviorStateIdle();
            });
    }

    SimVector UnitBehaviorService::getNanoPoint(UnitId id)
    {
        auto pieceId = runCobQuery(id, "QueryNanoPiece");
        if (!pieceId)
        {
            return sim->getUnitState(id).position;
        }

        return getPiecePosition(id, *pieceId);
    }

    SimVector UnitBehaviorService::getPieceLocalPosition(UnitId id, unsigned int pieceId)
    {
        auto& unit = sim->getUnitState(id);

        const auto& pieceName = unit.cobEnvironment->_script->pieces.at(pieceId);
        auto pieceTransform = sim->getUnitPieceLocalTransform(id, pieceName);

        return pieceTransform * SimVector(0_ss, 0_ss, 0_ss);
    }

    SimVector UnitBehaviorService::getPiecePosition(UnitId id, unsigned int pieceId)
    {
        auto& unit = sim->getUnitState(id);

        return unit.getTransform() * getPieceLocalPosition(id, pieceId);
    }

    SimAngle UnitBehaviorService::getPieceXZRotation(UnitId id, unsigned int pieceId)
    {
        auto& unit = sim->getUnitState(id);

        const auto& pieceName = unit.cobEnvironment->_script->pieces.at(pieceId);
        auto pieceTransform = sim->getUnitPieceLocalTransform(id, pieceName);

        auto mat = unit.getTransform() * pieceTransform;

        auto a = Vector2x<SimScalar>(0_ss, 1_ss);
        auto b = mat.mult3x3(SimVector(0_ss, 0_ss, 1_ss)).xz();
        if (b.lengthSquared() == 0_ss)
        {
            return SimAngle(0);
        }

        // angleTo is computed in a space where Y points up,
        // but in our XZ space (Z is our Y here), Z points down.
        // This means we need to negate (and rewrap) the rotation value.
        return -angleTo(a, b);
    }

    UnitBehaviorService::BuildPieceInfo UnitBehaviorService::getBuildPieceInfo(UnitId id)
    {
        auto pieceId = runCobQuery(id, "QueryBuildInfo");
        if (!pieceId)
        {
            const auto& unit = sim->getUnitState(id);
            return BuildPieceInfo{unit.position, unit.rotation};
        }

        return BuildPieceInfo{getPiecePosition(id, *pieceId), getPieceXZRotation(id, *pieceId)};
    }

    std::optional<SimVector> UnitBehaviorService::getTargetPosition(const UnitWeaponAttackTarget& target)
    {
        return match(
            target,
            [](const SimVector& v) { return std::make_optional(v); },
            [this](UnitId id) { return tryGetSweetSpot(id); });
    }

    PathDestination UnitBehaviorService::resolvePathDestination(UnitState& s, const MovingStateGoal& goal)
    {
        return match(
            goal,
            [&](const SimVector& v) -> PathDestination {
                return v;
            },
            [&](const DiscreteRect& r) -> PathDestination {
                return r;
            },
            [&](const UnitId& u) -> PathDestination {
                return getUnitPositionWithCache(s, u);
            });
    }

    void UnitBehaviorService::groundUnitMoveTo(UnitInfo unitInfo, const MovingStateGoal& goal)
    {
        auto movingState = std::get_if<NavigationStateMoving>(&unitInfo.state->navigationState.state);

        if (!movingState || movingState->movementGoal != goal)
        {
            // request a path to follow
            unitInfo.state->navigationState.state = NavigationStateMoving{goal, resolvePathDestination(*unitInfo.state, goal), std::nullopt, true};
            sim->requestPath(unitInfo.id);
            return;
        }

        // check to see if our goal has moved from its original location
        auto resolvedDestination = resolvePathDestination(*unitInfo.state, goal);
        if (resolvedDestination != movingState->pathDestination)
        {
            // The resolved position of our goal has changed.
            // We'll assume that this change isn't too big
            // i.e. we don't need to throw away our previous path,
            // we can still continue following it
            // while we wait for a new path to be computed.
            movingState->pathDestination = resolvedDestination;
            sim->requestPath(unitInfo.id);
            movingState->pathRequested = true;
        }

        // if we are colliding, request a new path
        if (unitInfo.state->inCollision && !movingState->pathRequested)
        {
            // only request a new path if we don't have one yet,
            // or we've already had our current one for a bit
            if (!movingState->path || (sim->gameTime - movingState->path->pathCreationTime) >= GameTime(30))
            {
                sim->requestPath(unitInfo.id);
                movingState->pathRequested = true;
            }
        }

        // if a path is available, attempt to follow it
        if (movingState->path)
        {
            auto groundPhysics = std::get_if<UnitPhysicsInfoGround>(&unitInfo.state->physics);
            if (groundPhysics == nullptr)
            {
                throw std::logic_error("ground unit does not have ground physics");
            }
            if (followPath(unitInfo, *groundPhysics, *movingState->path))
            {
                // We finished following the path.
                // This doesn't necessarily mean we are at the goal.
                // The path might have been a partial path.
                // Request a new path to get us the rest of the way there.
                if (!movingState->path || (sim->gameTime - movingState->path->pathCreationTime) >= GameTime(30))
                {
                    sim->requestPath(unitInfo.id);
                    movingState->pathRequested = true;
                }
            }
        }
    }

    bool UnitBehaviorService::flyingUnitMoveTo(UnitInfo unitInfo, const MovingStateGoal& goal)
    {
        return match(
            unitInfo.state->physics,
            [&](UnitPhysicsInfoGround& p) {
                p.steeringInfo.shouldTakeOff = true;
                return false;
            },
            [&](const UnitPhysicsInfoAir& p) {
                return flyTowardsGoal(unitInfo, goal);
            });
    }

    bool UnitBehaviorService::navigateTo(UnitInfo unitInfo, const NavigationGoal& goal)
    {
        unitInfo.state->navigationState.desiredDestination = goal;

        return hasReachedGoal(*sim, sim->terrain, *unitInfo.state, *unitInfo.definition, goal);
    }

    void UnitBehaviorService::moveTo(UnitInfo unitInfo, const MovingStateGoal& goal)
    {
        if (unitInfo.definition->canFly)
        {
            flyingUnitMoveTo(unitInfo, goal);
        }
        else
        {
            groundUnitMoveTo(unitInfo, goal);
        }
    }

    UnitCreationStatus UnitBehaviorService::createNewUnit(UnitInfo unitInfo, const std::string& unitType, const SimVector& position)
    {
        if (auto s = std::get_if<UnitBehaviorStateCreatingUnit>(&unitInfo.state->behaviourState))
        {
            if (s->unitType == unitType && s->position == position)
            {
                return s->status;
            }
        }

        const auto& targetUnitDefinition = sim->unitDefinitions.at(unitType);
        auto footprintRect = sim->computeFootprintRegion(position, targetUnitDefinition.movementCollisionInfo);
        if (navigateTo(unitInfo, footprintRect))
        {
            // If we only got "as close as we could" because the site cannot be
            // reached, give the order up rather than lathing across a wall.
            if (auto moving = std::get_if<NavigationStateMoving>(&unitInfo.state->navigationState.state); moving != nullptr && moving->reachableDestination)
            {
                return UnitCreationStatusFailed();
            }

            changeState(*unitInfo.state, UnitBehaviorStateCreatingUnit{unitType, unitInfo.state->owner, position});
            sim->unitCreationRequests.push_back(unitInfo.id);
        }

        return UnitCreationStatusPending();
    }

    bool UnitBehaviorService::buildUnit(UnitInfo unitInfo, const std::string& unitType, const SimVector& position)
    {
        auto& unit = sim->getUnitState(unitInfo.id);
        if (!unit.buildOrderUnitId)
        {
            auto result = createNewUnit(unitInfo, unitType, position);
            return match(
                result,
                [&](const UnitCreationStatusPending&) { return false; },
                [&](const UnitCreationStatusFailed&) { return true; },
                [&](const UnitCreationStatusDone& d) {
                    unit.buildOrderUnitId = d.unitId;
                    return deployBuildArm(unitInfo, d.unitId);
                });
        }

        return deployBuildArm(unitInfo, *unit.buildOrderUnitId);
    }

    bool UnitBehaviorService::reclaimTarget(UnitInfo unitInfo, std::variant<UnitId, FeatureId> target)
    {
        auto targetPosition = match(
            target,
            [&](const UnitId& id) -> std::optional<SimVector> {
                auto u = sim->tryGetUnitState(id);
                if (!u)
                {
                    return std::nullopt;
                }
                return u->get().position;
            },
            [&](const FeatureId& id) -> std::optional<SimVector> {
                const auto& f = sim->tryGetFeature(id);
                if (!f)
                {
                    return std::nullopt;
                }
                return f->get().position;
            });

        if (!targetPosition)
        {
            // target has gone away, throw away this order
            return true;
        }

        // FIXME: figure out actual range of reclaiming
        auto maxRangeSquared = 300_ss * 300_ss;
        if (unitInfo.state->position.distanceSquared(*targetPosition) > maxRangeSquared)
        {
            auto navigationGoal = match(
                target, [&](const UnitId& u) -> NavigationGoal { return u; }, [&](const FeatureId& f) -> NavigationGoal { return f; });
            navigateTo(unitInfo, navigationGoal);
        }
        else
        {
            // we're in range, start reclaiming
            return deployReclaimArm(unitInfo, target);
        }

        return false;
    }

    bool UnitBehaviorService::buildExistingUnit(UnitInfo unitInfo, UnitId targetUnitId)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);

        if (!targetUnitRef || targetUnitRef->get().isDead() || !targetUnitRef->get().isBeingBuilt(sim->unitDefinitions.at(targetUnitRef->get().unitType)))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();

        // Reach is measured to the building's footprint, not its centre, so a
        // builder stops as soon as it is within arm's length of any edge
        // instead of walking round to the front.
        if (!withinBuildReach(unitInfo, targetUnit))
        {
            // Out of arm's length: the spray stops until it is back in range.
            if (auto buildingState = std::get_if<UnitBehaviorStateBuilding>(&unitInfo.state->behaviourState))
            {
                buildingState->nanoParticleOrigin = std::nullopt;
            }
            const auto& targetDefinition = sim->unitDefinitions.at(targetUnit.unitType);
            auto rect = sim->computeFootprintRegion(targetUnit.position, targetDefinition.movementCollisionInfo);
            auto reachTiles = std::max(0, static_cast<int>(simScalarToFloat(unitInfo.definition->buildDistance) / simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits)) - 1);
            navigateTo(unitInfo, rect.expand(reachTiles));
            return false;
        }

        // we're close enough -- actually build the unit
        return deployBuildArm(unitInfo, targetUnitId);
    }

    void UnitBehaviorService::changeState(UnitState& unit, const UnitBehaviorState& newState)
    {
        bool wasWorking = std::holds_alternative<UnitBehaviorStateBuilding>(unit.behaviourState)
            || std::holds_alternative<UnitBehaviorStateReclaiming>(unit.behaviourState);
        bool willBeWorking = std::holds_alternative<UnitBehaviorStateBuilding>(newState)
            || std::holds_alternative<UnitBehaviorStateReclaiming>(newState);

        // Stow the arm when the work stops. Moving straight from one job to
        // the next — a builder following a factory from one unit to the next,
        // say — keeps it out: stowing and redeploying in the same breath wins
        // nothing and risks the stow clearing the build stance the redeploy
        // just set, which leaves the builder with its arm up doing nothing.
        if (wasWorking && !willBeWorking)
        {
            unit.cobEnvironment->createThread("StopBuilding");
        }
        unit.behaviourState = newState;
    }
    bool UnitBehaviorService::deployBuildArm(UnitInfo unitInfo, UnitId targetUnitId)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);
        if (!targetUnitRef || targetUnitRef->get().isDead() || !targetUnitRef->get().isBeingBuilt(sim->unitDefinitions.at(targetUnitRef->get().unitType)))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();
        const auto& targetUnitDefinition = sim->unitDefinitions.at(targetUnit.unitType);

        if (!prepareBuilderForWork(unitInfo, targetUnit.position))
        {
            return false;
        }

        return match(
            unitInfo.state->behaviourState,
            [&](UnitBehaviorStateBuilding& buildingState) {
                if (targetUnitId != buildingState.targetUnit)
                {
                    changeState(*unitInfo.state, UnitBehaviorStateIdle());
                    return buildExistingUnit(unitInfo, targetUnitId);
                }

                if (!unitInfo.state->inBuildStance)
                {
                    // We are not in the correct stance to build the unit yet, wait.
                    return false;
                }

                auto costs = targetUnit.getBuildCostInfo(targetUnitDefinition, unitInfo.definition->workerTimePerTick);
                auto gotResources = sim->addResourceDelta(
                    unitInfo.id,
                    -Energy(targetUnitDefinition.buildCostEnergy.value * static_cast<float>(unitInfo.definition->workerTimePerTick) / static_cast<float>(targetUnitDefinition.buildTime)),
                    -Metal(targetUnitDefinition.buildCostMetal.value * static_cast<float>(unitInfo.definition->workerTimePerTick) / static_cast<float>(targetUnitDefinition.buildTime)),
                    -costs.energyCost,
                    -costs.metalCost);

                if (!gotResources)
                {
                    // we don't have resources available to build -- wait
                    buildingState.nanoParticleOrigin = std::nullopt;
                    return false;
                }
                buildingState.nanoParticleOrigin = getNanoPoint(unitInfo.id);

                if (targetUnit.addBuildProgress(targetUnitDefinition, unitInfo.definition->workerTimePerTick))
                {
                    sim->events.push_back(UnitCompleteEvent{buildingState.targetUnit});

                    if (targetUnitDefinition.activateWhenBuilt)
                    {
                        sim->activateUnit(buildingState.targetUnit);
                    }

                    changeState(*unitInfo.state, UnitBehaviorStateIdle());
                    return true;
                }
                return false;
            },
            [&](const auto&) {
                // An arm still being stowed by a StopBuilding thread sets
                // INBUILDSTANCE back to zero, and if it gets there after
                // StartBuilding has set it the builder ends up with its arm
                // raised and nothing else happening. Let the stow finish.
                if (unitInfo.state->cobEnvironment->isThreadRunning("StopBuilding"))
                {
                    return false;
                }

                auto nanoFromPosition = getNanoPoint(unitInfo.id);
                auto headingAndPitch = computeLineOfSightHeadingAndPitch(unitInfo.state->rotation, nanoFromPosition, targetUnit.position);
                auto heading = headingAndPitch.first;
                auto pitch = headingAndPitch.second;

                changeState(*unitInfo.state, UnitBehaviorStateBuilding{targetUnitId, std::nullopt});
                unitInfo.state->cobEnvironment->createThread("StartBuilding", {toCobAngle(heading).value, toCobAngle(pitch).value});
                return false;
            });
    }

    bool UnitBehaviorService::deployReclaimArm(UnitInfo unitInfo, std::variant<UnitId, FeatureId> target)
    {
        auto isValidTarget = match(
            target,
            [&](const UnitId& targetUnitId) {
                auto targetUnitRef = sim->tryGetUnitState(targetUnitId);
                return targetUnitRef && targetUnitRef->get().isAlive();
            },
            [&](const FeatureId& targetFeatureId) {
                auto targetFeatureRef = sim->tryGetFeature(targetFeatureId);
                if (!targetFeatureRef)
                {
                    return false;
                }
                return sim->getFeatureDefinition(targetFeatureRef->get().featureName).reclaimable;
            });

        if (!isValidTarget)
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }

        auto workPosition = match(
            target,
            [&](const UnitId& targetUnitId) { return sim->getUnitState(targetUnitId).position; },
            [&](const FeatureId& targetFeatureId) { return sim->getFeature(targetFeatureId).position; });
        if (!prepareBuilderForWork(unitInfo, workPosition))
        {
            return false;
        }

        return match(
            unitInfo.state->behaviourState,
            [&](UnitBehaviorStateReclaiming& reclaimingState) {
                if (target != reclaimingState.target)
                {
                    changeState(*unitInfo.state, UnitBehaviorStateIdle());
                    return reclaimTarget(unitInfo, target);
                }

                if (!unitInfo.state->inBuildStance)
                {
                    // We are not in the correct stance to build the unit yet, wait.
                    return false;
                }

                reclaimingState.nanoParticleOrigin = getNanoPoint(unitInfo.id);

                return match(
                    target,
                    [&](const UnitId& targetUnitId) {
                        auto finished = sim->reclaimUnit(targetUnitId, unitInfo.state->owner, unitInfo.definition->workerTimePerTick);
                        if (finished)
                        {
                            changeState(*unitInfo.state, UnitBehaviorStateIdle());
                        }
                        return finished;
                    },
                    [&](const FeatureId& targetFeatureId) {
                        auto finished = sim->reclaimFeature(targetFeatureId, unitInfo.state->owner, unitInfo.definition->workerTimePerTick);
                        if (finished)
                        {
                            changeState(*unitInfo.state, UnitBehaviorStateIdle());
                        }
                        return finished;
                    });
            },
            [&](const auto&) {
                // As in deployBuildArm: wait for any stow to finish, or the
                // arm goes up and the unit then sits there doing nothing.
                if (unitInfo.state->cobEnvironment->isThreadRunning("StopBuilding"))
                {
                    return false;
                }

                auto nanoFromPosition = getNanoPoint(unitInfo.id);
                auto targetPosition = match(
                    target,
                    [&](const UnitId& targetUnitId) {
                        const auto& targetUnit = sim->getUnitState(targetUnitId);
                        return targetUnit.position;
                    },
                    [&](const FeatureId& targetFeatureId) {
                        const auto& targetFeature = sim->getFeature(targetFeatureId);
                        return targetFeature.position;
                    });
                auto headingAndPitch = computeLineOfSightHeadingAndPitch(unitInfo.state->rotation, nanoFromPosition, targetPosition);
                auto heading = headingAndPitch.first;
                auto pitch = headingAndPitch.second;

                changeState(*unitInfo.state, UnitBehaviorStateReclaiming{target, std::nullopt});
                unitInfo.state->cobEnvironment->createThread("StartBuilding", {toCobAngle(heading).value, toCobAngle(pitch).value});
                return false;
            });
    }

    bool UnitBehaviorService::deployRepairArm(UnitInfo unitInfo, UnitId targetUnitId)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);
        if (!targetUnitRef || targetUnitRef->get().isDead())
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();
        const auto& targetUnitDefinition = sim->unitDefinitions.at(targetUnit.unitType);

        // Repairing reuses the building state so the nanolathe effect and
        // the StartBuilding/StopBuilding script hooks behave the same way.
        return match(
            unitInfo.state->behaviourState,
            [&](UnitBehaviorStateBuilding& buildingState) {
                if (targetUnitId != buildingState.targetUnit)
                {
                    changeState(*unitInfo.state, UnitBehaviorStateIdle());
                    return repairExistingUnit(unitInfo, targetUnitId);
                }

                if (!unitInfo.state->inBuildStance)
                {
                    // We are not in the correct stance to repair the unit yet, wait.
                    return false;
                }

                buildingState.nanoParticleOrigin = getNanoPoint(unitInfo.id);

                // Repair at the same rate the unit would be built: full health
                // takes buildTime worth of worker time. Repairing costs nothing,
                // as in TA.
                auto buildTime = std::max(1u, targetUnitDefinition.buildTime);
                auto healthPerTick = std::max(1u, (targetUnitDefinition.maxHitPoints * unitInfo.definition->workerTimePerTick) / buildTime);
                targetUnit.hitPoints = std::min(targetUnitDefinition.maxHitPoints, targetUnit.hitPoints + healthPerTick);

                if (targetUnit.hitPoints >= targetUnitDefinition.maxHitPoints)
                {
                    changeState(*unitInfo.state, UnitBehaviorStateIdle());
                    return true;
                }
                return false;
            },
            [&](const auto&) {
                // An arm still being stowed by a StopBuilding thread sets
                // INBUILDSTANCE back to zero, and if it gets there after
                // StartBuilding has set it the builder ends up with its arm
                // raised and nothing else happening. Let the stow finish.
                if (unitInfo.state->cobEnvironment->isThreadRunning("StopBuilding"))
                {
                    return false;
                }

                auto nanoFromPosition = getNanoPoint(unitInfo.id);
                auto headingAndPitch = computeLineOfSightHeadingAndPitch(unitInfo.state->rotation, nanoFromPosition, targetUnit.position);
                auto heading = headingAndPitch.first;
                auto pitch = headingAndPitch.second;

                changeState(*unitInfo.state, UnitBehaviorStateBuilding{targetUnitId, std::nullopt});
                unitInfo.state->cobEnvironment->createThread("StartBuilding", {toCobAngle(heading).value, toCobAngle(pitch).value});
                return false;
            });
    }

    bool UnitBehaviorService::climbToCruiseAltitude(UnitInfo unitInfo)
    {
        auto targetHeight = getTargetAltitude(sim->terrain, unitInfo.state->position.x, unitInfo.state->position.z, *unitInfo.definition);

        unitInfo.state->position.y = rweMin(unitInfo.state->position.y + 1_ss, targetHeight);

        return unitInfo.state->position.y == targetHeight;
    }

    bool UnitBehaviorService::descendToGroundLevel(UnitInfo unitInfo)
    {
        auto terrainHeight = sim->terrain.getHeightAt(unitInfo.state->position.x, unitInfo.state->position.z);

        unitInfo.state->position.y = rweMax(unitInfo.state->position.y - 1_ss, terrainHeight);

        return unitInfo.state->position.y == terrainHeight;
    }

    void UnitBehaviorService::transitionFromGroundToAir(UnitInfo unitInfo)
    {
        unitInfo.state->activate();

        unitInfo.state->physics = UnitPhysicsInfoAir();
        auto footprintRect = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
        auto footprintRegion = sim->occupiedGrid.tryToRegion(footprintRect);
        assert(!!footprintRegion);
        sim->occupiedGrid.forEach(*footprintRegion, [](auto& cell) {
            cell.mobileUnitId = std::nullopt;
        });
        sim->flyingUnitsSet.insert(unitInfo.id);
    }

    bool UnitBehaviorService::tryTransitionFromAirToGround(UnitInfo unitInfo)
    {
        auto footprintRect = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
        auto footprintRegion = sim->occupiedGrid.tryToRegion(footprintRect);
        assert(!!footprintRegion);

        if (sim->isCollisionAt(*footprintRegion))
        {
            return false;
        }

        sim->occupiedGrid.forEach(*footprintRegion, [&](auto& cell) {
            cell.mobileUnitId = unitInfo.id;
        });
        sim->flyingUnitsSet.erase(unitInfo.id);

        unitInfo.state->physics = UnitPhysicsInfoGround();

        return true;
    }

    bool UnitBehaviorService::flyTowardsGoal(UnitInfo unitInfo, const MovingStateGoal& goal)
    {
        auto destination = match(
            goal,
            [&](const SimVector& pos) {
                return pos;
            },
            [&](const DiscreteRect& rect) {
                return findClosestPointToFootprintXZ(sim->terrain, rect, unitInfo.state->position);
            },
            [&](const UnitId& u) {
                const auto& unit = sim->getUnitState(u);
                return unit.position;
            });

        SimVector xzPosition(unitInfo.state->position.x, 0_ss, unitInfo.state->position.z);
        SimVector xzDestination(destination.x, 0_ss, destination.z);
        auto distanceSquared = xzPosition.distanceSquared(xzDestination);

        if (distanceSquared < (8_ss * 8_ss))
        {
            return true;
        }

        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr)
        {
            throw std::logic_error("cannot fly towards goal because unit does not have air physics");
        }

        match(
            airPhysics->movementState,
            [&](AirMovementStateFlying& m) {
                auto targetHeight = getTargetAltitude(sim->terrain, destination.x, destination.z, *unitInfo.definition);
                SimVector destinationAtAltitude(destination.x, targetHeight, destination.z);

                m.targetPosition = destinationAtAltitude;
            },
            [&](AirMovementStateTakingOff& m) {
                // Head that way as soon as the wheels leave the ground.
                auto targetHeight = getTargetAltitude(sim->terrain, destination.x, destination.z, *unitInfo.definition);
                m.targetPosition = SimVector(destination.x, targetHeight, destination.z);
            },
            [&](AirMovementStateLanding& m) {
                m.shouldAbort = true;
            },
            [&](const AirMovementStateAttackRun&) {
                // Aircraft is mid-attack-run; the run handler decides where to fly.
            });

        return false;
    }
}
