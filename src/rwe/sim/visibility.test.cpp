#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/lostdf/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/PlayerVisibility.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * A 64x64 heightmap made of bands running along z, given as
         * (firstColumn, lastColumn, height). Because a band's height is
         * constant along z, every vision cell in the columns it covers ends
         * up with that height whatever the projected-space skew does to the
         * rows, which keeps these tests easy to reason about.
         */
        Grid<unsigned char> makeBandedHeightmap(const std::vector<std::tuple<int, int, int>>& bands)
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (const auto& [firstColumn, lastColumn, height] : bands)
            {
                for (int y = 0; y < 64; ++y)
                {
                    for (int x = firstColumn; x <= lastColumn; ++x)
                    {
                        heights.set(x, y, static_cast<unsigned char>(height));
                    }
                }
            }
            return heights;
        }

        PlayerId addPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
        }

        void defineUnit(
            GameSimulation& sim,
            const std::string& type,
            unsigned int sight,
            unsigned int radar,
            bool onOffable,
            int modelHeight = 0,
            unsigned int sonar = 0)
        {
            UnitDefinition d{};
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = sight;
            d.radarDistance = radar;
            d.sonarDistance = sonar;
            d.onOffable = onOffable;
            d.objectName = type;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;

            UnitModelDefinition model;
            model.height = intToSimScalar(modelHeight);
            sim.unitModelDefinitions[type] = model;
        }

        UnitId addUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = type;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            return unitId;
        }

        /** The cell `cells` to the east of the one holding `origin`. */
        Point eastOf(const GameSimulation& sim, const SimVector& origin, int cells)
        {
            auto cell = sim.visionCellAt(origin);
            return Point(cell.x + cells, cell.y);
        }
    }

    TEST_CASE("line of sight", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineUnit(sim, "scout", /*sight*/ 100u, /*radar*/ 0u, false);

        // 64x64 tiles -> 32x32 vision cells of 32 world units. Map centre is world (0, 0).
        auto here = SimVector(0_ss, 0_ss, 0_ss);
        auto nearby = SimVector(64_ss, 0_ss, 0_ss);
        auto farAway = SimVector(400_ss, 0_ss, 0_ss);

        auto scoutId = addUnit(sim, "scout", us, here, script);
        REQUIRE_FALSE(sim.isExploredBy(us, here));

        sim.tick();

        SECTION("a unit reveals its surroundings to its owner only")
        {
            REQUIRE(sim.isVisibleTo(us, here));
            REQUIRE(sim.isVisibleTo(us, nearby));
            REQUIRE_FALSE(sim.isVisibleTo(us, farAway));
            REQUIRE_FALSE(sim.isVisibleTo(them, here));
            REQUIRE(sim.canSeeUnit(us, scoutId));
            REQUIRE_FALSE(sim.canSeeUnit(them, scoutId));
        }

        SECTION("explored ground stays explored after the unit is gone")
        {
            sim.getUnitState(scoutId).markAsDeadNoCorpse();
            sim.tick();
            REQUIRE_FALSE(sim.isVisibleTo(us, here));
            REQUIRE(sim.isExploredBy(us, here));
        }

        SECTION("positions off the map are neither explored nor visible")
        {
            REQUIRE_FALSE(sim.isVisibleTo(us, SimVector(-5000_ss, 0_ss, 0_ss)));
            REQUIRE_FALSE(sim.isExploredBy(us, SimVector(0_ss, 0_ss, 5000_ss)));
        }
    }

    TEST_CASE("sight radius is capped at eight cells", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        // A sight distance far beyond anything in the game data.
        defineUnit(sim, "seer", /*sight*/ 10000u, /*radar*/ 0u, false);

        auto here = SimVector(0_ss, 0_ss, 0_ss);
        addUnit(sim, "seer", us, here, script);
        sim.tick();

        const auto& vis = sim.playerVisibility.at(us.value);

        SECTION("the eighth cell out is still seen")
        {
            REQUIRE(vis.isVisible(eastOf(sim, here, 8)));
        }

        SECTION("nothing beyond it is")
        {
            REQUIRE_FALSE(vis.isVisible(eastOf(sim, here, 9)));
            REQUIRE_FALSE(vis.isVisible(eastOf(sim, here, 20)));
            // 8 cells is 256 world units, whatever SightDistance says.
            REQUIRE_FALSE(sim.isVisibleTo(us, SimVector(300_ss, 0_ss, 0_ss)));
        }

        SECTION("the fan reaches just as far in all four directions")
        {
            // The authored rays cover one quadrant and are replicated by the
            // four ninety degree rotations, so the disc comes out symmetric.
            auto cell = sim.visionCellAt(here);
            REQUIRE(vis.isVisible(Point(cell.x + 8, cell.y)));
            REQUIRE(vis.isVisible(Point(cell.x - 8, cell.y)));
            REQUIRE(vis.isVisible(Point(cell.x, cell.y + 8)));
            REQUIRE(vis.isVisible(Point(cell.x, cell.y - 8)));
        }
    }

    TEST_CASE("los.tdf ray tables", "[visibility]")
    {
        // TABLEINFO plus the first two tables, verbatim from TA's file.
        std::string source =
            "[TABLEINFO]\n"
            "\t{\n"
            "\tnumtables=3;\n"
            "\t}\n"
            "\n"
            "[TABLE1]\n"
            "\t{\n"
            "\tnumlines=2;\t// Radius of 1\n"
            "\tline1= 1, 0, 1;\n"
            "\tline2= 1, 1, 1;\n"
            "\t}\n"
            "\n"
            "[TABLE2]\n"
            "\t{\n"
            "\tnumlines=4;\t// Radius of 2\n"
            "\tline1= 2, 0, 1, 0, 2;\n"
            "\tline2= 2, 0, 1, 1, 2;\n"
            "\tline3= 1, 1, 1;\n"
            "\tline4= 2, 1, 0, 2, 1;\n"
            "\t}\n";

        auto tables = parseLosTdf(parseTdfFromString(source));

        REQUIRE(tables.maxRadius() == 2);
        // A radius of 0 sees only the cell underfoot, so entry 0 is empty.
        REQUIRE(tables.tableForRadius(0).rays.empty());
        REQUIRE(tables.tableForRadius(1).rays.size() == 2);
        REQUIRE(tables.tableForRadius(2).rays.size() == 4);

        // Each line is a step count followed by that many dx,dy pairs.
        const auto& ray = tables.tableForRadius(2).rays[3];
        REQUIRE(ray.size() == 2);
        REQUIRE(ray[0] == Point(1, 0));
        REQUIRE(ray[1] == Point(2, 1));
    }

    TEST_CASE("hills block line of sight", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        // A tall ridge across heightmap columns 36..37 (vision cell column 18)
        // on an otherwise flat 64x64 map.
        GameSimulation sim(MapTerrain(makeBandedHeightmap({{36, 37, 120}}), 0_ss), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        defineUnit(sim, "scout", /*sight*/ 200u, /*radar*/ 0u, false);

        // Unit at world x = 0 (tile 32); the ridge is at tiles 36..37 (world 64..96).
        addUnit(sim, "scout", us, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.tick();

        SECTION("ground on the near side of the ridge is visible")
        {
            REQUIRE(sim.isVisibleTo(us, SimVector(40_ss, 0_ss, 0_ss)));
            REQUIRE(sim.isVisibleTo(us, SimVector(-150_ss, 0_ss, 0_ss)));
        }

        SECTION("ground behind the ridge is not")
        {
            REQUIRE_FALSE(sim.isVisibleTo(us, SimVector(160_ss, 0_ss, 0_ss)));
            REQUIRE_FALSE(sim.isVisibleTo(us, SimVector(190_ss, 0_ss, 0_ss)));
        }

        SECTION("the ridge itself can be seen")
        {
            REQUIRE(sim.isVisibleTo(us, SimVector(80_ss, 0_ss, 0_ss)));
        }
    }

    TEST_CASE("a ridge shadows the valley but not the hilltop", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        // West to east: a plateau 200 high out to vision column 16, a dip at
        // columns 17..18, a ridge 100 high at column 19, then a flat plain.
        GameSimulation sim(MapTerrain(makeBandedHeightmap({{0, 33, 200}, {38, 39, 100}}), 0_ss), 0u, 0, 0);
        auto hillPlayer = addPlayer(sim, "hill");
        auto valleyPlayer = addPlayer(sim, "valley");
        defineUnit(sim, "scout", /*sight*/ 200u, /*radar*/ 0u, false, /*modelHeight*/ 20);

        // On the plateau's eastern lip, looking down over the ridge.
        auto hillPos = SimVector(8_ss, 200_ss, 0_ss);
        // In the dip immediately west of the ridge, looking at it head on.
        auto valleyPos = SimVector(70_ss, 0_ss, 0_ss);

        addUnit(sim, "scout", hillPlayer, hillPos, script);
        addUnit(sim, "scout", valleyPlayer, valleyPos, script);
        sim.tick();

        const auto& hillVis = sim.playerVisibility.at(hillPlayer.value);
        const auto& valleyVis = sim.playerVisibility.at(valleyPlayer.value);

        // The projected-space skew puts the two units in different grid rows,
        // so compare cells relative to each unit's own cell.
        auto farPlainFromHill = eastOf(sim, hillPos, 6);
        auto farPlainFromValley = eastOf(sim, valleyPos, 4);

        SECTION("the unit down in the dip sees the ridge but nothing past it")
        {
            REQUIRE(valleyVis.isVisible(eastOf(sim, valleyPos, 1)));
            REQUIRE_FALSE(valleyVis.isVisible(eastOf(sim, valleyPos, 2)));
            REQUIRE_FALSE(valleyVis.isVisible(farPlainFromValley));
        }

        SECTION("the unit up on the plateau sees over it")
        {
            REQUIRE(hillVis.isVisible(farPlainFromHill));
        }

        SECTION("both are looking at the same piece of ground")
        {
            REQUIRE(farPlainFromHill.x == farPlainFromValley.x);
        }
    }

    TEST_CASE("a blocked ray stays blocked, and its shadow widens", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        // A 60 high ridge two cells east, then a 150 high peak four cells east.
        GameSimulation sim(MapTerrain(makeBandedHeightmap({{36, 37, 60}, {40, 41, 150}}), 0_ss), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        defineUnit(sim, "scout", /*sight*/ 200u, /*radar*/ 0u, false, /*modelHeight*/ 20);

        auto here = SimVector(0_ss, 0_ss, 0_ss);
        addUnit(sim, "scout", us, here, script);
        sim.tick();

        const auto& vis = sim.playerVisibility.at(us.value);

        SECTION("the ridge is seen and the ground right behind it is not")
        {
            REQUIRE(vis.isVisible(eastOf(sim, here, 2)));
            REQUIRE_FALSE(vis.isVisible(eastOf(sim, here, 3)));
        }

        SECTION("a peak poking above the shadow line is seen again")
        {
            REQUIRE(vis.isVisible(eastOf(sim, here, 4)));
        }

        SECTION("and the peak in turn shadows the ground behind it")
        {
            REQUIRE_FALSE(vis.isVisible(eastOf(sim, here, 5)));
            REQUIRE_FALSE(vis.isVisible(eastOf(sim, here, 6)));
        }
    }

    TEST_CASE("vision cell heights are biased two ways", "[visibility]")
    {
        // Heightmap column 0 is 120 high, everything else is flat. Vision cell
        // column 0 therefore covers both a 120 and a 0 sample.
        Grid<unsigned char> heights(4, 16, static_cast<unsigned char>(0));
        for (int y = 0; y < 16; ++y)
        {
            heights.set(0, y, 120);
        }

        SECTION("the reveal height leans high and the occlude height leans low")
        {
            auto grid = computeVisionHeights(heights, 0);
            // (2 * 120 + 0) / 3 and (120 + 2 * 0) / 3.
            REQUIRE(static_cast<int>(grid.reveal.get(0, 2)) == 80);
            REQUIRE(static_cast<int>(grid.occlude.get(0, 2)) == 40);

            // A cell with a single height keeps it.
            REQUIRE(static_cast<int>(grid.reveal.get(1, 2)) == 0);
            REQUIRE(static_cast<int>(grid.occlude.get(1, 2)) == 0);
        }

        SECTION("the seabed does not count: sight travels over the water")
        {
            auto grid = computeVisionHeights(heights, 50);
            REQUIRE(static_cast<int>(grid.reveal.get(1, 2)) == 50);
            REQUIRE(static_cast<int>(grid.occlude.get(1, 2)) == 50);
            // The mixed cell's reveal height is above the water either way.
            REQUIRE(static_cast<int>(grid.reveal.get(0, 2)) == 80);
            REQUIRE(static_cast<int>(grid.occlude.get(0, 2)) == 50);
        }
    }

    TEST_CASE("broken ground blocks less than it reveals", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        // Heightmap column 36 is 30 high and column 37 is flat, so vision cell
        // column 18 reveals at (2 * 30 + 0) / 3 = 20 but only occludes at
        // (30 + 2 * 0) / 3 = 10.
        Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
        for (int y = 0; y < 64; ++y)
        {
            heights.set(36, y, 30);
        }
        GameSimulation sim(MapTerrain(std::move(heights), 0_ss), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        defineUnit(sim, "scout", /*sight*/ 200u, /*radar*/ 0u, false, /*modelHeight*/ 40);

        auto here = SimVector(0_ss, 0_ss, 0_ss);
        addUnit(sim, "scout", us, here, script);
        sim.tick();

        const auto& vis = sim.playerVisibility.at(us.value);

        // An eye at 41 grazing the broken cell's occlude height of 10 is
        // already below ground level by the next cell along, so the ground
        // behind it is seen. Were the cell to block at its reveal height of
        // 20 instead, it would not be.
        REQUIRE(vis.isVisible(eastOf(sim, here, 2)));
        REQUIRE(vis.isVisible(eastOf(sim, here, 3)));
    }

    TEST_CASE("visibility counts the units that can see a cell", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        defineUnit(sim, "scout", /*sight*/ 200u, /*radar*/ 0u, false);

        auto here = SimVector(0_ss, 0_ss, 0_ss);
        auto scoutId = addUnit(sim, "scout", us, here, script);
        sim.tick();

        const auto& vis = sim.playerVisibility.at(us.value);
        auto cell = sim.visionCellAt(here);

        SECTION("one unit counts once, however many of its rays cross a cell")
        {
            REQUIRE(vis.visibleCount(cell) == 1);
            REQUIRE(vis.visibleCount(eastOf(sim, here, 1)) == 1);
            REQUIRE(vis.visibleCount(eastOf(sim, here, 2)) == 1);
        }

        SECTION("a second unit on the same ground counts again")
        {
            addUnit(sim, "scout", us, here, script);
            sim.tick();
            REQUIRE(vis.visibleCount(cell) == 2);
            REQUIRE(vis.visibleCount(eastOf(sim, here, 1)) == 2);
        }

        SECTION("the count drops back when a unit dies")
        {
            addUnit(sim, "scout", us, here, script);
            sim.tick();
            sim.getUnitState(scoutId).markAsDeadNoCorpse();
            sim.tick();
            REQUIRE(vis.visibleCount(cell) == 1);
        }
    }

    TEST_CASE("the vision grid is indexed in projected space", "[visibility]")
    {
        // A 128 high band at heightmap columns 40..41 on an otherwise flat map.
        GameSimulation sim(MapTerrain(makeBandedHeightmap({{40, 41, 128}}), 0_ss), 0u, 0, 0);

        auto onFlat = SimVector(0_ss, 0_ss, 0_ss);
        auto onBand = SimVector(140_ss, 0_ss, 0_ss);

        auto flatCell = sim.visionCellAt(onFlat);
        auto bandCell = sim.visionCellAt(onBand);

        // Same world z, but the band's ground is drawn 128 / 2 = 64 world
        // units further up the screen, which is two 32 unit cells.
        REQUIRE(bandCell.y == flatCell.y - 2);
        REQUIRE(bandCell.x == flatCell.x + 4);
    }

    TEST_CASE("radar", "[visibility]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineUnit(sim, "radar", /*sight*/ 32u, /*radar*/ 300u, /*onOffable*/ true);
        defineUnit(sim, "tank", 32u, 0u, false);

        auto radarId = addUnit(sim, "radar", us, SimVector(0_ss, 0_ss, 0_ss), script);
        auto enemyId = addUnit(sim, "tank", them, SimVector(200_ss, 0_ss, 0_ss), script);

        SECTION("a switched-off radar covers nothing")
        {
            sim.getUnitState(radarId).activated = false;
            sim.tick();
            REQUIRE_FALSE(sim.canDetectUnit(us, enemyId));
            REQUIRE_FALSE(sim.isOnRadarOf(us, SimVector(200_ss, 0_ss, 0_ss)));
        }

        SECTION("an active radar detects but does not reveal")
        {
            sim.getUnitState(radarId).activated = true;
            sim.tick();
            REQUIRE(sim.isOnRadarOf(us, SimVector(200_ss, 0_ss, 0_ss)));
            REQUIRE(sim.canDetectUnit(us, enemyId));
            REQUIRE_FALSE(sim.canSeeUnit(us, enemyId));
            // Radar contacts do not count as exploring the ground.
            REQUIRE_FALSE(sim.isExploredBy(us, SimVector(200_ss, 0_ss, 0_ss)));
            REQUIRE_FALSE(sim.isVisibleTo(us, SimVector(200_ss, 0_ss, 0_ss)));
        }

        SECTION("nothing outside the range is detected")
        {
            sim.getUnitState(radarId).activated = true;
            auto& enemy = sim.getUnitState(enemyId);
            enemy.position = SimVector(400_ss, 0_ss, 0_ss);
            enemy.previousPosition = enemy.position;
            sim.tick();
            REQUIRE_FALSE(sim.canDetectUnit(us, enemyId));
        }

        SECTION("terrain does not block radar")
        {
            // Rebuild on a map with a wall between the dish and its contact.
            GameSimulation hilly(MapTerrain(makeBandedHeightmap({{36, 37, 250}}), 0_ss), 0u, 0, 0);
            auto a = addPlayer(hilly, "us");
            auto b = addPlayer(hilly, "them");
            defineUnit(hilly, "radar", 32u, 300u, true);
            defineUnit(hilly, "tank", 32u, 0u, false);
            auto dishId = addUnit(hilly, "radar", a, SimVector(0_ss, 0_ss, 0_ss), script);
            auto hiddenId = addUnit(hilly, "tank", b, SimVector(200_ss, 0_ss, 0_ss), script);
            hilly.getUnitState(dishId).activated = true;
            hilly.tick();

            REQUIRE(hilly.canDetectUnit(a, hiddenId));
            REQUIRE_FALSE(hilly.canSeeUnit(a, hiddenId));
        }

        SECTION("altitude extends radar range")
        {
            GameSimulation air(makeFlatTerrain(), 0u, 0, 0);
            auto a = addPlayer(air, "us");
            auto b = addPlayer(air, "them");
            defineUnit(air, "plane", 32u, 100u, false);
            defineUnit(air, "tank", 32u, 0u, false);
            auto targetId = addUnit(air, "tank", b, SimVector(250_ss, 0_ss, 0_ss), script);

            auto planeId = addUnit(air, "plane", a, SimVector(0_ss, 0_ss, 0_ss), script);
            air.tick();
            REQUIRE_FALSE(air.canDetectUnit(a, targetId));

            // 100 + 2 * 100 = 300 world units of reach from up there.
            auto& plane = air.getUnitState(planeId);
            plane.position = SimVector(0_ss, 100_ss, 0_ss);
            plane.previousPosition = plane.position;
            air.tick();
            REQUIRE(air.canDetectUnit(a, targetId));
        }

        SECTION("sonar detects at its own flat range")
        {
            GameSimulation sea(makeFlatTerrain(), 0u, 0, 0);
            auto a = addPlayer(sea, "us");
            auto b = addPlayer(sea, "them");
            defineUnit(sea, "sonar", 32u, /*radar*/ 0u, false, /*modelHeight*/ 0, /*sonar*/ 300u);
            defineUnit(sea, "sub", 32u, 0u, false);
            addUnit(sea, "sonar", a, SimVector(0_ss, 0_ss, 0_ss), script);
            auto subId = addUnit(sea, "sub", b, SimVector(200_ss, 0_ss, 0_ss), script);
            sea.tick();

            REQUIRE(sea.canDetectUnit(a, subId));
            REQUIRE_FALSE(sea.canSeeUnit(a, subId));
        }

        SECTION("units always see and detect their own")
        {
            sim.getUnitState(radarId).activated = false;
            sim.tick();
            REQUIRE(sim.canSeeUnit(them, enemyId));
            REQUIRE(sim.canDetectUnit(them, enemyId));
        }
    }

    TEST_CASE("a radar contact is a minimap dot and nothing in the world", "[visibility]")
    {
        // The original's world render never walks the unit list: it consumes a
        // list rebuilt each frame by 0x48BAE0, which admits a unit only if it
        // is the viewer's own or passes the can-see predicate at 0x465AC0 --
        // and that predicate reads the line-of-sight grid and never looks at
        // the radar bits. The minimap draw at 0x466DC0 is the one place that
        // takes the raw detection bits, so a radar-only contact appears there
        // and nowhere else. These are the two predicates RWE draws from:
        // canSeeUnit gates the world, canDetectUnit gates the minimap dot.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        defineUnit(sim, "radar", /*sight*/ 32u, /*radar*/ 300u, /*onOffable*/ true);
        defineUnit(sim, "tank", 32u, 0u, false);

        auto radarId = addUnit(sim, "radar", us, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(radarId).activated = true;

        SECTION("a contact out of sight but on radar gets the dot only")
        {
            auto enemyId = addUnit(sim, "tank", them, SimVector(200_ss, 0_ss, 0_ss), script);
            sim.tick();

            REQUIRE(sim.canDetectUnit(us, enemyId));
            REQUIRE_FALSE(sim.canSeeUnit(us, enemyId));
        }

        SECTION("a contact inside sight range gets both")
        {
            auto enemyId = addUnit(sim, "tank", them, SimVector(16_ss, 0_ss, 0_ss), script);
            sim.tick();

            REQUIRE(sim.canDetectUnit(us, enemyId));
            REQUIRE(sim.canSeeUnit(us, enemyId));
        }

        SECTION("a contact outside both gets neither")
        {
            auto enemyId = addUnit(sim, "tank", them, SimVector(600_ss, 0_ss, 0_ss), script);
            sim.tick();

            REQUIRE_FALSE(sim.canDetectUnit(us, enemyId));
            REQUIRE_FALSE(sim.canSeeUnit(us, enemyId));
        }
    }
}
