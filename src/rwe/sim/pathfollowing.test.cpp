#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/save_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * ARMPW, rev31 `units/ARMPW.fbi`: MaxVelocity=1.8, Acceleration=0.1,
         * BrakeRate=0.19, TurnRate=1120, FootprintX/Z=2, MaxSlope=17,
         * MaxWaterDepth=12. A Peewee turns 6.2 degrees a tick, so its turning
         * circle at top speed is about seventeen world units.
         */
        UnitDefinition makePeeweeDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 1.8_ssf;
            d.acceleration = 0.1_ssf;
            d.brakeRate = 0.19_ssf;
            d.turnRate = 1120_ss;
            d.maxHitPoints = 250;
            d.buildTime = 0u;
            d.sightDistance = 280u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 17u, 255u, 0u, 12u};
            return d;
        }

        /**
         * ARMFLASH, rev31 `units/ARMFLASH.fbi`: MaxVelocity=2,
         * Acceleration=0.017, BrakeRate=0.02, TurnRate=475, FootprintX/Z=2,
         * MaxSlope=10, MaxWaterDepth=12.
         *
         * The Flash is the unit issue #36 is about. Its turn rate of 475 is
         * 2.6 degrees a tick, so at its top speed of 2 it needs a turning
         * circle of about forty-four world units -- nearly three map squares,
         * and far wider than the radius at which a waypoint retires. Its
         * brake rate of 0.02 is a hundred ticks from top speed to a stop.
         */
        UnitDefinition makeFlashDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 2_ss;
            d.acceleration = 0.017_ssf;
            d.brakeRate = 0.02_ssf;
            d.turnRate = 475_ss;
            d.maxHitPoints = 250;
            d.buildTime = 0u;
            d.sightDistance = 240u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 10u, 255u, 0u, 12u};
            return d;
        }

        /**
         * Everything but the players, so that the same function can build the
         * simulation a save is loaded into -- a load insists on an empty
         * player table.
         */
        GameSimulation makeRouteSim()
        {
            GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);

            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

            sim.unitDefinitions["peewee"] = makePeeweeDef();
            sim.unitDefinitions["flash"] = makeFlashDef();

            CobScript empty;
            empty.staticVariableCount = 0;
            empty.pieces.push_back("base");
            sim.unitScriptDefinitions["peewee"] = empty;
            sim.unitScriptDefinitions["flash"] = empty;

            // Nothing may be spent on searching: every route in this file was
            // installed by the test, and a real one arriving would replace it.
            sim.pathFindingService.expansionBudgetPerTick = 0;

            return sim;
        }

        UnitId spawn(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, SimAngle rotation, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.rotation = rotation;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /**
         * Installs a route by hand in the shape the follower now expects:
         * `waypoints[0]` is the corner the unit has left and the iterator
         * starts at `waypoints[1]`, which is TotalA.exe's `wp[0]` and `wp[1]`.
         * Setting the iterator here rather than leaning on the constructor is
         * deliberate: it makes the test say what the layout is instead of
         * inheriting it.
         *
         * The goal handed in has to be the one the unit's order carries, or
         * groundUnitMoveTo decides the goal has changed and throws the route
         * away for its straight-line stand-in.
         */
        void giveRoute(GameSimulation& sim, UnitId id, const std::vector<SimVector>& points)
        {
            UnitPath path;
            path.waypoints = points;
            PathFollowingInfo info(std::move(path), sim.gameTime);
            info.currentWaypoint = info.path.waypoints.begin() + 1;

            auto& unit = sim.getUnitState(id);
            auto goal = points.back();
            unit.orders.push_back(MoveOrder(goal));
            unit.navigationState.state = NavigationStateMoving{
                MovingStateGoal(goal),
                PathDestination(goal),
                std::move(info),
                true};
        }

        /** Perpendicular distance from p to the line through a and b, in the xz plane. */
        SimScalar distanceFromSegmentLine(const SimVector& a, const SimVector& b, const SimVector& p)
        {
            auto segment = SimVector(b.x - a.x, 0_ss, b.z - a.z);
            auto toPoint = SimVector(p.x - a.x, 0_ss, p.z - a.z);
            auto length = segment.length();
            if (length == 0_ss)
            {
                return toPoint.length();
            }
            auto cross = (segment.x * toPoint.z) - (segment.z * toPoint.x);
            return rweAbs(cross) / length;
        }

        SimScalar xzDistance(const SimVector& a, const SimVector& b)
        {
            return SimVector(a.x - b.x, 0_ss, a.z - b.z).length();
        }

        /**
         * Catch2 stringifies whatever a REQUIRE compares, and Vector3x's
         * operator<< does not compile for a SimScalar, so vectors are compared
         * through a plain bool rather than inside the macro.
         */
        bool sameXz(const SimVector& a, const SimVector& b)
        {
            return a.x == b.x && a.z == b.z;
        }

        bool sameWaypoints(const std::vector<SimVector>& a, const std::vector<SimVector>& b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
        }

        SimScalar currentSpeedOf(const GameSimulation& sim, UnitId id)
        {
            const auto* ground = std::get_if<UnitPhysicsInfoGround>(&sim.getUnitState(id).physics);
            return ground == nullptr ? 0_ss : ground->currentSpeed;
        }

        std::size_t currentWaypointIndex(const GameSimulation& sim, UnitId id)
        {
            const auto* moving = std::get_if<NavigationStateMoving>(&sim.getUnitState(id).navigationState.state);
            if (moving == nullptr || !moving->path)
            {
                return 0;
            }
            return static_cast<std::size_t>(moving->path->currentWaypoint - moving->path->path.waypoints.begin());
        }
    }

    // TotalA.exe 0x43CD20, written up as TOTALA-EXE.md section 102, and
    // upstream issue #36. The original steers at a point on the segment it is
    // walking rather than at the corner, and brakes on any tick where the turn
    // it still has to make will not fit in the distance it has left.
    TEST_CASE("a unit taking a corner slows for it and never doubles back", "[pathfollowing]")
    {
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeRouteSim();
        auto player = addPlayer(sim);

        // A right angle with a short second leg: four hundred units of run-up,
        // then a two-square hop north. A Flash's turning circle is wider than
        // that leg, so this is exactly the shape that used to send it sailing
        // past the last waypoint and back to it. Measured on the follower this
        // replaces, the worst step along the second segment was -0.77 world
        // units in a tick -- the unit walking backwards down its own route.
        auto start = SimVector(-400_ss, 0_ss, 0_ss);
        auto corner = SimVector(0_ss, 0_ss, 0_ss);
        auto finish = SimVector(0_ss, 0_ss, 32_ss);

        auto id = spawn(sim, "flash", player, start, UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss)), script);
        giveRoute(sim, id, {start, corner, finish});

        SimScalar speedLongBefore = 0_ss;
        std::optional<SimScalar> speedAtCorner;
        std::optional<int> cornerTick;
        SimScalar worstBackwards = 0_ss;
        int worstBackwardsTick = -1;
        std::vector<SimScalar> speeds;

        for (int t = 0; t < 900; ++t)
        {
            auto before = sim.getUnitState(id).position;
            auto index = currentWaypointIndex(sim, id);
            sim.tick();
            auto after = sim.getUnitState(id).position;
            speeds.push_back(currentSpeedOf(sim, id));

            const auto* moving = std::get_if<NavigationStateMoving>(&sim.getUnitState(id).navigationState.state);
            if (moving == nullptr || !moving->path || index == 0)
            {
                continue;
            }

            // The segment the unit was walking when it took this step.
            const auto& segmentFrom = moving->path->path.waypoints[index - 1];
            const auto& segmentTo = moving->path->path.waypoints[index];
            auto direction = SimVector(segmentTo.x - segmentFrom.x, 0_ss, segmentTo.z - segmentFrom.z).normalized();
            auto step = SimVector(after.x - before.x, 0_ss, after.z - before.z);
            auto along = step.dot(direction);
            if (along < worstBackwards)
            {
                worstBackwards = along;
                worstBackwardsTick = t;
            }

            if (!cornerTick && xzDistance(after, corner) <= 5_ss)
            {
                cornerTick = t;
                speedAtCorner = currentSpeedOf(sim, id);
                speedLongBefore = speeds.at(static_cast<std::size_t>(t) - 100u);
            }
        }

        // The corner brake, as something you can watch: the unit is going
        // slower on the tick it comes within five world units of the corner
        // than it was a hundred ticks earlier out on the straight. Five is
        // the original's advance radius; RWE retires a waypoint further out
        // than that, so this is a measuring point rather than an event.
        REQUIRE(cornerTick.has_value());
        REQUIRE(speedAtCorner.has_value());
        INFO("reached the corner on tick " << *cornerTick << " at " << speedAtCorner->value << ", a hundred ticks earlier " << speedLongBefore.value);
        REQUIRE(*speedAtCorner < speedLongBefore);

        // The assertion #36 is about. A unit that has overshot a waypoint
        // turns round and walks back to it, which shows up here as a step with
        // a negative component along the segment it is supposed to be walking.
        INFO("worst backwards step " << worstBackwards.value << " on tick " << worstBackwardsTick);
        REQUIRE(worstBackwards >= 0_ss);

        // And it still finishes the job.
        REQUIRE(sim.getUnitState(id).orders.empty());
    }

    TEST_CASE("a unit off its line is steered back onto it rather than at the corner", "[pathfollowing]")
    {
        // The corridor. The aim point sits on the segment prev -> next, at the
        // distance from next that the unit itself is, less the eighty-unit
        // look-ahead, so a unit that has drifted sideways is steered back onto
        // the line. Aiming at the corner instead -- all RWE could do without
        // the corner behind it in the list -- walks the chord, and a unit that
        // starts twenty units off the line is still twelve units off it a
        // quarter of the way along. Twelve is what this measured before.
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeRouteSim();
        auto player = addPlayer(sim);

        auto segmentStart = SimVector(-400_ss, 0_ss, 0_ss);
        auto corner = SimVector(0_ss, 0_ss, 0_ss);
        auto finish = SimVector(0_ss, 0_ss, 400_ss);

        // Twenty units north of the line it is meant to be walking, pointing
        // along it.
        auto id = spawn(sim, "peewee", player, SimVector(-400_ss, 0_ss, 20_ss), UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss)), script);
        giveRoute(sim, id, {segmentStart, corner, finish});

        SimScalar worstDeviation = 0_ss;
        bool measuredAnything = false;
        for (int t = 0; t < 900; ++t)
        {
            sim.tick();
            const auto& position = sim.getUnitState(id).position;
            auto toCorner = xzDistance(position, corner);

            // The middle of the first segment: far enough in that the unit has
            // had time to converge, and still outside the look-ahead, where
            // the aim point stops being projected and becomes the corner.
            if (toCorner > 250_ss || toCorner <= 80_ss || currentWaypointIndex(sim, id) != 1)
            {
                continue;
            }
            measuredAnything = true;
            worstDeviation = rweMax(worstDeviation, distanceFromSegmentLine(segmentStart, corner, position));
        }

        REQUIRE(measuredAnything);
        INFO("worst deviation from the segment " << worstDeviation.value);
        REQUIRE(worstDeviation < 4_ss);
    }

    TEST_CASE("a unit ordered to move steps off on the tick it is ordered", "[pathfollowing]")
    {
        // The straight-line stand-in of section 87 is two points now, the
        // unit's own position and the goal, which is what 0x44F3F2 writes. On
        // a straight line the aim point eighty units ahead and the goal itself
        // give the same heading, and that is the point of it: the segment
        // costs nothing here and is there for the tick the unit stops being on
        // the line.
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeRouteSim();
        auto player = addPlayer(sim);

        auto start = SimVector(-400_ss, 0_ss, 0_ss);
        auto destination = SimVector(400_ss, 0_ss, 0_ss);
        auto id = spawn(sim, "peewee", player, start, UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss)), script);
        sim.getUnitState(id).orders.push_back(MoveOrder(destination));

        sim.tick();

        const auto& unit = sim.getUnitState(id);
        REQUIRE(unit.position.x > start.x);

        const auto* moving = std::get_if<NavigationStateMoving>(&unit.navigationState.state);
        REQUIRE(moving != nullptr);
        REQUIRE(moving->path.has_value());
        REQUIRE(moving->path->path.waypoints.size() == 2);
        REQUIRE(sameXz(moving->path->path.waypoints.front(), start));
        REQUIRE(sameXz(moving->path->path.waypoints.back(), destination));
        REQUIRE(currentWaypointIndex(sim, id) == 1);
        REQUIRE(unit.rotation.value == UnitState::toRotation(destination - start).value);
    }

    TEST_CASE("a unit halfway along a route keeps the corner behind it across a save", "[pathfollowing][saveload]")
    {
        auto script = makeEmptyCobScript({"base"});

        auto simA = makeRouteSim();
        auto player = addPlayer(simA);

        auto start = SimVector(-400_ss, 0_ss, 0_ss);
        auto corner = SimVector(0_ss, 0_ss, 0_ss);
        auto finish = SimVector(0_ss, 0_ss, 400_ss);
        auto id = spawn(simA, "peewee", player, start, UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss)), script);
        giveRoute(simA, id, {start, corner, finish});

        tick(simA, 100);

        const auto* movingA = std::get_if<NavigationStateMoving>(&simA.getUnitState(id).navigationState.state);
        REQUIRE(movingA != nullptr);
        REQUIRE(movingA->path.has_value());
        REQUIRE(movingA->path->path.waypoints.size() == 3);
        REQUIRE(currentWaypointIndex(simA, id) == 1);

        auto saved = saveSimulationToJson(simA);

        auto simB = makeRouteSim();
        loadSimulationFromJson(saved, simB);

        const auto* movingB = std::get_if<NavigationStateMoving>(&simB.getUnitState(id).navigationState.state);
        REQUIRE(movingB != nullptr);
        REQUIRE(movingB->path.has_value());

        // The corner the unit came from is an entry in the list rather than a
        // field beside it, so the serialisation that was already there carries
        // it without being told to.
        REQUIRE(sameWaypoints(movingB->path->path.waypoints, movingA->path->path.waypoints));
        REQUIRE(currentWaypointIndex(simB, id) == currentWaypointIndex(simA, id));
        REQUIRE(computeHashOf(simA) == computeHashOf(simB));
        REQUIRE(saveSimulationToJson(simB) == saved);

        for (int i = 0; i < 300; ++i)
        {
            simA.tick();
            simB.tick();
            INFO("tick " << i << " after the save");
            REQUIRE(computeHashOf(simA) == computeHashOf(simB));
        }
    }
}
