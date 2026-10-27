#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/game/save_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/MapBoundary.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/movement.h>
#include <rwe/sim/sim_test_util.h>
#include <sstream>

namespace rwe
{
    namespace
    {
        // Cometctr.ufo, Maps/Comet Catcher.tnt: 384x480, sea 0.
        // The sampled middle of each edge has height 55; 0 and 255 exercise
        // the projected-border extremes. See BOUNDARY-001 and validation doc.
        MapTerrain borderTerrain(unsigned char height)
        {
            return MapTerrain(Grid<unsigned char>(384, 480, height), 0_ss);
        }

        const MovementClassDefinition commander{"TANKDS2", 2, 2, 0, 100, 32, 32};
    }

    TEST_CASE("Reserved map borders follow projected height rather than a flat rectangle", "[mapboundary]")
    {
        for (auto height : {0, 55, 255})
        {
            CAPTURE(height);
            GameSimulation sim(borderTerrain(static_cast<unsigned char>(height)), 0, 0, 0);
            initializeMapBoundaryExclusions(sim);
            const auto& mask = sim.terrain.getLavaExclusions();
            const int firstTop = height == 0 ? 0 : height == 55 ? 2 : 8;
            const int firstBottom = height == 0 ? 472 : height == 55 ? 473 : 479;
            for (int y = 0; y < mask.getHeight(); ++y)
            {
                REQUIRE(static_cast<bool>(mask.get(192, y)) == (y < firstTop || y >= firstBottom));
                REQUIRE(mask.get(382, y));
            }
            REQUIRE_FALSE(mask.get(381, 240));
            REQUIRE_FALSE(mask.get(0, 240));
            REQUIRE(isGridPointWalkable(sim.terrain, commander, 192, 240));
            REQUIRE_FALSE(isGridPointWalkable(sim.terrain, commander, 381, 240));
            REQUIRE(sim.isCollisionAt(DiscreteRect(381, 240, 2, 2), UnitId(0)));
            REQUIRE_FALSE(sim.isCollisionAt(DiscreteRect(192, 240, 2, 2), UnitId(0)));
        }
    }

    TEST_CASE("Border exclusions retain feature origins, block continuations and survive saves", "[mapboundary][saveload]")
    {
        GameSimulation sim(borderTerrain(55), 0, 0, 0);
        FeatureDefinition definition{};
        definition.name = "RockTeeth";
        definition.footprintX = definition.footprintZ = 2;
        definition.damage = 2000;
        auto type = sim.featureDefinitions.insert(definition);
        auto feature = sim.addFeature(type, 20, 0);
        REQUIRE(feature);
        sim.terrain.setLavaExclusions({240 * 383 + 10});
        initializeMapBoundaryExclusions(sim);
        REQUIRE_FALSE(sim.terrain.getLavaExclusions().get(20, 0));
        REQUIRE(sim.terrain.getLavaExclusions().get(21, 0));
        REQUIRE(sim.terrain.getLavaExclusions().get(10, 240));
        sim.deleteFeature(*feature);
        REQUIRE_FALSE(sim.terrain.getLavaExclusions().get(20, 0));
        REQUIRE(sim.terrain.getLavaExclusions().get(21, 0));
        const auto saved = saveSimulationToJson(sim);
        const auto hash = computeHashOf(sim);
        sim.terrain.setLavaExclusions({});
        REQUIRE(computeHashOf(sim) != hash);
        loadSimulationFromJson(saved, sim);
        REQUIRE(computeHashOf(sim) == hash);
        REQUIRE(saveSimulationToJson(sim) == saved);
    }

    TEST_CASE("Raw TNT void cells survive loading and lava preparation", "[mapboundary]")
    {
        TntHeader header{};
        header.magicNumber = TntMagicNumber;
        header.width = header.height = 4;
        header.numberOfTiles = 1;
        header.mapDataOffset = sizeof(header);
        header.mapAttributesOffset = sizeof(header) + 8;
        header.featuresOffset = header.mapAttributesOffset + 16 * sizeof(TntTileAttributes);
        std::stringstream bytes(std::ios::in | std::ios::out | std::ios::binary);
        bytes.write(reinterpret_cast<const char*>(&header), sizeof(header));
        const char tiles[8]{};
        bytes.write(tiles, sizeof(tiles));
        for (int i = 0; i < 16; ++i)
        {
            TntTileAttributes cell{55, i == 5 ? TntTileAttributes::FeatureVoid : TntTileAttributes::FeatureNone, 0};
            bytes.write(reinterpret_cast<const char*>(&cell), sizeof(cell));
        }
        bytes.seekg(0);
        TntArchive archive(&bytes);
        OtaRecord ota{};
        ota.schemas.emplace_back();
        auto data = readMapData(archive, ota, 0);
        GameSimulation sim(std::move(data.terrain), 0, 0, 0);
        for (bool lava : {false, true})
        {
            sim.initializeLavaExclusions(lava);
            REQUIRE(sim.terrain.getLavaExclusions().get(1, 1));
            REQUIRE_FALSE(sim.terrain.getLavaExclusions().get(0, 0));
        }
    }

    TEST_CASE("A commander cannot path or physically step across a reserved border", "[mapboundary]")
    {
        for (auto backend : {PathfindingBackend::RweAStar, PathfindingBackend::OpenAnnihilation})
        {
            CAPTURE(static_cast<int>(backend));
            GameSimulation sim(borderTerrain(55), 0, 0, 0);
            initializeMapBoundaryExclusions(sim);
            sim.pathFindingService.backend = backend;
            auto player = addPlayer(sim);
            UnitDefinition definition{};
            definition.objectName = "commander";
            definition.isMobile = definition.canMove = true;
            definition.maxHitPoints = 3000;
            // ARMCOM in rev31.gp3 and TANKDS2 in totala1.hpi.
            definition.maxVelocity = 1.2_ssf;
            definition.acceleration = 0.15_ssf;
            definition.brakeRate = 0.3_ssf;
            definition.turnRate = 1044_ss;
            definition.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2, 2, 32, 32, 0, 100};
            sim.unitDefinitions["ARMCOM"] = definition;
            sim.unitScriptDefinitions["ARMCOM"] = *makeEmptyCobScript();
            sim.unitModelDefinitions["commander"] = createUnitModelDefinition(10_ss, {});
            auto start = sim.terrain.heightmapIndexToWorldCorner(192, 468);
            start.y = 55_ss;
            auto id = sim.trySpawnUnit("ARMCOM", player, start, std::nullopt);
            REQUIRE(id);
            auto& unit = sim.getUnitState(*id);
            unit.finishBuilding(definition);
            auto interior = sim.terrain.heightmapIndexToWorldCorner(192, 470);
            unit.orders.push_back(MoveOrder(interior));
            tick(sim, 300);
            REQUIRE(unit.position.z > start.z + 16_ss); // The valid control must actually move.
            REQUIRE(unit.orders.empty());
            auto goal = sim.terrain.heightmapIndexToWorldCorner(192, 476);
            unit.orders.push_back(MoveOrder(goal));
            tick(sim, 600);
            auto footprint = sim.computeFootprintRegion(unit.position, definition.movementCollisionInfo);
            REQUIRE_FALSE(sim.terrain.hasLavaExclusion(footprint));
            REQUIRE(footprint.bottom() <= 473);

            // The direct-step collision gate uses the same mask even when
            // the caller exempts the cells the unit currently occupies.
            REQUIRE(sim.isCollisionAt(DiscreteRect(192, 472, 2, 2), *id, footprint));
        }
    }
}
