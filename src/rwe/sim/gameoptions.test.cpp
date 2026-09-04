#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * 64x64 heightmap tiles of 16 world units each, so the map runs from
         * world -512 to +512 on both axes and the vision grid is 32x32 cells
         * of 32 world units. Everything below is written in world units and
         * converted by visionCellAt, exactly as the game does it.
         */
        MapTerrain makeOptionsTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * The same map with bands running along z, given as (firstColumn,
         * lastColumn, height). A band's height does not vary along z, so every
         * vision cell in the columns it covers ends up at that height whatever
         * the projected-space skew does to the rows.
         */
        MapTerrain makeRidgeTerrain(const std::vector<std::tuple<int, int, int>>& bands)
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
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addOptionsPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(100000.0f),
                Energy(100000.0f),
                Metal(100000.0f),
                Energy(100000.0f),
                Metal(100000.0f),
                Energy(100000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeOptionsScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            return script;
        }

        void defineOptionsUnit(GameSimulation& sim, const std::string& type, unsigned int sight, bool commander = false)
        {
            UnitDefinition d{};
            d.objectName = type;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = sight;
            d.commander = commander;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;

            UnitModelDefinition model;
            model.height = 0_ss;
            sim.unitModelDefinitions[type] = model;
        }

        UnitId addOptionsUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& position, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = type;
            unit.owner = owner;
            unit.position = position;
            unit.previousPosition = position;
            unit.hitPoints = 100;
            return unitId;
        }

        /** A corner of the map far outside any of these tests' sight ranges. */
        const SimVector FarCorner(400_ss, 0_ss, 400_ss);
    }

    TEST_CASE("Mapped hands over the ground and nothing else", "[gameoptions]")
    {
        auto script = makeOptionsScript();

        SECTION("Unmapped starts black, as it always has")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            auto us = addOptionsPlayer(sim, "us");
            REQUIRE_FALSE(sim.isExploredBy(us, FarCorner));
        }

        SECTION("Mapped starts explored but unlit")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.mappingMode = MappingMode::Mapped;
            auto us = addOptionsPlayer(sim, "us");

            // Before a single tick: the ground is known, the light is not on.
            REQUIRE(sim.isExploredBy(us, FarCorner));
            REQUIRE_FALSE(sim.isVisibleTo(us, FarCorner));
            REQUIRE(sim.isExploredBy(us, SimVector(0_ss, 0_ss, 0_ss)));
        }

        SECTION("a unit standing on mapped ground is still hidden")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.mappingMode = MappingMode::Mapped;
            auto us = addOptionsPlayer(sim, "us");
            auto them = addOptionsPlayer(sim, "them");
            defineOptionsUnit(sim, "scout", 100u);

            addOptionsUnit(sim, "scout", us, SimVector(0_ss, 0_ss, 0_ss), script);
            auto theirsId = addOptionsUnit(sim, "scout", them, FarCorner, script);

            sim.tick();

            REQUIRE(sim.isExploredBy(us, FarCorner));
            REQUIRE_FALSE(sim.isVisibleTo(us, FarCorner));
            REQUIRE_FALSE(sim.canSeeUnit(us, theirsId));
            REQUIRE_FALSE(sim.canDetectUnit(us, theirsId));
        }

        SECTION("a player added later is mapped too")
        {
            // The grid is handed over as the player is created, so it does not
            // matter whether a player joins before or after the option is read.
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.mappingMode = MappingMode::Mapped;
            addOptionsPlayer(sim, "us");
            auto late = addOptionsPlayer(sim, "late");
            REQUIRE(sim.isExploredBy(late, FarCorner));
        }
    }

    TEST_CASE("Circular sight ignores the ground it crosses", "[gameoptions]")
    {
        auto script = makeOptionsScript();

        // A tall ridge across heightmap columns 36..37 -- vision cell column
        // 18 -- on an otherwise flat map. The scout sits at world x = 0, in
        // cell column 16, so the ridge is two cells east of it.
        auto behindTheRidge = SimVector(160_ss, 0_ss, 0_ss);
        auto wellBehindTheRidge = SimVector(190_ss, 0_ss, 0_ss);

        SECTION("True is blocked by the ridge")
        {
            GameSimulation sim(makeRidgeTerrain({{36, 37, 120}}), 0u, 0, 0);
            auto us = addOptionsPlayer(sim, "us");
            defineOptionsUnit(sim, "scout", 200u);
            addOptionsUnit(sim, "scout", us, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.tick();

            REQUIRE(sim.isVisibleTo(us, SimVector(80_ss, 0_ss, 0_ss)));
            REQUIRE_FALSE(sim.isVisibleTo(us, behindTheRidge));
            REQUIRE_FALSE(sim.isVisibleTo(us, wellBehindTheRidge));
        }

        SECTION("Circular sees straight over it")
        {
            GameSimulation sim(makeRidgeTerrain({{36, 37, 120}}), 0u, 0, 0);
            sim.lineOfSightMode = LineOfSightMode::Circular;
            auto us = addOptionsPlayer(sim, "us");
            defineOptionsUnit(sim, "scout", 200u);
            addOptionsUnit(sim, "scout", us, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.tick();

            REQUIRE(sim.isVisibleTo(us, behindTheRidge));
            REQUIRE(sim.isVisibleTo(us, wellBehindTheRidge));
        }

        SECTION("Circular is a radius, not a licence to see everything")
        {
            GameSimulation sim(makeRidgeTerrain({{36, 37, 120}}), 0u, 0, 0);
            sim.lineOfSightMode = LineOfSightMode::Circular;
            auto us = addOptionsPlayer(sim, "us");
            defineOptionsUnit(sim, "scout", 200u);
            addOptionsUnit(sim, "scout", us, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.tick();

            // Two hundred world units is six cells; nine cells out is dark.
            REQUIRE_FALSE(sim.isVisibleTo(us, SimVector(300_ss, 0_ss, 0_ss)));
            REQUIRE_FALSE(sim.isVisibleTo(us, FarCorner));
        }

        SECTION("a unit with no sight at all still sees only its own cell")
        {
            // The original's smallest circular mask is five cells across even
            // for a blind unit. RWE deliberately does not copy that.
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.lineOfSightMode = LineOfSightMode::Circular;
            auto us = addOptionsPlayer(sim, "us");
            defineOptionsUnit(sim, "blind", 0u);
            addOptionsUnit(sim, "blind", us, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.tick();

            REQUIRE(sim.isVisibleTo(us, SimVector(0_ss, 0_ss, 0_ss)));
            REQUIRE_FALSE(sim.isVisibleTo(us, SimVector(64_ss, 0_ss, 0_ss)));
        }
    }

    TEST_CASE("Permanent keeps what has been seen lit", "[gameoptions]")
    {
        auto script = makeOptionsScript();
        auto home = SimVector(0_ss, 0_ss, 0_ss);
        auto away = SimVector(400_ss, 0_ss, 0_ss);

        SECTION("True lets the ground fade back to memory")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            auto us = addOptionsPlayer(sim, "us");
            auto them = addOptionsPlayer(sim, "them");
            defineOptionsUnit(sim, "scout", 100u);

            auto scoutId = addOptionsUnit(sim, "scout", us, home, script);
            auto theirsId = addOptionsUnit(sim, "scout", them, home, script);
            sim.tick();
            REQUIRE(sim.canSeeUnit(us, theirsId));

            sim.getUnitState(scoutId).position = away;
            sim.tick();

            REQUIRE(sim.isExploredBy(us, home));
            REQUIRE_FALSE(sim.isVisibleTo(us, home));
            REQUIRE_FALSE(sim.canSeeUnit(us, theirsId));
        }

        SECTION("Permanent leaves the light on, and the unit under it")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.lineOfSightMode = LineOfSightMode::Permanent;
            auto us = addOptionsPlayer(sim, "us");
            auto them = addOptionsPlayer(sim, "them");
            defineOptionsUnit(sim, "scout", 100u);

            auto scoutId = addOptionsUnit(sim, "scout", us, home, script);
            auto theirsId = addOptionsUnit(sim, "scout", them, home, script);
            sim.tick();

            sim.getUnitState(scoutId).position = away;
            sim.tick();

            REQUIRE(sim.isVisibleTo(us, home));
            REQUIRE(sim.canSeeUnit(us, theirsId));
            REQUIRE(sim.canDetectUnit(us, theirsId));
        }

        SECTION("but a unit walking into ground nobody has been to disappears")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.lineOfSightMode = LineOfSightMode::Permanent;
            auto us = addOptionsPlayer(sim, "us");
            auto them = addOptionsPlayer(sim, "them");
            defineOptionsUnit(sim, "scout", 100u);

            addOptionsUnit(sim, "scout", us, home, script);
            auto theirsId = addOptionsUnit(sim, "scout", them, home, script);
            sim.tick();
            REQUIRE(sim.canSeeUnit(us, theirsId));

            // Off into a corner of the map we have never been near.
            sim.getUnitState(theirsId).position = FarCorner;
            sim.tick();

            REQUIRE_FALSE(sim.isExploredBy(us, FarCorner));
            REQUIRE_FALSE(sim.isVisibleTo(us, FarCorner));
            REQUIRE_FALSE(sim.canSeeUnit(us, theirsId));
            REQUIRE_FALSE(sim.canDetectUnit(us, theirsId));

            // And walking back into the lit ground brings it straight back.
            sim.getUnitState(theirsId).position = home;
            sim.tick();
            REQUIRE(sim.canSeeUnit(us, theirsId));
        }

        SECTION("Permanent with Mapped is the whole map, all the time")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.lineOfSightMode = LineOfSightMode::Permanent;
            sim.mappingMode = MappingMode::Mapped;
            auto us = addOptionsPlayer(sim, "us");
            auto them = addOptionsPlayer(sim, "them");
            defineOptionsUnit(sim, "scout", 100u);

            addOptionsUnit(sim, "scout", us, home, script);
            auto theirsId = addOptionsUnit(sim, "scout", them, FarCorner, script);

            sim.tick();

            REQUIRE(sim.isVisibleTo(us, FarCorner));
            REQUIRE(sim.canSeeUnit(us, theirsId));

            // Nowhere left to hide: there is no unexplored ground to walk into.
            sim.getUnitState(theirsId).position = SimVector(-400_ss, 0_ss, -400_ss);
            sim.tick();
            REQUIRE(sim.canSeeUnit(us, theirsId));
        }
    }

    TEST_CASE("start positions are dealt, never invented", "[gameoptions]")
    {
        std::vector<int> fourSlots{1, 2, 3, 4};

        SECTION("Fixed gives every slot its own position back")
        {
            std::minstd_rand rng(1234u);
            REQUIRE(dealStartPositions(fourSlots, StartLocationMode::Fixed, rng) == fourSlots);
        }

        SECTION("Fixed does not disturb the generator")
        {
            // Whether the deal draws matters: everything else seeded from this
            // stream -- the wind, the AI -- would shift under it otherwise.
            std::minstd_rand rng(1234u);
            dealStartPositions(fourSlots, StartLocationMode::Fixed, rng);
            std::minstd_rand untouched(1234u);
            REQUIRE(rng() == untouched());
        }

        SECTION("Random uses each of the map's positions exactly once")
        {
            std::minstd_rand rng(99u);
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                auto dealt = dealStartPositions(fourSlots, StartLocationMode::Random, rng);
                REQUIRE(dealt.size() == fourSlots.size());
                auto sorted = dealt;
                std::sort(sorted.begin(), sorted.end());
                REQUIRE(sorted == fourSlots);
            }
        }

        SECTION("Random deals the slots the map actually has, gaps and all")
        {
            // Players in lobby slots 2, 5 and 9 own StartPos3, 6 and 10; the
            // shuffle moves them between each other and nowhere else.
            std::vector<int> gappy{3, 6, 10};
            std::minstd_rand rng(7u);
            auto dealt = dealStartPositions(gappy, StartLocationMode::Random, rng);
            auto sorted = dealt;
            std::sort(sorted.begin(), sorted.end());
            REQUIRE(sorted == gappy);
        }

        SECTION("the same seed deals the same hand")
        {
            std::minstd_rand a(20260904u);
            std::minstd_rand b(20260904u);
            auto first = dealStartPositions(fourSlots, StartLocationMode::Random, a);
            auto second = dealStartPositions(fourSlots, StartLocationMode::Random, b);
            REQUIRE(first == second);

            // And the stream advances in step, so a later draw agrees too.
            REQUIRE(a() == b());
        }

        SECTION("and it really does shuffle")
        {
            bool sawSomethingElse = false;
            for (unsigned int seed = 1; seed <= 64 && !sawSomethingElse; ++seed)
            {
                std::minstd_rand rng(seed);
                sawSomethingElse = dealStartPositions(fourSlots, StartLocationMode::Random, rng) != fourSlots;
            }
            REQUIRE(sawSomethingElse);
        }

        SECTION("one player is dealt the one position there is")
        {
            std::minstd_rand rng(3u);
            std::vector<int> one{1};
            REQUIRE(dealStartPositions(one, StartLocationMode::Random, rng) == one);
            REQUIRE(dealStartPositions({}, StartLocationMode::Random, rng).empty());
        }
    }

    TEST_CASE("what losing the commander costs", "[gameoptions]")
    {
        auto script = makeOptionsScript();

        SECTION("Game Ends: the commander takes the player with it")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            auto us = addOptionsPlayer(sim, "us");
            auto them = addOptionsPlayer(sim, "them");
            defineOptionsUnit(sim, "commander", 100u, /*commander*/ true);
            defineOptionsUnit(sim, "tank", 100u);

            auto ourCommanderId = addOptionsUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
            auto ourTankId = addOptionsUnit(sim, "tank", us, SimVector(64_ss, 0_ss, 0_ss), script);
            addOptionsUnit(sim, "commander", them, SimVector(-200_ss, 0_ss, 0_ss), script);

            sim.killUnit(ourCommanderId);
            sim.tick();

            REQUIRE(sim.getPlayer(us).status == GamePlayerStatus::Dead);
            // The whole army goes with the commander.
            REQUIRE_FALSE(sim.tryGetUnitState(ourTankId).has_value());
            REQUIRE(std::holds_alternative<WinStatusWon>(sim.computeWinStatus()));
        }

        SECTION("Game Continues: the commander is just another unit")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.commanderDeathMode = CommanderDeathMode::GameContinues;
            auto us = addOptionsPlayer(sim, "us");
            auto them = addOptionsPlayer(sim, "them");
            defineOptionsUnit(sim, "commander", 100u, /*commander*/ true);
            defineOptionsUnit(sim, "tank", 100u);

            auto ourCommanderId = addOptionsUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
            auto ourTankId = addOptionsUnit(sim, "tank", us, SimVector(64_ss, 0_ss, 0_ss), script);
            addOptionsUnit(sim, "commander", them, SimVector(-200_ss, 0_ss, 0_ss), script);

            sim.killUnit(ourCommanderId);
            sim.tick();

            // It died, and it was counted as a loss -- the ordinary death path
            // ran, only the defeat did not follow.
            REQUIRE_FALSE(sim.tryGetUnitState(ourCommanderId).has_value());
            REQUIRE(sim.getPlayer(us).unitsLost == 1u);
            REQUIRE(sim.getPlayer(us).status == GamePlayerStatus::Alive);
            REQUIRE(sim.getUnitState(ourTankId).isAlive());
            REQUIRE(std::holds_alternative<WinStatusUndecided>(sim.computeWinStatus()));

            // The last unit standing is what actually ends it.
            sim.killUnit(ourTankId);
            sim.tick();

            REQUIRE(sim.getPlayer(us).status == GamePlayerStatus::Dead);
            auto winStatus = sim.computeWinStatus();
            REQUIRE(std::holds_alternative<WinStatusWon>(winStatus));
            REQUIRE(std::get<WinStatusWon>(winStatus).winner == them);
        }

        SECTION("Game Continues: a building on its own keeps a player in")
        {
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.commanderDeathMode = CommanderDeathMode::GameContinues;
            auto us = addOptionsPlayer(sim, "us");
            addOptionsPlayer(sim, "them");
            defineOptionsUnit(sim, "commander", 100u, /*commander*/ true);
            defineOptionsUnit(sim, "factory", 100u);
            sim.unitDefinitions["factory"].isMobile = false;

            auto ourCommanderId = addOptionsUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
            addOptionsUnit(sim, "factory", us, SimVector(64_ss, 0_ss, 0_ss), script);

            sim.killUnit(ourCommanderId);
            for (int i = 0; i < 5; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getPlayer(us).status == GamePlayerStatus::Alive);
        }

        SECTION("Game Continues: a player who has never had a unit is not defeated")
        {
            // processVictoryCondition runs every tick, including the ones
            // before anything has been spawned. A plain "owns nothing" test
            // would wipe the board out on tick one.
            GameSimulation sim(makeOptionsTerrain(), 0u, 0, 0);
            sim.commanderDeathMode = CommanderDeathMode::GameContinues;
            auto us = addOptionsPlayer(sim, "us");

            for (int i = 0; i < 5; ++i)
            {
                sim.tick();
            }

            REQUIRE(sim.getPlayer(us).status == GamePlayerStatus::Alive);
        }
    }
}
