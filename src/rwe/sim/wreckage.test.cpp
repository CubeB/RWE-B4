#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/Projectile.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * A solid, blocking feature in the shape of a TA rock or unit wreck:
         * worth something to reclaim, and with hit points of its own.
         */
        FeatureDefinition makeWreckDef(const std::string& name, unsigned int metal, unsigned int hitPoints)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 20_ss;
            d.blocking = true;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = metal;
            d.hitDensity = 100;
            d.damage = hitPoints;
            return d;
        }

        PlayerId addPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("salvager"),
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

        /** A shell that does `damage` points inside `radius`, centred wherever you drop it. */
        Projectile makeShell(const SimVector& position, unsigned int damage, SimScalar radius)
        {
            Projectile p{};
            p.weaponType = "TESTSHELL";
            p.owner = PlayerId(0);
            p.position = position;
            p.previousPosition = position;
            p.origin = position;
            p.damage.insert_or_assign("DEFAULT", damage);
            p.damageRadius = radius;
            return p;
        }
    }


    TEST_CASE("wreckage dropped in water sinks to the sea bed", "[wreckage]")
    {
        // The original spawns a corpse at the height its unit died at, then
        // gives it a fixed downward velocity when the ground beneath is at or
        // below sea level (0x486416), spending it a tick at a time until the
        // wreck reaches the bottom. Nothing floats: a play-test remembered a
        // hovercraft wreck sitting on the surface, but that was RWE's own
        // behaviour -- it had no feature physics at all -- rather than the
        // original's.
        auto makeSeaTerrain = [](unsigned char groundHeight, SimScalar seaLevel) {
            Grid<unsigned char> heights(32, 32, groundHeight);
            return MapTerrain(std::move(heights), seaLevel);
        };

        SECTION("a wreck over water is given a sink speed and reaches the bottom")
        {
            // Ground at 0, sea level at 60: sixty units of water to fall through.
            GameSimulation sim(makeSeaTerrain(0, 60_ss), 0u, 0, 0);
            sim.featureDefinitions.insert(makeWreckDef("HULK", 100, 1000));
            sim.featureNameIndex.insert_or_assign("HULK", FeatureDefinitionId(0));

            auto surface = SimVector(200_ss, 60_ss, 200_ss);
            sim.trySpawnFeature("HULK", surface, SimAngle(0), false);
            REQUIRE(sim.features.begin() != sim.features.end());

            auto featureId = sim.features.begin()->first;
            REQUIRE(sim.features.tryGet(featureId)->get().velocity.y < 0_ss);

            // 0.175 a tick through sixty units is about 343 ticks; give it room.
            for (int i = 0; i < 600; ++i)
            {
                sim.updateFallingFeatures();
            }

            const auto& landed = sim.features.tryGet(featureId)->get();
            REQUIRE(landed.velocity.y == 0_ss);
            REQUIRE(landed.position.y == sim.terrain.getHeightAt(landed.position.x, landed.position.z));
        }

        SECTION("a wreck on dry ground never moves")
        {
            // Ground well above the water: the corpse spawner leaves the
            // velocity alone, and the sweep skips it for ever after.
            GameSimulation sim(makeSeaTerrain(200, 0_ss), 0u, 0, 0);
            sim.featureDefinitions.insert(makeWreckDef("HULK", 100, 1000));
            sim.featureNameIndex.insert_or_assign("HULK", FeatureDefinitionId(0));

            auto ground = sim.terrain.getHeightAt(200_ss, 200_ss);
            sim.trySpawnFeature("HULK", SimVector(200_ss, ground, 200_ss), SimAngle(0), false);

            auto featureId = sim.features.begin()->first;
            auto before = sim.features.tryGet(featureId)->get().position;
            REQUIRE(sim.features.tryGet(featureId)->get().velocity.y == 0_ss);

            for (int i = 0; i < 60; ++i)
            {
                sim.updateFallingFeatures();
            }

            REQUIRE(sim.features.tryGet(featureId)->get().position.y == before.y);
        }

        SECTION("a unit flagged isfeature leaves its wreck on the surface")
        {
            // The floating dragon's teeth. The original skips them by that
            // flag, which is the clearest evidence the sink rule is deliberate.
            GameSimulation sim(makeSeaTerrain(0, 60_ss), 0u, 0, 0);
            sim.featureDefinitions.insert(makeWreckDef("FLOATINGTEETH", 100, 1000));
            sim.featureNameIndex.insert_or_assign("FLOATINGTEETH", FeatureDefinitionId(0));

            auto surface = SimVector(200_ss, 60_ss, 200_ss);
            sim.trySpawnFeature("FLOATINGTEETH", surface, SimAngle(0), true);

            auto featureId = sim.features.begin()->first;
            REQUIRE(sim.features.tryGet(featureId)->get().velocity.y == 0_ss);

            for (int i = 0; i < 120; ++i)
            {
                sim.updateFallingFeatures();
            }

            REQUIRE(sim.features.tryGet(featureId)->get().position.y == surface.y);
        }
    }

    TEST_CASE("feature hit points", "[wreckage]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);

        // Modelled on TA's greenworld [Rock]: metal=100, damage=2000,
        // featuredead=Rock1a (a more battered rock worth the same metal).
        auto rubbleDef = sim.featureDefinitions.insert(makeWreckDef("rubble", 100u, 0u));
        auto rockDef = sim.featureDefinitions.insert(makeWreckDef("rock", 100u, 2000u));
        sim.featureDefinitions.get(rockDef).featureDead = rubbleDef;

        SECTION("a feature is placed at the hit points its definition declares")
        {
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            REQUIRE(sim.getFeature(rockId).hitPoints == 2000u);
        }

        SECTION("a blast chips a feature, and enough of them break it apart")
        {
            // Force-attacking a wreck field to clear a lane is a standing
            // part of play. A feature's `damage` key is its hit points; a
            // blast pays the same falloff-scaled damage a unit would take,
            // and at zero the feature breaks down to its featureDead form.
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto position = sim.getFeature(rockId).position;

            sim.doProjectileImpact(makeShell(position, 500u, 64_ss), ImpactType::Normal);
            REQUIRE(sim.tryGetFeature(rockId).has_value());
            REQUIRE(sim.getFeature(rockId).hitPoints < 2000u);
            REQUIRE(sim.getFeature(rockId).hitPoints > 0u);

            // Far more damage than it has left: it goes, and the rubble
            // stands in its place.
            sim.doProjectileImpact(makeShell(position, 100000u, 64_ss), ImpactType::Normal);
            bool foundRubble = false;
            for (const auto& [_, feature] : sim.features)
            {
                if (feature.featureName == rubbleDef)
                {
                    foundRubble = true;
                }
                REQUIRE(feature.featureName != rockDef);
            }
            REQUIRE(foundRubble);
        }

        SECTION("a weapon whose data exempts it never touches a feature")
        {
            // The flag is still honoured, so a mod can declare a weapon that
            // leaves scenery alone. Nothing in the shipped data sets it any
            // more: it used to be derived from the render type, on the lore
            // that beams cannot hurt wreckage, and that derivation is gone --
            // see parseWeaponDefinition for why the binary does not support
            // it, and the case below for what a laser does now.
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto position = sim.getFeature(rockId).position;

            auto shell = makeShell(position, 100000u, 64_ss);
            sim.weaponDefinitions[shell.weaponType].damagesFeatures = false;
            sim.doProjectileImpact(shell, ImpactType::Normal);

            REQUIRE(sim.tryGetFeature(rockId).has_value());
            REQUIRE(sim.getFeature(rockId).hitPoints == 2000u);
        }

        SECTION("a laser blasts a rock like anything else")
        {
            // Render type 0 is one of the two laser draws and used to switch
            // feature damage off. In the original it is a drawing attribute
            // and gates nothing about damage, so a laser clears a lane like
            // any other gun -- which is what stops two armies deadlocking
            // against a wall of corpses neither can mark.
            auto tdf = parseTdfFromString("[LASER]\n{\nrendertype=0;\nrange=100;\n}\n");
            auto block = tdf.findBlock("LASER");
            REQUIRE(block.has_value());
            REQUIRE(parseWeaponDefinition(parseWeaponBlock(block->get())).damagesFeatures);

            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto position = sim.getFeature(rockId).position;

            auto shell = makeShell(position, 100000u, 64_ss);
            sim.doProjectileImpact(shell, ImpactType::Normal);

            // Breaking it down replaces it, so the old id is gone: walk the
            // list the way the case above does.
            bool foundRubble = false;
            for (const auto& [_, feature] : sim.features)
            {
                if (feature.featureName == rubbleDef)
                {
                    foundRubble = true;
                }
                REQUIRE(feature.featureName != rockDef);
            }
            REQUIRE(foundRubble);
        }
    }

    TEST_CASE("reclaim work scales with what is left of the feature", "[wreckage]")
    {
        SECTION("a substantial rock takes far longer to clear than a bush")
        {
            // Straight out of TA's greenworld data: [Rock] is metal=100 damage=2000,
            // [Shrub1] is energy=20 with no damage key at all.
            auto rock = makeWreckDef("rock", 100u, 2000u);
            auto bush = makeWreckDef("bush", 0u, 0u);
            bush.metal = 0u;
            bush.energy = 20u;

            REQUIRE(computeFeatureReclaimWork(rock, rock.damage) == 600u);
            REQUIRE(computeFeatureReclaimWork(bush, bush.damage) == 20u);
        }

        SECTION("fewer hit points means less to clear away")
        {
            auto rock = makeWreckDef("rock", 100u, 2000u);
            REQUIRE(computeFeatureReclaimWork(rock, 400u) < computeFeatureReclaimWork(rock, 2000u));
        }

        SECTION("work is never zero, so valueless scenery can still be cleared")
        {
            auto smudge = makeWreckDef("smudge", 0u, 0u);
            REQUIRE(computeFeatureReclaimWork(smudge, 0u) == 1u);
        }
    }

    TEST_CASE("a feature yields its full metal when reclaimed", "[wreckage]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto rockDef = sim.featureDefinitions.insert(makeWreckDef("rock", 100u, 2000u));
        auto rockId = sim.addFeature(rockDef, 8, 8).value();

        // Nothing has worn the rock down -- weapons cannot -- so it is reclaimed
        // at the full hit points it was placed with.
        REQUIRE(sim.getFeature(rockId).hitPoints == 2000u);

        // 100 metal + 2000/4 hit points = 600 work, taken in thirty bites of 20.
        for (int i = 0; i < 29; ++i)
        {
            REQUIRE_FALSE(sim.reclaimFeature(rockId, player, 20u));
        }
        REQUIRE(sim.reclaimFeature(rockId, player, 20u));

        REQUIRE_FALSE(sim.tryGetFeature(rockId).has_value());

        const auto& info = sim.getPlayer(player);
        REQUIRE(info.metal.value + info.metalProductionBuffer.value == Catch::Approx(100.0f));
    }
}
