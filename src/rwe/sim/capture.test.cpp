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
         * ARMCOM.FBI. The commander is one of only two units in the shipped
         * data that name `CanCapture`, so it is the only honest captor to
         * test with.
         */
        UnitDefinition makeCommanderDef(bool canCapture)
        {
            UnitDefinition d{};
            d.builder = true;
            d.canCapture = canCapture;
            d.workerTimePerTick = 300u / 30u;
            d.maxHitPoints = 3000;
            d.buildCostEnergy = Energy(34125);
            d.buildCostMetal = Metal(29854);
            d.buildTime = 90000u;
            d.buildDistance = 60_ss;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** ARMSOLAR.FBI. */
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

        /**
         * The number the original arrives at for a healthy ARMSOLAR with no
         * kills: trunc(760*0.015 + 145*3/14 + 150) = trunc(192.47).
         */
        constexpr unsigned int SolarCaptureTicks = 192;

        UnitId addCaptor(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script, bool canCapture = true)
        {
            sim.unitDefinitions["captor"] = makeCommanderDef(canCapture);
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

        CaptureOrder& frontCaptureOrder(GameSimulation& sim, UnitId unitId)
        {
            return std::get<CaptureOrder>(sim.getUnitState(unitId).orders.front());
        }
    }

    TEST_CASE("GameSimulation::computeCaptureTime", "[capture]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarId = addUnitOfType(sim, "solar", enemy, SimVector(100_ss, 0_ss, 100_ss), script);
        auto& solar = sim.getUnitState(solarId);
        // The shared fixture hands out a hundred hit points whatever the
        // definition says; capture time is scaled by the damage, so these
        // want a whole one.
        solar.hitPoints = 326;

        SECTION("is the build cost, undamaged and unblooded")
        {
            REQUIRE(sim.computeCaptureTime(solar) == SolarCaptureTicks);
        }

        SECTION("does not depend on the captor at all")
        {
            // No overload takes one: `workertime` appears nowhere in the
            // original's Capture mission, so a commander and a construction
            // kbot would take exactly as long as each other.
            REQUIRE(sim.computeCaptureTime(solar) == SolarCaptureTicks);
        }

        SECTION("halves as the target approaches death")
        {
            solar.hitPoints = 0;
            REQUIRE(sim.computeCaptureTime(solar) == SolarCaptureTicks / 2);

            solar.hitPoints = 163;
            REQUIRE(sim.computeCaptureTime(solar) == (163u + 326u) * SolarCaptureTicks / (2u * 326u));
        }

        SECTION("grows by a tenth for every five kills the target has")
        {
            solar.kills = 4;
            REQUIRE(sim.computeCaptureTime(solar) == SolarCaptureTicks);

            solar.kills = 5;
            REQUIRE(sim.computeCaptureTime(solar) == (11u * SolarCaptureTicks) / 10u);

            // Unlike the damage tiers, which stop at five, this one does not.
            solar.kills = 50;
            REQUIRE(sim.computeCaptureTime(solar) == (20u * SolarCaptureTicks) / 10u);
        }

        SECTION("is capped at sixty seconds before the scaling")
        {
            // ARMCOM: raw cost gives 7059 ticks, clamped to 1800.
            sim.unitDefinitions["commander"] = makeCommanderDef(true);
            auto comId = addUnitOfType(sim, "commander", enemy, SimVector(300_ss, 0_ss, 300_ss), script);
            sim.getUnitState(comId).hitPoints = 3000;
            REQUIRE(sim.computeCaptureTime(sim.getUnitState(comId)) == 1800u);
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

        SECTION("changes owner and wipes the unit's orders")
        {
            REQUIRE(sim.captureUnit(solarId, player));
            REQUIRE(solar.isOwnedBy(player));
            REQUIRE(solar.orders.empty());
            REQUIRE(countEvents(sim) == 1);
        }

        SECTION("is a no-op on a unit the captor already owns")
        {
            solar.owner = player;
            REQUIRE(sim.captureUnit(solarId, player));
            REQUIRE(countEvents(sim) == 0);
        }

        SECTION("reports completion for a dead unit")
        {
            solar.markAsDead();
            REQUIRE(sim.captureUnit(solarId, player));
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

        for (int i = 0; i < 400 && !sim.getUnitState(solarId).isOwnedBy(player); ++i)
        {
            sim.tick();
        }

        REQUIRE(sim.getUnitState(solarId).isOwnedBy(player));
        const auto& captor = sim.getUnitState(captorId);
        REQUIRE(captor.orders.empty());
        REQUIRE(std::holds_alternative<UnitBehaviorStateIdle>(captor.behaviourState));
    }

    TEST_CASE("capture takes the time the target's build cost says, not the captor's worker time", "[capture]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 326;
        auto captorId = addCaptor(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));

        int ticks = 0;
        while (ticks < 400 && !sim.getUnitState(solarId).isOwnedBy(player))
        {
            sim.tick();
            ++ticks;
        }

        // One tick of work a tick, plus the tick spent raising the arm before
        // any work is done.
        REQUIRE(ticks == static_cast<int>(SolarCaptureTicks) + 1);
    }

    TEST_CASE("capture progress goes with the captor, not the target", "[capture]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 326;
        auto captorId = addCaptor(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));

        for (int i = 0; i < 60; ++i)
        {
            sim.tick();
        }

        SECTION("the work is counted on the order")
        {
            const auto& order = frontCaptureOrder(sim, captorId);
            REQUIRE(order.progress > 0u);
            REQUIRE(order.totalWork == SolarCaptureTicks);
            REQUIRE(sim.getUnitState(solarId).isOwnedBy(enemy));
        }

        SECTION("and is lost the moment the captor is given something else to do")
        {
            // The original keeps it on the mission record (mission+0x36) and
            // frees the record with the order, so the next attempt starts from
            // nothing however far the last one got.
            sim.getUnitState(captorId).orders.clear();
            sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));
            REQUIRE(frontCaptureOrder(sim, captorId).progress == 0u);

            for (int i = 0; i < 60; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getUnitState(solarId).isOwnedBy(enemy));
            // Still less than half done, which it would not be if the first
            // sixty ticks had been banked on the solar collector.
            REQUIRE(frontCaptureOrder(sim, captorId).progress < SolarCaptureTicks / 2u);
        }

        SECTION("and does not shrink when the target is damaged mid-capture")
        {
            // 0x404313 runs once, in the mission's state 0. Softening the
            // target up afterwards buys the captor nothing.
            sim.getUnitState(solarId).hitPoints = 1;
            sim.tick();
            REQUIRE(frontCaptureOrder(sim, captorId).totalWork == SolarCaptureTicks);
        }
    }

    TEST_CASE("two captors on one target do not pool their work", "[capture]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 326;
        auto firstId = addCaptor(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        auto secondId = addCaptor(sim, player, solarPosition + SimVector(-40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(firstId).orders.push_back(CaptureOrder(solarId));
        sim.getUnitState(secondId).orders.push_back(CaptureOrder(solarId));

        int ticks = 0;
        while (ticks < 400 && !sim.getUnitState(solarId).isOwnedBy(player))
        {
            sim.tick();
            ++ticks;
        }

        // Two captors take exactly as long as one, because each counts its own
        // progress on its own order.
        REQUIRE(ticks == static_cast<int>(SolarCaptureTicks) + 1);
    }

    TEST_CASE("a nanoframe cannot be captured", "[capture]")
    {
        // 0x404313 requires the target's remaining build fraction to be
        // exactly zero, and refuses otherwise with "That unit is a cloud of
        // vapor and cannot be captured" (0x5015E0).
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();

        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        sim.getUnitState(solarId).buildTimeCompleted = 1u;
        auto captorId = addCaptor(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));

        sim.tick();

        REQUIRE(sim.getUnitState(captorId).orders.empty());
        REQUIRE(sim.getUnitState(solarId).isOwnedBy(enemy));
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
    }
}
