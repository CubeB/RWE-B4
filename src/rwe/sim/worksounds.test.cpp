#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

/**
 * The two work sounds, and when the simulation says they are due.
 *
 * Sound slot 11 is `working` -- `reclaim1` in every construction unit's
 * category in the shipped data -- and the original plays it once when work
 * actually starts, from all three of Reclaim (0x404C69), ReclaimUnit
 * (0x4048B5) and Capture (0x404568). Slot 16 is `capture`, played when a
 * capture finishes (0x4046cc). See TOTALA-EXE.md §97.
 *
 * The simulation's part of that is one event apiece:
 * UnitStartedReclaimingEvent on the first tick of work, and the
 * UnitCapturedEvent that already existed at the end of a capture. What these
 * pin is the *once*, and the *when* -- the first tick on which the nanolathe
 * does something, not the tick the order was given.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        FeatureDefinition makeWreckDef()
        {
            FeatureDefinition d{};
            d.name = "armsolar_dead";
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 10_ss;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = 100;
            d.energy = 50;
            d.blocking = true;
            return d;
        }

        /** A builder rooted to the spot: it is already where the work is. */
        UnitDefinition makeBuilderDef()
        {
            UnitDefinition d{};
            d.builder = true;
            d.canReclamate = true;
            d.workerTimePerTick = 30u;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.buildDistance = 100_ss;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /**
         * ARMCK.FBI, the Arm construction kbot, for the one test that needs
         * the builder to walk to the job before it can start it.
         */
        UnitDefinition makeMobileBuilderDef()
        {
            UnitDefinition d = makeBuilderDef();
            d.objectName = "armck";
            d.isMobile = true;
            d.canMove = true;
            d.sightDistance = 235u;
            d.buildDistance = 40_ss;
            d.maxVelocity = 0.8_ssf;
            d.acceleration = 0.12_ssf;
            d.brakeRate = 0.24_ssf;
            d.turnRate = 1020_ss;
            return d;
        }

        /** ARMSOLAR.FBI, as the thing to be captured. */
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

        UnitDefinition makeCaptorDef()
        {
            UnitDefinition d = makeBuilderDef();
            d.canCapture = true;
            d.buildDistance = 60_ss;
            return d;
        }

        UnitId addWorker(GameSimulation& sim, const std::string& type, const UnitDefinition& def, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions[type] = def;
            auto unitId = addUnitOfType(sim, type, owner, pos, script);
            // Normally set by the COB script's StartBuilding thread; the
            // empty test script has none, so pretend the arm is deployed.
            sim.getUnitState(unitId).inBuildStance = true;
            return unitId;
        }

        template <typename T>
        int countEvents(const GameSimulation& sim)
        {
            int n = 0;
            for (const auto& e : sim.events)
            {
                if (std::holds_alternative<T>(e))
                {
                    ++n;
                }
            }
            return n;
        }
    }

    TEST_CASE("a builder announces a feature reclaim exactly once, on the tick the work starts", "[reclaim][worksound]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "builder");

        auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
        auto wreckId = sim.addFeature(wreckDef, 20, 20).value();
        auto wreckPosition = sim.getFeature(wreckId).position;

        auto builderId = addWorker(sim, "builder", makeBuilderDef(), player, wreckPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(wreckId));

        // The tick the order is issued only gets the arm out: the state
        // changes to reclaiming and the job proper waits for the next tick.
        sim.tick();
        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 0);

        sim.tick();
        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 1);

        for (int i = 0; i < 60 && sim.tryGetFeature(wreckId); ++i)
        {
            sim.tick();
        }

        REQUIRE_FALSE(sim.tryGetFeature(wreckId).has_value());
        // Once for the job, not once a tick for as long as it ran.
        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 1);
    }

    TEST_CASE("reclaiming a unit announces itself the same way", "[reclaim][worksound]")
    {
        // Reclaim and ReclaimUnit are separate handlers in the original and
        // both play the same slot, so both are worth pinning.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "builder");
        auto enemy = addPlayer(sim, "enemy");

        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarPosition = SimVector(400_ss, 0_ss, 400_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        auto builderId = addWorker(sim, "builder", makeBuilderDef(), player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(solarId));

        for (int i = 0; i < 200 && sim.tryGetUnitState(solarId); ++i)
        {
            sim.tick();
        }

        REQUIRE_FALSE(sim.tryGetUnitState(solarId).has_value());
        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 1);
    }

    TEST_CASE("the announcement waits for the work, not for the order", "[reclaim][worksound]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "builder");

        auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
        auto wreckId = sim.addFeature(wreckDef, 10, 10).value();
        auto wreckPosition = sim.getFeature(wreckId).position;

        // Well outside the reclaim reach, so the kbot has to walk there
        // first. Nothing may be announced while it is on its way.
        auto startPosition = wreckPosition + SimVector(400_ss, 0_ss, 0_ss);
        auto builderId = addWorker(sim, "armck", makeMobileBuilderDef(), player, startPosition, script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(wreckId));

        int ticksWaited = 0;
        while (ticksWaited < 900 && countEvents<UnitStartedReclaimingEvent>(sim) == 0)
        {
            sim.tick();
            ++ticksWaited;
        }

        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 1);
        // It really did have to travel: the announcement is not the order
        // acknowledging itself a tick or two later.
        REQUIRE(ticksWaited > 10);
        bool builderMoved = !(sim.getUnitState(builderId).position == startPosition);
        REQUIRE(builderMoved);
    }

    TEST_CASE("each new job is announced again", "[reclaim][worksound]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "builder");

        auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
        auto firstId = sim.addFeature(wreckDef, 20, 20).value();
        auto secondId = sim.addFeature(wreckDef, 20, 24).value();
        auto firstPosition = sim.getFeature(firstId).position;

        auto builderId = addWorker(sim, "builder", makeBuilderDef(), player, firstPosition + SimVector(40_ss, 0_ss, 20_ss), script);
        auto& builder = sim.getUnitState(builderId);
        builder.orders.push_back(ReclaimOrder(firstId));
        builder.orders.push_back(ReclaimOrder(secondId));

        for (int i = 0; i < 120 && sim.tryGetFeature(secondId); ++i)
        {
            sim.tick();
        }

        REQUIRE_FALSE(sim.tryGetFeature(firstId).has_value());
        REQUIRE_FALSE(sim.tryGetFeature(secondId).has_value());
        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 2);
    }

    TEST_CASE("a capture is announced once at the start and once when it lands", "[capture][worksound]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");

        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarPosition = SimVector(400_ss, 0_ss, 400_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 326;
        auto captorId = addWorker(sim, "captor", makeCaptorDef(), player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));

        // The arm goes out on the first tick and the first tick of progress
        // is the second one, which is where the `working` sound is due.
        sim.tick();
        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 0);
        sim.tick();
        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 1);

        for (int i = 0; i < 400 && !sim.getUnitState(solarId).isOwnedBy(player); ++i)
        {
            sim.tick();
        }

        REQUIRE(sim.getUnitState(solarId).isOwnedBy(player));
        // Slot 11 once for the job, slot 16 once when it landed.
        REQUIRE(countEvents<UnitStartedReclaimingEvent>(sim) == 1);
        REQUIRE(countEvents<UnitCapturedEvent>(sim) == 1);
    }

    TEST_CASE("the capture event names the unit that did it", "[capture][worksound]")
    {
        // Slot 16 is the captor's sound, so the scene needs the captor and
        // not just the player it now belongs to.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");

        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarPosition = SimVector(400_ss, 0_ss, 400_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        sim.getUnitState(solarId).hitPoints = 326;
        auto captorId = addWorker(sim, "captor", makeCaptorDef(), player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(captorId).orders.push_back(CaptureOrder(solarId));

        for (int i = 0; i < 400 && !sim.getUnitState(solarId).isOwnedBy(player); ++i)
        {
            sim.tick();
        }

        std::optional<UnitCapturedEvent> captured;
        for (const auto& e : sim.events)
        {
            if (auto c = std::get_if<UnitCapturedEvent>(&e))
            {
                captured = *c;
            }
        }

        REQUIRE(captured.has_value());
        REQUIRE(captured->unitId == solarId);
        REQUIRE(captured->previousOwner == enemy);
        REQUIRE(captured->newOwner == player);
        REQUIRE(captured->captorUnitId == captorId);
    }

    TEST_CASE("a change of hands with no captor names none", "[capture][worksound]")
    {
        // captureUnit is callable without one, and then there is nobody to
        // play slot 16.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "captor");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarId = addUnitOfType(sim, "solar", enemy, SimVector(400_ss, 0_ss, 400_ss), script);

        REQUIRE(sim.captureUnit(solarId, player));

        std::optional<UnitCapturedEvent> captured;
        for (const auto& e : sim.events)
        {
            if (auto c = std::get_if<UnitCapturedEvent>(&e))
            {
                captured = *c;
            }
        }

        REQUIRE(captured.has_value());
        REQUIRE_FALSE(captured->captorUnitId.has_value());
    }
}
