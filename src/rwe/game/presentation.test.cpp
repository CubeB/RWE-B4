#include <algorithm>
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
#include <rwe/resource_io.h>
#include <rwe/sim/sim_test_util.h>

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

    TEST_CASE("vent steam is drawn wherever the ground is explored and dimmed where it is not in sight (BT-151)", "[presentation]")
    {
        REQUIRE(ventSteamDrawMode(true, true) == VentSteamDrawMode::Normal);
        REQUIRE(ventSteamDrawMode(true, false) == VentSteamDrawMode::Dimmed);
        REQUIRE(ventSteamDrawMode(false, false) == VentSteamDrawMode::Hidden);
    }

    TEST_CASE("the target ring is an ellipse, R across and 0.89 R up and down (BT-185)", "[presentation]")
    {
        auto segments = targetRingSegments(Vector2f(100.0f, 50.0f), 32);

        // Sixteen segments of two points each.
        REQUIRE(segments.size() == 32);

        float minX = 1e9f;
        float maxX = -1e9f;
        float minY = 1e9f;
        float maxY = -1e9f;
        for (const auto& p : segments)
        {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        // Horizontal radius 32, vertical (int)(32 x 0.89) = 28.
        REQUIRE(maxX == Catch::Approx(132.0f));
        REQUIRE(minX == Catch::Approx(68.0f));
        REQUIRE(maxY == Catch::Approx(78.0f));
        REQUIRE(minY == Catch::Approx(22.0f));

        // The segments run on from one another and close.
        for (std::size_t i = 1; i < segments.size() - 1; i += 2)
        {
            REQUIRE(segments[i].x == Catch::Approx(segments[i + 1].x));
            REQUIRE(segments[i].y == Catch::Approx(segments[i + 1].y));
        }
        REQUIRE(segments.back().x == Catch::Approx(segments.front().x));
        REQUIRE(segments.back().y == Catch::Approx(segments.front().y));
    }

    TEST_CASE("the target ring's radius is the target's footprint radius, or 32 with no target unit (BT-185)", "[presentation]")
    {
        REQUIRE(targetRingRadius(std::nullopt) == 32);
        REQUIRE(targetRingRadius(std::pair<unsigned int, unsigned int>{2, 2}) == 16);
        REQUIRE(targetRingRadius(std::pair<unsigned int, unsigned int>{3, 5}) == 40);
    }

    TEST_CASE("only an order with a target unit is ringed while Shift is held, not a move or a patrol point (BT-185)", "[presentation]")
    {
        UnitState unit(std::vector<UnitMesh>{}, nullptr);

        // A place is not a target: the ring is drawn only where the order
        // names a unit, so none of these gets one.
        REQUIRE_FALSE(orderTargetUnit(UnitOrder(MoveOrder(SimVector(10_ss, 0_ss, 10_ss))), unit).has_value());
        REQUIRE_FALSE(orderTargetUnit(UnitOrder(PatrolOrder(SimVector(10_ss, 0_ss, 10_ss))), unit).has_value());
        REQUIRE_FALSE(orderTargetUnit(UnitOrder(UnloadOrder(SimVector(10_ss, 0_ss, 10_ss))), unit).has_value());

        // A unit target is.
        UnitId target(7);
        auto attack = orderTargetUnit(createAttackOrder(target), unit);
        REQUIRE(attack.has_value());
        REQUIRE(*attack == target);
        REQUIRE(*orderTargetUnit(UnitOrder(GuardOrder(target)), unit) == target);
    }

    TEST_CASE("an enemy structure is drawn only when the ground under it is in sight, not when one cell at its centre is", "[presentation]")
    {
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto& visibility = sim.playerVisibility.at(player.value);

        UnitDefinition plant{};
        plant.isMobile = false;
        // Six cells across, as a vehicle plant is: three vision cells wide.
        plant.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 0u, 0u};

        auto centre = SimVector(512_ss, 0_ss, 512_ss);
        auto light = [&](const SimVector& at) {
            auto cell = sim.visionCellAt(at);
            REQUIRE(visibility.contains(cell));
            visibility.visible.set(cell.x, cell.y, static_cast<unsigned char>(1));
        };

        // Nothing lit: not drawn.
        REQUIRE_FALSE(structureGroundIsInSight(sim, visibility, centre, plant));

        // Only the cell at its centre, which is all the position test asks.
        light(centre);
        REQUIRE(visibility.isVisible(sim.visionCellAt(centre)));
        REQUIRE_FALSE(structureGroundIsInSight(sim, visibility, centre, plant));

        // Three corners and the centre: still ground in the dark.
        light(SimVector(centre.x - 48_ss, 0_ss, centre.z - 48_ss));
        light(SimVector(centre.x + 48_ss, 0_ss, centre.z - 48_ss));
        light(SimVector(centre.x - 48_ss, 0_ss, centre.z + 48_ss));
        REQUIRE_FALSE(structureGroundIsInSight(sim, visibility, centre, plant));

        // The fourth corner: the whole footprint is uncovered.
        light(SimVector(centre.x + 48_ss, 0_ss, centre.z + 48_ss));
        REQUIRE(structureGroundIsInSight(sim, visibility, centre, plant));
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
