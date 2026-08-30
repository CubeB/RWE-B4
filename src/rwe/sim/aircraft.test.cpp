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
