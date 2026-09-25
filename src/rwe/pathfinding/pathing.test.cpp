#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/pathfinding/UnitPathFinder.h>
#include <rwe/pathfinding/pathfinding_utils.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/movement.h>
#include <sstream>
#include <memory>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeTerrain(int width, int height, const std::function<unsigned char(int, int)>& heightAt)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    heights.set(x, y, heightAt(x, y));
                }
            }
            return MapTerrain(std::move(heights), 0_ss);
        }

        UnitDefinition makeTankDef(unsigned int maxSlope)
        {
            UnitDefinition d{};
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 4_ss;
            d.acceleration = 4_ss;
            d.brakeRate = 4_ss;
            d.turnRate = 4000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            // {footprintX, footprintZ, maxSlope, maxWaterSlope, minWaterDepth, maxWaterDepth}
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, maxSlope, maxSlope, 0u, 255u};
            return d;
        }

        /** World-space centre of a 1x1 footprint at heightmap cell (x, y). */
        SimVector cellCenter(const GameSimulation& sim, int x, int y)
        {
            auto corner = sim.terrain.heightmapIndexToWorldCorner(x, y);
            return corner + SimVector(8_ss, 0_ss, 8_ss);
        }

        UnitId addTank(GameSimulation& sim, PlayerId owner, int cellX, int cellY, const std::shared_ptr<CobScript>& script, unsigned int maxSlope = 10u)
        {
            sim.unitDefinitions["tank"] = makeTankDef(maxSlope);
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::move(env));
            unit.unitType = "tank";
            unit.owner = owner;
            unit.position = cellCenter(sim, cellX, cellY);
            unit.previousPosition = unit.position;
            unit.hitPoints = 100;
            // Register through the real spawn path so the unit occupies grid
            // cells; movement and collision depend on that.
            auto unitId = sim.tryAddUnit(std::move(unit));
            REQUIRE(unitId.has_value());
            return *unitId;
        }

        FeatureDefinitionId addWallDef(GameSimulation& sim)
        {
            FeatureDefinition d{};
            d.name = "wall";
            d.footprintX = 1;
            d.footprintZ = 1;
            d.height = 10_ss;
            d.blocking = true;
            d.indestructible = true;
            return sim.featureDefinitions.insert(d);
        }

        void placeWall(GameSimulation& sim, FeatureDefinitionId wall, int x, int y)
        {
            REQUIRE(sim.addFeature(wall, x, y).has_value());
        }

        bool pathContainsStep(const std::vector<Point>& path, const Point& from, const Point& to)
        {
            for (size_t i = 1; i < path.size(); ++i)
            {
                if (path[i - 1] == from && path[i] == to)
                {
                    return true;
                }
            }
            return false;
        }
    }

    TEST_CASE("the pathfinder does not squeeze diagonally between touching obstacle corners", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(16, 16), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto wall = addWallDef(sim);
        auto tankId = addTank(sim, player, 2, 2, script);

        // Obstacles east and south of the unit; the cell diagonally
        // south-east is free but only reachable by going around.
        placeWall(sim, wall, 3, 2);
        placeWall(sim, wall, 2, 3);

        UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(3, 3));
        auto result = pathFinder.findPath(Point(2, 2));

        REQUIRE(result.type == AStarPathType::Complete);
        REQUIRE(result.path.size() > 2);
        REQUIRE_FALSE(pathContainsStep(result.path, Point(2, 2), Point(3, 3)));
    }

    TEST_CASE("the pathfinder routes around steep but passable ground", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        // A corrugated band across the middle of the map: every cell in it has
        // slope 6, passable for a unit with max slope 10 but counted as rough.
        // Flat ground above and below offers a longer but smoother route.
        GameSimulation sim(makeTerrain(16, 12, [](int x, int y) { return (x >= 4 && x <= 10 && y >= 4 && y <= 6) ? static_cast<unsigned char>((x % 2) * 6) : static_cast<unsigned char>(0); }), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto tankId = addTank(sim, player, 1, 5, script, 10u);

        UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(12, 5));
        auto result = pathFinder.findPath(Point(1, 5));

        REQUIRE(result.type == AStarPathType::Complete);
        std::ostringstream pathText;
        for (const auto& p : result.path)
        {
            pathText << "(" << p.x << "," << p.y << ") ";
        }
        INFO("path: " << pathText.str());
        for (const auto& p : result.path)
        {
            // Cells whose 2x2 height block contains a raised column (odd x in 5..9).
            bool onSlope = p.x >= 4 && p.x <= 9 && p.y >= 3 && p.y <= 6;
            REQUIRE_FALSE(onSlope);
        }
    }

    TEST_CASE("a class whose BadSlope equals its MaxSlope pays nothing for ground a tank calls rough", "[pathing]")
    {
        // TANKHOVER3 and TANKHOVER4 in the shipped data: MaxSlope=12,
        // BadSlope=12. The original's cell test admits a dry cell free up to
        // BadSlope and only "tight" between that and MaxSlope (0x47E145), so
        // for a hovercraft the corrugated band above is ordinary ground and
        // the straight line through it is the route. A class left at the
        // seeded default, half the max, still goes round.
        auto script = makeEmptyCobScript();
        auto corrugated = [](int x, int y) { return (x >= 4 && x <= 10 && y >= 4 && y <= 6) ? static_cast<unsigned char>((x % 2) * 6) : static_cast<unsigned char>(0); };

        auto routeThroughBand = [&](const MovementClassDefinition& definition) {
            GameSimulation sim(makeTerrain(16, 12, corrugated), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto classId = sim.movementClassDatabase.registerMovementClass(definition);
            sim.movementClassCollisionService.registerMovementClass(classId, computeWalkableGrid(sim.terrain, definition));

            UnitDefinition hover = makeTankDef(definition.maxSlope);
            hover.movementCollisionInfo = UnitDefinition::NamedMovementClass{classId};
            sim.unitDefinitions["tank"] = hover;
            auto unitId = addUnitOfType(sim, "tank", player, cellCenter(sim, 1, 5), script);

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, unitId, classId, 1u, 1u, Point(12, 5));
            auto result = pathFinder.findPath(Point(1, 5));
            REQUIRE(result.type == AStarPathType::Complete);
            std::ostringstream pathText;
            for (const auto& p : result.path)
            {
                pathText << "(" << p.x << "," << p.y << ") ";
            }
            INFO("path: " << pathText.str());
            bool crossesBand = false;
            for (const auto& p : result.path)
            {
                if (p.x >= 4 && p.x <= 9 && p.y >= 3 && p.y <= 6)
                {
                    crossesBand = true;
                }
            }
            return crossesBand;
        };

        // {name, footprintX, footprintZ, minWaterDepth, maxWaterDepth, maxSlope, maxWaterSlope, badSlope, badWaterSlope}
        REQUIRE(routeThroughBand(MovementClassDefinition{"TANKHOVER3", 1u, 1u, 0u, 255u, 12u, 12u, 12u, 12u}));
        // The band's cells are slope 6 exactly, and a cell is rough only
        // above the threshold, so the control sits one below it.
        REQUIRE_FALSE(routeThroughBand(MovementClassDefinition{"TANK", 1u, 1u, 0u, 255u, 12u, 12u, 5u, 5u}));
    }

    namespace
    {
        /**
         * Land at height 6 with sea level 5, and a strip of shallow water
         * (height 4) at x in [6, 8] from row 4 down to the bottom edge.
         * Rows 0-2 stay dry all the way across, so a unit on row 4 can
         * cross the strip directly (wading) or hop up two rows and stay dry.
         */
        MapTerrain makeShallowStripTerrain()
        {
            Grid<unsigned char> heights(16, 12, static_cast<unsigned char>(6));
            for (int y = 4; y < 12; ++y)
            {
                for (int x = 6; x <= 8; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(4));
                }
            }
            return MapTerrain(std::move(heights), 5_ss);
        }

        std::string describePath(const std::vector<Point>& path)
        {
            std::ostringstream pathText;
            for (const auto& p : path)
            {
                pathText << "(" << p.x << "," << p.y << ") ";
            }
            return pathText.str();
        }

        /** True when the cell's 2x2 height block touches the shallow strip. */
        bool touchesStrip(const Point& p)
        {
            return p.x >= 5 && p.x <= 8 && p.y >= 3;
        }
    }

    TEST_CASE("an amphibious unit detours over land around shallow water when the dry route is not much longer", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeShallowStripTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto tankId = addTank(sim, player, 3, 4, script, 10u);

        // Straight across is 8 steps, 4 of them wading; the dry route over
        // rows 2-3 is about 9.7 steps' worth. With wading at double cost
        // the dry route wins.
        UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(11, 4));
        auto result = pathFinder.findPath(Point(3, 4));

        REQUIRE(result.type == AStarPathType::Complete);
        INFO("path: " << describePath(result.path));
        for (const auto& p : result.path)
        {
            REQUIRE_FALSE(touchesStrip(p));
        }
    }

    TEST_CASE("a floating unit is not penalised for water and goes straight through", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeShallowStripTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto boatId = addTank(sim, player, 3, 4, script, 10u);
        sim.unitDefinitions["tank"].floater = true;

        UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, boatId, std::nullopt, 1u, 1u, Point(11, 4));
        auto result = pathFinder.findPath(Point(3, 4));

        REQUIRE(result.type == AStarPathType::Complete);
        INFO("path: " << describePath(result.path));
        // Straight along row 4: nine cells, no detour.
        REQUIRE(result.path.size() == 9);
        for (const auto& p : result.path)
        {
            REQUIRE(p.y == 4);
        }
    }

    TEST_CASE("a goal something is standing on is answered next to it", "[pathing]")
    {
        // Measured on Crystal Maze with RWE_PATH_PROFILE=1: nine path
        // requests in ten aim at a cell something is standing on, because
        // that is what an attack order is -- the goal resolves to the
        // target's own position. A cell with a unit on it is not walkable,
        // so A* can only answer by closing every cell it can reach, and
        // although such searches were 15% of the total they spent 78% of
        // every expansion the budget had.
        //
        // PathFindingService relaxes such a goal before the search starts,
        // to the nearest cell the unit could actually stand on. It is a
        // relaxation and not a cap: the search still returns the shortest
        // route to the relaxed goal, so nothing that merely takes a long
        // time is cut short.
        //
        // This exercises the pathfinder half of it -- what setAcceptableDistance
        // does to a blocked goal -- since the service's own gate is not
        // reachable from here.
        auto script = makeEmptyCobScript();

        SECTION("without the relaxation the search closes the reachable map")
        {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto wall = addWallDef(sim);
            auto tankId = addTank(sim, player, 2, 32, script);
            placeWall(sim, wall, 40, 32);

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(40, 32));
            auto result = pathFinder.findPath(Point(2, 32));

            // Nothing can stand where the wall is, so there is no route to
            // the goal itself and the search has to prove it.
            REQUIRE(result.type == AStarPathType::Partial);
            REQUIRE(result.exhausted);
            INFO("closed " << result.closedVertices.size());
            REQUIRE(result.closedVertices.size() > 1000);
        }

        SECTION("relaxed to the cell beside it, the same walk is a short search")
        {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto wall = addWallDef(sim);
            auto tankId = addTank(sim, player, 2, 32, script);
            placeWall(sim, wall, 40, 32);

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(40, 32));
            // Ten is one straight step, in the units octileDistanceScore
            // counts in; this is what the service works out for itself by
            // looking outward from the goal for somewhere standable.
            pathFinder.setAcceptableDistance(10);
            auto result = pathFinder.findPath(Point(2, 32));

            REQUIRE(result.type == AStarPathType::Complete);
            REQUIRE_FALSE(result.exhausted);
            REQUIRE_FALSE(result.path.empty());

            // It stops beside the wall rather than on it.
            auto last = result.path.back();
            CHECK(octileDistanceScore(last, Point(40, 32)) <= 10);
            CHECK(last != Point(40, 32));

            // And it cost a fraction of the sweep above.
            INFO("closed " << result.closedVertices.size());
            CHECK(result.closedVertices.size() < 1000);
        }
    }

    /**
     * The routes below are not the only sensible ones: on open ground a great
     * many eight-way routes are the same length, and which one comes out is
     * decided by how the open list breaks a tie between two vertices of equal
     * estimated cost. That makes them the right thing to pin. The other tests
     * in this file check properties -- around the obstacle, off the slope --
     * and a rewrite of the search that reordered the open list could satisfy
     * every one of them while sending every unit in the game down a different
     * road. These cases fail if the answer changes at all.
     *
     * If one of them fails after a deliberate change to the search, work out
     * the new route by hand before believing it, and say in the commit message
     * why the route moved.
     */
    TEST_CASE("the pathfinder returns the same route it always has", "[pathing]")
    {
        auto script = makeEmptyCobScript();

        SECTION("across open ground, where many routes are the same length")
        {
            GameSimulation sim(makeFlatTerrain(24, 18), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto tankId = addTank(sim, player, 2, 2, script);

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(20, 15));
            auto result = pathFinder.findPath(Point(2, 2));

            REQUIRE(result.type == AStarPathType::Complete);
            REQUIRE(describePath(result.path) == "(2,2) (3,2) (4,2) (5,2) (6,2) (7,2) (8,3) (9,4) (10,5) (11,6) (12,7) (13,8) (14,9) (15,10) (16,11) (17,12) (18,13) (19,14) (20,15) ");
        }

        SECTION("around a wall with a gap in it")
        {
            GameSimulation sim(makeFlatTerrain(24, 18), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto wall = addWallDef(sim);
            auto tankId = addTank(sim, player, 2, 9, script);

            // A wall down the middle of the map with one gap, low down.
            for (int y = 0; y <= 12; ++y)
            {
                placeWall(sim, wall, 11, y);
            }

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(20, 9));
            auto result = pathFinder.findPath(Point(2, 9));

            REQUIRE(result.type == AStarPathType::Complete);
            REQUIRE(describePath(result.path) == "(2,9) (3,9) (4,9) (5,9) (6,10) (7,11) (8,12) (9,13) (10,14) (11,14) (12,14) (13,14) (14,14) (15,14) (16,13) (17,12) (18,11) (19,10) (20,9) ");
        }

        SECTION("over ground that is passable but rough")
        {
            GameSimulation sim(makeTerrain(16, 12, [](int x, int y) { return (x >= 4 && x <= 10 && y >= 4 && y <= 6) ? static_cast<unsigned char>((x % 2) * 6) : static_cast<unsigned char>(0); }), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto tankId = addTank(sim, player, 1, 5, script, 10u);

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(12, 5));
            auto result = pathFinder.findPath(Point(1, 5));

            REQUIRE(result.type == AStarPathType::Complete);
            REQUIRE(describePath(result.path) == "(1,5) (2,5) (3,6) (4,7) (5,7) (6,7) (7,7) (8,7) (9,7) (10,7) (11,6) (12,5) ");
        }

        SECTION("detouring around water a wading unit would rather avoid")
        {
            GameSimulation sim(makeShallowStripTerrain(), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto tankId = addTank(sim, player, 3, 4, script, 10u);

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(11, 4));
            auto result = pathFinder.findPath(Point(3, 4));

            REQUIRE(result.type == AStarPathType::Complete);
            REQUIRE(describePath(result.path) == "(3,4) (4,3) (5,2) (6,2) (7,2) (8,2) (9,2) (10,3) (11,4) ");
        }

        SECTION("stopping at the closest reachable point when the goal is walled off")
        {
            GameSimulation sim(makeFlatTerrain(24, 18), 0u, 0, 0);
            auto player = addPlayer(sim);
            auto wall = addWallDef(sim);
            auto tankId = addTank(sim, player, 2, 9, script);

            // The goal sits in a sealed box, so the search runs out of open
            // vertices and falls back on the closest one it saw. Which vertex
            // that is depends on the order the search closed them in.
            for (int x = 17; x <= 21; ++x)
            {
                placeWall(sim, wall, x, 7);
                placeWall(sim, wall, x, 11);
            }
            for (int y = 8; y <= 10; ++y)
            {
                placeWall(sim, wall, 17, y);
                placeWall(sim, wall, 21, y);
            }

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(19, 9));
            auto result = pathFinder.findPath(Point(2, 9));

            REQUIRE(result.type == AStarPathType::Partial);
            REQUIRE(result.exhausted);
            REQUIRE(describePath(result.path) == "(2,9) (3,9) (4,9) (5,9) (6,9) (7,9) (8,9) (9,9) (10,9) (11,9) (12,9) (13,9) (14,9) (15,9) (16,9) ");
        }
    }

    TEST_CASE("a move order to an unreachable point completes at the closest reachable point", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(16, 16), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto wall = addWallDef(sim);
        auto tankId = addTank(sim, player, 2, 8, script);

        // Wall off cell (8, 8) completely.
        for (int y = 7; y <= 9; ++y)
        {
            for (int x = 7; x <= 9; ++x)
            {
                if (x != 8 || y != 8)
                {
                    placeWall(sim, wall, x, y);
                }
            }
        }

        auto target = cellCenter(sim, 8, 8);
        sim.getUnitState(tankId).orders.push_back(MoveOrder(target));

        for (int i = 0; i < 600 && !sim.getUnitState(tankId).orders.empty(); ++i)
        {
            sim.tick();
        }

        const auto& tank = sim.getUnitState(tankId);
        {
            auto moving = std::get_if<NavigationStateMoving>(&tank.navigationState.state);
            auto ground = std::get_if<UnitPhysicsInfoGround>(&tank.physics);
            auto direction = UnitState::toDirection(tank.rotation);
            auto next = tank.position + direction * 4_ss;
            auto nextRegion = sim.computeFootprintRegion(next, sim.unitDefinitions.at("tank").movementCollisionInfo);
            auto walkable = isGridPointWalkable(sim.terrain, sim.getAdHocMovementClass(sim.unitDefinitions.at("tank").movementCollisionInfo), nextRegion.x, nextRegion.y);
            INFO("dir=" << direction.x.value << "," << direction.z.value
                        << " next=" << next.x.value << "," << next.z.value
                        << " nextRegion=" << nextRegion.x << "," << nextRegion.y
                        << " collision=" << sim.isCollisionAt(nextRegion, tankId)
                        << " walkable=" << walkable
                        << " waypoint=" << (moving && moving->path ? moving->path->path.waypoints.back().x.value : -999.0f) << "," << (moving && moving->path ? moving->path->path.waypoints.back().z.value : -999.0f)
                        << " target=" << (ground ? ground->steeringInfo.targetSpeed.value : -1.0f));
            INFO("pos=" << tank.position.x.value << "," << tank.position.z.value
                        << " moving=" << (moving != nullptr)
                        << " hasPath=" << (moving && moving->path ? 1 : 0)
                        << " waypoints=" << (moving && moving->path ? moving->path->path.waypoints.size() : 0)
                        << " reachable=" << (moving && moving->reachableDestination ? 1 : 0)
                        << " pathRequested=" << (moving ? static_cast<int>(moving->pathRequested) : -1)
                        << " desired=" << tank.navigationState.desiredDestination.has_value()
                        << " speed=" << (ground ? ground->currentSpeed.value : -1.0f)
                        << " inCollision=" << tank.inCollision
                        << " pendingRequests=" << sim.pathRequests.size());
            REQUIRE(tank.orders.empty());
        }
        // We stopped next to the wall, not inside it.
        auto cell = sim.terrain.worldToHeightmapCoordinate(tank.position);
        REQUIRE(cell.x < 7);
        REQUIRE(cell.x > 2);
    }

    TEST_CASE("a goal across water is resolved without flooding the reachable map", "[pathing]")
    {
        // The goal lies in a different terrain region from the unit, so no
        // route can exist. The cheap first pass cannot get any closer than
        // where the unit stands -- it is hard against the water -- which used
        // to leave the A* to run out over the whole reachable component
        // before it could say the obvious. The service now reads the terrain
        // regions and finishes at once.
        auto script = makeEmptyCobScript();

        // Land either side of a channel too deep for the walker. Sea level 30
        // and the channel at 0 makes it depth 30, past the class's 0.
        Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(60));
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 30; x < 34; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(0));
            }
        }
        GameSimulation sim(MapTerrain(std::move(heights), 30_ss), 0u, 0, 0);
        auto player = addPlayer(sim);

        // Terrain only reaches the search through a named movement class's
        // registered walkable grid, so the walker needs one.
        MovementClassDefinition walkerClass{"BENCHWALK", 1u, 1u, 0u, 0u, 255u, 255u};
        auto classId = sim.movementClassDatabase.registerMovementClass(walkerClass);
        sim.movementClassCollisionService.registerMovementClass(classId, computeWalkableGrid(sim.terrain, walkerClass));

        UnitDefinition tank = makeTankDef(255u);
        tank.movementCollisionInfo = UnitDefinition::NamedMovementClass{classId};
        sim.unitDefinitions["tank"] = tank;

        // Hard against the west bank, directly opposite the goal on the east.
        auto tankId = addUnitOfType(sim, "tank", player, cellCenter(sim, 29, 32), script);
        sim.getUnitState(tankId).orders.push_back(MoveOrder(cellCenter(sim, 40, 32)));

        for (int i = 0; i < 200 && !sim.getUnitState(tankId).orders.empty(); ++i)
        {
            sim.tick();
        }

        // Proving the place cannot be reached must not cost a sweep of every
        // cell the unit can get to.
        REQUIRE(sim.pathFindingService.counters.expansions < 200);

        // And it is the terrain-region answer that said so, not an exhausted
        // search: the two used to be counted together, which made the playtest
        // H1 rule fire on a pathfinder that was doing exactly this.
        REQUIRE(sim.pathFindingService.counters.searchesRegionUnreachable >= 1);
        REQUIRE(sim.pathFindingService.counters.searchesExhausted == 0);

        // The order completed, and the unit stopped on its own bank.
        REQUIRE(sim.getUnitState(tankId).orders.empty());
        auto cell = sim.terrain.worldToHeightmapCoordinate(sim.getUnitState(tankId).position);
        REQUIRE(cell.x < 30);
    }

    TEST_CASE("answering across water still costs the tick something", "[pathing]")
    {
        // A search answered from the terrain regions spends no expansions,
        // and the update loop is bounded by expansions. So a queue full of
        // goals across the same water was answered in its entirety on one
        // tick, however many units were in it and however far each of their
        // walks ran before giving up. The walk is what gets charged now,
        // because it is the work that was actually done.
        auto script = makeEmptyCobScript();

        Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(60));
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 30; x < 34; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(0));
            }
        }
        GameSimulation sim(MapTerrain(std::move(heights), 30_ss), 0u, 0, 0);
        auto player = addPlayer(sim);

        MovementClassDefinition walkerClass{"BENCHWALK", 1u, 1u, 0u, 0u, 255u, 255u};
        auto classId = sim.movementClassDatabase.registerMovementClass(walkerClass);
        sim.movementClassCollisionService.registerMovementClass(classId, computeWalkableGrid(sim.terrain, walkerClass));

        UnitDefinition tank = makeTankDef(255u);
        tank.movementCollisionInfo = UnitDefinition::NamedMovementClass{classId};
        sim.unitDefinitions["tank"] = tank;

        // A rank of them along the west bank, every one ordered across.
        const int rank = 12;
        for (int i = 0; i < rank; ++i)
        {
            auto id = addUnitOfType(sim, "tank", player, cellCenter(sim, 29, 20 + i), script);
            sim.getUnitState(id).orders.push_back(MoveOrder(cellCenter(sim, 40, 20 + i)));
        }

        // Small enough that a dozen walks cannot fit inside it. The figure
        // is not a property of anything: it is chosen to be smaller than
        // what a dozen of these walks cost, so that the charge has to show.
        sim.pathFindingService.expansionBudgetPerTick = 8;

        // Twice: the first tick is where the orders turn into requests, and
        // the service has already run by the time they do.
        sim.tick();
        sim.tick();

        // Some of them were answered and the rest are still queued. Before
        // the charge, every one of the twelve was answered on this tick.
        auto answered = sim.pathFindingService.counters.searches;
        CHECK(answered > 0);
        CHECK(answered < rank);

        // And it is the charge doing it and not the expansions, of which a
        // goal in another region spends none.
        CHECK(sim.pathFindingService.counters.expansions == 0);

        // The queue still drains: what changed is how many ticks it takes,
        // not whether it finishes.
        for (int i = 0; i < 200 && !sim.pathRequests.empty(); ++i)
        {
            sim.tick();
        }
        CHECK(sim.pathRequests.empty());
        CHECK(sim.pathFindingService.counters.searches >= rank);
    }

    TEST_CASE("a build order for a site the builder cannot reach is dropped", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(16, 16), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto wall = addWallDef(sim);

        // A builder tank, walled in on all sides.
        sim.unitDefinitions["tank"] = makeTankDef(10u);
        auto tankId = addTank(sim, player, 8, 8, script);
        sim.unitDefinitions["tank"].builder = true;
        sim.unitDefinitions["tank"].buildDistance = 64_ss;
        for (int y = 6; y <= 10; ++y)
        {
            for (int x = 6; x <= 10; ++x)
            {
                if (std::max(std::abs(x - 8), std::abs(y - 8)) == 2)
                {
                    placeWall(sim, wall, x, y);
                }
            }
        }

        auto solar = makeTankDef(10u);
        solar.isMobile = false;
        solar.canMove = false;
        solar.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["solar"] = solar;

        sim.getUnitState(tankId).orders.push_back(BuildOrder("solar", cellCenter(sim, 14, 8)));

        for (int i = 0; i < 120 && !sim.getUnitState(tankId).orders.empty(); ++i)
        {
            sim.tick();
        }

        REQUIRE(sim.getUnitState(tankId).orders.empty());
        REQUIRE(sim.units.tryGet(tankId).has_value());
        // Nothing was lathed into existence across the wall.
        int units = 0;
        for (const auto& [_, u] : sim.units)
        {
            (void)u;
            ++units;
        }
        REQUIRE(units == 1);
    }

    TEST_CASE("a unit standing inside a blocking feature can walk out of it", "[pathing]")
    {
        // A builder standing inside a wreck it was told to reclaim -- or one a
        // corpse was dropped on top of -- must still be able to walk away.
        // Before the fix every one of the eight neighbouring footprints also
        // collided with the wreck, so the search had no successors, returned a
        // one-point path, and the unit was told it had arrived where it stood.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim);

        // ARMCK: the 2x2-footprint construction kbot the autoreclaim tests use.
        UnitDefinition kbot = makeTankDef(255u);
        kbot.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["armck"] = kbot;

        // armsolar_dead from features/Corpses/arm_corpses.tdf: five cells
        // across and blocking, so it blocks every movement class the same way
        // (TOTALA-EXE.md §95).
        FeatureDefinition wreckDef{};
        wreckDef.name = "armsolar_dead";
        wreckDef.footprintX = 5;
        wreckDef.footprintZ = 5;
        wreckDef.height = 40_ss;
        wreckDef.reclaimable = true;
        wreckDef.metal = 116;
        wreckDef.damage = 261;
        wreckDef.blocking = true;
        auto wreck = sim.featureDefinitions.insert(wreckDef);
        auto wreckId = sim.addFeature(wreck, 20, 20).value();
        auto wreckPosition = sim.getFeature(wreckId).position;

        // Thirty world units from the wreck's centre: the kbot's 2x2 footprint
        // and all eight of its neighbours lie on the wreck's cells 20..24.
        auto unitId = addUnitOfType(sim, "armck", player, wreckPosition + SimVector(30_ss, 0_ss, 0_ss), script);
        const auto& unitDefinition = sim.unitDefinitions.at("armck");
        auto startRegion = sim.computeFootprintRegion(sim.getUnitState(unitId).position, unitDefinition.movementCollisionInfo);
        REQUIRE(sim.isCollisionAt(startRegion, unitId));

        auto destination = cellCenter(sim, 34, 22);
        sim.getUnitState(unitId).orders.push_back(MoveOrder(destination));

        for (int i = 0; i < 2000 && !sim.getUnitState(unitId).orders.empty(); ++i)
        {
            sim.tick();
        }

        // The order completed, and it completed somewhere off the wreck.
        REQUIRE(sim.getUnitState(unitId).orders.empty());
        auto endRegion = sim.computeFootprintRegion(sim.getUnitState(unitId).position, unitDefinition.movementCollisionInfo);
        INFO("start=" << startRegion.x << "," << startRegion.y << " end=" << endRegion.x << "," << endRegion.y);
        REQUIRE_FALSE(sim.isCollisionAt(endRegion, unitId));
        // Every footprint cell is outside the wreck's 5x5 footprint.
        REQUIRE((endRegion.x + endRegion.width <= 20 || endRegion.x >= 25 || endRegion.y + endRegion.height <= 20 || endRegion.y >= 25));
    }

    TEST_CASE("computeSlopeSpeedFactor", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        // Flat ground for x < 4, a plateau of height 8 from x = 4.
        GameSimulation sim(makeTerrain(10, 10, [](int x, int) { return x >= 4 ? 8 : 0; }), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto tankId = addTank(sim, player, 3, 3, script, 16u);
        auto& tank = sim.getUnitState(tankId);
        // Stand exactly on the tile corner so heights are not interpolated.
        tank.position = sim.terrain.heightmapIndexToWorldCorner(3, 3);

        SECTION("climbing towards the slope limit slows the unit")
        {
            tank.rotation = UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss));
            // rise 8 over one tile against a limit of 16 -> 1 - (0.5 / 2) = 0.75
            REQUIRE(computeSlopeSpeedFactor(sim.terrain, tank, 16u).value == Catch::Approx(0.75f));
        }

        SECTION("a steeper climb is never slower than a quarter speed")
        {
            tank.rotation = UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss));
            REQUIRE(computeSlopeSpeedFactor(sim.terrain, tank, 4u).value == Catch::Approx(0.25f));
        }

        SECTION("flat or downhill ground is full speed")
        {
            tank.rotation = UnitState::toRotation(SimVector(-1_ss, 0_ss, 0_ss));
            REQUIRE(computeSlopeSpeedFactor(sim.terrain, tank, 16u).value == Catch::Approx(1.0f));
        }

        SECTION("units with no slope limit are never slowed")
        {
            tank.rotation = UnitState::toRotation(SimVector(1_ss, 0_ss, 0_ss));
            REQUIRE(computeSlopeSpeedFactor(sim.terrain, tank, 255u).value == Catch::Approx(1.0f));
        }
    }

    TEST_CASE("a search cut into slices reaches the answer it would have reached in one go", "[pathing]")
    {
        // The whole point of slicing is that it is invisible in the result:
        // the scheduler stops the search when the tick's budget runs out and
        // carries it on next tick, and what comes out is the route an
        // uninterrupted search would have found. One expansion per slice is
        // the cruellest schedule there is, so it is the one to check.
        auto script = makeEmptyCobScript();

        GameSimulation sim(makeFlatTerrain(24, 18), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto wall = addWallDef(sim);
        auto tankId = addTank(sim, player, 2, 9, script);

        for (int y = 0; y <= 12; ++y)
        {
            placeWall(sim, wall, 11, y);
        }

        UnitPathFinder wholeFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(20, 9));
        auto whole = wholeFinder.findPath(Point(2, 9));

        UnitPathFinder slicedFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(20, 9));
        slicedFinder.beginSearch(Point(2, 9));

        unsigned int slices = 0;
        unsigned int expansions = 0;
        while (!slicedFinder.isSearchFinished())
        {
            expansions += slicedFinder.stepSearch(1);
            ++slices;
        }
        auto sliced = slicedFinder.takeResult();

        REQUIRE(sliced.type == whole.type);
        REQUIRE(describePath(sliced.path) == describePath(whole.path));

        // Every expansion is accounted for, which is what the scheduler
        // deducts from its budget, and the search really was interrupted
        // rather than quietly run to completion on the first call.
        REQUIRE(expansions == whole.closedVertices.size());
        REQUIRE(slices > 1);
    }

    TEST_CASE("a search is not cut off after a thousand expansions", "[pathing]")
    {
        // RWE used to stop a search dead at a thousand expansions and hand
        // back whatever partial route it had, which the unit walked before
        // asking again. The original has no such cap -- it slices its A* and
        // resumes it, so a search ends only at the goal or at an empty open
        // list (TOTALA-EXE.md S:87). A goal walled off on a large map is the
        // case that tells the two apart: the old cap stopped at a thousand
        // with the open list still full, where an uncapped search looks at
        // every cell it can reach and comes back knowing the place cannot be
        // reached at all.
        auto script = makeEmptyCobScript();

        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto wall = addWallDef(sim);
        auto tankId = addTank(sim, player, 2, 2, script);

        for (int y = 58; y <= 60; ++y)
        {
            for (int x = 58; x <= 60; ++x)
            {
                if (x != 59 || y != 59)
                {
                    placeWall(sim, wall, x, y);
                }
            }
        }

        UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, tankId, std::nullopt, 1u, 1u, Point(59, 59));
        auto result = pathFinder.findPath(Point(2, 2));

        REQUIRE(result.type == AStarPathType::Partial);
        REQUIRE(result.exhausted);
        REQUIRE(result.closedVertices.size() > 1000);
    }

    TEST_CASE("a unit that asks again while its search is sliced keeps its place in the queue", "[pathing]")
    {
        // The scheduler serves the queue one search at a time and carries a
        // search that outruns the tick into the next one. The request at the
        // head of the queue is the one that search belongs to: update() asserts
        // it when the search lands, and a save reads a suspended search off the
        // head. A unit can ask again while its own search is in flight -- its
        // goal is re-resolved each tick, or it finishes the straight line it
        // was walking while it waited -- and requestPath used to shuffle it to
        // the back for fairness, which left the head pointing at a different
        // unit and tripped the assertion the moment the search completed
        // (issue #72, seen in large fights where searches span many ticks).
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(24, 18), 0u, 0, 0);
        auto player = addPlayer(sim);

        // `first` is the unit whose search will be sliced; `second` only ever
        // holds a request, so that the head is a different unit whenever
        // `first` is asked again.
        auto first = addTank(sim, player, 2, 2, script);
        auto second = addTank(sim, player, 2, 5, script);

        // One expansion a tick: no search can finish in the tick it began.
        sim.pathFindingService.expansionBudgetPerTick = 1;

        sim.getUnitState(first).orders.push_back(MoveOrder(cellCenter(sim, 20, 15)));

        for (int i = 0; i < 400; ++i)
        {
            sim.tick();

            // Stand in for the behaviour pass re-asking on the unit whose
            // search is in flight (see the collision and end-of-path branches
            // in groundUnitMoveTo, and the re-resolved-goal branch above them).
            // The request is for the place `first` is already heading, so the
            // search in flight is its turn and it must stay at the head.
            sim.requestPath(second);
            sim.requestPath(first);
        }

        // It got there in the end: the sliced search did complete and the
        // unit followed the route rather than being wedged in the queue.
        auto start = cellCenter(sim, 2, 2);
        REQUIRE(sim.getUnitState(first).position.distanceSquared(start) > 0_ss);
    }
}
