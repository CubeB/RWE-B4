#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>
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
