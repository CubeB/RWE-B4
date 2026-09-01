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

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("repairer"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(10000.0f),
                Energy(10000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
        }

        UnitDefinition makeBuilderDef(unsigned int workerTimePerTick)
        {
            UnitDefinition d{};
            d.builder = true;
            d.workerTimePerTick = workerTimePerTick;
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
            d.buildCostMetal = Metal(100.0f);
            d.buildCostEnergy = Energy(50.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId addUnitOfType(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.buildTimeCompleted = sim.unitDefinitions.at(unitType).buildTime;
            return unitId;
        }

        UnitId addBuilderUnit(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions["builder"] = makeBuilderDef(30u);
            auto unitId = addUnitOfType(sim, "builder", owner, pos, script);
            // Normally set by the COB script's StartBuilding thread; the
            // empty test script has none, so pretend the arm is deployed.
            sim.getUnitState(unitId).inBuildStance = true;
            return unitId;
        }

        void tickUntil(GameSimulation& sim, int maxTicks, const std::function<bool()>& done)
        {
            for (int i = 0; i < maxTicks && !done(); ++i)
            {
                sim.tick();
            }
        }
    }

    TEST_CASE("a builder with a repair order restores a damaged unit to full health", "[repair]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", player, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 10;

        auto builderId = addBuilderUnit(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(RepairOrder(solarId));

        // Health per tick = maxHitPoints * workerTimePerTick / buildTime = 100 * 30 / 150 = 20,
        // so 10 -> 100 takes five repairing ticks plus one to deploy the arm.
        sim.tick();
        sim.tick();
        REQUIRE(sim.getUnitState(solarId).hitPoints == 30u);
        REQUIRE(std::holds_alternative<UnitBehaviorStateBuilding>(sim.getUnitState(builderId).behaviourState));

        tickUntil(sim, 20, [&]() { return sim.getUnitState(builderId).orders.empty(); });

        REQUIRE(sim.getUnitState(solarId).hitPoints == 100u);
        const auto& builder = sim.getUnitState(builderId);
        REQUIRE(builder.orders.empty());
        REQUIRE(std::holds_alternative<UnitBehaviorStateIdle>(builder.behaviourState));

        // Repairing is free.
        REQUIRE(builder.metalRequestBuffer.value == 0.0f);
        REQUIRE(builder.energyRequestBuffer.value == 0.0f);
    }

    TEST_CASE("a repair order on an unfinished unit completes its construction", "[repair]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", player, solarPosition, script);
        sim.getUnitState(solarId).buildTimeCompleted = 60u;
        sim.getUnitState(solarId).hitPoints = 1;

        auto builderId = addBuilderUnit(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(RepairOrder(solarId));

        tickUntil(sim, 20, [&]() { return sim.getUnitState(builderId).orders.empty(); });

        const auto& solar = sim.getUnitState(solarId);
        REQUIRE_FALSE(solar.isBeingBuilt(sim.unitDefinitions.at("solar")));
        REQUIRE(sim.getUnitState(builderId).orders.empty());
    }

    TEST_CASE("a repair order on a healthy unit completes immediately", "[repair]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarId = addUnitOfType(sim, "solar", player, SimVector(200_ss, 0_ss, 200_ss), script);
        auto builderId = addBuilderUnit(sim, player, SimVector(240_ss, 0_ss, 200_ss), script);
        sim.getUnitState(builderId).orders.push_back(RepairOrder(solarId));

        sim.tick();

        REQUIRE(sim.getUnitState(builderId).orders.empty());
        REQUIRE(std::holds_alternative<UnitBehaviorStateIdle>(sim.getUnitState(builderId).behaviourState));
    }

    TEST_CASE("a unit ignores an order to repair itself", "[repair]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto builderId = addBuilderUnit(sim, player, SimVector(100_ss, 0_ss, 100_ss), script);
        sim.getUnitState(builderId).hitPoints = 5;
        sim.getUnitState(builderId).orders.push_back(RepairOrder(builderId));

        sim.tick();

        REQUIRE(sim.getUnitState(builderId).orders.empty());
        REQUIRE(sim.getUnitState(builderId).hitPoints == 5u);
    }
}
