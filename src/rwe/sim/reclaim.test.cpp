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
            // A real construction unit names CanReclamate. The original gates
            // both reclaiming and repairing on it and never looks at
            // workertime (0x489960, and the bit 9 mirror at 0x4899CC).
            d.canReclamate = true;
            d.workerTimePerTick = workerTimePerTick;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.buildDistance = 100_ss;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId addUnitOfType(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            unit.buildTimeCompleted = sim.unitDefinitions.at(unitType).buildTime;
            return unitId;
        }

        UnitId addBuilderUnit(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions["builder"] = makeBuilderDef(30u);
            auto unitId = addUnitOfType(sim, "builder", owner, pos, script);
            // Normally set by the COB script's StartBuilding thread; the
            // empty test script has none, so pretend the arm is deployed.
            sim.getUnitState(unitId).inBuildStance = true;
            return unitId;
        }

        UnitDefinition makeSolarDef()
        {
            UnitDefinition d{};
            d.maxHitPoints = 100;
            d.buildTime = 150u;
            d.buildCostMetal = Metal(100.0f);
            d.buildCostEnergy = Energy(50.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }
    }

    TEST_CASE("GameSimulation::reclaimUnit", "[reclaim]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarId = addUnitOfType(sim, "solar", player, SimVector(100_ss, 0_ss, 100_ss), script);
        const auto& info = sim.getPlayer(player);

        SECTION("a complete unit pays back its full build cost over buildTime worth of work")
        {
            for (int i = 0; i < 4; ++i)
            {
                REQUIRE_FALSE(sim.reclaimUnit(solarId, player, 30u));
                REQUIRE(sim.getUnitState(solarId).isAlive());
            }
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(80.0f));

            REQUIRE(sim.reclaimUnit(solarId, player, 30u));
            REQUIRE(sim.getUnitState(solarId).isDead());
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(100.0f));
            REQUIRE(info.energyProductionBuffer.value == Catch::Approx(50.0f));

            // Reclaimed units leave no wreck.
            sim.tick();
            REQUIRE_FALSE(sim.tryGetUnitState(solarId).has_value());
            REQUIRE(sim.features.begin() == sim.features.end());
        }

        SECTION("a half-built unit only pays back the half that was invested")
        {
            sim.getUnitState(solarId).buildTimeCompleted = 75u;
            REQUIRE(sim.reclaimUnit(solarId, player, 150u));
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(50.0f));
            REQUIRE(info.energyProductionBuffer.value == Catch::Approx(25.0f));
        }

        SECTION("reports completion for a unit that is already dead")
        {
            sim.getUnitState(solarId).markAsDead();
            REQUIRE(sim.reclaimUnit(solarId, player, 30u));
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(0.0f));
        }
    }

    TEST_CASE("a builder with a reclaim order reclaims an enemy unit over successive ticks", "[reclaim]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto enemy = addPlayer(sim);

        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        auto builderId = addBuilderUnit(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(solarId));

        for (int i = 0; i < 20 && sim.tryGetUnitState(solarId); ++i)
        {
            sim.tick();
        }

        REQUIRE_FALSE(sim.tryGetUnitState(solarId).has_value());
        const auto& builder = sim.getUnitState(builderId);
        REQUIRE(builder.orders.empty());
        REQUIRE(std::holds_alternative<UnitBehaviorStateIdle>(builder.behaviourState));

        const auto& info = sim.getPlayer(player);
        REQUIRE(info.metal.value + info.metalProductionBuffer.value == Catch::Approx(100.0f));
        REQUIRE(sim.getPlayer(enemy).metalProductionBuffer.value == Catch::Approx(0.0f));
    }

    TEST_CASE("a unit ignores an order to reclaim itself", "[reclaim]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto builderId = addBuilderUnit(sim, player, SimVector(100_ss, 0_ss, 100_ss), script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(builderId));

        sim.tick();

        const auto& builder = sim.getUnitState(builderId);
        REQUIRE(builder.isAlive());
        REQUIRE(builder.orders.empty());
        REQUIRE(builder.reclaimProgress == 0u);
    }

    TEST_CASE("computeFeatureReclaimWork", "[reclaim]")
    {
        SECTION("is the sum of the feature's metal and energy when it has no hit points")
        {
            REQUIRE(computeFeatureReclaimWork(makeFeatureDef("rock", 100u, 50u, true), 0u) == 150u);
        }

        SECTION("adds a quarter of the feature's remaining hit points")
        {
            REQUIRE(computeFeatureReclaimWork(makeFeatureDef("rock", 100u, 50u, true), 2000u) == 650u);
        }

        SECTION("is never zero, so worthless features can still be cleared")
        {
            REQUIRE(computeFeatureReclaimWork(makeFeatureDef("twig", 0u, 0u, true), 0u) == 1u);
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

    TEST_CASE("a factory cannot reclaim, however much worker time it has", "[reclaim]")
    {
        // The rule RWE was missing. The original's CanReclaimTarget
        // (0x489960) tests CanReclamate and never reads workertime at all,
        // so a factory -- which has a worker time so that it can build --
        // must refuse. Twenty-one units in the base game are in this
        // position: every factory, both air repair pads, both carriers and
        // CORSOLAR.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);

        auto rockDef = sim.featureDefinitions.insert(makeFeatureDef("rock", 100u, 50u, true));
        auto rockId = sim.addFeature(rockDef, 4, 4).value();
        auto rockPosition = sim.getFeature(rockId).position;

        auto factoryDef = makeBuilderDef(30u);
        factoryDef.canReclamate = false;
        sim.unitDefinitions["factory"] = factoryDef;
        auto factoryId = addUnitOfType(sim, "factory", player, rockPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(factoryId).inBuildStance = true;
        sim.getUnitState(factoryId).orders.push_back(ReclaimOrder(rockId));

        for (int i = 0; i < 20; ++i)
        {
            sim.tick();
        }

        // The rock survives and the order is dropped rather than left to
        // block the queue.
        REQUIRE(sim.tryGetFeature(rockId).has_value());
        REQUIRE(sim.getUnitState(factoryId).orders.empty());
    }

    TEST_CASE("a commander cannot be reclaimed", "[reclaim]")
    {
        // 0x48998F rejects a target whose definition can capture, and only
        // the two Commanders set CanCapture. So the observable rule is that
        // a Commander is the one unit you may not recycle.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);

        auto commanderDef = makeBuilderDef(30u);
        commanderDef.canCapture = true;
        sim.unitDefinitions["commander"] = commanderDef;
        auto commanderId = addUnitOfType(sim, "commander", player, SimVector(200_ss, 0_ss, 200_ss), script);

        auto builderId = addBuilderUnit(sim, player, SimVector(240_ss, 0_ss, 200_ss), script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(commanderId));

        auto startingHitPoints = sim.getUnitState(commanderId).hitPoints;
        for (int i = 0; i < 20; ++i)
        {
            sim.tick();
        }

        REQUIRE(sim.getUnitState(commanderId).hitPoints == startingHitPoints);
        REQUIRE(sim.getUnitState(builderId).orders.empty());
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
