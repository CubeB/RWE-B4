#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/pathfinding/OaUnitPathSearch.h>
#include <rwe/pathfinding/PathfindingBackend.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <vector>

namespace rwe
{
    namespace
    {
        UnitDefinition makeWalkerDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = SimScalar(1.5f);
            d.acceleration = 0.1_ssf;
            d.brakeRate = 0.1_ssf;
            d.turnRate = 360_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawn(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "walker";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            return sim.tryAddUnit(std::move(unit)).value();
        }
    }

    TEST_CASE("the original search backend finds a route to a reachable goal", "[pathing][oa]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["walker"] = makeWalkerDef();
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        auto unitId = spawn(sim, player, SimVector(-300_ss, 0_ss, -300_ss), script);

        auto start = sim.computeFootprintRegion(sim.getUnitState(unitId).position, sim.unitDefinitions.at("walker").movementCollisionInfo);
        OaUnitPathSearch search(&sim, unitId, std::nullopt, start.width, start.height, Point(20, 20), 0);
        search.beginSearch(Point(start.x, start.y));

        while (!search.isSearchFinished())
        {
            REQUIRE(search.stepSearch(100) > 0);
        }

        auto path = search.takeResult(sim);
        REQUIRE_FALSE(path.destinationUnreachable);
        REQUIRE(path.waypoints.size() >= 2);

        // The route begins behind the unit and ends on the goal cell, which is
        // where the original's route emitter puts it: the footprint centre at
        // the goal cell, shifted into RWE's centred world.
        auto origin = sim.terrain.heightmapIndexToWorldCorner(Point(0, 0));
        auto goalCentre = origin
            + SimVector(SimScalar(static_cast<float>(20 * 16 + static_cast<int>(start.width) * 8)), 0_ss, SimScalar(static_cast<float>(20 * 16 + static_cast<int>(start.height) * 8)));
        REQUIRE(path.waypoints.back().x == goalCentre.x);
        REQUIRE(path.waypoints.back().z == goalCentre.z);
    }

    TEST_CASE("the original search backend reports a sealed-in unit with no route", "[pathing][oa]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["walker"] = makeWalkerDef();
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        // A 2x2 unit at cells (4,4)..(5,5), walled in by a 4x4 ring.
        auto spawnPos = sim.terrain.heightmapIndexToWorldCorner(Point(4, 4)) + SimVector(16_ss, 0_ss, 16_ss);
        auto unitId = spawn(sim, player, spawnPos, script);

        FeatureDefinition wall{};
        wall.name = "wall";
        wall.footprintX = 1;
        wall.footprintZ = 1;
        wall.height = 10_ss;
        wall.blocking = true;
        wall.indestructible = true;
        auto wallId = sim.featureDefinitions.insert(wall);
        for (int y = 3; y <= 6; ++y)
        {
            for (int x = 3; x <= 6; ++x)
            {
                const bool interior = x >= 4 && x <= 5 && y >= 4 && y <= 5;
                if (!interior)
                {
                    REQUIRE(sim.addFeature(wallId, x, y).has_value());
                }
            }
        }

        auto start = sim.computeFootprintRegion(sim.getUnitState(unitId).position, sim.unitDefinitions.at("walker").movementCollisionInfo);
        OaUnitPathSearch search(&sim, unitId, std::nullopt, start.width, start.height, Point(20, 20), 0);
        search.beginSearch(Point(start.x, start.y));

        // Bounded so a regression that runs for ever fails rather than hangs.
        unsigned int expansions = 0;
        while (!search.isSearchFinished() && expansions < 200000)
        {
            expansions += search.stepSearch(100);
        }

        REQUIRE(search.isSearchFinished());
        auto path = search.takeResult(sim);
        REQUIRE(path.destinationUnreachable);
    }

    TEST_CASE("the original search backend slices to the same route as an uninterrupted search", "[pathing][oa]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["walker"] = makeWalkerDef();
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        auto unitId = spawn(sim, player, SimVector(-300_ss, 0_ss, -300_ss), script);
        auto start = sim.computeFootprintRegion(sim.getUnitState(unitId).position, sim.unitDefinitions.at("walker").movementCollisionInfo);

        auto run = [&](unsigned int slice) {
            OaUnitPathSearch search(&sim, unitId, std::nullopt, start.width, start.height, Point(24, 18), 0);
            search.beginSearch(Point(start.x, start.y));
            while (!search.isSearchFinished())
            {
                search.stepSearch(slice);
            }
            return search.takeResult(sim).waypoints;
        };

        auto oneByOne = run(1);
        auto inSlices = run(100);
        REQUIRE(oneByOne.size() == inSlices.size());
        for (std::size_t i = 0; i < oneByOne.size(); ++i)
        {
            REQUIRE(oneByOne[i].x == inSlices[i].x);
            REQUIRE(oneByOne[i].z == inSlices[i].z);
        }
    }

    TEST_CASE("the original search backend relaxes a blocked goal and stops short of it", "[pathing][oa]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["walker"] = makeWalkerDef();
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        auto unitId = spawn(sim, player, SimVector(-200_ss, 0_ss, -200_ss), script);

        // One wall on the goal cell: the unit cannot stand there, but it can
        // stand beside it, so the search relaxes the goal.
        FeatureDefinition wall{};
        wall.name = "wall";
        wall.footprintX = 1;
        wall.footprintZ = 1;
        wall.height = 10_ss;
        wall.blocking = true;
        wall.indestructible = true;
        auto wallId = sim.featureDefinitions.insert(wall);
        REQUIRE(sim.addFeature(wallId, 16, 16).has_value());

        auto start = sim.computeFootprintRegion(sim.getUnitState(unitId).position, sim.unitDefinitions.at("walker").movementCollisionInfo);
        OaUnitPathSearch search(&sim, unitId, std::nullopt, start.width, start.height, Point(16, 16), 0);
        search.beginSearch(Point(start.x, start.y));

        unsigned int expansions = 0;
        while (!search.isSearchFinished() && expansions < 200000)
        {
            expansions += search.stepSearch(100);
        }

        REQUIRE(search.isSearchFinished());
        REQUIRE(search.goalWasRelaxed());

        auto path = search.takeResult(sim);
        REQUIRE(path.destinationUnreachable);

        auto origin = sim.terrain.heightmapIndexToWorldCorner(Point(0, 0));
        auto goalCentre = origin
            + SimVector(SimScalar(static_cast<float>(16 * 16 + static_cast<int>(start.width) * 8)), 0_ss, SimScalar(static_cast<float>(16 * 16 + static_cast<int>(start.height) * 8)));
        const bool atGoalCentre = path.waypoints.back().x == goalCentre.x && path.waypoints.back().z == goalCentre.z;
        REQUIRE_FALSE(atGoalCentre);
    }
    TEST_CASE("a unit ordered to move arrives under the original search backend", "[pathing][oa]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["walker"] = makeWalkerDef();
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        sim.pathFindingService.backend = PathfindingBackend::OpenAnnihilation;

        auto destination = SimVector(200_ss, 0_ss, 120_ss);
        auto unitId = spawn(sim, player, SimVector(-200_ss, 0_ss, -120_ss), script);
        sim.getUnitState(unitId).orders.push_back(MoveOrder(destination));

        for (int tick = 0; tick < 1200 && !sim.getUnitState(unitId).orders.empty(); ++tick)
        {
            sim.tick();
        }

        const auto& unit = sim.getUnitState(unitId);
        REQUIRE(unit.orders.empty());
        REQUIRE(unit.position.distanceSquared(destination) < (32_ss * 32_ss));
    }
}