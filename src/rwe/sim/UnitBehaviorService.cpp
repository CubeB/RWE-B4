#include "UnitBehaviorService.h"
#include <algorithm>
#include <limits>
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
    namespace
    {
        /**
         * How near a gunship has to get to its station before it counts as
         * arrived and picks the next one. The original uses sixteen units,
         * which is close enough that it really does fly to each point.
         */
        const SimScalar HoverAttackArrivalTolerance = 16_ss;

        // The original's idle circuits, read out of VTOL_SeekAttack (0x4103E0,
        // where an aircraft goes when its target dies) and VTOL_Follow
        // (0x40FBE0, the guard mission). Both build the same thing: a goal on
        // a ring around a point, and a bearing that steps back by a fixed
        // amount plus a random eighth of a turn every time the goal is met.

        // The goal's arrival tolerance, 0x80 at 0x4106C4 and 0x410211.
        const SimScalar AirLoiterArrivalTolerance = 128_ss;

        // The ring is weapon range plus this, 0xA0 at 0x41064B and 0x410175.
        const SimScalar AirLoiterStandoff = 160_ss;

        // What a unit with no weapon at all uses instead: 0x1400000, i.e. 320
        // world units, at 0x4101B0. The branch is on unit+0x110 bit 31, which
        // is copied out of the definition bit meaning "names a Weapon1, 2 or
        // 3" (0x485AAD), and it has to be there: the armed side reads weapon
        // slot zero's definition, which an unarmed unit does not have. So a
        // construction aircraft guarding a factory works a ring twenty tiles
        // across around it, which is the milling about that was reported.
        const SimScalar AirLoiterUnarmedRadius = 320_ss;

        // 0x5555, a third of a turn, at 0x410634. The search circuit's step.
        const SimAngle AirLoiterSeekStep = SimAngle(0x5555);

        // 0x4000, a quarter turn, at 0x410151. The guard circuit's step.
        const SimAngle AirLoiterGuardStep = SimAngle(0x4000);

        // rand(0x2000) on top of either, at 0x410625 and 0x410142. Both are
        // subtracted, so a circuit always works round the same way.
        const SimAngle AirLoiterStepJitter = SimAngle(0x2000);

        SimVector airVelocity(const AirMovementState& state)
        {
            return match(
                state,
                [](const AirMovementStateFlying& m) { return m.currentVelocity; },
                [](const AirMovementStateTakingOff& m) { return m.currentVelocity; },
                [](const AirMovementStateAttackRun& m) { return m.currentVelocity; },
                [](const AirMovementStateHoverAttack& m) { return m.currentVelocity; },
                [](const AirMovementStateLanding&) { return SimVector(0_ss, 0_ss, 0_ss); });
        }

        /**
         * The bank an aircraft is holding, which in the original is honest
         * aerodynamics rather than an animation: tan(roll) = BankScale times
         * the sideways acceleration over gravity.
         *
         * The acceleration is passed through a one-pole lag, and the constants
         * are chosen so the lag's gain cancels out of the division — which is
         * what gives the ramp on entering a turn and the wash-out on leaving
         * it, with no rate limit or clamp needed anywhere.
         */
        SimScalar computeNewBankAngle(const UnitState& unit, const UnitDefinition& unitDefinition, UnitPhysicsInfoAir& physics, const SimVector& deltaVelocity)
        {
            // 62259/65536 in the original's fixed point.
            const SimScalar lag(0.9499817f);
            physics.bankAccum = (physics.bankAccum * lag) + deltaVelocity;

            // Sideways is the component along the aircraft's right hand.
            auto heading = unit.rotation;
            auto lateral = (physics.bankAccum.x * cos(heading)) - (physics.bankAccum.z * sin(heading));

            // Gravity is 112 world units per second squared on nearly every
            // map the game ships, which is 112/900 per tick squared; dividing
            // by (1 - lag) undoes the lag's gain.
            const SimScalar gravityOverLagGain((112.0f / 900.0f) / (1.0f - 0.9499817f));

            // Accelerating to the right drops the right wing, so the model
            // rolls the other way about its nose.
            auto angle = atan2(-unitDefinition.bankScale * lateral, gravityOverLagGain);
            auto radians = toRadians(angle).value;
            if (radians > Pif)
            {
                radians -= 2.0f * Pif;
            }
            return SimScalar(radians);
        }

    }

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

        // The slow work facing only holds while the work pattern asserts it
        // afresh each tick; any other business turns normally.
        unitInfo.state->slowFacePoint = std::nullopt;

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
                    },
                    [&](const AirMovementStateHoverAttack&) {
                        // As above: the gunship handler owns its own station.
                    });
            });

        // clear navigation targets
        unitInfo.state->navigationState.desiredDestination = std::nullopt;

        // A stunned unit does nothing at all. The original parks a sleeping
        // order in front of the unit's own for the duration (0x402D10) and
        // clears every weapon's aim on the way in (0x402D46), so the unit
        // coasts to a halt, stops shooting and stops building -- and picks up
        // whatever it was doing when it comes round, because its orders are
        // still underneath. The scripts keep running, so it goes on smoking.
        auto paralyzed = unitInfo.state->isParalyzed(sim->gameTime);
        if (paralyzed)
        {
            unitInfo.state->clearWeaponTargets();
        }

        // Run unit and weapon AI
        if (!paralyzed && !unitInfo.state->isBeingBuilt(*unitInfo.definition))
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
                // An aircraft with nothing left to do goes and lands — but not
                // while there is something in front of it worth shooting. The
                // original hands an idle unit to a search at sight range and
                // turns whatever it finds into an attack mission, which for a
                // gunship is the standoff ring. RWE only ever pointed the
                // weapon, so a Brawler acquired a target through the ordinary
                // weapon path and then either shot at it from wherever it
                // happened to be or broke off to land while still firing,
                // which is not something either of these two ever does.
                std::optional<UnitId> freeTarget;
                if (unitInfo.definition->canAttack && unitInfo.state->fireOrders != UnitFireOrders::HoldFire)
                {
                    freeTarget = findEnemyInWeaponRange(unitInfo);
                    // Only go after something the owner can actually see. The
                    // search itself does not check, because a unit already on
                    // patrol breaks off on contact and has been relied on to
                    // do that from its first tick, before visibility for the
                    // tick has been worked out.
                    if (freeTarget && !sim->canDetectUnit(unitInfo.state->owner, *freeTarget))
                    {
                        freeTarget.reset();
                    }
                }

                if (freeTarget)
                {
                    // Give it a real order rather than steering it for one
                    // tick. The original turns what the search finds into a
                    // mission and keeps it until the target dies or the leash
                    // trips, and that persistence matters here: a gunship
                    // holds its ring at two thirds of weapon range, which for
                    // a Brawler is further out than the eight-cell cap on its
                    // own sight, so a unit that re-decided every tick would
                    // reach its station, lose the target it was already
                    // shooting at, and go looking for somewhere to land.
                    unitInfo.state->orders.push_back(createAttackOrder(*freeTarget));
                }
                else if (unitInfo.state->airLoiter && unitInfo.state->airLoiter->reason == UnitState::AirLoiterState::Reason::AttackEnded)
                {
                    // An attack that ran out of target does not end with the
                    // aircraft going home. AirStrike (0x411F50) hands the
                    // aircraft a VTOL_SeekAttack anchored on its own position
                    // the moment its target dies, and that mission walks a
                    // bearing round the spot for ever — it only ever gives up
                    // to go and land when the aircraft is below three quarters
                    // health with a repair pad within reach. So the bomber
                    // keeps flying over what it just flattened, which is what
                    // the original looks like and what RWE was missing.
                    auto anchor = unitInfo.state->airLoiter->anchor;
                    flyAirLoiterCircuit(unitInfo, UnitState::AirLoiterState::Reason::AttackEnded, anchor, AirLoiterSeekStep);
                }
                else
                {
                    // Anything else that has simply run out of orders does go
                    // home: the original installs the definition's
                    // DefaultMissionType, which for all twenty-one shipped
                    // aircraft is VTOL_Standby (0x40F7D0), and that hops about
                    // only while the aircraft is carrying something. Empty, it
                    // pushes VTOL_LandIfCan and sets down.
                    unitInfo.state->airLoiter = std::nullopt;
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
                    },
                    [&](const AirMovementStateHoverAttack&) {
                        // Same again: a gunship left on station with no orders
                        // and nothing in range would otherwise shuttle back and
                        // forth for ever.
                        airPhysics->movementState = AirMovementStateFlying();
                        unitInfo.state->clearWeaponTargets();
                    });
                }
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
                updateWeaponStockpile(unitId, i);
                updateWeapon(unitId, i);
            }
        }

        if (unitInfo.definition->isMobile)
        {
            updateNavigation(unitInfo);

            applyUnitSteering(unitInfo);

            updateUnitPosition(unitInfo);

            updateMoveRateBand(unitInfo);

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
                        },
                        [&](const AirMovementStateHoverAttack&) {
                            // Likewise the gunship: hoverAttackTarget owns the transitions.
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

    void UnitBehaviorService::updateWeaponStockpile(UnitId id, unsigned int weaponIndex)
    {
        auto& unit = sim->getUnitState(id);
        auto& weapon = unit.weapons[weaponIndex];
        if (!weapon)
        {
            return;
        }

        const auto& weaponDefinition = sim->weaponDefinitions.at(weapon->weaponType);
        if (!weaponDefinition.stockpile)
        {
            return;
        }

        if (weapon->stockpileStepDelay > 0)
        {
            --weapon->stockpileStepDelay;
            return;
        }

        if (weapon->queuedRounds <= 0)
        {
            return;
        }

        // A full magazine holds on to the order rather than dropping it: the
        // original sits still and looks again in ten seconds (0x402CD0).
        if (weapon->stockedRounds >= MaxStockedRounds)
        {
            weapon->stockpileStepDelay = StockpileFullRetryTicks;
            return;
        }

        // TotalA.exe 0x402BD4. A round takes the weapon's own reloadtime to
        // build, and the cost is a straight ramp across that: after P ticks the
        // total spent is trunc(P * cost / T), and this step pays the difference.
        // Truncating each end rather than the step itself is what makes the
        // whole run come to exactly what the TDF asked for, however awkward the
        // division -- replaying it against the shipped numbers gives a nuclear
        // missile 180 seconds, 2000 metal and 180000 energy to the unit.
        auto totalTicks = std::max(1, static_cast<int>(deltaSecondsToTicks(weaponDefinition.reloadTime).value));
        auto from = weapon->stockpileProgress;
        auto to = std::min(from + StockpileStepTicks, totalTicks);

        auto energy = Energy(static_cast<float>(
            stockpileRampTotal(to, totalTicks, weaponDefinition.energyPerShot.value)
            - stockpileRampTotal(from, totalTicks, weaponDefinition.energyPerShot.value)));
        auto metal = Metal(static_cast<float>(
            stockpileRampTotal(to, totalTicks, weaponDefinition.metalPerShot.value)
            - stockpileRampTotal(from, totalTicks, weaponDefinition.metalPerShot.value)));

        if (!sim->addResourceDelta(id, -energy, -metal))
        {
            // Nothing was taken, so nothing was built. The original makes no
            // progress and comes back in ten ticks rather than five (0x402C9C),
            // so a stalled silo asks the economy half as often.
            weapon->stockpileStepDelay = StockpileStallRetryTicks;
            return;
        }

        weapon->stockpileProgress = to;
        // The work happens on one tick in five, so four idle ticks follow it.
        weapon->stockpileStepDelay = StockpileStepTicks - 1;

        if (to >= totalTicks)
        {
            ++weapon->stockedRounds;
            --weapon->queuedRounds;
            weapon->stockpileProgress = 0;
        }
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
                if (auto target = chooseTarget(id, weaponIndex))
                {
                    weapon->state = UnitWeaponStateAttacking(*target);
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

            // If we are on hold fire, the target is a unit,
            // and we don't have an explicit order to attack that unit,
            // drop the target.
            // A unit on return fire keeps what it picked up: whatever it is
            // holding, it was handed by the thing that shot it.
            if (unit.fireOrders == UnitFireOrders::HoldFire)
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
                            [&](const AirMovementStateHoverAttack& m) { bomberVelocity = m.currentVelocity; },
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

                if (weaponNeedsTheHullTurned(weaponDefinition))
                {
                    // Nothing on a mount to swing, so there is no aim script to
                    // run and nothing to wait for: the original skips straight
                    // past the script call for these (0x49E205) and lets the
                    // fire handler decide. That handler measures the bearing to
                    // the target against the unit's own heading and declines
                    // the shot if the two are further apart than tolerance
                    // (0x49DA59 into the check at 0x49D880), so the gun stays
                    // quiet until whatever is steering the unit brings it round.
                    if (angleBetweenIsLessOrEqual(heading, SimAngle(0), weaponDefinition.tolerance)
                        && sim->gameTime >= weapon->readyTime)
                    {
                        aimingState->attackInfo = UnitWeaponStateAttacking::FireInfo{heading, pitch, *targetPosition, std::nullopt, 0, GameTime(0)};
                        tryFireWeapon(id, weaponIndex);
                    }

                    return;
                }

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

        // What the shot costs is settled before anything else happens
        // (TotalA.exe 0x49E3D5). A stockpiled weapon needs a round in the
        // magazine; everything else has to find its energypershot and
        // metalpershot in the player's stores there and then. Either way a
        // weapon that cannot pay simply does not fire -- the original never
        // lets the shot go and takes the debt afterwards, which is what stops a
        // commander with a flat battery from D-gunning anything.
        if (weaponDefinition.stockpile)
        {
            if (weapon->stockedRounds <= 0)
            {
                return;
            }
        }
        else
        {
            // Asked against the stores directly rather than through the
            // ordinary request-and-settle path. That path pays every consumer
            // a share of whatever there is and carries the rest as debt, which
            // is right for a builder but not for a shot: the original settles
            // the price before the round leaves the barrel, and a weapon that
            // cannot cover it in full simply does not fire.
            const auto& player = sim->getPlayer(unit.owner);
            if (player.energy < weaponDefinition.energyPerShot || player.metal < weaponDefinition.metalPerShot)
            {
                return;
            }
            sim->addResourceDelta(id, -weaponDefinition.energyPerShot, -weaponDefinition.metalPerShot);
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
            [&](const ProjectilePhysicsTypeSelfPropelled&) {
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
                    [&](const AirMovementStateHoverAttack& m) { bomberVelocity = m.currentVelocity; },
                    [&](const AirMovementStateLanding&) {});
            }
            inheritedVelocity = bomberVelocity;
        }

        auto targetUnit = std::get_if<UnitId>(&attackInfo->target);
        auto targetUnitOption = targetUnit == nullptr ? std::optional<UnitId>() : std::make_optional(*targetUnit);
        sim->spawnProjectile(unit.owner, *weapon, firingPoint, direction, (fireInfo->targetPosition - firingPoint).length(), targetUnitOption, id, inheritedVelocity, fireInfo->targetPosition);

        sim->events.push_back(FireWeaponEvent{weapon->weaponType, fireInfo->burstsFired, firingPoint});

        if (weaponDefinition.stockpile)
        {
            --weapon->stockedRounds;
        }

        // If we just started the burst, set the reload timer
        if (fireInfo->burstsFired == 0)
        {
            unit.cobEnvironment->createThread(getFireScriptName(weaponIndex));
            if (!weaponDefinition.stockpile)
            {
                // A stockpiled weapon gets no reload timer at all: the original
                // takes the round and jumps straight past the reload
                // calculation (0x49E455), because the wait was the build.
                weapon->readyTime = gameTime + deltaSecondsToTicks(weaponDefinition.reloadTime);
            }
        }

        // Recoil: let the script rock the unit away from the shot. TA's own
        // rockunit.h takes an angle about each of the x and z axes, heels the
        // hull over that far and lets it settle back.
        //
        // The original asks for this after every projectile it spawns and
        // never asks for a bomb (0x49DD60, the bomb handler, is the one fire
        // handler that does not go through a spawn routine), and the angle is
        // always the same rockUnitAngle -- see the note on that constant.
        // Only seventeen of the two hundred shipped scripts define a
        // RockUnit, by including rockunit.h or its floating-structure
        // variant; for the other hundred and eighty-three the original's
        // script lookup comes back empty and starts nothing, which is what
        // createThread does here too.
        if (!isBomb)
        {
            // Which way it heels is worked out in the unit's own frame, so a
            // tank broadside on to its target rolls rather than pitches.
            auto rockAngles = computeRockUnitAngles(unit.rotation, direction, intToSimScalar(rockUnitAngle));
            unit.cobEnvironment->createThread("RockUnit", {rockAngles.first, rockAngles.second});
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
                        // Working on something, a construction aircraft keeps
                        // its nose on the job rather than on where it is
                        // flying — turned at its own rate, which for these
                        // aircraft is slow enough that the step to the next
                        // station takes most of the five seconds to come
                        // round, exactly as in the original.
                        if (unitInfo.state->slowFacePoint)
                        {
                            auto direction = *unitInfo.state->slowFacePoint - unitInfo.state->position;
                            direction.y = 0_ss;
                            if (direction.lengthSquared() > 0_ss)
                            {
                                unitInfo.state->rotation = turnTowards(unitInfo.state->rotation, UnitState::toRotation(direction), turnRateThisFrame);
                            }
                            return;
                        }
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
                    },
                    [&](const AirMovementStateHoverAttack& m) {
                        // A gunship keeps its nose on the target and slides
                        // sideways around the ring, which is what the swing
                        // looks like from the ground and, more to the point,
                        // the only way it can shoot at all: both gunship
                        // weapons say turret=0, so the gun is bolted to the
                        // hull and only bears within about a third of a right
                        // angle of straight ahead. Pointing the nose along the
                        // flight path instead leaves it crossing the target at
                        // better than sixty degrees off, and it managed under
                        // a quarter of the fire it should.
                        SimVector toTarget(
                            m.targetPosition.x - unitInfo.state->position.x,
                            0_ss,
                            m.targetPosition.z - unitInfo.state->position.z);
                        if (toTarget.lengthSquared() == 0_ss)
                        {
                            return;
                        }
                        unitInfo.state->rotation = turnTowards(unitInfo.state->rotation, UnitState::toRotation(toTarget), turnRateThisFrame);
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
                p.previousRoll = p.roll;
                auto velocityBefore = airVelocity(p.movementState);

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
                    },
                    [&](AirMovementStateHoverAttack& m) {
                        m.currentVelocity = computeNewHoverAttackVelocity(*unitInfo.state, *unitInfo.definition, m);
                    });

                p.roll = computeNewBankAngle(*unitInfo.state, *unitInfo.definition, p, airVelocity(p.movementState) - velocityBefore);
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
                    },
                    [&](const AirMovementStateHoverAttack& m) {
                        // Same as the attack run: fly the velocity, then ease
                        // towards cruise height rather than snapping to it.
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

        // Any order at all ends an idle circuit, so that when this one is done
        // the aircraft goes home as it should. A guard order is the exception:
        // it flies a circuit of its own and needs the bearing it has built up.
        if (!std::holds_alternative<GuardOrder>(order))
        {
            unitInfo.state->airLoiter = std::nullopt;
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

    SimScalar UnitBehaviorService::airLoiterRadius(UnitInfo unitInfo) const
    {
        const auto& weapon = unitInfo.state->weapons[0];
        if (!weapon)
        {
            return AirLoiterUnarmedRadius;
        }
        return sim->weaponDefinitions.at(weapon->weaponType).maxRange + AirLoiterStandoff;
    }

    void UnitBehaviorService::beginAirLoiter(UnitInfo unitInfo, UnitState::AirLoiterState::Reason reason, const SimVector& anchor)
    {
        if (!unitInfo.definition->canFly)
        {
            return;
        }

        // The original takes a fresh bearing at random when it sets one of
        // these up (0x410774 for the search, 0x410310 for the guard), so a
        // flight coming off the same target does not all end up on one side
        // of it.
        std::uniform_int_distribution<unsigned int> anywhere(0, 0xffffu);
        unitInfo.state->airLoiter = UnitState::AirLoiterState{reason, anchor, SimAngle(anywhere(sim->rng))};
    }

    void UnitBehaviorService::flyAirLoiterCircuit(UnitInfo unitInfo, UnitState::AirLoiterState::Reason reason, const SimVector& anchor, SimAngle stepBase)
    {
        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr)
        {
            // Sitting on the ground there is no circuit to fly, and a stale
            // one left behind would keep the aircraft from ever landing again.
            unitInfo.state->airLoiter = std::nullopt;
            return;
        }

        // Climbing out or setting down: leave the transition alone. The
        // original's handlers do the same, sleeping until the aircraft is
        // properly airborne before they command anything (0x40F957).
        if (std::holds_alternative<AirMovementStateTakingOff>(airPhysics->movementState)
            || std::holds_alternative<AirMovementStateLanding>(airPhysics->movementState))
        {
            return;
        }

        // The circuit has to be reachable from an attack run and from the
        // gunship's ring as well as from level flight. The case this was
        // written for is a bomber whose target dies in the middle of its run,
        // which is exactly where a handover that only works from level flight
        // leaves an aircraft stuck.
        if (!std::holds_alternative<AirMovementStateFlying>(airPhysics->movementState))
        {
            AirMovementStateFlying flying;
            flying.currentVelocity = airVelocity(airPhysics->movementState);
            airPhysics->movementState = flying;
            unitInfo.state->clearWeaponTargets();
        }
        auto& flying = std::get<AirMovementStateFlying>(airPhysics->movementState);

        if (!unitInfo.state->airLoiter || unitInfo.state->airLoiter->reason != reason)
        {
            beginAirLoiter(unitInfo, reason, anchor);
            if (!unitInfo.state->airLoiter)
            {
                return;
            }
        }
        auto& loiter = *unitInfo.state->airLoiter;

        // A guard follows what it is guarding, so the centre is refreshed
        // every tick; a search circuit is handed the same fixed point back.
        loiter.anchor = anchor;

        auto radius = airLoiterRadius(unitInfo);
        auto station = loiter.anchor + (UnitState::toDirection(loiter.bearing) * radius);

        SimVector toStation(station.x - unitInfo.state->position.x, 0_ss, station.z - unitInfo.state->position.z);
        if (toStation.lengthSquared() <= AirLoiterArrivalTolerance * AirLoiterArrivalTolerance)
        {
            // On station. Work the bearing round for the next one. The step is
            // more than a quarter turn and less than half of one, so successive
            // stations are joined by chords that pass close to the middle:
            // this is why a bomber keeps coming back over what it killed
            // rather than settling into a tidy orbit.
            std::uniform_int_distribution<unsigned int> jitter(0, AirLoiterStepJitter.value);
            loiter.bearing = loiter.bearing - stepBase - SimAngle(jitter(sim->rng));
            station = loiter.anchor + (UnitState::toDirection(loiter.bearing) * radius);
        }

        station.y = getTargetAltitude(sim->terrain, station.x, station.z, *unitInfo.definition);
        flying.targetPosition = station;
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

    namespace
    {
        // The original's work pattern, from its build/repair task update. Every
        // 150 ticks — a global timer, not a per-unit dwell, so every builder
        // on the map moves on the same tick — it takes the bearing from the
        // job to itself, steps it back by a seventh of a turn and puts its
        // goal there, one build-distance out, facing the job. Seven stations,
        // one lap every thirty-five seconds.
        constexpr unsigned int WorkOrbitStepTicks = 150;
        constexpr int WorkOrbitStationCount = 7;
        const SimAngle WorkOrbitStep(static_cast<uint16_t>(65536 / WorkOrbitStationCount));
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

        auto flying = std::get_if<AirMovementStateFlying>(&airPhysics->movementState);
        if (flying == nullptr)
        {
            return true;
        }


        // The original's work pattern. Its build task keeps the aircraft's
        // goal on a circle of one build-distance about the job and, every 150
        // ticks, steps that goal a seventh of a turn round the circle and
        // points the nose back at the job. The aircraft simply flies to
        // wherever its goal currently is, so the movement is a dash, a wait,
        // and another dash — seven stations, one lap every thirty-five
        // seconds. The timer is global rather than per unit, so every builder
        // on the map shifts on the same tick, as in the original.
        auto& orbitOpt = unitInfo.state->airWorkOrbit;
        if (!orbitOpt || orbitOpt->workPosition.distanceSquared(workPosition) > (48_ss * 48_ss))
        {
            orbitOpt = UnitState::AirWorkOrbitState{workPosition, SimAngle(0), false};
        }
        auto& orbit = *orbitOpt;
        // Follow a target that drifts (a unit under repair edging about).
        orbit.workPosition = workPosition;

        // One build-distance out, flat, exactly as the original: not scaled
        // by the size of what is being worked on. The floor only guards a
        // definition that gives no build distance at all.
        auto radius = rweMax(16_ss, unitInfo.definition->buildDistance);

        if (!orbit.started || (sim->gameTime.value % WorkOrbitStepTicks) == 0)
        {
            if (!orbit.started)
            {
                // Take up station from wherever it happens to be, so it does
                // not fly across the job to reach an arbitrary starting point.
                SimVector fromJob(unitInfo.state->position.x - workPosition.x, 0_ss, unitInfo.state->position.z - workPosition.z);
                orbit.bearing = fromJob.lengthSquared() > 0_ss
                    ? UnitState::toRotation(fromJob)
                    : unitInfo.state->rotation;
                orbit.started = true;
            }
            else
            {
                orbit.bearing = orbit.bearing - WorkOrbitStep;
            }
        }

        auto station = workPosition + (UnitState::toDirection(orbit.bearing) * radius);
        station.y = getTargetAltitude(sim->terrain, station.x, station.z, *unitInfo.definition);
        flying->targetPosition = station;

        // The nose stays on the job, turned at the aircraft's own rate. For a
        // construction aircraft that is slow enough that the fifty-one degree
        // step between stations takes most of the five seconds to come round.
        unitInfo.state->slowFacePoint = workPosition;

        // It lathes the whole time, dashing or not, exactly as the original
        // does — its build task never stops to wait for the aircraft to
        // arrive anywhere.
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
        return sim->weaponCanHitUnit(weaponDefinition, target);
    }

    std::optional<UnitId> UnitBehaviorService::chooseTarget(UnitId id, unsigned int weaponIndex)
    {
        const auto& unit = sim->getUnitState(id);
        const auto& unitDefinition = sim->unitDefinitions.at(unit.unitType);
        const auto& weaponDefinition = sim->weaponDefinitions.at(unit.weapons[weaponIndex]->weaponType);
        const auto& badCategory = unitDefinition.badTargetCategory.at(weaponIndex);

        // The original scores each candidate with a random number drawn
        // between zero and the square of the distance to it, and takes the
        // lowest score. Near things therefore win most of the time without
        // winning every time, which is what stops a dozen units in a group
        // all firing at the same unfortunate Peewee. A weapon's bad target
        // category does not rule a candidate out, it only puts it behind
        // everything else, so a Samson with no aircraft to shoot at will
        // still shoot at tanks.
        std::optional<UnitId> best;
        std::optional<UnitId> bestBad;
        auto bestScore = std::numeric_limits<int>::max();
        auto bestBadScore = std::numeric_limits<int>::max();

        // The original exempts the computer players from the ShootMe rule,
        // so an AI's units do go after the buildings a human player's units
        // would walk past.
        auto ignoresShootMe = sim->getPlayer(unit.owner).type == GamePlayerType::Computer;

        for (const auto& entry : sim->units)
        {
            auto otherUnitId = entry.first;
            const auto& otherUnit = entry.second;

            if (otherUnit.isDead() || otherUnit.isOwnedBy(unit.owner))
            {
                continue;
            }

            const auto& otherUnitDefinition = sim->unitDefinitions.at(otherUnit.unitType);

            // Buildings that do not ask to be shot at are left alone unless
            // somebody orders otherwise, which is why nothing in the original
            // wanders over to a metal extractor and opens up on it.
            if (!otherUnitDefinition.shootMe && !ignoresShootMe)
            {
                continue;
            }

            auto distanceSquared = unit.position.distanceSquared(otherUnit.position);
            if (distanceSquared > weaponDefinition.maxRange * weaponDefinition.maxRange)
            {
                continue;
            }

            // Only what the owner can see or has on radar is fair game,
            // and a torpedo cannot reach something standing on land.
            if (!sim->canDetectUnit(unit.owner, otherUnitId) || !weaponCanHitUnit(weaponDefinition, otherUnit))
            {
                continue;
            }

            // The draw is over whole world units squared, and the original's
            // random number generator returns zero for anything under two.
            auto range = static_cast<int>(simScalarToUInt(distanceSquared));
            auto score = 0;
            if (range > 1)
            {
                std::uniform_int_distribution dist(0, range - 1);
                score = dist(sim->rng);
            }

            if (categoryListContains(otherUnitDefinition.category, badCategory))
            {
                if (score < bestBadScore)
                {
                    bestBadScore = score;
                    bestBad = otherUnitId;
                }
            }
            else if (score < bestScore)
            {
                bestScore = score;
                best = otherUnitId;
            }
        }

        return best ? best : bestBad;
    }

    bool UnitBehaviorService::attackTarget(UnitInfo unitInfo, const AttackTarget& target)
    {
        // A crawling bomb has no weapon to aim, so this has to come first. The
        // original turns an attack order on one of these into its own mission,
        // ATTACK_KAMIKAZE (0x43F38A), whose handler at 0x403336 walks the unit
        // to within kamikazedistance of the target and, on arrival, issues the
        // ordinary SELFDESTRUCT order (0x4032E4).
        if (unitInfo.definition->kamikaze)
        {
            return kamikazeRun(unitInfo, target);
        }

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

    void UnitBehaviorService::updateMoveRateBand(UnitInfo unitInfo)
    {
        // The original's 0x43DA70. A moving unit sits in one of three speed
        // bands and a stopped unit in band zero; the band is remembered and a
        // script is run only when it changes. StopMoving fires on the way into
        // zero, StartMoving on the way out of it, and then whichever of
        // MoveRate1/2/3 the new band names.
        //
        // Almost nothing in the shipped data names a threshold, and both
        // default to twice the unit's top speed, so in practice a moving unit
        // is in band one and it is MoveRate1 that fires. That is what the
        // Atlas hangs its thruster flames off: it has no StartMoving at all.
        auto speed = unitInfo.state->previousPosition.distance(unitInfo.state->position);

        // The original tests its movement state's speed against zero; RWE reads
        // the speed back off the step just taken, so it wants a little slack
        // for fixed-point drift on a unit that is standing still.
        unsigned int band = 0;
        if (speed >= 0.1_ssf)
        {
            band = speed <= unitInfo.definition->moveRate1
                ? 1
                : (speed <= unitInfo.definition->moveRate2 ? 2 : 3);
        }

        auto previousBand = unitInfo.state->moveRateBand;
        if (band == previousBand)
        {
            return;
        }
        unitInfo.state->moveRateBand = band;

        if (band == 0)
        {
            unitInfo.state->cobEnvironment->createThread("StopMoving");
            return;
        }

        if (previousBand == 0)
        {
            unitInfo.state->cobEnvironment->createThread("StartMoving");
        }

        // A script that does not define the function ignores this.
        unitInfo.state->cobEnvironment->createThread("MoveRate" + std::to_string(band));
    }

    bool UnitBehaviorService::kamikazeRun(UnitInfo unitInfo, const AttackTarget& target)
    {
        auto targetPosition = getTargetPosition(target);
        if (!targetPosition)
        {
            // Whatever it was chasing has gone; there is nothing left to die on.
            return true;
        }

        // 0x403364: the trigger radius is the definition's kamikazedistance with
        // a floor of sixteen world units, so a unit that names none still goes
        // off when it arrives rather than grinding into the target forever. The
        // Roach uses 40 and the Invader 80.
        auto triggerDistance = SimScalar(static_cast<float>(std::max(unitInfo.definition->kamikazeDistance, 16u)));

        if (unitInfo.state->position.distanceSquared(*targetPosition) > triggerDistance * triggerDistance)
        {
            navigateTo(unitInfo, attackTargetToNavigationGoal(target));
            return false;
        }

        // Arrived. The original hands off to SELFDESTRUCT, so the blast is the
        // unit's SelfDestructAs -- CRAWL_BLAST rather than the smaller
        // CRAWL_BLASTSML it leaves behind when something else kills it.
        sim->selfDestructUnit(unitInfo.id);
        return true;
    }

    bool UnitBehaviorService::attackTargetAir(UnitInfo unitInfo, const AttackTarget& target)
    {
        // Resolve the target position. If the target has gone away, drop the order.
        auto targetPosition = getTargetPosition(target);
        if (!targetPosition)
        {
            unitInfo.state->clearWeaponTargets();

            // The target is gone. The original does not let the aircraft stop
            // here: AirStrike's prologue appends a VTOL_SeekAttack carrying the
            // aircraft's own position (0x411FF5-0x412043) and deletes itself,
            // and that mission circles the spot indefinitely. Arm the circuit
            // now, on the tick the target dies, so that it survives into the
            // idle path from the next tick; the aircraft may well still be in
            // the middle of a run, and the circuit takes over from any state.
            beginAirLoiter(unitInfo, UnitState::AirLoiterState::Reason::AttackEnded, unitInfo.state->position);

            // Drop the run itself here as well. Whatever picks the aircraft up
            // next — the circuit, or another order behind this one — expects to
            // be steering an aircraft in level flight, and neither the run nor
            // the ring gives up its steering to anyone else.
            if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics))
            {
                if (!std::holds_alternative<AirMovementStateFlying>(airPhysics->movementState)
                    && !std::holds_alternative<AirMovementStateTakingOff>(airPhysics->movementState)
                    && !std::holds_alternative<AirMovementStateLanding>(airPhysics->movementState))
                {
                    AirMovementStateFlying flying;
                    flying.currentVelocity = airVelocity(airPhysics->movementState);
                    airPhysics->movementState = flying;
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

        // Gunships get their own behaviour, but only against a unit they can
        // actually work around. The original gates it the same way: a Brawler
        // sent at bare ground, or carrying a dropped weapon, flies the ordinary
        // pattern instead.
        if (unitInfo.definition->hoverAttack
            && std::holds_alternative<UnitId>(target)
            && !std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType))
        {
            return hoverAttackTarget(unitInfo, target, *targetPosition, weaponDefinition.maxRange);
        }

        if (std::holds_alternative<AirMovementStateHoverAttack>(airPhysics->movementState))
        {
            // Was working a ring, but this target does not qualify for one.
            AirMovementStateFlying flying;
            flying.currentVelocity = std::get<AirMovementStateHoverAttack>(airPhysics->movementState).currentVelocity;
            airPhysics->movementState = flying;
        }

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

    bool UnitBehaviorService::hoverAttackTarget(UnitInfo unitInfo, const AttackTarget& target, const SimVector& targetPosition, SimScalar weaponMaxRange)
    {
        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        auto radius = hoverAttackRingRadius(weaponMaxRange);

        // Take over from whatever it was doing, and start again from scratch
        // if it has been given a different target: the ring is centred on the
        // thing being shot at, so one built around somewhere else is no use.
        //
        // This has to accept an attack run as well as level flight. A gunship
        // that was working a patch of ground — an attack-move, or an order
        // given on a spot rather than on a unit — is in a run, and taking over
        // only from level flight left it flying passes at a unit indefinitely.
        // That is the old behaviour turning up again at random, which is
        // exactly what it looked like from the ground.
        auto existing = std::get_if<AirMovementStateHoverAttack>(&airPhysics->movementState);
        if (existing == nullptr || existing->target != target)
        {
            AirMovementStateHoverAttack hover(target);
            hover.currentVelocity = airVelocity(airPhysics->movementState);
            hover.phase = AirMovementStateHoverAttack::Phase::Closing;

            // Close on a point half way in, thrown up to 45 degrees off the
            // straight line. A flight ordered onto the same target therefore
            // fans out on the way in instead of arriving in single file.
            SimVector toTarget(targetPosition.x - unitInfo.state->position.x, 0_ss, targetPosition.z - unitInfo.state->position.z);
            auto half = toTarget.length() / 2_ss;
            auto heading = toTarget.lengthSquared() > 0_ss ? UnitState::toRotation(toTarget) : unitInfo.state->rotation;
            std::uniform_int_distribution<unsigned int> spread(0, QuarterTurn.value);
            auto scatter = SimAngle(spread(sim->rng)) - SimAngle(QuarterTurn.value / 2);
            auto direction = UnitState::toDirection(heading + scatter);
            auto altitude = getTargetAltitude(sim->terrain, unitInfo.state->position.x, unitInfo.state->position.z, *unitInfo.definition);
            hover.station = SimVector(
                unitInfo.state->position.x + (direction.x * half),
                altitude,
                unitInfo.state->position.z + (direction.z * half));
            airPhysics->movementState = hover;
        }

        auto hover = std::get_if<AirMovementStateHoverAttack>(&airPhysics->movementState);
        if (hover == nullptr)
        {
            return false;
        }
        hover->target = target;
        hover->targetPosition = targetPosition;

        SimVector toStation(hover->station.x - unitInfo.state->position.x, 0_ss, hover->station.z - unitInfo.state->position.z);
        SimVector toTarget(targetPosition.x - unitInfo.state->position.x, 0_ss, targetPosition.z - unitInfo.state->position.z);

        if (hover->phase == AirMovementStateHoverAttack::Phase::Closing)
        {
            // The approach ends as soon as the target is within reach, whether
            // or not the half-way point was ever made: the point of it was to
            // get into range, and it is in range now.
            auto closeEnough = toTarget.lengthSquared() <= weaponMaxRange * weaponMaxRange;
            if (closeEnough || toStation.lengthSquared() <= HoverAttackArrivalTolerance * HoverAttackArrivalTolerance)
            {
                hover->phase = AirMovementStateHoverAttack::Phase::Swinging;
                hover->swingPositive = false;
                hover->outOfRangeArrivals = 0;
                hover->station = nextHoverAttackStation(unitInfo, *hover, targetPosition, radius, weaponMaxRange);
            }
        }
        else if (toStation.lengthSquared() <= HoverAttackArrivalTolerance * HoverAttackArrivalTolerance)
        {
            // On station. Pick the next one, 45 degrees round, the other way
            // from last time.
            hover->station = nextHoverAttackStation(unitInfo, *hover, targetPosition, radius, weaponMaxRange);
        }

        // The gun takes the target once and holds it: the gunship keeps
        // shooting all the way across the swing, not only while pointed at it.
        if (hover->phase == AirMovementStateHoverAttack::Phase::Swinging)
        {
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

    SimVector UnitBehaviorService::nextHoverAttackStation(UnitInfo unitInfo, AirMovementStateHoverAttack& hover, const SimVector& targetPosition, SimScalar radius, SimScalar weaponMaxRange)
    {
        const auto& unitPosition = unitInfo.state->position;

        SimVector fromTarget(unitPosition.x - targetPosition.x, 0_ss, unitPosition.z - targetPosition.z);
        if (fromTarget.lengthSquared() <= weaponMaxRange * weaponMaxRange)
        {
            hover.outOfRangeArrivals = 0;
        }
        else
        {
            ++hover.outOfRangeArrivals;
        }

        auto stationAltitude = [&](const SimVector& p) {
            return getTargetAltitude(sim->terrain, p.x, p.z, *unitInfo.definition);
        };

        if (hover.outOfRangeArrivals >= 2)
        {
            // Two arrivals running without a shot: something is in the way, or
            // the target has moved off. Stop working round from here and take
            // a fresh bearing at full range instead.
            hover.outOfRangeArrivals = 0;
            std::uniform_int_distribution<unsigned int> anywhere(0, 0xffffu);
            auto station = hoverAttackStation(targetPosition, SimAngle(anywhere(sim->rng)), weaponMaxRange);
            station.y = stationAltitude(station);
            return station;
        }

        // A quarter of a quarter turn is 45 degrees, and the side alternates,
        // so it shuttles between two points on the ring rather than orbiting.
        auto bearing = hoverAttackBearing(unitPosition, targetPosition);
        auto step = SimAngle(QuarterTurn.value / 2u);
        bearing = hover.swingPositive ? bearing + step : bearing - step;
        hover.swingPositive = !hover.swingPositive;

        auto station = hoverAttackStation(targetPosition, bearing, radius);
        station.y = stationAltitude(station);
        return station;
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

        // Nothing to help with. An aircraft does not park over what it is
        // guarding: a guard order on something that can fly becomes
        // VTOL_Follow (0x40FBE0, chosen at 0x43F4C7), and with no work to copy
        // from the guarded unit that mission spends its time putting its goal
        // on a ring around it and stepping the bearing round on arrival
        // (0x41013B). The ring is the guard's own weapon range plus 160, or
        // 320 for something with no weapon at all — so a construction aircraft
        // guarding an idle factory mills about the neighbourhood rather than
        // hanging motionless beside it, which is what was reported.
        if (unitInfo.definition->canFly)
        {
            if (auto groundPhysics = std::get_if<UnitPhysicsInfoGround>(&unitInfo.state->physics))
            {
                // VTOL_Follow's state 0 puts the aircraft in the air before it
                // does anything else (0x4102BE).
                groundPhysics->steeringInfo.shouldTakeOff = true;
                return false;
            }

            flyAirLoiterCircuit(unitInfo, UnitState::AirLoiterState::Reason::Guarding, targetUnit.position, AirLoiterGuardStep);
            return false;
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

            // NoChaseCategory is about leaving your post, not about what you
            // may shoot: forty-four of the shipped units name VTOL there, and
            // they carry on with what they were doing rather than stopping
            // for an aircraft. Fire at will still shoots at it in passing.
            const auto& otherDefinition = sim->unitDefinitions.at(other.unitType);
            if (categoryListContains(otherDefinition.category, unitInfo.definition->noChaseCategory))
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

        if (!prepareBuilderForWork(unitInfo, targetUnit.position))
        {
            return false;
        }

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
            },
            [&](const AirMovementStateHoverAttack&) {
                // Likewise on station: the gunship handler decides where to fly.
            });

        return false;
    }
}
