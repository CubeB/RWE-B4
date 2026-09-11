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
#include <string>
#include <variant>
#include <vector>

/**
 * The three unit voices that were never played -- `underattack`, `repair`
 * and `cant` -- and the simulation events the scene plays them off. What
 * triggers each was read out of the callers of 0x47F780, the routine that
 * plays a sound slot: see TOTALA-EXE.md §97.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        UnitDefinition makeBuilderDef(unsigned int workerTimePerTick)
        {
            UnitDefinition d{};
            d.builder = true;
            d.canReclamate = true;
            d.workerTimePerTick = workerTimePerTick;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.buildDistance = 100_ss;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** ARMSOLAR.FBI, as the capture and reclaim tests model it. */
        UnitDefinition makeSolarDef()
        {
            UnitDefinition d{};
            d.maxHitPoints = 326;
            d.buildCostEnergy = Energy(760);
            d.buildCostMetal = Metal(145);
            d.buildTime = 2495u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        template <typename Event>
        int countEvents(const GameSimulation& sim)
        {
            int n = 0;
            for (const auto& e : sim.events)
            {
                if (std::holds_alternative<Event>(e))
                {
                    ++n;
                }
            }
            return n;
        }

        template <typename Event>
        std::vector<Event> eventsOf(const GameSimulation& sim)
        {
            std::vector<Event> out;
            for (const auto& e : sim.events)
            {
                if (auto p = std::get_if<Event>(&e))
                {
                    out.push_back(*p);
                }
            }
            return out;
        }
    }

    TEST_CASE("a damaged unit's event says whether the hit was a paralyser", "[unitnotifications]")
    {
        // The under-attack voice turns on the original's cause byte: a
        // weapon hit from your own side still warns, a paralyser from your
        // own side does not. The scene decides; the event carries the fact.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarId = addUnitOfType(sim, "solar", player, SimVector(100_ss, 0_ss, 100_ss), script);

        sim.applyDamage(solarId, 10u, std::nullopt, false);
        sim.applyDamage(solarId, 10u, std::nullopt, true);

        auto damaged = eventsOf<UnitDamagedEvent>(sim);
        REQUIRE(damaged.size() == 2);
        REQUIRE(damaged[0].unitId == solarId);
        REQUIRE_FALSE(damaged[0].paralyzer);
        REQUIRE(damaged[1].paralyzer);
    }

    TEST_CASE("a repairer says so once when the job is done", "[unitnotifications]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();
        sim.unitDefinitions["builder"] = makeBuilderDef(30u);

        auto solarId = addUnitOfType(sim, "solar", player, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.getUnitState(solarId).hitPoints = 300u;
        auto builderId = addUnitOfType(sim, "builder", player, SimVector(240_ss, 0_ss, 200_ss), script);
        sim.getUnitState(builderId).inBuildStance = true;
        sim.getUnitState(builderId).orders.push_back(RepairOrder(solarId));

        // Nothing until the target is whole.
        sim.tick();
        sim.events.clear();
        REQUIRE(sim.getUnitState(solarId).hitPoints < 326u);

        int ticks = 0;
        while (ticks < 100 && !sim.getUnitState(builderId).orders.empty())
        {
            sim.tick();
            ++ticks;
        }
        REQUIRE(sim.getUnitState(solarId).hitPoints == 326u);
        REQUIRE(sim.getUnitState(builderId).orders.empty());

        auto repaired = eventsOf<UnitRepairedEvent>(sim);
        REQUIRE(repaired.size() == 1);
        // The voice is the repairer's, not the mended unit's.
        REQUIRE(repaired[0].unitId == builderId);
    }

    TEST_CASE("cannot comply: a nanoframe is a cloud of vapor", "[unitnotifications]")
    {
        // 0x40432E, with 0x5015E0.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();
        auto captorDef = makeBuilderDef(10u);
        captorDef.canCapture = true;
        sim.unitDefinitions["captor"] = captorDef;

        auto solarId = addUnitOfType(sim, "solar", enemy, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.getUnitState(solarId).buildTimeCompleted = 1u;
        auto captorId = addUnitOfType(sim, "captor", player, SimVector(240_ss, 0_ss, 200_ss), script);
        sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));

        sim.tick();

        REQUIRE(sim.getUnitState(captorId).orders.empty());
        auto refused = eventsOf<UnitCannotComplyEvent>(sim);
        REQUIRE(refused.size() == 1);
        REQUIRE(refused[0].unitId == captorId);
        REQUIRE(refused[0].message == "That unit is a cloud of vapor and cannot be captured");
    }

    TEST_CASE("cannot comply: a commander cannot be reclaimed", "[unitnotifications]")
    {
        // 0x4047A6, with 0x50164C.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto commanderDef = makeBuilderDef(30u);
        commanderDef.canCapture = true;
        sim.unitDefinitions["commander"] = commanderDef;
        sim.unitDefinitions["builder"] = makeBuilderDef(30u);
        auto commanderId = addUnitOfType(sim, "commander", player, SimVector(200_ss, 0_ss, 200_ss), script);
        auto builderId = addUnitOfType(sim, "builder", player, SimVector(240_ss, 0_ss, 200_ss), script);
        sim.getUnitState(builderId).inBuildStance = true;
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(commanderId));

        sim.tick();

        REQUIRE(sim.getUnitState(builderId).orders.empty());
        auto refused = eventsOf<UnitCannotComplyEvent>(sim);
        REQUIRE(refused.size() == 1);
        REQUIRE(refused[0].unitId == builderId);
        REQUIRE(refused[0].message == "That unit cannot be reclaimed");
    }

    TEST_CASE("a refusal that ends an order says so exactly once", "[unitnotifications]")
    {
        // The order goes with the message, so ticking on is silent.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();
        auto captorDef = makeBuilderDef(10u);
        captorDef.canCapture = true;
        sim.unitDefinitions["captor"] = captorDef;
        auto solarId = addUnitOfType(sim, "solar", enemy, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.getUnitState(solarId).buildTimeCompleted = 1u;
        auto captorId = addUnitOfType(sim, "captor", player, SimVector(240_ss, 0_ss, 200_ss), script);
        sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));

        for (int i = 0; i < 5; ++i)
        {
            sim.tick();
        }
        REQUIRE(countEvents<UnitCannotComplyEvent>(sim) == 1);
    }
}
