#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/DiscreteRect.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/pathfinding/UnitPathFinder.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    /*
     * Whether a wreck blocks a hovercraft, and the answer is that it does.
     *
     * A play-test expected a hovercraft skimming the surface to pass over a
     * wreck lying on the sea bed. The original refuses it, and not narrowly:
     * blocking is a property of the feature TYPE, is answered from the map
     * square alone, and never reaches the feature's position at all.
     *
     * A map square is 13 bytes. The word at +8 holds the feature type index
     * occupying it -- 0xFFFF none, 0xFFFE a continuation cell that steps back
     * to the origin square by the byte offsets at +0xA/+0xB, 0xFFFB..0xFFFD
     * void markers that block outright. Every movement test in TotalA.exe
     * resolves that index against the definition table at globals+0x1426F,
     * stride 0x100, and rejects the cell when bit 6 of featdef+0xFE -- the
     * `blocking` key of S:15's feature flag table -- is set:
     *
     *   0x47E0F7  the per-movement-class footprint test, 0x47DFC0, which is
     *             what fills the pathfinder's 2-bit passability grids
     *             (0x47E1F0 packs the answer, 0x440830 stamps the rectangle)
     *   0x47DEDE  the single-cell version of the same, 0x47DE60
     *   0x47DCE8  the footprint test that excludes one unit, used by the mover
     *   0x47E4AB  "may this unit stand here", 0x47E2D0
     *   0x47D5E6  the building placement yardmap walk (S:15, S:27)
     *
     * Those five are the complete set of readers of that bit in the binary,
     * and every one of them rejects before a single field of the movement
     * class is consulted. There is no height term and no altitude term: the
     * feature's own record -- its y included -- is never fetched. There is no
     * exemption either, for `canhover` (def+0x241 bit 12), for `floater`, or
     * for a movement class of any name. The only per-class fields in the whole
     * routine are footprint (mc+0x4/+0x6), water depth (mc+0x8/+0xA) and the
     * four slope bytes (mc+0xC..0xF), and they are read after the feature has
     * already been allowed or refused.
     *
     * The sink does not change it. The falling-feature physics at 0x424214
     * writes position and velocity and touches no map square, so a corpse
     * occupies the cells it was placed on from the tick it appears until it is
     * reclaimed, at whatever depth it has reached (TOTALA-EXE-WRECKS.md).
     *
     * The shipped data is what makes the rule visible now that wreckage sinks.
     * All thirteen hovercraft in ccdata leave a corpse written like a land
     * unit's -- armah_dead: footprintx/z 3, height 20, blocking 1 -- while a
     * ship's is a flat plate authored to be driven over -- armroy_dead:
     * footprintx/z 5, height 4, blocking 0. So a hover battle really does
     * leave the sea bed obstructed and a naval one does not, in the original
     * as much as here.
     */
    namespace
    {
        /** Sixty units of open water, the depth of the shipped naval maps. */
        MapTerrain makeOpenSea()
        {
            Grid<unsigned char> heights(32, 32, static_cast<unsigned char>(20));
            return MapTerrain(std::move(heights), 80_ss);
        }

        /** World-space centre of a 3x3 footprint whose top-left cell is (x, y). */
        SimVector footprint3Center(const GameSimulation& sim, int x, int y)
        {
            return sim.terrain.heightmapIndexToWorldCorner(x, y) + SimVector(24_ss, 0_ss, 24_ss);
        }

        /** Does a 3x3 footprint placed at p overlap the given block of cells? */
        bool footprint3Overlaps(const Point& p, const DiscreteRect& cells)
        {
            return p.x < cells.x + cells.width
                && cells.x < p.x + 3
                && p.y < cells.y + cells.height
                && cells.y < p.y + 3;
        }

        /**
         * MOVEINFO.TDF [CLASS14], verbatim: TANKHOVER3, footprint 3, MaxSlope
         * and BadSlope 12, MaxWaterSlope and BadWaterSlope 255, and no
         * water-depth key at all. The missing MaxWaterDepth is what lets a
         * hovercraft cross any depth; RWE defaults it to 255 where the
         * original defaults it to 10000, which on a byte heightmap is the same
         * thing.
         *
         * Do not be misled by ARMSH.FBI's own MaxWaterDepth=0: S:30 records
         * that a unit's FBI water depths are ignored once it names a movement
         * class, and every hovercraft but the two transports names this one.
         */
        MovementClassId registerHoverClass(GameSimulation& sim)
        {
            MovementClassDefinition mc{"TANKHOVER3", 3u, 3u, 0u, 255u, 12u, 255u};
            auto id = sim.movementClassDatabase.registerMovementClass(mc);
            sim.movementClassCollisionService.registerMovementClass(id, computeWalkableGrid(sim.terrain, mc));
            return id;
        }

        /**
         * A hovercraft in the shape of ARMSH: footprint 3, canhover, and the
         * TANKHOVER3 movement class that every hovercraft but the two
         * transports uses.
         *
         * It goes in through tryAddUnit rather than sim_test_util's
         * addUnitOfType because the point of the test is the occupied grid,
         * and only the real spawn path stamps a unit's footprint into it.
         */
        UnitId addHovercraft(GameSimulation& sim, MovementClassId hoverClass, PlayerId owner, int cellX, int cellY, const std::shared_ptr<CobScript>& script)
        {
            UnitDefinition d{};
            d.isMobile = true;
            d.canMove = true;
            d.canHover = true;
            d.maxVelocity = 4_ss;
            d.acceleration = 4_ss;
            d.brakeRate = 4_ss;
            d.turnRate = 4000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::NamedMovementClass{hoverClass};
            sim.unitDefinitions["ARMSH"] = d;

            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::move(env));
            unit.unitType = "ARMSH";
            unit.owner = owner;
            // A hovercraft rides on the surface, so its y is the sea level.
            unit.position = footprint3Center(sim, cellX, cellY);
            unit.position.y = sim.terrain.getSeaLevel();
            unit.previousPosition = unit.position;
            unit.hitPoints = 100;
            auto unitId = sim.tryAddUnit(std::move(unit));
            REQUIRE(unitId.has_value());
            return *unitId;
        }

        /** ccdata features Corpses Armah_dead.tdf [armah_dead], verbatim. */
        FeatureDefinitionId addHoverCorpseDef(GameSimulation& sim)
        {
            FeatureDefinition d{};
            d.name = "armah_dead";
            d.footprintX = 3;
            d.footprintZ = 3;
            d.height = 20_ss;
            d.blocking = true;
            d.reclaimable = true;
            d.hitDensity = 100;
            d.metal = 96;
            d.damage = 300;
            auto id = sim.featureDefinitions.insert(d);
            // The name index is keyed upper-case: tryGetFeatureDefinitionId
            // upper-cases what it is handed, as the TDF reader does.
            sim.featureNameIndex.insert_or_assign("ARMAH_DEAD", id);
            return id;
        }

        /** rev31 features Corpses arm_corpses.tdf [armroy_dead], verbatim. */
        FeatureDefinitionId addShipCorpseDef(GameSimulation& sim)
        {
            FeatureDefinition d{};
            d.name = "armroy_dead";
            d.footprintX = 5;
            d.footprintZ = 5;
            d.height = 4_ss;
            d.blocking = false;
            d.reclaimable = true;
            d.hitDensity = 100;
            d.metal = 718;
            d.damage = 24000;
            auto id = sim.featureDefinitions.insert(d);
            sim.featureNameIndex.insert_or_assign("ARMROY_DEAD", id);
            return id;
        }

        /** Sink anything that is falling until nothing is moving any more. */
        void settleFeatures(GameSimulation& sim)
        {
            // Sixty units at 0.175 a tick is 343 ticks; 600 leaves room.
            for (int i = 0; i < 600; ++i)
            {
                sim.updateFallingFeatures();
            }
            for (const auto& [id, feature] : sim.features)
            {
                // Extra parentheses: Catch2 would otherwise try to print a
                // SimVector, and Vector3x's operator<< needs one for SimScalar.
                REQUIRE((feature.velocity == SimVector(0_ss, 0_ss, 0_ss)));
            }
        }
    }

    TEST_CASE("a wreck on the sea bed blocks a hovercraft exactly as it blocks anything else", "[wreckage]")
    {
        auto script = makeEmptyCobScript();

        SECTION("open water is free")
        {
            GameSimulation sim(makeOpenSea(), 0u, 0, 0);
            auto hoverClass = registerHoverClass(sim);
            auto player = addPlayer(sim);
            auto hoverId = addHovercraft(sim, hoverClass, player, 2, 2, script);

            // Nothing between the two ends: the hovercraft crosses sixty units
            // of open water without a detour.
            REQUIRE_FALSE(sim.isCollisionAt(DiscreteRect(12, 2, 3, 3), hoverId));

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, hoverId, hoverClass, 3u, 3u, Point(12, 2));
            auto result = pathFinder.findPath(Point(2, 2));
            REQUIRE(result.type == AStarPathType::Complete);
            REQUIRE_FALSE(result.path.empty());
            REQUIRE((result.path.back() == Point(12, 2)));
        }

        SECTION("a hovercraft's own wreck blocks the cells it sank onto")
        {
            GameSimulation sim(makeOpenSea(), 0u, 0, 0);
            auto hoverClass = registerHoverClass(sim);
            auto player = addPlayer(sim);
            auto hoverId = addHovercraft(sim, hoverClass, player, 2, 2, script);
            addHoverCorpseDef(sim);

            // The corpse is spawned where the hovercraft died, which for a
            // hovercraft is the surface (0x486360 copies the unit's own y).
            auto deathPlace = footprint3Center(sim, 7, 2);
            deathPlace.y = sim.terrain.getSeaLevel();
            sim.trySpawnFeature("ARMAH_DEAD", deathPlace, SimAngle(0), false);
            REQUIRE(sim.features.begin() != sim.features.end());
            auto wreckId = sim.features.begin()->first;

            DiscreteRect wreckCells(7, 2, 3, 3);

            // Blocking from the tick it appears, while it is still at the
            // surface and has not begun to fall.
            REQUIRE(sim.isCollisionAt(wreckCells, hoverId));

            settleFeatures(sim);

            // On the bottom now, sixty units under the hovercraft's hull...
            const auto& wreck = sim.features.tryGet(wreckId)->get();
            REQUIRE(wreck.position.y < sim.terrain.getSeaLevel());
            REQUIRE(wreck.position.y == sim.terrain.getHeightAt(wreck.position.x, wreck.position.z));

            // ... and blocking just the same. Depth is no part of the test in
            // the original and must not become part of it here.
            REQUIRE(sim.isCollisionAt(wreckCells, hoverId));

            // The pathfinder agrees: there is a route to the far side, and it
            // does not go through the wreck.
            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, hoverId, hoverClass, 3u, 3u, Point(12, 2));
            auto result = pathFinder.findPath(Point(2, 2));
            REQUIRE(result.type == AStarPathType::Complete);
            REQUIRE(result.path.size() > 2);
            for (const auto& p : result.path)
            {
                REQUIRE_FALSE(footprint3Overlaps(p, wreckCells));
            }
        }

        SECTION("a ship's wreck on the same sea bed does not block")
        {
            // The difference is the data, not the engine: a naval corpse is
            // authored blocking=0 so that a fleet is not walled in by its own
            // dead. It occupies the cells all the same -- it is there to be
            // reclaimed -- and only the blocking flag decides whether that
            // stops anybody.
            GameSimulation sim(makeOpenSea(), 0u, 0, 0);
            auto hoverClass = registerHoverClass(sim);
            auto player = addPlayer(sim);
            auto hoverId = addHovercraft(sim, hoverClass, player, 2, 2, script);
            addShipCorpseDef(sim);

            auto deathPlace = sim.terrain.heightmapIndexToWorldCorner(7, 2) + SimVector(40_ss, 0_ss, 40_ss);
            deathPlace.y = sim.terrain.getSeaLevel();
            sim.trySpawnFeature("ARMROY_DEAD", deathPlace, SimAngle(0), false);
            REQUIRE(sim.features.begin() != sim.features.end());

            settleFeatures(sim);

            REQUIRE(sim.occupiedGrid.get(8, 3).featureId.has_value());
            REQUIRE_FALSE(sim.isCollisionAt(DiscreteRect(7, 2, 3, 3), hoverId));

            UnitPathFinder pathFinder(&sim, &sim.movementClassCollisionService, hoverId, hoverClass, 3u, 3u, Point(12, 2));
            auto result = pathFinder.findPath(Point(2, 2));
            REQUIRE(result.type == AStarPathType::Complete);
        }

        SECTION("the wreck blocks the hovercraft above it and the submarine beside it alike")
        {
            // The same cells, asked on behalf of two movement classes that
            // share nothing: one riding sixty units above the wreck, one on
            // the bottom with it. The original answers from the feature type
            // before it has looked at either class, so the two cannot differ.
            GameSimulation sim(makeOpenSea(), 0u, 0, 0);
            auto hoverClass = registerHoverClass(sim);
            auto player = addPlayer(sim);
            auto hoverId = addHovercraft(sim, hoverClass, player, 2, 2, script);
            addHoverCorpseDef(sim);

            auto deathPlace = footprint3Center(sim, 7, 2);
            deathPlace.y = sim.terrain.getSeaLevel();
            sim.trySpawnFeature("ARMAH_DEAD", deathPlace, SimAngle(0), false);
            settleFeatures(sim);

            UnitDefinition sub{};
            sub.isMobile = true;
            sub.canMove = true;
            sub.maxVelocity = 4_ss;
            sub.acceleration = 4_ss;
            sub.brakeRate = 4_ss;
            sub.turnRate = 4000_ss;
            sub.maxHitPoints = 100;
            sub.buildTime = 0u;
            // {footprintX, footprintZ, maxSlope, maxWaterSlope, minWaterDepth, maxWaterDepth}
            sub.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 12u, 255u};
            sim.unitDefinitions["ARMSUB"] = sub;

            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            UnitState subUnit(pieces, std::move(env));
            subUnit.unitType = "ARMSUB";
            subUnit.owner = player;
            subUnit.position = footprint3Center(sim, 2, 8);
            subUnit.previousPosition = subUnit.position;
            subUnit.hitPoints = 100;
            auto subId = sim.tryAddUnit(std::move(subUnit));
            REQUIRE(subId.has_value());

            DiscreteRect wreckCells(7, 2, 3, 3);
            REQUIRE(sim.isCollisionAt(wreckCells, hoverId));
            REQUIRE(sim.isCollisionAt(wreckCells, *subId));
        }
    }
}
