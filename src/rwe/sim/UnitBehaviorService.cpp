#include "UnitBehaviorService.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <rwe/cob/CobExecutionContext.h>
#include <rwe/cob/cob_util.h>
#include <rwe/sim/AirMovement.h>
#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/FlatRange.h>
#include <rwe/sim/MissionScripts.h>
#include <rwe/sim/ProjectileSpawn.h>
#include <rwe/sim/RepairPatrol.h>
#include <rwe/sim/SimRandom.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/TaLiveSenderHooks.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitModelVertices.h>
#include <rwe/sim/cob.h>
#include <rwe/sim/movement.h>
#include <rwe/sim/sim_prof.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/match.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    namespace
    {
        /**
         * How long a builder holds its nanolathe arm out after a job ends,
         * waiting to see whether another one turns up. Half a second: long
         * enough for the next order in the queue, or the next wreck on a
         * reclaiming patrol, and short enough that a builder walking away
         * stows on the way rather than carrying the arm across the map.
         */
        constexpr unsigned int ArmStowGraceTicks = 15;

        /**
         * How near its heading has to be to the job before a builder starts
         * work. A sixteenth of a turn: close enough that the arm comes out
         * pointing at what it is lathing, loose enough that a slow turner is
         * not held up over the last degree of it.
         */
        constexpr SimAngle WorkFacingTolerance = SimAngle(1u << 12u);

        /**
         * How square on to the job a ground builder has to be before it puts
         * its arm out. StartBuilding is handed the heading from the builder's
         * nozzle to the job as the builder stands when it is sent, and the
         * script turns the torso to that and holds it. The body is still
         * coming round to the job after that (it is only held to
         * WorkFacingTolerance, a sixteenth of a turn, to start), so the torso
         * was left turned by whatever the body had still to go: a commander
         * built with its upper half twisted off the building and the spray
         * leaving the arm at a slant. Wait the few ticks it takes to come
         * square instead; the facing is asserted every tick, so it gets
         * there.
         */
        constexpr SimAngle WorkDeployFacingTolerance = SimAngle(1u << 8u);

        bool builderIsSquareOnTo(const UnitState& builder, const UnitDefinition& definition, const SimVector& work)
        {
            if (definition.canFly || definition.turnRate <= 0_ss)
            {
                return true;
            }
            auto direction = work - builder.position;
            direction.y = 0_ss;
            if (direction.lengthSquared() == 0_ss)
            {
                return true;
            }
            return angleBetweenIsLessOrEqual(builder.rotation, UnitState::toRotation(direction), WorkDeployFacingTolerance);
        }

        /**
         * How near a gunship has to get to its station before it counts as
         * arrived and picks the next one. The original uses sixteen units,
         * which is close enough that it really does fly to each point.
         */
        const SimScalar HoverAttackArrivalTolerance = 16_ss;

        /**
         * How far inside its own weapon's maximum range an attacker's
         * approach point sits (see attackApproachGoal). A margin rather than
         * the exact edge, so the point does not land right on the range
         * boundary and have the unit flicker between navigating and firing
         * as fixed-point rounding nudges it a unit either side of maxRange.
         */
        const SimScalar AttackApproachRangeMargin = 8_ss;

        /**
         * How far outside a target's own footprint an attacker's approach
         * point sits, for a weapon whose range is too short to clear the
         * footprint on its own -- a short-ranged unit against a Big Bertha,
         * say. Without a floor like this the approach point would fall
         * inside the target and the attacker would try to walk into it.
         */
        const SimScalar AttackApproachFootprintMargin = 8_ss;

        /**
         * How much a target may drift before an attacker's cached approach
         * point (see attackApproachGoal) is treated as stale and recomputed.
         * The same eight units hasReachedGoal accepts as "arrived", chosen
         * here for the same reason: comfortably past the float rounding a
         * normalise-and-multiply round trip leaves behind, without being so
         * loose that a target which has genuinely moved gets ignored.
         */
        const SimScalar AttackApproachDriftTolerance = 8_ss;

        /**
         * The original's per-unit repath rate limit, in ticks: a navigator is
         * polled for "wants a path" at most once every 60 (`WantsPath`,
         * 0x44F260; TOTALA-EXE-MOVEMENT.md S:87). RWE had no limit, and a
         * unit chasing a moving target re-asked every few ticks -- measured on
         * Crystal Maze at a median of 30 ticks between searches for one unit,
         * three quarters of them under 60. Each re-ask threw away the search
         * already in flight, and a thrown-away search is a whole component's
         * worth of expansions that bought nothing. See groundUnitMoveTo.
         */
        constexpr unsigned int PathRequestCooldownTicks = 60;

        /**
         * How close two resolved destinations have to be before a changed goal
         * is treated as the same goal drifting -- an attacker's stand-off
         * point following its target -- rather than a new order. A unit that
         * already has a route keeps walking it while it waits out the rate
         * limit; a goal genuinely somewhere else gets the stand-in at once, so
         * a new order is never ignored.
         */
        const SimScalar PathGoalRetargetTolerance = 64_ss;

        /** Whether two resolved destinations name the same place, near enough. */
        bool destinationsAreClose(const PathDestination& a, const PathDestination& b)
        {
            const auto* va = std::get_if<SimVector>(&a);
            const auto* vb = std::get_if<SimVector>(&b);
            if (va == nullptr || vb == nullptr)
            {
                // Anything that is not two points -- a footprint goal, or a
                // mix of kinds -- is a new order and never a drift.
                return false;
            }
            auto dx = va->x - vb->x;
            auto dz = va->z - vb->z;
            return (dx * dx) + (dz * dz) <= (PathGoalRetargetTolerance * PathGoalRetargetTolerance);
        }

        SimVector airVelocity(const AirMovementState& state)
        {
            return AirMovement::airVelocity(state);
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

        // Tell the script how long the unit takes to reload, which is what
        // decides how long it holds an aiming pose. The original works this
        // out while it is initialising the three weapon slots: 0x49E070 keeps
        // the largest `reloadtime` it sees (`wdef+0xE4`, a word already
        // multiplied by 30, so a tick count), and 0x49E14B-0x49E17A turns it
        // into milliseconds -- `ticks * 1000`, then the magic-number divide by
        // 30 -- before starting "SetMaxReloadTime" with it through
        // StartScriptByName at 0x49E186. That call site is the only reference
        // to the string anywhere in the image, and it is unconditional, so a
        // unit with no weapons gets a zero rather than nothing.
        //
        // Forty-two of the 157 shipped scripts answer it, and they all do the
        // same thing with the number: `restore_delay = time * n`, the sleep at
        // the top of RestoreAfterDelay before the unit stows whatever aiming
        // opened up. A script that is never told keeps the 3000 its Create()
        // sets, and three seconds is shorter than most weapons take to
        // reload -- CORMSHIP's rockets are 9 seconds, ARMMSHIP's 12, and both
        // double it -- so a missile ship was shutting its hatches between
        // shots instead of holding them open across a reload.
        auto maxReloadTicks = 0;
        for (const auto& weapon : unit.weapons)
        {
            if (!weapon)
            {
                continue;
            }
            const auto& weaponDefinition = sim->weaponDefinitions.at(weapon->weaponType);
            maxReloadTicks = std::max(maxReloadTicks, static_cast<int>(deltaSecondsToTicks(weaponDefinition.reloadTime).value));
        }
        unit.cobEnvironment->createThread("SetMaxReloadTime", {(maxReloadTicks * 1000) / SimTicksPerSecond});

        if (unitDefinition.extractsMetal != Metal(0))
        {
            auto footprint = sim->computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            // BT-3: N = sum(metal byte + 1) over the in-map cells, handed over
            // as a signed 16-bit value (0x437840).
            auto work = sim->extractorMetalWork(footprint);
            unit.cobEnvironment->createThread("SetSpeed", {static_cast<int>(static_cast<std::int16_t>(work))});
        }

        runUnitCobScripts(*sim, unitId);

        for (int i = 0; i < getSize(unit.weapons); ++i)
        {
            auto& weapon = unit.weapons[i];
            if (!weapon)
            {
                continue;
            }
            auto localAimingPoint = getLocalAimingPoint(unitId, i);
            auto localFiringPoint = getLocalFiringPoint(unitId, i);
            // 0x49E070 (0x49E0FB-0x49E11B): the Z difference of the Query piece
            // over the AimFrom piece, times the double 1.25 at 0x4FDA98,
            // truncated, kept in 16.16 and never rewritten. Taken in the
            // unit's own frame, which is the frame the original sees when the
            // unit is built facing 0 -- the heading it has at that moment is
            // not established for a factory's product.
            weapon->ballisticZOffset = muzzleCorrectionOf(localFiringPoint.z - localAimingPoint.z);
        }
    }

    void UnitBehaviorService::updateWind(int windSpeed, SimAngle windDirection)
    {
        // The integer speed the map's range drew, shifted left four bits
        // (0x43797B). The shipped wind scripts spin their blades at this
        // value in 1/65536-turn units a second.
        int cobWindSpeed = windSpeed * 16;
        int cobWindDirection = toCobAngle(windDirection).value;

        for (auto& [id, unit] : sim->units)
        {
            const auto& unitDefinition = sim->unitDefinitions.at(unit.unitType);
            if (unitDefinition.windGenerator > Energy(0))
            {
                unit.cobEnvironment->createThread("SetSpeed", {cobWindSpeed});
                unit.cobEnvironment->createThread("SetDirection", {cobWindDirection});
            }
        }
    }

    void UnitBehaviorService::update(UnitId unitId)
    {
        auto unitInfo = sim->getUnitInfo(unitId);

        // A unit killed earlier in this pass is still in the map until the
        // end of the tick, but it is out of play: the original's death
        // handler takes it off the board at once. Its orders must not run --
        // a Roach killed by its neighbour's blast went on to detonate as well.
        if (unitInfo.state->isDead())
        {
            return;
        }

        // A unit in a transport's grip just rides along (see updateCarriedUnits).
        if (unitInfo.state->carriedBy)
        {
            return;
        }

        // A Remote player's unit makes no decision here: its owner does, and
        // this machine applies the results.
        if (!simulatesLocally(*sim, unitInfo.state->owner))
        {
            updateRemote(unitInfo);
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
                    },
                    [&](const AirMovementStateDogfight&) {
                        // And the same for a fighter: dogfightTarget owns
                        // every transition in it.
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

        // A stow put off when the last job ended: if nothing has taken its
        // place by now, the arm goes away.
        updatePendingArmStow(unitInfo);

        if (!paralyzed && !unitInfo.state->isBeingBuilt(*unitInfo.definition))
        {
            if (!unitInfo.state->buildQueue.empty())
            {
                auto& entry = unitInfo.state->buildQueue.front();
                if (handleBuild(unitInfo, entry.first))
                {
                    if (entry.second > 1)
                    {
                        --entry.second;
                    }
                    else if (unitInfo.state->repeatBuildQueue)
                    {
                        // The opt-in factory repeat (#417). The entry that has
                        // just been worked off goes to the back of the
                        // factory's own queue instead of being consumed, so
                        // one queued type loops for ever and a queue of
                        // several keeps its order -- each finished entry
                        // rejoins behind whatever is still waiting, so a
                        // factory works its list round and round rather than
                        // restarting it.
                        //
                        // The type is copied out first: pop_front invalidates
                        // the reference this branch is holding, which is the
                        // kind of use-after-free that reads as a corrupted
                        // queue rather than as a crash.
                        auto finishedType = entry.first;
                        unitInfo.state->buildQueue.pop_front();
                        unitInfo.state->buildQueue.emplace_back(finishedType, 1);
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

            // An air repair pad mends whatever is sitting on it. The pad is
            // a builder, and this is the work it does when it has nothing
            // else, so a damaged aircraft that flies to a pad is actually
            // repaired rather than merely parked. Ordered work comes first: a
            // pad told to assist a factory does that instead.
            auto padIsMending = false;
            if (unitInfo.state->orders.empty() && unitIsAnUsableAirBase(*unitInfo.state, *unitInfo.definition))
            {
                if (auto patient = findAircraftToRepairOnPad(*sim, unitInfo))
                {
                    padIsMending = true;
                    // Straight to the arm, not through repairExistingUnit:
                    // the reach has already been decided by the search, which
                    // uses the larger of BuildDistance and half the pad's own
                    // footprint. repairExistingUnit re-tests with BuildDistance
                    // alone -- ARMASP's is 6, against a four-by-four footprint
                    // 64 world units across -- so it refused every patient its
                    // own search had just found and sent an immobile pad off to
                    // "navigate" towards an aircraft parked in its middle. The
                    // pads have never mended anything, and the end-to-end test
                    // that would have shown it did not exist until now.
                    deployRepairArm(unitInfo, *patient);
                }
            }

            // A damaged aircraft breaks off what it is doing and goes home.
            maybeBreakOffToRepairPad(unitInfo);

            if (!unitInfo.state->orders.empty())
            {
                // An order of its own takes the weapons off the guard mission.
                if (unitInfo.state->guardPhase != 0 || unitInfo.state->guardTarget)
                {
                    resetGuardNoMove(*unitInfo.state);
                }
                RWE_SIMPROF("b.orders");
                // Non-const: a capture order counts its own progress, so it
                // is written back through this reference. Taking a
                // reference to front() and holding it across the call is
                // safe -- the handlers that touch the deque only push at
                // either end, and a deque insertion does not invalidate
                // references to elements already in it.
                auto& order = unitInfo.state->orders.front();

                if (handleOrder(unitInfo, order))
                {
                    unitInfo.state->orders.pop_front();
                    unitInfo.state->buildOrderUnitId = std::nullopt;
                }
            }
            else if (std::holds_alternative<UnitPhysicsInfoAir>(unitInfo.state->physics) && sim->missionScripts && sim->missionScripts->isRunning(unitId))
            {
                // A mission aircraft whose list is on a wait, a hunt's pause
                // or any other step of its own has an empty queue and is still
                // not idle: the original's head mission is the WAIT, and only
                // an empty list or a Standby looks for a fight (0x407069). It
                // does not go after what it sees, or off to land; its weapons
                // still answer from where it is. (A ground unit's idle branch
                // only resets its state, which is right for it either way.)
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
                //
                // Not on Hold Position, though: every one of 0x43B1F0's nine
                // callers, Standby included, gets nothing back for a unit
                // whose movement mode is 0 (0x43B211), so such a unit shoots
                // from where it is and never goes after anything (issue #109).
                //
                // And on Fire At Will exactly (BT-74): 0x43B700, which every
                // standby, patrol and seek-attack mission calls first, is
                // `cmp ecx,0x200000 / jne`, so a unit on Return Fire answers
                // being shot (returnFire, from the damage handler) but never
                // goes hunting for something it merely saw.
                std::optional<UnitId> freeTarget;
                if (unitInfo.definition->canAttack && unitInfo.state->fireOrders == UnitFireOrders::FireAtWill
                    && unitInfo.state->moveOrders != UnitMovementOrders::HoldPosition)
                {
                    freeTarget = findEnemyToEngage(unitInfo);
                    // Only go after something the owner can actually see. The
                    // search itself does not check, because a unit already on
                    // patrol breaks off on contact and has been relied on to
                    // do that from its first tick, before visibility for the
                    // tick has been worked out.
                    if (freeTarget && !sim->canSeeUnit(unitInfo.state->owner, *freeTarget))
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
                    //
                    // BT-79: the unit is idle, so what the original has at
                    // the head is its default-mission node, marked 0x4000,
                    // and the sighting routine copies that mark onto what it
                    // inserts in front of it. The nodes are automatic, and a
                    // Shift-queued order drops them. (A unit with no default
                    // mission has no such head, and its nodes stay.)
                    const bool automatic = unitInfo.definition->defaultMission != DefaultMission::None;
                    auto attack = createAttackOrder(*freeTarget);
                    std::get<AttackOrder>(attack).automatic = automatic;
                    unitInfo.state->orders.push_back(std::move(attack));
                    if (unitInfo.state->moveOrders == UnitMovementOrders::Maneuver)
                    {
                        // Maneuver comes back to where it was when it saw
                        // the target (0x43B25D-0x43B2B1): the same return
                        // move the patrol break-off plants, behind the attack.
                        MoveOrder back(unitInfo.state->position);
                        back.automatic = automatic;
                        unitInfo.state->orders.push_back(std::move(back));
                    }
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

                    // BOUNDARY-004: VTOL_SeekAttack is one of the six
                    // handlers with the off-grid recovery, and its earlier
                    // termination is the path-failure event. Off the grid the
                    // circuit is replaced by the step toward the centre; the
                    // circuit itself is kept and resumes on re-entry.
                    if (airIsOutsideGrid(unitInfo))
                    {
                        steerAirBackInside(unitInfo);
                    }
                    else
                    {
                        flyAirLoiterCircuit(unitInfo, UnitState::AirLoiterState::Reason::AttackEnded, anchor, AirMovement::LoiterSeekStep);
                    }
                }
                else if (unitInfo.definition->canFly
                    && !unitInfo.state->carriedUnits.empty()
                    && std::holds_alternative<AirMovementStateFlying>(airPhysics->movementState))
                {
                    // Airborne and carrying: VTOL_Standby's state 2 takes its
                    // hop branch and never lands (0x40F823, S:128). Only when
                    // empty does it push VTOL_LandIfCan, which is the branch
                    // below.
                    flyIdleHop(unitInfo);
                }
                else
                {
                    // Anything else that has simply run out of orders does go
                    // home: the original installs the definition's
                    // DefaultMissionType, which for all twenty-one shipped
                    // aircraft is VTOL_Standby (0x40F7D0), and that hops about
                    // only while the aircraft is carrying something. Empty, it
                    // pushes VTOL_LandIfCan and sets down. A LandingHold
                    // circuit is kept across ticks: it is the answer for an
                    // aircraft with nowhere to land, and its anchor must not
                    // move with the aircraft.
                    if (!unitInfo.state->airLoiter
                        || unitInfo.state->airLoiter->reason != UnitState::AirLoiterState::Reason::LandingHold)
                    {
                        unitInfo.state->airLoiter = std::nullopt;
                    }
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
                        [&](const AirMovementStateDogfight&) {
                            // A fighter whose bandit is gone has nothing to
                            // pursue; level out and let the idle path decide.
                            airPhysics->movementState = AirMovementStateFlying();
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
            else if (!padIsMending)
            {
                // ...and a pad in the middle of mending something is not
                // idle. This reset runs after the repair block above, so
                // without the guard it wiped the building state the pad had
                // just entered -- and told the script to stow the arm -- every
                // single tick, which is the other half of why the pads never
                // actually mended anything.
                changeState(*unitInfo.state, UnitBehaviorStateIdle());

                // The one DefaultMissionType RWE's idle behaviour did not
                // already amount to. The rest are what an idle unit here
                // does anyway for the units that name them: VTOL_Standby is
                // the aircraft branch above, Guard_NoMove and no mission at
                // all leave a building's weapons to find their own targets,
                // and Standby's search is what the weapons stand in for.
                if (unitInfo.definition->defaultMission == DefaultMission::StandbyMine)
                {
                    pollMine(unitInfo);
                }
                else if (unitInfo.definition->defaultMission == DefaultMission::GuardNoMove)
                {
                    runGuardNoMove(unitInfo);
                }
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

            {
                RWE_SIMPROF("b.weapons");
                for (Index i = 0; i < getSize(unitInfo.state->weapons); ++i)
                {
                    // Nearly every unit in the game leaves two of its three
                    // slots empty, and both of the calls below open by
                    // looking the unit up again only to find nothing there.
                    // Skipping here does what they would have done.
                    if (!unitInfo.state->weapons[i])
                    {
                        continue;
                    }

                    updateWeaponStockpile(unitId, i);
                    updateWeapon(unitId, i);
                }
            }
        }

        if (unitInfo.definition->isMobile)
        {
            RWE_SIMPROF("b.move");
            updateNavigation(unitInfo);

            applyUnitSteering(unitInfo);

            updateUnitPosition(unitInfo);

            updateMoveRateBand(unitInfo);

            // The last step of the mover (0x43DD20): tell the script its water
            // state if it has changed, whether or not the unit moved (BT-11).
            updateSfxOccupancy(*sim, unitInfo);

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
                            if (AirMovement::takeoffReachedCruise(m, unitInfo.state->position.y, targetHeight))
                            {
                                p.movementState = AirMovement::finishTakeoff(m);
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
                                auto landingPoint = airBaseLandingPoint(unitInfo);
                                auto targetHeight = landingPoint
                                    ? landingPoint->second.y
                                    : sim->terrain.getHeightAt(unitInfo.state->position.x, unitInfo.state->position.z);
                                if (unitInfo.state->position.y == targetHeight)
                                {
                                    if (landingPoint)
                                    {
                                        // Exactly on the piece for the last
                                        // step, so the aircraft ends where the
                                        // original's piece goal would put it.
                                        // The descent has already walked it to
                                        // within a unit.
                                        unitInfo.state->position.x = landingPoint->second.x;
                                        unitInfo.state->position.z = landingPoint->second.z;
                                    }

                                    auto pad = landingPoint ? std::optional<UnitId>(landingPoint->first) : std::nullopt;
                                    if (!tryTransitionFromAirToGround(unitInfo, pad))
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
                                //
                                // **Unless there is a pad to come down on.** A
                                // boat carrier is a pad whose ground is under
                                // the waterline for the whole of its life --
                                // ARMCARRY floats on `BOATD6` with
                                // `MinWaterDepth=30` -- so asking about the
                                // terrain refuses every descent onto one, and
                                // an aircraft sent to a carrier flew to within
                                // a world unit of the hull and then hovered
                                // over it for ever without once entering the
                                // landing state.
                                //
                                // The question worth asking is the one the
                                // original asks, and it is not about water at
                                // all: the descent target is the pad's own
                                // landing piece. `VTOL_Landing` calls
                                // `QueryLandingPad` (0x501C14) at 0x411A35,
                                // gets up to four piece ids back, picks a free
                                // one and hands it to the navigator as a piece
                                // goal (0x44E250 at 0x411D60). That piece is
                                // above the waterline whether the pad stands
                                // on land or floats -- `landpad` at 20 on
                                // ARMASP, `landpad1` at 27.05 on ARMCARRY --
                                // so it is what decides where the aircraft
                                // ends up, and the terrain under the pad never
                                // enters into it.
                                //
                                // `airBaseLandingPoint` answers exactly that
                                // question, from the same piece, and it only
                                // answers when the aircraft is already over
                                // the pad -- which is the case here, or it
                                // would not be landing. With no pad underneath
                                // there is nothing to save, and the aircraft
                                // keeps flying over open water as it did
                                // before.
                                auto landingPoint = airBaseLandingPoint(unitInfo);
                                auto ground = sim->terrain.getHeightAt(unitInfo.state->position.x, unitInfo.state->position.z);
                                if (!landingPoint && ground < sim->terrain.getSeaLevel())
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
                        },
                        [&](const AirMovementStateDogfight&) {
                            // Likewise the fighter.
                        });
                });
        }
    }

    bool followPath(const GameSimulation& sim, UnitInfo unitInfo, UnitPhysicsInfoGround& physics, PathFollowingInfo& path);

    void UnitBehaviorService::updateRemote(UnitInfo unitInfo)
    {
        if (!unitInfo.definition->isMobile)
        {
            return;
        }

        // No order, no search, no target choice: the owner's replicated path
        // or goal is the only thing steering this unit, so its results never
        // contend with a decision made here. The steering targets are reset
        // first, so nothing left over from a previous tick pushes it.
        match(
            unitInfo.state->physics,
            [&](UnitPhysicsInfoGround& p) {
                p.steeringInfo = SteeringInfo{unitInfo.state->rotation, 0_ss};
                if (auto* moving = std::get_if<NavigationStateMoving>(&unitInfo.state->navigationState.state))
                {
                    if (moving->path)
                    {
                        followPath(*sim, unitInfo, p, *moving->path);
                    }
                }
                applyUnitSteering(unitInfo);
                updateRemoteGroundPosition(unitInfo, p);
            },
            [&](UnitPhysicsInfoAir& p) {
                match(
                    p.movementState,
                    [&](AirMovementStateFlying& s) {
                        s.targetPosition = std::nullopt;
                        if (unitInfo.state->navigationState.desiredDestination)
                        {
                            if (auto* target = std::get_if<SimVector>(&*unitInfo.state->navigationState.desiredDestination))
                            {
                                s.targetPosition = *target;
                            }
                        }
                    },
                    [&](const AirMovementStateTakingOff&) {},
                    [&](const AirMovementStateLanding&) {},
                    [&](const AirMovementStateAttackRun&) {},
                    [&](const AirMovementStateHoverAttack&) {},
                    [&](const AirMovementStateDogfight&) {});
                applyUnitSteering(unitInfo);
                updateUnitPosition(unitInfo);
            });

        // The owner sends no walk animation: each machine starts it from the
        // unit's own movement, as TA does, so a puppet walks rather than slides.
        updateMoveRateBand(unitInfo);

        // And the same occupancy step runs again for a puppet (BT-11).
        updateSfxOccupancy(*sim, unitInfo);
    }

    void UnitBehaviorService::updateRemoteGroundPosition(UnitInfo unitInfo, UnitPhysicsInfoGround& physics)
    {
        unitInfo.state->previousPosition = unitInfo.state->position;
        unitInfo.state->inCollision = false;

        auto direction = UnitState::toDirection(unitInfo.state->rotation);
        if (physics.currentSpeed > 0_ss)
        {
            auto newPosition = unitInfo.state->position + (direction * physics.currentSpeed);
            newPosition.y = computeGroundUnitHeight(sim->terrain, *unitInfo.definition, newPosition.x, newPosition.z);

            // The owner already resolved unit collisions along this path, so a
            // step is never refused here: a peer's stale footprint must not pin
            // a puppet where its owner had it move. The occupied grid is kept
            // in step all the same, for anything local that still reads it,
            // but a step whose footprint leaves the grid is dropped rather
            // than made to index it.
            auto currentFootprint = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
            auto newFootprint = sim->computeFootprintRegion(newPosition, unitInfo.definition->movementCollisionInfo);
            if (sim->occupiedGrid.tryToRegion(currentFootprint) && sim->occupiedGrid.tryToRegion(newFootprint))
            {
                sim->moveUnitOccupiedArea(currentFootprint, newFootprint, unitInfo.id);
                unitInfo.state->position = newPosition;
            }
        }

        updateGroundTilt(unitInfo, physics);
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
                    // BOUNDARY-004: VTOL_LandIfCan's off-grid recovery comes
                    // before the landing search, and is what brings an idle
                    // or stopped aircraft home rather than leaving it to
                    // hover outside for ever. Landing itself still refuses an
                    // off-grid target, because the placement test behind
                    // findLandingLocation rejects any footprint outside the
                    // full grid. This branch replaces only the movement
                    // objective; the landing search resumes on re-entry.
                    if (unitInfo.definition->canFly && airIsOutsideGrid(unitInfo))
                    {
                        steerAirBackInside(unitInfo);
                        return std::optional<MovingStateGoal>();
                    }

                    // An aircraft with nothing to do lands where it stands,
                    // damaged or not. This used to prefer a repair pad, on the
                    // strength of reading 0x4105B9 as the standby mission
                    // swapping itself for a VTOL_LANDING; 0x4105B9 is inside
                    // VTOL_SeekAttack (0x4103E0), not VTOL_Standby (0x40F7D0),
                    // and neither Standby nor VTOL_LandIfCan carries the health
                    // gate at all. Going home is something an aircraft breaks
                    // off *work* to do -- see maybeBreakOffToRepairPad -- and
                    // an idle one does not do it, which is why the FAQ tells
                    // you to give parked damaged planes a one-point patrol
                    // route to make them go and get mended.
                    auto landingLocation = findLandingLocation(*sim, unitInfo);
                    if (!landingLocation)
                    {
                        // Nowhere within reach to set down: open water, or no
                        // clear ground in the search rings. Leaving the
                        // aircraft with no goal parks it in the air -- its
                        // air controller has nothing to steer at and the
                        // arrival profile decelerates it to a standstill.
                        // Hold a circuit around the spot it went idle on and
                        // keep looking; the search itself is still throttled
                        // inside findLandingLocation.
                        if (unitInfo.definition->canFly)
                        {
                            auto anchor = unitInfo.state->airLoiter
                                    && unitInfo.state->airLoiter->reason == UnitState::AirLoiterState::Reason::LandingHold
                                ? unitInfo.state->airLoiter->anchor
                                : unitInfo.state->position;
                            flyAirLoiterCircuit(
                                unitInfo,
                                UnitState::AirLoiterState::Reason::LandingHold,
                                anchor,
                                AirMovement::LoiterSeekStep);
                        }
                        return std::optional<MovingStateGoal>();
                    }

                    // A spot appeared: the hold is over.
                    if (unitInfo.state->airLoiter
                        && unitInfo.state->airLoiter->reason == UnitState::AirLoiterState::Reason::LandingHold)
                    {
                        unitInfo.state->airLoiter = std::nullopt;
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

    /**
     * The ground path follower, TotalA.exe 0x43CD20, written up as
     * TOTALA-EXE.md section 102. Only ground units come here: the original
     * branches on canfly at 0x43DD3E and so does moveTo.
     *
     * waypoints[currentWaypoint - 1] is the corner the unit has left and
     * waypoints[currentWaypoint] the one it is heading for, which is the
     * navigator's wp[0] and wp[1].
     */
    bool followPath(const GameSimulation& sim, UnitInfo unitInfo, UnitPhysicsInfoGround& physics, PathFollowingInfo& path)
    {
        SimVector xzPosition(unitInfo.state->position.x, 0_ss, unitInfo.state->position.z);

        // Navigator::Update (0x44F1A0) runs before the follower every tick and
        // retires the corner behind the unit as soon as it is close enough to
        // the one ahead. Exactly one goes per tick -- there is no loop there,
        // so however fast a unit is travelling it cannot skip two corners in a
        // tick. The radius is RWE's rather than the original's five; see
        // PathWaypointAdvanceDistance for the measurement that decided it.
        {
            const auto& next = *path.currentWaypoint;
            SimVector xzNext(next.x, 0_ss, next.z);
            auto isFinal = path.currentWaypoint == (path.path.waypoints.end() - 1);
            auto radius = isFinal ? PathFinalWaypointAdvanceDistance : PathWaypointAdvanceDistance;
            auto closeEnough = xzPosition.distanceSquared(xzNext) <= (radius * radius);

            // BT-96: a ground Move to a point ends when the unit stands on the
            // goal's map square, not when it is within eight units of the
            // click. The last corner is the click itself, so a unit that is
            // within eight units of it but on the neighbouring square has not
            // arrived, and retiring the corner here would brake it where it
            // is -- and the repath would then find it "already there" and
            // never move it again. Keep steering at the corner until the unit
            // is on its square; the goal test ends the order on entering it.
            if (closeEnough && isFinal && !unitInfo.definition->canFly)
            {
                if (auto* moving = std::get_if<NavigationStateMoving>(&unitInfo.state->navigationState.state))
                {
                    auto goalPoint = std::get_if<SimVector>(&moving->movementGoal);
                    if (goalPoint != nullptr && goalPoint->x == next.x && goalPoint->z == next.z
                        && !isOnGoalSquare(sim, *unitInfo.state, *unitInfo.definition, *goalPoint))
                    {
                        closeEnough = false;
                    }
                }
            }

            if (closeEnough)
            {
                if (isFinal)
                {
                    // The list has run out. 0x43CD36: a unit with no path
                    // brakes at brakerate and does not turn. Whether the
                    // *order* is finished is the goal test's business, not
                    // the follower's -- see hasReachedGoal.
                    physics.steeringInfo = SteeringInfo{unitInfo.state->rotation, 0_ss};
                    return true;
                }

                ++path.currentWaypoint;
            }
        }

        // GetWaypoints asks for three and clamps the index it reads with
        // min(i, count - 1) (0x44F16A), so on a path of three points or fewer
        // the third repeats the goal -- which is what turns the arrival test
        // into the brake into the destination.
        auto afterNext = path.currentWaypoint + 1;
        if (afterNext == path.path.waypoints.end())
        {
            afterNext = path.currentWaypoint;
        }

        physics.steeringInfo = followSegment(
            *unitInfo.state,
            *unitInfo.definition,
            physics,
            *(path.currentWaypoint - 1),
            *path.currentWaypoint,
            *afterNext);
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
            if (weaponDefinition.interceptor)
            {
                // An interceptor never looks for a unit: the auto-target scan
                // hands it the projectile search instead (0x408B31 calling
                // 0x49D120) and aims at whatever comes back. Neither of the two
                // anti-nuke weapons in the shipped data carries `commandfire`,
                // which is what lets them engage on their own.
                if (unit.fireOrders != UnitFireOrders::HoldFire)
                {
                    if (auto target = sim->findInterceptTarget(id, weaponIndex))
                    {
                        weapon->state = UnitWeaponStateAttacking(*target);
                    }
                }
            }
            else if (unit.fireOrders == UnitFireOrders::FireAtWill
                && (!weaponDefinition.commandFire || sim->getPlayer(unit.owner).type == GamePlayerType::Computer)
                && !std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType))
            {
                // A `commandfire` weapon is normally fired by hand and never
                // acquires a target of its own -- that is what makes the D-gun
                // a decision rather than a gun. The original makes one
                // exception, in the auto-acquire scan at 0x4089A0: "commandfire
                // weapons do not either, **unless the player is of type 2**".
                // Type 2 is the computer player, the same `player+0x73` byte
                // that exempts an AI from the ShootMe rule in chooseTarget
                // below -- so half of that one finding was already implemented
                // here and this half was not.
                //
                // So an AI commander does D-gun what comes at it, and a human's
                // does not unless told. Reported from a play-test: a commander
                // stood and died under fire from several units with the one
                // weapon that would have cleared them unused.
                //
                // A dropped weapon is excluded from this scan entirely --
                // 0x408A7F skips it before the loop ever runs a comparison, so
                // a bomb never auto-acquires (§86). Nothing else routes a bomb
                // into an attack order except the break-off in
                // handlePatrolOrder / findEnemyToEngage, which is the only way
                // a bomber ever drops anything.
                if (auto target = chooseTarget(id, weaponIndex))
                {
                    weapon->state = UnitWeaponStateAttacking(*target);
                }
            }
        }
        else if (auto aimingState = std::get_if<UnitWeaponStateAttacking>(&weapon->state); aimingState != nullptr)
        {
            // Unit targets require current player-wide sight even after
            // acquisition, including during a burst. Ground fire and missile
            // interception have no unit visibility requirement. Already
            // launched projectiles continue under their ordinary physics.
            if (auto target = std::get_if<UnitId>(&aimingState->target))
            {
                auto targetRef = sim->tryGetUnitState(*target);
                if (!targetRef || targetRef->get().isDead() || !sim->canSeeUnit(unit.owner, *target))
                {
                    unit.clearWeaponTarget(weaponIndex);
                    return;
                }
            }
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

            // A bad target category is a preference when a target is picked,
            // but it is more than that once one is held: the scan at 0x4089A0
            // keeps an existing target only while it is alive, still in range
            // and **not in that slot's bad-target set**. One that is in the set
            // is dropped where it stands and 0x40B7B0 picks again, so a Jethro
            // that opened up on a tank for want of anything better comes off it
            // the moment an aircraft is in reach. Without this the first thing
            // an anti-air unit shot at was the last: RWE preferred air when it
            // chose and then never chose again.
            //
            // Two things the original gets for free and this has to say out
            // loud. The scan only looks at a slot no mission owns (the "weapon
            // free" bit), so an explicit attack order is left alone -- and the
            // handler would only put the target straight back anyway. And the
            // scan advances its cursor `unitCount/30 + 1` units a tick, so it
            // reaches any given unit about once a second; this is phased per
            // unit to that cadence rather than run every tick, because a gun
            // that gave up its aim thirty times a second would never fire.
            if (unit.fireOrders == UnitFireOrders::FireAtWill
                && !weaponDefinition.interceptor
                && (sim->gameTime.value + id.value) % static_cast<unsigned int>(SimTicksPerSecond) == 0)
            {
                if (auto targetUnit = std::get_if<UnitId>(&aimingState->target); targetUnit != nullptr)
                {
                    auto orderedAtIt = false;
                    if (!unit.orders.empty())
                    {
                        if (auto attackOrder = std::get_if<AttackOrder>(&unit.orders.front()); attackOrder != nullptr)
                        {
                            auto orderTarget = std::get_if<UnitId>(&attackOrder->target);
                            orderedAtIt = orderTarget != nullptr && *orderTarget == *targetUnit;
                        }
                    }

                    // A target the guard mission handed the weapons is theirs
                    // to keep, as an ordered one is (BT-91).
                    orderedAtIt = orderedAtIt || (unit.guardTarget && *unit.guardTarget == *targetUnit);

                    if (!orderedAtIt)
                    {
                        const auto& unitDefinition = sim->unitDefinitions.at(unit.unitType);
                        const auto& badCategory = unitDefinition.badTargetCategory.at(weaponIndex);
                        auto targetUnitState = sim->tryGetUnitState(*targetUnit);
                        if (targetUnitState
                            && categoryListContains(sim->unitDefinitions.at(targetUnitState->get().unitType).category, badCategory))
                        {
                            unit.clearWeaponTarget(weaponIndex);
                            return;
                        }
                    }
                }
            }

            auto targetPosition = getTargetPosition(aimingState->target);

            // TOTALA-EXE-WEAPONS.md S:126 (0x48A1E0): a shooter with six or
            // more kills leads a moving unit target. The one point it
            // produces is what the range test, the heading/pitch solve and the
            // launch all read, so it is added here and goes everywhere.
            if (targetPosition)
            {
                if (auto leadTargetId = std::get_if<UnitId>(&aimingState->target); leadTargetId != nullptr)
                {
                    if (auto leadTarget = sim->tryGetUnitState(*leadTargetId))
                    {
                        const auto& leadTargetState = leadTarget->get();
                        auto targetVelocity = match(
                            leadTargetState.physics,
                            [&](const UnitPhysicsInfoAir& a) { return airVelocity(a.movementState); },
                            [&](const UnitPhysicsInfoGround& g) { return UnitState::toDirection(leadTargetState.rotation) * g.currentSpeed; });
                        bool cruise = false;
                        if (auto selfPropelled = std::get_if<ProjectilePhysicsTypeSelfPropelled>(&weaponDefinition.physicsType))
                        {
                            cruise = selfPropelled->cruise;
                        }
                        *targetPosition = *targetPosition + computeAimLead(unit.position, *targetPosition, targetVelocity, weaponDefinition.velocity, cruise, unit.kills, veterancyRules(sim->unitDefinitions.at(unit.unitType).veterancy, sim->communityPatchMode));
                    }
                }
            }

            // An interceptor's engagement rule is `coverage`, not `range`: both
            // anti-nukes say range=32000, which is the whole map, and coverage
            // 2000. What it is held against is the missile's aim point, the
            // same thing the acquisition tested (0x49D18D), because the box the
            // launcher defends does not move when the missile does.
            bool targetLost = !targetPosition;
            if (weaponDefinition.interceptor)
            {
                auto targetProjectile = std::get_if<ProjectileId>(&aimingState->target);
                auto projectile = targetProjectile == nullptr ? std::nullopt : sim->projectiles.tryGet(*targetProjectile);
                targetLost = !projectile || projectile->get().isDead || !projectile->get().targetPosition
                    || !GameSimulation::isWithinCoverage(unit.position, *projectile->get().targetPosition, weaponDefinition.coverage);
            }

            if (targetLost)
            {
                unit.clearWeaponTarget(weaponIndex);
            }
            else if (!weaponDefinition.interceptor
                && !std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType)
                && !withinFlatRange(unit.position, *targetPosition, weaponDefinition.maxRange))
            {
                // S:127: out of range prevents firing, but retains the target.
                return;
            }
            else if (std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType))
            {
                // There is no bombsight in the original's weapon code at all.
                // The bomb handler 0x49DD60 spawns the round on the first fire
                // check with the reload run down, with no aim script, no
                // tolerance test and no look at the ground: the round simply
                // inherits the aircraft's heading and speed. What decides
                // WHERE the bombs fall is the mission. AirStrike's state 4
                // (0x412394) withholds the target until the aircraft is within
                //   1 + attackrunlength + ftol(sqrt(2 * cruisealt / g) * 30 * speed)
                // of it, the second term being how far a bomb dropped now
                // travels while it falls from cruise altitude; state 5 then
                // hands weapon 0 the ground point and the weapon drops on
                // every reload; state 6 takes it away again attackrunlength
                // world units past the release point. So the stick starts
                // 1 + attackrunlength short of the target and walks up to it.
                // The COB aim dance is skipped for the same reason it is in
                // the original: a bomb is neither turret nor vlaunch.
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
                            [&](const AirMovementStateDogfight& m) { bomberVelocity = m.currentVelocity; },
                            [&](const AirMovementStateLanding&) {});
                    }

                    const auto& bomberDefinition = sim->unitDefinitions.at(unit.unitType);
                    SimVector flatToTarget(targetPosition->x - unit.position.x, 0_ss, targetPosition->z - unit.position.z);
                    SimVector flatVelocity(bomberVelocity.x, 0_ss, bomberVelocity.z);
                    auto trigger = bombReleaseTrigger(bomberDefinition, flatVelocity.length(), sim->gravityPerTick());

                    bool releasing = false;
                    if (runState && runState->releasePoint)
                    {
                        // Mid-stick: state 5's run, which state 6 ends
                        // attackrunlength past where the first bomb left.
                        SimVector flatFromRelease(unit.position.x - runState->releasePoint->x, 0_ss, unit.position.z - runState->releasePoint->z);
                        releasing = flatFromRelease.lengthSquared() < bomberDefinition.attackRunLength * bomberDefinition.attackRunLength;
                    }
                    else if (flatToTarget.lengthSquared() <= trigger * trigger)
                    {
                        releasing = true;
                        if (runState)
                        {
                            runState->releasePoint = unit.position;
                        }
                    }

                    if (releasing)
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

                auto headingAndPitch = computeHeadingAndPitch(unit.rotation, aimFromPosition, *targetPosition, weaponDefinition.velocity, sim->gravityPerTick(), weaponDefinition.physicsType);
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
                    notifyAimScriptStarted(id, unit, getAimScriptName(weaponIndex), toCobAngle(heading).value, toCobAngle(pitch).value);
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
            else
            {
                if (auto aimInfo = std::get_if<UnitWeaponStateAttacking::AimInfo>(&aimingState->attackInfo))
                {
                    auto returnValue = unit.cobEnvironment->tryReapThread(aimInfo->thread);
                    if (returnValue)
                    {
                        if (*returnValue)
                        {
                            aimingState->attackInfo = UnitWeaponStateAttacking::AimedInfo{aimInfo->lastHeading, aimInfo->lastPitch};
                        }
                        else
                        {
                            // A no is where RWE knowingly parts from the
                            // original. 0x49D580 finds the answer still zero
                            // and leaves at 0x49D86D with bit 0 up, so a
                            // script that refuses is not asked again until
                            // something clears the bit -- and what clears it
                            // on a change of target has not been read. Asking
                            // again next tick cannot strand a gun the
                            // original would have freed.
                            aimingState->attackInfo = UnitWeaponStateAttacking::IdleInfo{};
                        }
                    }
                    else if (!unit.cobEnvironment->ownsThread(aimInfo->thread))
                    {
                        // The aim thread is gone without an answer: killed
                        // by a later aim's signal -- a commander's laser and
                        // its D-gun share one -- or finished and swept in a
                        // pass this weapon did not get to look at. Nothing
                        // will ever answer, so ask again. It used to wait
                        // here for good, a gun that never fired again until
                        // its target was cleared; or, when the allocator
                        // handed the dead thread's address to the next one,
                        // to take that thread's answer for its own.
                        aimingState->attackInfo = UnitWeaponStateAttacking::IdleInfo{};
                    }
                }

                // Aimed, and waiting for the reload. The original does not
                // look at an aimed slot at all while its reload counter is
                // running (0x49E3AE), and starts no new aim while bit 0 is up
                // (0x49E211), so a gun that has come round sits on its aim for
                // the rest of the reload. RWE used to go back to idle here and
                // run the script again every other tick of every reload, which
                // is upstream #42: 151 aims for 12 shots from a commander
                // attacking the ground.
                if (auto aimedInfo = std::get_if<UnitWeaponStateAttacking::AimedInfo>(&aimingState->attackInfo);
                    aimedInfo != nullptr && sim->gameTime >= weapon->readyTime)
                {
                    // The reload is done: check the target again for drift
                    // against the angles the script was sent.
                    auto aimFromPosition = getAimingPoint(id, weaponIndex);

                    auto headingAndPitch = computeHeadingAndPitch(unit.rotation, aimFromPosition, *targetPosition, weaponDefinition.velocity, sim->gravityPerTick(), weaponDefinition.physicsType);
                    auto heading = headingAndPitch.first;
                    auto pitch = headingAndPitch.second;

                    if (angleBetweenIsLessOrEqual(heading, aimedInfo->lastHeading, weaponDefinition.tolerance) && angleBetweenIsLessOrEqual(pitch, aimedInfo->lastPitch, weaponDefinition.pitchTolerance))
                    {
                        aimingState->attackInfo = UnitWeaponStateAttacking::FireInfo{heading, pitch, *targetPosition, std::nullopt, 0, GameTime(0)};
                        tryFireWeapon(id, weaponIndex);
                    }
                    else
                    {
                        // Moved past tolerance while the reload ran. The
                        // original drops bit 0 (0x49D68A) and aims afresh on
                        // the next tick, which is what idle does here.
                        aimingState->attackInfo = UnitWeaponStateAttacking::IdleInfo{};
                    }
                }
            }
        }
    }

    SimVector UnitBehaviorService::changeDirectionByRandomAngle(const SimVector& direction, SimAngle spread)
    {
        // `sprayangle` is the whole width of the spread, not the deviation
        // either side of the aim: the original draws rand(sprayangle) and
        // takes half of it back off (0x49B903-0x49B91B), landing uniformly on
        // [-sprayangle/2, sprayangle/2). Drawing the full angle and flipping a
        // coin for the sign, as this used to, made every burst weapon in the
        // game scatter twice as wide as it should.
        if (spread == SimAngle(0))
        {
            return direction;
        }

        // Taken modulo rather than through a uniform_int_distribution, whose
        // mapping the standard does not pin down: std::minstd_rand itself is
        // specified exactly, so a modulo keeps every client drawing the same
        // number. It is also what the original's own rand-below-n does.
        auto& rng = sim->rng;
        auto angle = SimAngle(static_cast<uint16_t>(rng() % spread.value)) - SimAngle(spread.value / 2u);

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

        auto gameTime = sim->gameTime;
        if (gameTime < fireInfo->readyTime)
        {
            return;
        }

        // Community Patch (#934): TADR's notoverwater/notoverland hold the
        // shot without spending it, and nottoair/nottounderwater refuse a
        // target that became ineligible after it was chosen. Before the cost,
        // so ammunition and per-shot resources survive a suppressed frame.
        if (sim->communityPatchMode)
        {
            auto ground = sim->terrain.getHeightAt(unit.position.x, unit.position.z);
            auto overWater = ground <= sim->terrain.getSeaLevel();
            if (communityPatchSuppressesFire(weaponDefinition, overWater))
            {
                return;
            }
            if (auto targetId = std::get_if<UnitId>(&attackInfo->target))
            {
                if (auto target = sim->tryGetUnitState(*targetId))
                {
                    const auto& targetState = target->get();
                    if ((weaponDefinition.notToAir && isFlying(targetState.physics))
                        || (weaponDefinition.notToUnderwater && !sim->modelTopIsAboveSea(targetState)))
                    {
                        return;
                    }
                }
            }
        }

        // What the shot costs is settled before anything else happens
        // (TotalA.exe 0x49E3D5). A stockpiled weapon needs a round in the
        // magazine; everything else has to find its energypershot and
        // metalpershot in the player's stores there and then. Either way a
        // weapon that cannot pay simply does not fire -- the original never
        // lets the shot go and takes the debt afterwards, which is what stops a
        // commander with a flat battery from D-gunning anything.
        // The launch is abandoned outright if the thing it was going to chase
        // is no longer there (0x49DC17 fetches the target projectile first and
        // gives up without one). Without this an anti-nuke would spend a round
        // on a missile that had already gone off.
        if (weaponDefinition.interceptor)
        {
            auto targetProjectile = std::get_if<ProjectileId>(&attackInfo->target);
            auto projectile = targetProjectile == nullptr ? std::nullopt : sim->projectiles.tryGet(*targetProjectile);
            if (!projectile || projectile->get().isDead)
            {
                return;
            }
        }

        if (weaponDefinition.stockpile)
        {
            if (weapon->stockedRounds <= 0)
            {
                return;
            }
        }
        else
        {
            // Taken from the stores directly rather than through the ordinary
            // request-and-settle path. That path pays every consumer a share
            // of whatever there is and carries the rest as debt, which is
            // right for a builder but not for a shot: the original subtracts
            // the price before the round leaves the barrel (0x401220,
            // 0x401260, 0x4012A0), so the next shot this second sees the
            // smaller stock, a weapon that cannot cover it in full simply does
            // not fire, and a shooter that owes for something else still pays
            // rather than firing for free.
            if (!sim->chargeStockpile(id, weaponDefinition.energyPerShot, weaponDefinition.metalPerShot))
            {
                return;
            }
        }

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


        // How far the round strays from where the mount was pointed. The
        // original does this between solving the aim and spawning the
        // projectile (0x49D6D7), so it lands on every kind of weapon and not
        // just the ballistic ones, and it is the reason a Vulcan firing at its
        // own maximum range in the original scatters shells over a patch some
        // seven hundred units deep against a hundred-unit blast. A bomb is left
        // out because its release is decided by the bombsight rather than by an
        // aim we could perturb.
        // Only a weapon on a turret scatters. The original picks a fire
        // handler per weapon out of the flags (0x49E010) and the arithmetic
        // below lives inside the turret one: the handler the other weapons get,
        // 0x49D9C0, does not call the random number generator at all, where the
        // turret handler calls it exactly twice. So a torpedo, a bomb, a
        // vertical launch and every aircraft weapon shoot exactly where they
        // are pointed however badly hurt the shooter is -- which matters most
        // for the gunships, since both their weapons are turret=0.
        if (weaponAimScatters(weaponDefinition))
        {
            // Deliberately not gated on the weapon having an `accuracy`. The
            // original runs this arithmetic for every shot the turret handler
            // takes, whatever the weapon says, and the health term is not
            // conditional either, so a weapon with a perfect `accuracy=0`
            // still starts to spread once its owner has been shot up. At full
            // health the term cancels exactly and such a weapon stays perfect,
            // which is why this is not as drastic as it sounds -- nothing
            // changes for an undamaged unit.
            const auto& unitDefinition = sim->unitDefinitions.at(unit.unitType);
            auto cone = computeAccuracyCone(weaponDefinition.accuracy, unit.hitPoints, unitDefinition.maxHitPoints, unit.kills, veterancyRules(unitDefinition.veterancy, sim->communityPatchMode));
            if (cone != SimAngle(0))
            {
                // Uniform on [-cone/2, cone/2), drawn twice over, exactly as
                // the two rand() calls at 0x49D727 and 0x49D733 do it. Modulo
                // rather than a distribution, so that every client draws the
                // same number -- see changeDirectionByRandomAngle.
                auto& rng = sim->rng;
                auto half = SimAngle(cone.value / 2u);
                auto headingError = SimAngle(static_cast<uint16_t>(rng() % cone.value)) - half;
                auto pitchError = SimAngle(static_cast<uint16_t>(rng() % cone.value)) - half;
                direction = applyAimError(direction, headingError, pitchError);
            }
        }

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
                    [&](const AirMovementStateDogfight& m) { bomberVelocity = m.currentVelocity; },
                    [&](const AirMovementStateLanding&) {});
            }
            inheritedVelocity = bomberVelocity;
        }

        auto targetUnit = std::get_if<UnitId>(&attackInfo->target);
        auto targetUnitOption = targetUnit == nullptr ? std::optional<UnitId>() : std::make_optional(*targetUnit);
        auto targetProjectile = std::get_if<ProjectileId>(&attackInfo->target);
        auto targetProjectileOption = targetProjectile == nullptr ? std::optional<ProjectileId>() : std::make_optional(*targetProjectile);
        sim->spawnProjectile(ProjectileSpawn{
            .owner = unit.owner,
            .weapon = &*weapon,
            .position = firingPoint,
            .direction = direction,
            .distanceToTarget = (fireInfo->targetPosition - firingPoint).length(),
            .targetUnit = targetUnitOption,
            .attacker = id,
            .inheritedVelocity = inheritedVelocity,
            .targetPosition = fireInfo->targetPosition,
            .targetProjectile = targetProjectileOption,
            .burstChildHorizontalDistance = fireInfo->burstsFired > 0
                ? std::make_optional(SimVector(fireInfo->targetPosition.x - firingPoint.x, 0_ss, fireInfo->targetPosition.z - firingPoint.z).length())
                : std::nullopt,
        });

        // The demo's 0x0d, and the only place the ids exist: the event carries
        // the weapon type and the fire point and nothing that names a unit or a
        // slot. `direction` is the round's launch direction after the aim error
        // the turret handler above scattered it by, which is the attitude the
        // corpus's rotation triple records.
        if (sim->demoRecorder)
        {
            std::optional<UnitId> targetUnit;
            if (auto unitTarget = std::get_if<UnitId>(&attackInfo->target))
            {
                targetUnit = *unitTarget;
            }
            sim->demoRecorder->shotFired(*sim, id, weaponIndex, targetUnit, firingPoint, fireInfo->targetPosition, direction);
        }

        if (sim->taLiveSender.attached())
        {
            std::optional<UnitId> targetUnit;
            if (auto unitTarget = std::get_if<UnitId>(&attackInfo->target))
            {
                targetUnit = *unitTarget;
            }
            sim->taLiveSender.shotFired(sim->taLiveSender.context, *sim, id, weaponIndex, targetUnit, firingPoint, fireInfo->targetPosition, direction);
        }

        sim->events.push_back(FireWeaponEvent{weapon->weaponType, fireInfo->burstsFired, firingPoint});

        if (weaponDefinition.commandFire)
        {
            // Tell whatever ordered this that the shot went off; the order
            // ends on it. See UnitState::commandFireShotFired.
            unit.commandFireShotFired = true;
        }

        if (weaponDefinition.stockpile)
        {
            --weapon->stockedRounds;
        }

        // The Fire script and RockUnit are started inside the spawn routines,
        // which run once for the first round of a burst; the later rounds are
        // made by the per-tick projectile update and start no script at all
        // (BT-113, 0x49C9C0/0x49CC20/0x49CDE0 against 0x49B810-0x49BE5C). A
        // bomb handler starts neither.
        if (fireInfo->burstsFired == 0)
        {
            unit.cobEnvironment->createThread(getFireScriptName(weaponIndex));
            if (!weaponDefinition.stockpile)
            {
                // A stockpiled weapon gets no reload timer at all: the original
                // takes the round and jumps straight past the reload
                // calculation (0x49E455), because the wait was the build.
                //
                // What the rest wait is not the flat TDF number: 0x49E468
                // scales it by how hurt the shooter is and by its kill count.
                // See computeReloadTicks -- an undamaged unit with no kills
                // gets exactly its `reloadtime` back, so nothing changes for
                // the ordinary case.
                const auto& shooterDefinition = sim->unitDefinitions.at(unit.unitType);
                weapon->readyTime = gameTime + computeReloadTicks(weaponDefinition.reloadTime, unit.hitPoints, shooterDefinition.maxHitPoints, unit.kills, veterancyRules(shooterDefinition.veterancy, sim->communityPatchMode));
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
        if (!isBomb && fireInfo->burstsFired == 0)
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
                // Working on something, a builder faces the job rather than
                // whatever heading it stopped on -- see slowFacePoint, and
                // the turn every work mission in the original makes.
                if (unitInfo.state->slowFacePoint)
                {
                    auto direction = *unitInfo.state->slowFacePoint - unitInfo.state->position;
                    direction.y = 0_ss;
                    if (direction.lengthSquared() > 0_ss)
                    {
                        unitInfo.state->rotation = turnTowards(unitInfo.state->rotation, UnitState::toRotation(direction), turnRateThisFrame);
                        return;
                    }
                }
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
                    },
                    [&](const AirMovementStateDogfight& m) {
                        // A fighter flies where it is pointing, unlike the
                        // gunship beside it: its whole geometry -- the
                        // forward half-plane test, the extend, the break --
                        // is measured off the nose, so the nose follows the
                        // flight path.
                        SimVector heading = m.currentVelocity;
                        heading.y = 0_ss;
                        if (heading.lengthSquared() == 0_ss)
                        {
                            return;
                        }
                        unitInfo.state->rotation = turnTowards(unitInfo.state->rotation, UnitState::toRotation(heading), turnRateThisFrame);
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
                AirMovement::updateVelocity(p, *unitInfo.state, *unitInfo.definition, sim->gravity);
            });
    }

    void UnitBehaviorService::updateGroundTilt(UnitInfo unitInfo, UnitPhysicsInfoGround& physics)
    {
        // The original's per-tick placement (0x48A870): an `upright` unit
        // stays vertical off one sample under its centre (0x48A8BF), anything
        // else tilts to the slope under four rotated footprint corners
        // (0x48A490 writes unit+0x68 and unit+0x64). Every tick, moving or
        // not, since the ground can change under a standing unit. A floater
        // that is not upright is not aligned at all (BT-42): its height is
        // fixed by sea level and its pitch and bank are never touched, so a
        // ship does not tilt. Only canhover holds the corner samples at sea.
        physics.previousPitch = physics.pitch;
        physics.previousRoll = physics.roll;
        if (unitInfo.definition->upright || unitInfo.definition->floater)
        {
            physics.pitch = 0_ss;
            physics.roll = 0_ss;
            return;
        }
        // BT-41 (0x48A490): the four points are vertices of the root model's
        // selection primitive, not the footprint's corners, and the unit's
        // height is the mean of the four samples. A model that was read from a
        // 3DO but has no selection primitive is left exactly as it is, as the
        // original leaves it; a model with no vertex data at all (a fixture
        // built by hand) keeps the footprint's corners.
        //
        // Not for an aircraft sitting on the ground or a unit hanging from a
        // transport: their height is somebody else's (a pad's, a hook's), and
        // this routine would drop them to the ground under them.
        auto modelIt = sim->unitModelDefinitions.find(unitInfo.definition->objectName);
        if (!unitInfo.definition->canFly && !unitInfo.state->carriedBy
            && modelIt != sim->unitModelDefinitions.end() && modelIt->second.vertices)
        {
            const auto& rectangle = modelIt->second.vertices->selectionRectangle;
            if (!rectangle)
            {
                return;
            }
            if (auto aligned = computeSelectionAlignment(sim->terrain, unitInfo.state->position, unitInfo.state->rotation, *rectangle, unitInfo.definition->canHover))
            {
                physics.pitch = aligned->pitch;
                physics.roll = aligned->roll;
                unitInfo.state->position.y = aligned->y;
            }
            return;
        }
        auto [footprintX, footprintZ] = sim->getFootprintXZ(unitInfo.definition->movementCollisionInfo);
        auto halfWidth = SimScalar(static_cast<float>(footprintX) * 8.0f);
        auto halfLength = SimScalar(static_cast<float>(footprintZ) * 8.0f);
        auto tilt = computeGroundTilt(sim->terrain, unitInfo.state->position, unitInfo.state->rotation, halfLength, halfWidth, unitInfo.definition->canHover);
        physics.pitch = tilt.pitch;
        physics.roll = tilt.roll;
    }

    void UnitBehaviorService::updateGroundUnitPosition(UnitInfo unitInfo, UnitPhysicsInfoGround& physics)
    {
        auto direction = UnitState::toDirection(unitInfo.state->rotation);

        if (physics.currentSpeed > 0_ss)
        {
            auto newPosition = unitInfo.state->position + (direction * physics.currentSpeed);
            newPosition.y = computeGroundUnitHeight(sim->terrain, *unitInfo.definition, newPosition.x, newPosition.z);

            if (!tryApplyMovementToPosition(unitInfo, newPosition))
            {
                unitInfo.state->inCollision = true;

                // 0x43D6D0, the blocked branch (BT-12): a refused step does
                // not slide along the obstacle. The unit is held inside the
                // window that maps to the cell it is already in -- the
                // centre of its footprint block, plus or minus just under
                // eight world units on each axis -- and, if its stored speed
                // is above half the type's maximum, speed is dropped to that
                // half. The velocity is speed along the current heading, so
                // storing the speed recomputes it.
                auto region = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
                auto [footprintX, footprintZ] = sim->getFootprintXZ(unitInfo.definition->movementCollisionInfo);
                auto corner = sim->terrain.heightmapIndexToWorldCorner(region.x, region.y);
                // The original stops 0x7FFFF in 16.16 (just under eight) short of
                // the edge. A float world coordinate cannot resolve that at the
                // far side of a big map, and a position that rounds into the
                // next cell is refused again, so the margin is 1/64.
                const auto reach = 8_ss - SimScalar(1.0f / 64.0f);
                const auto centreX = corner.x + SimScalar(static_cast<float>(footprintX) * 8.0f);
                const auto centreZ = corner.z + SimScalar(static_cast<float>(footprintZ) * 8.0f);

                SimVector held(
                    std::clamp(newPosition.x, centreX - reach, centreX + reach),
                    0_ss,
                    std::clamp(newPosition.z, centreZ - reach, centreZ + reach));
                held.y = computeGroundUnitHeight(sim->terrain, *unitInfo.definition, held.x, held.z);
                tryApplyMovementToPosition(unitInfo, held);

                const auto halfMax = unitInfo.definition->maxVelocity / 2_ss;
                if (physics.currentSpeed > halfMax)
                {
                    physics.currentSpeed = halfMax;
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
            [&](UnitPhysicsInfoGround& p) {
                updateGroundUnitPosition(unitInfo, p);
                updateGroundTilt(unitInfo, p);
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
                    },
                    [&](const AirMovementStateDogfight& m) {
                        // The same, and deliberately so. The original's
                        // dogfight has no altitude discipline of its own --
                        // the one call in it that looks like one, 0x44EC10,
                        // is an empty stub that throws its argument away --
                        // so a fighter is left to the ordinary cruise height
                        // rather than chasing its bandit up and down.
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

        //
        // The cells the unit already stands on do not count against it: a unit
        // dropped inside a blocking feature -- a corpse over the ground it
        // occupies, say -- has no move that is collision-free until it has
        // stepped clear, and would otherwise be pinned where it is. This only
        // lets it walk over its own body and off the obstacle; entering a
        // blocking cell it is not already on is still refused. It is not the
        // original's wall-push (which is not decoded -- see TOTALA-EXE.md
        // §87), just a unit being allowed to leave the ground it is on.
        auto newFootprintRegion = sim->computeFootprintRegion(newPosition, unitInfo.definition->movementCollisionInfo);
        auto currentFootprintRegion = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);

        if (sim->isCollisionAt(newFootprintRegion, unitInfo.id, currentFootprintRegion))
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

        auto footprintRegion = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
        sim->moveUnitOccupiedArea(footprintRegion, newFootprintRegion, unitInfo.id);

        unitInfo.state->position = newPosition;

        // The script is told how the unit sits in the water from
        // updateSfxOccupancy, once a tick at the end of the mover's update
        // (0x43DD20) and not from here: the original asks every mobile unit
        // every tick, moving or not (BT-11).
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

        // A query answers or it does not. One that faults, or that tries to
        // wait on something, gets the caller's fallback -- the same answer a
        // unit with no such script gets -- rather than ending the game on
        // every peer. A signal is sent as it always was and the query goes
        // on: the thread is not a scheduled one, so no signal can kill it.
        // Issue #75.
        try
        {
            auto status = context.execute();
            while (const auto* signal = std::get_if<CobEnvironment::SignalStatus>(&status))
            {
                unit.cobEnvironment->sendSignal(signal->signal);
                status = context.execute();
            }
            if (std::get_if<CobEnvironment::FinishedStatus>(&status) == nullptr)
            {
                LOG_WARN << "COB query " << name << " blocked before completing; ignoring it";
                return std::nullopt;
            }
        }
        catch (const std::exception& e)
        {
            LOG_WARN << "COB query " << name << " stopped: " << e.what();
            return std::nullopt;
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

    /**
     * BT-60 (0x43E3C0 and 0x43E0B0): the aim point of a unit target. The script
     * is asked for SweetSpot with a default of piece 0; the piece's posed
     * vertices are bounded on each axis with zero always inside the box; the
     * result per axis is the box's midpoint plus the unit's position. Nothing
     * is added for the piece offset (the posed points already include it) and
     * nothing is rotated afterwards.
     *
     * The z axis is the uncertain part (medium confidence in the finding). The
     * original's model z is the world's reversed, and the routine adds the
     * box's z as it stands without negating it, so an off-centre box is
     * mirrored in z against where the piece is drawn. RWE's pieces are in the
     * right-handed frame already (the 3DO z is negated at load), so the
     * original's z is minus RWE's. A box symmetric about zero shows nothing.
     *
     * Not modelled: the original poses the vertices lazily, so the point lags
     * a slow turn by up to eight angle units; and its midpoint truncates the
     * 16.16 sum toward zero.
     */
    SimVector UnitBehaviorService::getSweetSpot(UnitId id)
    {
        const auto& unit = sim->getUnitState(id);

        auto pieceId = runCobQuery(id, "SweetSpot");
        auto pieceNumber = pieceId ? *pieceId : 0;

        // A unit with no model definition at all (some fixtures) has no
        // vertices to bound, which is the same as a model with none.
        const auto& unitDefinition = sim->unitDefinitions.at(unit.unitType);
        auto modelIt = sim->unitModelDefinitions.find(unitDefinition.objectName);
        const auto& scriptPieces = unit.cobEnvironment->_script->pieces;
        if (modelIt == sim->unitModelDefinitions.end() || !modelIt->second.vertices
            || pieceNumber < 0 || static_cast<std::size_t>(pieceNumber) >= scriptPieces.size())
        {
            return unit.position;
        }
        const auto& model = modelIt->second;

        const auto& pieceName = scriptPieces[static_cast<std::size_t>(pieceNumber)];
        auto pieceIndex = model.pieceIndicesByName.find(toUpper(pieceName));
        if (pieceIndex == model.pieceIndicesByName.end()
            || static_cast<std::size_t>(pieceIndex->second) >= model.vertices->perPiece.size())
        {
            return unit.position;
        }

        auto local = sim->getUnitPieceLocalTransform(id, pieceName);
        auto turn = Matrix4x<SimScalar>::rotationY(sin(unit.rotation), cos(unit.rotation));

        auto minX = 0_ss;
        auto maxX = 0_ss;
        auto minY = 0_ss;
        auto maxY = 0_ss;
        auto minZ = 0_ss;
        auto maxZ = 0_ss;
        for (const auto& vertex : model.vertices->perPiece[static_cast<std::size_t>(pieceIndex->second)])
        {
            auto posed = turn * (local * vertex);
            minX = rweMin(minX, posed.x);
            maxX = rweMax(maxX, posed.x);
            minY = rweMin(minY, posed.y);
            maxY = rweMax(maxY, posed.y);
            minZ = rweMin(minZ, posed.z);
            maxZ = rweMax(maxZ, posed.z);
        }

        return SimVector(
            unit.position.x + ((minX + maxX) / 2_ss),
            unit.position.y + ((minY + maxY) / 2_ss),
            unit.position.z - ((minZ + maxZ) / 2_ss));
    }

    std::optional<SimVector> UnitBehaviorService::tryGetSweetSpot(UnitId id)
    {
        if (!sim->unitExists(id))
        {
            return std::nullopt;
        }

        return getSweetSpot(id);
    }

    void UnitBehaviorService::dropAirAttackRun(UnitInfo unitInfo)
    {
        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr || !std::holds_alternative<AirMovementStateAttackRun>(airPhysics->movementState))
        {
            return;
        }

        // Carry the speed over. An aircraft handed a new order in the middle
        // of a run does not stop in the air to take it.
        AirMovementStateFlying flying;
        flying.currentVelocity = std::get<AirMovementStateAttackRun>(airPhysics->movementState).currentVelocity;
        airPhysics->movementState = flying;
        unitInfo.state->clearWeaponTargets();
    }

    bool UnitBehaviorService::airIsOutsideGrid(UnitInfo unitInfo) const
    {
        return unitFootprintIsOutsideTerrain(*sim, *unitInfo.state, *unitInfo.definition);
    }

    void UnitBehaviorService::steerAirBackInside(UnitInfo unitInfo)
    {
        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr)
        {
            return;
        }

        // Climbing out or setting down is a transition rather than a movement
        // objective; leave it to the air state machine.
        if (std::holds_alternative<AirMovementStateTakingOff>(airPhysics->movementState)
            || std::holds_alternative<AirMovementStateLanding>(airPhysics->movementState))
        {
            return;
        }

        // Whatever the aircraft was flying is movement, not order state: the
        // original's branch replaces the attached movement objective and
        // leaves the mission's own state alone. Hand the run, the ring or the
        // dogfight back as level flight, keeping the speed it had, as the
        // idle circuits do.
        if (!std::holds_alternative<AirMovementStateFlying>(airPhysics->movementState))
        {
            AirMovementStateFlying flying;
            flying.currentVelocity = airVelocity(airPhysics->movementState);
            airPhysics->movementState = flying;
            unitInfo.state->clearWeaponTargets();
        }
        auto& flying = std::get<AirMovementStateFlying>(airPhysics->movementState);

        flying.targetPosition = aircraftBoundaryRecoveryGoal(sim->terrain, unitInfo.state->position);
    }

    void UnitBehaviorService::maybeBreakOffToRepairPad(UnitInfo unitInfo)
    {
        // Seven of the original's twenty-two VTOL mission handlers open with
        // the same pair of instructions: the three-quarter health gate and a
        // 3840-unit air base query (0x41055F, 0x4109B9, 0x410F95, 0x412613,
        // 0x412B91, 0x413ADC, 0x4153E3). When the query finds something the
        // handler pushes a VTOL_LANDING mission carrying the pad to the front
        // of the unit's mission list (0x43ACB0) and returns, so the work it
        // was doing is still underneath and comes back afterwards.
        //
        // Which seven matters as much as the gate itself. VTOL_Move and
        // VTOL_Standby are not among them, so a plane merely sent somewhere,
        // or one standing idle, never goes off to be mended. Patrol and the
        // eligible attack missions do; Follow (the modelled Guard mission)
        // does not. The original SeekGuard fallback is not modelled here.
        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);

        // AirToAir (0x412D40) is the one attack handler with no health gate
        // in it: a fighter already locked onto another aircraft fights on
        // however badly hurt it is. Its three sibling handlers all break off.
        auto inDogfight = airPhysics != nullptr
            && std::holds_alternative<AirMovementStateDogfight>(airPhysics->movementState);

        // TOTALA-EXE-ECONOMY.md S:130: the test runs only when the mission's
        // own handler does -- on its timer, 30 ticks for the attack missions
        // and patrol and 45 for a constructor's RepairPatrol -- and not
        // every tick. The handlers' timers are not modelled one by one; each
        // unit is given its own phase of the period, which keeps the cadence
        // and is stateless. [open] The gunship's gate is in the swing branch
        // only, so it is not applied while the ring is still being closed.
        auto frontOrderBreaksOff = false;
        if (!unitInfo.state->orders.empty() && orderBreaksOffForRepair(unitInfo.state->orders.front()))
        {
            frontOrderBreaksOff = true;
            unsigned int period = 30;
            if (std::holds_alternative<PatrolOrder>(unitInfo.state->orders.front()) && unitInfo.definition->canReclamate)
            {
                period = repairPatrolPeriod(true);
            }
            if (airPhysics != nullptr)
            {
                if (auto hover = std::get_if<AirMovementStateHoverAttack>(&airPhysics->movementState);
                    hover != nullptr && hover->phase != AirMovementStateHoverAttack::Phase::Swinging)
                {
                    frontOrderBreaksOff = false;
                }
            }
            if (frontOrderBreaksOff)
            {
                frontOrderBreaksOff = (sim->gameTime.value + unitInfo.id.value) % period == 0;
            }
        }

        if (!AirMovement::canDivertForRepair(
                unitInfo.definition->canFly,
                airPhysics != nullptr,
                inDogfight,
                frontOrderBreaksOff,
                aircraftWantsRepair(*unitInfo.state, *unitInfo.definition)))
        {
            return;
        }

        auto airBase = findAirBaseToLandOn(*sim, unitInfo);
        if (!airBase)
        {
            return;
        }

        unitInfo.state->clearWeaponTargets();
        dropAirAttackRun(unitInfo);
        unitInfo.state->orders.push_front(LandOnAirBaseOrder(*airBase));
    }

    bool UnitBehaviorService::handleLandOnAirBaseOrder(UnitInfo unitInfo, const LandOnAirBaseOrder& order)
    {
        if (!unitInfo.definition->canFly)
        {
            return true;
        }

        // "Landing aborted: no pads available" (0x501C30): VTOL_Landing
        // re-tests its pad before it commits (0x411DEB, calling 0x47E570),
        // and what that call tests is whether the pad is *free* -- it fails
        // on `pad+0x86` being set or on a match while walking the pad's list
        // at `pad+0x8a`. It is not an on/off test, which matters, because a
        // pad switched off after the aircraft set out does not turn it back:
        //
        //     "turning an aircraft repair pad 'Off' would stop aircraft from
        //     returning to it. However, those that were already making their
        //     way towards it will continue towards it."
        //
        // Both halves come from the split. New arrivals stop because the
        // query walks the owner's air base list, which holds only switched-on
        // pads (0x40ABCF). A trip under way carries on because the re-test on
        // the way in never asks about the switch. Distance does not cancel it
        // either -- the mission holds its target however far it has to go.
        auto padRef = sim->tryGetUnitState(order.target);
        if (!padRef)
        {
            return true;
        }
        const auto& pad = padRef->get();
        const auto& padDefinition = sim->unitDefinitions.at(pad.unitType);
        if (!pad.isOwnedBy(unitInfo.state->owner) || pad.isDead())
        {
            return true;
        }

        // Standing on it beats every claim. "...other damaged aircraft will not
        // use that pad until the occupying aircraft has been repaired and has
        // left", and the half that matters here is that the aircraft already
        // on the pad is the one that stays: this test used to run *after* the
        // claim test, so a later arrival with a lower unit id turned a parked
        // aircraft off its own pad with "no pads available".
        //
        // Flat distance, not three-dimensional: the aircraft is up on the
        // pad's deck, twenty world units above its base on an ARMASP, and
        // measuring that height against a reach sized from the footprint left
        // a parked aircraft not counting as parked.
        auto reach = airBaseRepairReach(*sim, padDefinition);
        auto parked = std::holds_alternative<UnitPhysicsInfoGround>(unitInfo.state->physics)
            && distanceSquaredXZ(unitInfo.state->position, pad.position) <= (reach * reach);

        // Somebody is on it. VTOL_Landing does not abort for that (S:130):
        // state 1 finds no free slot and circles the pad at weapon 0's range,
        // advancing a quarter turn a leg with a tolerance of 128, and the
        // order stays until a slot is free. The pre-filter that used to turn
        // such an aircraft away with "no pads available" is gone; the
        // original's own message belongs to state 5's failed attach, which
        // here is the same test and is never reached with the pad taken.
        if (!parked && airBaseIsClaimedByAnother(*sim, order.target, unitInfo.id))
        {
            auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
            if (airPhysics == nullptr)
            {
                // Standing somewhere else on the ground: get airborne first.
                if (auto groundPhysics = std::get_if<UnitPhysicsInfoGround>(&unitInfo.state->physics))
                {
                    groundPhysics->steeringInfo.shouldTakeOff = true;
                }
                return false;
            }

            if (auto landing = std::get_if<AirMovementStateLanding>(&airPhysics->movementState))
            {
                // Part way through setting down on a pad that has been
                // taken from under it: climb away rather than stack.
                landing->shouldAbort = true;
                return false;
            }
            if (std::holds_alternative<AirMovementStateTakingOff>(airPhysics->movementState))
            {
                return false;
            }

            if (!std::holds_alternative<AirMovementStateFlying>(airPhysics->movementState))
            {
                AirMovementStateFlying flying;
                flying.currentVelocity = airVelocity(airPhysics->movementState);
                airPhysics->movementState = flying;
                unitInfo.state->clearWeaponTargets();
            }
            auto& flying = std::get<AirMovementStateFlying>(airPhysics->movementState);

            if (!unitInfo.state->airLoiter || unitInfo.state->airLoiter->reason != UnitState::AirLoiterState::Reason::PadQueue)
            {
                // Entered at the bearing the aircraft is already on, so no
                // draw is taken.
                auto bearing = atan2(unitInfo.state->position.x - pad.position.x, unitInfo.state->position.z - pad.position.z);
                unitInfo.state->airLoiter = UnitState::AirLoiterState{UnitState::AirLoiterState::Reason::PadQueue, pad.position, bearing};
            }
            auto& loiter = *unitInfo.state->airLoiter;
            loiter.anchor = pad.position;

            // Weapon 0's range, or the unarmed ring's radius for an aircraft
            // with none. [open]
            const auto& primary = unitInfo.state->weapons[0];
            auto radius = primary
                ? sim->weaponDefinitions.at(primary->weaponType).maxRange
                : AirMovement::LoiterUnarmedRadius;

            auto station = AirMovement::loiterStation(loiter.anchor, loiter.bearing, radius);
            if (AirMovement::loiterArrived(unitInfo.state->position, station))
            {
                loiter.bearing = loiter.bearing - SimAngle(0x4000);
                station = AirMovement::loiterStation(loiter.anchor, loiter.bearing, radius);
            }
            station.y = getTargetAltitude(sim->terrain, station.x, station.z, *unitInfo.definition);
            flying.targetPosition = station;
            return false;
        }

        // A slot is free: the wait, if there was one, is over.
        if (unitInfo.state->airLoiter && unitInfo.state->airLoiter->reason == UnitState::AirLoiterState::Reason::PadQueue)
        {
            unitInfo.state->airLoiter = std::nullopt;
        }

        if (parked)
        {
            // SELFREPAIR (0x402430) in state 1 does nothing but compare the
            // aircraft's hit points against its maximum every tick, and only
            // when they meet does it advance, say "Unit repaired" and return
            // 5 -- deleting itself so the mission underneath resumes. The
            // mending is not its work: the pad does that, in the builder
            // path, which is why an aircraft on a switched-off pad sits there
            // indefinitely instead of taking off again.
            if (unitInfo.state->hitPoints >= unitInfo.definition->maxHitPoints)
            {
                // The aircraft's own "Unit repaired" (0x402491, and
                // VTOL_GetRepaired's at 0x415298).
                sim->events.push_back(UnitRepairedEvent{unitInfo.id});
                return true;
            }
            return false;
        }

        if (navigateTo(unitInfo, pad.position))
        {
            if (auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics))
            {
                if (auto flying = std::get_if<AirMovementStateFlying>(&airPhysics->movementState))
                {
                    flying->shouldLand = true;
                }
            }
        }

        return false;
    }

    bool UnitBehaviorService::handleOrder(UnitInfo unitInfo, UnitOrder& order)
    {
        // If an order that steers the aircraft itself arrives while it is
        // mid-AttackRun (e.g. the player issued Move, or the attack was
        // replaced), drop the run so the new order can drive Flying-mode
        // steering. Otherwise the aircraft freezes — the dispatchers for those
        // orders do not know how to drive a unit in AttackRun state.
        //
        // Patrol is the exception, because patrol engages: it calls
        // attackTarget itself the moment something hostile comes into sight,
        // and wiping the run here undid that every tick. The bomber rebuilt
        // its run from a standing start each tick and crawled forward at one
        // tick of acceleration, never reaching a release point. Patrol drops
        // the run in handlePatrolOrder instead, on the ticks it is really
        // flying the route.
        if (!std::holds_alternative<AttackOrder>(order) && !std::holds_alternative<PatrolOrder>(order))
        {
            dropAirAttackRun(unitInfo);
        }

        // Any order at all ends an idle circuit, so that when this one is done
        // the aircraft goes home as it should. A guard order is the exception:
        // it flies a circuit of its own and needs the bearing it has built up.
        // Patrol legs and occupied-pad circuits retain their navigation state.
        if (!std::holds_alternative<GuardOrder>(order)
            && !std::holds_alternative<PatrolOrder>(order)
            && !std::holds_alternative<LandOnAirBaseOrder>(order))
        {
            unitInfo.state->airLoiter = std::nullopt;
        }

        return match(
            order,
            [&](MoveOrder& o) {
                return handleMoveOrder(unitInfo, o);
            },
            [&](AttackOrder& o) {
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
            [&](ResurrectOrder& o) {
                return handleResurrectOrder(unitInfo, o);
            },
            [&](const RepairOrder& o) {
                return handleRepairOrder(unitInfo, o);
            },
            [&](const PatrolOrder& o) {
                return handlePatrolOrder(unitInfo, o);
            },
            [&](CaptureOrder& o) {
                return handleCaptureOrder(unitInfo, o);
            },
            [&](const LoadOrder& o) {
                return handleLoadOrder(unitInfo, o);
            },
            [&](UnloadOrder& o) {
                return handleUnloadOrder(unitInfo, o);
            },
            [&](const DgunOrder& o) {
                return handleDgunOrder(unitInfo, o);
            },
            [&](const LandOnAirBaseOrder& o) {
                return handleLandOnAirBaseOrder(unitInfo, o);
            });
    }

    bool UnitBehaviorService::withinBuildReach(UnitInfo unitInfo, const UnitState& target) const
    {
        const auto& targetDefinition = sim->unitDefinitions.at(target.unitType);
        return withinBuildReachOfRect(unitInfo, sim->computeFootprintRegion(target.position, targetDefinition.movementCollisionInfo));
    }

    bool UnitBehaviorService::withinBuildReachOfRect(UnitInfo unitInfo, const DiscreteRect& rect, SimScalar extraReach) const
    {
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
        auto reach = unitInfo.definition->buildDistance + extraReach;
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
            return AirMovement::loiterRadius(false, 0_ss);
        }
        return AirMovement::loiterRadius(true, sim->weaponDefinitions.at(weapon->weaponType).maxRange);
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
        unitInfo.state->airLoiter = UnitState::AirLoiterState{reason, anchor, AirMovement::loiterEntryBearing(sim->rng())};
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
        auto station = AirMovement::loiterStation(loiter.anchor, loiter.bearing, radius);

        if (AirMovement::loiterArrived(unitInfo.state->position, station))
        {
            // On station. Work the bearing round for the next one. The step is
            // more than a quarter turn and less than half of one, so successive
            // stations are joined by chords that pass close to the middle:
            // this is why a bomber keeps coming back over what it killed
            // rather than settling into a tidy orbit.
            loiter.bearing = AirMovement::nextLoiterBearing(loiter.bearing, stepBase, sim->rng());
            station = AirMovement::loiterStation(loiter.anchor, loiter.bearing, radius);
        }

        station.y = getTargetAltitude(sim->terrain, station.x, station.z, *unitInfo.definition);
        flying.targetPosition = station;
    }

    void UnitBehaviorService::flyIdleHop(UnitInfo unitInfo)
    {
        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr)
        {
            return;
        }
        auto flying = std::get_if<AirMovementStateFlying>(&airPhysics->movementState);
        if (flying == nullptr)
        {
            return;
        }

        // An airborne aircraft carrying something does not land (S:128).
        flying->shouldLand = false;

        const auto& position = unitInfo.state->position;
        if (!unitInfo.state->airLoiter || unitInfo.state->airLoiter->reason != UnitState::AirLoiterState::Reason::IdleHop)
        {
            // State 0 (0x40F9B8): the anchor is where the aircraft went idle,
            // taken as the integer x and z words of its position, so the hop
            // is about that spot and not about wherever it has drifted to.
            // The first hop follows on the tick state 2 is reached.
            SimVector anchor(
                SimScalar(std::floor(simScalarToFloat(position.x))),
                0_ss,
                SimScalar(std::floor(simScalarToFloat(position.z))));

            UnitState::AirLoiterState hop;
            hop.reason = UnitState::AirLoiterState::Reason::IdleHop;
            hop.anchor = anchor;
            hop.waypoint = anchor;
            hop.dueAt = sim->gameTime + GameTime(AirMovement::IdleHopFirstDelayTicks);
            unitInfo.state->airLoiter = hop;
            return;
        }

        auto& hop = *unitInfo.state->airLoiter;
        if (sim->gameTime >= hop.dueAt)
        {
            // Three draws, in the original's order (0x40F82F-0x40F8D4): the
            // bearing, then the distance, then the wait.
            auto bearingDraw = randomBelow(sim->rng, 0x10000u);
            auto radiusDraw = randomBelow(sim->rng, AirMovement::IdleHopRadiusSpan);
            auto timerDraw = randomBelow(sim->rng, AirMovement::IdleHopTimerSpan);

            hop.waypoint = AirMovement::idleHopWaypoint(hop.anchor, bearingDraw, radiusDraw);
            hop.dueAt = sim->gameTime + GameTime(AirMovement::IdleHopTimerMin + timerDraw);
        }

        // Full cruising altitude, no tolerance: the ordinary arrival test.
        auto target = hop.waypoint;
        target.y = getTargetAltitude(sim->terrain, target.x, target.z, *unitInfo.definition);
        flying->targetPosition = target;
    }

    bool UnitBehaviorService::flyAirPatrolLeg(UnitInfo unitInfo, const SimVector& waypoint)
    {
        auto& loiter = unitInfo.state->airLoiter;
        const auto& position = unitInfo.state->position;

        // State 2 re-derives the goal every time it runs (S:125): on the
        // 30-tick timer, and when the waypoint is a new one. It is a point on
        // the line from where the aircraft is now through the waypoint, 320
        // beyond it, at the waypoint's own height; x and z are not clamped to
        // the map, the original's goal constructor copies the vector as given.
        if (!loiter
            || loiter->reason != UnitState::AirLoiterState::Reason::Patrol
            || loiter->anchor != waypoint
            || sim->gameTime >= loiter->dueAt)
        {
            SimVector toWaypoint(waypoint.x - position.x, 0_ss, waypoint.z - position.z);
            // atan2(0, 0) is the bearing 0 (0x48A980).
            auto bearing = toWaypoint.lengthSquared() == 0_ss ? SimAngle(0) : UnitState::toRotation(toWaypoint);
            auto beyond = UnitState::toDirection(bearing) * AirMovement::PatrolOvershoot;

            UnitState::AirLoiterState leg;
            leg.reason = UnitState::AirLoiterState::Reason::Patrol;
            leg.anchor = waypoint;
            leg.waypoint = SimVector(waypoint.x + beyond.x, waypoint.y, waypoint.z + beyond.z);
            leg.dueAt = sim->gameTime + GameTime(AirMovement::PatrolReaimTicks);
            loiter = leg;
        }

        // Steer at the goal, not the waypoint.
        navigateTo(unitInfo, loiter->waypoint);

        // Still on the ground: state 0 takes off first, and a leg that began
        // within 336 of its goal must not be retired before it has flown.
        if (!std::holds_alternative<UnitPhysicsInfoAir>(unitInfo.state->physics))
        {
            return false;
        }

        // Complete when strictly inside the tolerance of the goal, every tick
        // (0x44E5B0): 16 short of the waypoint for an aircraft on the line.
        SimVector toGoal(loiter->waypoint.x - position.x, 0_ss, loiter->waypoint.z - position.z);
        return toGoal.lengthSquared() < AirMovement::PatrolArrivalTolerance * AirMovement::PatrolArrivalTolerance;
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
            // On the ground, face the job before working on it. Every work
            // mission in the original turns: the bearing to the target
            // (0x48A980, an atan2 over the two positions), less the unit's
            // own heading (unit+0x66), handed to the turn at 0x438590 --
            // whose nine callers are the build, repair, capture, reclaim and
            // resurrect handlers and the two aircraft ones. RWE turned only
            // aircraft, so a builder that was already in reach lathed from
            // whatever heading it happened to stop on.
            unitInfo.state->slowFacePoint = workPosition;

            // A builder that can turn finishes turning first. One that cannot
            // -- no turn rate at all -- must not be made to wait for a turn it
            // can never make.
            if (unitInfo.definition->turnRate > 0_ss)
            {
                auto direction = workPosition - unitInfo.state->position;
                direction.y = 0_ss;
                if (direction.lengthSquared() > 0_ss
                    && !angleBetweenIsLessOrEqual(unitInfo.state->rotation, UnitState::toRotation(direction), WorkFacingTolerance))
                {
                    return false;
                }
            }

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

        // An air transport that has already told its script to lower the hook
        // for this pickup folds the arms back when it gives up on it: the
        // abort arm of VTOL_Pickup state 4 (§36). Nothing is owed on the
        // crane path, whose scripts do not define EndTransport at all (§39).
        auto abandonPickup = [&]() {
            if (unitInfo.definition->canFly && unitInfo.state->transportScriptTarget)
            {
                unitInfo.state->cobEnvironment->createThread("EndTransport");
            }
            unitInfo.state->transportScriptTarget = std::nullopt;
        };

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
            abandonPickup();
            return true;
        }
        auto& target = targetRef->get();
        const auto& targetDefinition = sim->unitDefinitions.at(target.unitType);

        // The original's order-time predicate (0x489A90); the rules live on
        // the simulation so they can be asked from anywhere.
        auto [footprintX, footprintZ] = sim->getFootprintXZ(targetDefinition.movementCollisionInfo);
        if (!sim->canLoadUnitIntoTransport(unitInfo.id, loadOrder.target))
        {
            abandonPickup();
            return true;
        }

        // Every transport that is not an aircraft loads with the crane: the
        // hover transports' scripts are structural copies of the sea
        // transports', and the original routes any canfly=0 transport's LOAD
        // order to the same ground pickup mission.
        bool isShip = !unitInfo.definition->canFly;
        auto dx = unitInfo.state->position.x - target.position.x;
        auto dz = unitInfo.state->position.z - target.position.z;
        auto flatDistanceSquared = (dx * dx) + (dz * dz);

        // An air transport hovers over the unit; a ship's crane reaches the shore.
        auto pickupRange = isShip ? CraneReach : 24_ss + SimScalar(static_cast<float>(std::max(footprintX, footprintZ))) * MapTerrain::HeightTileWidthInWorldUnits;
        if (flatDistanceSquared > pickupRange * pickupRange)
        {
            // Only the transport moves. The original's Ground_Pickup
            // (0x406780, state 4) installs a move goal at the passenger's
            // position and nothing else: the passenger is never given an
            // order, and the crane reaches it from wherever the hull's own
            // path stops, which for a ship is the water's edge (TOTALA-EXE-
            // TRANSPORTS.md S:34). RWE used to walk the passenger out to a
            // point measured from the hull, which sent it wading into the sea
            // after a ship that was still far out and moving (issue #193).
            navigateTo(unitInfo, loadOrder.target);
            return false;
        }

        auto cargoHeight = sim->unitModelDefinitions.at(targetDefinition.objectName).height;

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

        // An air transport: VTOL_Pickup states 2 to 4 (§36, and §39's "Air
        // transports (canFly)"). Having arrived over the cargo at cruise
        // altitude it asks the script which piece the unit will hang from,
        // tells the script how far to lower that piece, and only then
        // descends -- to the altitude that leaves the lowered hook on the
        // unit. RWE ran the two calls the other way round until now: it
        // dropped to the unit, attached it, and started the animation
        // afterwards, so the Atlas reached down for cargo it was already
        // holding, and the hook came down on an empty patch of ground.

        // QueryTransport returns a piece id and nothing else, so asking again
        // on the tick of the attach is cheaper than carrying the answer
        // across ticks in unit state.
        std::string piece;
        if (auto pieceId = runCobQuery(unitInfo.id, "QueryTransport"))
        {
            const auto& pieces = unitInfo.state->cobEnvironment->_script->pieces;
            if (*pieceId >= 0 && static_cast<std::size_t>(*pieceId) < pieces.size())
            {
                piece = pieces[static_cast<std::size_t>(*pieceId)];
            }
        }

        // BeginTransport(h) goes out once for the pickup. The Atlas's is a
        // single move-now that puts the hook at y = -h, and h is the cargo's
        // model height: the original hands over the whole 16.16 dword at
        // targetdef+0x16E, which is the scaling a script also sees from
        // UNIT_HEIGHT. transportScriptTarget is what remembers the call has
        // gone out, the same way it does for the crane's TransportPickup.
        if (unitInfo.state->transportScriptTarget != loadOrder.target)
        {
            unitInfo.state->transportScriptTarget = loadOrder.target;
            unitInfo.state->transportScriptStartedAt = sim->gameTime;
            auto thread = unitInfo.state->cobEnvironment->createThread("BeginTransport", {CobPosition::fromFloat(simScalarToFloat(cargoHeight)).value});
            LOG_DEBUG << "Transport " << unitInfo.id.value << " BeginTransport(" << simScalarToFloat(cargoHeight) << ") " << (thread ? "started" : "not in script");
        }

        // Then the descent. The original's goal altitude is the negated
        // y-offset of the piece the script has just moved, which after
        // BeginTransport(h) lowered it by h is the cargo's own height above
        // the ground -- the hook comes to rest on the unit rather than the
        // hull coming down to it.
        SimVector hoverPoint(target.position.x, target.position.y + cargoHeight, target.position.z);
        if (!hoverTowards(unitInfo, hoverPoint))
        {
            return false;
        }

        // State 4: the engine does this attach itself, at the piece the
        // script named.
        sim->loadUnitIntoTransport(unitInfo.id, loadOrder.target, piece);
        unitInfo.state->transportScriptTarget = std::nullopt;
        return true;
    }

    bool UnitBehaviorService::handleUnloadOrder(UnitInfo unitInfo, UnloadOrder& unloadOrder)
    {
        LOG_DEBUG << "Transport " << unitInfo.id.value << " unload order: carrying " << unitInfo.state->carriedUnits.size() << ", floater " << unitInfo.definition->floater;
        if (unitInfo.state->carriedUnits.empty())
        {
            unitInfo.state->transportScriptTarget = std::nullopt;
            return true;
        }

        // Every non-flying transport unloads with the crane, hover ones
        // included -- their scripts are copies of the sea transports'.
        bool crane = !unitInfo.definition->canFly;

        // A refused air drop waits out its park before trying again (below).
        if (!crane && sim->gameTime < unloadOrder.parkedUntil)
        {
            return false;
        }

        auto dx = unitInfo.state->position.x - unloadOrder.destination.x;
        auto dz = unitInfo.state->position.z - unloadOrder.destination.z;
        // An aircraft needs room to stop, so it starts its descent from
        // further out than a ship's crane needs.
        auto dropRange = crane
            ? CraneReach
            : rweMax(32_ss, unitInfo.definition->maxVelocity * 8_ss);
        if ((dx * dx) + (dz * dz) > dropRange * dropRange)
        {
            navigateTo(unitInfo, unloadOrder.destination);
            return false;
        }

        if (crane)
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

        // An air transport asks whether its cargo can be set down before it
        // goes anywhere near the ground, and asks again as it lets go:
        // VTOL_Unload states 1 and 2 run the drop-legality test first
        // (0x411635, 0x4116F2, TOTALA-EXE.md S:36 and S:39). Refused, the
        // original says "Unable to unload unit" and parks the mission for
        // rand(30)+30 ticks before starting over (return 9), and it never
        // leaves cruise height for a drop it cannot make. RWE used to go down
        // first and ask only as it let go, and it went down to the ground
        // under the point -- the seabed, over water -- so an Atlas told to
        // unload a tank over the sea dived under it.
        //
        // RWE's test is the nearest clear footprint within twelve cells
        // rather than the original's one footprint on the point, and the
        // transport settles over the spot it found.
        auto carriedId = unitInfo.state->carriedUnits.front();
        auto refuse = [&]() {
            sim->events.push_back(UnitCannotComplyEvent{unitInfo.id, "Unable to unload unit"});
            unloadOrder.parkedUntil = sim->gameTime + GameTime(30u + randomBelow(sim->rng, 30u));
            return false;
        };

        auto spot = sim->findUnloadSpot(carriedId, unloadOrder.destination);
        if (!spot)
        {
            return refuse();
        }

        // Low over the spot, with the cargo's height between the transport
        // and the surface -- the water's, where the spot is under it, since
        // an amphibian is let go from above the sea and not carried down.
        auto cargoHeight = simScalarToFloat(sim->unitModelDefinitions.at(sim->unitDefinitions.at(sim->getUnitState(carriedId).unitType).objectName).height);
        auto surface = rweMax(spot->position.y, sim->terrain.getSeaLevel());
        SimVector hoverPoint(spot->position.x, surface + SimScalar(cargoHeight + HoverClearance), spot->position.z);
        if (!hoverTowards(unitInfo, hoverPoint))
        {
            return false;
        }

        // One unload order sets down one unit; the rest stay aboard until
        // they are ordered out in turn.
        if (!sim->unloadUnitFromTransport(unitInfo.id, carriedId, unloadOrder.destination))
        {
            return refuse();
        }
        unitInfo.state->cobEnvironment->createThread("EndTransport");
        return true;
    }

    bool UnitBehaviorService::handleMoveOrder(UnitInfo unitInfo, MoveOrder& moveOrder)
    {
        if (!unitInfo.definition->isMobile)
        {
            return false;
        }

        // BT-100: a Move whose goal no route reaches is waiting out its retry.
        // The unit asks for nothing this tick, so it stands, as the original's
        // mission does asleep with no path.
        if (moveOrder.unreachableRetryTicks > 0)
        {
            --moveOrder.unreachableRetryTicks;
            if (moveOrder.unreachableRetryTicks > 0)
            {
                return false;
            }
        }

        auto arrived = navigateTo(unitInfo, moveOrder.destination);

        // BT-100 (0x4031D0, 0x44F080, and the executor's result-9 arm at
        // 0x43B8E5): when the search finds no route and the unit is not
        // already at the goal, the Move mission is woken with bit 0x40, which
        // is not arrival, and returns 9. The executor then deletes the node if
        // another follows, and otherwise resets it to state 0 and sleeps 30
        // plus a random 0 to 29 ticks before asking for the same goal again --
        // so a lone unreachable Move never ends. RWE's pathfinder records the
        // closest reachable point as reachableDestination, and a point goal
        // used to count as reached there, which ended the order. A ground
        // unit standing on the real goal square has still arrived.
        if (!unitInfo.definition->canFly)
        {
            auto moving = std::get_if<NavigationStateMoving>(&unitInfo.state->navigationState.state);
            if (moving != nullptr && moving->reachableDestination)
            {
                auto goalPoint = std::get_if<SimVector>(&moving->movementGoal);
                if (goalPoint != nullptr && *goalPoint == moveOrder.destination
                    && !isOnGoalSquare(*sim, *unitInfo.state, *unitInfo.definition, moveOrder.destination))
                {
                    if (unitInfo.state->orders.size() > 1)
                    {
                        // The next order runs; the caller pops this one.
                        return true;
                    }

                    // The draw is a modulo of the generator's raw output, never
                    // uniform_int_distribution (CLAUDE.md, Determinism).
                    moveOrder.unreachableRetryTicks = 30u + randomBelow(sim->rng, 30u);
                    unitInfo.state->navigationState.desiredDestination = std::nullopt;
                    return false;
                }
            }
        }

        if (arrived)
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

    bool UnitBehaviorService::handleAttackOrder(UnitInfo unitInfo, AttackOrder& attackOrder)
    {
        // A unit that picked this target for itself gives up once it is
        // maneuverleashlength from the spot where it first saw it. Every
        // attack handler in the original opens with this test -- 0x4034D2 for
        // ground, 0x412084 in AirStrike, 0x41284E in AirToGround, 0x413675 in
        // the gunship's -- against an anchor written at 0x43B330. The
        // distance is flat and in whole world units.
        if (attackOrder.leash && attackOrder.leash->distance > 0_ss)
        {
            auto dx = unitInfo.state->position.x - attackOrder.leash->anchor.x;
            auto dz = unitInfo.state->position.z - attackOrder.leash->anchor.z;
            if ((dx * dx) + (dz * dz) >= attackOrder.leash->distance * attackOrder.leash->distance)
            {
                unitInfo.state->clearWeaponTargets();
                dropAirAttackRun(unitInfo);
                return true;
            }
        }

        return attackTarget(unitInfo, attackOrder.target, attackOrder.lastSeenPosition);
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

    /**
     * See the declaration in UnitBehaviorService.h for what this is for
     * (issue #66: a group ordered to attack one unit was sending every
     * attacker at the same cell).
     *
     * Why the goal has to stay stable rather than tracking the attacker's
     * exact position every tick: groundUnitMoveTo only keeps following its
     * current path when the newly-resolved goal compares equal to the one
     * the path was built for (`movingState->movementGoal != goal`); a goal
     * that differs even fractionally from one tick to the next throws the
     * path away and re-enters PathFindingService's request queue every
     * tick, for every attacker -- the same queue this change exists to stop
     * flooding, and can even ask for a second path for a unit whose first
     * request is still the scheduler's active search, which is exactly the
     * ordering PathFindingService::update asserts cannot happen. A large
     * group crowding the same approach is exactly when searches take more
     * than one tick to finish, so it is exactly when that matters.
     *
     * A plain UnitId goal already has this problem solved, and not merely by
     * being cheap to compare: for a stationary target it is invariant.
     * groundUnitMoveTo's identity check never resets the path (the goal is
     * just the id), and the position it resolves to, through
     * getUnitPositionWithCache, comes back bit-identical to what it was
     * before as long as the target has not actually moved -- so
     * `resolvedDestination != movingState->pathDestination` is false and no
     * second request ever goes in. A point on a bearing from the attacker's
     * own position does not get that for free, because the attacker is the
     * one moving.
     *
     * So it is cached the same way, but in a slot of its own rather than
     * sharing unitPositionCache: that one holds the target's own position
     * under the very same key, and a capture order on a unit just attacked
     * would have read this approach point back as the target's position.
     * It is invalidated on whether the target has moved rather than on a
     * timer: the cached point is only good as long as it is still
     * standoffDistance from the target's current position, which is exactly
     * what changes if the target does and never changes if it does not. A
     * stationary target -- the common case, and every case this fixes --
     * therefore gets exactly one approach point and one path request per
     * attacker for the whole engagement, same as the old UnitId goal did;
     * a moving one gets a fresh bearing only when it has actually moved far
     * enough to matter.
     */
    NavigationGoal UnitBehaviorService::attackApproachGoal(UnitInfo unitInfo, UnitId targetId, const SimVector& targetPosition, const WeaponDefinition& weaponDefinition) const
    {
        auto targetUnitRef = sim->tryGetUnitState(targetId);
        if (!targetUnitRef)
        {
            // The caller already resolved a position for this target this
            // tick, so it cannot actually have gone away; fall back to the
            // plain goal, which does no worse than before this change.
            return targetId;
        }
        const auto& targetUnit = targetUnitRef->get();
        const auto& targetDefinition = sim->unitDefinitions.at(targetUnit.unitType);

        auto footprintRect = sim->computeFootprintRegion(targetPosition, targetDefinition.movementCollisionInfo);
        auto footprintHalfWidth = (SimScalar(static_cast<float>(footprintRect.width)) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss;
        auto footprintHalfHeight = (SimScalar(static_cast<float>(footprintRect.height)) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss;
        auto footprintStandoff = rweMax(footprintHalfWidth, footprintHalfHeight) + AttackApproachFootprintMargin;

        auto rangeStandoff = weaponDefinition.maxRange > AttackApproachRangeMargin
            ? weaponDefinition.maxRange - AttackApproachRangeMargin
            : 0_ss;

        // Never closer than the footprint allows, even if that means
        // standing outside the weapon's own range -- a short-ranged unit
        // ordered at something too large to reach at all still has one
        // consistent place to walk to rather than trying to sit inside it.
        auto standoffDistance = rweMax(footprintStandoff, rangeStandoff);

        auto& cache = unitInfo.state->navigationState.attackApproachCache;
        if (cache && cache->unitId == targetId)
        {
            // A band around the stand-off distance rather than exact
            // equality: the cached point came from multiplying by a
            // normalised direction, and normalising is a division by a
            // square root, so it does not round-trip to bit-exact float
            // equality even when nothing has moved. Eight units is the
            // same tolerance hasReachedGoal accepts as arrival, and
            // comfortably clears that rounding while still catching any
            // real movement worth reacting to.
            //
            // A band, and not |d^2 - s^2| against the tolerance squared:
            // that difference factors as (d - s)(d + s), which would make
            // the tolerance actually applied to the target's movement
            // 64/(2s) -- about a sixth of a unit at a stand-off of 200,
            // rather than the eight intended -- and would throw the cache
            // away on almost every tick for a target that was moving at
            // all, which is the case that most needs its path request left
            // alone.
            auto lowerBound = standoffDistance > AttackApproachDriftTolerance
                ? standoffDistance - AttackApproachDriftTolerance
                : 0_ss;
            auto upperBound = standoffDistance + AttackApproachDriftTolerance;
            auto driftSquared = cache->position.distanceSquared(targetPosition);
            if (driftSquared > (lowerBound * lowerBound) && driftSquared < (upperBound * upperBound))
            {
                return cache->position;
            }
        }

        auto offset = unitInfo.state->position - targetPosition;
        offset.y = 0_ss;
        auto direction = offset.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));

        auto approachPoint = targetPosition + (direction * standoffDistance);
        approachPoint.y = targetPosition.y;

        cache = UnitPositionCache{targetId, approachPoint, sim->gameTime};
        return approachPoint;
    }

    bool UnitBehaviorService::weaponCanHitUnit(const WeaponDefinition& weaponDefinition, const UnitState& attacker, const UnitState& target) const
    {
        return sim->weaponCanHitUnit(weaponDefinition, attacker, target);
    }

    std::optional<UnitId> UnitBehaviorService::chooseTarget(UnitId id, unsigned int weaponIndex, TargetSearchMode mode)
    {
        RWE_SIMPROF("b.choose");
        const auto& unit = sim->getUnitState(id);
        const auto& unitDefinition = sim->unitDefinitions.at(unit.unitType);
        const auto& badCategory = unitDefinition.badTargetCategory.at(weaponIndex);

        // A kamikaze unit skips 0x49ABB0, the weapon's range and eligibility
        // test, altogether ("Kamikaze units skip it entirely",
        // TOTALA-EXE-WEAPONS.md §9). The only kamikaze units that search are
        // Core Contingency's mines, which have no weapon at all and ask the
        // sight-range question from Standby_Mine; for them there is no
        // weapon to read, and the pool is simply what they can see.
        const auto& weaponSlot = unit.weapons[weaponIndex];
        const WeaponDefinition* weaponDefinition = weaponSlot ? &sim->weaponDefinitions.at(weaponSlot->weaponType) : nullptr;
        const auto skipsEligibility = unitDefinition.kamikaze;
        if (weaponDefinition == nullptr && !(skipsEligibility && mode == TargetSearchMode::SightDistance))
        {
            return std::nullopt;
        }

        // The candidate pool comes out of 0x40AD80 with a radius that depends
        // on what is being asked: the weapon's own range when a gun is looking
        // for something to shoot at, the unit's SightDistance when the
        // question is whether to abandon its post and go hunting. For most
        // units the second is much the larger, but not for all -- a Freedom
        // Fighter sees 350 and shoots 510.
        // ...but the sight-range question is bounded by the gun as well.
        // 0x40B7B0's mode-0 branch gathers candidates in a circle of
        // SightDistance (0x40B848), and then every candidate has to survive
        // 0x49ABB0 at 0x40B914 -- the weapon eligibility test, which ends in
        // its own range check. So the radius a unit will actually leave its
        // post for is the smaller of the two, and for many units that is the
        // gun: a Peewee sees 280 and breaks off at 180.
        const auto sightRadius = SimScalar(static_cast<float>(unitDefinition.sightDistance));
        auto searchRadius = mode == TargetSearchMode::SightDistance
            ? (skipsEligibility || weaponDefinition == nullptr ? sightRadius : rweMin(sightRadius, weaponDefinition->maxRange))
            : weaponDefinition->maxRange;

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

        // The pool comes out of the spatial index rather than out of a walk
        // of every unit in the game. That walk was the engine's single
        // largest cost in a real fight: at 800 units on Painted Desert it was
        // 10.0ms of the 11.8ms the behaviour pass spent per tick, over the
        // ~694 searches a tick, which is the O(n^2) the profile shows.
        //
        // The index hands back candidates in ascending unit id, which is the
        // order iterating the unit list visits them in, so the draws from the
        // random generator below fall on the same units in the same sequence
        // as they did before. What comes back is a superset of what is really
        // in range -- positions in the index are as of the start of the tick
        // and the reach is widened to cover a tick of movement -- so every
        // test the whole-list walk made is still made here, against the live
        // unit, and only the candidates that could not possibly matter are
        // skipped.
        const auto& candidates = sim->getUnitSpatialIndex().collectEnemiesNear(
            simScalarToFloat(unit.position.x),
            simScalarToFloat(unit.position.z),
            simScalarToFloat(searchRadius),
            unit.owner);

        for (auto otherUnitId : candidates)
        {
            auto otherUnitRef = sim->units.tryGet(otherUnitId);
            if (!otherUnitRef)
            {
                continue;
            }
            const auto& otherUnit = otherUnitRef->get();

            // The index has already dropped the searcher's own side; a unit
            // it does not know is dead has still to be checked for here.
            if (otherUnit.isDead())
            {
                continue;
            }

            // Range before the definition lookup, which the whole-list
            // version did the other way round. None of these rejections
            // draws from the random generator, so which order they are
            // applied in is not observable -- and the lookup is of a unit
            // type name in a hash map, many times the cost of a squared
            // distance.
            //
            // Flat, like every range test the original has (S:127, 0x40AD80):
            // a target directly overhead is as near as one beside the gun.
            if (!withinFlatRange(unit.position, otherUnit.position, searchRadius))
            {
                continue;
            }

            // A teammate's unit is not a candidate. The original's list
            // builder 0x40AA40 tests the searcher's ally byte for the
            // candidate's player (0x40AB05) before it asks whether the
            // candidate can even be seen, and skips the unit when it is set.
            // The index only dropped the searcher's own side (issue #237).
            if (sim->arePlayersAllied(unit.owner, otherUnit.owner))
            {
                continue;
            }

            // Nor is a mission's Immune unit (0x40AB24): the gates the
            // campaign puts down on the map are not something to shoot at
            // unprompted.
            if (otherUnit.immune)
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

            // NoChaseCategory is about leaving your post, not about what you
            // may shoot: the original only consults it when the third argument
            // is zero (0x40B8E6). Forty-four of the shipped units name VTOL
            // there and carry on with what they were doing rather than setting
            // off after an aircraft, but they still shoot at one in passing.
            if (mode == TargetSearchMode::SightDistance
                && categoryListContains(otherUnitDefinition.category, unitDefinition.noChaseCategory))
            {
                continue;
            }

            // Only what the owner can actually see is fair game, and a torpedo
            // cannot reach something standing on land. Seeing it, not holding
            // it on radar: 0x40AA40 puts every candidate through 0x465AC0
            // before the list this scan walks is built, and that predicate
            // never reads the radar bit.
            if (!sim->canSeeUnit(unit.owner, otherUnitId)
                || (!skipsEligibility && !weaponCanHitUnit(*weaponDefinition, unit, otherUnit)))
            {
                continue;
            }

            // The draw is over whole world units squared, and the original's
            // random number generator returns zero for anything under two.
            // The two squares are floored separately (0x40B7B0, S:127).
            // Taken modulo rather than through a uniform_int_distribution,
            // whose bias correction is implementation-defined and would put
            // two builds of the engine out of step with one another.
            auto range = static_cast<int>(flatRangeScore(unit.position, otherUnit.position));
            auto score = 0;
            if (range > 1)
            {
                score = static_cast<int>(sim->rng() % static_cast<unsigned int>(range));
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

    std::optional<SimVector> UnitBehaviorService::resolveAttackTargetPosition(UnitInfo unitInfo, const AttackTarget& target, std::optional<SimVector>& lastSeenPosition)
    {
        // See the declaration for the rule and where it came from.
        auto targetUnitId = std::get_if<UnitId>(&target);
        if (targetUnitId == nullptr)
        {
            // A ground target is a place, and a place does not hide.
            return getTargetPosition(target);
        }

        // A target that has died ends the order however well we remember where
        // it was: the remembered position is for a unit out of sight, not for
        // one that is gone.
        auto targetRef = sim->tryGetUnitState(*targetUnitId);
        if (!targetRef || targetRef->get().isDead())
        {
            return std::nullopt;
        }

        if (sim->canSeeUnit(unitInfo.state->owner, *targetUnitId))
        {
            auto livePosition = tryGetSweetSpot(*targetUnitId);
            lastSeenPosition = livePosition;
            return livePosition;
        }

        if (lastSeenPosition)
        {
            return lastSeenPosition;
        }

        // A new order against an unseen unit has no position to act on.
        // Wait for sight rather than leaking its current location.
        return std::nullopt;
    }

    bool UnitBehaviorService::attackTarget(UnitInfo unitInfo, const AttackTarget& target, std::optional<SimVector>& lastSeenPosition)
    {
        // Where this attacker believes its target is, which is not where the
        // target is once it has gone into the fog. Resolved before anything
        // else so that every path below -- the crawling bomb, the aircraft,
        // the ground approach -- works from the same answer.
        auto targetPosition = resolveAttackTargetPosition(unitInfo, target, lastSeenPosition);

        if (auto targetId = std::get_if<UnitId>(&target))
        {
            auto targetRef = sim->tryGetUnitState(*targetId);
            if (targetRef && !targetRef->get().isDead() && !sim->canSeeUnit(unitInfo.state->owner, *targetId))
            {
                unitInfo.state->clearWeaponTargets();
                if (targetPosition)
                {
                    // Search only the remembered point. Do not consult the
                    // hidden target's live position, range or movement type.
                    if (withinFlatRange(unitInfo.state->position, *targetPosition, 32_ss))
                    {
                        return true;
                    }
                    navigateTo(unitInfo, *targetPosition);
                }
                return false;
            }
        }

        // A crawling bomb has no weapon to aim, so this has to come first. The
        // original turns an attack order on one of these into its own mission,
        // ATTACK_KAMIKAZE (0x43F38A), whose handler at 0x403336 walks the unit
        // to within kamikazedistance of the target and, on arrival, issues the
        // ordinary SELFDESTRUCT order (0x4032E4).
        if (unitInfo.definition->kamikaze)
        {
            return kamikazeRun(unitInfo, target, targetPosition);
        }

        if (!unitInfo.state->weapons[0])
        {
            return true;
        }

        // Which of the first two slots carries the chase (BT-63: the order
        // builder works from those two and never the third). Slot 0 unless it
        // cannot reach the target and an enabled second slot can -- a
        // Crusader's depth charge in Weapon2 against a submerged submarine,
        // which the order builder accepts and whose chase this keeps going
        // instead of finishing on the first tick.
        std::size_t chaseSlot = 0;

        // A water weapon ordered at a unit on dry land has nothing to do.
        if (auto targetUnitId = std::get_if<UnitId>(&target))
        {
            auto targetUnit = sim->tryGetUnitState(*targetUnitId);
            const auto& weaponDefinition = sim->weaponDefinitions.at(unitInfo.state->weapons[0]->weaponType);
            if (targetUnit && !weaponCanHitUnit(weaponDefinition, *unitInfo.state, targetUnit->get()))
            {
                if (!unitInfo.definition->canFly
                    && unitInfo.state->weapons[1]
                    && weaponCanHitUnit(sim->weaponDefinitions.at(unitInfo.state->weapons[1]->weaponType), *unitInfo.state, targetUnit->get()))
                {
                    chaseSlot = 1;
                }
                else
                {
                    return true;
                }
            }
        }
        else if (!sim->weaponCanFireFrom(sim->weaponDefinitions.at(unitInfo.state->weapons[0]->weaponType), *unitInfo.state))
        {
            // A shot at a point has no target to test, but the shooter's half
            // of the water rule still holds: a gun under the sea does not fire
            // at the ground either (NAVAL-CONSTRUCTION-DGUN-FINDINGS.md).
            return true;
        }

        // Aircraft execute attack runs rather than the ground-unit
        // approach/aim/fire pattern.
        if (unitInfo.definition->canFly)
        {
            return attackTargetAir(unitInfo, target, targetPosition);
        }

        const auto& weaponDefinition = sim->weaponDefinitions.at(unitInfo.state->weapons[chaseSlot]->weaponType);

        if (!targetPosition)
        {
            return true;
        }

        // The chase distance is flat too (S:127, 0x4034A0).
        if (!withinFlatRange(unitInfo.state->position, *targetPosition, weaponDefinition.maxRange))
        {
            // A unit target gets a stand-off point of its own rather than
            // the target's bare centre -- see attackApproachGoal. A ground
            // point (an attack-move order with no unit behind it) has
            // nothing to stand off from and no footprint to collide two
            // attackers on in the first place, so it keeps going straight at
            // the point as before.
            if (auto targetUnitId = std::get_if<UnitId>(&target))
            {
                navigateTo(unitInfo, attackApproachGoal(unitInfo, *targetUnitId, *targetPosition, weaponDefinition));
            }
            else
            {
                navigateTo(unitInfo, attackTargetToNavigationGoal(target));
            }
        }
        else
        {
            for (unsigned int i = static_cast<unsigned int>(chaseSlot); i < 2; ++i)
            {
                match(
                    target,
                    [&](const UnitId& u) { unitInfo.state->setWeaponTarget(i, u); },
                    [&](const SimVector& v) { unitInfo.state->setWeaponTarget(i, v); });
            }
        }

        return false;
    }

    void UnitBehaviorService::notifyAimScriptStarted(UnitId id, const UnitState& unit, const std::string& functionName, int heading, int pitch)
    {
        if (!sim->taLiveSender.attached() || sim->taLiveSender.aimScriptStarted == nullptr || !unit.cobEnvironment)
        {
            return;
        }
        const auto* script = unit.cobEnvironment->script();
        if (script == nullptr)
        {
            return;
        }
        for (std::size_t i = 0; i < script->functions.size(); ++i)
        {
            if (script->functions[i].name == functionName)
            {
                sim->taLiveSender.aimScriptStarted(sim->taLiveSender.context, id, static_cast<unsigned int>(i), heading, pitch);
                return;
            }
        }
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

    bool UnitBehaviorService::kamikazeRun(UnitInfo unitInfo, const AttackTarget& target, const std::optional<SimVector>& targetPosition)
    {
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

    bool UnitBehaviorService::attackTargetAir(UnitInfo unitInfo, const AttackTarget& target, const std::optional<SimVector>& targetPosition)
    {
        // The position is resolved by the caller, and is the last one this
        // aircraft's owner saw if the target is in fog. Empty means the target
        // has gone away, and the order goes with it.
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
                // A dogfight leaves weapon 0 pointed at the bandit and never
                // takes it off (0x412FC3 has no matching clear anywhere in
                // the handler), so the aim has to be dropped here with the
                // state.
                unitInfo.state->clearWeaponTargets();
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

        // An aircraft after another aircraft is a different mission
        // entirely: 0x43F2CB gives AIRTOAIR to anything whose weapon is not
        // dropped when the target can fly. A bomber gets no mission at all
        // for the same target -- see the bomb branch just below -- so the
        // two halves of this test read together as one rule: what a flying
        // target becomes depends on whether the weapon is dropped.
        if (auto targetUnitId = std::get_if<UnitId>(&target))
        {
            if (!std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType))
            {
                if (auto targetState = sim->tryGetUnitState(*targetUnitId))
                {
                    if (sim->unitDefinitions.at(targetState->get().unitType).canFly)
                    {
                        return dogfightTarget(unitInfo, target, *targetUnitId, weaponDefinition.maxRange);
                    }
                }
            }
            else if (auto targetState = sim->tryGetUnitState(*targetUnitId))
            {
                if (sim->unitDefinitions.at(targetState->get().unitType).canFly)
                {
                    // And a bomber sent at an aircraft is not AIRTOAIR either --
                    // it is nothing. 0x43F2AA skips AIRSTRIKE for a flying
                    // target too, so execution falls through 0x43F2CB ->
                    // 0x43F2F0 -> 0x43F31B and lands in 0x4401DC, whose entire
                    // body is `mov eax,[esp+0x14] / mov [eax],0 / ret` -- it
                    // clears the mission-created flag and hands back nothing.
                    // This order came from a patrol sighting (handlePatrolOrder
                    // pushes AttackOrder unconditionally, with no test for
                    // whether the quarry flies), so dropping it here is exactly
                    // that "no mission": the patrol underneath resumes on its
                    // own, without ever having built an attack run to chase
                    // the aircraft with. See TOTALA-EXE.md §86.
                    unitInfo.state->clearWeaponTargets();
                    return true;
                }
            }
        }

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
            // A gun-armed aircraft flies the original's strafing pass
            // (`AirToGround`, 0x412710) rather than the bomber's run: it
            // overshoots to three weapon ranges past the target and breaks
            // ninety degrees before coming round. A dropped weapon keeps
            // `AirStrike`'s modest run-out -- a bomb's 1280 range would send
            // the aircraft clean off the map.
            // An air target never reaches here any more -- it is sent to
            // the dogfight above -- so this is the ground case only.
            auto strafingPass = !std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType);
            auto runOutDistance = strafingPass
                ? weaponDefinition.maxRange * 3_ss
                : defaultAttackRunOutDistance(*unitInfo.definition, weaponDefinition.maxRange);

            // Cache target position at cruise altitude so steering doesn't dive.
            auto altitude = getTargetAltitude(sim->terrain, targetPosition->x, targetPosition->z, *unitInfo.definition);
            airPhysics->movementState = AirMovement::beginAttackRun(
                target,
                SimVector(targetPosition->x, altitude, targetPosition->z),
                unitInfo.state->rotation,
                flying->currentVelocity,
                strafingPass,
                runOutDistance);
        }

        auto attackRun = std::get_if<AirMovementStateAttackRun>(&airPhysics->movementState);
        if (attackRun == nullptr)
        {
            // Defensive: nothing to do.
            return false;
        }

        attackRun->target = target;

        // BOUNDARY-004: strafing is the exception among the attack missions.
        // AirToGround (0x412710) does not aim at the centre off the grid: it
        // sets a 30-tick deadline and forces state 2, the attack state, whose
        // goal is the target's current position with the weapon range as its
        // approach radius. RWE's Approaching phase is that goal, so an
        // off-grid strafing run is re-aimed on the target every 30 ticks and
        // keeps attacking from outside. The run, its target and the order are
        // all retained. A bomber (strafingPass false) takes no branch at all.
        const bool outsideGrid = airIsOutsideGrid(unitInfo);
        if (outsideGrid && attackRun->strafingPass)
        {
            if (!attackRun->overflowRepathAt || sim->gameTime >= *attackRun->overflowRepathAt)
            {
                attackRun->overflowRepathAt = sim->gameTime + GameTime(30);
                attackRun->phase = AirMovementStateAttackRun::Phase::Approaching;
                attackRun->bombsDroppedThisPass = 0;
                attackRun->releasePoint.reset();
            }
        }
        else if (!outsideGrid)
        {
            // Back inside: a later excursion starts a fresh deadline rather
            // than inheriting whatever was left of this one.
            attackRun->overflowRepathAt.reset();
        }

        // Refresh the cached target position (target may move) and step the
        // phase machine.
        auto altitude = getTargetAltitude(sim->terrain, targetPosition->x, targetPosition->z, *unitInfo.definition);
        auto step = AirMovement::stepAttackRun(
            *attackRun,
            *unitInfo.state,
            *unitInfo.definition,
            SimVector(targetPosition->x, altitude, targetPosition->z),
            *targetPosition,
            weaponDefinition.maxRange);

        // The strafing pass's ninety-degree break. The phase machine has no
        // dice of its own, so the side is drawn here: the original takes
        // `rand(2)` and turns the heading by a quarter turn either way, then
        // flies one weapon range. Drawn straight off the simulation's rng so
        // every peer breaks the same way.
        if (attackRun->strafingPass
            && step.previousPhase == AirMovementStateAttackRun::Phase::Departing
            && attackRun->phase == AirMovementStateAttackRun::Phase::Approaching)
        {
            auto side = (sim->rng() % 2u) == 0u;
            attackRun->breakWaypoint = AirMovement::strafeBreakWaypoint(
                unitInfo.state->position,
                attackRun->currentVelocity,
                unitInfo.state->rotation,
                weaponDefinition.maxRange,
                side);
            attackRun->phase = AirMovementStateAttackRun::Phase::Breaking;
        }

        // If we just transitioned into Engaging, capture the run-out direction
        // after stepping. Approaching's phase change to Engaging happens in
        // stepAttackRunPhase.
        AirMovement::captureRunOutDirection(
            *attackRun,
            step.previousPhase,
            unitInfo.state->position,
            *targetPosition,
            unitInfo.state->rotation);

        if (step.weaponsHot)
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

    bool UnitBehaviorService::dogfightTarget(UnitInfo unitInfo, const AttackTarget& target, UnitId targetId, SimScalar weaponMaxRange)
    {
        auto airPhysics = std::get_if<UnitPhysicsInfoAir>(&unitInfo.state->physics);
        if (airPhysics == nullptr)
        {
            return false;
        }

        auto bandit = sim->tryGetUnitState(targetId);
        if (!bandit)
        {
            return true;
        }
        const auto& banditState = bandit->get();
        const auto& banditDefinition = sim->unitDefinitions.at(banditState.unitType);

        // Take over from whatever it was doing. Starting again on a new
        // target matters here: every number in the state is measured against
        // one particular aircraft.
        auto existing = std::get_if<AirMovementStateDogfight>(&airPhysics->movementState);
        if (existing == nullptr || existing->target != target)
        {
            AirMovementStateDogfight dogfight(target);
            dogfight.currentVelocity = airVelocity(airPhysics->movementState);
            dogfight.goalPosition = banditState.position;
            dogfight.nextDecision = sim->gameTime;
            airPhysics->movementState = dogfight;
        }

        auto dogfight = std::get_if<AirMovementStateDogfight>(&airPhysics->movementState);
        if (dogfight == nullptr)
        {
            return false;
        }
        dogfight->target = target;

        // Weapon 0 is held on the bandit for the whole engagement and never
        // let go (0x412FC3, and there is no matching clear anywhere in the
        // handler); the other two are left free to pick their own targets.
        // Range, aim and reload are the weapon update's business, exactly as
        // they are for the strafing pass -- this mission has no range test of
        // its own at all.
        unitInfo.state->setWeaponTarget(0, targetId);

        SimVector heading = dogfight->currentVelocity;
        heading.y = 0_ss;
        if (heading.lengthSquared() == 0_ss)
        {
            heading = UnitState::toDirection(unitInfo.state->rotation);
        }

        // Off the map, and nothing else matters until it is back. The
        // original checks this before it even looks at the leash (0x412E5E),
        // and sends the aircraft 800 units towards the rendered map centre
        // without touching any of the state -- so the pursuit picks up where
        // it left off. Two fighters working each other over will drift a long
        // way otherwise: an extend runs thirty times top speed ahead and a
        // break goes two weapon ranges out.
        if (airIsOutsideGrid(unitInfo))
        {
            dogfight->goalPosition = aircraftBoundaryRecoveryGoal(sim->terrain, unitInfo.state->position);
            dogfight->goalVelocity = SimVector(0_ss, 0_ss, 0_ss);
            return false;
        }

        SimVector toBandit(
            banditState.position.x - unitInfo.state->position.x,
            0_ss,
            banditState.position.z - unitInfo.state->position.z);

        auto beginBreak = [&]() {
            // Ninety degrees to a side drawn once, one weapon range out; the
            // second leg goes twice as far the same way (0x413CB8 onwards).
            dogfight->breakLeft = (sim->rng() % 2u) == 0u;
            dogfight->phase = AirMovementStateDogfight::Phase::BreakingOut;
            dogfight->offNoseCounter = 0;
            auto quarter = SimAngle(16384);
            auto facing = UnitState::toRotation(heading);
            auto away = UnitState::toDirection(dogfight->breakLeft ? facing - quarter : facing + quarter);
            dogfight->breakWaypoint = SimVector(
                unitInfo.state->position.x + (away.x * weaponMaxRange),
                unitInfo.state->position.y,
                unitInfo.state->position.z + (away.z * weaponMaxRange));
        };

        // A break flies its leg and is not reconsidered until it arrives.
        if (dogfight->phase == AirMovementStateDogfight::Phase::BreakingOut
            || dogfight->phase == AirMovementStateDogfight::Phase::BreakingAway)
        {
            SimVector toWaypoint(
                dogfight->breakWaypoint.x - unitInfo.state->position.x,
                0_ss,
                dogfight->breakWaypoint.z - unitInfo.state->position.z);

            // The break's own goal carries a tolerance of 128 (0x413D40).
            if (toWaypoint.lengthSquared() > (128_ss * 128_ss))
            {
                return false;
            }

            if (dogfight->phase == AirMovementStateDogfight::Phase::BreakingOut)
            {
                auto quarter = SimAngle(16384);
                auto facing = UnitState::toRotation(heading);
                auto away = UnitState::toDirection(dogfight->breakLeft ? facing - quarter : facing + quarter);
                auto second = weaponMaxRange * 2_ss;
                dogfight->breakWaypoint = SimVector(
                    unitInfo.state->position.x + (away.x * second),
                    unitInfo.state->position.y,
                    unitInfo.state->position.z + (away.z * second));
                dogfight->phase = AirMovementStateDogfight::Phase::BreakingAway;
                return false;
            }

            // Both legs flown: the evade mission deletes itself and AirToAir
            // starts over from its first state (0x413BF1 case 2, then 0x413397).
            dogfight->phase = AirMovementStateDogfight::Phase::Pursuing;
            dogfight->nextDecision = sim->gameTime;
            dogfight->goalPosition = banditState.position;
            dogfight->goalVelocity = SimVector(0_ss, 0_ss, 0_ss);
            return false;
        }

        // Has the goal been reached? The original's goal is satisfied within
        // 48 units in x and z (0x44EB60, a hard-coded float with no setter).
        SimVector toGoal(
            dogfight->goalPosition.x - unitInfo.state->position.x,
            0_ss,
            dogfight->goalPosition.z - unitInfo.state->position.z);
        auto reachedGoal = toGoal.lengthSquared() <= (48_ss * 48_ss);

        if (reachedGoal)
        {
            // An overshoot. With the bandit still in front, run on for a
            // second and come back at him; with him behind, break off.
            if (!targetIsAhead(heading, toBandit))
            {
                beginBreak();
                return false;
            }

            // Thirty times top speed ahead, receding at top speed: a point
            // that cannot be caught, which is the point -- the leg ends on
            // its timer (0x41305A).
            auto v = unitInfo.definition->maxVelocity;
            auto nose = heading.normalizedOr(UnitState::toDirection(unitInfo.state->rotation));
            dogfight->phase = AirMovementStateDogfight::Phase::Extending;
            dogfight->goalPosition = SimVector(
                unitInfo.state->position.x + (nose.x * (30_ss * v)),
                unitInfo.state->position.y,
                unitInfo.state->position.z + (nose.z * (30_ss * v)));
            dogfight->goalVelocity = SimVector(nose.x * v, 0_ss, nose.z * v);
            dogfight->offNoseCounter = 0;
            dogfight->nextDecision = sim->gameTime + GameTime(60 + randomBelow(sim->rng, 30));
            return false;
        }

        if (sim->gameTime < dogfight->nextDecision)
        {
            return false;
        }

        // A decision. Where is he?
        if (targetIsAhead(heading, toBandit))
        {
            dogfight->offNoseCounter = 0;
        }
        else
        {
            dogfight->offNoseCounter += 45;
        }

        if (dogfight->offNoseCounter >= 90)
        {
            beginBreak();
            return false;
        }

        dogfight->nextDecision = sim->gameTime + GameTime(45);

        // Inside a hundred and sixty units nothing is installed at all: it
        // holds whatever it was already flying (0x413237).
        if (toBandit.lengthSquared() <= (160_ss * 160_ss))
        {
            return false;
        }

        // Otherwise lead him: a point forty-five ticks along his own
        // velocity, which then runs on at his speed plus half his top speed.
        // That last term is unconditional in the original, so a hovering
        // bandit still gets a goal that slides along his heading.
        auto banditVelocity = match(
            banditState.physics,
            [&](const UnitPhysicsInfoAir& a) { return airVelocity(a.movementState); },
            [&](const UnitPhysicsInfoGround& g) { return UnitState::toDirection(banditState.rotation) * g.currentSpeed; });
        SimVector lead(
            banditState.position.x + (banditVelocity.x * 45_ss),
            banditState.position.y,
            banditState.position.z + (banditVelocity.z * 45_ss));

        auto banditNose = UnitState::toDirection(banditState.rotation);
        auto half = banditDefinition.maxVelocity / 2_ss;
        dogfight->phase = AirMovementStateDogfight::Phase::Pursuing;
        dogfight->goalPosition = lead;
        dogfight->goalVelocity = SimVector(
            banditVelocity.x + (banditNose.x * half),
            0_ss,
            banditVelocity.z + (banditNose.z * half));

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
            // (all four draws here take the modulo rather than going through
            // uniform_int_distribution: see the note at chooseTarget.)
            auto scatter = SimAngle(sim->rng() % (QuarterTurn.value + 1u)) - SimAngle(QuarterTurn.value / 2);
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

        // BOUNDARY-004: AirToGroundHover is one of the six handlers with the
        // off-grid recovery. It replaces the station with the 800-toward-centre
        // point and leaves the ring, its phase and its target as they were, so
        // the gunship resumes its swing once it is back inside the grid.
        if (airIsOutsideGrid(unitInfo))
        {
            hover->station = aircraftBoundaryRecoveryGoal(sim->terrain, unitInfo.state->position);
        }
        else if (hover->phase == AirMovementStateHoverAttack::Phase::Closing)
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
        // The miss counter is cumulative: it moves only on a miss and an
        // in-range arrival leaves it alone (TOTALA-EXE-MISSIONS.md S:129).
        // It is reset when the ring is entered and at the reroll below.
        if (fromTarget.lengthSquared() > weaponMaxRange * weaponMaxRange)
        {
            ++hover.outOfRangeArrivals;
        }

        auto stationAltitude = [&](const SimVector& p) {
            return getTargetAltitude(sim->terrain, p.x, p.z, *unitInfo.definition);
        };

        if (hover.outOfRangeArrivals >= 2)
        {
            // The reroll is a no-op in the original (S:129). It draws a
            // bearing (rand(0x10000), 0x413929) and builds a goal at range
            // R on it, then never installs the goal: nothing is told to
            // move. The draw is the only effect, and it is kept because the
            // generator's sequence is shared with everything else. The
            // station stays where the aircraft is, so the next arrival test
            // passes at once and the ordinary swing installs the next
            // 45-degree station a tick later, as the original's does.
            hover.outOfRangeArrivals = 0;

            [[maybe_unused]] auto discarded = sim->rng() % 0x10000u;

            auto station = unitPosition;
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

        // Neither escort handler (0x406300 on the ground, 0x40FBE0 in the air)
        // asks who owns the guarded unit: the order ends on a missing target and
        // on nothing about alliance, and an ally later captured by an enemy is
        // still followed. The alliance table gates only the offer (the default
        // ladder's `allied && canGuard`) and the retaliation step, which looks
        // at the attacker. BT-98.
        if (!target)
        {
            return true;
        }
        auto& targetUnit = target->get();

        // An escort reacts to whoever hit the guarded unit, unless the attacker
        // is an ally of the escort's owner or the escort's NoChaseCategory
        // names it (the retaliation step of 0x406300 and 0x40FBE0, the only
        // place either reads the alliance table, which is why the guarded unit
        // may be a teammate's; BT-98, #643). An attack order first; failing
        // that the weapon slots are retargeted onto it. Once per hit: the
        // escort remembers the hit it has answered. A hit counts for a second;
        // the original delivers it as an event on the tick it lands, which
        // RWE's unit pass would otherwise see a tick late or twice.
        if (targetUnit.lastAttacker
            && targetUnit.lastAttackedAt > unitInfo.state->guardSeenAttackAt
            && targetUnit.lastAttackedAt + GameTime(SimTicksPerSecond) >= sim->gameTime)
        {
            unitInfo.state->guardSeenAttackAt = targetUnit.lastAttackedAt;
            auto attackerId = *targetUnit.lastAttacker;
            auto attackerRef = sim->tryGetUnitState(attackerId);
            if (attackerRef && !attackerRef->get().isDead() && attackerId != unitInfo.id
                && !sim->arePlayersAllied(unitInfo.state->owner, attackerRef->get().owner)
                && !categoryListContains(sim->unitDefinitions.at(attackerRef->get().unitType).category, unitInfo.definition->noChaseCategory)
                && unitInfo.state->fireOrders != UnitFireOrders::HoldFire)
            {
                if (unitInfo.definition->canAttack && sim->canSeeUnit(unitInfo.state->owner, attackerId)
                    && attackOrderIsAccepted(*sim, *unitInfo.definition, *unitInfo.state, attackerRef->get()))
                {
                    auto attack = createAttackOrder(attackerId);
                    std::get<AttackOrder>(attack).automatic = unitInfo.definition->defaultMission != DefaultMission::None;
                    unitInfo.state->orders.push_front(std::move(attack));
                    return false;
                }

                sim->returnFire(unitInfo.id, attackerId);
            }
        }

        if (auto bs = std::get_if<UnitBehaviorStateBuilding>(&targetUnit.behaviourState); unitInfo.definition->builder && bs)
        {
            buildExistingUnit(unitInfo, bs->targetUnit);
            return false;
        }

        if (auto fs = std::get_if<FactoryBehaviorStateBuilding>(&targetUnit.factoryState); unitInfo.definition->builder && fs)
        {
            if (fs->targetUnit)
            {
                // Beside the factory, not beside the frame inside it.
                buildExistingUnit(unitInfo, fs->targetUnit->first, guardOrder.target);
                return false;
            }
        }

        // Guarding the frame itself rather than the factory making it. A
        // player picks the thing they want finished, and both orders mean
        // the same job; without this the guard fell through to "stay close"
        // below, walked to within 200 of the pad and stood there watching.
        // Same reach rule as the repair path: measured to the factory when
        // the frame is on its pad.
        if (unitInfo.definition->builder
            && targetUnit.isBeingBuilt(sim->unitDefinitions.at(targetUnit.unitType)))
        {
            buildExistingUnit(unitInfo, guardOrder.target, factoryBuilding(guardOrder.target));
            return false;
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

            // BOUNDARY-004: VTOL_Follow's off-grid recovery. No retained
            // target and a cancellation are the earlier terminations, and
            // both were settled above; while the aircraft is outside, the
            // movement objective is the 800-toward-centre point instead of
            // the ring around the guarded unit. The order, its target and
            // the aircraft's state are retained.
            if (airIsOutsideGrid(unitInfo))
            {
                steerAirBackInside(unitInfo);
                return false;
            }

            flyAirLoiterCircuit(unitInfo, UnitState::AirLoiterState::Reason::Guarding, targetUnit.position, AirMovement::LoiterGuardStep);
            return false;
        }

        // Community Patch: a construction unit guarding a factory stays where
        // it finished the last build instead of walking to a new spot. TADR
        // 2025.5.18, "Improved behaviour of con units when guarding a factory
        // - they stay put after finishing a build", made an option of the
        // 2025.7.12 "Improved behaviour of con units" entry (stay put;
        // cavedog; scatter). RWE's ordinary path below is the one that moves:
        // once the frame is released there is no factory job to copy, so the
        // guard falls through to "stay close" and, if it finished the build
        // from outside 200 of the factory's centre, walks in towards it. The
        // guard was already within working reach when it did that work, so
        // staying there costs it nothing: the next frame is still in reach.
        // Out of reach it keeps the approach, because a unit ordered to guard
        // a factory from across the map must still arrive to assist.
        //
        // Ground only: a construction aircraft's guard is the VTOL circuit
        // handled above, which TADR does not touch. This is RWE-original
        // ruleset work under ADR-0006, not a byte-tactics comparison.
        const auto& guardedDefinition = sim->unitDefinitions.at(targetUnit.unitType);
        if (sim->communityPatchMode
            && unitInfo.definition->builder
            && !unitInfo.definition->canFly
            && guardedDefinition.builder
            && !guardedDefinition.isMobile
            && withinBuildReach(unitInfo, targetUnit))
        {
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

        // Reclaiming is gated on `canreclamate` and on nothing else. The
        // original's CanReclaimTarget (0x489960) tests that bit first and
        // never looks at `workertime` at all, which is the rule RWE was
        // missing: sixteen units in the base game name the key, and
        // twenty-one more have a worker time without it -- every factory,
        // both air repair pads, both carriers and CORSOLAR. All of those
        // were happily reclaiming.
        if (!unitInfo.definition->canReclamate)
        {
            return true;
        }

        if (auto targetUnit = std::get_if<UnitId>(&reclaimOrder.target))
        {
            if (auto target = sim->tryGetUnitState(*targetUnit))
            {
                // A unit that can capture cannot itself be reclaimed
                // (0x48998F, mirrored in the capture handler at 0x4042E3).
                // Only the two Commanders set `cancapture`, so what the rule
                // amounts to is that a Commander is the one thing you may
                // not recycle.
                const auto& targetDefinition = sim->unitDefinitions.at(target->get().unitType);
                if (targetDefinition.canCapture)
                {
                    // 0x50164C, from ReclaimUnit (0x4047A6).
                    sim->events.push_back(UnitCannotComplyEvent{unitInfo.id, "That unit cannot be reclaimed"});
                    return true;
                }
            }
        }

        return reclaimTarget(unitInfo, reclaimOrder.target);
    }

    bool UnitBehaviorService::handleResurrectOrder(UnitInfo unitInfo, ResurrectOrder& resurrectOrder)
    {
        // 0x404E55: the capability is tested before anything else.
        if (!unitInfo.definition->canResurrect)
        {
            return true;
        }

        auto featureRef = sim->tryGetFeature(resurrectOrder.target);
        if (!featureRef)
        {
            // The corpse has gone. 0x501684, spelt as the original spells it.
            sim->events.push_back(UnitCannotComplyEvent{unitInfo.id, "Ressurection failed"});
            return true;
        }
        const auto& feature = featureRef->get();
        const auto& featureDefinition = sim->getFeatureDefinition(feature.featureName);

        // 0x404E0F tests the feature's reclaimable bit, not a resurrect bit
        // of its own: anything you could have reclaimed you can raise.
        if (!featureDefinition.reclaimable)
        {
            return true;
        }

        auto unitType = sim->resurrectedUnitType(featureDefinition.name);
        if (!unitType)
        {
            return true;
        }

        // The second copy of the invented 300, unremarked where reclaim's at
        // least admitted itself. Raising a wreck reaches exactly as far as
        // reclaiming it, so it is measured the same way.
        auto rect = sim->computeFootprintRegion(feature.position, static_cast<unsigned int>(featureDefinition.footprintX), static_cast<unsigned int>(featureDefinition.footprintZ));
        if (!withinBuildReachOfRect(unitInfo, rect))
        {
            // The footprint itself, as reclaim does: see reclaimTarget.
            navigateTo(unitInfo, rect);
            return false;
        }

        // The original draws a nanolathe every tick it works -- 0x40509D
        // calls the same beam routine build, repair, reclaim and capture do.
        // Getting the arms out is what makes it appear, and a construction
        // aircraft has to be flying first.
        if (!prepareBuilderForWork(unitInfo, feature.position))
        {
            return false;
        }

        if (auto state = std::get_if<UnitBehaviorStateResurrecting>(&unitInfo.state->behaviourState);
            state == nullptr || state->target != resurrectOrder.target)
        {
            changeState(*unitInfo.state, UnitBehaviorStateResurrecting{resurrectOrder.target, std::nullopt});
            return false;
        }

        if (!unitInfo.state->inBuildStance)
        {
            // Not in the stance yet; the script is still swinging the arms out.
            return false;
        }

        std::get<UnitBehaviorStateResurrecting>(unitInfo.state->behaviourState).nanoParticleOrigin
            = getNanoPoint(unitInfo.id);

        if (!resurrectOrder.remainingTicks)
        {
            // (BuildTime * 0.3) / (WorkerTime / 30), and this is the one job
            // of the three that does scale with the builder -- capture does
            // not, and neither does reclaim. The divisor is already the
            // integer WorkerTime/30 the original computes.
            const auto& resurrectedDefinition = sim->unitDefinitions.at(*unitType);
            auto workerTime = unitInfo.definition->workerTimePerTick;
            if (workerTime == 0)
            {
                // No worker time is an infinite job; the original would divide
                // by zero here, and a unit that cannot work cannot raise
                // anything.
                return true;
            }

            auto work = (static_cast<std::uint64_t>(resurrectedDefinition.buildTime) * 3u) / 10u;
            resurrectOrder.remainingTicks = static_cast<unsigned int>(work / workerTime);
        }

        if (*resurrectOrder.remainingTicks > 0)
        {
            --*resurrectOrder.remainingTicks;
            return false;
        }

        // The work is done, but the unit is not made here. The behaviour
        // pass is walking `units` -- a VectorMap over a std::deque -- and
        // creating a unit appends to that deque, which invalidates the
        // iterator the walk is holding. Every other creation in the tick
        // already knows this: a build order and a factory roll-off both push
        // onto unitCreationRequests and let spawnNewUnits do the work once
        // the pass is over, and deleteDeadUnits waits its turn the same way.
        // Resurrect was the one handler that created a unit inline.
        //
        // Nothing visible went wrong, which is why it stood: one append
        // rarely moves a deque's node map, so the stale iterator usually
        // still lands somewhere sensible. MSVC's checked iterators say so
        // outright, and so does a _GLIBCXX_DEBUG build; an ordinary build is
        // simply not looking.
        //
        // The corpse is removed over there too, and its removal is what ends
        // this order: the next tick finds no feature and drops it. The
        // original's own order is the other way round -- it creates the unit
        // at 0x405104 and removes the corpse at 0x405198 -- but a corpse is
        // `blocking` and RWE will not place a unit on an occupied footprint,
        // so creating first could never succeed. Same outcome, opposite
        // order.
        sim->unitCreationRequests.push_back(unitInfo.id);
        return false;
    }

    bool UnitBehaviorService::handleRepairOrder(UnitInfo unitInfo, const RepairOrder& repairOrder)
    {
        if (repairOrder.target == unitInfo.id)
        {
            // A unit cannot repair itself.
            return true;
        }

        // A job a patrolling constructor started for itself ends when it is
        // `leash` from where it stood (S:131, 0x43B400); tested as an
        // attack's is -- flat, in whole world units. [open]
        if (repairOrder.leash && repairOrder.leash->distance > 0_ss)
        {
            auto dx = unitInfo.state->position.x - repairOrder.leash->anchor.x;
            auto dz = unitInfo.state->position.z - repairOrder.leash->anchor.z;
            if ((dx * dx) + (dz * dz) >= repairOrder.leash->distance * repairOrder.leash->distance)
            {
                return true;
            }
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
            //
            // If it is still on a factory's pad, the reach is measured to the
            // factory and not to it. A frame there stands at the middle of a
            // building nothing can walk into, so a builder measuring its arm
            // to the frame walks as close as the yard allows, finds itself
            // short and stands there for the rest of the game -- which is
            // what a play-test reported of a commander told to repair a unit
            // under construction in a factory. The guard path has measured to
            // the factory since S:57; this one never did.
            return buildExistingUnit(unitInfo, repairOrder.target, factoryBuilding(repairOrder.target));
        }

        if (target.hitPoints >= targetDefinition.maxHitPoints)
        {
            // Nothing to repair. The "Unit repaired" voice is raised where
            // the mending finishes, in deployRepairArm, so an order for a
            // unit that was already whole says nothing.
            return true;
        }

        // Repairing is gated on the same FBI key. The parser mirrors
        // `canreclamate` into bit 9 of the same word (0x42CA3B-0x42CA4D) and
        // CanRepair at 0x4899CC tests that mirror, so one key really does
        // govern both: a unit that may not reclaim may not repair either.
        // Finishing somebody's half-built structure above is construction
        // rather than repair and is deliberately left outside this gate.
        if (!unitInfo.definition->canReclamate)
        {
            return true;
        }

        return repairExistingUnit(unitInfo, repairOrder.target);
    }

    void UnitBehaviorService::mendRaisedUnit(UnitId resurrectorId, FeatureId corpse, UnitId raisedId)
    {
        auto& resurrector = sim->getUnitState(resurrectorId);
        const auto& raised = sim->getUnitState(raisedId);

        // The order builder's command 8 needs the orderer's repair capability
        // (canreclamate, 0x4899B0) and a target whose health differs from its
        // maximum; for a finished unit it yields REPAIRUNIT, which goes in at
        // the head of the list as the mission ends. The mission's own order is
        // dropped with it rather than left to find no corpse and complain.
        if (!sim->unitDefinitions.at(resurrector.unitType).canReclamate
            || raised.hitPoints >= sim->unitDefinitions.at(raised.unitType).maxHitPoints)
        {
            return;
        }

        auto& orders = resurrector.orders;
        for (auto it = orders.begin(); it != orders.end(); ++it)
        {
            if (const auto* resurrect = std::get_if<ResurrectOrder>(&*it); resurrect != nullptr && resurrect->target == corpse)
            {
                orders.erase(it);
                break;
            }
        }
        orders.push_front(RepairOrder(raisedId));
        interruptCurrentTask(resurrectorId);
    }

    void UnitBehaviorService::resetGuardNoMove(UnitState& unit)
    {
        unit.guardPhase = 0;
        unit.guardWakeAt = GameTime(0);
        unit.guardTarget = std::nullopt;
        unit.guardPosition = std::nullopt;
        unit.guardLimit = 0;
        unit.guardCounter = 0;
    }

    bool UnitBehaviorService::guardCanEngage(const UnitState& unit, UnitId target) const
    {
        // 0x49ABB0 for the mission's target: it is there, the owner can see it,
        // and some weapon of the tower's can reach and hit it.
        auto targetRef = sim->tryGetUnitState(target);
        if (!targetRef || targetRef->get().isDead() || !sim->canSeeUnit(unit.owner, target))
        {
            return false;
        }
        const auto& other = targetRef->get();
        for (const auto& weapon : unit.weapons)
        {
            if (!weapon)
            {
                continue;
            }
            const auto& definition = sim->weaponDefinitions.at(weapon->weaponType);
            if (withinFlatRange(unit.position, other.position, definition.maxRange) && weaponCanHitUnit(definition, unit, other))
            {
                return true;
            }
        }
        return false;
    }

    void UnitBehaviorService::guardAssignWeapons(UnitState& unit, UnitId target)
    {
        // 0x48A060: every slot that can take a target is aimed at it. The ones
        // that never acquire a target of their own -- interceptors, bombs, a
        // hand-fired weapon -- are left to whatever fires them.
        for (auto& weapon : unit.weapons)
        {
            if (!weapon)
            {
                continue;
            }
            const auto& definition = sim->weaponDefinitions.at(weapon->weaponType);
            if (definition.interceptor || definition.commandFire || std::holds_alternative<ProjectilePhysicsTypeBomb>(definition.physicsType))
            {
                continue;
            }
            if (const auto* attacking = std::get_if<UnitWeaponStateAttacking>(&weapon->state))
            {
                if (const auto* current = std::get_if<UnitId>(&attacking->target); current != nullptr && *current == target)
                {
                    continue;
                }
            }
            weapon->state = UnitWeaponStateAttacking(target);
        }
    }

    /**
     * BT-91 (0x4021F0): what Guard_NoMove does, which is not only to free the
     * weapons. It is a four-state machine over weapon slot 0's own pick:
     *
     *   0  release the weapons, sleep 30, go to 1.
     *   1  adopt slot 0's target. Live: remember where it stands, aim every
     *      weapon at it, set the persistence limit to 3 plus 0 to 2 and the
     *      counter to 0, go to 2. None: sleep 30 and look again.
     *   2  hold. Each pass clears the counter when a weapon has fired and
     *      adds one when none has. Within the limit and still engageable, wait.
     *      Otherwise a draw below 100: 80 or more starts again from 0; below
     *      80 clears the counter and adopts again.
     *   3  the target died or the weapons were cleared: pick one of the
     *      visible enemies within 640 of where it stood and aim at it (go to
     *      1); none starts again from 0.
     *
     * RWE's own weapon scan lets each slot choose and keep its own target
     * with nothing to make them agree and nothing to make a tower give up,
     * which is what this replaces for the 26 armed towers that name it.
     *
     * What the finding leaves open, and what stands in for it: the node's
     * wake bits are unnamed, so a pass is every 30 ticks and "the wake carried
     * 0x4000", which clears the counter, is taken to mean a weapon fired
     * within the pass (a reload still running).
     */
    void UnitBehaviorService::runGuardNoMove(UnitInfo unitInfo)
    {
        auto& unit = *unitInfo.state;
        constexpr unsigned int PassTicks = 30;
        constexpr float RecoveryRadius = 640.0f;

        // An unarmed tower has nothing to hand out, and a tower told to hold
        // fire has nothing taking a target.
        if (!unit.weapons[0] || unit.fireOrders == UnitFireOrders::HoldFire)
        {
            if (unit.guardPhase != 0 || unit.guardTarget)
            {
                resetGuardNoMove(unit);
            }
            return;
        }

        const auto now = sim->gameTime;

        // The entry guard (wake 0x10008): the target died or the weapons were
        // cleared. It does not wait out a sleep.
        if (unit.guardPhase == 2)
        {
            auto stillThere = unit.guardTarget && sim->tryGetUnitState(*unit.guardTarget)
                && !sim->getUnitState(*unit.guardTarget).isDead();
            if (!stillThere || std::holds_alternative<UnitWeaponStateIdle>(unit.weapons[0]->state))
            {
                if (stillThere && unit.guardTarget)
                {
                    unit.guardPosition = sim->getUnitState(*unit.guardTarget).position;
                }
                unit.guardTarget = std::nullopt;
                unit.guardPhase = 3;
                unit.guardWakeAt = now;
            }
        }

        if (now < unit.guardWakeAt)
        {
            return;
        }

        switch (unit.guardPhase)
        {
            case 0:
            {
                for (unsigned int i = 0; i < unit.weapons.size(); ++i)
                {
                    if (unit.weapons[i])
                    {
                        unit.clearWeaponTarget(i);
                    }
                }
                unit.guardTarget = std::nullopt;
                unit.guardWakeAt = now + GameTime(PassTicks);
                unit.guardPhase = 1;
                return;
            }
            case 1:
            {
                std::optional<UnitId> picked;
                if (const auto* attacking = std::get_if<UnitWeaponStateAttacking>(&unit.weapons[0]->state))
                {
                    if (const auto* targetUnit = std::get_if<UnitId>(&attacking->target))
                    {
                        auto targetRef = sim->tryGetUnitState(*targetUnit);
                        if (targetRef && !targetRef->get().isDead())
                        {
                            picked = *targetUnit;
                            unit.guardPosition = targetRef->get().position;
                        }
                    }
                }
                if (!picked)
                {
                    unit.guardWakeAt = now + GameTime(PassTicks);
                    return;
                }
                unit.guardTarget = picked;
                guardAssignWeapons(unit, *picked);
                // Drawn from the one stream, never uniform_int_distribution.
                unit.guardLimit = 3u + randomBelow(sim->rng, 3u);
                unit.guardCounter = 0;
                unit.guardPhase = 2;
                unit.guardWakeAt = now + GameTime(PassTicks);
                return;
            }
            case 2:
            {
                bool fired = false;
                for (const auto& weapon : unit.weapons)
                {
                    fired = fired || (weapon && weapon->readyTime > now);
                }
                unit.guardCounter = fired ? 0u : unit.guardCounter + 1u;

                if (unit.guardCounter <= unit.guardLimit && unit.guardTarget && guardCanEngage(unit, *unit.guardTarget))
                {
                    unit.guardWakeAt = now + GameTime(PassTicks);
                    return;
                }

                if (randomBelow(sim->rng, 100u) >= 80u)
                {
                    unit.guardPhase = 0;
                }
                else
                {
                    unit.guardCounter = 0;
                    unit.guardPhase = 1;
                }
                unit.guardWakeAt = now;
                return;
            }
            default:
            {
                // 3: look round the place the target stood.
                std::vector<UnitId> candidates;
                if (unit.guardPosition)
                {
                    auto found = sim->getUnitSpatialIndex().collectEnemiesNear(
                        simScalarToFloat(unit.guardPosition->x),
                        simScalarToFloat(unit.guardPosition->z),
                        RecoveryRadius,
                        unit.owner);
                    for (auto candidateId : found)
                    {
                        auto candidate = sim->tryGetUnitState(candidateId);
                        if (!candidate || candidate->get().isDead() || sim->arePlayersAllied(unit.owner, candidate->get().owner)
                            || !sim->canSeeUnit(unit.owner, candidateId))
                        {
                            continue;
                        }
                        if (!withinFlatRange(*unit.guardPosition, candidate->get().position, SimScalar(RecoveryRadius)))
                        {
                            continue;
                        }
                        candidates.push_back(candidateId);
                    }
                }
                if (candidates.empty())
                {
                    unit.guardPhase = 0;
                    unit.guardWakeAt = now;
                    return;
                }
                auto chosen = candidates[randomBelow(sim->rng, static_cast<unsigned int>(candidates.size()))];
                unit.guardTarget = chosen;
                guardAssignWeapons(unit, chosen);
                unit.guardPhase = 1;
                unit.guardWakeAt = now;
                return;
            }
        }
    }

    void UnitBehaviorService::pollMine(UnitInfo unitInfo)
    {
        auto& unit = *unitInfo.state;

        // State 0 ends the mission at once for anything that can move
        // (unit+0x110 bit 29, which 0x485A81 sets when bmcode is zero), and
        // a mine already counting down has SELFDESTRUCT in front of this
        // mission, not this.
        if (unitInfo.definition->isMobile || unit.selfDestructTime)
        {
            return;
        }

        // Installed: free the weapons and sleep a tick (0x40616B-0x406182).
        if (!unit.minePollAt)
        {
            unit.minePollAt = sim->gameTime + GameTime(1);
            return;
        }
        if (sim->gameTime < *unit.minePollAt)
        {
            return;
        }

        // 0x43B700 finds nothing unless the mine is on Fire At Will, which
        // is how ARMMINE6 and CORMINE6 -- StandingFireOrder=0, with the
        // button -- sit quiet until the player arms them. What it finds is
        // the ordinary chooser's pick over the mine's SightDistance, and the
        // pick has to be standing on the ground (target+0x110 & 3 == 1,
        // 0x4060BF): an aircraft overhead does not set one off.
        std::optional<UnitId> sighting;
        if (unit.fireOrders == UnitFireOrders::FireAtWill)
        {
            sighting = chooseTarget(unitInfo.id, 0, TargetSearchMode::SightDistance);
        }
        if (sighting && !isFlying(sim->getUnitState(*sighting).physics))
        {
            // Push SELFDESTRUCT (0x4060D8-0x406108): the definition's own
            // countdown, then the blast.
            unit.minePollAt.reset();
            sim->startSelfDestruct(unitInfo.id);
            return;
        }

        // Nothing: sleep rand(30)+30 and look again (0x40612A-0x406142).
        unit.minePollAt = sim->gameTime + GameTime(30u + randomBelow(sim->rng, 30u));
    }

    std::optional<UnitId> UnitBehaviorService::findEnemyToEngage(UnitInfo unitInfo)
    {
        // The original does not have a second search for this. Standby,
        // patrol and the ground missions all call 0x43B700, which checks the
        // unit is on Fire At Will and then hands straight over to the ordinary
        // chooser with its third argument zero. So a unit deciding whether to
        // go and fight weighs candidates exactly as its gun would, bad target
        // categories and all -- which is what keeps a Freedom Fighter, whose
        // wpri_badTargetCategory is NOTAIR, off the tanks for as long as there
        // is anything in the air to shoot at instead.
        if (!unitInfo.state->weapons[0])
        {
            return std::nullopt;
        }

        auto candidate = chooseTarget(unitInfo.id, 0, TargetSearchMode::SightDistance);
        if (!candidate)
        {
            return std::nullopt;
        }

        // A bomb is never handed a mission against anything that can fly:
        // the mission builder skips AIRSTRIKE for an air target at 0x43F2AA
        // and falls through to no mission at all (S:86). Refusing the
        // candidate here, where the patrol polls, is what keeps that refusal
        // free: a bomber that broke off for a fighter parked beside its
        // route, was refused, and broke off again next tick, would mill
        // about beside it instead of carrying on (issue #70). The chooser is
        // the ordinary one, so a flying candidate is declined rather than
        // replaced; the poll simply asks again next tick.
        const auto& weaponDefinition = sim->weaponDefinitions.at(unitInfo.state->weapons[0]->weaponType);
        if (std::holds_alternative<ProjectilePhysicsTypeBomb>(weaponDefinition.physicsType))
        {
            auto target = sim->tryGetUnitState(*candidate);
            if (target && sim->unitDefinitions.at(target->get().unitType).canFly)
            {
                return std::nullopt;
            }
        }

        return candidate;
    }

    bool UnitBehaviorService::handleDgunOrder(UnitInfo unitInfo, const DgunOrder& order)
    {
        if (auto target = std::get_if<UnitId>(&order.target))
        {
            auto targetRef = sim->tryGetUnitState(*target);
            if (!targetRef || targetRef->get().isDead())
            {
                return true;
            }
            if (!sim->canSeeUnit(unitInfo.state->owner, *target))
            {
                unitInfo.state->clearWeaponTargets();
                return false;
            }
        }
        // Slot 2 -- Weapon3 in the FBI -- by index, because that is what the
        // original does: 0x403190 writes the literal 2 into the mission's
        // weapon-slot word and everything downstream indexes with it. It
        // never goes looking for the commandfire bit. The shipped commanders
        // agree: ARMCOM declares Weapon1 and Weapon3 and no Weapon2 at all,
        // and its COB has AimTertiary and FireTertiary to match.
        const unsigned int weaponIndex = 2;
        if (!unitInfo.state->weapons[weaponIndex])
        {
            return true;
        }

        // One shot ends it, and that is the commandfire rule rather than a
        // D-gun one.
        if (unitInfo.state->commandFireShotFired)
        {
            unitInfo.state->commandFireShotFired = false;
            unitInfo.state->clearWeaponTarget(weaponIndex);
            return true;
        }

        auto targetPosition = getTargetPosition(order.target);
        if (!targetPosition)
        {
            // The target died. The original's weak reference nulls the
            // mission's target pointer (0x489789) and the handler's own guard
            // deletes the mission on the next pass (0x4034C0).
            unitInfo.state->clearWeaponTarget(weaponIndex);
            return true;
        }

        const auto& weaponDefinition = sim->weaponDefinitions.at(unitInfo.state->weapons[weaponIndex]->weaponType);

        // The D-gun is an ordinary weapon to the water rule: it has no
        // WaterWeapon key and no exemption for ships or submarines. A unit
        // target needs both ends of the shot out of the sea, so a submerged
        // submarine cannot be picked as the target, while a ship with its
        // model top above the surface can. A point on the ground is judged on
        // the shooter alone. A refused acquisition ends the order here, as it
        // does for an ordinary attack. NAVAL-CONSTRUCTION-DGUN-FINDINGS.md,
        // "A fully submerged target normally fails acquisition".
        if (auto targetUnitId = std::get_if<UnitId>(&order.target))
        {
            auto targetUnit = sim->tryGetUnitState(*targetUnitId);
            if (targetUnit && !sim->weaponCanHitUnit(weaponDefinition, *unitInfo.state, targetUnit->get()))
            {
                unitInfo.state->clearWeaponTarget(weaponIndex);
                return true;
            }
        }
        else if (!sim->weaponCanFireFrom(weaponDefinition, *unitInfo.state))
        {
            unitInfo.state->clearWeaponTarget(weaponIndex);
            return true;
        }

        // The approach closes to the weapon's full range -- 0x403623 asks
        // 0x49ADF0 for it, which is just wdef+0xDC -- and the range test is
        // flat: 0x49AD7E squares dx and dz and never looks at y.
        if (!withinFlatRange(unitInfo.state->position, *targetPosition, weaponDefinition.maxRange))
        {
            // The disintegrator reaches 240, which is nothing: walking into
            // range is most of what this order does.
            unitInfo.state->clearWeaponTarget(weaponIndex);
            navigateTo(unitInfo, attackTargetToNavigationGoal(order.target));
            return false;
        }

        // In range: point it. Everything after this is the ordinary weapon
        // machinery -- it is an ordinary weapon, and the tests in
        // sim/dgun.test.cpp pin what makes it unusual: it will not fire
        // without energypershot in the bank, and it is never given a target
        // by anything except an order like this one.
        // Clear any stale shot flag as the target goes on, the way the
        // original's aim call clears the event bits (0x48A060 ends with
        // `unit+0xBA &= 0x83FF`). Without it, a nuke fired a moment ago would
        // end this order before it had fired anything of its own.
        unitInfo.state->commandFireShotFired = false;
        match(
            order.target,
            [&](const UnitId& u) { unitInfo.state->setWeaponTarget(weaponIndex, u); },
            [&](const SimVector& v) { unitInfo.state->setWeaponTarget(weaponIndex, v); });

        return false;
    }

    bool UnitBehaviorService::handlePatrolOrder(UnitInfo unitInfo, const PatrolOrder& patrolOrder)
    {
        if (!unitInfo.definition->isMobile)
        {
            // A static unit with no mover is given QPATROL, not a patrol
            // (0x43F3B9 tests the mover before it picks the mission), and
            // QPATROL never completes: it sleeps and rotates to the tail of
            // the list, like QMOVE. That is what makes a patrol set on a
            // factory work -- the waypoints stay in its queue, and when a
            // unit is finished GetBuilt (0x402DA0) walks that queue and
            // issues each waypoint to the product through the ordinary order
            // builder (command 9 for a patrol). handleMoveOrder keeps its
            // order for the same reason. Anything that cannot patrol has no
            // way to have been given one, so the drop is only a backstop.
            return !unitInfo.definition->canPatrol;
        }

        // A unit that can repair is not on this mission at all in the
        // original. 0x43F3E6 tests the definition's repair bit before
        // anything else and hands out RepairPatrol instead of Patrol, and
        // neither RepairPatrol handler is among the six callers of the
        // acquisition search -- so a construction unit walking a route clears
        // wreckage and mends things and never breaks off to chase anything.
        // RWE runs both jobs from one handler, which is a fair simplification
        // of two missions built on the same skeleton; what it must not do is
        // offer the engage check to a unit that would never have been given
        // it.
        auto repairs = unitInfo.definition->canReclamate;

        // Break off for anything hostile close enough to be worth it.
        //
        // Fire At Will exactly: 0x43B700 is `cmp ecx,0x200000 / jne`, so a
        // unit on Return Fire shoots back at whatever shoots first but never
        // leaves its route for a target it merely saw.
        //
        // And not at all on Hold Position: 0x43B1F0 returns without a
        // mission when the unit's movement mode is 0 and the call was not
        // forced (0x43B211), and the poll never forces. The mode is the
        // unit's own, the one the MOVEORD button and the COB SET move, not
        // the definition's standing order it started on (issue #109).
        if (!repairs && unitInfo.state->fireOrders == UnitFireOrders::FireAtWill
            && unitInfo.state->moveOrders != UnitMovementOrders::HoldPosition)
        {
            if (auto enemy = findEnemyToEngage(unitInfo))
            {
                // A sighting becomes a real order, not a steer for one tick.
                // 0x43B1F0 prepends an attack mission and lets it own the
                // unit until it deletes itself; the patrol mission behind it
                // is untouched, so the route resumes at the same waypoint it
                // was already heading for. Doing it per tick instead meant
                // re-picking a target every tick -- and the chooser is
                // deliberately random -- which an attack run cannot survive.
                if (unitInfo.state->moveOrders == UnitMovementOrders::Maneuver)
                {
                    // Maneuver walks back to where it was standing when it
                    // saw the target, before carrying on with the route
                    // (0x43B285-0x43B2B1). Pushed first so it ends up behind
                    // the attack.
                    unitInfo.state->orders.push_front(MoveOrder(unitInfo.state->position));
                }

                auto leash = AttackLeash(unitInfo.state->position, unitInfo.definition->maneuverLeashLength);
                unitInfo.state->orders.push_front(AttackOrder(*enemy, leash));
                return false;
            }
        }

        // A builder on patrol clears the battlefield as it goes. This is the
        // original's only automatic reclaim: `0x47EA40`, the area scan that
        // reads the `autoreclaimable` bit, has exactly two callers -- the
        // ground RepairPatrol at `0x405B93` and VTOL_RepairPatrol at
        // `0x41564F`. There is no area-reclaim command and no idle-builder
        // sweep anywhere in the binary, so this is where the key belongs.
        // See TOTALA-EXE.md §97.
        //
        // S:131: the decision is taken on the mission's timer -- 60 ticks on
        // the ground, 45 in the air -- and what it chooses is pushed in front
        // of the patrol as an order of its own, which owns the unit until it
        // is done: 0x405CA2-0x405D18 builds a RECLAIM mission, 0x43B400 a
        // REPAIR or HELPBUILD one. The timer is a phase of the period per
        // unit rather than a counter; see runRepairPatrolDecision. [open]
        if (repairs
            && (sim->gameTime.value + unitInfo.id.value) % repairPatrolPeriod(unitInfo.definition->canFly) == 0
            && runRepairPatrolDecision(unitInfo))
        {
            return false;
        }

        // Nothing to fight: this order is steering again, so an aircraft
        // still holding a run from a target that has died or moved out of
        // reach hands it back before navigating.
        dropAirAttackRun(unitInfo);

        // An aircraft's leg is not flown at the waypoint but at a goal 320
        // past it (S:125). A construction aircraft is on VTOL_RepairPatrol
        // instead, which has no overshoot (S:131), so it keeps the ordinary
        // arrival below.
        if (unitInfo.definition->canFly && !repairs)
        {
            if (flyAirPatrolLeg(unitInfo, patrolOrder.destination))
            {
                // The mission retires the waypoint and goes to the tail of the
                // list (return 6): copy first, the caller pops the front.
                auto destination = patrolOrder.destination;
                unitInfo.state->airLoiter = std::nullopt;
                unitInfo.state->orders.push_back(PatrolOrder(destination));
                return true;
            }
            return false;
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

    bool UnitBehaviorService::runRepairPatrolDecision(UnitInfo unitInfo)
    {
        // TOTALA-EXE-ECONOMY.md S:131. State 1 of the original's
        // RepairPatrol, in the order it takes things: the repair stage (own
        // or allied, hurt, in sight, one random pick), then the reclaim
        // stage. Both pick with the sim's generator, and the number and
        // order of the draws is the register's.
        const bool flies = unitInfo.definition->canFly;
        auto& orders = unitInfo.state->orders;
        const auto here = unitInfo.state->position;

        // 0x43B400 returning non-zero makes the patrol return 6, which
        // retires the waypoint: the patrol order goes to the back of the
        // list, behind the job that was just put in front of it. [open] --
        // the register glosses 6 that way only for the arrival case.
        auto retireWaypoint = [&](std::size_t pushed) {
            if (orders.size() > pushed && std::holds_alternative<PatrolOrder>(orders[pushed]))
            {
                auto destination = std::get<PatrolOrder>(orders[pushed]).destination;
                orders.erase(orders.begin() + static_cast<std::ptrdiff_t>(pushed));
                orders.push_back(PatrolOrder(destination));
            }
        };

        if (auto candidate = pickRepairPatrolRepairTarget(*sim, unitInfo))
        {
            const auto& candidateState = sim->getUnitState(*candidate);
            const auto finished = !candidateState.isBeingBuilt(sim->unitDefinitions.at(candidateState.unitType));

            if (flies && !finished)
            {
                // VTOL_HELPBUILD at the front with no position and no leash,
                // and the timer left to run (return 3).
                orders.push_front(RepairOrder(*candidate));
                return true;
            }

            // 0x43B400, by the unit's own move-order mode.
            std::optional<AttackLeash> leash;
            bool moveHome = true;
            switch (unitInfo.state->moveOrders)
            {
                case UnitMovementOrders::HoldPosition:
                    leash = AttackLeash(here, SimScalar(static_cast<float>(unitInfo.definition->sightDistance)));
                    break;
                case UnitMovementOrders::Maneuver:
                    leash = AttackLeash(here, unitInfo.definition->maneuverLeashLength);
                    break;
                case UnitMovementOrders::Roam:
                    moveHome = false;
                    break;
            }

            // [job][move home][old head...]
            std::size_t pushed = 1;
            if (moveHome)
            {
                orders.push_front(MoveOrder(here));
                ++pushed;
            }
            if (leash)
            {
                orders.push_front(RepairOrder(*candidate, *leash));
            }
            else
            {
                orders.push_front(RepairOrder(*candidate));
            }
            retireWaypoint(pushed);
            return true;
        }

        // The reclaim stage: always in the air, with a side of 240; on the
        // ground only while one of the stores is under a fifth, with the
        // unit's sight as the side (so the half-extent is half of it).
        //
        // Under the Community Patch (TADR 2026.10.6, "Patrol reclaim
        // thresholds") both kinds of unit use the same gate, each against the
        // player's own metal and energy levels, and only then does the air
        // scan at 240 while the ground unit scans its sight. An air
        // constructor therefore no longer reclaims whatever its owner's
        // stocks; 0 means "never low", so a patrol at 0 never reclaims.
        SimScalar side = 240_ss;
        {
            const auto& player = sim->getPlayer(unitInfo.state->owner);
            const auto metalFraction = sim->communityPatchMode
                ? static_cast<double>(player.reclaimMetalPercent) / 100.0
                : 0.2;
            const auto energyFraction = sim->communityPatchMode
                ? static_cast<double>(player.reclaimEnergyPercent) / 100.0
                : 0.2;
            const auto metalShort = static_cast<double>(player.metal.value) < metalFraction * static_cast<double>(player.maxMetal.value);
            const auto energyShort = static_cast<double>(player.energy.value) < energyFraction * static_cast<double>(player.maxEnergy.value);
            if (sim->communityPatchMode || !flies)
            {
                if (!metalShort && !energyShort)
                {
                    return false;
                }
            }
            if (!flies)
            {
                side = SimScalar(static_cast<float>(unitInfo.definition->sightDistance));
            }
        }

        if (auto feature = pickRepairPatrolReclaimTarget(*sim, unitInfo, side))
        {
            orders.push_front(ReclaimOrder(*feature));
            return true;
        }

        return false;
    }

    bool UnitBehaviorService::handleCaptureOrder(UnitInfo unitInfo, CaptureOrder& captureOrder)
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

        // A nanoframe cannot be taken. 0x404313 requires the target's
        // remaining build fraction to be exactly zero and otherwise refuses
        // with 0x5015E0, "That unit is a cloud of vapor and cannot be
        // captured" -- which is also, in one string, what the original
        // thinks an unfinished building is.
        if (targetRef->get().isBeingBuilt(sim->unitDefinitions.at(targetRef->get().unitType)))
        {
            sim->events.push_back(UnitCannotComplyEvent{unitInfo.id, "That unit is a cloud of vapor and cannot be captured"});
            return true;
        }

        // The mission's state 0 works the total out once, off the target as it
        // stands when the order starts, and never looks again (0x404313).
        if (!captureOrder.totalWork)
        {
            captureOrder.totalWork = sim->computeCaptureTime(targetRef->get());
        }

        return captureExistingUnit(unitInfo, captureOrder);
    }

    bool UnitBehaviorService::captureExistingUnit(UnitInfo unitInfo, CaptureOrder& captureOrder)
    {
        auto targetUnitRef = sim->tryGetUnitState(captureOrder.target);
        if (!targetUnitRef || targetUnitRef->get().isDead() || targetUnitRef->get().isOwnedBy(unitInfo.state->owner))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();

        // Ownership is the other machine's to change; a local captor cannot
        // take a Remote player's unit.
        if (!simulatesLocally(*sim, targetUnit.owner))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }

        // Reach measured to the footprint, as building measures it. The FIXME
        // this used to point at is settled in reclaimTarget: the original's
        // work missions all reach by Builddistance. Measuring to the footprint
        // rather than the centre only ever helps, so anywhere navigation
        // already brought a captor close enough it still does.
        if (!withinBuildReach(unitInfo, targetUnit))
        {
            navigateTo(unitInfo, captureOrder.target);
            return false;
        }

        return deployCaptureArm(unitInfo, captureOrder);
    }

    bool UnitBehaviorService::deployCaptureArm(UnitInfo unitInfo, CaptureOrder& captureOrder)
    {
        auto targetUnitId = captureOrder.target;
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
                    return captureExistingUnit(unitInfo, captureOrder);
                }

                if (!unitInfo.state->inBuildStance)
                {
                    return false;
                }

                buildingState.nanoParticleOrigin = getNanoPoint(unitInfo.id);

                // Sound slot 11, `working`. The original's Capture handler
                // plays it at 0x404568, alongside the two reclaim handlers
                // that play the same slot, once as work actually starts --
                // which is here, on the first tick of progress, and not when
                // the order was given or when the arm went up. The progress
                // count lives on the order, so a job dropped and retaken
                // announces itself again. See TOTALA-EXE.md §97.
                if (captureOrder.progress == 0)
                {
                    sim->events.push_back(UnitStartedReclaimingEvent{unitInfo.id});
                }

                // One tick of progress a tick, for every captor alike: the
                // mission adds two every two ticks (0x404698) and consults no
                // worker time anywhere. The count lives on the order, so
                // dropping the order drops the progress with it.
                captureOrder.progress += 1;
                if (captureOrder.progress < captureOrder.totalWork.value_or(0))
                {
                    return false;
                }

                auto finished = sim->captureUnit(targetUnitId, unitInfo.state->owner, unitInfo.id);
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

                // Square on to the job before the arm goes out (WorkDeployFacingTolerance).
                if (!builderIsSquareOnTo(*unitInfo.state, *unitInfo.definition, targetUnit.position))
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

        // Reach measured to the footprint, as building measures it; see
        // captureExistingUnit and reclaimTarget for the same change.
        if (!withinBuildReach(unitInfo, targetUnit))
        {
            // The thing being mended can walk away -- a damaged unit ordered
            // home, an aircraft taking off. The arm was left deployed when
            // that happened, because this branch navigates and returns
            // without touching the state, and the nanolathe is drawn from
            // the building state: from the camera it sprays across whatever
            // distance has opened up. Put the arm away and then walk.
            if (auto building = std::get_if<UnitBehaviorStateBuilding>(&unitInfo.state->behaviourState);
                building != nullptr && building->targetUnit == targetUnitId)
            {
                changeState(*unitInfo.state, UnitBehaviorStateIdle());
            }
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
                        // Ask again, for the reason given in createNewUnit:
                        // spawnNewUnits empties the request queue every pass,
                        // so a yard waiting out a blocked pad has to put its
                        // hand up on each tick or it is simply forgotten.
                        sim->unitCreationRequests.push_back(unitInfo.id);
                        return false;
                    },
                    [&](const UnitCreationStatusDone& s) {
                        unitInfo.state->cobEnvironment->createThread("StartBuilding");
                        unitInfo.state->factoryState = FactoryBehaviorStateBuilding{std::make_pair(s.unitId, std::optional<SimVector>())};
                        return false;
                    },
                    [&](const UnitCreationStatusFailed&) {
                        // Nothing sets this for a yard, which waits out a
                        // blocked pad however long it takes (spawnNewUnits);
                        // if something ever does, drop the queue entry.
                        unitInfo.state->factoryState = FactoryBehaviorStateIdle();
                        sim->deactivateUnit(unitInfo.id);
                        return true;
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
                if (!targetUnitOption || targetUnitOption->get().isDead())
                {
                    // TOTALA-EXE-ECONOMY S:134: an externally killed product
                    // flushes the complete primary list, including the rally.
                    unitInfo.state->clearOrders();
                    unitInfo.state->buildQueue.clear();
                    unitInfo.state->cobEnvironment->createThread("StopBuilding");
                    sim->deactivateUnit(unitInfo.id);
                    unitInfo.state->factoryState = FactoryBehaviorStateIdle();
                    return false;
                }

                auto& targetUnit = targetUnitOption->get();
                const auto& targetUnitDefinition = sim->unitDefinitions.at(targetUnit.unitType);

                if (targetUnit.unitType != unitType)
                {
                    if (targetUnit.isBeingBuilt(targetUnitDefinition) && !targetUnit.isDead())
                    {
                        // Death cause 9: the builder takes its own frame back
                        // (0x402701). Nobody's Losses move for it.
                        sim->removeUnfinishedUnit(state.targetUnit->first);
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
                    // The product is released, and the ordinary height rule
                    // applies: a ground or sea unit with a mover is on the
                    // terrain under its centre, so a submarine drops straight
                    // down to the sea bed below the pad (owner's observation,
                    // NAVAL-CONSTRUCTION-DGUN-FINDINGS.md). RWE re-snaps height
                    // only when a unit moves, so it is done here, same x and z.
                    // A floater sits at sea level minus its WaterLine and a
                    // hover craft is held at sea level, as whenever they move
                    // (computeGroundUnitHeight).
                    if (targetUnitDefinition.isMobile && !targetUnitDefinition.canFly)
                    {
                        auto settled = computeGroundUnitHeight(sim->terrain, targetUnitDefinition, targetUnit.position.x, targetUnit.position.z);
                        targetUnit.position.y = settled;
                        targetUnit.previousPosition.y = settled;
                    }

                    // TOTALA-EXE-INTERFACE S:133: only mobile products inherit
                    // the current posture and the route their capabilities allow.
                    if (targetUnitDefinition.isMobile)
                    {
                        targetUnit.moveOrders = unitInfo.state->moveOrders;
                        targetUnit.fireOrders = unitInfo.state->fireOrders;
                        for (const auto& rally : unitInfo.state->orders)
                        {
                            if ((targetUnitDefinition.canMove && std::holds_alternative<MoveOrder>(rally))
                                || (targetUnitDefinition.canPatrol && std::holds_alternative<PatrolOrder>(rally)))
                            {
                                targetUnit.addOrder(rally);
                            }
                        }
                        if (targetUnit.orders.empty())
                        {
                            auto footprintRect = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
                            targetUnit.addOrder(BuggerOffOrder(footprintRect));
                        }
                        sim->events.push_back(UnitFromFactoryEvent{state.targetUnit->first, unitInfo.id});
                    }
                    unitInfo.state->cobEnvironment->createThread("StopBuilding");
                    sim->deactivateUnit(unitInfo.id);
                    unitInfo.state->factoryState = FactoryBehaviorStateIdle();
                    return true;
                }

                // While the product is attached it follows the build piece in
                // all three coordinates (NAVAL-SYSTEMS-PARITY.md section 1).
                // Only a Floater is given a floor, sea level minus its
                // WaterLine, and nothing is ever pushed down. A submarine is
                // not a floater, so it gets none: the owner watched retail TA
                // draw an unfinished sub with its top poking out of the water
                // above the yard (NAVAL-CONSTRUCTION-DGUN-FINDINGS.md,
                // "submarine under construction"). The seabed placement
                // applies when the product is released, below. A hover
                // product keeps RWE's sea-level floor, which the reference
                // does not give it; no shipped yard builds one over water.
                if (targetUnitDefinition.floater)
                {
                    buildPieceInfo.position.y = rweMax(
                        buildPieceInfo.position.y,
                        sim->terrain.getSeaLevel() - intToSimScalar(static_cast<int>(targetUnitDefinition.waterLine)));
                }
                else if (targetUnitDefinition.canHover)
                {
                    buildPieceInfo.position.y = rweMax(buildPieceInfo.position.y, sim->terrain.getSeaLevel());
                }

                tryApplyMovementToPosition(sim->getUnitInfo(state.targetUnit->first), buildPieceInfo.position);
                targetUnit.rotation = buildPieceInfo.rotation;

                // The claim on the frame is staked before the economy is
                // asked for anything (0x41BA9D sets event bit 15 on the
                // target at the top of the build routine), so a factory
                // stalled for metal still holds the frame's decay off.
                targetUnit.nanoframeWorkedOn = true;

                auto costs = targetUnit.getBuildCostInfo(targetUnitDefinition, unitInfo.definition->workerTimePerTick);
                auto gotResources = sim->addResourceDelta(
                    unitInfo.id,
                    -Energy(targetUnitDefinition.buildCostEnergy.value * static_cast<float>(unitInfo.definition->workerTimePerTick) / static_cast<float>(targetUnitDefinition.buildTime)),
                    -Metal(targetUnitDefinition.buildCostMetal.value * static_cast<float>(unitInfo.definition->workerTimePerTick) / static_cast<float>(targetUnitDefinition.buildTime)),
                    -costs.energyCost,
                    -costs.metalCost);

                if (!gotResources)
                {
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
        // The frame may already be gone. One shot on the pad is swept off
        // the unit list at the end of the tick it died in, and the order
        // that empties the queue can land on the next, so the id the plant
        // still holds names nothing -- and getUnitState throws on it. That
        // is the ordinary case at a factory under fire, which is exactly
        // when the AI cancels a queue (PR #82, found as a crash in the
        // arena), and a player cancelling at the same moment met it too.
        // A frame that is still there is removed as before.
        auto removeFrameIfStanding = [&](UnitId frameId) {
            if (auto frame = sim->tryGetUnitState(frameId); frame && !frame->get().isDead())
            {
                const auto& definition = sim->unitDefinitions.at(frame->get().unitType);
                if (frame->get().isBeingBuilt(definition) && definition.buildTime > 0)
                {
                    // S:134: cancellation truncates before the income handicap;
                    // the refund settles with the rest of this second's income.
                    auto refund = std::trunc(static_cast<double>(frame->get().buildTimeCompleted)
                        * definition.buildCostMetal.value / definition.buildTime);
                    sim->addResourceDelta(unitInfo.id, Energy(0), Metal(static_cast<float>(refund)));
                    sim->removeUnfinishedUnit(frameId);
                }
            }
        };

        match(
            unitInfo.state->factoryState,
            [&](const FactoryBehaviorStateIdle&) {
                // do nothing
            },
            [&](const FactoryBehaviorStateCreatingUnit& state) {
                match(
                    state.status,
                    [&](const UnitCreationStatusDone& d) {
                        removeFrameIfStanding(d.unitId);
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
                    removeFrameIfStanding(state.targetUnit->first);
                    unitInfo.state->cobEnvironment->createThread("StopBuilding");
                }
                sim->deactivateUnit(unitInfo.id);
                unitInfo.state->factoryState = FactoryBehaviorStateIdle();
            });
    }

    SimVector UnitBehaviorService::getNanoPoint(UnitId id)
    {
        auto& unit = sim->getUnitState(id);

        // Asked already this tick: the same nozzle, not the next one. The
        // query alternates on a unit that has two of them, so asking twice in
        // one tick pins the spray to one side for ever -- see
        // UnitState::nanoPointQueriedAt.
        if (unit.nanoPointQueriedAt == sim->gameTime)
        {
            return unit.nanoPoint;
        }

        auto pieceId = runCobQuery(id, "QueryNanoPiece");
        unit.nanoPoint = pieceId ? getPiecePosition(id, *pieceId) : unit.position;
        unit.nanoPointQueriedAt = sim->gameTime;
        return unit.nanoPoint;
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
            [this](UnitId id) { return tryGetSweetSpot(id); },
            [this](ProjectileId id) -> std::optional<SimVector> {
                auto projectile = sim->projectiles.tryGet(id);
                if (!projectile || projectile->get().isDead)
                {
                    return std::nullopt;
                }
                return projectile->get().position;
            });
    }

    std::optional<SimVector> UnitBehaviorService::getTargetPosition(const AttackTarget& target)
    {
        // An order names a unit or a place. A weapon's target can also be a
        // projectile, which no order can ask for, so the narrower thing is
        // widened rather than the two answers being written out twice.
        return getTargetPosition(match(
            target,
            [](UnitId id) -> UnitWeaponAttackTarget { return id; },
            [](const SimVector& v) -> UnitWeaponAttackTarget { return v; }));
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
            if (movingState && movingState->pathRequested)
            {
                // A search for this unit is already in flight. Let it land:
                // the goal has only just moved, and throwing the search away
                // buys a whole component's worth of expansions and nothing
                // else. The new goal is in desiredDestination already, so this
                // branch is entered again on the tick the search completes.
            }
            else
            {
                auto destination = resolvePathDestination(*unitInfo.state, goal);
                auto previousRequestTime = movingState ? movingState->lastPathRequestTime : std::nullopt;

                if (movingState && movingState->path && !movingState->pathIsStandIn
                    && destinationsAreClose(destination, movingState->pathDestination))
                {
                    // Same goal drifting: keep the route in hand and let the
                    // rate-limited request below fetch a fresher one.
                    movingState->movementGoal = goal;
                    movingState->wantsPath = true;
                }
                else
                {
                    // Walk at it in a straight line for now, and ask for a route.
                    //
                    // The original does exactly this and it matters more than
                    // anything else about its pathfinder: 0x44F3F2 stores a
                    // two-point path -- where the unit is standing, then the
                    // goal -- sets the "I have a path" flag alongside the "I
                    // want a path" one, and the unit is moving on the tick the
                    // order was given. The real route overwrites it whenever
                    // the search gets round to that unit.
                    //
                    // Without it a unit does nothing at all until its search
                    // completes, and with four hundred a side that is seconds
                    // of an army standing still. It also makes a truncated or
                    // slow path a matter of walking an inelegant line rather
                    // than not walking.
                    UnitPath straightLine;
                    // Two points, where the unit is standing and then the goal,
                    // which is what 0x44F3F2 writes. The first is the segment
                    // the follower steers along; it costs nothing on a straight
                    // run and is there for the tick the unit stops being on the
                    // line.
                    straightLine.waypoints.push_back(unitInfo.state->position);
                    straightLine.waypoints.push_back(match(
                        destination,
                        [&](const SimVector& v) { return v; },
                        [&](const DiscreteRect& r) {
                            // The same centre PathFindingService aims at for a
                            // rect destination, so the stand-in heads where the
                            // route will.
                            auto corner = sim->terrain.heightmapIndexToWorldCorner(r.x, r.y);
                            auto halfWidth = (SimScalar(r.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss;
                            auto halfHeight = (SimScalar(r.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss;
                            auto center = corner + SimVector(halfWidth, 0_ss, halfHeight);
                            center.y = sim->terrain.getHeightAt(center.x, center.z);
                            return center;
                        }));

                    unitInfo.state->navigationState.state = NavigationStateMoving{
                        goal,
                        destination,
                        PathFollowingInfo(std::move(straightLine), sim->gameTime),
                        false};
                    movingState = std::get_if<NavigationStateMoving>(&unitInfo.state->navigationState.state);
                    movingState->pathIsStandIn = true;
                    movingState->wantsPath = true;
                    movingState->lastPathRequestTime = previousRequestTime;
                }
            }
        }
        else
        {
            auto resolvedDestination = resolvePathDestination(*unitInfo.state, goal);
            if (resolvedDestination != movingState->pathDestination)
            {
                // The resolved position of our goal has changed.
                // We'll assume that this change isn't too big
                // i.e. we don't need to throw away our previous path,
                // we can still continue following it
                // while we wait for a new path to be computed.
                //
                // pathDestination only advances here when nothing is in
                // flight: moving it under a search is what gets that search
                // thrown away the next time it is resumed.
                if (!movingState->pathRequested)
                {
                    movingState->pathDestination = resolvedDestination;
                }
                movingState->wantsPath = true;
            }

            if (unitInfo.state->inCollision && !movingState->pathRequested)
            {
                if (!movingState->path || (sim->gameTime - movingState->path->pathCreationTime) >= GameTime(30))
                {
                    movingState->wantsPath = true;
                }
            }
        }

        // The one place a path is asked for, and so the one place the rate
        // limit has to be applied. A unit still on the stand-in has not been
        // served yet and wants one the moment the limit lets it ask again,
        // which is what makes the limit a delay rather than a lost request.
        if (movingState->pathIsStandIn)
        {
            movingState->wantsPath = true;
        }
        if (movingState->wantsPath && !movingState->pathRequested
            && (!movingState->lastPathRequestTime
                || (sim->gameTime - *movingState->lastPathRequestTime) >= GameTime(PathRequestCooldownTicks)))
        {
            movingState->pathDestination = resolvePathDestination(*unitInfo.state, movingState->movementGoal);
            sim->requestPath(unitInfo.id);
            movingState->pathRequested = true;
            movingState->wantsPath = false;
            movingState->lastPathRequestTime = sim->gameTime;
        }

        if (movingState->path)
        {
            auto groundPhysics = std::get_if<UnitPhysicsInfoGround>(&unitInfo.state->physics);
            if (groundPhysics == nullptr)
            {
                throw std::logic_error("ground unit does not have ground physics");
            }
            if (followPath(*sim, unitInfo, *groundPhysics, *movingState->path))
            {
                // We finished following the path.
                // This doesn't necessarily mean we are at the goal.
                // The path might have been a partial path.
                // Ask for a new path to get us the rest of the way there,
                // through the same rate limit as everything else.
                if (!movingState->path || (sim->gameTime - movingState->path->pathCreationTime) >= GameTime(30))
                {
                    movingState->wantsPath = true;
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
            [&](const UnitPhysicsInfoAir& /*p*/) {
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

    bool UnitBehaviorService::siteWithinReachOfUnreachableSite(UnitInfo unitInfo, const UnitDefinition& siteDefinition, const SimVector& site) const
    {
        const auto dx = static_cast<double>(simScalarToFloat(unitInfo.state->position.x - site.x));
        const auto dz = static_cast<double>(simScalarToFloat(unitInfo.state->position.z - site.z));
        // Whole world units, from the high word of the original's 16.16.
        const auto distance = std::floor(std::sqrt(dx * dx + dz * dz));

        auto halfDiagonal = [&](const UnitDefinition& definition) {
            const auto [fx, fz] = sim->getFootprintXZ(definition.movementCollisionInfo);
            return 8.0 * std::sqrt(static_cast<double>(fx) * static_cast<double>(fx) + static_cast<double>(fz) * static_cast<double>(fz));
        };

        const auto left = distance - halfDiagonal(*unitInfo.definition) - halfDiagonal(siteDefinition);
        return left <= static_cast<double>(simScalarToFloat(unitInfo.definition->buildDistance));
    }

    UnitCreationStatus UnitBehaviorService::createNewUnit(UnitInfo unitInfo, const std::string& unitType, const SimVector& position)
    {
        if (auto s = std::get_if<UnitBehaviorStateCreatingUnit>(&unitInfo.state->behaviourState))
        {
            if (s->unitType == unitType && s->position == position)
            {
                // Still waiting on a site that was occupied: ask again. The
                // request queue is emptied on every pass of spawnNewUnits, so
                // a job that is not re-asked for is never looked at again --
                // which is why a blocked site used to get exactly one try.
                if (std::holds_alternative<UnitCreationStatusPending>(s->status))
                {
                    sim->unitCreationRequests.push_back(unitInfo.id);
                }
                return s->status;
            }
        }

        const auto& targetUnitDefinition = sim->unitDefinitions.at(unitType);
        auto footprintRect = sim->computeFootprintRegion(position, targetUnitDefinition.movementCollisionInfo);
        if (navigateTo(unitInfo, footprintRect))
        {
            // If we only got "as close as we could" because the site cannot be
            // reached, give the order up rather than lathing across a wall --
            // and say so. This is a different refusal from a site that is
            // merely occupied: there is nothing to wait for, because nothing
            // about the ground in between is going to change.
            //
            // BT-97 (0x403B63): not when the site is still inside the builder's
            // reach from where it stopped. The original measures the flat
            // distance from the builder to the site, takes off half of each
            // footprint's diagonal (the root of the two cell counts squared,
            // times 8) and, if what is left is within the build distance,
            // ignores the failure and carries on as for an arrival -- so a
            // builder on one bank of a stream builds across it. Otherwise it
            // says "I can't reach the construction site" (0x501580).
            if (auto moving = std::get_if<NavigationStateMoving>(&unitInfo.state->navigationState.state); moving != nullptr && moving->reachableDestination)
            {
                if (!siteWithinReachOfUnreachableSite(unitInfo, targetUnitDefinition, position))
                {
                    sim->events.push_back(UnitCannotComplyEvent{unitInfo.id, "I can't reach the construction site"});
                    return UnitCreationStatusFailed();
                }
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

                    // This is the creation tick, as far as the builder is
                    // concerned: RWE lays the frame down in spawnNewUnits at
                    // the end of the tick the request went in, so the first
                    // tick a builder can lathe it is this one, the tick it
                    // picks the finished creation up. The original creates and
                    // lathes in one handler on one tick; the two are a tick
                    // apart here and the lathes still start on the first tick
                    // there is a frame to lathe. TOTALA-EXE.md 107.
                    return deployBuildArm(unitInfo, d.unitId, true);
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
            return true;
        }

        // A unit and a feature are reached by different rules in the original,
        // and they are different handlers: ReclaimUnit (0x404730) against
        // Reclaim (0x404AD0).
        //
        // A unit is reached by the reclaimer's own Builddistance. ReclaimUnit
        // state 4 loads it from def+0x212 at 0x4048F4, squares it at 0x40494E
        // and compares against dx^2 + dz^2. (It adds a term off the target's
        // definition, def+0x184, read at that one instruction and written
        // nowhere else in the binary, so it contributes nothing.) Measured
        // here to the footprint, the way building measures it.
        //
        // A feature has no reach test anywhere in its handler. Reclaim state 0
        // (0x404B5B) checks canreclamate and installs a move goal on the
        // feature's own square (0x438AD0); the later states work out the job's
        // length, turn to face it and work. Arriving is the whole test, which
        // is what navigateTo returns.
        //
        // What stood here was a flat 300 for both, under a comment admitting
        // it was a guess: seven and a half times an ARMCK's forty, which is
        // what let a builder lathe a wreck from across the yard rather than
        // walking up to it.
        // A feature is measured the same way here, and that is RWE's own
        // approximation rather than the original's rule: gating a feature on
        // having arrived at its move goal, which is what the original does,
        // never reports arrival in RWE, because the feature's own footprint
        // is blocking and the goal cells are inside it. Builddistance to the
        // footprint expresses the same intent -- walk up to it, then work --
        // in the idiom RWE's navigation actually has.
        auto rect = match(
            target,
            [&](const UnitId& id) {
                const auto& u = sim->getUnitState(id);
                return sim->computeFootprintRegion(u.position, sim->unitDefinitions.at(u.unitType).movementCollisionInfo);
            },
            [&](const FeatureId& id) {
                const auto& f = sim->getFeature(id);
                const auto& d = sim->getFeatureDefinition(f.featureName);
                return sim->computeFootprintRegion(f.position, static_cast<unsigned int>(d.footprintX), static_cast<unsigned int>(d.footprintZ));
            });

        // BT-92 (0x404730, work check at 0x4048FB): the ground handler's reach
        // is the build distance plus a term of the target's own, the integer
        // part of (X extent + Z extent) / 3 in world units. The aircraft
        // handler adds nothing. The extents are the definition's +0x176 and
        // +0x17E, which the loader writes at the 0x42D10A block as the
        // footprint in world units, 16 a cell; that reading is the corpus's
        // (TOTALA-EXE-DATA.md) and matches the finding's "roughly 10 to 40".
        SimScalar extraReach = 0_ss;
        if (const auto* targetUnitId = std::get_if<UnitId>(&target); targetUnitId != nullptr && !unitInfo.definition->canFly)
        {
            const auto& targetUnit = sim->getUnitState(*targetUnitId);
            const auto [fx, fz] = sim->getFootprintXZ(sim->unitDefinitions.at(targetUnit.unitType).movementCollisionInfo);
            extraReach = intToSimScalar(static_cast<int>((fx * 16u + fz * 16u) / 3u));
        }

        if (!withinBuildReachOfRect(unitInfo, rect, extraReach))
        {
            // Walk at the thing itself, not at a ring drawn one build
            // distance around it. hasReachedGoal reports success as soon as
            // the pathfinder has got as close as it can, and on a ring that
            // can be a cell short of the reach test -- which parks the
            // builder a few units outside its own arm for ever, the same
            // standing-about failure as B4 #57. Aimed at the footprint, the
            // relaxation lands on the feature itself when nothing blocks it
            // and alongside when something does, and both are well inside
            // Builddistance.
            auto arrived = navigateTo(unitInfo, rect);

            // For a feature, arriving is the original's whole test (see
            // above), and navigateTo also says "arrived" when the pathfinder
            // could get no closer -- a wreck on the sea floor, with the
            // builder on the shore. Waiting for Builddistance there stalled
            // for ever with no beam and no complaint, which is what a
            // play-test saw as "reclaiming under water from land does not
            // work". A unit target keeps the strict reach: ReclaimUnit
            // (0x404730) does test Builddistance.
            if (!arrived || !std::holds_alternative<FeatureId>(target))
            {
                return false;
            }
        }

        return deployReclaimArm(unitInfo, target);
    }

    std::optional<UnitId> UnitBehaviorService::factoryBuilding(UnitId frameId) const
    {
        for (const auto& [id, unit] : sim->units)
        {
            auto fs = std::get_if<FactoryBehaviorStateBuilding>(&unit.factoryState);
            if (fs && fs->targetUnit && fs->targetUnit->first == frameId)
            {
                return UnitId(id);
            }
        }
        return std::nullopt;
    }

    bool UnitBehaviorService::buildExistingUnit(UnitInfo unitInfo, UnitId targetUnitId, std::optional<UnitId> standNextTo)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);

        if (!targetUnitRef || targetUnitRef->get().isDead() || !targetUnitRef->get().isBeingBuilt(sim->unitDefinitions.at(targetUnitRef->get().unitType)))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();

        // What a builder has to get alongside is not always what it is
        // lathing. A unit coming out of a factory stands at the factory's
        // build piece -- the middle of a building nothing can walk into -- and
        // measuring the reach to *that* put every assister permanently out of
        // range: an ARMCK reaches 40 and a vehicle plant is 128 across, so the
        // frame's own footprint sits some 48 units inside the nearest cell the
        // builder can stand on. It walked as close as the yard allowed, found
        // itself short, and stood there for the rest of the game, which is
        // exactly what the play-test reported. A guard assisting a factory
        // therefore measures to the factory.
        auto standNextToRef = standNextTo ? sim->tryGetUnitState(*standNextTo) : std::nullopt;
        const auto& reachUnit = standNextToRef ? standNextToRef->get() : targetUnit;

        // Reach is measured to the footprint, not the centre, so a builder
        // stops as soon as it is within arm's length of any edge instead of
        // walking round to the front.
        if (!withinBuildReach(unitInfo, reachUnit))
        {
            // Out of arm's length: the spray stops until it is back in range.
            if (auto buildingState = std::get_if<UnitBehaviorStateBuilding>(&unitInfo.state->behaviourState))
            {
                buildingState->nanoParticleOrigin = std::nullopt;
            }
            const auto& reachDefinition = sim->unitDefinitions.at(reachUnit.unitType);
            auto rect = sim->computeFootprintRegion(reachUnit.position, reachDefinition.movementCollisionInfo);
            auto reachTiles = std::max(0, static_cast<int>(simScalarToFloat(unitInfo.definition->buildDistance) / simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits)) - 1);
            if (navigateTo(unitInfo, rect.expand(reachTiles)))
            {
                // MA-01 / RA-03: rectangle arrival can be outside the work
                // circle near a corner. Keep approaching instead of asking
                // forever for a goal navigation already considers reached.
                navigateTo(unitInfo, rect);
            }
            return false;
        }

        return deployBuildArm(unitInfo, targetUnitId);
    }

    void UnitBehaviorService::updatePendingArmStow(UnitInfo unitInfo)
    {
        auto& unit = *unitInfo.state;
        if (!unit.armStowDueTime)
        {
            return;
        }

        // Work found in the meantime: the arm stays out and simply swings
        // round to it, which is the whole point of the delay.
        if (std::holds_alternative<UnitBehaviorStateBuilding>(unit.behaviourState)
            || std::holds_alternative<UnitBehaviorStateReclaiming>(unit.behaviourState))
        {
            unit.armStowDueTime = std::nullopt;
            return;
        }

        if (sim->gameTime < *unit.armStowDueTime)
        {
            return;
        }

        unit.armStowDueTime = std::nullopt;
        unit.cobEnvironment->createThread("StopBuilding");
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
            // Not on the spot: a builder that takes up another job within the
            // grace keeps its arm out and only turns to it. See
            // UnitState::armStowDueTime and updatePendingArmStow.
            unit.armStowDueTime = sim->gameTime + GameTime(ArmStowGraceTicks);
        }
        else if (willBeWorking)
        {
            // Working again, so whatever stow was pending is off.
            unit.armStowDueTime = std::nullopt;
        }
        unit.behaviourState = newState;
    }
    bool UnitBehaviorService::latheNanoframe(UnitInfo unitInfo, UnitId targetUnitId)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);
        if (!targetUnitRef)
        {
            return false;
        }
        auto& targetUnit = targetUnitRef->get();
        const auto& targetUnitDefinition = sim->unitDefinitions.at(targetUnit.unitType);

        auto buildingState = std::get_if<UnitBehaviorStateBuilding>(&unitInfo.state->behaviourState);
        if (buildingState == nullptr || buildingState->targetUnit != targetUnitId)
        {
            return false;
        }

        // As in the factory path above: the builder claims the frame
        // before the economy is consulted, so a builder waiting on
        // metal still keeps the frame from decaying.
        targetUnit.nanoframeWorkedOn = true;

        auto costs = targetUnit.getBuildCostInfo(targetUnitDefinition, unitInfo.definition->workerTimePerTick);
        auto gotResources = sim->addResourceDelta(
            unitInfo.id,
            -Energy(targetUnitDefinition.buildCostEnergy.value * static_cast<float>(unitInfo.definition->workerTimePerTick) / static_cast<float>(targetUnitDefinition.buildTime)),
            -Metal(targetUnitDefinition.buildCostMetal.value * static_cast<float>(unitInfo.definition->workerTimePerTick) / static_cast<float>(targetUnitDefinition.buildTime)),
            -costs.energyCost,
            -costs.metalCost);

        if (!gotResources)
        {
            buildingState->nanoParticleOrigin = std::nullopt;
            return false;
        }
        buildingState->nanoParticleOrigin = getNanoPoint(unitInfo.id);

        if (targetUnit.addBuildProgress(targetUnitDefinition, unitInfo.definition->workerTimePerTick))
        {
            sim->events.push_back(UnitCompleteEvent{targetUnitId});

            if (targetUnitDefinition.activateWhenBuilt)
            {
                sim->activateUnit(targetUnitId);
            }

            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        return false;
    }

    bool UnitBehaviorService::deployBuildArm(UnitInfo unitInfo, UnitId targetUnitId, bool frameJustCreated)
    {
        auto targetUnitRef = sim->tryGetUnitState(targetUnitId);
        if (!targetUnitRef || targetUnitRef->get().isDead() || !targetUnitRef->get().isBeingBuilt(sim->unitDefinitions.at(targetUnitRef->get().unitType)))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }
        auto& targetUnit = targetUnitRef->get();

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

                // A construction aircraft does not wait: the original's two
                // VTOL build missions never make it. VTOL_MobileBuild calls
                // the INBUILDSTANCE wait and throws the answer away, and
                // VTOL_HelpBuild does not call it at all (TOTALA-EXE.md 107).
                // Only a ground builder and a factory wait, and what they are
                // waiting for is their own script's deploy sequence, which is
                // mod data rather than engine behaviour.
                if (!unitInfo.definition->canFly && !unitInfo.state->inBuildStance)
                {
                    return false;
                }

                return latheNanoframe(unitInfo, targetUnitId);
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

                // Square on to the job before the arm goes out (WorkDeployFacingTolerance).
                if (!builderIsSquareOnTo(*unitInfo.state, *unitInfo.definition, targetUnit.position))
                {
                    return false;
                }

                auto nanoFromPosition = getNanoPoint(unitInfo.id);
                auto headingAndPitch = computeLineOfSightHeadingAndPitch(unitInfo.state->rotation, nanoFromPosition, targetUnit.position);
                auto heading = headingAndPitch.first;
                auto pitch = headingAndPitch.second;

                changeState(*unitInfo.state, UnitBehaviorStateBuilding{targetUnitId, std::nullopt});
                unitInfo.state->cobEnvironment->createThread("StartBuilding", {toCobAngle(heading).value, toCobAngle(pitch).value});

                if (!frameJustCreated || !unitInfo.definition->canFly)
                {
                    return false;
                }

                // TOTALA-EXE.md 107: a construction aircraft lathes TWICE on
                // the tick it creates its nanoframe. Its mission creates the
                // frame, queues StartBuilding and returns 1, which lets the
                // service loop run the lathe state in the same tick; that
                // state calls the stance wait, whose side effect leaves event
                // bit 4 in the wake mask, and the COB `set` every builder
                // script has left pending matches it, so the loop runs the
                // lathe a second time before the tick ends. This is the same
                // three steps in the same order -- the deploy above, then two
                // lathes -- and it is the whole of the tick a construction
                // aircraft finishes ahead of a factory.
                if (latheNanoframe(unitInfo, targetUnitId))
                {
                    return true;
                }
                return latheNanoframe(unitInfo, targetUnitId);
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

                // RA-04: aircraft feature reclaim has no build-stance gate.
                if (!unitInfo.state->inBuildStance
                    && !(unitInfo.definition->canFly && std::holds_alternative<FeatureId>(target)))
                {
                    return false;
                }

                // Sound slot 11, `working` -- `reclaim1` for every shipped
                // construction category. Both of the original's reclaim
                // handlers play it once as work actually starts (Reclaim at
                // 0x404C69, ReclaimUnit at 0x4048B5), which is this tick: the
                // builder is in reach and the arm is out. An empty
                // nanoParticleOrigin is what says no work has been done on
                // this job yet -- it is set just below and only ever
                // refreshed thereafter, and a new job arrives through
                // UnitBehaviorStateIdle with a fresh one. See TOTALA-EXE.md
                // §97.
                if (!reclaimingState.nanoParticleOrigin)
                {
                    sim->events.push_back(UnitStartedReclaimingEvent{unitInfo.id});
                }

                reclaimingState.nanoParticleOrigin = getNanoPoint(unitInfo.id);

                return match(
                    target,
                    [&](const UnitId& targetUnitId) {
                        // ReclaimUnit's state 4 (0x4048D2) takes the unit
                        // apart a bite at a time: a pass every two ticks
                        // counts mission+0x3A up by two, and once it has
                        // reached fifteen the step 0x438650 sized is applied
                        // as damage and the count starts again. Sixteen ticks
                        // a bite, then, and the target's health bar is what
                        // the info panel shows for the progress.
                        auto targetRef = sim->tryGetUnitState(targetUnitId);
                        if (!targetRef || targetRef->get().isDead())
                        {
                            changeState(*unitInfo.state, UnitBehaviorStateIdle());
                            return true;
                        }

                        reclaimingState.stepCounter += 1;
                        if (reclaimingState.stepCounter < 16)
                        {
                            return false;
                        }
                        reclaimingState.stepCounter = 0;

                        const auto& targetDefinition = sim->unitDefinitions.at(targetRef->get().unitType);
                        auto damage = computeUnitReclaimStep(unitInfo.definition->workerTime, unitInfo.state->kills, targetDefinition.maxHitPoints, targetDefinition.buildCostMetal, veterancyRules(unitInfo.definition->veterancy, sim->communityPatchMode));
                        auto finished = sim->reclaimUnitStep(targetUnitId, unitInfo.state->owner, damage);
                        if (finished)
                        {
                            changeState(*unitInfo.state, UnitBehaviorStateIdle());
                        }
                        return finished;
                    },
                    [&](const FeatureId& targetFeatureId) {
                        // PLAYTEST-RECLAIM-ASSIST-FINDINGS RA-04: a per-order
                        // countdown, independent of WorkerTime and wreck HP.
                        // Tick zero is the first decrement; ground jobs have
                        // one additional two-tick visit before payout.
                        const auto& feature = sim->getFeature(targetFeatureId);
                        const auto& definition = sim->getFeatureDefinition(feature.featureName);
                        auto timer = static_cast<unsigned int>((unitInfo.definition->canFly ? 30.0 : 15.0)
                            + (static_cast<double>(definition.metal) + definition.energy) / 2.0);
                        auto elapsed = 2u * ((timer + 1u) / 2u) + (unitInfo.definition->canFly ? 0u : 2u);
                        if (reclaimingState.stepCounter++ < elapsed)
                        {
                            return false;
                        }
                        // Complete in one operation. Legacy saved shared work
                        // is deducted by reclaimFeature to avoid paying twice.
                        auto finished = sim->reclaimFeature(targetFeatureId, unitInfo.state->owner,
                            computeFeatureReclaimWork(definition, feature.hitPoints));
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
                // Square on to the job before the arm goes out (WorkDeployFacingTolerance).
                if (!builderIsSquareOnTo(*unitInfo.state, *unitInfo.definition, targetPosition))
                {
                    return false;
                }

                auto headingAndPitch = computeLineOfSightHeadingAndPitch(unitInfo.state->rotation, nanoFromPosition, targetPosition);
                auto heading = headingAndPitch.first;
                auto pitch = headingAndPitch.second;

                changeState(*unitInfo.state, UnitBehaviorStateReclaiming{target, std::nullopt});
                // RA-04: aircraft feature jobs query the nano piece without
                // invoking StartBuilding. Unit reclaim keeps its existing path.
                if (!(unitInfo.definition->canFly && std::holds_alternative<FeatureId>(target)))
                {
                    unitInfo.state->cobEnvironment->createThread("StartBuilding", {toCobAngle(heading).value, toCobAngle(pitch).value});
                }
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

        // A Remote player's unit is the owner's to mend -- its health lives on
        // the other machine -- so a local repairer cannot touch it.
        if (!simulatesLocally(*sim, targetUnit.owner))
        {
            changeState(*unitInfo.state, UnitBehaviorStateIdle());
            return true;
        }

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

                // Wait for the arm to come up -- but only if there is an arm.
                // **None of the four `isairbase` units in the shipped data has
                // a `StartBuilding` thread at all**: ARMASP and CORASP carry
                // SmokeUnit, Create, SweetSpot, QueryLandingPad, QueryNanoPiece
                // and Killed, and the two carriers not even QueryNanoPiece.
                // Their `Create` never touches INBUILDSTANCE either. So the
                // thread created below found nothing, the stance was never set,
                // and a pad waiting for it waited for ever -- which is the
                // third and decisive reason the pads never mended anything.
                // The original has no stance test anywhere in its repair tick
                // (0x41BD10); RWE's is a sequencing device for the nanolathe
                // animation, so it applies to a unit that has one.
                if (!unitInfo.state->inBuildStance && unitHasBuildArm(*unitInfo.state))
                {
                    return false;
                }

                buildingState.nanoParticleOrigin = getNanoPoint(unitInfo.id);

                // The repair tick, 0x41BD10 -- the one routine every repairer
                // in the game goes through: the two ground Repair missions,
                // VTOL_RepairUnit, and the SELFREPAIR a landing aircraft is
                // handed by VTOL_Landing (issue #52, §94). It works out two
                // numbers and then throws most of them away:
                //
                //   work   = WorkerTime / 30                 (integer)
                //   hp     = trunc((maxdamage       * work - 1) / buildtime + 1)
                //   energy = trunc((buildcostenergy * work - 1) / buildtime + 1)
                //   hp = min(hp, 1)       0x41BD87
                //   energy = min(energy, 1)  0x41BD97
                //
                // Both clamps are *upper* bounds, and that is the whole of the
                // behaviour. Since the expression reaches 1 whenever the
                // product does, it collapses to: a repairer with any worker
                // time at all mends exactly one hit point a tick and pays
                // exactly one energy for it, whatever it is mending and
                // however fast a worker it is; one with none mends nothing.
                // Repair scales with the number of repairers, not with their
                // WorkerTime, and nothing in the shipped data has a WorkerTime
                // between 1 and 29, so the second case is unreachable there.
                //
                // RWE had `max` where the original has `min`, which let a
                // construction vehicle mend a dragon's tooth -- 3500 hit
                // points on a buildtime of 520 -- at forty hit points a tick
                // instead of one. A fifth of all repairer/target pairs in the
                // shipped data diverged.
                auto work = unitInfo.definition->workerTimePerTick;
                auto healthPerTick = targetUnitDefinition.maxHitPoints * work >= 1u ? 1u : 0u;
                auto energyPerTick = Energy(targetUnitDefinition.buildCostEnergy.value * static_cast<float>(work) >= 1.0f ? 1.0f : 0.0f);

                // And it is not free, which the comment this replaces claimed:
                // 0x41BDB7 asks the repairer's own economy block for the
                // energy and 0x41BDBC does nothing at all if it is turned
                // down. So a player in energy debt cannot repair.
                if (!sim->addEnergyRequest(unitInfo.id, energyPerTick))
                {
                    return false;
                }

                targetUnit.hitPoints = std::min(targetUnitDefinition.maxHitPoints, targetUnit.hitPoints + healthPerTick);

                if (targetUnit.hitPoints >= targetUnitDefinition.maxHitPoints)
                {
                    // "Unit repaired", sound slot 10, from the repairer's
                    // side: RepairUnit and VTOL_RepairUnit (0x415219) say
                    // it as the job lands.
                    sim->events.push_back(UnitRepairedEvent{unitInfo.id});
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

                // Square on to the job before the arm goes out (WorkDeployFacingTolerance).
                if (!builderIsSquareOnTo(*unitInfo.state, *unitInfo.definition, targetUnit.position))
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

    bool UnitBehaviorService::unitHasBuildArm(const UnitState& unit) const
    {
        // "Does this unit's script raise a nanolathe arm?" A unit with no
        // StartBuilding thread has nothing to deploy and nothing to wait for.
        if (!unit.cobEnvironment)
        {
            return false;
        }

        const auto& functions = unit.cobEnvironment->_script->functions;
        return std::any_of(functions.begin(), functions.end(), [](const auto& f) { return f.name == "StartBuilding"; });
    }

    std::optional<std::pair<UnitId, SimVector>> UnitBehaviorService::airBaseLandingPoint(UnitInfo unitInfo)
    {
        // An aircraft coming down on a repair pad stops on the pad's *deck*,
        // not on the ground under it. The original asks the pad's own script
        // where that is: `VTOL_Landing` calls `QueryLandingPad` (0x501C14) at
        // 0x411A35 and again at 0x411CEC, gets back up to four piece ids,
        // picks one that is free, keeps it on the mission at `mission+0x36`
        // and hands it to the navigator as a *piece* goal (0x44E250 at
        // 0x411D60). ARMASP's script answers with piece 1, `landpad`, and that
        // piece sits at (0, 20, 0) in the model -- twenty world units up. RWE
        // descended to the terrain instead, so an aircraft landing on a pad
        // sank through the platform and came to rest inside the ground.
        //
        // Only the height is taken from the piece. Its x and z are the pad's
        // own on ARMASP -- `landpad` is at (0, 20, 0) -- but **not** on a
        // carrier, whose deck pieces stand well off the hull (ARMCARRY's at
        // z=+52.5 and z=-37.5, CORCARRY's 22.8 and 35.1 units away). That
        // distance is not this function's problem to solve -- it is the reason
        // the aircraft is walked onto the piece over the descent at all, and
        // the reason `handleLandOnAirBaseOrder`'s goal needs to be the piece
        // rather than the origin.
        if (unitInfo.state->orders.empty())
        {
            return std::nullopt;
        }

        auto order = std::get_if<LandOnAirBaseOrder>(&unitInfo.state->orders.front());
        if (order == nullptr)
        {
            return std::nullopt;
        }

        auto padRef = sim->tryGetUnitState(order->target);
        if (!padRef || padRef->get().isDead())
        {
            return std::nullopt;
        }
        const auto& pad = padRef->get();

        // And only when it really is over the pad: an aircraft that gave up
        // and set down somewhere else lands on the ground like anything else.
        //
        // Measured to the pad's own origin, which is the right anchor for the
        // question being asked -- *is the aircraft over the hull?* -- and is
        // why this is not where a carrier goes wrong. The reach is 48 world
        // units for a 6x6 and ARMCARRY's bow deck piece is 52.5 away, so an
        // aircraft starts its descent inside the reach and then walks out of it
        // toward the piece, losing the answer on the way. What is wrong is the
        // goal it is walking toward, not the distance measured here: see
        // `handleLandOnAirBaseOrder`, which navigates to `pad.position` while
        // the landing point is the deck piece, and `descendToGroundLevel`,
        // which is what walks it out of the reach in the first place.
        auto reach = airBaseRepairReach(*sim, sim->unitDefinitions.at(pad.unitType));
        if (distanceSquaredXZ(unitInfo.state->position, pad.position) > reach * reach)
        {
            return std::nullopt;
        }

        auto pieceId = runCobQuery(order->target, "QueryLandingPad");
        if (!pieceId)
        {
            return std::nullopt;
        }

        return std::make_pair(order->target, getPiecePosition(order->target, static_cast<unsigned int>(*pieceId)));
    }

    bool UnitBehaviorService::descendToGroundLevel(UnitInfo unitInfo)
    {
        auto landingPoint = airBaseLandingPoint(unitInfo);
        auto targetHeight = landingPoint
            ? landingPoint->second.y
            : sim->terrain.getHeightAt(unitInfo.state->position.x, unitInfo.state->position.z);

        // Settle onto the piece as it comes down. The original has no need of
        // this -- its landing goal *is* the pad's piece, so the aircraft has
        // already flown to it before the descent begins -- but RWE descends
        // where it arrived, which is anywhere inside the eight-unit arrival
        // tolerance plus a tick of its own speed. Walking the last few units
        // in over the descent puts it on the middle of the deck rather than
        // half off the edge.
        if (landingPoint)
        {
            auto step = 1_ss;
            auto dx = landingPoint->second.x - unitInfo.state->position.x;
            auto dz = landingPoint->second.z - unitInfo.state->position.z;
            unitInfo.state->position.x += rweMax(-step, rweMin(dx, step));
            unitInfo.state->position.z += rweMax(-step, rweMin(dz, step));
        }

        unitInfo.state->position.y = rweMax(unitInfo.state->position.y - 1_ss, targetHeight);

        return unitInfo.state->position.y == targetHeight;
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

    bool UnitBehaviorService::tryTransitionFromAirToGround(UnitInfo unitInfo, std::optional<UnitId> pad)
    {
        auto footprintRect = sim->computeFootprintRegion(unitInfo.state->position, unitInfo.definition->movementCollisionInfo);
        auto footprintRegion = sim->occupiedGrid.tryToRegion(footprintRect);
        assert(!!footprintRegion);

        // The pad it is coming down on does not block it. **This is what kept
        // the pads from working at all in a real game.** ARMASP's yardmap is
        // `oooo oooo oooo oooo`; `o` parses to YardMapCell::Ground, and Ground
        // is impassable. So the ordinary collision test refused every
        // touchdown on a pad: the aircraft set landingFailed, climbed away and
        // came back round for ever, and because it never became a ground unit
        // nothing mended it and nothing counted it as occupying the pad --
        // which is also why a later arrival was offered the same pad and
        // turned the first one away with "no pads available". The original
        // never asks the question: a landed aircraft is *attached* to the pad
        // (0x48AAC0 links it, 0x47E570 walks the links to see which slots are
        // free), carried rather than standing on cells.
        auto blocked = pad
            ? sim->isCollisionAtIgnoringBuilding(*footprintRegion, *pad)
            : sim->isCollisionAt(*footprintRegion);
        if (blocked)
        {
            return false;
        }

        sim->occupiedGrid.forEach(*footprintRegion, [&](auto& cell) {
            cell.mobileUnitId = unitInfo.id;
        });
        sim->flyingUnitsSet.erase(unitInfo.id);
        unitInfo.state->occupancyStamp = sim->gameTime;

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
            },
            [&](const AirMovementStateDogfight&) {
                // And likewise mid-dogfight.
            });

        return false;
    }
}
