#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitPieceDefinition.h>
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

        UnitDefinition makeFactoryDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.builder = true;
            d.workerTimePerTick = 10;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(6, 6, YardMapCell::GroundPassable);
            return d;
        }

        UnitDefinition makeMobileDef(unsigned int buildTime)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = 6_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = buildTime;
            d.buildCostMetal = Metal(1.0f);
            d.buildCostEnergy = Energy(1.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        void registerTestModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
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

    TEST_CASE("cannot comply: a blocked build site is waited on, then given up", "[unitnotifications]")
    {
        // The site check is run again when the builder actually comes to put
        // the unit down, and a site that is occupied then is announced rather
        // than silently abandoned: "Waiting for target area to clear" at the
        // front of the run, a quiet retry every thirty ticks, and "Target area
        // was blocked" when ten of them have gone by. Both captions are in the
        // cant table -- 403cdf/414020 and 403d10/414055 -- from the two
        // unit-creation sites that wrap 0x47D2E0 through 0x47DB70.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim);

        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        // Upper case because createUnit upper-cases the type name before
        // tryAddUnit looks it up again: a lower-case key is registered
        // where the spawn path cannot see it.
        auto solarDefinition = makeSolarDef();
        solarDefinition.objectName = "model";
        solarDefinition.yardMap = Grid<YardMapCell>(2, 2, YardMapCell::Ground);
        sim.unitDefinitions["SOLAR"] = solarDefinition;
        sim.unitScriptDefinitions["SOLAR"] = *script;
        sim.unitDefinitions["builder"] = makeBuilderDef(30u);

        // Something is already standing exactly where the solar is to go, and
        // it has to be spawned the real way: trySpawnUnit stamps the occupancy
        // grid, where the shared helper only puts a unit in the list. A blocker
        // that occupies nothing leaves the site free and the build quietly
        // succeeds, which is what this test used to measure.
        SimVector site(200_ss, 0_ss, 200_ss);
        auto blockerId = sim.trySpawnUnit("SOLAR", player, site, std::nullopt).value();

        // Finished, so it cannot rot away as an abandoned nanoframe part-way
        // through the run and clear the site behind our backs.
        sim.getUnitState(blockerId).buildTimeCompleted = solarDefinition.buildTime;

        // The builder stands exactly where the engine reckons it has arrived,
        // rather than at a distance worked out by hand: createNewUnit asks for
        // nothing at all until navigateTo says it is there, so a builder parked
        // short of the site never attempts the spawn and never refuses.
        auto siteRect = sim.computeFootprintRegion(site, solarDefinition.movementCollisionInfo);
        auto builderFootprint = sim.getFootprintXZ(sim.unitDefinitions.at("builder").movementCollisionInfo);
        auto standPoint = findClosestPointToFootprintXZForUnit(
            sim.terrain,
            siteRect,
            SimVector(1000_ss, 0_ss, 200_ss),
            static_cast<int>(builderFootprint.first),
            static_cast<int>(builderFootprint.second));

        auto builderId = addUnitOfType(sim, "builder", player, standPoint, script);
        sim.getUnitState(builderId).inBuildStance = true;
        sim.getUnitState(builderId).orders.push_back(BuildOrder("SOLAR", site));

        sim.tick();

        auto refused = eventsOf<UnitCannotComplyEvent>(sim);
        REQUIRE(refused.size() == 1);
        REQUIRE(refused[0].unitId == builderId);
        REQUIRE(refused[0].message == "Waiting for target area to clear");

        // While it waits it keeps the order and says nothing further.
        tick(sim, 100);
        REQUIRE_FALSE(sim.getUnitState(builderId).orders.empty());
        REQUIRE(countEvents<UnitCannotComplyEvent>(sim) == 1);

        // Ten tries at thirty ticks apiece, and it gives the order up.
        tick(sim, 250);
        auto all = eventsOf<UnitCannotComplyEvent>(sim);
        REQUIRE(all.size() == 2);
        REQUIRE(all[1].unitId == builderId);
        REQUIRE(all[1].message == "Target area was blocked");
        REQUIRE(sim.getUnitState(builderId).orders.empty());
    }

    TEST_CASE("a factory tells a unit on its pad to move, and production continues", "[unitnotifications]")
    {
        // The original's BUGGER_OFF is write-only -- the only reader of bit 3
        // of unit+0x10f is the COB `get` (0x480A96) -- and its site check
        // (0x47DB70) only waits, so a friendly unit parked on a spawn point
        // costs it ten tries and its queue entry. RWE tells the blocker to
        // leave instead, which is a deliberate divergence (TOTALA-EXE.md 112).
        // The sweep is over the site the yard is trying to use, not the yard's
        // own footprint: a hull standing at the pad is outside the building's
        // cells and would never hear about it otherwise.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim, "CORE");
        registerTestModel(sim);

        sim.unitDefinitions["FACT"] = makeFactoryDef();
        sim.unitScriptDefinitions["FACT"] = *script;
        sim.unitDefinitions["TANK"] = makeMobileDef(100u);
        sim.unitScriptDefinitions["TANK"] = *script;
        sim.unitDefinitions["BLOCKER"] = makeMobileDef(0u);
        sim.unitScriptDefinitions["BLOCKER"] = *script;

        auto pad = SimVector(600_ss, 0_ss, 600_ss);
        auto factoryId = addUnitOfType(sim, "FACT", player, pad, script);
        sim.getUnitState(factoryId).inBuildStance = true;
        sim.getUnitState(factoryId).buildQueue.emplace_back("TANK", 1);

        // The yard starts -- one tick takes it from Idle to Building -- and
        // only then does a friendly unit park exactly on its pad. Spawned the
        // real way, so it stamps the occupancy grid the site check reads.
        sim.tick();
        auto blockerId = sim.trySpawnUnit("BLOCKER", player, pad, std::nullopt).value();

        // The yard asks for the frame, finds the pad occupied, and tells the
        // blocker to move off it. That order is the whole of the fix.
        bool told = false;
        for (int i = 0; i < 10 && !told; ++i)
        {
            sim.tick();
            for (const auto& order : sim.getUnitState(blockerId).orders)
            {
                told = told || std::holds_alternative<BuggerOffOrder>(order);
            }
        }
        REQUIRE(told);

        // And it walks off: the frame goes down, without the yard ever giving
        // the queue entry up.
        auto productExists = [&]() {
            for (const auto& entry : sim.units)
            {
                if (entry.second.unitType == "TANK")
                {
                    return true;
                }
            }
            return false;
        };

        bool produced = false;
        for (int i = 0; i < 600 && !produced; ++i)
        {
            sim.tick();
            produced = productExists();
        }
        REQUIRE(produced);

        for (const auto& e : eventsOf<UnitCannotComplyEvent>(sim))
        {
            REQUIRE(e.message != "Target area was blocked");
        }
    }
}
