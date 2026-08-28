#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/pathfinding/UnitPathFinder.h>
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

        MapTerrain makeFlatTerrain(int width = 16, int height = 16)
        {
            return makeTerrain(width, height, [](int, int) { return 0; });
        }

        PlayerId addPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("player"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
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
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
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

    TEST_CASE("a move order to an unreachable point completes at the closest reachable point", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
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

    TEST_CASE("a build order for a site the builder cannot reach is dropped", "[pathing]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
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
}
