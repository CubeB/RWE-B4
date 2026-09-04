#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        UnitDefinition makeCaptorDef(bool canCapture)
        {
            UnitDefinition d{};
            d.builder = true;
            d.canCapture = canCapture;
            d.workerTimePerTick = 30u;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.buildDistance = 100_ss;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeSolarDef()
        {
            UnitDefinition d{};
            d.maxHitPoints = 100;
            d.buildTime = 150u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId addCaptor(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script, bool canCapture = true)
        {
            sim.unitDefinitions["captor"] = makeCaptorDef(canCapture);
            auto unitId = addUnitOfType(sim, "captor", owner, pos, script);
            sim.getUnitState(unitId).inBuildStance = true;
            return unitId;
        }

        int countEvents(const GameSimulation& sim)
        {
            int n = 0;
            for (const auto& e : sim.events)
            {
                if (std::holds_alternative<UnitCapturedEvent>(e))
                {
                    ++n;
                }
            }
            return n;
        }
    }

    TEST_CASE("GameSimulation::captureUnit", "[capture]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarId = addUnitOfType(sim, "solar", enemy, SimVector(100_ss, 0_ss, 100_ss), script);
        auto& solar = sim.getUnitState(solarId);
        solar.orders.push_back(MoveOrder(SimVector(0_ss, 0_ss, 0_ss)));

        SECTION("changes owner after buildTime worth of work and wipes the unit's orders")
        {
            for (int i = 0; i < 4; ++i)
            {
                REQUIRE_FALSE(sim.captureUnit(solarId, player, 30u));
                REQUIRE(solar.isOwnedBy(enemy));
            }
            REQUIRE(solar.captureProgress == 120u);

            REQUIRE(sim.captureUnit(solarId, player, 30u));
            REQUIRE(solar.isOwnedBy(player));
            REQUIRE(solar.captureProgress == 0u);
            REQUIRE(solar.orders.empty());
            REQUIRE(countEvents(sim) == 1);
        }

        SECTION("is a no-op on a unit the captor already owns")
        {
            solar.owner = player;
            REQUIRE(sim.captureUnit(solarId, player, 30u));
            REQUIRE(solar.captureProgress == 0u);
            REQUIRE(countEvents(sim) == 0);
        }

        SECTION("reports completion for a dead unit")
        {
            solar.markAsDead();
            REQUIRE(sim.captureUnit(solarId, player, 30u));
            REQUIRE(countEvents(sim) == 0);
        }
    }

    TEST_CASE("a capturing unit with a capture order takes over an enemy unit over successive ticks", "[capture]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        auto captorId = addCaptor(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));

        for (int i = 0; i < 20 && !sim.getUnitState(solarId).isOwnedBy(player); ++i)
        {
            sim.tick();
        }

        REQUIRE(sim.getUnitState(solarId).isOwnedBy(player));
        const auto& captor = sim.getUnitState(captorId);
        REQUIRE(captor.orders.empty());
        REQUIRE(std::holds_alternative<UnitBehaviorStateIdle>(captor.behaviourState));
    }

    TEST_CASE("a unit that cannot capture drops capture orders", "[capture]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        auto builderId = addCaptor(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script, /*canCapture*/ false);
        sim.getUnitState(builderId).orders.push_back(CaptureOrder(solarId));

        sim.tick();

        REQUIRE(sim.getUnitState(builderId).orders.empty());
        REQUIRE(sim.getUnitState(solarId).isOwnedBy(enemy));
        REQUIRE(sim.getUnitState(solarId).captureProgress == 0u);
    }
}
