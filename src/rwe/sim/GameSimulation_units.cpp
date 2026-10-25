#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimRandom.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/movement.h>
#include <rwe/util/collection_util.h>
#include <rwe/util/rwe_string.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rwe
{
    std::optional<UnitWeapon> tryCreateWeapon(const GameSimulation& sim, const std::string& weaponType)
    {
        if (sim.weaponDefinitions.find(toUpper(weaponType)) == sim.weaponDefinitions.end())
        {
            return std::nullopt;
        }

        UnitWeapon weapon;
        weapon.weaponType = toUpper(weaponType);
        return weapon;
    }

    std::vector<UnitMesh> createUnitMeshes(const GameSimulation& sim, const std::string& objectName)
    {
        const auto& def = sim.unitModelDefinitions.at(objectName);

        const auto& pieceDefs = def.pieces;

        std::vector<UnitMesh> pieces(pieceDefs.size());
        for (Index i = 0; i < getSize(pieces); ++i)
        {
            pieces[i].name = pieceDefs[i].name;
        }

        return pieces;
    }

    UnitState createUnit(
        GameSimulation& simulation,
        const std::string& unitType,
        PlayerId owner,
        const SimVector& position,
        std::optional<SimAngle> rotation)
    {
        const auto& unitDefinition = simulation.unitDefinitions.at(unitType);

        auto meshes = createUnitMeshes(simulation, unitDefinition.objectName);
        auto modelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);

        // Every piece of every model starts shaded, mobile or not: the
        // piece-list builder does `or byte [ebx+0x28],0x4` unconditionally
        // (0x45AF31, TOTALA-EXE-SHADING.md S:11), and a script has to say
        // DONT_SHADE to turn it off. RWE used to clear the flag for anything
        // mobile, which meant the whole of the shading work reached buildings
        // and nothing else.
        //
        // The opt-out is not theoretical: 290 of the 714 shipped scripts call
        // DONT_SHADE and not one calls SHADE, so the data already says where
        // shading is unwanted -- canopies, glass, the pieces authored to be
        // read flat -- and it can only say so if the default is on.

        const auto& script = simulation.unitScriptDefinitions.at(unitType);
        auto cobEnv = std::make_unique<CobEnvironment>(&script);
        UnitState unit(meshes, std::move(cobEnv));
        unit.unitType = toUpper(unitType);
        unit.owner = owner;
        unit.position = position;
        unit.previousPosition = position;

        // The original seeds both standing orders from the definition here and
        // never consults it again, so anything that moves them afterwards --
        // the buttons, a script -- sticks for the life of the unit.
        unit.moveOrders = unitDefinition.standingMoveOrder;
        unit.fireOrders = unitDefinition.standingFireOrder;

        // DELIBERATE DIVERGENCE (see docs/TOTALA-EXE.md section 83).
        //
        // A bomber that ships on Hold Fire is given Fire At Will instead. In
        // the shipped data ARMTHUND and CORSHAD are `StandingFireOrder=0`
        // while ARMPNIX and CORHURC are `2`, which is the whole reason a
        // patrolling Phoenix bombs what it passes over and a patrolling
        // Thunder flies by: the patrol handler's engage check is Fire At Will
        // exactly, so for the level-one bombers it never runs.
        //
        // Changed here rather than in the patrol check so the divergence is
        // visible and reversible -- the fire-order button reads Fire At Will,
        // and a player who wants the original sets Hold Fire by hand. The
        // bombers then take the same path the Phoenix already takes instead
        // of a second one written for them.
        if (unit.fireOrders == UnitFireOrders::HoldFire && unitDefinition.canFly)
        {
            auto carriesABomb = [&](const std::string& weaponName) {
                if (weaponName.empty())
                {
                    return false;
                }
                auto it = simulation.weaponDefinitions.find(toUpper(weaponName));
                return it != simulation.weaponDefinitions.end()
                    && std::holds_alternative<ProjectilePhysicsTypeBomb>(it->second.physicsType);
            };

            if (carriesABomb(unitDefinition.weapon1)
                || carriesABomb(unitDefinition.weapon2)
                || carriesABomb(unitDefinition.weapon3))
            {
                unit.fireOrders = UnitFireOrders::FireAtWill;
            }
        }

        // Init_Cloaked is seeded in the same breath by the original, out of the
        // same flags dword. It only asks for the cloak; whether the unit gets
        // one is still settled a second at a time by the energy.
        unit.cloakRequested = unitDefinition.initCloaked;

        if (rotation)
        {
            unit.rotation = *rotation;
            unit.previousRotation = *rotation;
        }
        else if (unitDefinition.isMobile)
        {
            // spawn the unit facing the other way
            unit.rotation = HalfTurn;
            unit.previousRotation = HalfTurn;
        }

        if (!unitDefinition.weapon1.empty())
        {
            unit.weapons[0] = tryCreateWeapon(simulation, unitDefinition.weapon1);
        }
        if (!unitDefinition.weapon2.empty())
        {
            unit.weapons[1] = tryCreateWeapon(simulation, unitDefinition.weapon2);
        }
        if (!unitDefinition.weapon3.empty())
        {
            unit.weapons[2] = tryCreateWeapon(simulation, unitDefinition.weapon3);
        }

        return unit;
    }

    SimScalar GameSimulation::computeBuildHeight(const UnitDefinition& unitDefinition, const DiscreteRect& footprint) const
    {
        // A cell the building stands on: the original tests yardmap bit 3,
        // which is set for the characters o O c f y G and clear for the water
        // ones C Y w and for a gap.
        auto standsOn = [](YardMapCell cell) {
            switch (cell)
            {
                case YardMapCell::Ground:
                case YardMapCell::GroundPassableWhenClosed:
                case YardMapCell::GroundGeoPassableWhenOpen:
                case YardMapCell::GroundNoFeature:
                case YardMapCell::GroundPassable:
                case YardMapCell::Geo:
                    return true;
                default:
                    return false;
            }
        };

        const auto& heights = terrain.getHeightMap();

        auto lowestLandCorner = std::optional<SimScalar>();
        for (int dy = 0; dy < footprint.height; ++dy)
        {
            for (int dx = 0; dx < footprint.width; ++dx)
            {
                auto x = footprint.x + dx;
                auto y = footprint.y + dy;
                if (x < 0 || y < 0)
                {
                    continue;
                }
                auto ux = static_cast<unsigned int>(x);
                auto uy = static_cast<unsigned int>(y);
                if (ux + 1 >= static_cast<unsigned int>(heights.getWidth()) || uy + 1 >= static_cast<unsigned int>(heights.getHeight()))
                {
                    continue;
                }

                // No yardmap at all means the whole footprint is ground,
                // which is what a unit without one occupies.
                if (unitDefinition.yardMap)
                {
                    auto ux2 = static_cast<unsigned int>(dx);
                    auto uy2 = static_cast<unsigned int>(dy);
                    if (ux2 >= static_cast<unsigned int>(unitDefinition.yardMap->getWidth()) || uy2 >= static_cast<unsigned int>(unitDefinition.yardMap->getHeight()))
                    {
                        continue;
                    }
                    if (!standsOn(unitDefinition.yardMap->get(ux2, uy2)))
                    {
                        continue;
                    }
                }

                // The lowest of the cell's four corners, as 0x47D8D2 takes
                // `cell+0x6` -- the low corner the terrain record already
                // holds.
                auto corner = rweMin(
                    rweMin(intToSimScalar(heights.get(ux, uy)), intToSimScalar(heights.get(ux + 1, uy))),
                    rweMin(intToSimScalar(heights.get(ux, uy + 1)), intToSimScalar(heights.get(ux + 1, uy + 1))));

                lowestLandCorner = lowestLandCorner ? rweMin(*lowestLandCorner, corner) : corner;
            }
        }

        if (lowestLandCorner)
        {
            return *lowestLandCorner;
        }

        // Nothing in the footprint stands on the ground: a water building.
        return terrain.getSeaLevel() - intToSimScalar(static_cast<int>(unitDefinition.waterLine));
    }

    std::optional<UnitId> GameSimulation::trySpawnUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<SimAngle> rotation)
    {
        auto unit = createUnit(*this, unitType, owner, position, rotation);
        const auto& unitDefinition = unitDefinitions.at(unitType);

        if (!unitDefinition.isMobile)
        {
            // A building is levelled onto its footprint the moment it is
            // created (0x47DDC0 immediately before CreateUnit), and nothing
            // moves it afterwards -- the per-tick height routine returns at
            // once for anything without a mover.
            auto footprint = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            unit.position.y = computeBuildHeight(unitDefinition, footprint);
            unit.previousPosition.y = unit.position.y;
        }
        else if (unitDefinition.floater && !unitDefinition.upright)
        {
            // Not aligned at all: sea level minus WaterLine, whatever the
            // ground (0x48A870, BT-42).
            unit.position.y = terrain.getSeaLevel() - intToSimScalar(static_cast<int>(unitDefinition.waterLine));
            unit.previousPosition.y = unit.position.y;
        }
        else if (unitDefinition.canHover)
        {
            unit.position.y = rweMax(terrain.getSeaLevel(), unit.position.y);
            unit.previousPosition.y = unit.position.y;
        }
        else if (!unitDefinition.canFly)
        {
            // And a mobile ground or sea unit is settled onto the terrain
            // inside CreateUnit too (0x486109 calling 0x48A870), before it is
            // ever drawn. Without that a submarine appears at the height of
            // the shipyard's build piece and drops to the sea bed on its
            // first step, which is what the play-test saw.
            unit.position.y = terrain.getHeightAt(unit.position.x, unit.position.z);
            unit.previousPosition.y = unit.position.y;
        }

        // TODO: if we failed to add the unit throw some warning
        auto unitId = tryAddUnit(std::move(unit));

        if (unitId)
        {
            // Give the unit its demo id before anything else can look at it:
            // a build's 0x09 needs the frame to have one, and an already
            // complete unit -- a commander, a resurrection -- needs it to
            // appear in the 0x2c round robin at all.
            if (demoRecorder)
            {
                demoRecorder->unitCreated(*this, *unitId);
            }

            if (taLiveSender.attached())
            {
                taLiveSender.unitCreated(taLiveSender.context, *this, *unitId);
            }

            UnitBehaviorService(this).onCreate(*unitId);

            // The original hangs a `GetBuilt` mission off the frame as it is
            // placed, and that mission's first act is to set a timer. A unit
            // that arrives already finished -- a start-position commander --
            // never gets one.
            auto& spawnedUnit = getUnitState(*unitId);
            if (spawnedUnit.isBeingBuilt(unitDefinition))
            {
                spawnedUnit.nanoframeDecayTime = gameTime + GameTime(NanoframeDecayGraceTicks);
            }

            events.push_back(UnitSpawnedEvent{*unitId});
        }

        return unitId;
    }

    std::optional<UnitId> GameSimulation::trySpawnCompletedUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<SimAngle> rotation)
    {
        auto unitId = trySpawnUnit(unitType, owner, position, rotation);
        if (unitId)
        {
            auto& unit = getUnitState(*unitId);
            unit.finishBuilding(unitDefinitions.at(unit.unitType));
        }
        return unitId;
    }

    void GameSimulation::setHitPoints(UnitId unitId, unsigned int hitPoints)
    {
        getUnitState(unitId).hitPoints = hitPoints;
    }

    std::optional<UnitId> GameSimulation::tryAddUnit(UnitState&& unit)
    {
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        if (isCollisionAt(footprintRect))
        {
            return std::nullopt;
        }

        // The win test reads this (BT-193); the first unit made for a player
        // is what sets it in the original.
        if (unit.owner.value < players.size())
        {
            players[unit.owner.value].hasCreatedUnits = true;
            if (unitDefinition.commander && players[unit.owner.value].commanderType.empty())
            {
                // Initial commanders are resolved from the side data by the
                // shared loader; retain that identity for Deathmatch.
                players[unit.owner.value].commanderType = unit.unitType;
            }
        }

        auto unitId = units.emplace(std::move(unit));
        const auto& insertedUnit = units.tryGet(unitId)->get();

        // The spatial index has never heard of this one, and a search that
        // consulted it would not find the unit at all.
        invalidateUnitSpatialIndex();

        auto footprintRegion = occupiedGrid.tryToRegion(footprintRect);
        assert(!!footprintRegion);

        if (unitDefinition.isMobile)
        {
            occupiedGrid.forEach(*footprintRegion, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
            // A map insertion, so the occupancy stamp is now (BT-58).
            units.tryGet(unitId)->get().occupancyStamp = gameTime;
        }
        else
        {
            assert(!!unitDefinition.yardMap);
            occupiedGrid.forEach2(footprintRegion->x, footprintRegion->y, *unitDefinition.yardMap, [&](auto& cell, const auto& yardMapCell) {
                cell.buildingInfo = OccupiedCellBuildingInfo{unitId, isPassable(yardMapCell, insertedUnit.yardOpen)};
            });
        }

        return unitId;
    }

    bool GameSimulation::isInsideBuildableArea(unsigned int x, unsigned int y, unsigned int footprintX, unsigned int footprintZ) const
    {
        // The first thing 0x47D2E0 does, before it looks at a single cell
        // (0x47D302-0x47D352): the footprint's corner must be at least one
        // cell in from the top and the left -- `cmp cx,1 / jl refuse` for
        // each axis -- and its far side must stop short of the last cell,
        // `x + footprintX >= width` refusing at 0x47D339. So nothing is ever
        // built touching the edge of the map, on any side. RWE had no such
        // rule, and both the players and the computer could plant a
        // building flush against the boundary.
        auto width = static_cast<unsigned int>(occupiedGrid.getWidth());
        auto height = static_cast<unsigned int>(occupiedGrid.getHeight());
        return x >= 1u && y >= 1u && x + footprintX < width && y + footprintZ < height;
    }

    bool GameSimulation::canBeBuiltAt(const rwe::MovementClassDefinition& mc, const std::optional<Grid<YardMapCell>>& yardMap, bool yardMapContainsGeo, unsigned int x, unsigned int y) const
    {
        if (!isInsideBuildableArea(x, y, mc.footprintX, mc.footprintZ))
        {
            return false;
        }

        if (isCollisionAt(DiscreteRect(x, y, mc.footprintX, mc.footprintZ)))
        {
            return false;
        }

        if (!isGridPointWalkable(terrain, mc, x, y))
        {
            return false;
        }

        if (yardMapContainsGeo && yardMap && !containsAnyGeoMatch(*yardMap, x, y))
        {
            return false;
        }

        return true;
    }

    bool GameSimulation::canBeBuiltAtAsSeenBy(const MovementClassDefinition& mc, const std::optional<Grid<YardMapCell>>& yardMap, bool yardMapContainsGeo, unsigned int x, unsigned int y, PlayerId player) const
    {
        if (!isInsideBuildableArea(x, y, mc.footprintX, mc.footprintZ))
        {
            return false;
        }

        // Ground nobody on this side has ever looked at. The box is red over
        // all of it, whatever the terrain underneath happens to be.
        //
        // This is the other half of the fog rule below, and it pulls the
        // opposite way on purpose. Below, an undiscovered occupant is
        // ignored, because refusing the placement would announce that
        // something is standing there. Here the whole square is undiscovered,
        // so a box that went green would announce that the ground is flat and
        // clear -- a free survey of the map, taken by sweeping the cursor
        // across the black with a building on it, which is a good deal more
        // than one hidden tank. Reported from play on Coast To Coast: placing
        // into unexplored ground showed "green where it can be placed and red
        // where it cant, even though the land is unexplored".
        //
        // Every cell of the footprint, not its centre: a building half on
        // known ground is half a survey.
        for (unsigned int cellZ = y; cellZ < y + mc.footprintZ; ++cellZ)
        {
            for (unsigned int cellX = x; cellX < x + mc.footprintX; ++cellX)
            {
                if (!isExploredBy(player, terrain.heightmapIndexToWorldCenter(static_cast<int>(cellX), static_cast<int>(cellZ))))
                {
                    return false;
                }
            }
        }

        auto region = occupiedGrid.tryToRegion(DiscreteRect(x, y, mc.footprintX, mc.footprintZ));
        if (!region)
        {
            return false;
        }

        // A unit the player has not seen does not stand in the way of the
        // placement box, because letting it would say that it is there. A
        // feature does: a wreck or a tree is part of the ground rather than
        // somebody's secret, and the report this answers was about buildings.
        auto blocked = occupiedGrid.any(*region, [&](const auto& cell) {
            if (cell.mobileUnitId && canSeeUnit(player, *cell.mobileUnitId))
            {
                return true;
            }
            if (cell.buildingInfo && !cell.buildingInfo->passable && canSeeUnit(player, cell.buildingInfo->unit))
            {
                return true;
            }
            if (cell.featureId)
            {
                const auto& f = getFeature(*cell.featureId);
                if (getFeatureDefinition(f.featureName).blocking)
                {
                    return true;
                }
            }
            return false;
        });
        if (blocked)
        {
            return false;
        }

        if (!isGridPointWalkable(terrain, mc, x, y))
        {
            return false;
        }

        if (yardMapContainsGeo && yardMap && !containsAnyGeoMatch(*yardMap, x, y))
        {
            return false;
        }

        return true;
    }

    DiscreteRect GameSimulation::computeFootprintRegion(const SimVector& position, unsigned int footprintX, unsigned int footprintZ) const
    {
        auto halfFootprintX = SimScalar(footprintX * MapTerrain::HeightTileWidthInWorldUnits.value / 2);
        auto halfFootprintZ = SimScalar(footprintZ * MapTerrain::HeightTileHeightInWorldUnits.value / 2);
        SimVector topLeft(
            position.x - halfFootprintX,
            position.y,
            position.z - halfFootprintZ);

        auto cell = terrain.worldToHeightmapCoordinateNearest(topLeft);

        return DiscreteRect(cell.x, cell.y, footprintX, footprintZ);
    }

    unsigned int GameSimulation::extractorMetalWork(const DiscreteRect& footprint) const
    {
        // 0x437840: the sum gains the metal byte plus one for each cell the
        // bounds-checked lookup (0x481550) finds, and nothing for one it does
        // not. A footprint with no metal under it is still worth one a cell.
        auto region = metalGrid.clipRegion(footprint);
        auto metalBytes = metalGrid.accumulate(region, 0u, std::plus<>());
        return metalBytes + static_cast<unsigned int>(region.width) * static_cast<unsigned int>(region.height);
    }

    DiscreteRect GameSimulation::computeFootprintRegion(const SimVector& position, const UnitDefinition::MovementCollisionInfo& collisionInfo) const
    {
        auto [footprintX, footprintZ] = getFootprintXZ(collisionInfo);
        return computeFootprintRegion(position, footprintX, footprintZ);
    }

    bool GameSimulation::anyFeatureOccupies(const DiscreteRect& rect) const
    {
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return occupiedGrid.any(*region, [&](const auto& cell) {
            return cell.featureId.has_value();
        });
    }

    bool GameSimulation::containsAnyGeoMatch(const Grid<YardMapCell>& yardMap, unsigned int x, unsigned int y) const
    {
        // Iterate explicitly with per-cell bounds checks against the underlying
        // vector size. The build cursor can hover with a footprint that extends
        // past geoGrid's edge (geoGrid is one cell smaller than the heightmap),
        // and Grid::any2 / Grid::get would otherwise walk off the end.
        const int geoW = geoGrid.getWidth();
        const int geoH = geoGrid.getHeight();
        const int yardW = yardMap.getWidth();
        const int yardH = yardMap.getHeight();
        const int baseX = static_cast<int>(x);
        const int baseY = static_cast<int>(y);
        const auto& geoVec = geoGrid.getVector();
        const size_t geoSize = geoVec.size();

        for (int dy = 0; dy < yardH; ++dy)
        {
            const int gy = baseY + dy;
            if (gy < 0 || gy >= geoH)
            {
                continue;
            }
            for (int dx = 0; dx < yardW; ++dx)
            {
                const int gx = baseX + dx;
                if (gx < 0 || gx >= geoW)
                {
                    continue;
                }
                const size_t idx = static_cast<size_t>(gy) * static_cast<size_t>(geoW) + static_cast<size_t>(gx);
                if (idx >= geoSize)
                {
                    continue;
                }
                if (geoVec[idx] && isGeo(yardMap.get(dx, dy)))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool GameSimulation::isCollisionAt(const DiscreteRect& rect) const
    {
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return isCollisionAt(*region);
    }

    bool GameSimulation::isCollisionAt(const GridRegion& region) const
    {
        // BT-264: ground exclusion also applies to direct traversal.
        if (terrain.hasLavaExclusion(DiscreteRect(region.x, region.y, region.width, region.height)))
        {
            return true;
        }
        return occupiedGrid.any(region, [&](const auto& cell) {
            if (cell.mobileUnitId)
            {
                return true;
            }
            if (cell.buildingInfo && !cell.buildingInfo->passable)
            {
                return true;
            }
            if (cell.featureId)
            {
                const auto& f = getFeature(*cell.featureId);
                const auto& def = getFeatureDefinition(f.featureName);
                if (def.blocking)
                {
                    return true;
                }
            }

            return false;
        });
    }

    bool GameSimulation::isCollisionAtIgnoringBuilding(const GridRegion& region, UnitId building) const
    {
        // BT-264: ground exclusion also applies to direct traversal.
        if (terrain.hasLavaExclusion(DiscreteRect(region.x, region.y, region.width, region.height)))
        {
            return true;
        }
        return occupiedGrid.any(region, [&](const auto& cell) {
            if (cell.mobileUnitId)
            {
                return true;
            }
            if (cell.buildingInfo && !cell.buildingInfo->passable && cell.buildingInfo->unit != building)
            {
                return true;
            }
            if (cell.featureId)
            {
                const auto& f = getFeature(*cell.featureId);
                const auto& def = getFeatureDefinition(f.featureName);
                if (def.blocking)
                {
                    return true;
                }
            }

            return false;
        });
    }

    bool GameSimulation::isCollisionAt(const DiscreteRect& rect, UnitId self) const
    {
        // BT-264: ground exclusion also applies to direct traversal.
        if (terrain.hasLavaExclusion(rect))
        {
            return true;
        }
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return region->any([&](const GridCoordinates& c) { return cellBlocksUnit(occupiedGrid.get(c), self); });
    }

    bool GameSimulation::isCollisionAt(const DiscreteRect& rect, UnitId self, const DiscreteRect& passableRegion) const
    {
        // BT-264: ground exclusion also applies to direct traversal.
        if (terrain.hasLavaExclusion(rect))
        {
            return true;
        }
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return region->any([&](const GridCoordinates& c) {
            // The unit is already standing on this cell, so stepping across it
            // is not entering an obstacle. Everything else blocks as usual.
            if (passableRegion.contains(Point(c.x, c.y)))
            {
                return false;
            }
            return cellBlocksUnit(occupiedGrid.get(c), self);
        });
    }

    bool GameSimulation::isCollisionAtForSearch(const DiscreteRect& rect, UnitId self, const DiscreteRect& passableRegion, GameTime searchTime) const
    {
        // BT-264: ground exclusion also applies to direct traversal.
        if (terrain.hasLavaExclusion(rect))
        {
            return true;
        }
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        // The class stamp: this search's tick less the thirty-tick window,
        // read as zero until the game is that old.
        constexpr unsigned int recentlyMovedWindow = 30;
        const auto classStamp = searchTime.value < recentlyMovedWindow ? 0u : searchTime.value - recentlyMovedWindow;

        return region->any([&](const GridCoordinates& c) {
            if (passableRegion.contains(Point(c.x, c.y)))
            {
                return false;
            }
            const auto& cell = occupiedGrid.get(c);
            if (cell.mobileUnitId && *cell.mobileUnitId != self)
            {
                auto occupant = tryGetUnitState(*cell.mobileUnitId);
                if (!occupant || occupant->get().occupancyStamp.value < classStamp)
                {
                    return true;
                }
            }
            // The mobile occupant has been excused; the building and the
            // feature are judged as ever.
            return cellBlocksUnit(cell, self, true);
        });
    }

    bool GameSimulation::cellBlocksUnit(const OccupiedCell& cell, UnitId self, bool excuseMobile) const
    {
        if (!excuseMobile && cell.mobileUnitId && *cell.mobileUnitId != self)
        {
            return true;
        }
        if (cell.buildingInfo && !cell.buildingInfo->passable)
        {
            return true;
        }
        if (cell.featureId)
        {
            const auto& f = getFeature(*cell.featureId);
            const auto& def = getFeatureDefinition(f.featureName);
            if (def.blocking)
            {
                return true;
            }
        }

        return false;
    }

    bool GameSimulation::isYardmapBlocked(unsigned int x, unsigned int y, const Grid<YardMapCell>& yardMap, bool open, UnitId self) const
    {
        return occupiedGrid.any2(x, y, yardMap, [&](const auto& cell, const auto& yardMapCell) {
            if (isPassable(yardMapCell, open))
            {
                return false;
            }

            if (cell.mobileUnitId)
            {
                return true;
            }
            if (cell.buildingInfo && !cell.buildingInfo->passable && cell.buildingInfo->unit != self)
            {
                return true;
            }
            if (cell.featureId)
            {
                const auto& f = getFeature(*cell.featureId);
                const auto& def = getFeatureDefinition(f.featureName);
                if (def.blocking)
                {
                    return true;
                }
            }

            return false;
        });
    }

    UnitState& GameSimulation::getUnitState(UnitId id)
    {
        auto it = units.find(id);
        assert(it != units.end());
        return it->second;
    }

    const UnitState& GameSimulation::getUnitState(UnitId id) const
    {
        auto it = units.find(id);
        assert(it != units.end());
        return it->second;
    }

    UnitInfo GameSimulation::getUnitInfo(UnitId id)
    {
        auto& state = getUnitState(id);
        const auto& definition = unitDefinitions.at(state.unitType);
        return UnitInfo(id, &state, &definition);
    }

    ConstUnitInfo GameSimulation::getUnitInfo(UnitId id) const
    {
        auto& state = getUnitState(id);
        const auto& definition = unitDefinitions.at(state.unitType);
        return ConstUnitInfo(id, &state, &definition);
    }

    std::optional<std::reference_wrapper<UnitState>> GameSimulation::tryGetUnitState(UnitId id)
    {
        return tryFind(units, id);
    }

    std::optional<std::reference_wrapper<const UnitState>> GameSimulation::tryGetUnitState(UnitId id) const
    {
        return tryFind(units, id);
    }

    std::optional<std::reference_wrapper<const UnitState>> GameSimulation::tryGetUnitState(CobUnitId id) const
    {
        return tryFind(units, UnitId(id.value));
    }

    bool GameSimulation::unitExists(UnitId id) const
    {
        auto it = units.find(id);
        return it != units.end();
    }

    MapFeature& GameSimulation::getFeature(FeatureId id)
    {
        auto it = features.find(id);
        assert(it != features.end());
        return it->second;
    }

    const MapFeature& GameSimulation::getFeature(FeatureId id) const
    {
        auto it = features.find(id);
        assert(it != features.end());
        return it->second;
    }

    std::optional<std::reference_wrapper<MapFeature>> GameSimulation::tryGetFeature(FeatureId id)
    {
        return tryFind(features, id);
    }

    std::optional<std::reference_wrapper<const MapFeature>> GameSimulation::tryGetFeature(FeatureId id) const
    {
        return tryFind(features, id);
    }

    GamePlayerInfo& GameSimulation::getPlayer(PlayerId player)
    {
        return players.at(player.value);
    }

    const GamePlayerInfo& GameSimulation::getPlayer(PlayerId player) const
    {
        return players.at(player.value);
    }

    void GameSimulation::moveObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar position, SimScalar speed)
    {
        getUnitState(unitId).moveObject(name, axis, position, speed);
    }

    void GameSimulation::moveObjectNow(UnitId unitId, const std::string& name, SimAxis axis, SimScalar position)
    {
        getUnitState(unitId).moveObjectNow(name, axis, position);
    }

    void GameSimulation::turnObject(UnitId unitId, const std::string& name, SimAxis axis, SimAngle angle, SimScalar speed)
    {
        getUnitState(unitId).turnObject(name, axis, angle, speed);
    }

    void GameSimulation::turnObjectNow(UnitId unitId, const std::string& name, SimAxis axis, SimAngle angle)
    {
        getUnitState(unitId).turnObjectNow(name, axis, angle);
    }

    void GameSimulation::spinObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar speed, SimScalar acceleration)
    {
        getUnitState(unitId).spinObject(name, axis, speed, acceleration);
    }

    void GameSimulation::stopSpinObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar deceleration)
    {
        getUnitState(unitId).stopSpinObject(name, axis, deceleration);
    }


    std::optional<SimVector> GameSimulation::intersectLineWithTerrain(const Line3x<SimScalar>& line) const
    {
        return terrain.intersectLine(line);
    }

    void GameSimulation::moveUnitOccupiedArea(const DiscreteRect& oldRect, const DiscreteRect& newRect, UnitId unitId)
    {
        auto oldRegion = occupiedGrid.tryToRegion(oldRect);
        assert(!!oldRegion);
        auto newRegion = occupiedGrid.tryToRegion(newRect);
        assert(!!newRegion);

        occupiedGrid.forEach(*oldRegion, [](auto& cell) { cell.mobileUnitId = std::nullopt; });
        occupiedGrid.forEach(*newRegion, [unitId](auto& cell) { cell.mobileUnitId = unitId; });

        // Changing square is a map insertion in the original, and a map
        // insertion writes the occupancy stamp (mover+0x26, BT-58). A step
        // that stays inside the same footprint does not.
        if (!(oldRect == newRect))
        {
            if (auto unit = tryGetUnitState(unitId))
            {
                unit->get().occupancyStamp = gameTime;
            }
        }
    }

    void GameSimulation::requestPath(UnitId unitId)
    {
        // A search that outlived its tick is still the unit's turn: it holds
        // the head of the queue, and a unit that asks again for the same place
        // while it runs must leave it there. Moving it to the back would put a
        // different unit at the head and break the scheduler's assumption that
        // the search belongs to the request at the head (and the save format,
        // which reads a suspended search off the head). If the unit's goal has
        // moved on, the search in flight is thrown away and the request is
        // queued normally below.
        if (pathFindingService.onPathRequested(*this, unitId))
        {
            return;
        }

        PathRequest request{unitId};

        // If the unit is already in the queue for a path,
        // we'll assume that they no longer care about their old request
        // and that their new request is for some new path,
        // so we'll move them to the back of the queue for fairness.
        //
        // Unless they are at the FRONT. A search is sliced across ticks, and
        // the pathfinder's half-finished search always belongs to the front
        // of this queue: it says so with an assertion when the search
        // completes, and pops the front to retire it. Moving the front unit
        // to the back broke that -- in a Debug build the assertion, in a
        // Release build the wrong request popped and the right one left
        // behind for a unit whose path had already been delivered. It took a
        // unit asking twice while its own search was suspended, which needs
        // a busy queue and a goal that moves: thirty hulls sailing at a
        // remembered enemy is both. Left at the front, a changed destination
        // is already handled where the search is resumed, which abandons it
        // and starts again; an unchanged one simply finishes.
        auto it = std::find(pathRequests.begin(), pathRequests.end(), request);
        if (it != pathRequests.end())
        {
            if (it == pathRequests.begin())
            {
                return;
            }
            pathRequests.erase(it);
        }

        pathRequests.push_back(PathRequest{unitId});
    }

    SimVector toMissileDirection(SimAngle heading, SimAngle pitch)
    {
        auto horizontal = cos(pitch);
        return SimVector(sin(heading) * horizontal, sin(pitch), cos(heading) * horizontal);
    }

    SimVector computeWindVector(SimAngle direction, int speed)
    {
        // The original builds the map's wind vector once, each time the wind
        // changes (0x490CA4-0x490D35): X from the routine at 0x4b70ef, Z from
        // the one at 0x4b7123, each negated and then doubled.
        //
        // Which of those is sin and which is cos is worth spelling out,
        // because docs/TOTALA-EXE.md had it backwards until this was ported.
        // The two routines are identical but for the table index, which the
        // second advances by 0x4000 -- a quarter turn. Both read 0x509f00,
        // whose first entry is 0 and which peaks at 0x2000, so that table is a
        // SINE table of amplitude 8192: the first routine is sin and the
        // second cos, not the reverse. Each multiplies by the speed and then
        // does shrd ..., 0xd -- a >>13 that exactly cancels the 8192 -- so a
        // routine returns trig(direction) * speed.
        //
        // That happens to be RWE's own convention too; see toMissileDirection
        // directly above, which puts sin in X and cos in Z for the same reason.
        //
        // The components the original stores are 16.16 fixed point, like a
        // position, which is what the 65536 converts out of. At Brain Coral's
        // maxwindspeed of 3000 the wind carries a shell 0.092 world units a
        // tick -- around six units over a Crusader shell's flight, against a
        // damage radius of 24. Enough to matter, not enough to dominate.
        //
        // There is no Y term. The word at globals+0x37ED0 is never written
        // anywhere in the binary, so the wind is strictly horizontal.
        auto magnitude = SimScalar(-2 * speed) / 65536_ss;
        return SimVector(sin(direction) * magnitude, 0_ss, cos(direction) * magnitude);
    }

    Projectile GameSimulation::createProjectileFromWeapon(const ProjectileSpawn& spawn)
    {
        const auto& weaponType = spawn.weapon ? spawn.weapon->weaponType : spawn.weaponType;
        const auto& weaponDefinition = weaponDefinitions.at(weaponType);
        const auto& owner = spawn.owner;
        const auto& position = spawn.position;
        const auto& direction = spawn.direction;
        const auto& targetUnit = spawn.targetUnit;
        const auto& attacker = spawn.attacker;
        const auto& inheritedVelocity = spawn.inheritedVelocity;
        const auto& targetPosition = spawn.targetPosition;
        const auto& targetProjectile = spawn.targetProjectile;

        Projectile projectile;
        projectile.weaponType = weaponType;
        projectile.owner = owner;
        projectile.attacker = attacker;
        projectile.position = position;
        projectile.previousPosition = position;
        projectile.origin = position;
        projectile.velocity = direction * weaponDefinition.velocity;
        if (inheritedVelocity)
        {
            // Bombs released from bombers inherit the aircraft's velocity at
            // release. Without this, bombs would fall straight down from the
            // firing piece while the bomber has already moved past, causing
            // visible misses. (See bombsight handling in tryFireWeapon.)
            projectile.velocity = projectile.velocity + *inheritedVelocity;
        }

        // The ballistic spawn's launch correction (0x49CE62-0x49CE8A, BT-114):
        // sin(pitch) times the speed MINUS trunc(slot / weaponvelocity) whole
        // ticks of gravity, the slot being the 1.25 x Z difference between the
        // weapon's Query and AimFrom pieces written when the unit was built.
        // The solver that chose the pitch never sees it, so the shell leaves
        // flatter than the angle it solved.
        if (spawn.weapon && std::holds_alternative<ProjectilePhysicsTypeBallistic>(weaponDefinition.physicsType))
        {
            auto ticks = launchCorrectionTicks(spawn.weapon->ballisticZOffset, weaponDefinition.velocity);
            projectile.velocity.y -= gravityPerTick() * intToSimScalar(static_cast<int>(ticks));
        }

        projectile.lastSmoke = gameTime;

        projectile.damage = weaponDefinition.damage;

        projectile.damageRadius = weaponDefinition.damageRadius;
        projectile.edgeEffectiveness = weaponDefinition.edgeEffectiveness;

        // How long a straight-flying round lives, and the order the original
        // asks the questions in (0x49C942). It works the life out of the
        // weapon's own `range` and `weaponvelocity` -- `(range << 16) /
        // velocity` in the 16.16 the parser stored the speed as, which is
        // simply "how many ticks to fly the full range" -- and only falls back
        // on `weapontimer` for a weapon that declares no velocity to divide
        // by. So `weapontimer` is a fallback rather than an override, and RWE
        // had it the other way round: ARM_DISINTEGRATOR names both, and the
        // four seconds it asks for are not what the original uses.
        //
        // The number is the *range*, not the distance to whatever was aimed
        // at. Nothing shortens the flight to suit the target: the round is
        // launched along a direction fixed at the muzzle and flies its whole
        // 240 units whether the target is at 30 or at 239. For an ordinary
        // weapon that is only visible on a miss; for the D-gun, whose blast
        // is not consumed by going off (see noExplode), it is the whole
        // reason the trail carries on past what it was fired at.
        auto lineOfSight = std::holds_alternative<ProjectilePhysicsTypeLineOfSight>(weaponDefinition.physicsType);
        if (lineOfSight && weaponDefinition.velocity > 0_ss)
        {
            // Done in the original's fixed point rather than in floats, so
            // that a range that divides exactly by the speed gives the round
            // number rather than one tick less to a rounding error.
            auto rangeFixed = static_cast<int64_t>(simScalarToFixed(weaponDefinition.maxRange));
            auto velocityFixed = static_cast<int64_t>(simScalarToFixed(weaponDefinition.velocity));
            auto lifeTicks = static_cast<unsigned int>(std::max<int64_t>(rangeFixed / velocityFixed, 1));
            projectile.dieOnFrame = gameTime + GameTime(lifeTicks);
        }
        else if (weaponDefinition.weaponTimer)
        {
            auto randomDecay = weaponDefinition.randomDecay.value().value;
            auto randomVal = randomBelow(rng, randomDecay + 1u);
            projectile.dieOnFrame = gameTime + *weaponDefinition.weaponTimer - GameTime(randomDecay / 2) + GameTime(randomVal);
        }

        projectile.createdAt = gameTime;
        projectile.groundBounce = weaponDefinition.groundBounce;
        const bool isBurstChild = spawn.burstChildHorizontalDistance.has_value();

        projectile.targetUnit = targetUnit;
        projectile.targetPosition = targetPosition;
        projectile.targetProjectile = targetProjectile;

        if (auto selfProp = std::get_if<ProjectilePhysicsTypeSelfPropelled>(&weaponDefinition.physicsType))
        {
            // A missile leaves at `startvelocity`; failing that it leaves at the
            // top speed if it has no motor to build up with, and otherwise from a
            // standstill (0x49C980). A vertical launch points straight up and is
            // not moving at all, so the whole climb comes out of the motor
            // (0x49CC20).
            if (selfProp->startVelocity != 0_ss)
            {
                projectile.speed = selfProp->startVelocity;
            }
            else if (selfProp->acceleration == 0_ss)
            {
                projectile.speed = selfProp->maxVelocity;
            }
            else
            {
                projectile.speed = 0_ss;
            }

            if (selfProp->vLaunch)
            {
                projectile.heading = SimAngle(0);
                projectile.pitch = QuarterTurn;
                projectile.velocity = SimVector(0_ss, 0_ss, 0_ss);
            }
            else
            {
                auto flat = SimVector(direction.x, 0_ss, direction.z);
                projectile.heading = UnitState::toRotation(flat);
                projectile.pitch = atan2(direction.y, flat.length());
                projectile.velocity = toMissileDirection(projectile.heading, projectile.pitch) * projectile.speed;
            }

            // The burn is `range / weaponvelocity` ticks -- how long the missile
            // would take to fly its whole range at the speed cap -- unless the
            // weapon says `noautorange`, in which case it is `weapontimer`, and
            // that is what times a vertical launch's climb (0x49C920). Running
            // out is not death: without `burnblow` the missile coasts on, which
            // is why one fired at the far edge of its range arrives unguided.
            auto burn = weaponDefinition.weaponTimer.value_or(GameTime(0));
            if (selfProp->autoRange)
            {
                burn = GameTime(simScalarToUInt(weaponDefinition.maxRange / selfProp->maxVelocity));
            }
            projectile.motorOutFrame = gameTime + burn;
            projectile.dieOnFrame = std::nullopt;
        }

        if (isBurstChild)
        {
            // A child of a burst has a life of its own (0x49B87F-0x49B8BC): the
            // tick plus the weapon's timer when the weapon names one, else the
            // stored horizontal distance plus 16 world units over the child's
            // own scalar speed, with randomdecay moving it about half its
            // interval either way (0x49B8BF-0x49B8EB). A missile's expiry is
            // the end of its burn, as the first round's is.
            GameTime expiry;
            if (weaponDefinition.weaponTimer)
            {
                expiry = gameTime + *weaponDefinition.weaponTimer;
            }
            else
            {
                auto childSpeed = projectile.velocity.length();
                if (std::holds_alternative<ProjectilePhysicsTypeSelfPropelled>(weaponDefinition.physicsType))
                {
                    childSpeed = projectile.speed > 0_ss ? projectile.speed : std::get<ProjectilePhysicsTypeSelfPropelled>(weaponDefinition.physicsType).maxVelocity;
                }
                if (childSpeed <= 0_ss)
                {
                    childSpeed = weaponDefinition.velocity;
                }
                auto distanceFixed = static_cast<int64_t>(simScalarToFixed(*spawn.burstChildHorizontalDistance + 16_ss));
                auto speedFixed = static_cast<int64_t>(simScalarToFixed(childSpeed));
                auto lifeTicks = speedFixed > 0 ? std::max<int64_t>(distanceFixed / speedFixed, 1) : int64_t(1);
                expiry = gameTime + GameTime(static_cast<unsigned int>(lifeTicks));
            }
            auto randomDecay = weaponDefinition.randomDecay.value_or(GameTime(0)).value;
            auto randomVal = randomBelow(rng, randomDecay + 1u);
            expiry = expiry - GameTime(randomDecay / 2) + GameTime(randomVal);

            if (std::holds_alternative<ProjectilePhysicsTypeSelfPropelled>(weaponDefinition.physicsType))
            {
                projectile.motorOutFrame = expiry;
            }
            else
            {
                projectile.dieOnFrame = expiry;
            }
        }

        return projectile;
    }

    void GameSimulation::spawnProjectile(const ProjectileSpawn& spawn)
    {
        projectiles.emplace(createProjectileFromWeapon(spawn));
    }

    std::vector<int> dealStartPositions(const std::vector<int>& startPositions, StartLocationMode mode, std::minstd_rand& rng)
    {
        auto result = startPositions;

        if (mode == StartLocationMode::Fixed)
        {
            return result;
        }

        // Fisher-Yates, drawing each index straight out of the simulation's
        // own generator. It is walked from the back so that the very first
        // draw can still place any element anywhere, which an unshuffled
        // forward pass cannot.
        for (std::size_t i = result.size(); i > 1; --i)
        {
            auto j = static_cast<std::size_t>(rng() % static_cast<std::minstd_rand::result_type>(i));
            std::swap(result[i - 1], result[j]);
        }

        return result;
    }
}
