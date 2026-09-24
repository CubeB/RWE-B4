#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/game/save_util.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
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

        /** Starts with empty stores, so anything gained came from reclaiming. */
        PlayerId addPlayerWithNothing(GameSimulation& sim)
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

        UnitDefinition makeBuilderDef(unsigned int workerTimePerTick)
        {
            UnitDefinition d{};
            d.builder = true;
            // A real construction unit names CanReclamate. The original gates
            // both reclaiming and repairing on it and never looks at
            // workertime (0x489960, and the bit 9 mirror at 0x4899CC).
            d.canReclamate = true;
            d.workerTimePerTick = workerTimePerTick;
            d.workerTime = workerTimePerTick * 30u;
            // A reclaim now spans several settles, and a player with no
            // storage has its stock clamped to nothing at each of them.
            d.metalStorage = Metal(10000.0f);
            d.energyStorage = Energy(10000.0f);
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.buildDistance = 100_ss;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId addBuilderUnit(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            sim.unitDefinitions["builder"] = makeBuilderDef(10u);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithNothing(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarId = addUnitOfType(sim, "solar", player, SimVector(100_ss, 0_ss, 100_ss), script);
        const auto& info = sim.getPlayer(player);

        SECTION("a complete unit pays back its full build cost as it is taken apart")
        {
            // Bites of a fifth of its hit points, each handing back a fifth
            // of what went into it.
            for (int i = 0; i < 4; ++i)
            {
                REQUIRE_FALSE(sim.reclaimUnitStep(solarId, player, 20u));
                REQUIRE(sim.getUnitState(solarId).isAlive());
            }
            REQUIRE(sim.getUnitState(solarId).hitPoints == 20u);
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(80.0f));

            REQUIRE(sim.reclaimUnitStep(solarId, player, 20u));
            REQUIRE(sim.getUnitState(solarId).isDead());
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(100.0f));
            REQUIRE(info.energyProductionBuffer.value == Catch::Approx(50.0f));

            // Reclaimed units leave no wreck.
            sim.tick();
            REQUIRE_FALSE(sim.tryGetUnitState(solarId).has_value());
            REQUIRE(sim.features.begin() == sim.features.end());
        }

        SECTION("a bite larger than what is left takes only what is left, and pays for only that")
        {
            REQUIRE_FALSE(sim.reclaimUnitStep(solarId, player, 70u));
            REQUIRE(sim.getUnitState(solarId).hitPoints == 30u);
            REQUIRE(sim.reclaimUnitStep(solarId, player, 1000u));
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(100.0f));
        }

        SECTION("a half-built unit only pays back the half that was invested")
        {
            sim.getUnitState(solarId).buildTimeCompleted = 75u;
            REQUIRE(sim.reclaimUnitStep(solarId, player, 100u));
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(50.0f));
            REQUIRE(info.energyProductionBuffer.value == Catch::Approx(25.0f));
        }

        SECTION("reports completion for a unit that is already dead")
        {
            sim.getUnitState(solarId).markAsDead();
            REQUIRE(sim.reclaimUnitStep(solarId, player, 30u));
            REQUIRE(info.metalProductionBuffer.value == Catch::Approx(0.0f));
        }

        SECTION("recycling your own base is not a loss, but being eaten is")
        {
            // Death cause 5 is the only one whose handler asks who did it:
            // 0x486899 tests the recorded killer against the victim's owner
            // and falls through to the Losses increment only when they differ.
            // Without that, a builder clearing its own obsolete metal
            // extractors would run the end-of-game Losses column up.
            // `info` is deliberately not used here: adding a player can
            // reallocate the player vector out from under a held reference.
            auto enemy = addPlayerWithNothing(sim);

            REQUIRE(sim.reclaimUnitStep(solarId, player, 100u));
            REQUIRE(sim.getPlayer(player).unitsLost == 0u);

            auto otherId = addUnitOfType(sim, "solar", player, SimVector(200_ss, 0_ss, 200_ss), script);
            REQUIRE(sim.reclaimUnitStep(otherId, enemy, 100u));
            REQUIRE(sim.getPlayer(player).unitsLost == 1u);
        }
    }

    TEST_CASE("a nanoframe that never finished is nobody's loss", "[reclaim]")
    {
        // Death cause 9 -- the build tick giving up on a frame (0x41BC49), or
        // its builder taking it back (0x402701). The dispatch at 0x48688C
        // accepts only causes 1 to 6, so this one never reaches the counter
        // table at all. A unit shot to pieces while it was still a frame is a
        // different cause -- 1, an ordinary weapon death -- and does count.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithNothing(sim);
        sim.unitDefinitions["solar"] = makeSolarDef();
        const auto& info = sim.getPlayer(player);

        auto abandoned = addUnitOfType(sim, "solar", player, SimVector(100_ss, 0_ss, 100_ss), script);
        sim.removeUnfinishedUnit(abandoned);
        REQUIRE(info.unitsLost == 0u);

        auto shot = addUnitOfType(sim, "solar", player, SimVector(200_ss, 0_ss, 200_ss), script);
        sim.quietlyKillUnit(shot);
        REQUIRE(info.unitsLost == 1u);
    }

    TEST_CASE("a builder with a reclaim order reclaims an enemy unit over successive ticks", "[reclaim]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithNothing(sim);
        auto enemy = addPlayerWithNothing(sim);

        sim.unitDefinitions["solar"] = makeSolarDef();
        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(sim, "solar", enemy, solarPosition, script);
        auto builderId = addBuilderUnit(sim, player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(solarId));

        // The builder's WorkerTime of 300 against a 100-hit-point, 100-metal
        // solar: 0x438650 gives 300 * 1 * 100 * 15 / (300 * 100) = 15 a bite,
        // and a bite lands every sixteen ticks. Nothing happens to the target
        // for the first fifteen, then it loses fifteen hit points at once:
        // that step is what the info panel's health bar shows as progress.
        // The count starts once the builder is in reach with its arm out, a
        // tick or two in, so find the first bite rather than assume its tick.
        int firstBite = 0;
        for (int t = 1; t <= 40; ++t)
        {
            sim.tick();
            if (sim.getUnitState(solarId).hitPoints != 100u)
            {
                firstBite = t;
                break;
            }
        }
        REQUIRE(firstBite >= 16);
        REQUIRE(sim.getUnitState(solarId).hitPoints == 85u);
        tick(sim, 15);
        REQUIRE(sim.getUnitState(solarId).hitPoints == 85u);
        sim.tick();
        REQUIRE(sim.getUnitState(solarId).hitPoints == 70u);

        // Seven bites in all, so 112 ticks from the start.
        for (int i = 0; i < 200 && sim.tryGetUnitState(solarId); ++i)
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

    TEST_CASE("a game saved between two reclaim bites comes back on the same count", "[reclaim][saveload]")
    {
        // The count to the next bite is hashed, so a save that dropped it
        // would load into a different game: the next bite up to fifteen
        // ticks late.
        auto script = makeEmptyCobScript();
        auto define = [&](GameSimulation& sim) {
            // Neither unit moves, and a building needs a yardmap to be put
            // back on the map.
            auto solar = makeSolarDef();
            solar.yardMap = Grid<YardMapCell>(2, 2, YardMapCell::Ground);
            sim.unitDefinitions["solar"] = solar;
            auto builder = makeBuilderDef(10u);
            builder.yardMap = Grid<YardMapCell>(2, 2, YardMapCell::Ground);
            sim.unitDefinitions["builder"] = builder;
            sim.unitScriptDefinitions["solar"] = *script;
            sim.unitScriptDefinitions["builder"] = *script;
        };

        GameSimulation simA(makeFlatTerrain(64, 64), 0u, 0, 0);
        define(simA);
        auto player = addPlayerWithNothing(simA);
        auto enemy = addPlayerWithNothing(simA);
        auto solarPosition = SimVector(200_ss, 0_ss, 200_ss);
        auto solarId = addUnitOfType(simA, "solar", enemy, solarPosition, script);
        auto builderId = addUnitOfType(simA, "builder", player, solarPosition + SimVector(40_ss, 0_ss, 0_ss), script);
        simA.getUnitState(builderId).inBuildStance = true;
        simA.getUnitState(builderId).orders.push_back(ReclaimOrder(solarId));
        while (simA.getUnitState(solarId).hitPoints == 100u)
        {
            simA.tick();
        }
        tick(simA, 7);

        auto saved = saveSimulationToJson(simA);
        GameSimulation simB(makeFlatTerrain(64, 64), 0u, 0, 0);
        define(simB);
        loadSimulationFromJson(saved, simB);
        REQUIRE(computeHashOf(simA) == computeHashOf(simB));

        for (int i = 0; i < 40; ++i)
        {
            simA.tick();
            simB.tick();
            INFO("tick " << i << " after the load");
            REQUIRE(computeHashOf(simA) == computeHashOf(simB));
        }
    }

    TEST_CASE("a unit ignores an order to reclaim itself", "[reclaim]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithNothing(sim);
        auto builderId = addBuilderUnit(sim, player, SimVector(100_ss, 0_ss, 100_ss), script);
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(builderId));

        sim.tick();

        const auto& builder = sim.getUnitState(builderId);
        REQUIRE(builder.isAlive());
        REQUIRE(builder.orders.empty());
        REQUIRE(builder.hitPoints == 100u);
    }

    TEST_CASE("the size of a reclaim bite is 0x438650's", "[reclaim]")
    {
        // trunc(workerTime * ((kills + 5) / 5) * maxDamage * 15 / (300 * max(buildCostMetal, 10))), at least 1.
        // A 300-workertime builder on a 1000-hit-point, 150-metal unit: 100 a
        // bite, ten bites, 160 ticks.
        REQUIRE(computeUnitReclaimStep(300u, 0u, 1000u, Metal(150.0f)) == 100u);

        // Veterancy is integer fifths: four kills change nothing, five double it.
        REQUIRE(computeUnitReclaimStep(300u, 4u, 1000u, Metal(150.0f)) == 100u);
        REQUIRE(computeUnitReclaimStep(300u, 5u, 1000u, Metal(150.0f)) == 200u);

        // The cost is floored at ten, so a free unit is not undone in one bite.
        REQUIRE(computeUnitReclaimStep(300u, 0u, 100u, Metal(0.0f)) == 150u);

        // And a bite is never nothing.
        REQUIRE(computeUnitReclaimStep(1u, 0u, 1u, Metal(10000.0f)) == 1u);

        // MaxDamage cancels out of the total: whatever the hit points, the
        // unit lasts 300 * buildCostMetal / workerTime ticks of bites.
        REQUIRE(computeUnitReclaimStep(300u, 0u, 4000u, Metal(150.0f)) == 400u);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithNothing(sim);
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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithNothing(sim);

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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithNothing(sim);

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
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayerWithNothing(sim);

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
