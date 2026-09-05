#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

/**
 * A nanoframe nobody is building falls apart. See §92 of docs/TOTALA-EXE.md:
 * the frame carries its own `GetBuilt` mission, and once its timer comes round
 * to a period in which no builder claimed it, it loses one energy-point of its
 * own build cost per tick until there is nothing left.
 *
 * The numbers in here are the shipped rev31 ones, read straight out of
 * the `units` directory of rev31.gp3, because the whole rule is a statement about
 * BuildCostEnergy and a fixture that made its own up would prove nothing.
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

        /** Never short of anything, so a builder is only ever limited by its worker time. */
        PlayerId addWellStockedPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("builder"),
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

        /** Nothing stored and nothing coming in, so every request stalls. */
        PlayerId addBrokePlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("pauper"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(0.0f),
                Energy(0.0f),
                Metal(10000.0f),
                Energy(10000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
            };
            return sim.addPlayer(p);
        }

        /**
         * Runs a scrap of FBI through the real parser and the real definition
         * mapping, so a mistyped key fails here rather than quietly leaving a
         * field on its default -- which for BuildCostEnergy would be zero, and
         * would make every frame in this file vanish in one step.
         *
         * Objectname is the test model rather than the shipped one; the rule
         * never looks at the mesh.
         */
        UnitDefinition definitionFromFbi(const std::string& unitName, const std::string& keys)
        {
            auto tdf = parseTdfFromString(
                "[UNITINFO]\n{\nUnitName=" + unitName + ";\nObjectname=model;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n");
            auto fbi = parseUnitFbi(tdf);
            MovementClassDatabase movementClasses;
            return parseUnitDefinition(fbi, movementClasses);
        }

        // rev31 units/ARMSOLAR.fbi. 760 energy, so a full frame takes 760
        // ticks -- 25.3 seconds -- to decay away.
        const char* ArmSolarKeys =
            "FootprintX=5;\n"
            "FootprintZ=5;\n"
            "BuildCostEnergy=760;\n"
            "BuildCostMetal=145;\n"
            "MaxDamage=326;\n"
            "BuildTime=2495;\n"
            "BMcode=0;\n"
            "YardMap=ooooooooooooooooooooooooooo;\n"
            "Corpse=armsolar_dead;\n";

        // rev31 units/ARMPW.fbi -- a Peewee, the cheapest thing here.
        const char* ArmPwKeys =
            "FootprintX=2;\n"
            "FootprintZ=2;\n"
            "BuildCostEnergy=697;\n"
            "BuildCostMetal=53;\n"
            "MaxDamage=250;\n"
            "BuildTime=1452;\n"
            "BMcode=1;\n";

        // rev31 units/ARMLAB.fbi.
        const char* ArmLabKeys =
            "FootprintX=6;\n"
            "FootprintZ=5;\n"
            "BuildCostEnergy=1130;\n"
            "BuildCostMetal=705;\n"
            "MaxDamage=2690;\n"
            "BuildTime=6760;\n"
            "BMcode=0;\n"
            "YardMap=oooooooooooooooooooooooooooooo;\n";

        // rev31 units/ARMFUS.fbi. 36058 energy is twenty minutes: a half-built
        // fusion plant effectively never goes away, and that spread is the
        // point of the rule.
        const char* ArmFusKeys =
            "FootprintX=7;\n"
            "FootprintZ=7;\n"
            "BuildCostEnergy=36058;\n"
            "BuildCostMetal=5130;\n"
            "MaxDamage=3100;\n"
            "BuildTime=93768;\n"
            "BMcode=0;\n"
            "YardMap=ooooooooooooooooooooooooooooooooooooooooooooooooo;\n";

        // rev31 units/ARMCK.fbi -- a Construction Kbot, worker time 80, which
        // the parser divides by the tick rate to two work units a tick.
        const char* ArmCkKeys =
            "FootprintX=2;\n"
            "FootprintZ=2;\n"
            "BuildCostEnergy=2410;\n"
            "BuildCostMetal=120;\n"
            "MaxDamage=700;\n"
            "BuildTime=5597;\n"
            "WorkerTime=80;\n"
            "BMcode=1;\n"
            "Builder=1;\n"
            "Builddistance=40;\n"
            "CanReclamate=1;\n";

        FeatureDefinition makeWreckDef(const std::string& name)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 20_ss;
            d.blocking = true;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = 100;
            d.hitDensity = 100;
            d.damage = 1000;
            return d;
        }

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
         * A nanoframe placed the way the game places one, through trySpawnUnit,
         * because that is where the decay timer is wound, and then wound
         * forward to the given amount of work done. The hit points are set to
         * what the build routine would have left on it, so a decay step can be
         * checked against them.
         */
        UnitId placeNanoframe(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, unsigned int workDone)
        {
            auto unitId = sim.trySpawnUnit(unitType, owner, pos, std::nullopt).value();
            const auto& unitDefinition = sim.unitDefinitions.at(unitType);
            auto& unit = sim.getUnitState(unitId);
            unit.buildTimeCompleted = workDone;
            unit.hitPoints = (workDone * unitDefinition.maxHitPoints) / unitDefinition.buildTime;
            return unitId;
        }

        /** A builder assembled by hand, so it does not have to fit the occupied grid. */
        UnitId addBuilder(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto unitId = addUnitOfType(sim, "ARMCK", owner, pos, script);
            auto& unit = sim.getUnitState(unitId);
            unit.hitPoints = sim.unitDefinitions.at("ARMCK").maxHitPoints;
            // Normally set by the COB script's StartBuilding thread; the empty
            // test script has none, so pretend the arm is deployed.
            unit.inBuildStance = true;
            return unitId;
        }

        unsigned int progressOf(const GameSimulation& sim, UnitId unitId)
        {
            return sim.getUnitState(unitId).buildTimeCompleted;
        }

        /** How many ticks until the frame is gone, or nothing if it outlasts the budget. */
        std::optional<int> ticksUntilGone(GameSimulation& sim, UnitId unitId, int budget)
        {
            for (int i = 1; i <= budget; ++i)
            {
                sim.tick();
                if (!sim.tryGetUnitState(unitId))
                {
                    return i;
                }
            }
            return std::nullopt;
        }
    }

    TEST_CASE("a nanoframe nobody touches waits 330 ticks and then starts to fall apart", "[nanoframe]")
    {
        // The GetBuilt mission schedules +300 in its first state and +30 in
        // its second, neither of which tests anything, so the earliest a frame
        // can lose anything is eleven seconds after it is placed.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "ARMSOLAR", ArmSolarKeys, *script);
        auto player = addWellStockedPlayer(sim);

        auto frameId = placeNanoframe(sim, "ARMSOLAR", player, SimVector(100_ss, 0_ss, 100_ss), 2000u);

        tick(sim, 329);
        REQUIRE(progressOf(sim, frameId) == 2000u);

        sim.tick();
        REQUIRE(sim.gameTime == GameTime(330));

        // buildTime * 11 / buildCostEnergy = 2495 * 11 / 760 = 36 and a bit.
        REQUIRE(progressOf(sim, frameId) == 2000u - 36u);

        // And then one every eleven ticks, not every tick.
        tick(sim, 10);
        REQUIRE(progressOf(sim, frameId) == 2000u - 36u);
        sim.tick();
        REQUIRE(progressOf(sim, frameId) < 2000u - 36u);
    }

    TEST_CASE("a nanoframe decays at one energy-point of its build cost per tick", "[nanoframe]")
    {
        // The build time cancels out of TA's `buildtime * 11 / buildCostEnergy`
        // against the division the build routine itself does, so what is left
        // is the unit's own energy cost as a number of ticks. An ARMSOLAR
        // frame is 760 ticks from full to gone whatever its build time is.
        auto script = makeEmptyCobScript({"base"});

        struct Row
        {
            const char* name;
            const char* keys;
            unsigned int buildTime;
            int buildCostEnergy;
        };

        std::vector<Row> rows{
            Row{"ARMSOLAR", ArmSolarKeys, 2495u, 760},
            Row{"ARMPW", ArmPwKeys, 1452u, 697},
            Row{"ARMLAB", ArmLabKeys, 6760u, 1130}};

        for (const auto& row : rows)
        {
            INFO(row.name);

            GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
            registerTestModel(sim);
            defineUnit(sim, row.name, row.keys, *script);
            auto player = addWellStockedPlayer(sim);

            // As close to finished as a frame can get without being finished.
            auto frameId = placeNanoframe(sim, row.name, player, SimVector(100_ss, 0_ss, 100_ss), row.buildTime - 1u);

            auto gone = ticksUntilGone(sim, frameId, static_cast<int>(GameSimulation::NanoframeDecayGraceTicks) + row.buildCostEnergy + 32);
            REQUIRE(gone.has_value());

            // Measured from the first decay, not from placement.
            auto decayTicks = *gone - static_cast<int>(GameSimulation::NanoframeDecayGraceTicks);

            // Decay only happens on an eleven-tick lattice, so the answer can
            // fall short of the exact figure by less than one interval; it
            // must never run over it.
            REQUIRE(decayTicks <= row.buildCostEnergy);
            REQUIRE(decayTicks > row.buildCostEnergy - static_cast<int>(GameSimulation::NanoframeDecayTicks));
        }
    }

    TEST_CASE("an expensive nanoframe effectively never goes away", "[nanoframe]")
    {
        // 36058 energy is twenty minutes. A minute of neglect should barely
        // mark a fusion plant, and a rule that treated every frame the same
        // would have eaten it long before here.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "ARMFUS", ArmFusKeys, *script);
        auto player = addWellStockedPlayer(sim);

        auto frameId = placeNanoframe(sim, "ARMFUS", player, SimVector(100_ss, 0_ss, 100_ss), 93767u);

        tick(sim, 330 + 1800);
        REQUIRE(sim.tryGetUnitState(frameId).has_value());

        // A minute out of twenty is five per cent.
        REQUIRE(progressOf(sim, frameId) > (93768u * 9u) / 10u);
    }

    TEST_CASE("hit points come off a decaying nanoframe in step with its progress", "[nanoframe]")
    {
        // The original does not touch hit points here at all: it hands the
        // negated amount to the ordinary build routine, which recomputes
        // `trunc(fraction * maxdamage)` and applies the difference. So the
        // health bar tracks the frame exactly, backwards.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "ARMSOLAR", ArmSolarKeys, *script);
        auto player = addWellStockedPlayer(sim);

        auto frameId = placeNanoframe(sim, "ARMSOLAR", player, SimVector(100_ss, 0_ss, 100_ss), 2000u);
        REQUIRE(sim.getUnitState(frameId).hitPoints == (2000u * 326u) / 2495u);

        for (int step = 0; step < 8; ++step)
        {
            tick(sim, step == 0 ? 330 : 11);
            const auto& frame = sim.getUnitState(frameId);
            REQUIRE(frame.hitPoints == (frame.buildTimeCompleted * 326u) / 2495u);
        }

        // And it really is falling, not just staying consistent.
        REQUIRE(sim.getUnitState(frameId).hitPoints < (2000u * 326u) / 2495u);
    }

    TEST_CASE("a builder on the job holds the decay off indefinitely", "[nanoframe]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "ARMSOLAR", ArmSolarKeys, *script);
        defineUnit(sim, "ARMCK", ArmCkKeys, *script);
        auto player = addWellStockedPlayer(sim);

        auto framePosition = SimVector(100_ss, 0_ss, 100_ss);
        auto frameId = placeNanoframe(sim, "ARMSOLAR", player, framePosition, 100u);
        auto builderId = addBuilder(sim, player, framePosition + SimVector(60_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(CompleteBuildOrder(frameId));

        // Well past the 330-tick grace and several 30-tick checks beyond it.
        tick(sim, 600);

        REQUIRE(sim.tryGetUnitState(frameId).has_value());
        // Two work units a tick from an ARMCK's worker time of 80.
        REQUIRE(progressOf(sim, frameId) > 100u);
        REQUIRE(std::holds_alternative<UnitBehaviorStateBuilding>(sim.getUnitState(builderId).behaviourState));
    }

    TEST_CASE("a builder the economy has refused still holds the decay off", "[nanoframe]")
    {
        // TA sets the frame's event bit 15 at the top of the build routine,
        // before it asks the economy for anything, so a builder standing there
        // with nothing to spend is still enough to stop the frame rotting.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "ARMSOLAR", ArmSolarKeys, *script);
        defineUnit(sim, "ARMCK", ArmCkKeys, *script);
        auto player = addBrokePlayer(sim);

        auto framePosition = SimVector(100_ss, 0_ss, 100_ss);
        auto frameId = placeNanoframe(sim, "ARMSOLAR", player, framePosition, 100u);
        auto builderId = addBuilder(sim, player, framePosition + SimVector(60_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(CompleteBuildOrder(frameId));

        // RWE's economy only refuses a unit once a second's settle has left it
        // in debt, so the builder gets the first second on credit. Take the
        // reading after that, once it is properly broke.
        tick(sim, 100);
        auto progressWhenBroke = progressOf(sim, frameId);

        tick(sim, 500);

        REQUIRE(sim.tryGetUnitState(frameId).has_value());
        // Nothing more was paid for, so nothing more was built...
        REQUIRE(progressOf(sim, frameId) == progressWhenBroke);
        // ...and the builder is still standing over it, arm out.
        REQUIRE(std::holds_alternative<UnitBehaviorStateBuilding>(sim.getUnitState(builderId).behaviourState));
    }

    TEST_CASE("the decay starts thirty ticks after the last build tick", "[nanoframe]")
    {
        // While a builder is on the job the frame looks again every 30 ticks
        // rather than every 11, so the first thing an abandoned frame loses is
        // one second after the nanolathe stops -- not eleven.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "ARMSOLAR", ArmSolarKeys, *script);
        defineUnit(sim, "ARMCK", ArmCkKeys, *script);
        auto player = addWellStockedPlayer(sim);

        auto framePosition = SimVector(100_ss, 0_ss, 100_ss);
        auto frameId = placeNanoframe(sim, "ARMSOLAR", player, framePosition, 100u);
        auto builderId = addBuilder(sim, player, framePosition + SimVector(60_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(CompleteBuildOrder(frameId));

        // Tick 330 is a check the builder wins, so the next one falls at 360.
        tick(sim, 330);
        auto progressWhenAbandoned = progressOf(sim, frameId);
        REQUIRE(progressWhenAbandoned > 100u);

        sim.getUnitState(builderId).clearOrders();

        tick(sim, 29);
        REQUIRE(sim.gameTime == GameTime(359));
        REQUIRE(progressOf(sim, frameId) == progressWhenAbandoned);

        sim.tick();
        REQUIRE(progressOf(sim, frameId) == progressWhenAbandoned - 36u);
    }

    TEST_CASE("a nanoframe that decays away leaves no wreck", "[nanoframe]")
    {
        // The frame kills itself with damage cause 9, one of the three the
        // death routine short-circuits: no corpse, no Killed script, no
        // explosion. An ARMSOLAR names a corpse, and it is registered here, so
        // a wreck is what would appear if the rule were wrong.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "ARMSOLAR", ArmSolarKeys, *script);
        auto wreck = sim.featureDefinitions.insert(makeWreckDef("armsolar_dead"));
        sim.featureNameIndex.insert_or_assign("ARMSOLAR_DEAD", wreck);
        auto player = addWellStockedPlayer(sim);

        // One decay step's worth of work, so the frame is gone the first time
        // the timer comes round to nobody.
        auto frameId = placeNanoframe(sim, "ARMSOLAR", player, SimVector(100_ss, 0_ss, 100_ss), 20u);

        auto gone = ticksUntilGone(sim, frameId, 400);
        REQUIRE(gone.has_value());
        REQUIRE(*gone == 330);
        REQUIRE(sim.features.begin() == sim.features.end());

        // Nothing left behind on the next tick either.
        sim.tick();
        REQUIRE(sim.features.begin() == sim.features.end());
    }

    TEST_CASE("a unit killed while still under construction leaves no wreck", "[nanoframe]")
    {
        // Not the decay path: 0x4865D2 clears the corpse flag for any death at
        // all while the remaining build fraction is non-zero. Shoot a
        // half-built factory and there is nothing to reclaim.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        registerTestModel(sim);
        defineUnit(sim, "ARMSOLAR", ArmSolarKeys, *script);
        auto wreck = sim.featureDefinitions.insert(makeWreckDef("armsolar_dead"));
        sim.featureNameIndex.insert_or_assign("ARMSOLAR_DEAD", wreck);
        auto player = addWellStockedPlayer(sim);

        SECTION("a half-built one leaves nothing")
        {
            auto frameId = placeNanoframe(sim, "ARMSOLAR", player, SimVector(100_ss, 0_ss, 100_ss), 1200u);
            sim.killUnit(frameId);
            sim.tick();

            REQUIRE_FALSE(sim.tryGetUnitState(frameId).has_value());
            REQUIRE(sim.features.begin() == sim.features.end());
        }

        SECTION("a finished one still leaves its corpse")
        {
            auto unitId = placeNanoframe(sim, "ARMSOLAR", player, SimVector(100_ss, 0_ss, 100_ss), 2495u);
            REQUIRE_FALSE(sim.getUnitState(unitId).isBeingBuilt(sim.unitDefinitions.at("ARMSOLAR")));
            sim.killUnit(unitId);
            sim.tick();

            REQUIRE_FALSE(sim.tryGetUnitState(unitId).has_value());
            REQUIRE(sim.features.begin() != sim.features.end());
        }
    }
}
