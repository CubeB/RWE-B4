#include <catch2/catch_approx.hpp>
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

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        FeatureDefinition makeFeatureDef(const std::string& name, unsigned int metal, unsigned int energy, bool reclaimable)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 10_ss;
            d.reclaimable = reclaimable;
            d.autoreclaimable = reclaimable;
            d.metal = metal;
            d.energy = energy;
            d.blocking = true;
            return d;
        }

        PlayerId addPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("reclaimer"),
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

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
        }

        UnitDefinition makeBuilderDef(unsigned int workerTimePerTick)
        {
            UnitDefinition d{};
            d.builder = true;
            d.workerTimePerTick = workerTimePerTick;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.buildDistance = 100_ss;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId addBuilderUnit(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions["builder"] = makeBuilderDef(30u);
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = "builder";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            // Normally set by the COB script's StartBuilding thread; the
            // empty test script has none, so pretend the arm is deployed.
            unit.inBuildStance = true;
            return unitId;
        }
    }

    TEST_CASE("computeFeatureReclaimWork", "[reclaim]")
    {
        SECTION("is the sum of the feature's metal and energy")
        {
            REQUIRE(computeFeatureReclaimWork(makeFeatureDef("rock", 100u, 50u, true)) == 150u);
        }

        SECTION("is never zero, so worthless features can still be cleared")
        {
            REQUIRE(computeFeatureReclaimWork(makeFeatureDef("twig", 0u, 0u, true)) == 1u);
        }
    }

    TEST_CASE("GameSimulation::reclaimFeature", "[reclaim]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto rockDef = sim.featureDefinitions.insert(makeFeatureDef("rock", 100u, 50u, true));
        auto rockId = sim.addFeature(rockDef, 4, 4).value();
        auto footprint = sim.computeFootprintRegion(sim.getFeature(rockId).position, 2u, 2u);
        REQUIRE(sim.anyFeatureOccupies(footprint));

        SECTION("credits resources in proportion to work done and removes the feature when finished")
        {
            // 150 work total, 30 per call -> five calls
            for (int i = 0; i < 4; ++i)
            {
                REQUIRE_FALSE(sim.reclaimFeature(rockId, player, 30u));
                REQUIRE(sim.tryGetFeature(rockId).has_value());
            }
            const auto& info = sim.getPlayer(player);
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(80.0f));
            REQUIRE(info.energyProductionBuffer.value == Catch::Approx(40.0f));

            REQUIRE(sim.reclaimFeature(rockId, player, 30u));
            REQUIRE_FALSE(sim.tryGetFeature(rockId).has_value());
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(100.0f));
            REQUIRE(info.energyProductionBuffer.value == Catch::Approx(50.0f));

            // The footprint is free again.
            REQUIRE_FALSE(sim.anyFeatureOccupies(footprint));
        }

        SECTION("excess work does not over-credit")
        {
            REQUIRE(sim.reclaimFeature(rockId, player, 1000u));
            REQUIRE(sim.getPlayer(player).metalProductionBuffer.value == Catch::Approx(100.0f));
        }

        SECTION("spawns the reclamate feature in place of the reclaimed one")
        {
            auto smudgeDef = sim.featureDefinitions.insert(makeFeatureDef("smudge", 0u, 0u, false));
            sim.featureDefinitions.get(rockDef).featureReclamate = smudgeDef;
            auto position = sim.getFeature(rockId).position;

            REQUIRE(sim.reclaimFeature(rockId, player, 150u));
            REQUIRE_FALSE(sim.tryGetFeature(rockId).has_value());

            REQUIRE(sim.anyFeatureOccupies(footprint));
            bool foundSmudge = false;
            for (const auto& [id, feature] : sim.features)
            {
                if (feature.featureName == smudgeDef && feature.position == position)
                {
                    foundSmudge = true;
                }
            }
            REQUIRE(foundSmudge);
        }

        SECTION("refuses to reclaim features that are not reclaimable")
        {
            auto metalPatchDef = sim.featureDefinitions.insert(makeFeatureDef("patch", 500u, 0u, false));
            auto patchId = sim.addFeature(metalPatchDef, 10, 10).value();
            REQUIRE_FALSE(sim.reclaimFeature(patchId, player, 1000u));
            REQUIRE(sim.tryGetFeature(patchId).has_value());
            REQUIRE(sim.getPlayer(player).metalProductionBuffer.value == Catch::Approx(0.0f));
        }

        SECTION("reports completion for a feature that no longer exists")
        {
            sim.deleteFeature(rockId);
            REQUIRE(sim.reclaimFeature(rockId, player, 30u));
        }
    }

    TEST_CASE("a builder with a reclaim order reclaims the feature over successive ticks", "[reclaim]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);

        auto rockDef = sim.featureDefinitions.insert(makeFeatureDef("rock", 100u, 50u, true));
        auto rockId = sim.addFeature(rockDef, 4, 4).value();
        auto rockPosition = sim.getFeature(rockId).position;

        // Place the builder within reclaim range so no pathfinding is needed.
        auto builderId = addBuilderUnit(sim, player, rockPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(rockId));

        // 150 work at 30 per tick = 5 ticks of reclaiming, plus one tick to
        // enter the reclaiming state. Allow some slack.
        for (int i = 0; i < 20 && sim.tryGetFeature(rockId); ++i)
        {
            sim.tick();
        }

        REQUIRE_FALSE(sim.tryGetFeature(rockId).has_value());
        const auto& builder = sim.getUnitState(builderId);
        REQUIRE(builder.orders.empty());
        REQUIRE(std::holds_alternative<UnitBehaviorStateIdle>(builder.behaviourState));

        // Resources land in the production buffer until the per-second
        // resource update folds them into the player's stockpile.
        const auto& info = sim.getPlayer(player);
        REQUIRE(info.metal.value + info.metalProductionBuffer.value == Catch::Approx(100.0f));
        REQUIRE(info.energy.value + info.energyProductionBuffer.value == Catch::Approx(50.0f));
    }
}
