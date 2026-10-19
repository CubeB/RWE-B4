#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/save_util.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        struct FireWorld
        {
            GameSimulation sim{makeFlatTerrain(256, 256), 0u, 0, 0};
            FeatureDefinitionId tree;
            FeatureDefinitionId burnt;
            FeatureDefinitionId model;
            FeatureDefinitionId smudge;

            FireWorld()
            {
                // totala1.hpi green/TREES.TDF Tree1 and Tree1Dead, and the
                // 96 two-update Leafyburn01 frames (BT-44/255/257/258).
                FeatureDefinition d{};
                d.name = "Tree1Dead";
                d.footprintX = d.footprintZ = 1;
                d.height = 40_ss;
                d.blocking = d.reclaimable = true;
                d.energy = 50;
                burnt = sim.featureDefinitions.insert(d);
                // green/SMUDGES.TDF Smudge01: slotless, nonblocking, zero HP.
                d = FeatureDefinition{};
                d.name = "Smudge01";
                d.footprintX = d.footprintZ = 1;
                smudge = sim.featureDefinitions.insert(d);
                sim.featureDefinitions.get(smudge).featureDead = smudge;
                d = sim.featureDefinitions.get(burnt);
                d.name = "Tree1";
                d.flamable = true;
                d.energy = 250;
                d.featureBurnt = burnt;
                d.burnMin = 5;
                d.burnMax = 15;
                d.sparkTime = 5;
                d.spreadChance = 90;
                d.burnWeapon = "TreeBurn";
                d.burnFrameTicks.assign(96, 2);
                d.deathFrameTicks.assign(20, 3);
                d.reclaimFrameTicks.assign(15, 2);
                d.featureDead = d.featureReclamate = smudge;
                tree = sim.featureDefinitions.insert(d);
                // Shipped dragon's-teeth model footprint; other media is inert.
                d = FeatureDefinition{};
                d.name = "DragonsTeeth";
                d.footprintX = d.footprintZ = 2;
                d.height = 20_ss;
                d.damage = 4000;
                d.hasModel = d.blocking = true;
                model = sim.featureDefinitions.insert(d);
                WeaponDefinition burn{};
                burn.damageRadius = 32_ss;
                burn.damage["DEFAULT"] = 100;
                burn.edgeEffectiveness = SimScalar(0.5f);
                burn.damagesFeatures = false; // fires.tdf TreeBurn unitsonly=1.
                sim.weaponDefinitions["TREEBURN"] = burn;
                // Relevant shipped ARMSOLAR fields; inert mesh/COB isolate damage.
                UnitDefinition solar{};
                solar.objectName = "ARMSOLAR";
                solar.maxHitPoints = 326;
                solar.buildTime = 2495;
                solar.sightDistance = 210;
                solar.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5, 5, 10, 0, 0, 0};
                solar.yardMap = Grid<YardMapCell>(5, 5, YardMapCell::Ground);
                sim.unitDefinitions["ARMSOLAR"] = solar;
                sim.unitModelDefinitions["ARMSOLAR"] = createUnitModelDefinition(10_ss, {});
                sim.unitScriptDefinitions["ARMSOLAR"] = *makeEmptyCobScript();
                sim.rng.seed(7);
            }

            FeatureId plant(int x, int y)
            {
                auto id = sim.addFeature(tree, x, y);
                REQUIRE(id);
                return *id;
            }

            UnitId solar(PlayerId owner, SimVector position)
            {
                auto id = sim.trySpawnCompletedUnit("ARMSOLAR", owner, position, std::nullopt);
                REQUIRE(id);
                return *id;
            }

            void step(unsigned int updates, SimVector wind = SimVector(0_ss, 0_ss, 0_ss))
            {
                // Isolate the documented feature phase; other tick phases have
                // their own draws and an existing documented order divergence.
                for (unsigned int i = 0; i < updates; ++i)
                {
                    sim.gameTime += GameTime(1);
                    sim.updateFeatureAnimations(wind);
                }
            }

            FeatureId fillModels(unsigned int count)
            {
                FeatureId first{};
                for (unsigned int i = 0; i < count; ++i)
                {
                    const auto id = sim.addFeature(model, static_cast<int>(i % 32) * 2, static_cast<int>(i / 32) * 2);
                    REQUIRE(id);
                    if (i == 0)
                    {
                        first = *id;
                    }
                }
                return first;
            }
        };
    }

    TEST_CASE("Tree1 burns for its GAF updates and spends one half-to-full spark timer", "[burning]")
    {
        FireWorld w;
        auto source = w.plant(64, 64);
        auto corner = w.plant(67, 67);
        auto beyond = w.plant(68, 64);
        w.sim.igniteFeature(source);
        REQUIRE(w.sim.getFeature(source).animationTicksRemaining == 192);
        REQUIRE(w.sim.getFeature(source).burnSparkTicksRemaining == 97); // seed7: 75 + 337897%75.
        auto rngAfterIgnition = w.sim.rng;
        w.sim.igniteFeature(source);
        REQUIRE(w.sim.rng == rngAfterIgnition);
        w.step(96);
        REQUIRE_FALSE(w.sim.getFeature(corner).burningUntil);
        w.step(1);
        // Second seed7 draw is 58%100: succeeds against the candidate's 90.
        REQUIRE(w.sim.getFeature(corner).burningUntil);
        REQUIRE(w.sim.getFeature(corner).animationTicksRemaining == 192); // no same-pass cascade.
        REQUIRE_FALSE(w.sim.getFeature(beyond).burningUntil);
        REQUIRE(w.sim.getFeature(source).burnSparkTicksRemaining == 0);
        REQUIRE(w.sim.getFeature(source).nextSpark == GameTime(0));
        w.step(95);
        REQUIRE_FALSE(w.sim.tryGetFeature(source));
        bool foundBurnt = false;
        for (const auto& [id, f] : w.sim.features)
        {
            foundBurnt |= f.featureName == w.burnt;
        }
        REQUIRE(foundBurnt);
    }

    TEST_CASE("burn animation completion beats an equal spark countdown", "[burning]")
    {
        FireWorld w;
        auto source = w.plant(64, 64);
        auto neighbour = w.plant(65, 64);
        w.sim.igniteFeature(source);
        w.sim.getFeature(source).burnSparkTicksRemaining = 192;
        w.step(192);
        REQUIRE_FALSE(w.sim.tryGetFeature(source));
        REQUIRE_FALSE(w.sim.getFeature(neighbour).burningUntil);
    }

    TEST_CASE("the feature phase traverses existing fires newest first", "[burning]")
    {
        FireWorld w;
        auto first = w.plant(32, 32);
        auto nearFirst = w.plant(33, 32);
        auto second = w.plant(64, 64);
        auto nearSecond = w.plant(65, 64);
        w.sim.igniteFeature(first);
        w.sim.igniteFeature(second);
        w.sim.getFeature(first).burnSparkTicksRemaining = 1;
        w.sim.getFeature(second).burnSparkTicksRemaining = 1;
        w.step(1);
        REQUIRE(w.sim.getFeature(nearFirst).burningUntil);
        REQUIRE(w.sim.getFeature(nearSecond).burningUntil);
        REQUIRE(w.sim.getFeature(nearSecond).animationOrder < w.sim.getFeature(nearFirst).animationOrder);
        REQUIRE(w.sim.getFeature(nearFirst).animationTicksRemaining == 192);
    }

    TEST_CASE("burn damage hurts every owner once without an attacker bonus", "[burning]")
    {
        FireWorld w;
        auto source = w.plant(64, 64);
        auto leftOwner = addPlayer(w.sim);
        auto rightOwner = addPlayer(w.sim);
        const auto position = w.sim.getFeature(source).position;
        auto left = w.solar(leftOwner, position + SimVector(-56_ss, 0_ss, 0_ss));
        auto right = w.solar(rightOwner, position + SimVector(56_ss, 0_ss, 0_ss));
        w.sim.getUnitState(left).kills = 20;
        w.sim.getUnitState(right).lastAttacker = left;
        w.sim.rng.seed(7);
        w.sim.igniteFeature(source);
        w.step(97);
        // Grid placement leaves the victim's box 24 units from the fire.
        REQUIRE(rweSqrt(w.sim.createBoundingBox(w.sim.getUnitState(right)).distanceSquared(position)) == 24_ss);
        // floor(100*(.5+.5*(1-24/32)^2)) = 53.
        REQUIRE(w.sim.getUnitState(right).hitPoints == 273);
        REQUIRE(w.sim.getUnitState(left).hitPoints < 326);
        REQUIRE(w.sim.getUnitState(right).lastAttacker == left);
        REQUIRE(w.sim.getUnitState(right).lastAttackedAt == GameTime(0));
        const auto health = w.sim.getUnitState(right).hitPoints;
        w.step(94);
        REQUIRE(w.sim.getUnitState(right).hitPoints == health);
    }

    TEST_CASE("wind retries a failed square roll on entering the negative neighbour", "[burning]")
    {
        for (bool negative : {false, true})
        {
            FireWorld w;
            // Shipped Tree4 chance30 and 192-update timing. The candidate
            // retains its own chance, independent of the source Tree1's 90.
            auto tree4 = w.sim.featureDefinitions.get(w.tree);
            tree4.name = "Tree4";
            tree4.spreadChance = 30;
            auto definition = w.sim.featureDefinitions.insert(tree4);
            auto source = w.plant(64, 64);
            auto candidate = w.sim.addFeature(definition, 63, 64);
            REQUIRE(candidate);
            w.sim.igniteFeature(source);
            w.sim.getFeature(source).burnSparkTicksRemaining = 1;
            // BT-255's -4000/+4000 fixed-component example. Square roll58
            // fails; negative wind retries with14. Positive wind never exits.
            w.step(1, SimVector(simScalarFromFixed(negative ? -4000 : 4000), 0_ss, 0_ss));
            REQUIRE(w.sim.getFeature(*candidate).burningUntil.has_value() == negative);
        }
    }

    TEST_CASE("a disabled spark timer consumes no timer draw and never spreads", "[burning]")
    {
        FireWorld w;
        w.sim.featureDefinitions.get(w.tree).sparkTime = 0; // documented missing-key boundary.
        auto source = w.plant(64, 64);
        auto neighbour = w.plant(65, 64);
        auto before = w.sim.rng;
        w.sim.igniteFeature(source);
        REQUIRE(w.sim.rng == before);
        w.step(191);
        REQUIRE_FALSE(w.sim.getFeature(neighbour).burningUntil);
        REQUIRE(w.sim.getFeature(source).animationTicksRemaining == 1);
    }

    TEST_CASE("ignition refuses a full stateful pool without drawing and deletion releases capacity", "[burning]")
    {
        FireWorld w;
        auto source = w.plant(240, 240);
        std::optional<FeatureId> first;
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                auto placed = w.sim.addFeature(w.model, x * 2, y * 2);
                REQUIRE(placed);
                if (!first)
                {
                    first = placed;
                }
            }
        }
        auto before = w.sim.rng;
        w.sim.igniteFeature(source);
        REQUIRE_FALSE(w.sim.getFeature(source).burningUntil);
        REQUIRE(w.sim.rng == before);
        w.sim.deleteFeature(*first);
        w.sim.igniteFeature(source);
        REQUIRE(w.sim.getFeature(source).burningUntil);
        REQUIRE(w.sim.getFeature(source).burnSparkTicksRemaining == 97);
    }

    TEST_CASE("lethal burn credit uses the previous attacker if one exists", "[burning]")
    {
        for (bool priorAttacker : {false, true})
        {
            FireWorld w;
            auto source = w.plant(64, 64);
            auto attackerOwner = addPlayer(w.sim);
            auto victimOwner = addPlayer(w.sim);
            const auto position = w.sim.getFeature(source).position;
            auto attacker = w.solar(attackerOwner, position + SimVector(-200_ss, 0_ss, 0_ss));
            auto victim = w.solar(victimOwner, position + SimVector(56_ss, 0_ss, 0_ss));
            w.sim.getUnitState(victim).hitPoints = 1; // lethal boundary state, not a definition change.
            if (priorAttacker)
            {
                w.sim.getUnitState(victim).lastAttacker = attacker;
            }
            w.sim.rng.seed(7);
            w.sim.igniteFeature(source);
            w.step(97);
            REQUIRE(w.sim.getUnitState(victim).isDead());
            REQUIRE(w.sim.getUnitState(attacker).kills == (priorAttacker ? 1u : 0u));
            REQUIRE(w.sim.getPlayer(attackerOwner).unitsKilled == (priorAttacker ? 1u : 0u));
        }
    }

    TEST_CASE("fire countdowns and order survive save load and change the hash", "[burning]")
    {
        FireWorld w;
        auto first = w.plant(32, 32);
        auto second = w.plant(64, 64);
        w.sim.igniteFeature(first);
        w.sim.igniteFeature(second);
        w.step(46);
        auto before = computeHashOf(w.sim);
        std::swap(w.sim.getFeature(first).animationOrder, w.sim.getFeature(second).animationOrder);
        REQUIRE(computeHashOf(w.sim) != before);
        std::swap(w.sim.getFeature(first).animationOrder, w.sim.getFeature(second).animationOrder);
        auto saved = saveSimulationToJson(w.sim);
        FireWorld restored;
        loadSimulationFromJson(saved, restored.sim);
        REQUIRE(computeHashOf(restored.sim) == before);
        REQUIRE(saveSimulationToJson(restored.sim) == saved);
        w.step(146);
        restored.step(146);
        REQUIRE(computeHashOf(w.sim) == computeHashOf(restored.sim));
        REQUIRE(dumpJson(w.sim) == dumpJson(restored.sim));
        saved["version"] = 2;
        for (auto& feature : saved["features"])
        {
            feature.erase("animationTicksRemaining");
        }
        FireWorld legacy;
        REQUIRE_THROWS_AS(loadSimulationFromJson(saved, legacy.sim), std::runtime_error);
    }

    TEST_CASE("firestarter is a narrowed integer byte rather than a percentage", "[burning][weapontdf]")
    {
        for (const auto& [text, expected] : std::vector<std::pair<std::string, unsigned int>>{
                 {"0.9", 0}, {"1.9", 1}, {"70", 70}, {"-1.9", 255}, {"256.9", 0}, {"257", 1}})
        {
            INFO(text);
            auto tdf = parseTdfFromString("[BURN] { firestarter=" + text + "; }");
            auto weapon = parseWeaponBlock(tdf.findBlock("BURN")->get());
            REQUIRE(weapon.fireStarter == expected);
        }
    }

    TEST_CASE("death animation retains blocking and health until its primary expires", "[burning][feature-cap]")
    {
        FireWorld w;
        const auto id = w.plant(64, 64);
        const auto before = w.sim.rng;
        w.sim.damageFeature(id, 40); // SMALL_BUILDINGEX default damage.
        REQUIRE(w.sim.getFeature(id).animation == FeatureAnimation::Death);
        REQUIRE(w.sim.getFeature(id).animationTicksRemaining == 60);
        REQUIRE(w.sim.countStatefulFeatures() == 1);
        REQUIRE(w.sim.occupiedGrid.get(64, 64).featureId == id);
        REQUIRE(w.sim.getFeatureDefinition(w.sim.getFeature(id).featureName).blocking);
        w.sim.damageFeature(id, 40);
        w.sim.igniteFeature(id);
        REQUIRE(w.sim.getFeature(id).animationTicksRemaining == 60);
        REQUIRE(w.sim.rng == before);
        REQUIRE(w.sim.events.empty()); // Death has no allocation sound.
        w.step(59);
        REQUIRE(w.sim.getFeature(id).animationTicksRemaining == 1);
        w.step(1);
        REQUIRE(w.sim.countStatefulFeatures() == 0);
        const auto successor = w.sim.occupiedGrid.get(64, 64).featureId;
        REQUIRE(successor);
        REQUIRE(w.sim.getFeature(*successor).featureName == w.smudge);
    }

    TEST_CASE("reclaim retains its animation for30 updates with credit before transition", "[burning][feature-cap]")
    {
        FireWorld w;
        const auto id = w.plant(64, 64);
        const auto owner = addPlayer(w.sim);
        REQUIRE(w.sim.reclaimFeature(id, owner, 250)); // Tree1's full work/value.
        REQUIRE(w.sim.getPlayer(owner).energyProductionBuffer.value == 250.0f);
        REQUIRE(w.sim.getFeature(id).animation == FeatureAnimation::Reclaim);
        REQUIRE(w.sim.getFeature(id).animationTicksRemaining == 30);
        REQUIRE(w.sim.events.empty());
        w.step(29);
        REQUIRE(w.sim.occupiedGrid.get(64, 64).featureId == id);
        w.step(1);
        REQUIRE(w.sim.countStatefulFeatures() == 0);
        REQUIRE(w.sim.getFeature(*w.sim.occupiedGrid.get(64, 64).featureId).featureName == w.smudge);
    }

    TEST_CASE("full model pool refuses sprite death and reclaim animations without rollback of credit", "[burning][feature-cap]")
    {
        FireWorld w;
        const auto id = w.plant(240, 240);
        w.fillModels(2048);
        REQUIRE(w.sim.countStatefulFeatures() == 2048);
        REQUIRE_FALSE(w.sim.addFeature(w.model, 242, 240));
        const auto before = w.sim.rng;
        w.sim.damageFeature(id, 40);
        REQUIRE(w.sim.getFeature(id).hitPoints == 0);
        REQUIRE(w.sim.getFeature(id).animation == FeatureAnimation::None);
        const auto owner = addPlayer(w.sim);
        REQUIRE(w.sim.reclaimFeature(id, owner, 250));
        REQUIRE(w.sim.getPlayer(owner).energyProductionBuffer.value == 250.0f);
        REQUIRE(w.sim.getFeature(id).animation == FeatureAnimation::None);
        REQUIRE(w.sim.rng == before);
    }

    TEST_CASE("model placement clears before allocation and can reuse a removed fire slot", "[burning][feature-cap]")
    {
        for (bool removeFire : {false, true})
        {
            FireWorld w;
            const auto id = w.plant(240, 240);
            w.fillModels(removeFire ? 2047 : 2048);
            if (removeFire)
            {
                w.sim.igniteFeature(id);
            }
            REQUIRE(w.sim.countStatefulFeatures() == 2048);
            const auto placed = w.sim.addFeature(w.model, 240, 240);
            REQUIRE(placed.has_value() == removeFire);
            if (placed)
            {
                REQUIRE(w.sim.getFeature(*placed).featureName == w.model);
            }
            else
            {
                REQUIRE_FALSE(w.sim.occupiedGrid.get(240, 240).featureId);
            }
            REQUIRE(w.sim.countStatefulFeatures() == 2048);
        }
    }

    TEST_CASE("normal placement preserves protected occupants without restoring earlier removals", "[burning][feature-cap]")
    {
        FireWorld w;
        w.plant(240, 240);
        // totala1.hpi green/STEAMVENTS.TDF Geothermal.
        FeatureDefinition d{};
        d.name = "Geothermal";
        d.footprintX = d.footprintZ = 1;
        d.geothermal = d.indestructible = true;
        const auto protectedId = w.sim.addFeature(w.sim.featureDefinitions.insert(d), 241, 240);
        REQUIRE(protectedId);
        REQUIRE_FALSE(w.sim.addFeature(w.model, 240, 240));
        REQUIRE_FALSE(w.sim.occupiedGrid.get(240, 240).featureId);
        REQUIRE(w.sim.occupiedGrid.get(241, 240).featureId == protectedId);
    }

    TEST_CASE("replacement releases its model slot before requesting another at capacity", "[burning][feature-cap]")
    {
        FireWorld w;
        const auto first = w.fillModels(2048);
        const auto replacement = w.sim.replaceFeature(first, w.model);
        REQUIRE(replacement);
        REQUIRE(w.sim.countStatefulFeatures() == 2048);
        REQUIRE(w.sim.getFeature(*replacement).featureName == w.model);
        const auto sprite = w.plant(240, 240);
        REQUIRE_FALSE(w.sim.replaceFeature(sprite, w.model));
        REQUIRE_FALSE(w.sim.occupiedGrid.get(240, 240).featureId);
    }

    TEST_CASE("death and reclaim state survives save load and its kind changes the hash", "[burning][feature-cap][saveload]")
    {
        for (auto state : {FeatureAnimation::Death, FeatureAnimation::Reclaim})
        {
            FireWorld w;
            const auto id = w.plant(64, 64);
            REQUIRE(w.sim.startFeatureAnimation(id, state));
            w.step(7);
            const auto before = computeHashOf(w.sim);
            w.sim.getFeature(id).animation = state == FeatureAnimation::Death ? FeatureAnimation::Reclaim : FeatureAnimation::Death;
            REQUIRE(computeHashOf(w.sim) != before);
            w.sim.getFeature(id).animation = state;
            const auto saved = saveSimulationToJson(w.sim);
            FireWorld restored;
            loadSimulationFromJson(saved, restored.sim);
            REQUIRE(saveSimulationToJson(restored.sim) == saved);
            REQUIRE(computeHashOf(restored.sim) == before);
            const auto remaining = w.sim.getFeature(id).animationTicksRemaining;
            w.step(remaining);
            restored.step(remaining);
            REQUIRE(computeHashOf(w.sim) == computeHashOf(restored.sim));
            REQUIRE(dumpJson(w.sim) == dumpJson(restored.sim));
        }
    }

    TEST_CASE("successful ignition alone requests treeburn at the origin corner", "[burning][feature-cap]")
    {
        FireWorld w;
        const auto id = w.plant(64, 64);
        w.sim.igniteFeature(id);
        w.sim.igniteFeature(id);
        REQUIRE(w.sim.events.size() == 1);
        const auto event = std::get_if<FeatureIgnitedEvent>(&w.sim.events.front());
        REQUIRE(event);
        REQUIRE((event->position == w.sim.terrain.heightmapIndexToWorldCorner(64, 64)));
        w.step(97);
        REQUIRE(w.sim.events.size() == 1); // Source spread requests no new sound.
    }

    TEST_CASE("failed firestarter ignition replaces ordinary feature damage", "[burning]")
    {
        FireWorld w;
        auto source = w.plant(64, 64);
        w.sim.featureDefinitions.get(w.tree).burnFrameTicks.clear();
        const auto position = w.sim.getFeature(source).position;
        // rev31 WEAPONS.TDF ARM_DISINTEGRATOR's relevant fields.
        WeaponDefinition weapon{};
        weapon.fireStarter = 70;
        weapon.damagesFeatures = true;
        weapon.damageRadius = 24_ss;
        weapon.damage["DEFAULT"] = 30000;
        w.sim.weaponDefinitions["ARM_DISINTEGRATOR"] = weapon;
        Projectile p{};
        p.weaponType = "ARM_DISINTEGRATOR";
        p.owner = PlayerId(10);
        p.position = position;
        p.damage = weapon.damage;
        auto before = w.sim.rng;
        w.sim.applyDamageInRadius(position, weapon.damageRadius, p);
        REQUIRE(w.sim.tryGetFeature(source));
        REQUIRE_FALSE(w.sim.getFeature(source).burningUntil);
        REQUIRE(w.sim.rng == before);
    }
}
