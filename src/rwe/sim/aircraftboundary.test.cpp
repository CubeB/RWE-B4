#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/AirMovement.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/LosTables.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/combat_test_util.h>
#include <rwe/sim/sim_test_util.h>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * A 64 by 64 map is 1024 world units each way, so the full terrain
         * occupies [-512, 512] and the rendered rectangle [-496, 480] by
         * [-448, 416] -- BOUNDARY-001's full width less 32 and full height
         * less 128. An aircraft at x 600 is clearly outside the full grid, and
         * one at x 486 is between the rendered right edge and the full one.
         */
        constexpr int BoundaryMapTiles = 64;

        /** A fighter's numbers: fast enough to reach an edge in a test. */
        UnitDefinition makeBoundaryAircraftDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.canFly = true;
            d.cruiseAltitude = 150_ss;
            d.maxVelocity = 8_ss;
            d.acceleration = 0.5_ssf;
            d.brakeRate = 0.5_ssf;
            d.turnRate = 1000_ss;
            d.attackRunLength = 120_ss;
            d.maneuverLeashLength = 1280_ss;
            d.sightDistance = 500;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitDefinition makeBoundaryGunshipDef()
        {
            auto d = makeBoundaryAircraftDef();
            d.hoverAttack = true;
            return d;
        }

        /** A target that stands where it is put and does not shoot back hard enough to matter. */
        UnitDefinition makeBoundaryTargetDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.shootMe = true;
            d.sightDistance = 0;
            d.maxHitPoints = 100000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** A repair pad: a builder that is also an air base. */
        UnitDefinition makeBoundaryPadDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.builder = true;
            d.isAirBase = true;
            d.buildDistance = 40_ss;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            return d;
        }

        void registerBoundaryModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** A straight-flying gun, not dropped, so a gun-armed aircraft strafes. */
        void defineBoundaryGun(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 300_ss;
            w.reloadTime = SimScalar(0.5f);
            w.burst = 1;
            w.velocity = 300_ss / 30_ss;
            w.damageRadius = 8_ss;
            w.damage["DEFAULT"] = 1;
            sim.weaponDefinitions["gun"] = w;
        }

        /** A dropped weapon, so the same aircraft flies the bomber's run instead. */
        void defineBoundaryBomb(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeBomb();
            w.maxRange = 500_ss;
            w.reloadTime = SimScalar(0.5f);
            w.burst = 1;
            w.velocity = 0_ss;
            w.damageRadius = 16_ss;
            w.damage["DEFAULT"] = 1;
            sim.weaponDefinitions["bomb"] = w;
        }

        /** A boundary test world with both players present and the fog out of the way. */
        struct BoundaryWorld
        {
            GameSimulation sim;
            PlayerId us;
            PlayerId them;
            std::shared_ptr<CobScript> script{makeEmptyCobScript({"base"})};

            explicit BoundaryWorld(bool gunship = false)
                : sim(makeFlatTerrain(BoundaryMapTiles, BoundaryMapTiles), 0u, 0, 0)
            {
                us = addPlayer(sim, "us");
                them = addPlayer(sim, "them");
                sim.unitDefinitions["aircraft"] = gunship ? makeBoundaryGunshipDef() : makeBoundaryAircraftDef();
                sim.unitDefinitions["target"] = makeBoundaryTargetDef();
                sim.unitDefinitions["pad"] = makeBoundaryPadDef();
                registerBoundaryModel(sim);
                defineBoundaryGun(sim);
                defineBoundaryBomb(sim);
                sim.losTables = generateLosTables(8);
                revealCombatArena(sim);
            }

            UnitId spawnAircraft(const std::string& type, PlayerId owner, const SimVector& position)
            {
                auto id = addUnitOfType(sim, type, owner, position, script);
                auto& unit = sim.getUnitState(id);
                unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
                unit.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
                unit.fireOrders = UnitFireOrders::FireAtWill;
                sim.flyingUnitsSet.insert(id);
                return id;
            }

            UnitId spawnTarget(const SimVector& position)
            {
                auto id = addUnitOfType(sim, "target", them, position, script);
                sim.getUnitState(id).hitPoints = sim.unitDefinitions.at("target").maxHitPoints;
                return id;
            }

            void giveWeapon(UnitId id, const std::string& weaponType)
            {
                UnitWeapon weapon;
                weapon.weaponType = weaponType;
                sim.getUnitState(id).weapons[0] = weapon;
            }
        };

        const AirMovementStateFlying* flyingState(const UnitState& unit)
        {
            if (auto air = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
            {
                return std::get_if<AirMovementStateFlying>(&air->movementState);
            }
            return nullptr;
        }

        const AirMovementStateDogfight* dogfightState(const UnitState& unit)
        {
            if (auto air = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
            {
                return std::get_if<AirMovementStateDogfight>(&air->movementState);
            }
            return nullptr;
        }

        const AirMovementStateHoverAttack* hoverState(const UnitState& unit)
        {
            if (auto air = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
            {
                return std::get_if<AirMovementStateHoverAttack>(&air->movementState);
            }
            return nullptr;
        }

        const AirMovementStateAttackRun* attackRunState(const UnitState& unit)
        {
            if (auto air = std::get_if<UnitPhysicsInfoAir>(&unit.physics))
            {
                return std::get_if<AirMovementStateAttackRun>(&air->movementState);
            }
            return nullptr;
        }

        SimScalar flatDistanceSquared(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return (dx * dx) + (dz * dz);
        }

        /** The centre of BOUNDARY-001's rendered rectangle on the 64 by 64 test map. */
        SimVector renderedCentre()
        {
            return SimVector(-16_ss, 0_ss, -64_ss);
        }

        /** Whether a direction points toward the rendered centre from `position`. */
        bool pointsAtRenderedCentre(const SimVector& position, const SimVector& target)
        {
            SimVector toCentre(renderedCentre().x - position.x, 0_ss, renderedCentre().z - position.z);
            SimVector toTarget(target.x - position.x, 0_ss, target.z - position.z);
            return (toCentre.x * toTarget.x) + (toCentre.z * toTarget.z) > 0_ss;
        }
    }

    TEST_CASE("BOUNDARY-004: the outside-grid predicate is the footprint against the full terrain", "[aircraftboundary][boundary004]")
    {
        // The original's sentinel-bucket test, at 0x47CC30: the footprint's
        // origin cell below zero, or origin plus size at or beyond the full
        // terrain tile count. It is not the rendered rectangle, not a reserved
        // border and not the model's extent.
        auto terrain = makeFlatTerrain(BoundaryMapTiles, BoundaryMapTiles);

        SECTION("an origin before the first cell is outside")
        {
            REQUIRE(footprintIsOutsideTerrain(terrain, DiscreteRect(-1, 10, 2, 2)));
            REQUIRE(footprintIsOutsideTerrain(terrain, DiscreteRect(10, -1, 2, 2)));
            REQUIRE_FALSE(footprintIsOutsideTerrain(terrain, DiscreteRect(0, 10, 2, 2)));
            REQUIRE_FALSE(footprintIsOutsideTerrain(terrain, DiscreteRect(10, 0, 2, 2)));
        }

        SECTION("origin plus size at the count is outside, at the count less one is not")
        {
            REQUIRE(footprintIsOutsideTerrain(terrain, DiscreteRect(62, 10, 2, 2)));
            REQUIRE_FALSE(footprintIsOutsideTerrain(terrain, DiscreteRect(61, 10, 2, 2)));
            REQUIRE(footprintIsOutsideTerrain(terrain, DiscreteRect(10, 62, 2, 2)));
            REQUIRE_FALSE(footprintIsOutsideTerrain(terrain, DiscreteRect(10, 61, 2, 2)));

            // The `>=` consequence: a one-cell footprint can never occupy the
            // last column or row at all.
            REQUIRE(footprintIsOutsideTerrain(terrain, DiscreteRect(63, 10, 1, 1)));
            REQUIRE(footprintIsOutsideTerrain(terrain, DiscreteRect(10, 63, 1, 1)));
            REQUIRE_FALSE(footprintIsOutsideTerrain(terrain, DiscreteRect(62, 10, 1, 1)));
        }

        SECTION("the last rendered strip is inside the grid and is not the trigger")
        {
            // The rendered rectangle is 32 units narrower and 128 shorter than
            // the full terrain (BOUNDARY-001). An aircraft between the two
            // edges is in an ordinary bucket and must not be recovered.
            REQUIRE(terrain.rightCutoffInWorldUnits() == 480_ss);
            REQUIRE(terrain.bottomCutoffInWorldUnits() == 384_ss);

            BoundaryWorld world;
            auto inTheStrip = world.sim.computeFootprintRegion(SimVector(486_ss, 0_ss, 0_ss), world.sim.unitDefinitions.at("aircraft").movementCollisionInfo);
            REQUIRE(inTheStrip.x + inTheStrip.width < BoundaryMapTiles);
            REQUIRE_FALSE(footprintIsOutsideTerrain(world.sim.terrain, inTheStrip));

            auto pastTheFullEdge = world.sim.computeFootprintRegion(SimVector(500_ss, 0_ss, 0_ss), world.sim.unitDefinitions.at("aircraft").movementCollisionInfo);
            REQUIRE(footprintIsOutsideTerrain(world.sim.terrain, pastTheFullEdge));
        }

        SECTION("a unit's predicate uses its own footprint")
        {
            BoundaryWorld world;
            auto id = world.spawnAircraft("aircraft", world.us, SimVector(500_ss, 150_ss, 0_ss));
            REQUIRE(unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft")));

            world.sim.getUnitState(id).position = SimVector(0_ss, 150_ss, 0_ss);
            REQUIRE_FALSE(unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft")));
        }
    }

    TEST_CASE("BOUNDARY-004: the recovery goal is 800 units toward the rendered map centre", "[aircraftboundary][boundary004]")
    {
        auto terrain = makeFlatTerrain(BoundaryMapTiles, BoundaryMapTiles);

        SECTION("the centre is the full-terrain centre less 16 and 64")
        {
            // The finding's worked figure for an even-sized map: the rendered
            // centre sits at (-16, -64) in RWE's centred world coordinates.
            REQUIRE(aircraftBoundaryRecoveryGoal(terrain, SimVector(600_ss, 150_ss, -700_ss)).y == 150_ss);

            SimVector from(600_ss, 150_ss, -700_ss);
            auto goal = aircraftBoundaryRecoveryGoal(terrain, from);
            auto centre = renderedCentre();

            // The step is 800 long (allow the normalisation's rounding).
            auto distanceSquared = flatDistanceSquared(goal, from);
            REQUIRE(distanceSquared > (799_ss * 799_ss));
            REQUIRE(distanceSquared < (801_ss * 801_ss));

            // And on the bearing to the centre: the cross product is zero and
            // the dot product positive.
            SimVector toCentre(centre.x - from.x, 0_ss, centre.z - from.z);
            SimVector toGoal(goal.x - from.x, 0_ss, goal.z - from.z);
            auto cross = (toCentre.x * toGoal.z) - (toCentre.z * toGoal.x);
            auto dot = (toCentre.x * toGoal.x) + (toCentre.z * toGoal.z);
            REQUIRE(cross == 0_ss);
            REQUIRE(dot > 0_ss);
        }

        SECTION("the height is carried over, never steered")
        {
            auto goal = aircraftBoundaryRecoveryGoal(terrain, SimVector(-700_ss, 321_ss, 700_ss));
            REQUIRE(goal.y == 321_ss);
        }
    }

    TEST_CASE("BOUNDARY-004: a move order past the edge is neither clamped nor recovered", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(400_ss, 150_ss, 0_ss));
        auto destination = SimVector(700_ss, 0_ss, 0_ss);
        world.sim.getUnitState(id).orders.push_back(createMoveOrder(destination));

        world.sim.tick();

        const auto* flying = flyingState(world.sim.getUnitState(id));
        REQUIRE(flying != nullptr);
        REQUIRE(flying->targetPosition.has_value());
        // Pure cell quantisation in the original's move handler: no clamp.
        REQUIRE(flying->targetPosition->x == 700_ss);
        REQUIRE(flying->targetPosition->z == 0_ss);
        REQUIRE_FALSE(world.sim.getUnitState(id).airLoiter.has_value());

        // It keeps flying out and nothing turns it back.
        for (int tick = 0; tick < 30; ++tick)
        {
            world.sim.tick();
            flying = flyingState(world.sim.getUnitState(id));
            REQUIRE(flying != nullptr);
            REQUIRE(flying->targetPosition->x == 700_ss);
            REQUIRE_FALSE(world.sim.getUnitState(id).airLoiter.has_value());
        }
        REQUIRE(world.sim.getUnitState(id).position.x > 512_ss);
    }

    TEST_CASE("BOUNDARY-004: a patrol point near the edge takes the aircraft outside", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(300_ss, 150_ss, 0_ss));
        world.sim.getUnitState(id).orders.push_back(PatrolOrder(SimVector(470_ss, 0_ss, 0_ss)));

        world.sim.tick();

        const auto& unit = world.sim.getUnitState(id);
        REQUIRE(unit.airLoiter.has_value());
        REQUIRE(unit.airLoiter->reason == UnitState::AirLoiterState::Reason::Patrol);
        // 320 past the waypoint, from the aircraft: an objective outside the
        // rendered edge and outside the full grid. Nothing clamps it.
        REQUIRE(unit.airLoiter->waypoint.x > 700_ss);

        bool everOutside = false;
        auto furthestX = world.sim.getUnitState(id).position.x;
        for (int tick = 0; tick < 240; ++tick)
        {
            world.sim.tick();
            const auto& p = world.sim.getUnitState(id);
            everOutside = everOutside || unitFootprintIsOutsideTerrain(world.sim, p, world.sim.unitDefinitions.at("aircraft"));
            furthestX = rweMax(furthestX, p.position.x);
            // The patrol circuit keeps steering outward; a recovery branch
            // would replace it with a point toward the centre, and would
            // swap the loiter to LandingHold.
            if (p.airLoiter)
            {
                REQUIRE(p.airLoiter->reason == UnitState::AirLoiterState::Reason::Patrol);
            }
        }

        REQUIRE(everOutside);
        // Past the rendered right edge of this map (480), which is where the
        // patrol point at 470 plus its 320 overshoot sends it.
        REQUIRE(furthestX > 480_ss);
    }

    TEST_CASE("BOUNDARY-004: an idle aircraft outside the grid flies back to the centre and lands", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(600_ss, 150_ss, 0_ss));

        // Landing is the one path that cannot complete outside: the placement
        // test behind the landing search rejects any footprint outside the
        // full grid, so the spot it finds is always a map cell even for an
        // aircraft that has to be recovered first.
        auto spot = findLandingLocation(world.sim, world.sim.getUnitInfo(id));
        REQUIRE(spot.has_value());
        REQUIRE_FALSE(footprintIsOutsideTerrain(
            world.sim.terrain,
            world.sim.computeFootprintRegion(*spot, world.sim.unitDefinitions.at("aircraft").movementCollisionInfo)));

        auto before = world.sim.getUnitState(id).position;
        world.sim.tick();

        const auto* flying = flyingState(world.sim.getUnitState(id));
        REQUIRE(flying != nullptr);
        REQUIRE(flying->targetPosition.has_value());
        REQUIRE((*flying->targetPosition == aircraftBoundaryRecoveryGoal(world.sim.terrain, before)));
        REQUIRE_FALSE(world.sim.getUnitState(id).airLoiter.has_value());
        REQUIRE(pointsAtRenderedCentre(before, *flying->targetPosition));

        // Every pass re-issues the same objective from where it now is, until
        // the footprint is back inside.
        for (int tick = 0; tick < 60 && unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft")); ++tick)
        {
            before = world.sim.getUnitState(id).position;
            const auto wasOutside = unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft"));
            world.sim.tick();
            if (wasOutside)
            {
                flying = flyingState(world.sim.getUnitState(id));
                REQUIRE(flying != nullptr);
                REQUIRE(flying->targetPosition.has_value());
                REQUIRE((*flying->targetPosition == aircraftBoundaryRecoveryGoal(world.sim.terrain, before)));
            }
        }
        REQUIRE_FALSE(unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft")));

        // Back inside, the ordinary landing search takes over and it sets down.
        bool landed = false;
        for (int tick = 0; tick < 1500 && !landed; ++tick)
        {
            world.sim.tick();
            landed = std::holds_alternative<UnitPhysicsInfoGround>(world.sim.getUnitState(id).physics);
        }
        REQUIRE(landed);
    }

    TEST_CASE("BOUNDARY-004: Stop outside the grid queues the landing search, which recovers", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(600_ss, 150_ss, 0_ss));
        world.sim.getUnitState(id).orders.push_back(PatrolOrder(SimVector(470_ss, 0_ss, 0_ss)));

        for (int tick = 0; tick < 10; ++tick)
        {
            world.sim.tick();
        }
        REQUIRE(unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft")));

        REQUIRE(applyUnitCommandToSimulation(world.sim, world.us, PlayerUnitCommand(id, PlayerUnitCommand::Stop())));
        REQUIRE(world.sim.getUnitState(id).orders.empty());

        auto before = world.sim.getUnitState(id).position;
        world.sim.tick();
        const auto* flying = flyingState(world.sim.getUnitState(id));
        REQUIRE(flying != nullptr);
        REQUIRE(flying->targetPosition.has_value());
        REQUIRE((*flying->targetPosition == aircraftBoundaryRecoveryGoal(world.sim.terrain, before)));

        bool backInside = false;
        for (int tick = 0; tick < 120 && !backInside; ++tick)
        {
            world.sim.tick();
            backInside = !unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft"));
        }
        REQUIRE(backInside);
    }

    TEST_CASE("BOUNDARY-004: a guard order outside the grid recovers and then resumes its circuit", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto guarded = world.spawnTarget(SimVector(0_ss, 0_ss, 0_ss));
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(600_ss, 150_ss, 0_ss));
        world.sim.getUnitState(id).orders.push_back(GuardOrder(guarded));

        auto before = world.sim.getUnitState(id).position;
        world.sim.tick();

        const auto* flying = flyingState(world.sim.getUnitState(id));
        REQUIRE(flying != nullptr);
        REQUIRE(flying->targetPosition.has_value());
        REQUIRE((*flying->targetPosition == aircraftBoundaryRecoveryGoal(world.sim.terrain, before)));
        REQUIRE(pointsAtRenderedCentre(before, *flying->targetPosition));
        // The guard's ring is not armed while it is recovering.
        REQUIRE_FALSE(world.sim.getUnitState(id).airLoiter.has_value());

        bool backInside = false;
        for (int tick = 0; tick < 120 && !backInside; ++tick)
        {
            world.sim.tick();
            backInside = !unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft"));
        }
        REQUIRE(backInside);

        // Back inside, VTOL_Follow's ring resumes around the guarded unit.
        bool guarding = false;
        for (int tick = 0; tick < 120 && !guarding; ++tick)
        {
            world.sim.tick();
            const auto& p = world.sim.getUnitState(id);
            guarding = p.airLoiter && p.airLoiter->reason == UnitState::AirLoiterState::Reason::Guarding;
        }
        REQUIRE(guarding);
    }

    TEST_CASE("BOUNDARY-004: a post-attack search outside the grid heads for the centre", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto target = world.spawnTarget(SimVector(0_ss, 0_ss, 0_ss));
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(600_ss, 150_ss, 0_ss));
        world.giveWeapon(id, "gun");
        world.sim.getUnitState(id).orders.push_back(createAttackOrder(target));

        // Let the attack begin, then kill the target so the search is armed.
        for (int tick = 0; tick < 5; ++tick)
        {
            world.sim.tick();
        }
        world.sim.getUnitState(target).markAsDeadNoCorpse();
        world.sim.tick();
        world.sim.tick();

        const auto& unit = world.sim.getUnitState(id);
        REQUIRE(unit.airLoiter.has_value());
        REQUIRE(unit.airLoiter->reason == UnitState::AirLoiterState::Reason::AttackEnded);

        auto before = unit.position;
        world.sim.tick();
        const auto* flying = flyingState(world.sim.getUnitState(id));
        REQUIRE(flying != nullptr);
        REQUIRE(flying->targetPosition.has_value());
        REQUIRE((*flying->targetPosition == aircraftBoundaryRecoveryGoal(world.sim.terrain, before)));
        REQUIRE(world.sim.getUnitState(id).airLoiter->reason == UnitState::AirLoiterState::Reason::AttackEnded);
    }

    TEST_CASE("BOUNDARY-004: an air-to-air dogfight outside the grid steps toward the centre", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto bandit = world.spawnAircraft("aircraft", world.them, SimVector(0_ss, 150_ss, 0_ss));
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(600_ss, 150_ss, 0_ss));
        world.giveWeapon(id, "gun");
        world.giveWeapon(bandit, "gun");
        world.sim.getUnitState(id).orders.push_back(createAttackOrder(bandit));

        world.sim.tick();
        world.sim.tick();

        const auto* dogfight = dogfightState(world.sim.getUnitState(id));
        REQUIRE(dogfight != nullptr);
        // The bandit is retained -- only the movement objective moved.
        REQUIRE(std::get<UnitId>(dogfight->target) == bandit);
        REQUIRE((dogfight->goalVelocity == SimVector(0_ss, 0_ss, 0_ss)));
        REQUIRE(pointsAtRenderedCentre(world.sim.getUnitState(id).position, dogfight->goalPosition));

        bool backInside = false;
        for (int tick = 0; tick < 240 && !backInside; ++tick)
        {
            world.sim.tick();
            backInside = !unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft"));
        }
        REQUIRE(backInside);
        REQUIRE(dogfightState(world.sim.getUnitState(id)) != nullptr);
    }

    TEST_CASE("BOUNDARY-004: a gunship outside the grid puts its station on the way back in", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world(true);
        auto target = world.spawnTarget(SimVector(0_ss, 0_ss, 0_ss));
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(600_ss, 150_ss, 0_ss));
        world.giveWeapon(id, "gun");
        world.sim.getUnitState(id).orders.push_back(createAttackOrder(target));

        auto before = world.sim.getUnitState(id).position;
        world.sim.tick();

        const auto* hover = hoverState(world.sim.getUnitState(id));
        REQUIRE(hover != nullptr);
        REQUIRE(std::get<UnitId>(hover->target) == target);
        REQUIRE((hover->station == aircraftBoundaryRecoveryGoal(world.sim.terrain, before)));
        REQUIRE(pointsAtRenderedCentre(before, hover->station));

        bool backInside = false;
        for (int tick = 0; tick < 240 && !backInside; ++tick)
        {
            world.sim.tick();
            backInside = !unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft"));
        }
        REQUIRE(backInside);
        REQUIRE(hoverState(world.sim.getUnitState(id)) != nullptr);
    }

    TEST_CASE("BOUNDARY-004: a strafing run outside the grid re-aims on a 30-tick deadline", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto target = world.spawnTarget(SimVector(480_ss, 0_ss, 0_ss));
        // Far enough out that the approach, not the recovery, is what the
        // thirty ticks are measuring: the aircraft is still outside the grid
        // when the deadline expires.
        auto id = world.spawnAircraft("aircraft", world.us, SimVector(900_ss, 150_ss, 0_ss));
        world.giveWeapon(id, "gun");
        world.sim.getUnitState(id).orders.push_back(createAttackOrder(target));

        world.sim.tick();

        const auto* run = attackRunState(world.sim.getUnitState(id));
        REQUIRE(run != nullptr);
        REQUIRE(run->strafingPass);
        REQUIRE(std::get<UnitId>(run->target) == target);
        REQUIRE(run->overflowRepathAt.has_value());
        auto firstDeadline = *run->overflowRepathAt;
        REQUIRE(firstDeadline == world.sim.gameTime + GameTime(30));

        // It keeps attacking from outside: no centre-seeking objective, no
        // landing hold, and the run and its target stay.
        REQUIRE_FALSE(world.sim.getUnitState(id).airLoiter.has_value());
        REQUIRE(unitFootprintIsOutsideTerrain(world.sim, world.sim.getUnitState(id), world.sim.unitDefinitions.at("aircraft")));

        // The deadline is re-armed only when it expires, not every tick.
        for (int tick = 0; tick < 29; ++tick)
        {
            world.sim.tick();
            const auto* p = attackRunState(world.sim.getUnitState(id));
            REQUIRE(p != nullptr);
            REQUIRE(p->overflowRepathAt.has_value());
            REQUIRE(*p->overflowRepathAt == firstDeadline);
        }

        world.sim.tick();
        const auto* rearmed = attackRunState(world.sim.getUnitState(id));
        REQUIRE(rearmed != nullptr);
        REQUIRE(rearmed->overflowRepathAt.has_value());
        REQUIRE(*rearmed->overflowRepathAt == firstDeadline + GameTime(30));
        REQUIRE(std::get<UnitId>(rearmed->target) == target);
    }

    TEST_CASE("BOUNDARY-004: a bomber has no overflow branch and is not turned back at the edge", "[aircraftboundary][boundary004]")
    {
        SECTION("an off-grid bomber's run arms no deadline")
        {
            BoundaryWorld world;
            auto target = world.spawnTarget(SimVector(480_ss, 0_ss, 0_ss));
            auto id = world.spawnAircraft("aircraft", world.us, SimVector(600_ss, 150_ss, 0_ss));
            world.giveWeapon(id, "bomb");
            world.sim.getUnitState(id).orders.push_back(createAttackOrder(target));

            world.sim.tick();

            const auto* run = attackRunState(world.sim.getUnitState(id));
            REQUIRE(run != nullptr);
            REQUIRE_FALSE(run->strafingPass);
            REQUIRE_FALSE(run->overflowRepathAt.has_value());
        }

        SECTION("a run-out already outside the map stays in its departing phase")
        {
            // The edge turn-back was RWE's own rule; AirStrike has no boundary
            // test in any state. The aircraft is well outside a 64 by 64 map
            // and still inside its run-out distance.
            UnitDefinition def{};
            def.maxVelocity = 8_ss;
            def.turnRate = 1000_ss;
            def.cruiseAltitude = 150_ss;

            UnitState unit({}, std::unique_ptr<CobEnvironment>{});
            unit.position = SimVector(600_ss, 150_ss, 0_ss);

            AirMovementStateAttackRun run(SimVector(0_ss, 0_ss, 0_ss));
            run.lastKnownTargetPos = SimVector(0_ss, 150_ss, 0_ss);
            run.runOutDirection = SimVector(1_ss, 0_ss, 0_ss);
            run.runOutDistance = 2000_ss;
            run.phase = AirMovementStateAttackRun::Phase::Departing;

            auto step = AirMovement::stepAttackRun(
                run,
                unit,
                def,
                SimVector(0_ss, 150_ss, 0_ss),
                SimVector(0_ss, 0_ss, 0_ss),
                300_ss);
            REQUIRE_FALSE(step.weaponsHot);
            REQUIRE(run.phase == AirMovementStateAttackRun::Phase::Departing);
        }
    }

    TEST_CASE("BOUNDARY-004: landing on a pad is an ordinary return, not a centre recovery", "[aircraftboundary][boundary004]")
    {
        BoundaryWorld world;
        auto pad = addUnitOfType(world.sim, "pad", world.us, SimVector(0_ss, 0_ss, 0_ss), world.script);
        world.sim.getUnitState(pad).hitPoints = world.sim.unitDefinitions.at("pad").maxHitPoints;
        world.sim.getUnitState(pad).activated = true;

        auto id = world.spawnAircraft("aircraft", world.us, SimVector(600_ss, 150_ss, 0_ss));
        world.giveWeapon(id, "gun");
        world.sim.getUnitState(id).orders.push_back(LandOnAirBaseOrder(pad));

        world.sim.tick();

        const auto* flying = flyingState(world.sim.getUnitState(id));
        REQUIRE(flying != nullptr);
        REQUIRE(flying->targetPosition.has_value());
        // VTOL_Landing flies to the pad, which is a map cell; it must not be
        // sent the 800-unit centre objective instead.
        REQUIRE(flying->targetPosition->x == 0_ss);
        REQUIRE(flying->targetPosition->z == 0_ss);
    }
}
