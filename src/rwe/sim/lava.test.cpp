#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/game/save_util.h>
#include <rwe/game/dump_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/movement.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        // Real totala2.hpi Lava Run TNT corners, sea=21, at (133,42)
        // and (60,41). BT-264/265; docs/agents/lava-movement.md.
        MapTerrain lavaRunPatch(bool shallow)
        {
            const unsigned char shore[] = {31,21,21,35,24,30,34,33,35};
            const unsigned char wet[] = {16,17,20,18,25,21,25,27,25};
            Grid<unsigned char> heights(3, 3);
            for (int y = 0; y < 3; ++y)
            {
                for (int x = 0; x < 3; ++x)
                {
                    heights.set(x, y, (shallow ? wet : shore)[y * 3 + x]);
                }
            }
            return MapTerrain(std::move(heights), 21_ss);
        }
        // Shipped totala1.hpi gamedata/moveinfo.tdf, not FBI fallback depths.
        const MovementClassDefinition commander{"TANKDS2", 2, 2, 0, 100, 32, 32};
        const MovementClassDefinition peewee{"TANKSH2", 2, 2, 0, 12, 15, 15};

        FeatureDefinitionId smudge(GameSimulation& sim)
        {
            FeatureDefinition d{};
            d.name = "Smudge01"; // green/smudges.tdf: 1x1, nonblocking.
            d.footprintX = d.footprintZ = 1;
            return sim.featureDefinitions.insert(d);
        }
        FeatureDefinitionId rock(GameSimulation& sim)
        {
            FeatureDefinition d{};
            d.name = "RockTeeth"; // all worlds/dragonsteeth.tdf.
            d.footprintX = d.footprintZ = 2;
            d.blocking = true;
            d.damage = 2000;
            d.height = 20_ss;
            return sim.featureDefinitions.insert(d);
        }
    }

    TEST_CASE("Lava Run blocks commander and kbot footprints before depth limits", "[lava]")
    {
        for (bool shallow : {false, true})
        {
            GameSimulation sim(lavaRunPatch(shallow), 0, 0, 0);
            // Ordinary water permits both classes at these shipped heights.
            sim.initializeLavaExclusions(false);
            REQUIRE(isGridPointWalkable(sim.terrain, commander, 0, 0));
            REQUIRE(isGridPointWalkable(sim.terrain, peewee, 0, 0));
            sim.initializeLavaExclusions(true);
            REQUIRE_FALSE(isGridPointWalkable(sim.terrain, commander, 0, 0));
            REQUIRE_FALSE(isGridPointWalkable(sim.terrain, peewee, 0, 0));
            REQUIRE_FALSE(computeWalkableGrid(sim.terrain, commander).get(0, 0));
            REQUIRE_FALSE(computeWalkableGrid(sim.terrain, peewee).get(0, 0));
            const DiscreteRect footprint(0, 0, 2, 2);
            REQUIRE(sim.isCollisionAt(footprint));
            REQUIRE(sim.isCollisionAt(footprint, UnitId(0)));
            REQUIRE(sim.isCollisionAt(footprint, UnitId(0), footprint));
            REQUIRE(sim.isCollisionAtForSearch(footprint, UnitId(0), footprint, GameTime(0)));
            REQUIRE(sim.isCollisionAtIgnoringBuilding(GridRegion(0,0,2,2), UnitId(0)));
            if (!shallow)
            {
                // Equality and a low neighbour both count; wholly high does not.
                REQUIRE(sim.terrain.getLavaExclusions().get(0, 0));
                REQUIRE(sim.terrain.getLavaExclusions().get(1, 0));
                REQUIRE_FALSE(sim.terrain.getLavaExclusions().get(0, 1));
                REQUIRE_FALSE(sim.terrain.getLavaExclusions().get(1, 1));
            }
        }
    }

    TEST_CASE("Initial feature origins escape lava marking but continuations do not", "[lava]")
    {
        GameSimulation sim(makeFlatTerrain(8, 8), 0, 0, 0);
        auto id = sim.addFeature(rock(sim), 2, 2);
        REQUIRE(id);
        sim.initializeLavaExclusions(true);
        REQUIRE_FALSE(sim.terrain.hasLavaExclusion(DiscreteRect(2,2,1,1)));
        REQUIRE(sim.terrain.hasLavaExclusion(DiscreteRect(3,2,1,1)));
        sim.deleteFeature(*id);
        REQUIRE_FALSE(sim.terrain.hasLavaExclusion(DiscreteRect(2,2,1,1)));
        REQUIRE(sim.terrain.hasLavaExclusion(DiscreteRect(3,2,1,1)));
        REQUIRE(sim.addFeature(smudge(sim), 2, 2));
        REQUIRE_FALSE(sim.addFeature(smudge(sim), 3, 2));
    }

    TEST_CASE("Feature placement refuses lava after earlier footprint clearing", "[lava]")
    {
        GameSimulation sim(makeFlatTerrain(8, 8), 0, 0, 0);
        auto old = sim.addFeature(smudge(sim), 2, 2);
        REQUIRE(old);
        sim.initializeLavaExclusions(true);
        REQUIRE_FALSE(sim.addFeature(rock(sim), 2, 2));
        REQUIRE((sim.features.begin() == sim.features.end()));
        REQUIRE(sim.terrain.hasLavaExclusion(DiscreteRect(3,2,1,1)));
    }

    TEST_CASE("Lava state is saved hashed dumped and rebuilds movement caches", "[lava][saveload]")
    {
        GameSimulation sim(lavaRunPatch(false), 0, 0, 0);
        sim.initializeLavaExclusions(true);
        auto cls = sim.movementClassDatabase.registerMovementClass(commander);
        sim.movementClassCollisionService = createMovementClassCollisionService(sim.terrain, sim.movementClassDatabase);
        auto saved = saveSimulationToJson(sim);
        auto hash = computeHashOf(sim);
        sim.terrain.setLavaExclusions({2,3});
        REQUIRE(computeHashOf(sim) != hash);
        loadSimulationFromJson(saved, sim);
        REQUIRE(computeHashOf(sim) == hash);
        REQUIRE(saveSimulationToJson(sim) == saved);
        REQUIRE(dumpJson(sim).at("lavaExclusions") == saved.at("lavaExclusions"));
        REQUIRE_FALSE(sim.movementClassCollisionService.isWalkable(cls, Point(0,0)));
        saved["lavaExclusions"] = std::vector<unsigned int>{};
        loadSimulationFromJson(saved, sim);
        REQUIRE(sim.movementClassCollisionService.isWalkable(cls, Point(0,0)));
        REQUIRE_THROWS(sim.terrain.setLavaExclusions({4}));
    }
}
