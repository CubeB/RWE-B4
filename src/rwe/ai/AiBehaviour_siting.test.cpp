#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

/**
 * Where a building goes: the metal it is put on, the ground it is kept off,
  * and how the site search widens when nothing fits.
 *
 * Split out of AiBehaviour.test.cpp, which was 6,906 lines over 58 commits.
 * The fixtures these share are in ai_test_util.h.
 */
namespace rwe
{
    namespace
    {
        /**
         * A blank blackboard for the site-search cases below, which call
         * chooseBuildSite directly rather than through a controller. It
         * only has to know of no enemies: the search asks it nothing else.
         */
        const AiBlackboard& siteTestBlackboard()
        {
            static const AiBlackboard bb{};
            return bb;
        }
    }

    TEST_CASE("a building one builder has been sent for is not planned again for another", "[ai]")
    {
        // The planner counts what stands and what is going up, and a frame
        // only appears when the builder reaches the site. With two builders
        // that left a gap: the commander was sent 1200 units to put up a
        // radar, the construction kbot fell idle a second later, and the
        // counts said there was no radar, so it built one too. Both stood
        // to the end of the game, 128 units apart.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // A base past its opening, so the plan has reached the radar.
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        SECTION("with nobody sent for a radar, the idle kbot is")
        {
            sim.getUnitState(commanderId).orders.push_back(MoveOrder(SimVector(-400_ss, 0_ss, 0_ss)));
            runTicks(sim, controller, 31, commands);
            auto types = buildOrderTypes(commands);
            REQUIRE(types == std::vector<std::string>{"ARMRAD"});
        }

        SECTION("with the commander on its way to build one, the kbot is given something else")
        {
            // Far enough that no frame goes down inside the planning pass.
            sim.getUnitState(commanderId).orders.push_back(BuildOrder("ARMRAD", SimVector(-400_ss, 0_ss, 0_ss)));
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.idleBuilders == std::vector<UnitId>{kbotId});
            REQUIRE(bb.ownedTotalCounts.at("ARMRAD") == 1);
            REQUIRE(bb.ownedCompletedCounts.count("ARMRAD") == 0);
            auto types = buildOrderTypes(commands);
            REQUIRE(types.size() == 1);
            REQUIRE(types.front() != "ARMRAD");
        }
    }

    TEST_CASE("the AI expands to the metal it is shown, and stays on its own side of the map", "[ai]")
    {
        // The metal spots are on a player's map from the start, and the
        // expansion search used to wait for the AI to have explored them,
        // which with no scout until the eighth extractor meant a plateau
        // from the seventh minute to the sixteenth. Two patches, both well
        // outside the commander's sight of 300: one at (328, 328) and one
        // at (-440, -440) in world units (cells 52,52 and 4,4 of the
        // 64-wide map, which is centred on the middle).
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        for (auto cell : {52, 4})
        {
            for (int y = cell; y < cell + 2; ++y)
            {
                for (int x = cell; x < cell + 2; ++x)
                {
                    sim.metalGrid.set(x, y, static_cast<unsigned char>(200));
                }
            }
        }
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        // The near search is what fills in around the base; shrink it so
        // that only the expansion search can reach either patch.
        auto profile = makeDefaultStandardProfile();
        profile.nearMexSearchRadius = 100_ss;

        SECTION("an unexplored patch is claimed, nearest first")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto orders = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(orders.size() == 1);
            REQUIRE(orders.front().unitType == "ARMMEX");
            REQUIRE(orders.front().position.x > 300_ss);
            REQUIRE(orders.front().position.z > 300_ss);
        }

        SECTION("with the exploration gate on, it waits to have seen the ground")
        {
            profile.expansionNeedsExploredGround = true;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countOrdersFor(commands, "ARMMEX") == 0);

            // Mapped: the ground is handed over, and the patch with it. The
            // one shared explored grid is marked with the AI's group bit, which
            // is what isExploredBy reads.
            sim.playerVisibility.at(ai.value).exploreAll(ExploredMark{&sim.explored, sim.losGroupBitFor(ai)});
            sim.getUnitState(commanderId).orders.clear();
            commands.clear();
            runTicks(sim, controller, 31, commands);
            REQUIRE(countOrdersFor(commands, "ARMMEX") == 1);
        }

        SECTION("a patch nearer the enemy's base than ours is left to them")
        {
            // An enemy building beside the nearer patch, and an omniscient
            // AI so that it counts as found.
            addUnit(sim, "ARMSOLAR", human, SimVector(400_ss, 0_ss, 400_ss), script);
            profile.cheatModeOmniscient = true;

            SECTION("so the further patch, on our side, is taken instead")
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                REQUIRE(controller.getBlackboard().enemyBasePosition.has_value());
                auto orders = ordersFor<BuildOrder>(commands, commanderId);
                REQUIRE(orders.size() == 1);
                REQUIRE(orders.front().unitType == "ARMMEX");
                REQUIRE(orders.front().position.x < -300_ss);
            }

            SECTION("unless the knob says the whole map is fair game")
            {
                profile.expansionStaysOnOurSide = false;
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto orders = ordersFor<BuildOrder>(commands, commanderId);
                REQUIRE(orders.size() == 1);
                REQUIRE(orders.front().unitType == "ARMMEX");
                REQUIRE(orders.front().position.x > 300_ss);
            }
        }

        SECTION("a patch under a known enemy's guns is left alone")
        {
            // An enemy tower beside the nearer patch, the side rule off so
            // that only the guns can rule the patch out.
            addUnit(sim, "ARMLLT", human, SimVector(400_ss, 0_ss, 400_ss), script);
            profile.cheatModeOmniscient = true;
            profile.expansionStaysOnOurSide = false;

            SECTION("the builder takes the further patch")
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto orders = ordersFor<BuildOrder>(commands, commanderId);
                REQUIRE(orders.size() == 1);
                REQUIRE(orders.front().unitType == "ARMMEX");
                REQUIRE(orders.front().position.x < -300_ss);
            }

            SECTION("with the rule off it walks into them")
            {
                // Both rules, because there are two now and they ask the
                // same question of different things.
                // mexAvoidsEnemyGunsRadius steers the extractor SEARCH to
                // another patch; noticeProductionHarassment declines any
                // site at all that has a gun on it, at the point the order
                // would be issued, which is the backstop for everything
                // that is not an extractor. Switching off only the first no
                // longer walks into the guns, because the second still
                // refuses the order.
                profile.mexAvoidsEnemyGunsRadius = 0_ss;
                profile.noticeProductionHarassment = false;
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto orders = ordersFor<BuildOrder>(commands, commanderId);
                REQUIRE(orders.size() == 1);
                REQUIRE(orders.front().position.x > 300_ss);
            }
        }

        SECTION("a site whose order was dropped is left alone for a while")
        {
            profile.failedSiteMemorySeconds = 2;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto first = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(first.size() == 1);
            REQUIRE(first.front().unitType == "ARMMEX");

            // The order is dropped -- unreachable, say -- and the commander
            // is idle again with nothing standing at the site. The next
            // pass must not send it straight back.
            sim.getUnitState(commanderId).orders.clear();
            commands.clear();
            runTicks(sim, controller, 31, commands);
            auto second = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(second.size() == 1);
            bool sameSite = second.front().position.distanceSquared(first.front().position) < (1_ss * 1_ss);
            REQUIRE_FALSE(sameSite);

            // The memory expires, and the patch is fair game again.
            sim.getUnitState(commanderId).orders.clear();
            commands.clear();
            runTicks(sim, controller, 91, commands);
            REQUIRE(countOrdersFor(commands, "ARMMEX") >= 1);
        }

        SECTION("the commander stays within its leash of home, whichever search finds the patch")
        {
            // Both patches lie 460 and 620 from home; the near search would
            // reach them from where it stands, and must not.
            profile.nearMexSearchRadius = 1200_ss;
            profile.commanderMexSearchRadius = 400_ss;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countOrdersFor(commands, "ARMMEX") == 0);
        }
    }

    TEST_CASE("solar collectors go up in rows behind the base", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto profile = makeDefaultStandardProfile();
        AiBlackboard bb{};
        bb.sideUnits = resolveAiSideUnits(sim, "ARM");
        bb.sideUnitsResolved = true;
        bb.baseAnchor = SimVector(0_ss, 0_ss, 0_ss);
        // The enemy to the east, so behind is west.
        bb.enemyBasePosition = SimVector(400_ss, 0_ss, 0_ss);

        const auto& solarDef = sim.unitDefinitions.at("ARMSOLAR");
        auto footprint = sim.getFootprintXZ(solarDef.movementCollisionInfo);
        const auto spacing = SimScalar(static_cast<float>(std::max(footprint.first, footprint.second) + 2) * 16.0f);

        BuildManager planner;
        std::minstd_rand rng(42u);

        SECTION("the first behind the base, on the nearest ring")
        {
            auto site = planner.chooseEnergyRowSite(sim, ai, profile, bb, "ARMSOLAR", *bb.baseAnchor, *bb.baseAnchor, rng, nullptr);
            REQUIRE(site.has_value());
            REQUIRE(site->x < 0_ss);
            REQUIRE(rweAbs(site->x) <= spacing + 1_ss);
            REQUIRE(rweAbs(site->z) <= spacing + 1_ss);
        }

        SECTION("the next beside it in a row, the side nearest the builder")
        {
            const SimVector first(-spacing, 0_ss, 0_ss);
            addUnit(sim, "ARMSOLAR", ai, first, script);
            const SimVector builder(-spacing, 0_ss, 200_ss);
            auto site = planner.chooseEnergyRowSite(sim, ai, profile, bb, "ARMSOLAR", *bb.baseAnchor, builder, rng, nullptr);
            REQUIRE(site.has_value());
            REQUIRE(rweAbs(site->x - first.x) <= 1_ss);
            REQUIRE(rweAbs(site->z - (first.z + spacing)) <= 1_ss);
        }
    }

    TEST_CASE("an extractor goes to the heart of a deposit, not its near edge", "[ai]")
    {
        // A metal patch is one CELL, not one deposit, so the nearest ring of
        // the site search to find anything is a deposit's near edge -- and the
        // search used to stop there, putting a 3x3 extractor half off every
        // deposit bigger than about two cells square. Extraction is the metal
        // under the footprint, so that was income lost, not a cosmetic
        // offset. Reported from a replay on Brain Coral.
        GameSimulation sim(makeFlatTerrain(128, 128), /*surfaceMetal*/ 0u, 0, 0);
        addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        UnitDefinition mexDef;
        mexDef.isMobile = false;
        mexDef.builder = false;
        // ARMMEX and ARMUWMEX are both 3x3.
        mexDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["MEX"] = mexDef;

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto anchorHm = sim.terrain.worldToHeightmapCoordinate(anchor);

        auto paint = [&](int x0, int z0, int width, int height) {
            for (int z = z0; z < z0 + height; ++z)
            {
                for (int x = x0; x < x0 + width; ++x)
                {
                    sim.metalGrid.set(x, z, static_cast<unsigned char>(200));
                }
            }
        };
        auto metalUnder = [&](const SimVector& site) {
            auto rect = sim.computeFootprintRegion(site, mexDef.movementCollisionInfo);
            unsigned int total = 0;
            for (int z = rect.y; z < rect.y + static_cast<int>(rect.height); ++z)
            {
                for (int x = rect.x; x < rect.x + static_cast<int>(rect.width); ++x)
                {
                    total += sim.metalGrid.get(x, z);
                }
            }
            return total;
        };

        SECTION("a deposit found from the side is taken at its heart")
        {
            // 5x5, its near edge six cells east of the anchor. On that edge a
            // 3x3 covers six of the deposit's cells; one ring further in it
            // covers nine.
            paint(anchorHm.x + 6, anchorHm.y - 2, 5, 5);
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {});
            REQUIRE(site.has_value());
            REQUIRE(metalUnder(*site) == 9u * 200u);
        }

        SECTION("a footprint wider than the deposit is centred on it, not hung off its edge")
        {
            // A moho is 5x5 where the deposit under it is 3x3, so every
            // placement centred on any of the deposit's nine cells covers
            // all nine and they all tie on metal. The ring order then took
            // the one nearest the anchor -- the deposit's near edge -- and
            // the building came out with the rock in a corner of it.
            // Reported from a replay on Crystal Maze: a moho "completely off
            // of a metal spot, it wasn't at all central".
            UnitDefinition mohoDef;
            mohoDef.isMobile = false;
            mohoDef.builder = false;
            mohoDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["MOHO"] = mohoDef;

            paint(anchorHm.x + 6, anchorHm.y - 1, 3, 3);
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            auto site = buildManager.chooseMexSite(sim, "MOHO", anchor, 1600_ss, rng, {});
            REQUIRE(site.has_value());

            // The deposit runs x+6..x+8 by y-1..y+1, so its heart is
            // (x+7, y) and a 5x5 centred there starts at (x+5, y-2).
            auto rect = sim.computeFootprintRegion(*site, mohoDef.movementCollisionInfo);
            CHECK(rect.x == anchorHm.x + 5);
            CHECK(rect.y == anchorHm.y - 2);

            // Every one of those placements already collected the whole
            // deposit, which is why metal alone could not tell them apart.
            CHECK(metalUnder(*site) == 9u * 200u);
        }

        SECTION("and a footprint the size of the deposit is placed exactly as before")
        {
            // The guard on the case above: where the metal test can already
            // tell the placements apart, nothing has changed.
            paint(anchorHm.x + 6, anchorHm.y - 1, 3, 3);
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {});
            REQUIRE(site.has_value());
            auto rect = sim.computeFootprintRegion(*site, mexDef.movementCollisionInfo);
            CHECK(rect.x == anchorHm.x + 6);
            CHECK(rect.y == anchorHm.y - 1);
            CHECK(metalUnder(*site) == 9u * 200u);
        }

        SECTION("but it does not walk past a spot to reach a richer deposit further off")
        {
            // A single cell three out, bare ground, then a 5x5 twelve out. The
            // walk goes INTO the deposit it has found; it is not a hunt for a
            // better one elsewhere, and bare ground is where it stops. Without
            // the gap test the lone cell's ring counts as improving and the
            // walk carries on out to the big deposit.
            paint(anchorHm.x + 3, anchorHm.y, 1, 1);
            paint(anchorHm.x + 12, anchorHm.y - 2, 5, 5);
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {});
            REQUIRE(site.has_value());
            REQUIRE(metalUnder(*site) == 200u);
        }

        SECTION("a rule whose edge runs through a deposit still gets its heart")
        {
            // The commander's leash cut a Great Divide rock so that only its
            // near column was inside, and the extractor went on that column,
            // over four of the rock's nine cells. The ground rule is asked of
            // the deposit now: one cell inside is enough.
            paint(anchorHm.x + 6, anchorHm.y - 1, 3, 3);
            const auto nearColumnEdge = sim.terrain.heightmapIndexToWorldCorner(anchorHm.x + 7, anchorHm.y).x;
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {}, [&](const SimVector& p) { return p.x < nearColumnEdge; });
            REQUIRE(site.has_value());
            REQUIRE(metalUnder(*site) == 9u * 200u);
        }

        SECTION("a deposit whose heart is refused waits, rather than taking the cell beside it")
        {
            // A dropped order is remembered as a site, and the next search
            // used to take the site next to it on the same rock: 101 of the
            // 173 extractors Great Divide put off-centre. With a second
            // deposit further off, that one is taken instead.
            paint(anchorHm.x + 6, anchorHm.y - 1, 3, 3);
            const auto heart = sim.terrain.heightmapIndexToWorldCenter(anchorHm.x + 7, anchorHm.y);
            auto notTheHeart = [&](const SimVector& p) { return p.distanceSquared(heart) > 4_ss; };
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            REQUIRE_FALSE(buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, notTheHeart).has_value());

            // A fresh planner, since the patches are indexed once a game.
            paint(anchorHm.x - 12, anchorHm.y - 1, 3, 3);
            BuildManager later;
            auto site = later.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, notTheHeart);
            REQUIRE(site.has_value());
            REQUIRE(site->x < anchor.x);
            REQUIRE(metalUnder(*site) == 9u * 200u);
        }

        SECTION("a unit standing on its heart: it waits, and after a minute takes what is free")
        {
            // A 2x2 kbot over the deposit's corner cell and the ground
            // beyond it: the centred extractor is refused, the placement one
            // row up is not, and it covers six of the nine cells.
            paint(anchorHm.x + 6, anchorHm.y - 1, 3, 3);
            auto kbot = makeDef(false, false, true, "", 100u);
            kbot.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["KBOT"] = kbot;
            auto script = makeEmptyCobScript();
            auto kbotId = addUnit(sim, "KBOT", PlayerId(0), sim.terrain.heightmapIndexToWorldCorner(anchorHm.x + 9, anchorHm.y + 2), script);
            // The fixture's addUnit leaves the occupancy grid alone, so the
            // kbot's cells are stamped by hand, as the simulation's own
            // tryAddUnit stamps them.
            auto kbotRect = sim.computeFootprintRegion(sim.getUnitState(kbotId).position, sim.unitDefinitions.at("KBOT").movementCollisionInfo);
            sim.occupiedGrid.forEach(sim.occupiedGrid.clipRegion(kbotRect), [&](auto& cell) { cell.mobileUnitId = kbotId; });

            BuildManager buildManager;
            std::minstd_rand rng(1u);
            REQUIRE_FALSE(buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {}).has_value());

            sim.gameTime = GameTime(sim.gameTime.value + (60u * 30u) - 1u);
            REQUIRE_FALSE(buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {}).has_value());

            sim.gameTime = GameTime(sim.gameTime.value + 1u);
            auto site = buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {});
            REQUIRE(site.has_value());
            REQUIRE(metalUnder(*site) == 6u * 200u);
        }

        SECTION("nothing goes where the camera cannot see")
        {
            // The bottom eight rows of height tiles are past the camera's
            // cut-off. A deposit there is out of a player's reach, and so out
            // of the computer's.
            const auto heights = sim.terrain.getHeightMap().getHeight();
            paint(anchorHm.x - 1, heights - 6, 3, 3);
            BuildManager buildManager;
            std::minstd_rand rng(1u);
            REQUIRE_FALSE(buildManager.chooseMexSite(sim, "MEX", anchor, 1600_ss, rng, {}).has_value());
        }
    }

    TEST_CASE("with no free patch left, an extractor is reclaimed for a moho -- one, and only with the metal in hand", "[ai]")
    {
        // The original refuses a building placed over a standing unit
        // (TOTALA-EXE.md, 0x47D547), so an upgrade is a reclaim and then a
        // build, and the patch earns nothing in between.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto profile = makeDefaultStandardProfile();

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));
        addUnit(sim, "ARMLAB", ai, SimVector(400_ss, 0_ss, -400_ss), script);
        addUnit(sim, "ARMALAB", ai, SimVector(-400_ss, 0_ss, -400_ss), script);
        auto advancedId = addUnit(sim, "ARMACK", ai, SimVector(-300_ss, 0_ss, 100_ss), script);

        // Well apart, so a 5x5 has room where each 3x3 stands. No metal is
        // painted anywhere: there is no free patch, which is the point.
        sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
        std::vector<UnitId> extractors;
        for (int i = 0; i < profile.targetMetalExtractorCount; ++i)
        {
            extractors.push_back(addUnit(sim, "ARMMEX", ai, SimVector(SimScalar(-200.0f + i * 160.0f), 0_ss, 200_ss), script));
        }
        sim.unitDefinitions["ARMMOHO"].buildCostMetal = Metal(1508.0f);
        sim.getPlayer(ai).maxMetal = Metal(2000.0f);

        auto reclaimedUnits = [&](const std::vector<PlayerCommand>& commands) {
            std::set<unsigned int> targets;
            for (const auto& order : ordersFor<ReclaimOrder>(commands, advancedId))
            {
                if (auto unit = std::get_if<UnitId>(&order.target))
                {
                    targets.insert(unit->value);
                }
            }
            return targets;
        };

        SECTION("the nearest one, and no other however many passes go by")
        {
            sim.getPlayer(ai).metal = Metal(1200.0f);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);

            auto targets = reclaimedUnits(commands);
            REQUIRE(targets.size() == 1);
            // The builder stands at x -300; the first extractor is at -200.
            REQUIRE(*targets.begin() == extractors.front().value);
            REQUIRE(ordersFor<BuildOrder>(commands, advancedId).empty());
        }

        SECTION("then the moho, where the extractor stood, and the loss is not read as a raid")
        {
            sim.getPlayer(ai).metal = Metal(1200.0f);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            REQUIRE(reclaimedUnits(commands).size() == 1);

            auto where = sim.getUnitState(extractors.front()).position;
            sim.quietlyKillUnit(extractors.front());
            commands.clear();
            runTicks(sim, controller, 91, commands);

            auto builds = ordersFor<BuildOrder>(commands, advancedId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMMOHO");
            REQUIRE(builds.front().position.x.value == where.x.value);
            REQUIRE(builds.front().position.z.value == where.z.value);
            REQUIRE(controller.getBlackboard().recentLosses.empty());
        }

        SECTION("not on an empty store: the patch would stand idle while the moho was saved for")
        {
            sim.getPlayer(ai).metal = Metal(300.0f);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);
            REQUIRE(reclaimedUnits(commands).empty());
        }

        SECTION("and not at all with the knob off")
        {
            sim.getPlayer(ai).metal = Metal(1200.0f);
            profile.extractorUpgrades = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);
            REQUIRE(reclaimedUnits(commands).empty());
        }
    }

    TEST_CASE("a geothermal plant goes on a vent, and is not asked for where there is none", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.lineOfSightMode = LineOfSightMode::Circular;
        sim.unitDefinitions["ARMGEO"] = makeDef(false, false, false, "", 50u);

        auto profile = makeDefaultStandardProfile();
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(-100_ss, 0_ss, -100_ss), script);

        // The opening already built, so the plan has got as far as the vent.
        for (int i = 0; i < profile.targetSolarCount; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-600.0f + i * 100.0f), 0_ss, 500_ss), script);
        }
        for (int i = 0; i < profile.targetMetalExtractorCount; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(SimScalar(-600.0f + i * 100.0f), 0_ss, 650_ss), script);
        }
        // And everything the plan puts ahead of it.
        addUnit(sim, "ARMRAD", ai, SimVector(-600_ss, 0_ss, -500_ss), script);
        addUnit(sim, "ARMRL", ai, SimVector(-500_ss, 0_ss, -500_ss), script);
        for (int i = 0; i < profile.targetDefenceCount; ++i)
        {
            addUnit(sim, "ARMLLT", ai, SimVector(SimScalar(-400.0f + i * 100.0f), 0_ss, -500_ss), script);
        }
        sim.getPlayer(ai).metal = Metal(1000.0f);
        sim.getPlayer(ai).maxMetal = Metal(2000.0f);

        auto tree = makeBuildTree();
        tree.buildableBy["ARMCK"].insert("ARMGEO");

        SECTION("on the vent")
        {
            FeatureDefinition vent{};
            vent.name = "steamvent";
            vent.footprintX = 2;
            vent.footprintZ = 2;
            vent.geothermal = true;
            auto ventDef = sim.featureDefinitions.insert(vent);
            auto ventId = sim.addFeature(ventDef, 26, 28).value();
            auto where = sim.getFeature(ventId).position;

            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);

            auto builds = ordersFor<BuildOrder>(commands, kbotId);
            auto geo = std::find_if(builds.begin(), builds.end(), [](const BuildOrder& o) { return o.unitType == "ARMGEO"; });
            REQUIRE(geo != builds.end());
            REQUIRE(geo->position.x.value == where.x.value);
            REQUIRE(geo->position.z.value == where.z.value);
        }

        SECTION("and nothing else does")
        {
            // A vent blocks nothing, so the placement test would let a solar
            // collector stand on it. Vents all round the builder, and no
            // button for the plant: whatever is built, none of it may
            // overlap one.
            FeatureDefinition vent{};
            vent.name = "steamvent";
            vent.footprintX = 2;
            vent.footprintZ = 2;
            vent.geothermal = true;
            // As every shipped vent is, and it is what puts one in the sim's vent grid.
            vent.indestructible = true;
            auto ventDef = sim.featureDefinitions.insert(vent);
            std::vector<SimVector> vents;
            for (int z = 14; z <= 34; z += 5)
            {
                for (int x = 10; x <= 34; x += 5)
                {
                    if (auto id = sim.addFeature(ventDef, x, z))
                    {
                        vents.push_back(sim.getFeature(*id).position);
                    }
                }
            }
            REQUIRE(vents.size() >= 6);
            auto hungry = profile;
            hungry.targetSolarCount += 4;
            AiPlayerController controller(ai, hungry, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            auto builds = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(!builds.empty());
            for (const auto& order : builds)
            {
                for (const auto& v : vents)
                {
                    auto dx = std::abs(order.position.x.value - v.x.value);
                    auto dz = std::abs(order.position.z.value - v.z.value);
                    INFO(order.unitType << " at " << order.position.x.value << "," << order.position.z.value << " vent " << v.x.value << "," << v.z.value);
                    REQUIRE((dx >= 32.0f || dz >= 32.0f));
                }
            }
        }

        SECTION("nothing, with no vent on the map")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            auto types = buildOrderTypes(commands);
            REQUIRE(std::find(types.begin(), types.end(), "ARMGEO") == types.end());
        }
    }

    TEST_CASE("the site search walks outward past a ring the builder cannot reach", "[ai]")
    {
        // The ring walk stops at the NEAREST ring with room on it, which is
        // right for a solar collector and is exactly what makes filtering
        // its result the wrong way to refuse a site: the ring empties and
        // the search gives up, so a base whose first ring happened to sit
        // across water would refuse to build at all rather than build
        // further out on its own ground.
        //
        // So the predicate is asked inside the walk. This test discriminates
        // between the two: everything within the first two rings is refused,
        // and a search that filtered afterwards would return nothing here.
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        UnitDefinition solarDef;
        solarDef.isMobile = false;
        solarDef.builder = false;
        solarDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["SOLAR"] = solarDef;

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto profile = makeDefaultStandardProfile();

        // Spacing is (max(footprint) + 2) tiles, so 7 tiles or 112 world
        // units for a 5x5: rings 1 and 2 reach x = -112 and -224, and only
        // ring 3 reaches past -300.
        auto farSideOnly = [](const SimVector& p) { return p.x < -300_ss; };

        BuildManager buildManager;
        std::minstd_rand rng(1u);
        auto site = buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rng, farSideOnly);

        REQUIRE(site.has_value());
        REQUIRE(site->x < -300_ss);
    }

    TEST_CASE("a building is not planted across a metal patch, anywhere under it", "[ai]")
    {
        // The rule existed; it only ever looked at ONE cell. collectBuildableSites
        // tested metalGrid.get(rect.x, rect.y) -- the footprint's top-left corner
        // -- so a building whose corner was clear sat straight down across a
        // patch. ARMSOLAR is 5x5, which left 24 of its 25 cells unchecked, and a
        // 145-metal solar parked on a patch denies a 50-metal extractor that spot
        // for the rest of the game. Observed in play before it was found here.
        GameSimulation sim(makeFlatTerrain(128, 128), /*surfaceMetal*/ 0u, 0, 0);
        addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        UnitDefinition solarDef;
        solarDef.isMobile = false;
        solarDef.builder = false;
        // ARMSOLAR's own footprint, because the size is the whole point: the
        // bigger it is, the more of it the old check could not see.
        solarDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["SOLAR"] = solarDef;

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto profile = makeDefaultStandardProfile();

        // Where it goes with nothing in the way.
        BuildManager clean;
        std::minstd_rand rngA(1u);
        auto firstSite = clean.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rngA);
        REQUIRE(firstSite.has_value());
        auto firstRect = sim.computeFootprintRegion(*firstSite, solarDef.movementCollisionInfo);

        // A patch in the MIDDLE of that footprint -- deliberately not its
        // top-left cell, which is the only one the old rule consulted. With the
        // same seed and the same anchor, the old code returned this very site
        // again; the corner it checked is still clear.
        sim.metalGrid.set(firstRect.x + 2, firstRect.y + 2, static_cast<unsigned char>(200));

        BuildManager retry;
        std::minstd_rand rngB(1u);
        auto secondSite = retry.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rngB);
        REQUIRE(secondSite.has_value());
        auto secondRect = sim.computeFootprintRegion(*secondSite, solarDef.movementCollisionInfo);

        // Not one cell of the chosen footprint may hold a patch.
        for (int y = secondRect.y; y < secondRect.y + static_cast<int>(secondRect.height); ++y)
        {
            for (int x = secondRect.x; x < secondRect.x + static_cast<int>(secondRect.width); ++x)
            {
                INFO("cell " << x << "," << y);
                REQUIRE(sim.metalGrid.get(x, y) <= sim.surfaceMetal);
            }
        }
    }

    TEST_CASE("a building is not planted across a factory's exit", "[ai]")
    {
        // A unit a factory finishes has to walk off the pad before the next
        // one can be spawned there, so a building hard against the factory
        // stops it producing for the rest of the game.
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto script = makeEmptyCobScript();

        UnitDefinition factoryDef;
        factoryDef.isMobile = false;
        factoryDef.builder = true;
        factoryDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["FACTORY"] = factoryDef;

        UnitDefinition solarDef;
        solarDef.isMobile = false;
        solarDef.builder = false;
        solarDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["SOLAR"] = solarDef;

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto factoryId = addUnitOfType(sim, "FACTORY", ai, anchor, script);
        auto factoryRect = sim.computeFootprintRegion(sim.getUnitState(factoryId).position, factoryDef.movementCollisionInfo);

        auto profile = makeDefaultStandardProfile();
        std::minstd_rand rng(1u);
        BuildManager buildManager;

        auto site = buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rng);
        REQUIRE(site.has_value());

        auto rect = sim.computeFootprintRegion(*site, solarDef.movementCollisionInfo);
        // Three tiles of lane, the factory's own margin, on at least one axis.
        auto clearX = (rect.x >= factoryRect.x + factoryRect.width + 3) || (factoryRect.x >= rect.x + rect.width + 3);
        auto clearZ = (rect.y >= factoryRect.y + factoryRect.height + 3) || (factoryRect.y >= rect.y + rect.height + 3);
        REQUIRE((clearX || clearZ));
    }

    TEST_CASE("a base with nowhere left still does not build across the plant's door", "[ai]")
    {
        // The case above is the roomy one, where keeping the lane costs
        // nothing. This is the one that was actually happening: reported
        // from a replay, "one of the core vehicle factories got blocked when
        // trying to produce units ... there should be enough space for units
        // to filter between structures in the base".
        //
        // collectBuildableSites ends "sites.empty() ? crowded : sites", and
        // the crowded list is everything that fits but stands too close to
        // something already up. On a cramped map that fallback is not the
        // exception it was written as, it is the ordinary path -- so the
        // factory's lane, which every other rule in the file protects, was
        // given away on every building the AI put up there.
        //
        // The search is clamped to one ring here, which is the compact way
        // to say "a base with nowhere left": ring one is 80 world units out
        // for a 3x3, which is inside a 7x7 neighbour's margin whichever way
        // it lies.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        UnitDefinition solarDef;
        solarDef.isMobile = false;
        solarDef.builder = false;
        solarDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["SOLAR"] = solarDef;

        UnitDefinition neighbourDef;
        neighbourDef.isMobile = false;
        neighbourDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{7u, 7u, 255u, 255u, 0u, 255u};

        auto anchor = SimVector(0_ss, 0_ss, 0_ss);
        auto profile = makeDefaultStandardProfile();
        // One ring, and no widening: (3 + 2) * 16 is 80 world units a ring.
        profile.maxMexSearchRadius = 100_ss;
        profile.buildSiteFallbackRadius = 100_ss;

        std::minstd_rand rng(1u);
        BuildManager buildManager;

        SECTION("the neighbour is a factory: nothing goes up")
        {
            auto factoryDef = neighbourDef;
            factoryDef.builder = true;
            sim.unitDefinitions["NEIGHBOUR"] = factoryDef;
            addUnitOfType(sim, "NEIGHBOUR", ai, anchor, script);

            CHECK_FALSE(buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rng).has_value());
        }

        SECTION("the same neighbour, not a factory: the crowded site is taken")
        {
            // The control, and the whole reason the section above means
            // anything: the same geometry, the same one ring, the same
            // crowding. Only the neighbour's being able to build differs,
            // and a packed base is better off with the solar collector than
            // without it.
            auto storeDef = neighbourDef;
            storeDef.builder = false;
            sim.unitDefinitions["NEIGHBOUR"] = storeDef;
            addUnitOfType(sim, "NEIGHBOUR", ai, anchor, script);

            CHECK(buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "SOLAR", anchor, rng).has_value());
        }
    }

    TEST_CASE("the site search widens when nothing fits, and only then", "[ai]")
    {
        // The island start that was costing the AI its entire game. A 6x6
        // lab needs spacing (6+2)*16 = 128 world units a ring, so a 512
        // budget is four rings; on a small island every one of those rings
        // is in the sea and the planner -- which takes the first want it can
        // SITE, not the first it can afford -- silently skipped the lab. No
        // lab meant no factory, no army, and a side that never left Boom,
        // while the shipyard got built anyway because its siting scans
        // MapIntel globally instead of walking rings. It read as an AI that
        // liked boats.
        //
        // Raising maxMexSearchRadius outright fixed that and cost about 5%
        // on land, losing two games of twenty in one slot of a swapped
        // Painted Desert run, so the widening is conditional instead. Both
        // halves of that are pinned here: it must find the far site when the
        // near rings are empty, and the kill switch must genuinely restore
        // the old refusal.
        //
        // The terrain is land only in a 10x10 tile patch at the origin --
        // bounded on BOTH axes, unlike makeWaterMapTerrain's strip, which is
        // narrow in x but runs the full height of the map and so always has
        // room along z inside 512.
        // World coordinates are CENTRED on the map, so heightmap tile and
        // world position relate as tile = (world + 896) / 16 for this
        // 112-tile map (112 * 16 = 1792 world units, half of it 896).
        // Getting that backwards is what made an earlier version of this
        // fixture put the anchor inside the island it was supposed to be
        // unable to reach.
        Grid<unsigned char> heights(112, 112, static_cast<unsigned char>(0));
        // The anchor's island: tiles 50-59, i.e. world -96 to 64.
        for (int y = 50; y < 60; ++y)
        {
            for (int x = 50; x < 60; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(90));
            }
        }
        // The far island: tiles 2-13, i.e. world -864 to -672.
        //
        // A 6x6 building spaces its rings (6+2)*16 = 128 apart, so a 512
        // budget is four rings reaching 512 world units from the anchor at
        // world -16. Rings 1-4 put their whole footprint in open water; the
        // first candidate whose 6x6 lands entirely on this island is ring 6,
        // 768 out, about 1086 away -- past the budget and well inside the
        // 2048 fallback. Up and to the left of the anchor, because the same
        // island down and to the right sits past the camera's cut-off, where
        // no site is offered (footprintInsideVisibleMap).
        for (int y = 2; y < 14; ++y)
        {
            for (int x = 2; x < 14; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(90));
            }
        }
        MapTerrain terrain(std::move(heights), 60_ss);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        // A 6x6 building that cannot stand in water. The field order is
        // {footprintX, footprintZ, maxSlope, maxWaterSlope, minWaterDepth,
        // maxWaterDepth}, so dry-land-only is a maxWaterDepth of 0 with the
        // slopes left open -- the same shape ReachabilityMap.test.cpp uses
        // for its land mover.
        UnitDefinition labDef;
        labDef.isMobile = false;
        labDef.builder = false;
        labDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["LAB"] = labDef;

        // World -16 is tile 55, the middle of the anchor's island.
        auto anchor = SimVector(-16_ss, 90_ss, -16_ss);
        std::minstd_rand rng(1u);
        BuildManager buildManager;

        SECTION("with the fallback off, the search gives up exactly as it used to")
        {
            auto profile = makeDefaultStandardProfile();
            // Equal, not smaller: the guard is a strict >, so this is the
            // documented way to switch the behaviour off.
            profile.buildSiteFallbackRadius = profile.maxMexSearchRadius;

            REQUIRE_FALSE(buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "LAB", anchor, rng).has_value());
        }

        SECTION("with it on, the far patch is found")
        {
            auto profile = makeDefaultStandardProfile();
            REQUIRE(profile.buildSiteFallbackRadius > profile.maxMexSearchRadius);

            auto site = buildManager.chooseBuildSite(sim, profile, siteTestBlackboard(), "LAB", anchor, rng);
            REQUIRE(site.has_value());

            // It really is the far patch, not somewhere the near rings could
            // have reached -- otherwise the section above would have found it
            // too and this would pin nothing.
            REQUIRE(flatDistanceBetween(*site, anchor) > profile.maxMexSearchRadius);

            // And it is dry: a site in the sea would mean the footprint test
            // is not being applied at the wider radius.
            REQUIRE(sim.terrain.getHeightAt(site->x, site->z) >= sim.terrain.getSeaLevel());
        }
    }

    TEST_CASE("nothing at all is built under an enemy gun, not just an extractor", "[ai]")
    {
        // What the all-water games actually lost, which is not what the
        // shipyard loses: two commanders replacing tidal generators under
        // an enemy scout ship, 286 and 232 units in one game. The extractor
        // search has refused a patch under guns since the Crystal Maze
        // measurement, but a frame has no hit points whatever it is going
        // to become, so the rule was never really about extractors.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        auto gunId = addUnit(sim, "ARMPT", human, SimVector(-330_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        // Off, so that only the general rule can rule a site out: an
        // extractor refused by the extractor search would prove nothing
        // about anything else.
        profile.mexAvoidsEnemyGunsRadius = 0_ss;
        // The gun is an intruder, and commanderDefendsAloneMaxIntruders is on
        // by default, so the commander would be sent at it instead of
        // building. What this test measures is where the building goes.
        profile.commanderDefendsAloneMaxIntruders = 0;
        // And the gun is inside commanderDangerRadius, so the commander would
        // run rather than build; this is the same switch the tests above use.
        profile.commanderDangerRadius = 0_ss;
        // builderAvoidsContestedRadius refuses any site near an armed enemy
        // too, which would leave the control below with nothing to find.
        profile.builderAvoidsContestedRadius = 0_ss;

        const auto& gunPosition = sim.getUnitState(gunId).position;
        auto radiusSquared = profile.productionHarassRadius * profile.productionHarassRadius;

        SECTION("every site the planner issues is out of its reach")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);
            auto orders = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!orders.empty());
            for (const auto& order : orders)
            {
                REQUIRE(gunPosition.distanceSquared(order.position) > radiusSquared);
            }
        }

        SECTION("and with the knob off, at least one is not")
        {
            profile.noticeProductionHarassment = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 200, commands);
            auto orders = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!orders.empty());
            auto underGuns = std::count_if(orders.begin(), orders.end(), [&](const BuildOrder& o) {
                return gunPosition.distanceSquared(o.position) <= radiusSquared;
            });
            REQUIRE(underGuns > 0);
        }
    }

    TEST_CASE("two builders do not plan the same metal patch", "[ai]")
    {
        // A build order is invisible on the map until the frame goes down,
        // so a second builder planned in the same pass sees free ground and
        // walks across the map to find it taken. Reported from a replay:
        // "construction bots frequently try and build on the same metal spot
        // they dont know what the other construction bot was already ordered
        // to do".
        ExpansionWorld w;
        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        BuildManager planner;
        const auto taken = ExpansionWorld::depositCentre(0);
        w.sim.getUnitState(w.constructorId).orders.push_back(BuildOrder("ARMMEX", taken));

        SECTION("the patch one of them is already walking to is spoken for")
        {
            REQUIRE(planner.siteClaimedByAnother(w.sim, w.ai, w.commanderId, taken, profile.claimedSiteRadius));
        }

        SECTION("another patch is not")
        {
            // Deposit 3, not 1: neighbouring deposits on this map are 96
            // apart, which is the claim radius itself.
            REQUIRE_FALSE(planner.siteClaimedByAnother(w.sim, w.ai, w.commanderId, ExpansionWorld::depositCentre(3), profile.claimedSiteRadius));
        }

        SECTION("a builder does not count its own order against itself")
        {
            REQUIRE_FALSE(planner.siteClaimedByAnother(w.sim, w.ai, w.constructorId, taken, profile.claimedSiteRadius));
        }

        SECTION("a frame somebody was sent back to finish counts too")
        {
            auto frameId = addUnit(w.sim, "ARMMEX", w.ai, ExpansionWorld::depositCentre(3), w.script);
            w.sim.getUnitState(frameId).buildTimeCompleted = 1u;
            w.sim.getUnitState(w.constructorId).orders.clear();
            w.sim.getUnitState(w.constructorId).orders.push_back(CompleteBuildOrder(frameId));
            REQUIRE(planner.siteClaimedByAnother(w.sim, w.ai, w.commanderId, ExpansionWorld::depositCentre(3), profile.claimedSiteRadius));
        }

        SECTION("switched off, nothing is spoken for")
        {
            REQUIRE_FALSE(planner.siteClaimedByAnother(w.sim, w.ai, w.commanderId, taken, 0_ss));
        }
    }

    TEST_CASE("deposits free on our side are counted: not ours, not theirs, not under a gun", "[ai]")
    {
        ExpansionWorld w;
        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;

        SECTION("all eight, with nothing on them")
        {
            AiPlayerController controller(w.ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(w.sim, controller, 2, commands);
            BuildManager planner;
            REQUIRE(planner.freeDepositsOnOurSide(w.sim, profile, controller.getBlackboard()) == 8);
        }

        SECTION("one of ours on a deposit, one of theirs on another, and a gun of theirs over a third")
        {
            // What of theirs is seen puts their base between the two, which
            // is not what this is about.
            profile.expansionStaysOnOurSide = false;
            addUnit(w.sim, "ARMMEX", w.ai, ExpansionWorld::depositCentre(0), w.script);
            addUnit(w.sim, "ARMMEX", w.human, ExpansionWorld::depositCentre(3), w.script);
            addUnit(w.sim, "ARMLLT", w.human, ExpansionWorld::depositCentre(7) + SimVector(60_ss, 0_ss, 0_ss), w.script);
            AiPlayerController controller(w.ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(w.sim, controller, 2, commands);
            BuildManager planner;
            REQUIRE(planner.freeDepositsOnOurSide(w.sim, profile, controller.getBlackboard()) == 5);
        }

        SECTION("one nearer their base than ours is theirs to take")
        {
            // Their base at (400, 400): the deposit at (304, 304) is nearer it.
            addUnit(w.sim, "ARMSOLAR", w.human, SimVector(400_ss, 0_ss, 400_ss), w.script);
            AiPlayerController controller(w.ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(w.sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().enemyBasePosition.has_value());
            BuildManager planner;
            REQUIRE(planner.freeDepositsOnOurSide(w.sim, profile, controller.getBlackboard()) == 7);
        }
    }
}
