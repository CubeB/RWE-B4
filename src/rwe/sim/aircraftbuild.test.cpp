#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

/**
 * A construction aircraft does not wait for its build stance, and it lathes
 * twice on the tick it lays its nanoframe down. See docs/TOTALA-EXE.md 107:
 * VTOL_MobileBuild (0x413D80) calls the INBUILDSTANCE wait and discards the
 * answer, and the wait's side effect -- putting event bit 4 in the mission's
 * wake mask -- matches the bit every COB `set` leaves pending on the unit, so
 * the mission service loop runs the lathe state a second time before the tick
 * ends. VTOL_HelpBuild does not consult the stance at all. A factory waits for
 * its stance before it creates anything, and lathes once a tick.
 *
 * This is the behaviour half. The corpus half -- that the arithmetic those
 * increments drive lands on real games' durations -- is buildtime.test.cpp.
 *
 * The numbers are the shipped TA: Escalation ones, read out of the data set the
 * demo corpus was recorded on: units/CORCA.fbi (WorkerTime 60, so two build
 * units a tick), units/CORLAB.fbi (WorkerTime 120, four a tick) and
 * units/CORDRAG.fbi (BuildTime 1130). A fixture that made its own up would
 * prove nothing about either rate.
 */
namespace rwe
{
    namespace
    {
        /** Never short of anything, so a builder is only ever limited by its worker time. */
        PlayerId addWellStockedPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("builder"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("CORE"),
                Metal(10000.0f),
                Energy(10000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
            };
            return sim.addPlayer(p);
        }

        /**
         * Runs a scrap of FBI through the real parser and the real definition
         * mapping, so WorkerTime reaches workerTimePerTick by the path the game
         * uses -- including the integer division by the tick rate, which is
         * half of what is being pinned here.
         */
        UnitDefinition definitionFromFbi(const std::string& unitName, const std::string& keys)
        {
            auto tdf = parseTdfFromString(
                "[UNITINFO]\n{\nUnitName=" + unitName + ";\nObjectname=model;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n");
            auto fbi = parseUnitFbi(tdf);
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(fbi, movementClasses);
        }

        // x-esc unitsE/CORCA.fbi -- the Core construction aircraft, WorkerTime
        // 60, so p = 2 build units a tick.
        const char* CorCaKeys =
            "FootprintX=3;\n"
            "FootprintZ=3;\n"
            "BuildCostEnergy=6106;\n"
            "BuildCostMetal=236;\n"
            "MaxDamage=290;\n"
            "BuildTime=10215;\n"
            "WorkerTime=60;\n"
            "BMcode=1;\n"
            "Builder=1;\n"
            "canmove=1;\n"
            "Canfly=1;\n"
            "MaxVelocity=7;\n"
            "BrakeRate=1.7;\n"
            "Acceleration=0.06;\n"
            "TurnRate=112;\n"
            "cruisealt=210;\n"
            "Builddistance=210;\n"
            "CanReclamate=1;\n"
            "SightDistance=288;\n";

        // x-esc unitsE/CORLAB.fbi -- a kbot lab, WorkerTime 120, so p = 4. It
        // is here as the contrast: a factory waits for its stance.
        const char* CorLabKeys =
            "FootprintX=6;\n"
            "FootprintZ=6;\n"
            "BuildCostEnergy=1760;\n"
            "BuildCostMetal=600;\n"
            "MaxDamage=2550;\n"
            "BuildTime=6700;\n"
            "WorkerTime=120;\n"
            "BMcode=0;\n"
            "Builder=1;\n"
            "Builddistance=128;\n"
            "SightDistance=273;\n"
            "YardMap=oooooooooooooooooooooooooooooooooooo;\n";

        // x-esc unitsE/CORDRAG.fbi -- dragon's teeth. BuildTime 1130 divides
        // exactly by CORCA's rate, which is what makes it the pair section 110
        // works through.
        const char* CorDragKeys =
            "FootprintX=2;\n"
            "FootprintZ=2;\n"
            "BuildCostEnergy=600;\n"
            "BuildCostMetal=32;\n"
            "MaxDamage=2000;\n"
            "BuildTime=1130;\n"
            "WorkerTime=0;\n"
            "BMcode=0;\n"
            "SightDistance=128;\n"
            "YardMap=f;\n";

        // x-esc unitsE/CORRAD.fbi -- a radar tower. BuildTime 1137 does NOT
        // divide by CORCA's rate, which is what makes it the pair to measure a
        // whole job with: RWE's integer accumulator and the original's float32
        // fraction need the same number of increments, so the duration can be
        // held against the corpus's own without section 88's tick in the way.
        const char* CorRadKeys =
            "FootprintX=2;\n"
            "FootprintZ=2;\n"
            "BuildCostEnergy=800;\n"
            "BuildCostMetal=50;\n"
            "MaxDamage=51;\n"
            "BuildTime=1137;\n"
            "WorkerTime=0;\n"
            "BMcode=0;\n"
            "SightDistance=224;\n"
            "RadarDistance=1792;\n"
            "YardMap=oooo;\n";

        // x-esc unitsE/CORCOM.fbi, cut to the keys that matter here. A
        // commander is the player's storage: GameSimulation::updateResources
        // rebuilds every player's capacity out of its live units each settle,
        // and a commander contributes the player's starting stock. Without one
        // the capacity is zero, the settle clamps the stockpile away and the
        // builder spends the rest of the game in debt -- which is the economy
        // behaving correctly and would have nothing to do with build timing.
        const char* CorComKeys =
            "FootprintX=2;\n"
            "FootprintZ=2;\n"
            "BuildCostEnergy=18128;\n"
            "BuildCostMetal=881;\n"
            "MaxDamage=5000;\n"
            "BuildTime=26941;\n"
            "WorkerTime=360;\n"
            "BMcode=1;\n"
            "Builder=1;\n"
            "canmove=1;\n"
            "MaxVelocity=1.05;\n"
            "Builddistance=120;\n"
            "EnergyMake=50;\n"
            "MetalMake=2;\n"
            "Commander=1;\n"
            "SightDistance=320;\n";

        void registerTestModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        void defineUnit(GameSimulation& sim, const std::string& name, const char* keys, const CobScript& script)
        {
            sim.unitDefinitions[name] = definitionFromFbi(name, keys);
            sim.unitScriptDefinitions[name] = script;
        }

        /**
         * A builder placed by hand, so it does not have to fit the occupied
         * grid, and airborne if it flies -- which is where a construction
         * aircraft spends its whole working life.
         */
        UnitId addBuilder(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto unitId = addUnitOfType(sim, unitType, owner, pos, script);
            auto& unit = sim.getUnitState(unitId);
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            if (sim.unitDefinitions.at(unitType).canFly)
            {
                unit.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
                sim.flyingUnitsSet.insert(unitId);
            }
            return unitId;
        }

        /** The one nanoframe in the world, if there is one yet. */
        std::optional<UnitId> findNanoframe(const GameSimulation& sim, const std::string& unitType)
        {
            for (const auto& entry : sim.units)
            {
                if (entry.second.unitType == unitType)
                {
                    return UnitId(entry.first);
                }
            }
            return std::nullopt;
        }

        struct FirstWork
        {
            /** The tick, counted from the order, on which the frame first gained anything. */
            int tick;

            /** How much it gained on that tick. */
            unsigned int progress;

            /** Whether the builder was ever in its build stance along the way. */
            bool everInStance;
        };

        FirstWork runUntilFirstIncrement(GameSimulation& sim, UnitId builderId, const std::string& productType, int budget)
        {
            bool everInStance = false;
            for (int i = 1; i <= budget; ++i)
            {
                sim.tick();
                everInStance = everInStance || sim.getUnitState(builderId).inBuildStance;
                if (auto frameId = findNanoframe(sim, productType))
                {
                    auto progress = sim.getUnitState(*frameId).buildTimeCompleted;
                    if (progress > 0u)
                    {
                        return FirstWork{i, progress, everInStance};
                    }
                }
            }
            return FirstWork{0, 0u, everInStance};
        }
    }

    TEST_CASE("a construction aircraft builds without waiting for its build stance", "[aircraftbuild]")
    {
        // The script here has no StartBuilding at all, so INBUILDSTANCE is
        // never set: if the aircraft were gated on it, as every RWE builder
        // used to be, nothing would ever happen. The original never gates one.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "CORCA", CorCaKeys, *script);
        defineUnit(sim, "CORDRAG", CorDragKeys, *script);
        defineUnit(sim, "CORCOM", CorComKeys, *script);
        auto player = addWellStockedPlayer(sim);
        addBuilder(sim, "CORCOM", player, SimVector(200_ss, 0_ss, 200_ss), script);

        auto site = SimVector(600_ss, 0_ss, 600_ss);
        auto builderId = addBuilder(sim, "CORCA", player, SimVector(600_ss, 0_ss, 560_ss), script);
        sim.getUnitState(builderId).orders.push_back(BuildOrder("CORDRAG", site));

        auto work = runUntilFirstIncrement(sim, builderId, "CORDRAG", 60);

        REQUIRE(work.tick > 0);
        REQUIRE_FALSE(work.everInStance);

        // Two increments on the tick the builder first has a frame to lathe,
        // which is CORCA's rate twice over: WorkerTime 60 / 30 = 2.
        REQUIRE(work.progress == 4u);

        // And one a tick from there on.
        auto frameRef = findNanoframe(sim, "CORDRAG");
        REQUIRE(frameRef.has_value());
        auto frameId = *frameRef;
        sim.tick();
        REQUIRE(sim.getUnitState(frameId).buildTimeCompleted == 6u);
        sim.tick();
        REQUIRE(sim.getUnitState(frameId).buildTimeCompleted == 8u);
    }

    TEST_CASE("a construction aircraft's second lathe is worth a whole tick of the job", "[aircraftbuild]")
    {
        // A radar tower is 1137 build units and the aircraft pays 2 a tick, so
        // 569 increments finish it. The first tick pays two of them, which
        // leaves 567 ticks after it -- one fewer than the 568 the same 569
        // increments would take one at a time. That one tick is the whole of
        // what section 110 explains, and 567 is what the demo corpus shows for
        // this exact pair over 19 builds.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "CORCA", CorCaKeys, *script);
        defineUnit(sim, "CORRAD", CorRadKeys, *script);
        defineUnit(sim, "CORCOM", CorComKeys, *script);
        auto player = addWellStockedPlayer(sim);
        addBuilder(sim, "CORCOM", player, SimVector(200_ss, 0_ss, 200_ss), script);

        auto site = SimVector(600_ss, 0_ss, 600_ss);
        auto builderId = addBuilder(sim, "CORCA", player, SimVector(600_ss, 0_ss, 560_ss), script);
        sim.getUnitState(builderId).orders.push_back(BuildOrder("CORRAD", site));

        auto work = runUntilFirstIncrement(sim, builderId, "CORRAD", 60);
        REQUIRE(work.tick > 0);
        REQUIRE(work.progress == 4u);

        auto frameRef = findNanoframe(sim, "CORRAD");
        REQUIRE(frameRef.has_value());
        auto frameId = *frameRef;
        const auto& productDefinition = sim.unitDefinitions.at("CORRAD");

        // A frame nobody is lathing falls apart and is deleted, so the walk
        // has to ask whether it is still there -- otherwise a change that
        // stopped the aircraft building would end this test in a crash inside
        // the simulation rather than in a failed expectation here.
        auto frameDecayed = false;
        int ticksLathing = 0;
        while (ticksLathing < 2000)
        {
            auto frame = sim.tryGetUnitState(frameId);
            if (!frame)
            {
                frameDecayed = true;
                break;
            }
            if (!frame->get().isBeingBuilt(productDefinition))
            {
                break;
            }
            sim.tick();
            ++ticksLathing;
        }

        INFO("the nanoframe decayed away, which means nothing was lathing it");
        REQUIRE_FALSE(frameDecayed);
        REQUIRE_FALSE(sim.getUnitState(frameId).isBeingBuilt(productDefinition));
        REQUIRE(sim.getUnitState(frameId).buildTimeCompleted == 1137u);
        REQUIRE(ticksLathing == 567);
    }

    TEST_CASE("a factory waits for its build stance and lathes once a tick", "[aircraftbuild]")
    {
        // The contrast, and the reason the change is confined to aircraft:
        // BuildingBuild waits for INBUILDSTANCE before it creates anything,
        // so its lathe state never calls the wait, and nothing can run it
        // twice in a tick.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "CORLAB", CorLabKeys, *script);
        defineUnit(sim, "CORDRAG", CorDragKeys, *script);
        defineUnit(sim, "CORCOM", CorComKeys, *script);
        auto player = addWellStockedPlayer(sim);
        addBuilder(sim, "CORCOM", player, SimVector(200_ss, 0_ss, 200_ss), script);

        auto factoryId = addBuilder(sim, "CORLAB", player, SimVector(600_ss, 0_ss, 600_ss), script);
        sim.getUnitState(factoryId).buildQueue.emplace_back("CORDRAG", 1);

        // Out of its stance, a factory does not so much as lay the frame down.
        tick(sim, 30);
        REQUIRE_FALSE(findNanoframe(sim, "CORDRAG").has_value());

        // The script would set this; the empty one has no StartBuilding.
        sim.getUnitState(factoryId).inBuildStance = true;

        auto work = runUntilFirstIncrement(sim, factoryId, "CORDRAG", 60);
        REQUIRE(work.tick > 0);

        // One increment, at CORLAB's rate of WorkerTime 120 / 30 = 4.
        REQUIRE(work.progress == 4u);

        auto frameRef = findNanoframe(sim, "CORDRAG");
        REQUIRE(frameRef.has_value());
        auto frameId = *frameRef;
        sim.tick();
        REQUIRE(sim.getUnitState(frameId).buildTimeCompleted == 8u);
    }

}
