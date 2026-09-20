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

        /**
         * Never short of anything, so a repair can only be limited by the
         * rules under test. The storage has to come from a *unit* -- the
         * per-second pass rebuilds `maxEnergy` from what is standing and then
         * clamps the stockpile to it -- so the builder below carries it.
         */
        PlayerId addWellStockedPlayer(GameSimulation& sim)
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

        UnitDefinition makeBuilderDef(unsigned int workerTimePerTick)
        {
            UnitDefinition d{};
            d.builder = true;
            // A real construction unit names CanReclamate. The original gates
            // both reclaiming and repairing on it and never looks at
            // workertime (0x489960, and the bit 9 mirror at 0x4899CC).
            d.canReclamate = true;
            d.workerTimePerTick = workerTimePerTick;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.buildDistance = 100_ss;
            // Somewhere for the player's stockpile to live; see
            // addWellStockedPlayer. Repairing costs energy now, and without
            // this the bank is emptied by the storage clamp after one second.
            d.energyStorage = Energy(10000.0f);
            d.metalStorage = Metal(10000.0f);
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

        /** At full health, ready to be damaged on purpose. */
        UnitId addUndamagedUnitOfType(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
            auto unitId = addUndamagedUnitOfType(sim, "builder", owner, pos, script);
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
        auto player = addWellStockedPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUndamagedUnitOfType(sim, "solar", player, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 10;

        auto builderId = addBuilderUnit(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(RepairOrder(solarId));

        // One hit point a tick, whatever the worker time and whatever is
        // being mended: 0x41BD87 caps it at one (§94). The first tick raises
        // the arm and the second is the first that mends, so 10 -> 100 is one
        // deploying tick and ninety repairing ones.
        sim.tick();
        sim.tick();
        REQUIRE(sim.getUnitState(solarId).hitPoints == 11u);
        REQUIRE(std::holds_alternative<UnitBehaviorStateBuilding>(sim.getUnitState(builderId).behaviourState));

        tickUntil(sim, 200, [&]() { return sim.getUnitState(builderId).orders.empty(); });

        REQUIRE(sim.getUnitState(solarId).hitPoints == 100u);
        const auto& builder = sim.getUnitState(builderId);
        REQUIRE(builder.orders.empty());
        REQUIRE(std::holds_alternative<UnitBehaviorStateIdle>(builder.behaviourState));

        // And it is not free: one energy a tick, also capped, out of the
        // repairer's own block (0x41BDB7). Metal is never asked for -- the
        // request is the single-resource 0x401180, not the two-resource
        // 0x4011C0 the build path uses.
        REQUIRE(builder.metalRequestBuffer.value == 0.0f);
        REQUIRE(builder.energyRequestBuffer.value > 0.0f);
    }

    TEST_CASE("the repair rate is one hit point a tick however fast the worker", "[repair]")
    {
        // The clamp at 0x41BD87 is an upper bound, and RWE used to have a
        // lower one, so a builder mended a cheap high-health thing -- a
        // dragon's tooth, 3500 points on a buildtime of 520 -- at forty hit
        // points a tick. Repair scales with the number of repairers, not with
        // their WorkerTime.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim);

        auto wallDef = makeSolarDef();
        wallDef.maxHitPoints = 3500u;
        wallDef.buildTime = 520u;
        sim.unitDefinitions["wall"] = wallDef;

        auto wallPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto wallId = addUndamagedUnitOfType(sim, "wall", player, wallPosition, script);
        sim.getUnitState(wallId).hitPoints = 1000u;

        // WorkerTime 300 in the FBI, the fastest anything in the shipped data
        // has; ten units of work a tick.
        sim.unitDefinitions["builder"] = makeBuilderDef(10u);
        auto builderId = addUndamagedUnitOfType(sim, "builder", player, wallPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).inBuildStance = true;
        sim.getUnitState(builderId).orders.push_back(RepairOrder(wallId));

        for (int i = 0; i < 11; ++i)
        {
            sim.tick();
        }

        // One deploying tick and ten mending ones.
        REQUIRE(sim.getUnitState(wallId).hitPoints == 1010u);
    }

    TEST_CASE("a builder in energy debt repairs nothing", "[repair]")
    {
        // 0x41BDB7 asks and 0x41BDBC does nothing at all if it is refused, so
        // a stalled player's builders stand with their arms up and mend
        // nothing. The request is still booked, which is what puts it in the
        // resource bars.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUndamagedUnitOfType(sim, "solar", player, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 10;

        auto builderId = addBuilderUnit(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(RepairOrder(solarId));
        sim.getUnitState(builderId).energyDebt = Energy(100.0f);

        for (int i = 0; i < 20; ++i)
        {
            sim.tick();
        }

        REQUIRE(sim.getUnitState(solarId).hitPoints == 10u);
        REQUIRE(sim.getUnitState(builderId).energyRequestBuffer.value == 0.0f);

        // Metal debt on its own does not stop it, because the request only
        // ever consults the energy side.
        sim.getUnitState(builderId).energyDebt = Energy(0.0f);
        sim.getUnitState(builderId).metalDebt = Metal(100.0f);
        sim.tick();
        REQUIRE(sim.getUnitState(solarId).hitPoints == 11u);
    }

    TEST_CASE("a repair order on an unfinished unit completes its construction", "[repair]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUndamagedUnitOfType(sim, "solar", player, solarPosition, script);
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
        auto player = addWellStockedPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarId = addUndamagedUnitOfType(sim, "solar", player, SimVector(200_ss, 0_ss, 200_ss), script);
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
        auto player = addWellStockedPlayer(sim);
        auto builderId = addBuilderUnit(sim, player, SimVector(100_ss, 0_ss, 100_ss), script);
        sim.getUnitState(builderId).hitPoints = 5;
        sim.getUnitState(builderId).orders.push_back(RepairOrder(builderId));

        sim.tick();

        REQUIRE(sim.getUnitState(builderId).orders.empty());
        REQUIRE(sim.getUnitState(builderId).hitPoints == 5u);
    }

    TEST_CASE("a factory cannot repair either, because one key gates both", "[repair]")
    {
        // The FBI parser mirrors CanReclamate into bit 9 of the same word
        // (0x42CA3B-0x42CA4D) and CanRepair at 0x4899CC tests that mirror,
        // so a unit that may not reclaim may not repair. A factory has a
        // worker time only so that it can build.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim);

        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarId = addUndamagedUnitOfType(sim, "solar", player, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.getUnitState(solarId).hitPoints = 10u;

        auto factoryDef = makeBuilderDef(30u);
        factoryDef.canReclamate = false;
        sim.unitDefinitions["factory"] = factoryDef;
        auto factoryId = addUndamagedUnitOfType(sim, "factory", player, SimVector(240_ss, 0_ss, 200_ss), script);
        sim.getUnitState(factoryId).inBuildStance = true;
        sim.getUnitState(factoryId).orders.push_back(RepairOrder(solarId));

        for (int i = 0; i < 20; ++i)
        {
            sim.tick();
        }

        REQUIRE(sim.getUnitState(solarId).hitPoints == 10u);
        REQUIRE(sim.getUnitState(factoryId).orders.empty());
    }

    TEST_CASE("a unit with a healtime mends itself in eight-tick steps", "[repair]")
    {
        // 0x48AF3D runs only on ticks that are a multiple of eight and then
        // adds healtime * 8 / 30, truncated. The commanders' shipped 27 gives
        // 7 points a step, 26.25 a second.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim);

        auto commanderDef = makeSolarDef();
        commanderDef.healTime = 27u;
        commanderDef.maxHitPoints = 100u;
        sim.unitDefinitions["commander"] = commanderDef;
        auto commanderId = addUndamagedUnitOfType(sim, "commander", player, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.getUnitState(commanderId).hitPoints = 10u;

        // Eight ticks buys exactly one step of seven.
        for (int i = 0; i < 8; ++i)
        {
            sim.tick();
        }
        REQUIRE(sim.getUnitState(commanderId).hitPoints == 17u);

        for (int i = 0; i < 8; ++i)
        {
            sim.tick();
        }
        REQUIRE(sim.getUnitState(commanderId).hitPoints == 24u);

        // It stops at full health rather than running over.
        for (int i = 0; i < 400; ++i)
        {
            sim.tick();
        }
        REQUIRE(sim.getUnitState(commanderId).hitPoints == 100u);
    }

    TEST_CASE("a unit without a healtime never mends itself", "[repair]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim);

        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarId = addUndamagedUnitOfType(sim, "solar", player, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.getUnitState(solarId).hitPoints = 10u;

        for (int i = 0; i < 100; ++i)
        {
            sim.tick();
        }

        REQUIRE(sim.getUnitState(solarId).hitPoints == 10u);
    }
    TEST_CASE("a repairer stops mending a unit that has moved out of its reach", "[repair]")
    {
        // Reported from a replay: nanolathe spraying across the map. Reach is
        // tested before the arm goes up and was never tested again, so a
        // target that walked away was mended from wherever it had got to.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUndamagedUnitOfType(sim, "solar", player, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 10;

        auto builderId = addBuilderUnit(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(RepairOrder(solarId));

        sim.tick();
        sim.tick();
        REQUIRE(sim.getUnitState(solarId).hitPoints == 11u);

        // Out of reach, as a damaged unit ordered home would be.
        sim.getUnitState(solarId).position = solarPosition + SimVector(1200_ss, 0_ss, 0_ss);
        auto before = sim.getUnitState(solarId).hitPoints;
        for (int i = 0; i < 20; ++i)
        {
            sim.tick();
        }
        CHECK(sim.getUnitState(solarId).hitPoints == before);
        CHECK_FALSE(std::holds_alternative<UnitBehaviorStateBuilding>(sim.getUnitState(builderId).behaviourState));
    }
}
