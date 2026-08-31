#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /** Dry land to the west, open sea to the east, split down the middle. */
        MapTerrain makeCoastTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(60));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 32; x < 64; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(0));
                }
            }
            return MapTerrain(std::move(heights), 30_ss);
        }

        PlayerId addPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("pilot"),
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
            script->pieces.push_back("base");
            return script;
        }

        void registerModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        UnitDefinition makePlaneDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canFly = true;
            d.cruiseAltitude = 100_ss;
            d.maxVelocity = 6_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitId spawnPlane(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "plane";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            auto id = sim.tryAddUnit(std::move(unit)).value();
            auto& spawned = sim.getUnitState(id);
            spawned.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            sim.flyingUnitsSet.insert(id);
            return id;
        }

        /** A proper immobile structure with a yardmap, buildable by the test builder. */
        UnitDefinition makeStructureDef(bool factory)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.builder = factory;
            d.maxHitPoints = 100;
            d.buildTime = 300u;
            d.buildCostMetal = Metal(1.0f);
            d.buildCostEnergy = Energy(1.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(4, 4, YardMapCell::Ground);
            return d;
        }

        UnitDefinition makeBuilderDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.builder = true;
            d.buildDistance = 200_ss;
            d.workerTimePerTick = 3;
            d.maxVelocity = 3_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawnGroundUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        int countArrivals(const GameSimulation& sim, UnitId unitId)
        {
            int n = 0;
            for (const auto& e : sim.events)
            {
                if (auto arrived = std::get_if<UnitArrivedEvent>(&e); arrived && arrived->unitId == unitId)
                {
                    ++n;
                }
            }
            return n;
        }
    }

    TEST_CASE("aircraft finish their orders instead of circling the spot", "[aircraft]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["plane"] = makePlaneDef();
        registerModel(sim);

        auto planeId = spawnPlane(sim, player, SimVector(-400_ss, 100_ss, 0_ss), script);

        SECTION("a move order completes on arrival")
        {
            sim.getUnitState(planeId).orders.push_back(createMoveOrder(SimVector(400_ss, 0_ss, 0_ss)));
            bool finished = false;
            for (int i = 0; i < 600 && !finished; ++i)
            {
                sim.tick();
                finished = sim.getUnitState(planeId).orders.empty();
            }
            REQUIRE(finished);
            REQUIRE(countArrivals(sim, planeId) == 1);
        }

        SECTION("only the last of a queue of moves is reported")
        {
            sim.getUnitState(planeId).orders.push_back(createMoveOrder(SimVector(0_ss, 0_ss, 200_ss)));
            sim.getUnitState(planeId).orders.push_back(createMoveOrder(SimVector(300_ss, 0_ss, 200_ss)));
            sim.getUnitState(planeId).orders.push_back(createMoveOrder(SimVector(300_ss, 0_ss, -200_ss)));

            bool finished = false;
            for (int i = 0; i < 1800 && !finished; ++i)
            {
                sim.tick();
                finished = sim.getUnitState(planeId).orders.empty();
            }
            REQUIRE(finished);
            // Three waypoints flown, one report at the end of the journey.
            REQUIRE(countArrivals(sim, planeId) == 1);
        }
    }

    TEST_CASE("an aircraft leaving the factory pad settles down by itself", "[aircraft]")
    {
        // A newly built aircraft is told to clear the pad and nothing else.
        // It used to hover there for good, because the order it was given
        // could never be satisfied to within a ground unit's tolerance.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["plane"] = makePlaneDef();
        registerModel(sim);

        auto planeId = spawnPlane(sim, player, SimVector(0_ss, 100_ss, 0_ss), script);
        auto padRect = sim.computeFootprintRegion(SimVector(0_ss, 0_ss, 0_ss), sim.unitDefinitions.at("plane").movementCollisionInfo);
        sim.getUnitState(planeId).orders.push_back(BuggerOffOrder(padRect));

        bool cleared = false;
        for (int i = 0; i < 900 && !cleared; ++i)
        {
            sim.tick();
            cleared = sim.getUnitState(planeId).orders.empty();
        }
        REQUIRE(cleared);

        // With nothing left to do it puts itself down instead of hovering.
        bool landed = false;
        for (int i = 0; i < 900 && !landed; ++i)
        {
            sim.tick();
            landed = std::holds_alternative<UnitPhysicsInfoGround>(sim.getUnitState(planeId).physics);
        }
        REQUIRE(landed);
    }

    TEST_CASE("buildings go up with a slight random twist", "[aircraft][construction]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["builder"] = makeBuilderDef();
        sim.unitDefinitions["STRUCTURE"] = makeStructureDef(false);
        sim.unitDefinitions["FACTORY"] = makeStructureDef(true);
        sim.unitScriptDefinitions["STRUCTURE"] = *script;
        sim.unitScriptDefinitions["FACTORY"] = *script;
        registerModel(sim);

        auto builderId = spawnGroundUnit(sim, "builder", player, SimVector(-60_ss, 0_ss, 0_ss), script);

        SECTION("an ordinary building is twisted up to ten degrees either way")
        {
            sim.getUnitState(builderId).orders.push_back(BuildOrder("STRUCTURE", SimVector(40_ss, 0_ss, 0_ss)));
            std::optional<UnitId> structureId;
            for (int i = 0; i < 300 && !structureId; ++i)
            {
                sim.tick();
                for (const auto& [id, unit] : sim.units)
                {
                    if (unit.unitType == "STRUCTURE")
                    {
                        structureId = id;
                    }
                }
            }
            REQUIRE(structureId.has_value());
            // Ten degrees is 1/36 of a 16-bit turn, about 1820.
            const auto tenDegrees = SimAngle(1821);
            auto twist = angleBetween(SimAngle(0), sim.getUnitState(*structureId).rotation);
            REQUIRE(twist.value <= tenDegrees.value);
            REQUIRE(twist.value > 0);
        }

        SECTION("a factory stays square so its pad lines up")
        {
            sim.getUnitState(builderId).orders.push_back(BuildOrder("FACTORY", SimVector(40_ss, 0_ss, 0_ss)));
            std::optional<UnitId> factoryId;
            for (int i = 0; i < 300 && !factoryId; ++i)
            {
                sim.tick();
                for (const auto& [id, unit] : sim.units)
                {
                    if (unit.unitType == "FACTORY")
                    {
                        factoryId = id;
                    }
                }
            }
            REQUIRE(factoryId.has_value());
            REQUIRE(sim.getUnitState(*factoryId).rotation == SimAngle(0));
        }
    }

    TEST_CASE("a construction aircraft works the ring pattern", "[aircraft][construction]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto airBuilder = makeBuilderDef();
        airBuilder.canFly = true;
        airBuilder.cruiseAltitude = 60_ss;
        airBuilder.maxVelocity = 5_ss;
        sim.unitDefinitions["AIRBUILDER"] = airBuilder;
        sim.unitDefinitions["plane"] = airBuilder;
        sim.unitDefinitions["STRUCTURE"] = makeStructureDef(false);
        sim.unitScriptDefinitions["STRUCTURE"] = *script;
        registerModel(sim);

        auto structurePosition = SimVector(0_ss, 0_ss, 0_ss);
        auto planeId = spawnPlane(sim, player, SimVector(-150_ss, 60_ss, 0_ss), script);
        sim.getUnitState(planeId).orders.push_back(BuildOrder("STRUCTURE", structurePosition));

        // The build stance never comes (the test script has no StartBuilding),
        // so the aircraft flies the pattern indefinitely: ideal for watching it.
        bool sawCentreStage = false;
        std::vector<int> stationsVisited;
        bool ringDistanceOk = true;
        float minRing = 1000.0f;
        float maxRing = 0.0f;
        auto steepestTransitRoll = 0_ss;
        auto steepestStationRoll = 0_ss;
        bool rolledLeft = false;
        bool rolledRight = false;
        for (int i = 0; i < 1500; ++i)
        {
            sim.tick();
            const auto& plane = sim.getUnitState(planeId);
            if (std::getenv("RWE_TRACE_ORBIT") && i % 15 == 0)
            {
                std::string o = plane.airWorkOrbit ? (std::to_string(plane.airWorkOrbit->pointIndex) + (plane.airWorkOrbit->onStation ? "+" : "-")) : "none";
                std::cout << "t=" << i << " pos=" << simScalarToFloat(plane.position.x) << "," << simScalarToFloat(plane.position.y) << "," << simScalarToFloat(plane.position.z) << " orbit=" << o << std::endl;
            }
            if (!plane.airWorkOrbit)
            {
                continue;
            }
            const auto& orbit = *plane.airWorkOrbit;
            auto roll = std::get<UnitPhysicsInfoAir>(plane.physics).roll;
            if (orbit.pointIndex >= 0)
            {
                if (orbit.onStation)
                {
                    // Give the bank a moment to level out after arriving.
                    if (sim.gameTime >= orbit.stationReachedAt + GameTime(30))
                    {
                        steepestStationRoll = rweMax(steepestStationRoll, rweAbs(roll));
                    }
                }
                else
                {
                    steepestTransitRoll = rweMax(steepestTransitRoll, rweAbs(roll));
                    rolledLeft = rolledLeft || roll < SimScalar(-0.04f);
                    rolledRight = rolledRight || roll > SimScalar(0.04f);
                }
            }
            if (orbit.pointIndex < 0 && orbit.onStation)
            {
                sawCentreStage = true;
            }
            if (orbit.pointIndex >= 0 && orbit.onStation)
            {
                if (stationsVisited.empty() || stationsVisited.back() != orbit.pointIndex)
                {
                    stationsVisited.push_back(orbit.pointIndex);
                }
                SimVector flat(plane.position.x - orbit.workPosition.x, 0_ss, plane.position.z - orbit.workPosition.z);
                auto distance = flat.length();
                // The 4x4 structure's ring radius is clamped to the 40-unit floor.
                minRing = std::min(minRing, simScalarToFloat(distance));
                maxRing = std::max(maxRing, simScalarToFloat(distance));
                if (distance < 15_ss || distance > 80_ss)
                {
                    ringDistanceOk = false;
                }
            }
        }

        REQUIRE(sawCentreStage);
        REQUIRE(stationsVisited.size() >= 2);
        // It banks on the way between stations and sits level once parked.
        REQUIRE(steepestTransitRoll > SimScalar(0.07f));
        REQUIRE(steepestStationRoll < SimScalar(0.06f));
        // The bank goes both ways over a hop: over into the move, back the
        // other way as it settles onto the next station.
        REQUIRE((rolledLeft && rolledRight));
        // After the random first station, movement is one step clockwise at a time.
        for (std::size_t i = 1; i < stationsVisited.size(); ++i)
        {
            REQUIRE(stationsVisited[i] == (stationsVisited[i - 1] + 1) % 8);
        }
        CAPTURE(minRing, maxRing);
        REQUIRE(ringDistanceOk);
    }

    TEST_CASE("aircraft look for dry land to set down on", "[aircraft]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeCoastTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["plane"] = makePlaneDef();
        registerModel(sim);

        SECTION("a spot over the sea is refused in favour of the shore")
        {
            // Well out over the water on the east side of the map.
            auto planeId = spawnPlane(sim, player, SimVector(300_ss, 100_ss, 0_ss), script);
            const auto& plane = sim.getUnitState(planeId);
            ConstUnitInfo info(planeId, &plane, &sim.unitDefinitions.at("plane"));

            auto spot = findLandingLocation(sim, info);
            REQUIRE(spot.has_value());
            REQUIRE(sim.terrain.getHeightAt(spot->x, spot->z) >= sim.terrain.getSeaLevel());
        }

        SECTION("a spot over clear dry land is taken as it is")
        {
            auto planeId = spawnPlane(sim, player, SimVector(-300_ss, 100_ss, 0_ss), script);
            const auto& plane = sim.getUnitState(planeId);
            ConstUnitInfo info(planeId, &plane, &sim.unitDefinitions.at("plane"));

            auto spot = findLandingLocation(sim, info);
            REQUIRE(spot.has_value());
            REQUIRE(spot->x == plane.position.x);
            REQUIRE(spot->z == plane.position.z);
        }

        SECTION("an idle aircraft over the sea ends up on the ground, on land")
        {
            auto planeId = spawnPlane(sim, player, SimVector(300_ss, 100_ss, 0_ss), script);
            bool landed = false;
            for (int i = 0; i < 1800 && !landed; ++i)
            {
                sim.tick();
                landed = std::holds_alternative<UnitPhysicsInfoGround>(sim.getUnitState(planeId).physics);
            }
            REQUIRE(landed);
            const auto& plane = sim.getUnitState(planeId);
            REQUIRE(sim.terrain.getHeightAt(plane.position.x, plane.position.z) >= sim.terrain.getSeaLevel());
        }
    }
}
