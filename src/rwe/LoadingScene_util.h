#pragma once

#include <deque>
#include <rwe/ColorPalette.h>
#include <rwe/game/WeaponMediaInfo.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/fbi/UnitFbi.h>
#include <rwe/io/moveinfotdf/MovementClassTdf.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <rwe/sim/FeatureDefinitionId.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <unordered_map>

namespace rwe
{
    MovementClassCollisionService createMovementClassCollisionService(const MapTerrain& terrain, const MovementClassDatabase& movementClassDatabase);

    std::unordered_map<std::string, CobScript> loadCobScripts(AbstractVirtualFileSystem& vfs);

    std::vector<std::string> getFeatureNames(TntArchive& tnt);

    Grid<std::size_t> getMapData(TntArchive& tnt);

    Grid<unsigned char> getHeightGrid(const Grid<TntTileAttributes>& attrs);

    Vector3f colorToVector(const Color& color);

    unsigned int colorDistance(const Color& a, const Color& b);

    Vector3f getLaserColorUtil(const std::vector<Color>& palette, const std::vector<Color>& guiPalette, unsigned int colorIndex);

    std::optional<std::string> getFxName(unsigned int code);

    MovementClassDefinition parseMovementClassDefinition(const MovementClassTdf& tdf);

    WeaponDefinition parseWeaponDefinition(const WeaponTdf& tdf);

    std::optional<YardMapCell> parseYardMapCell(char c);

    std::vector<YardMapCell> parseYardMapCells(const std::string& yardMap);

    Grid<YardMapCell> parseYardMap(unsigned int width, unsigned int height, const std::string& yardMap);

    UnitMovementOrders parseStandingMoveOrder(unsigned int value);

    UnitFireOrders parseStandingFireOrder(unsigned int value);

    UnitDefinition parseUnitDefinition(const UnitFbi& fbi, MovementClassDatabase& movementClassDatabase);

    /**
     * How many units a transport carries, from whichever key its FBI has.
     * TransportCapacity when it is there; the 1.0 key TransportMaxUnits when
     * only that is (TOTALA-EXE.md §88); and for a transport that names
     * neither, six at sea and one in the air. 0 for anything that is not a
     * transport.
     */
    unsigned int transportCapacityFromFbi(const UnitFbi& fbi);

    /**
     * What to tell the player about a transport whose capacity did not come
     * from the key the 3.1 exe reads, or nothing. Unpatched data is the
     * usual cause, and it is otherwise silent: the transport loads, just not
     * the number the patched game would load.
     */
    std::optional<std::string> transportCapacityWarning(const UnitFbi& fbi);

    WeaponMediaInfo parseWeaponMediaInfo(const std::vector<Color>& palette, const std::vector<Color>& guiPalette, const WeaponTdf& tdf);

    FeatureDefinitionId getFeatureId(FeatureDefinitionId& nextId, const std::unordered_map<std::string, FeatureDefinitionId>& featureNameIndex, std::deque<std::string>& openQueue, std::unordered_map<std::string, FeatureDefinitionId>& openSet, const std::string& featureName);
}
