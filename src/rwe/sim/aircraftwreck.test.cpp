#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain()
        {
            Grid<unsigned char> heights(32, 32, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * A `Killed(severity, corpsetype)` ladder: a run of `severity <= N`
         * rungs, each writing a corpse level into the second parameter, and
         * one more level for everything past the last rung.
         */
        struct KilledLadder
        {
            struct Rung
            {
                int threshold;
                int level;
            };

            std::vector<Rung> rungs;
            int fallthroughLevel{3};

            /**
             * Put a sleep between the ladder and the return, which is what a
             * shipped script does while it throws pieces about. It leaves the
             * thread suspended with its locals on the call stack rather than
             * finished with them in returnLocals.
             */
            bool sleepBeforeReturn{false};
        };

        /** The three bands the corpus pins at severity 25 and 50. */
        KilledLadder stockLadder()
        {
            return KilledLadder{{{25, 1}, {50, 2}}, 3, false};
        }

        /**
         * Assemble that ladder as COB. `CobScript` is plain data, so a script
         * is a vector of opcode words plus a name for the entry point; every
         * opcode used here takes exactly one operand word after it, and jump
         * operands are word indices into the same vector, so the forward ones
         * are emitted as a hole and patched once their target is known.
         */
        std::shared_ptr<CobScript> makeKilledLadderScript(const KilledLadder& ladder)
        {
            // GameSimulation hands createThread("Killed", {severity, 0}), so
            // local 0 is the severity and local 1 the corpse type that
            // readCorpseLevel reads back. There is no third local: the thread
            // never runs CREATE_LOCAL_VAR, and locals.at() throws.
            constexpr uint32_t severityLocal = 0;
            constexpr uint32_t corpseLocal = 1;

            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");

            auto& code = script->instructions;
            auto emit = [&](OpCode op) { code.push_back(static_cast<uint32_t>(op)); };
            auto emitOperand = [&](int operand) { code.push_back(static_cast<uint32_t>(operand)); };
            auto emitHole = [&]() {
                code.push_back(0);
                return code.size() - 1;
            };

            std::vector<size_t> jumpsToEnd;

            for (const auto& rung : ladder.rungs)
            {
                emit(OpCode::PUSH_LOCAL_VAR);
                emitOperand(severityLocal);
                emit(OpCode::PUSH_CONSTANT);
                emitOperand(rung.threshold);
                emit(OpCode::SET_LESS_OR_EQUAL);
                emit(OpCode::JUMP_IF_ZERO);
                auto nextRung = emitHole();

                emit(OpCode::PUSH_CONSTANT);
                emitOperand(rung.level);
                emit(OpCode::POP_LOCAL_VAR);
                emitOperand(corpseLocal);
                emit(OpCode::JUMP);
                jumpsToEnd.push_back(emitHole());

                code[nextRung] = static_cast<uint32_t>(code.size());
            }

            emit(OpCode::PUSH_CONSTANT);
            emitOperand(ladder.fallthroughLevel);
            emit(OpCode::POP_LOCAL_VAR);
            emitOperand(corpseLocal);

            for (auto hole : jumpsToEnd)
            {
                code[hole] = static_cast<uint32_t>(code.size());
            }

            if (ladder.sleepBeforeReturn)
            {
                emit(OpCode::PUSH_CONSTANT);
                emitOperand(100);
                emit(OpCode::SLEEP);
            }

            // RETURN pops a return value before it copies the locals into
            // returnLocals, so the stack cannot be empty when it lands.
            emit(OpCode::PUSH_LOCAL_VAR);
            emitOperand(corpseLocal);
            emit(OpCode::RETURN);

            script->functions.push_back(CobFunctionInfo{"Killed", 0});
            return script;
        }

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
            d.damage = 1000;
            return d;
        }

        UnitDefinition makeUnitDef(const std::string& corpse)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.corpse = corpse;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawn(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /**
         * A script whose Killed ladder asks for a particular corpse level:
         * `Killed(severity, corpsetype) { corpsetype = level; return 0; }`.
         * The shipped ones are three-band ladders off the severity; what they
         * write into the second parameter is all the spawner reads.
         */
        std::shared_ptr<CobScript> makeKilledLevelScript(const std::vector<std::string>& pieces, int level)
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            for (const auto& piece : pieces)
            {
                script->pieces.push_back(piece);
            }

            script->instructions = {
                static_cast<uint32_t>(OpCode::PUSH_CONSTANT),
                static_cast<uint32_t>(level),
                static_cast<uint32_t>(OpCode::POP_LOCAL_VAR),
                1u,
                static_cast<uint32_t>(OpCode::PUSH_CONSTANT),
                0u,
                static_cast<uint32_t>(OpCode::RETURN)};
            script->functions.push_back(CobFunctionInfo{"Killed", 0u});
            return script;
        }

        int countFeatures(const GameSimulation& sim)
        {
            int n = 0;
            for ([[maybe_unused]] const auto& f : sim.features)
            {
                ++n;
            }
            return n;
        }
    }

    TEST_CASE("a unit whose FBI names no Corpse leaves nothing behind", "[wreckage]")
    {
        // Every one of the thirty aircraft in the shipped data omits the
        // Corpse key, which is how the original gets its "aircraft leave no
        // wreck" behaviour -- there is no rule about aircraft anywhere, the
        // data simply names no corpse and 0x4863AC finds a sentinel.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        std::vector<UnitPieceDefinition> modelPieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(modelPieces));

        sim.featureDefinitions.insert(makeWreckDef("HULK"));
        sim.featureNameIndex.insert_or_assign("HULK", FeatureDefinitionId(0));

        SECTION("a ground unit with a corpse leaves a wreck")
        {
            sim.unitDefinitions["tank"] = makeUnitDef("HULK");
            auto id = spawn(sim, "tank", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.getUnitState(id).markAsDead();
            sim.tick();

            REQUIRE(countFeatures(sim) == 1);
        }

        SECTION("an aircraft, which names none, leaves nothing")
        {
            sim.unitDefinitions["bomber"] = makeUnitDef("");
            auto id = spawn(sim, "bomber", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.getUnitState(id).markAsDead();
            sim.tick();

            REQUIRE(countFeatures(sim) == 0);
        }
    }

    TEST_CASE("how hard a unit was hit decides what its Killed script is told", "[wreckage]")
    {
        // clamp(1, 100, (100 * overkill / maxdamage) / 2). RWE used to hand
        // every death the constant 50, so every unit came apart the same way
        // whatever killed it.
        SECTION("a kill with nothing to spare is the gentlest there is")
        {
            REQUIRE(computeKilledSeverity(0, 100) == 1);
        }

        SECTION("overkill equal to the unit's whole health is halfway up")
        {
            REQUIRE(computeKilledSeverity(100, 100) == 50);
        }

        SECTION("a shell worth twice the unit saturates the scale")
        {
            REQUIRE(computeKilledSeverity(200, 100) == 100);
            REQUIRE(computeKilledSeverity(1000, 100) == 100);
        }

        SECTION("the bands in between")
        {
            // A commander's 3000 hit points against a 1500-point overkill.
            REQUIRE(computeKilledSeverity(1500, 3000) == 25);
        }

        SECTION("a definition with no hit points at all does not divide by zero")
        {
            REQUIRE(computeKilledSeverity(50, 0) == 1);
        }
    }

    TEST_CASE("the corpse level walks the featuredead chain", "[wreckage]")
    {
        // A shipped Killed script reads the severity as a three-band ladder
        // and writes a level into its second parameter; the spawner then
        // walks the corpse feature's featuredead chain one step for each
        // level above the first (0x4863A7), and leaves nothing at all if the
        // chain runs out (0x4863AC). RWE ran the ladder and discarded the
        // answer, so every wreck in the game was the level-one wreck.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        std::vector<UnitPieceDefinition> modelPieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(modelPieces));

        // HULK breaks down to RUBBLE, and RUBBLE to nothing.
        auto hulk = sim.featureDefinitions.insert(makeWreckDef("HULK"));
        auto rubble = sim.featureDefinitions.insert(makeWreckDef("RUBBLE"));
        sim.featureNameIndex.insert_or_assign("HULK", hulk);
        sim.featureNameIndex.insert_or_assign("RUBBLE", rubble);
        sim.featureDefinitions.get(hulk).featureDead = rubble;

        sim.unitDefinitions["tank"] = makeUnitDef("HULK");

        auto killAtLevel = [&](unsigned int level) {
            auto id = spawn(sim, "tank", player, SimVector(100_ss, 0_ss, 100_ss), script);
            auto& unit = sim.getUnitState(id);
            unit.markAsDead();
            std::get<UnitState::LifeStateDead>(unit.lifeState).corpseLevel = level;
            sim.tick();
        };

        SECTION("level one leaves the wreck itself")
        {
            killAtLevel(1);
            REQUIRE(countFeatures(sim) == 1);
            REQUIRE(sim.getFeatureDefinition(sim.features.begin()->second.featureName).name == "HULK");
        }

        SECTION("level two leaves what the wreck breaks down to")
        {
            killAtLevel(2);
            REQUIRE(countFeatures(sim) == 1);
            REQUIRE(sim.getFeatureDefinition(sim.features.begin()->second.featureName).name == "RUBBLE");
        }

        SECTION("a level past the end of the chain leaves nothing")
        {
            killAtLevel(3);
            REQUIRE(countFeatures(sim) == 0);
        }
    }

    TEST_CASE("a Killed script's ladder decides the corpse", "[wreckage]")
    {
        // The seam between the two cases above: the severity is computed from
        // the blow, handed to a real COB `Killed`, and the level that script
        // writes back into its second parameter is read out of the thread and
        // walked down the featuredead chain. Neither end tests the middle --
        // the arithmetic case never runs a script and the chain case sets the
        // level by hand.
        //
        // The thresholds are not invented. `docs/TOTALA-EXE-WRECKS.md`, "What
        // 60,075 recorded deaths say about the severity", pins them at the
        // boundary over thirteen recorded games: severity 25 gives level 1 or
        // 2 and 26 gives 2; severity 50 gives 2 and 51 gives 3. Those are the
        // `severity <= 25` and `severity <= 50` rungs of the stock script.
        //
        // Expected difference from the original: none. The 1-vs-2 split the
        // corpus shows inside the 1-25 band is a difference between units'
        // own scripts, not the engine, so it is the script below that decides
        // it here -- which is exactly the point of running a real one.
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        std::vector<UnitPieceDefinition> modelPieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(modelPieces));

        // HULK breaks down to RUBBLE, and RUBBLE to nothing.
        auto hulk = sim.featureDefinitions.insert(makeWreckDef("HULK"));
        auto rubble = sim.featureDefinitions.insert(makeWreckDef("RUBBLE"));
        sim.featureNameIndex.insert_or_assign("HULK", hulk);
        sim.featureNameIndex.insert_or_assign("RUBBLE", rubble);
        sim.featureDefinitions.get(hulk).featureDead = rubble;

        // maxHitPoints 100 makes the severity half the overkill:
        // clamp(1, 100, (100 * overkill / 100) / 2).
        sim.unitDefinitions["tank"] = makeUnitDef("HULK");

        // Kill a unit running `script` with a blow of the given size, and
        // report the level its ladder settled on before the unit is swept up.
        auto killWith = [&](const std::shared_ptr<CobScript>& script, unsigned int overkill) {
            auto id = spawn(sim, "tank", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.killUnit(id, std::nullopt, overkill);
            return std::get<UnitState::LifeStateDead>(sim.getUnitState(id).lifeState).corpseLevel;
        };

        auto wreckOnTheMap = [&]() -> std::optional<std::string> {
            sim.tick();
            if (countFeatures(sim) == 0)
            {
                return std::nullopt;
            }
            return sim.getFeatureDefinition(sim.features.begin()->second.featureName).name;
        };

        SECTION("the gentlest band leaves the wreck whole")
        {
            auto script = makeKilledLadderScript(stockLadder());

            REQUIRE(killWith(script, 50) == 1); // severity 25
            REQUIRE(wreckOnTheMap() == "HULK");
        }

        SECTION("one point of overkill past the first rung drops a level")
        {
            auto script = makeKilledLadderScript(stockLadder());

            // Overkill 52 is severity 26, the first value the corpus shows
            // reading level 2 without exception.
            REQUIRE(killWith(script, 52) == 2);
            REQUIRE(wreckOnTheMap() == "RUBBLE");
        }

        SECTION("the top of the second band is still the second band")
        {
            auto script = makeKilledLadderScript(stockLadder());

            REQUIRE(killWith(script, 100) == 2); // severity 50
            REQUIRE(wreckOnTheMap() == "RUBBLE");
        }

        SECTION("past the last rung the chain runs out and nothing is left")
        {
            auto script = makeKilledLadderScript(stockLadder());

            REQUIRE(killWith(script, 102) == 3); // severity 51
            REQUIRE(wreckOnTheMap() == std::nullopt);
        }

        SECTION("a blow worth twice the unit saturates and takes the last band")
        {
            auto script = makeKilledLadderScript(stockLadder());

            REQUIRE(killWith(script, 200) == 3); // severity 100
            REQUIRE(wreckOnTheMap() == std::nullopt);
        }

        SECTION("a script still asleep when it is asked has its answer read off the call stack")
        {
            // The whole reason readCorpseLevel looks in two places. A real
            // `Killed` throws pieces about and sleeps between them, and the
            // unit is swept up at the end of this tick, so the thread never
            // reaches its RETURN and never fills returnLocals -- the level is
            // sitting in the locals of the suspended frame instead.
            auto ladder = stockLadder();
            ladder.sleepBeforeReturn = true;
            auto script = makeKilledLadderScript(ladder);

            REQUIRE(killWith(script, 52) == 2);
            REQUIRE(wreckOnTheMap() == "RUBBLE");
        }

        SECTION("a script that writes nothing back leaves the default")
        {
            // readCorpseLevel masks the level to four bits and treats 0 as no
            // answer (0x486D69), so a ladder writing 0 is indistinguishable
            // from one that never ran: the level stays at its default of 1
            // and the intact wreck is left. Level 0 does *not* mean "leave
            // nothing" on this path -- the corpus's level-0 deaths come from
            // causes 4, 5 and 9, which skip the script entirely.
            KilledLadder ladder;
            ladder.fallthroughLevel = 0;
            auto script = makeKilledLadderScript(ladder);

            REQUIRE(killWith(script, 200) == 1);
            REQUIRE(wreckOnTheMap() == "HULK");
        }

        SECTION("a level past the four bits the original keeps is wrapped, not obeyed")
        {
            // 0x486D69 masks to four bits before the walk, which is what
            // stops a script sending the chain walk off on its own. 17 comes
            // back as 1, the intact wreck.
            KilledLadder ladder;
            ladder.fallthroughLevel = 17;
            auto script = makeKilledLadderScript(ladder);

            REQUIRE(killWith(script, 200) == 1);
            REQUIRE(wreckOnTheMap() == "HULK");
        }
    }

    TEST_CASE("an isfeature unit always leaves its wreck", "[wreckage]")
    {
        // Death cause 7, and the answer to this document's long-standing
        // question -- what is "a wreck that does not burn on land"? It is a
        // type, not a situation. 0x41B9FE and 0x486167 both test bit 24 of
        // `def+0x241`, which is `isfeature`, and force the cause to 7; the
        // packer then pins the corpse level at 1 (0x486525) whatever the
        // Killed ladder asked for. In the shipped data that is the dragon's
        // teeth and the forts: scenery that happens to be built.
        auto script = makeKilledLevelScript({"base"}, 2);
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        std::vector<UnitPieceDefinition> modelPieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(modelPieces));

        // HULK breaks down to RUBBLE, as in the chain test above.
        auto hulk = sim.featureDefinitions.insert(makeWreckDef("HULK"));
        auto rubble = sim.featureDefinitions.insert(makeWreckDef("RUBBLE"));
        sim.featureNameIndex.insert_or_assign("HULK", hulk);
        sim.featureNameIndex.insert_or_assign("RUBBLE", rubble);
        sim.featureDefinitions.get(hulk).featureDead = rubble;

        sim.unitDefinitions["fort"] = makeUnitDef("HULK");

        SECTION("an ordinary unit is given the level its ladder asked for")
        {
            auto id = spawn(sim, "fort", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.killUnit(id, std::nullopt, 100000u);

            REQUIRE(std::get<UnitState::LifeStateDead>(sim.getUnitState(id).lifeState).corpseLevel == 2);

            sim.tick();
            REQUIRE(countFeatures(sim) == 1);
            REQUIRE(sim.getFeatureDefinition(sim.features.begin()->second.featureName).name == "RUBBLE");
        }

        SECTION("scenery gets the intact wreck instead, ladder or no ladder")
        {
            sim.unitDefinitions["fort"].isFeature = true;
            auto id = spawn(sim, "fort", player, SimVector(100_ss, 0_ss, 100_ss), script);
            sim.killUnit(id, std::nullopt, 100000u);

            const auto& dead = std::get<UnitState::LifeStateDead>(sim.getUnitState(id).lifeState);
            REQUIRE(dead.leaveCorpse);
            REQUIRE(dead.corpseLevel == 1);

            sim.tick();
            REQUIRE(countFeatures(sim) == 1);
            REQUIRE(sim.getFeatureDefinition(sim.features.begin()->second.featureName).name == "HULK");
        }

        SECTION("but a half-built one still leaves nothing")
        {
            // The forcing sits ahead of the nanoframe rule in the original --
            // 0x486525 runs before 0x4865D2 -- so being scenery does not save
            // a fort that was never finished.
            sim.unitDefinitions["fort"].isFeature = true;
            sim.unitDefinitions["fort"].buildTime = 100u;
            auto id = spawn(sim, "fort", player, SimVector(100_ss, 0_ss, 100_ss), script);
            REQUIRE(sim.getUnitState(id).isBeingBuilt(sim.unitDefinitions.at("fort")));

            sim.killUnit(id, std::nullopt, 100000u);
            REQUIRE_FALSE(std::get<UnitState::LifeStateDead>(sim.getUnitState(id).lifeState).leaveCorpse);

            sim.tick();
            REQUIRE(countFeatures(sim) == 0);
        }
    }
}
