#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
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
}
