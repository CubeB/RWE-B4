#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/MovieScene.h>
#include <rwe/LoadingScene_util.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitVisibility.h>
#include <rwe/resource_io.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/MeshService.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>
#include <cstdlib>
#include <algorithm>

/**
 * Small pure rules of how the game is drawn, each pinned to a byte-tactics
 * ruling (static reading at 97d7ac59, not executed against the original).
 * Nothing here reaches the simulation.
 */
namespace rwe
{
    TEST_CASE("a contact that is not visible reads Unidentified object with its sensor prefix (BT-141)", "[presentation]")
    {
        REQUIRE(unidentifiedObjectText(false) == "R: Unidentified object");
        REQUIRE(unidentifiedObjectText(true) == "S: Unidentified object");
    }

    TEST_CASE("pointing at a radar or sonar contact on the minimap reads it in the footer, a visible unit's dot does not (BT-141)", "[presentation]")
    {
        // In the world a unit is read whatever it is (the pick only finds what
        // can be seen anyway).
        REQUIRE(footerReadsHoveredUnit(false, true));
        REQUIRE(footerReadsHoveredUnit(false, false));

        // On the minimap a contact is read -- it can be pointed at nowhere
        // else -- and its line names nothing...
        REQUIRE(footerReadsHoveredUnit(true, false));

        // ...but a unit that can be seen is not named from its dot.
        REQUIRE_FALSE(footerReadsHoveredUnit(true, true));
    }

    TEST_CASE("vent steam is drawn wherever the ground is explored and dimmed where it is not in sight (BT-151)", "[presentation]")
    {
        REQUIRE(ventSteamDrawMode(true, true) == VentSteamDrawMode::Normal);
        REQUIRE(ventSteamDrawMode(true, false) == VentSteamDrawMode::Dimmed);
        REQUIRE(ventSteamDrawMode(false, false) == VentSteamDrawMode::Hidden);
    }

    TEST_CASE("rendering and simulation share the original's any-corner unit visibility", "[presentation][visibility]")
    {
        // CV-02: shipped ARMMEX has a 3x3 footprint and model height 28.6;
        // ARMLAB has a 6x6 footprint and height 14.75. The old draw gate
        // demanded the centre AND every ground corner, contrary to this rule.
        unsigned int footprint = 3;
        SimScalar height(28.6f);
        SECTION("metal extractor") {}
        SECTION("factory")
        {
            footprint = 6;
            height = SimScalar(14.75f);
        }

        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto enemy = addPlayer(sim, "enemy");
        auto& visibility = sim.playerVisibility.at(player.value);
        UnitDefinition plant{};
        plant.isMobile = false;
        plant.objectName = "model";
        plant.movementCollisionInfo = UnitDefinition::AdHocMovementClass{footprint, footprint, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["plant"] = plant;
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(height, {});
        auto centre = SimVector(512_ss, 0_ss, 512_ss);
        auto unit = addUnitOfType(sim, "plant", enemy, centre, makeEmptyCobScript());
        REQUIRE_FALSE(canSeeUnitInVisibility(sim, player, unit, visibility));

        auto half = intToSimScalar(static_cast<int>(footprint) * 8);
        auto corner = sim.visionCellAtHeight(SimVector(centre.x - half, 0_ss, centre.z - half), height);
        REQUIRE(visibility.contains(corner));
        visibility.visible.set(corner.x, corner.y, 1);
        REQUIRE_FALSE(sim.isVisibleTo(player, centre));
        REQUIRE(sim.canSeeUnit(player, unit));
        REQUIRE(canSeeUnitInVisibility(sim, player, unit, visibility));
        REQUIRE(computeUnitDrawStyle(false, false, canSeeUnitInVisibility(sim, player, unit, visibility)) == UnitDrawStyle::Solid);

        // Watching/revealed pictures are supplied explicitly; the query must
        // neither fall back to nor modify the simulation player's picture.
        auto watching = visibility;
        visibility.clearCurrent();
        REQUIRE_FALSE(sim.canSeeUnit(player, unit));
        REQUIRE(canSeeUnitInVisibility(sim, player, unit, watching));
        REQUIRE_FALSE(sim.canSeeUnit(player, unit));

        sim.getUnitState(unit).cloaked = true;
        REQUIRE_FALSE(canSeeUnitInVisibility(sim, player, unit, watching));
        REQUIRE(canSeeUnitInVisibility(sim, enemy, unit, watching));
    }

    TEST_CASE("a film is drawn at x 0, y = (480 - height) / 2, unscaled (BT-157)", "[presentation]")
    {
        // Film 5 is 640 by 304 with no flag: in its own pixels at y 88, bars
        // above and below, not stretched to 4:3.
        auto film5 = moviePlacement(640, 304, 0);
        REQUIRE(film5.x == 0);
        REQUIRE(film5.y == 88);
        REQUIRE(film5.width == 640);
        REQUIRE(film5.height == 304);

        // Films 1 to 4 are 640 by 240 with the height-doubling flag: they
        // fill the screen from the top.
        auto film1 = moviePlacement(640, 240, 2);
        REQUIRE(film1.x == 0);
        REQUIRE(film1.y == 0);
        REQUIRE(film1.width == 640);
        REQUIRE(film1.height == 480);

        // A full-height film needs no bars.
        auto full = moviePlacement(640, 480, 0);
        REQUIRE(full.y == 0);
        REQUIRE(full.height == 480);
    }

    TEST_CASE("the resource HUD's displayed amount moves an eighth of the gap, at least one (BT-148)", "[presentation]")
    {
        // 1000 away: an eighth is 125.
        REQUIRE(hudGlideStep(0.0f, 1000.0f, 2000.0f) == Catch::Approx(125.0f));
        // Both are truncated first: 10.9 is 10, 100.9 is 100, 90 / 8 is 11.
        REQUIRE(hudGlideStep(10.9f, 100.9f, 2000.0f) == Catch::Approx(21.0f));
        // Less than eight away the eighth truncates to zero and the step is forced to one.
        REQUIRE(hudGlideStep(10.0f, 15.0f, 2000.0f) == Catch::Approx(11.0f));
        REQUIRE(hudGlideStep(15.0f, 10.0f, 2000.0f) == Catch::Approx(14.0f));
        // Downwards the division truncates toward zero: -90 / 8 is -11.
        REQUIRE(hudGlideStep(100.0f, 10.0f, 2000.0f) == Catch::Approx(89.0f));
        // Already there stays there.
        REQUIRE(hudGlideStep(40.0f, 40.0f, 2000.0f) == Catch::Approx(40.0f));
        // Held to the storage capacity.
        REQUIRE(hudGlideStep(95.0f, 5000.0f, 100.0f) == Catch::Approx(100.0f));
    }

    TEST_CASE("a resource bar fills an inclusive rectangle and is drawn only with storage (BT-148)", "[presentation]")
    {
        // 100 pixels wide: the bar's width is 99 and the rectangle inclusive.
        REQUIRE(hudBarFillPixels(100, 1000.0f, 1000.0f) == 100);
        REQUIRE(hudBarFillPixels(100, 500.0f, 1000.0f) == 50);
        REQUIRE(hudBarFillPixels(100, 0.0f, 1000.0f) == 1);
        REQUIRE(hudBarFillPixels(100, 3000.0f, 1000.0f) == 100);
        REQUIRE(hudBarFillPixels(100, 500.0f, 0.0f) == 0);
    }

    TEST_CASE("the share marker needs a threshold above zero and an amount above it (BT-148)", "[presentation]")
    {
        REQUIRE(hudShareMarkerWidth == 3);
        // Threshold 500 of 1000 on a 100 pixel bar is 49 pixels in.
        REQUIRE(hudShareMarkerOffset(100, 500.0f, 1000.0f, 800.0f) == 49);
        // Strictly above: at the threshold, no marker.
        REQUIRE_FALSE(hudShareMarkerOffset(100, 500.0f, 1000.0f, 500.0f).has_value());
        REQUIRE_FALSE(hudShareMarkerOffset(100, 0.0f, 1000.0f, 800.0f).has_value());
        REQUIRE_FALSE(hudShareMarkerOffset(100, -5.0f, 1000.0f, 800.0f).has_value());
    }

    TEST_CASE("energy income takes a K above 99999 and use only below minus 99999 (BT-148)", "[presentation]")
    {
        REQUIRE(formatHudEnergyIncome(Energy(1234.9f)) == "1234");
        REQUIRE(formatHudEnergyIncome(Energy(99999.0f)) == "99999");
        REQUIRE(formatHudEnergyIncome(Energy(123456.0f)) == "123K");

        REQUIRE(formatHudEnergyUse(Energy(1234.9f)) == "1234");
        // The quirk: a positive six-digit use prints in full.
        REQUIRE(formatHudEnergyUse(Energy(123456.0f)) == "123456");
        REQUIRE(formatHudEnergyUse(Energy(-250000.0f)) == "250K");
        REQUIRE(formatHudEnergyUse(Energy(-99999.0f)) == "99999");
    }

    TEST_CASE("an enemy's underwater parts are cut, the viewer's own and sonar contacts are tinted (BT-142)", "[presentation]")
    {
        REQUIRE_FALSE(unitWaterPartsAreCut(true, false));
        REQUIRE_FALSE(unitWaterPartsAreCut(true, true));
        REQUIRE_FALSE(unitWaterPartsAreCut(false, true));
        REQUIRE(unitWaterPartsAreCut(false, false));
    }

    TEST_CASE("watching admits shipped submerged models without changing simulation sensors", "[presentation][watching][retail]")
    {
        const auto* dataPath = std::getenv("RWE_SCENARIO_DATA");
        if (!dataPath)
        {
            SKIP("set RWE_SCENARIO_DATA to the retail archive directory");
        }
        auto vfs = constructVfs(dataPath);
        MeshService meshes(&vfs, nullptr, {}, {}, {});
        GameSimulation sim(MapTerrain(Grid<unsigned char>(64, 64, 0), 255_ss), 0, 0, 0);
        auto viewer = addPlayer(sim);
        auto owner = addPlayer(sim);
        std::vector<UnitId> units;
        for (const auto* name : {"ARMUWMEX", "CORUWMEX", "ARMSUB", "ARMTIDE"})
        {
            CAPTURE(name);
            auto bytes = vfs.readFile(std::string("units/") + name + ".fbi");
            REQUIRE(bytes);
            auto definition = parseUnitDefinition(parseUnitFbi(parseTdfFromBytes(*bytes)), sim.movementClassDatabase);
            sim.unitModelDefinitions[definition.objectName] = meshes.loadUnitMesh(definition.objectName).modelDefinition;
            sim.unitDefinitions[name] = definition;
            auto position = SimVector(SimScalar(static_cast<int>(units.size()) * 64), 0_ss, 0_ss);
            if (std::string(name) == "ARMTIDE")
            {
                position.y = 247_ss; // Shipped WaterLine=8, sea=255.
            }
            units.push_back(addUnitOfType(sim, name, owner, position, makeEmptyCobScript()));
        }
        auto picture = sim.playerVisibility.at(viewer.value);
        std::fill(picture.visible.getVector().begin(), picture.visible.getVector().end(), static_cast<unsigned char>(1));
        for (std::size_t i = 0; i < 3; ++i)
        {
            auto id = units[i];
            REQUIRE(sim.getUnitState(id).position.y + sim.modelHeightOf(sim.unitDefinitions.at(sim.getUnitState(id).unitType)) < 255_ss);
            REQUIRE_FALSE(canSeeUnitInVisibility(sim, viewer, id, picture));
            REQUIRE(canSeeUnitInVisibility(sim, owner, id, picture));
        }
        REQUIRE(canSeeUnitInVisibility(sim, viewer, units.back(), picture)); // Surface part stays visible.
        auto hash = computeHashOf(sim);
        grantWatchingSonarContacts(sim, viewer, picture);
        for (auto id : units)
        {
            REQUIRE(canSeeUnitInVisibility(sim, viewer, id, picture));
            REQUIRE_FALSE(unitWaterPartsAreCut(false, picture.sonarContacts.contains(id)));
        }
        REQUIRE(computeHashOf(sim) == hash);
        REQUIRE(sim.playerVisibility.at(viewer.value).sonarContacts.empty());

        // A real foreign-sonar-jammer entry erases the watcher grant again.
        sim.playerVisibility.at(viewer.value).radarJammers.push_back({SimVector(0_ss, 0_ss, 0_ss), 100_ss, true});
        grantWatchingSonarContacts(sim, viewer, picture);
        REQUIRE_FALSE(canSeeUnitInVisibility(sim, viewer, units.front(), picture));
        REQUIRE(unitWaterPartsAreCut(false, picture.sonarContacts.contains(units.front())));
        REQUIRE(canSeeUnitInVisibility(sim, viewer, units[1], picture));
        REQUIRE(sim.playerVisibility.at(viewer.value).sonarContacts.empty());
    }

    TEST_CASE("an FBI that says digger=1 makes a digger definition (BT-143)", "[presentation]")
    {
        MovementClassDatabase movementClasses;
        auto definitionFrom = [&](const std::string& keys) {
            const std::string nl(1, static_cast<char>(10));
            std::string text = "[UNITINFO]" + nl + "{" + nl + "UnitName=ARMAMB;" + nl + "Objectname=model;" + nl + "SoundCategory=NOSOUND;" + nl + keys + nl + "}" + nl;
            auto fbi = parseUnitFbi(parseTdfFromString(text));
            return parseUnitDefinition(fbi, movementClasses);
        };

        REQUIRE(definitionFrom("digger=1;").digger);
        REQUIRE_FALSE(definitionFrom("").digger);
    }

    TEST_CASE("the minimap picture is fitted into 126 pixels and centred (BT-152)", "[presentation]")
    {
        auto box = Rectangle2f::fromTopLeft(0.0f, 0.0f, 128.0f, 128.0f);

        // A square map is 126 by 126, in the middle of the 128 box.
        auto square = minimapPictureRect(box, 252.0f, 252.0f);
        REQUIRE(square.width() == Catch::Approx(126.0f));
        REQUIRE(square.height() == Catch::Approx(126.0f));
        REQUIRE(square.left() == Catch::Approx(1.0f));
        REQUIRE(square.top() == Catch::Approx(1.0f));

        // Twice as wide as tall: 126 by 63, the shorter side centred.
        auto wide = minimapPictureRect(box, 252.0f, 126.0f);
        REQUIRE(wide.width() == Catch::Approx(126.0f));
        REQUIRE(wide.height() == Catch::Approx(63.0f));
        REQUIRE(wide.left() == Catch::Approx(1.0f));
        REQUIRE(wide.top() == Catch::Approx(32.0f));

        // The aspect is truncated in integers: 126 x 100 / 252 is 50.
        auto tall = minimapPictureRect(box, 100.0f, 252.0f);
        REQUIRE(tall.height() == Catch::Approx(126.0f));
        REQUIRE(tall.width() == Catch::Approx(50.0f));
        REQUIRE(tall.left() == Catch::Approx(1.0f + 38.0f));
    }

    TEST_CASE("the minimap picture is black unexplored, the base seen and grey remembered (BT-152)", "[presentation]")
    {
        // A grey-ramp palette, so the gray table has a known answer: the mean
        // of a colour is a grey that is in the palette.
        ColorPalette palette;
        for (int i = 0; i < 256; ++i)
        {
            palette.emplace_back(static_cast<unsigned char>(i), static_cast<unsigned char>(i), static_cast<unsigned char>(i));
        }
        std::vector<unsigned char> ramp;
        for (int i = 0; i < 256; ++i)
        {
            ramp.push_back(static_cast<unsigned char>(i));
        }

        // A 2 by 2 base, one picture pixel to each, one fog cell to each.
        std::vector<Color> base{Color(90, 30, 0), Color(200, 100, 0), Color(10, 20, 30), Color(255, 255, 255)};
        auto state = [](int x, int y) {
            if (x == 0 && y == 0)
            {
                return MinimapCellState::Remembered;
            }
            if (x == 1 && y == 0)
            {
                return MinimapCellState::Seen;
            }
            return MinimapCellState::Unexplored;
        };
        auto picture = composeMinimapPicture(2, 2, base, 2, 2, ramp, palette, 2, 2, state);
        REQUIRE(picture.size() == 4);

        // (0, 0) is remembered: (90 + 30 + 0) / 3 = 40 in all three channels.
        REQUIRE(picture[0].r == 40);
        REQUIRE(picture[0].g == 40);
        REQUIRE(picture[0].b == 40);
        REQUIRE(picture[0].a == 255);

        // (1, 0) is in sight: the base pixel untouched.
        REQUIRE(picture[1].r == 200);
        REQUIRE(picture[1].g == 100);
        REQUIRE(picture[1].b == 0);

        // The unexplored row is opaque black whatever the base holds.
        for (int i = 2; i < 4; ++i)
        {
            REQUIRE(picture[i].r == 0);
            REQUIRE(picture[i].g == 0);
            REQUIRE(picture[i].b == 0);
            REQUIRE(picture[i].a == 255);
        }
    }

    TEST_CASE("a feature marked nodisplayinfo shows no footer text (BT-147)", "[presentation]")
    {
        FeatureDefinition ordinary{};
        ordinary.noDisplayInfo = false;
        REQUIRE(featureFooterIsShown(ordinary));

        FeatureDefinition iceChunk{};
        iceChunk.noDisplayInfo = true;
        REQUIRE_FALSE(featureFooterIsShown(iceChunk));
    }

    TEST_CASE("the nano spray lands within plus or minus 3/11 of the target's extent about its centre", "[presentation]")
    {
        // The look the owner prefers, kept over BT-144's 4/11 to 7/11 window
        // (half as wide, which drew the stream as a tight bundle through the
        // middle of the building). Six elevenths wide.
        REQUIRE(NanoLandingHalfWidth == Catch::Approx(3.0f / 11.0f));
        REQUIRE((NanoLandingHalfWidth * 2.0f) == Catch::Approx(6.0f / 11.0f));
    }
}
