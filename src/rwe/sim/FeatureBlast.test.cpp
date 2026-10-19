#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/FeatureBlast.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        struct BlastWorld
        {
            GameSimulation sim;
            FeatureDefinitionId rock;
            FeatureDefinitionId model;
            FeatureDefinitionId tree;

            explicit BlastWorld(MapTerrain terrain = makeFlatTerrain(256, 256))
                : sim(std::move(terrain), 0u, 0, 0)
            {
                // totala1.hpi features/all worlds/dragonsteeth.tdf.
                // Only fields relevant to reach/damage are needed; media inert.
                FeatureDefinition d{};
                d.name = "RockTeeth";
                d.footprintX = d.footprintZ = 2;
                d.height = 20_ss;
                d.blocking = d.reclaimable = true;
                d.damage = 2000;
                rock = sim.featureDefinitions.insert(d);
                d.name = "DragonsTeeth";
                d.hasModel = true;
                d.damage = 4000;
                d.featureDead = rock;
                model = sim.featureDefinitions.insert(d);

                // green/TREES.TDF Tree1 / TREES.GAF Leafyburn01 (BT-255).
                d = FeatureDefinition{};
                d.name = "Tree1";
                d.footprintX = d.footprintZ = 1;
                d.height = 40_ss;
                d.damage = 0; // The feature reader's default for omitted damage.
                d.blocking = d.reclaimable = d.flamable = true;
                d.sparkTime = 5;
                d.spreadChance = 90;
                d.burnFrameTicks.assign(96, 2);
                tree = sim.featureDefinitions.insert(d);

                // weapons/UNITS.TDF SMALL_BUILDINGEX: AoE205, default40.
                WeaponDefinition weapon{};
                weapon.damageRadius = 102.5_ssf;
                weapon.damage["DEFAULT"] = 40;
                sim.weaponDefinitions["SMALL_BUILDINGEX"] = weapon;
            }

            FeatureId plant(FeatureDefinitionId definition, int x, int y)
            {
                auto id = sim.addFeature(definition, x, y);
                REQUIRE(id);
                return *id;
            }

            void blast(SimVector position)
            {
                Projectile p{};
                p.weaponType = "SMALL_BUILDINGEX";
                p.owner = PlayerId(10);
                p.position = p.previousPosition = p.origin = position;
                p.damageRadius = 102.5_ssf;
                p.damage["DEFAULT"] = 40;
                sim.applyDamageInRadius(position, p.damageRadius, p);
            }
        };
    }

    TEST_CASE("feature blasts reject the low edge but reach a high continuation once", "[feature-blast]")
    {
        for (bool highSide : {false, true})
        {
            BlastWorld w;
            const auto id = w.plant(w.rock, 64, 64);
            const auto centre = w.sim.getFeature(id).position;
            w.blast(centre + SimVector(highSide ? 110_ss : -110_ss, 0_ss, 0_ss));
            REQUIRE(w.sim.getFeature(id).hitPoints == (highSide ? 1960 : 2000));
        }
    }

    TEST_CASE("model origin uses stored height but continuation samples shifted ground", "[feature-blast]")
    {
        BlastWorld w;
        const auto id = w.plant(w.model, 64, 64);
        const auto centre = w.sim.getFeature(id).position;
        w.sim.getFeature(id).position.y = 205_ss; // Controlled sinking/height boundary.
        w.blast(centre);
        // Origin is out of vertical reach; the continuation still qualifies.
        REQUIRE(w.sim.getFeature(id).hitPoints == 3960);
    }

    TEST_CASE("feature reach keeps blast fractions and floors the halved AoE", "[feature-blast]")
    {
        for (auto distance : {101.75_ssf, 102_ss, 102.25_ssf})
        {
            BlastWorld w;
            const auto id = w.plant(w.rock, 64, 64);
            w.blast(w.sim.getFeature(id).position - SimVector(distance, 0_ss, 0_ss));
            REQUIRE(w.sim.getFeature(id).hitPoints == (distance < 102_ss ? 1960 : 2000));
        }
    }

    TEST_CASE("feature height uses staged integer interpolation and missing-corner sentinel", "[feature-blast]")
    {
        Grid<unsigned char> heights(256, 256, static_cast<unsigned char>(84));
        // Actual Coast To Coast.tnt corner patch at (12,0):84,84,84,83,
        // translated here to the controlled feature location (BT-256).
        heights.set(66, 66, 83);
        BlastWorld w(MapTerrain(std::move(heights), 0_ss));
        FeatureDefinition d{};
        // totala1.hpi features/desert/rocks.tdf DryRock07, inert sprite media.
        d.name = "DryRock07";
        d.footprintX = d.footprintZ = 3;
        d.height = 5_ss;
        d.blocking = d.reclaimable = true;
        d.damage = 2000;
        const auto id = w.plant(w.sim.featureDefinitions.insert(d), 64, 64);
        const auto point = featureBlastPoint(w.sim.terrain, Point(64, 64), 3, 3);
        REQUIRE(point.y == 84_ss); // A single float bilinear sample gives83.75.
        w.blast(SimVector(point.x - 101.5_ssf, 94_ss, point.z));
        REQUIRE(w.sim.getFeature(id).hitPoints == 1960);
        REQUIRE(featureBlastPoint(w.sim.terrain, Point(254, 254), 2, 2).y == -1_ss);
    }

    TEST_CASE("feature window has exclusive upper bounds and two-stage negative rounding", "[feature-blast]")
    {
        BlastWorld w;
        auto origin = w.sim.terrain.heightmapIndexToWorldCorner(0, 0);
        REQUIRE(featureBlastWindow(w.sim.terrain, origin + SimVector(1024_ss, 0_ss, 1024_ss), 102.5_ssf, 256, 256)
            == DiscreteRect(57, 57, 14, 14));
        REQUIRE(featureBlastWindow(w.sim.terrain, origin - SimVector(0.25_ssf, 0_ss, 0.25_ssf), 102.5_ssf, 256, 256)
            == DiscreteRect(0, 0, 7, 7));
    }

    TEST_CASE("unitsonly blast does not touch features", "[feature-blast]")
    {
        BlastWorld w;
        const auto id = w.plant(w.rock, 64, 64);
        w.sim.weaponDefinitions.at("SMALL_BUILDINGEX").damagesFeatures = false;
        w.blast(w.sim.getFeature(id).position);
        REQUIRE(w.sim.getFeature(id).hitPoints == 2000);
    }

    TEST_CASE("active sprite consumes no blast-history slot before the 64th ordinary feature", "[feature-blast]")
    {
        BlastWorld w;
        const auto active = w.plant(w.tree, 60, 60);
        w.sim.igniteFeature(active);
        REQUIRE(w.sim.getFeature(active).burningUntil);
        const auto target = w.plant(w.model, 67, 67);
        unsigned int ordinary = 0;
        for (int y = 60; y <= 68 && ordinary < 63; ++y)
        {
            for (int x = 60; x <= 68 && ordinary < 63; ++x)
            {
                if (w.sim.occupiedGrid.get(x, y).featureId)
                {
                    continue;
                }
                w.plant(w.tree, x, y);
                ++ordinary;
            }
        }
        REQUIRE(ordinary == 63);
        w.blast(w.sim.terrain.heightmapIndexToWorldCenter(64, 64));
        REQUIRE(w.sim.getFeature(active).burningUntil);
        REQUIRE(w.sim.getFeature(target).hitPoints == 3960);
    }

    TEST_CASE("full feature history still permits repeated continuation damage", "[feature-blast]")
    {
        BlastWorld w;
        const auto target = w.plant(w.model, 67, 67);
        unsigned int ordinary = 0;
        for (int y = 60; y <= 68 && ordinary < 64; ++y)
        {
            for (int x = 60; x <= 68 && ordinary < 64; ++x)
            {
                if (!w.sim.occupiedGrid.get(x, y).featureId)
                {
                    w.plant(w.tree, x, y);
                    ++ordinary;
                }
            }
        }
        w.blast(w.sim.terrain.heightmapIndexToWorldCenter(64, 64));
        REQUIRE(w.sim.getFeature(target).hitPoints == 3840); // Four40-point hits.
    }
}
