#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

/**
 * Towers and teeth: where they stand, which way they face, when they get
  * fortified and what one has to be worth.
 *
 * Split out of AiBehaviour.test.cpp, which was 6,906 lines over 58 commits.
 * The fixtures these share are in ai_test_util.h.
 */
namespace rwe
{
    TEST_CASE("defences are spaced by their range, and the radar stands at the edge of the base", "[ai]")
    {
        // The nearest-free-ring rule put every tower on the ring around the
        // same post: three laser towers within 64 units of each other and
        // two Defenders side by side, each covering the ground the last one
        // did. And the radar went wherever the next free ring was, which is
        // the middle of the base, where it sees what the buildings already
        // see.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        const SimVector anchor(-300_ss, 0_ss, 0_ss);
        // The test world's one weapon reaches 200; the front post is 160
        // out from the anchor, towards the map centre.
        const SimScalar range = sim.weaponDefinitions.at("LASER").maxRange;
        auto profile = makeDefaultStandardProfile();
        const SimVector post = anchor + SimVector(profile.defenceDistanceFromBase, 0_ss, 0_ss);

        SECTION("the second laser tower goes a range from the first, along the front, not behind it")
        {
            layOutBase(sim, ai, script, anchor);
            addUnit(sim, "ARMLLT", ai, post, script);

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto order = theBuildOrder(commands, "ARMLLT");

            REQUIRE(flatDistanceBetween(order.position, post) >= range);
            // No further back than the post, and not out in front alone.
            REQUIRE(order.position.x >= post.x);
            REQUIRE(order.position.x < post.x + range);
            REQUIRE(flatDistanceBetween(order.position, anchor) > range);
        }

        SECTION("with the rule off, the second tower goes on the nearest free ring as before")
        {
            layOutBase(sim, ai, script, anchor);
            addUnit(sim, "ARMLLT", ai, post, script);
            profile.spreadDefences = false;

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto order = theBuildOrder(commands, "ARMLLT");
            REQUIRE(flatDistanceBetween(order.position, anchor) < range);
        }

        SECTION("the first anti-air tower goes to the middle of the base, where it covers all of it")
        {
            layOutBase(sim, ai, script, anchor);
            addUnit(sim, "ARMLLT", ai, post, script);
            addUnit(sim, "ARMLLT", ai, post + SimVector(0_ss, 0_ss, 200_ss), script);

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto order = theBuildOrder(commands, "ARMRL");
            REQUIRE(flatDistanceBetween(order.position, anchor) < 100_ss);
        }

        SECTION("the next anti-air tower goes where the first one's umbrella runs out")
        {
            layOutBase(sim, ai, script, anchor);
            addUnit(sim, "ARMLLT", ai, post, script);
            addUnit(sim, "ARMLLT", ai, post + SimVector(0_ss, 0_ss, 200_ss), script);
            addUnit(sim, "ARMRL", ai, anchor, script);
            // A cluster beyond the first tower's reach, and an enemy plane
            // in sight so that more anti-air is wanted at all.
            const SimVector cluster(50_ss, 0_ss, 0_ss);
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMSOLAR", ai, cluster + SimVector(0_ss, 0_ss, SimScalar(-40.0f + i * 40.0f)), script);
            }
            addUnit(sim, "ARMPEEP", human, anchor + SimVector(0_ss, 0_ss, 150_ss), script);

            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            auto order = theBuildOrder(commands, "ARMRL");

            REQUIRE(flatDistanceBetween(order.position, cluster) <= range);
            REQUIRE(flatDistanceBetween(order.position, anchor) >= range);
        }

        SECTION("the radar goes to the edge of the base facing the map centre")
        {
            layOutBase(sim, ai, script, anchor);
            // Take the base's radar away again; this section is about where it goes.
            for (auto& [unitId, unit] : sim.units)
            {
                if (unit.unitType == "ARMRAD")
                {
                    unit.markAsDeadNoCorpse();
                }
            }
            const SimVector radarPost = anchor + SimVector(profile.radarDistanceFromBase, 0_ss, 0_ss);

            SECTION("on")
            {
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto order = theBuildOrder(commands, "ARMRAD");
                REQUIRE(flatDistanceBetween(order.position, radarPost) < 100_ss);
            }

            SECTION("off, it goes on the nearest free ring, in the middle of everything")
            {
                profile.spreadDefences = false;
                AiPlayerController controller(ai, profile, 42u, MapIntel{});
                std::vector<PlayerCommand> commands;
                runTicks(sim, controller, 31, commands);
                auto order = theBuildOrder(commands, "ARMRAD");
                REQUIRE(flatDistanceBetween(order.position, anchor) < 200_ss);
            }
        }
    }

    TEST_CASE("a tower is not sited on ground the base cannot walk to", "[ai]")
    {
        // Scoring the whole base radius, rather than the nearest ring, let
        // the coverage rule pick a Defender site on a mesa the builder could
        // not climb. The order died on the spot, the same site won again
        // next pass, and a commander spent four minutes of one arena game
        // re-issuing it. Here the far bank plays the mesa.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeChannelTerrain(), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        // The base on the west bank with its towers, so the Defender is
        // next, and on the east bank a solar farm bigger than the base --
        // eleven buildings against sixteen -- so that coverage alone would
        // send the tower across the water.
        (void)human;
        const SimVector anchor(-300_ss, 60_ss, 0_ss);
        layOutBase(sim, ai, script, anchor);
        addUnit(sim, "ARMLLT", ai, anchor + SimVector(160_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLLT", ai, anchor + SimVector(160_ss, 0_ss, 200_ss), script);
        for (int column = 0; column < 2; ++column)
        {
            for (int row = 0; row < 8; ++row)
            {
                addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(250.0f + column * 40.0f), 60_ss, SimScalar(-140.0f + row * 40.0f)), script);
            }
        }

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);
        REQUIRE(controller.getBlackboard().groundReachabilityValid);
        auto order = theBuildOrder(commands, "ARMRL");

        // West of the channel, which runs down x -64..64.
        REQUIRE(order.position.x < -64_ss);
        REQUIRE(controller.getReachabilityMap().isReachable(sim, order.position));
    }

    TEST_CASE("an extractor cluster beyond the base's cover gets a tower of its own", "[ai]")
    {
        // The base is built out: opening quotas, lab, radar and its two own
        // towers, so that the first thing the plan has left to want is the
        // outpost tower. The map is only 1024 across, so the base's own
        // defence radius is brought in to 200 to leave room for an outpost.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMRAD", ai, SimVector(-100_ss, 0_ss, 100_ss), script);
        addUnit(sim, "ARMLLT", ai, SimVector(100_ss, 0_ss, -100_ss), script);
        addUnit(sim, "ARMLLT", ai, SimVector(-100_ss, 0_ss, -100_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.defendRadius = 200_ss;

        // Two extractors 566 from the base, 40 apart: one tower's reach
        // (the test laser's 200) covers both.
        const SimVector outpostA(-400_ss, 0_ss, -400_ss);
        const SimVector outpostB(-440_ss, 0_ss, -400_ss);

        SECTION("two undefended extractors together are a cluster, and the tower goes to them")
        {
            addUnit(sim, "ARMMEX", ai, outpostA, script);
            addUnit(sim, "ARMMEX", ai, outpostB, script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            BuildManager planner;
            auto plan = planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard());
            REQUIRE(plan.has_value());
            REQUIRE(plan->extractors == 2);
            REQUIRE_FALSE(plan->raided);
            REQUIRE(plan->anchor.x == -420_ss);
            REQUIRE(plan->anchor.z == -400_ss);

            auto orders = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(orders.size() == 1);
            REQUIRE(orders.front().unitType == "ARMLLT");
            REQUIRE(orders.front().position.distanceSquared(plan->anchor) < (200_ss * 200_ss));
        }

        SECTION("a cluster a tower already reaches wants nothing")
        {
            addUnit(sim, "ARMMEX", ai, outpostA, script);
            addUnit(sim, "ARMMEX", ai, outpostB, script);
            addUnit(sim, "ARMLLT", ai, SimVector(-420_ss, 0_ss, -380_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            BuildManager planner;
            REQUIRE_FALSE(planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard()).has_value());
        }

        SECTION("below the cluster minimum an extractor is not worth a tower, until it is raided")
        {
            profile.outpostDefenceMinExtractors = 2;
            addUnit(sim, "ARMMEX", ai, outpostA, script);
            auto raidedId = addUnit(sim, "ARMMEX", ai, outpostB, script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            BuildManager planner;

            // Both standing: a cluster of two. Kill one before the AI has
            // ever seen it, so its memory holds one lone extractor and no loss.
            sim.getUnitState(raidedId).markAsDead();
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().recentLosses.empty());
            REQUIRE_FALSE(planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard()).has_value());

            // Now the same loss, seen to happen.
            auto lostId = addUnit(sim, "ARMMEX", ai, outpostB, script);
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(lostId).markAsDead();
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().recentLosses.size() == 1);
            auto plan = planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard());
            REQUIRE(plan.has_value());
            REQUIRE(plan->raided);
            REQUIRE(plan->extractors == 1);
        }

        SECTION("a site the raid wiped out gets the tower at the loss, ready for the rebuild")
        {
            auto lostA = addUnit(sim, "ARMMEX", ai, outpostA, script);
            auto lostB = addUnit(sim, "ARMMEX", ai, outpostB, script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(lostA).markAsDead();
            sim.getUnitState(lostB).markAsDead();
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().recentLosses.size() == 2);

            BuildManager planner;
            auto plan = planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard());
            REQUIRE(plan.has_value());
            REQUIRE(plan->raided);
            REQUIRE(plan->extractors == 0);
            REQUIRE(plan->anchor.distanceSquared(outpostA) <= (50_ss * 50_ss));
        }

        SECTION("the outpost towers are bounded by the knob")
        {
            addUnit(sim, "ARMMEX", ai, outpostA, script);
            addUnit(sim, "ARMMEX", ai, outpostB, script);
            // One outpost tower already standing, elsewhere.
            addUnit(sim, "ARMLLT", ai, SimVector(400_ss, 0_ss, 400_ss), script);
            profile.outpostDefenceCount = 1;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            BuildManager planner;
            REQUIRE_FALSE(planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard()).has_value());
            profile.outpostDefenceCount = 2;
            REQUIRE(planner.planOutpostDefence(sim, ai, profile, controller.getBlackboard()).has_value());
        }
    }

    TEST_CASE("a laser tower is fortified: teeth across the approach, a missile tower behind", "[ai]")
    {
        // A base built out as far as its own two towers, the commander
        // walking away so the construction kbot is the only builder the
        // planner has, and the enemy's lab in sight to the east so the
        // approach has a direction. The shipped geometry is what the
        // profile holds; the test world's numbers only have to leave room.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMDRAG"] = makeDef(false, false, false, "", 10u);

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(-400_ss, 0_ss, 400_ss)));
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(-60_ss, 0_ss, 60_ss), script);
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-200.0f + i * 40.0f), 0_ss, 150_ss), script);
        }
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(-200_ss, 0_ss, SimScalar(-40.0f + i * 40.0f)), script);
        }
        addUnit(sim, "ARMLAB", ai, SimVector(0_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMVP", ai, SimVector(-120_ss, 0_ss, 220_ss), script);
        addUnit(sim, "ARMRAD", ai, SimVector(-100_ss, 0_ss, 100_ss), script);
        const SimVector tower(100_ss, 0_ss, -100_ss);
        auto towerId = addUnit(sim, "ARMLLT", ai, tower, script);
        addUnit(sim, "ARMLLT", ai, SimVector(-100_ss, 0_ss, -100_ss), script);
        addUnit(sim, "ARMLAB", human, SimVector(450_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.fortifyTowers = true;
        auto tree = makeBuildTree();
        tree.buildableBy["ARMCK"].insert("ARMDRAG");

        // The way from the first tower to the enemy's base, flat.
        auto towardsEnemy = SimVector(450_ss - tower.x, 0_ss, 0_ss - tower.z).normalizedOr(SimVector(1_ss, 0_ss, 0_ss));

        SECTION("the first tooth goes against the first tower, on the face towards the enemy")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyBasePosition.has_value());

            BuildManager planner;
            std::minstd_rand rng(42u);
            auto plan = planner.planFortification(sim, ai, profile, controller.getBlackboard(), rng);
            REQUIRE(plan.has_value());
            REQUIRE(plan->unitType == "ARMDRAG");
            REQUIRE(plan->tower == towerId);
            // Wrapped round the tower: the middle of its face towards the
            // enemy, one tooth's width from its centre.
            REQUIRE(rweAbs(plan->site.x - (tower.x + 32_ss)) <= 1_ss);
            REQUIRE(rweAbs(plan->site.z - tower.z) <= 1_ss);

            // And the construction kbot, which has the button, is sent to it.
            auto orders = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(std::any_of(orders.begin(), orders.end(), [](const BuildOrder& o) { return o.unitType == "ARMDRAG"; }));
        }

        SECTION("unwrapped, the first tooth goes across the approach, out in front of the first tower")
        {
            profile.fortifyTeethWrap = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            BuildManager planner;
            std::minstd_rand rng(42u);
            auto plan = planner.planFortification(sim, ai, profile, controller.getBlackboard(), rng);
            REQUIRE(plan.has_value());
            REQUIRE(plan->unitType == "ARMDRAG");
            REQUIRE(plan->tower == towerId);
            // The middle of the line, profile.fortifyTeethDistance out along
            // the approach: inside a tower's reach, outside a raider's.
            auto out = (plan->site - tower).dot(towardsEnemy);
            REQUIRE(out > profile.fortifyTeethDistance - 20_ss);
            REQUIRE(out < profile.fortifyTeethDistance + 20_ss);
            REQUIRE(flatDistanceBetween(plan->site, tower) < profile.fortifyTeethDistance + 20_ss);
        }

        SECTION("a full line wants a missile tower behind it")
        {
            BuildManager planner;
            std::minstd_rand rng(42u);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);

            // Stand up every tooth the plan asks for, in turn, until it asks
            // for something else.
            std::optional<BuildManager::FortificationPlan> plan;
            for (int i = 0; i < profile.fortifyTeethPerTower + 1; ++i)
            {
                plan = planner.planFortification(sim, ai, profile, controller.getBlackboard(), rng);
                REQUIRE(plan.has_value());
                if (plan->unitType != "ARMDRAG")
                {
                    break;
                }
                addUnit(sim, "ARMDRAG", ai, plan->site, script);
            }
            REQUIRE(plan->unitType == "ARMRL");
            REQUIRE(plan->tower == towerId);
            REQUIRE((plan->site - tower).dot(towardsEnemy) <= 0_ss);
            REQUIRE(flatDistanceBetween(plan->site, tower) <= profile.fortifyMissileCoverRadius);
        }

        SECTION("a tower short of its teeth gets a constructor of its own")
        {
            // One construction kbot stands, which is the whole of
            // targetConstructorCount; the teeth it owes are what ask for
            // a second.
            REQUIRE(profile.targetConstructorCount == 1);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") >= 1);
        }

        SECTION("off, nothing is fortified")
        {
            profile.fortifyTowers = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") == 0);

            BuildManager planner;
            std::minstd_rand rng(42u);
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, controller.getBlackboard(), rng).has_value());
            auto orders = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(std::none_of(orders.begin(), orders.end(), [](const BuildOrder& o) { return o.unitType == "ARMDRAG"; }));
        }
    }

    TEST_CASE("a lost tower is put back, fortified away from the base, and reinforced if it falls again", "[ai]")
    {
        // Driven directly, as the teeth-where-attacked test is: the losses
        // are written into the blackboard by hand, the way EconomyManager
        // writes them, and the planner is asked what it would do.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMDRAG"] = makeDef(false, false, false, "", 10u);

        const SimVector site(0_ss, 0_ss, 0_ss);
        const SimVector east(1_ss, 0_ss, 0_ss);
        auto profile = makeDefaultStandardProfile();
        profile.fortifyTowers = false;
        profile.fortifyWhereAttacked = false;

        AiBlackboard bb{};
        bb.sideUnits = resolveAiSideUnits(sim, "ARM");
        bb.sideUnitsResolved = true;
        bb.baseAnchor = SimVector(-300_ss, 0_ss, 0_ss);
        REQUIRE(bb.sideUnits.antiAirTower == "ARMRL");

        BuildManager planner;
        std::minstd_rand rng(42u);
        const auto delay = static_cast<unsigned int>(profile.rebuildDelaySeconds) * 30u;

        bb.now = GameTime(300);
        bb.recentLosses.push_back(LostBuilding{"ARMLLT", site, GameTime(300)});
        planner.recordLostDefences(sim, profile, bb);
        REQUIRE(planner.getLostDefenceSites().size() == 1);
        REQUIRE(planner.getLostDefenceSites().front().timesLost == 1);

        SECTION("not at once, not under a gun, and then where it stood")
        {
            bb.now = GameTime(300 + delay - 1);
            REQUIRE_FALSE(planner.planDefenceRebuild(sim, ai, profile, bb).has_value());

            bb.now = GameTime(300 + delay);
            bb.knownEnemies[9999u] = KnownEnemy{UnitId(9999u), "ARMPW", site + SimVector(150_ss, 0_ss, 0_ss), bb.now, false, true, false};
            REQUIRE_FALSE(planner.planDefenceRebuild(sim, ai, profile, bb).has_value());

            bb.knownEnemies.clear();
            auto plan = planner.planDefenceRebuild(sim, ai, profile, bb);
            REQUIRE(plan.has_value());
            REQUIRE(plan->unitType == "ARMLLT");
            REQUIRE(plan->site.distanceSquared(site) < 1_ss);
            REQUIRE_FALSE(plan->wreck.has_value());

            // Reading the losses again counts nothing twice.
            planner.recordLostDefences(sim, profile, bb);
            REQUIRE(planner.getLostDefenceSites().front().timesLost == 1);
        }

        SECTION("put back: teeth on the side away from the base, then nothing more")
        {
            auto towerId = addUnit(sim, "ARMLLT", ai, site, script);
            bb.standingBuildings[towerId.value] = StandingBuilding{"ARMLLT", site};
            bb.now = GameTime(300 + delay);
            REQUIRE_FALSE(planner.planDefenceRebuild(sim, ai, profile, bb).has_value());

            // Wrapped round it: the face away from the base first, then its
            // corners and round the sides, every one against the tower.
            for (int i = 0; i < profile.fortifyWrapTeeth; ++i)
            {
                auto next = planner.planFortification(sim, ai, profile, bb, rng);
                REQUIRE(next.has_value());
                REQUIRE(next->unitType == "ARMDRAG");
                REQUIRE((next->site - site).dot(east) >= 0_ss);
                REQUIRE(flatDistanceBetween(next->site, site) <= 48_ss);
                addUnit(sim, "ARMDRAG", ai, next->site, script);
            }
            // Lost once: no missile tower yet.
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());

            SECTION("and lost again with its teeth in front: a missile tower behind it")
            {
                bb.now = GameTime(3000);
                bb.recentLosses.insert(bb.recentLosses.begin(), LostBuilding{"ARMLLT", site, GameTime(3000)});
                planner.recordLostDefences(sim, profile, bb);
                REQUIRE(planner.getLostDefenceSites().front().timesLost == 2);

                auto next = planner.planFortification(sim, ai, profile, bb, rng);
                REQUIRE(next.has_value());
                REQUIRE(next->unitType == "ARMRL");
                REQUIRE((next->site - site).dot(east) <= 0_ss);
                REQUIRE(flatDistanceBetween(next->site, site) <= profile.fortifyMissileCoverRadius);
            }

            SECTION("switched off, nothing")
            {
                profile.fortifyRebuiltDefences = false;
                bb.recentLosses.insert(bb.recentLosses.begin(), LostBuilding{"ARMLLT", site, GameTime(3000)});
                REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
            }
        }

        SECTION("a solar collector lost is nobody's to put back")
        {
            bb.now = GameTime(400);
            bb.recentLosses.insert(bb.recentLosses.begin(), LostBuilding{"ARMSOLAR", SimVector(200_ss, 0_ss, 200_ss), GameTime(400)});
            planner.recordLostDefences(sim, profile, bb);
            REQUIRE(planner.getLostDefenceSites().size() == 1);
        }

        SECTION("switched off, nothing is remembered")
        {
            profile.rebuildLostDefences = false;
            planner.recordLostDefences(sim, profile, bb);
            REQUIRE(planner.getLostDefenceSites().empty());
            (void)human;
        }
    }

    TEST_CASE("teeth go where a defence keeps being attacked from, and nowhere else", "[ai]")
    {
        // Driven directly rather than through a controller, so that when an
        // attack happens and from where is set by hand: the watch is asked
        // to look, the tower loses hit points with an armed enemy standing
        // on one side of it, the enemy leaves, and the watch looks again.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMDRAG"] = makeDef(false, false, false, "", 10u);

        const SimVector tower(0_ss, 0_ss, 0_ss);
        auto towerId = addUnit(sim, "ARMLLT", ai, tower, script);
        sim.getUnitState(towerId).hitPoints = 100;

        auto profile = makeDefaultStandardProfile();
        profile.fortifyTowers = false;
        profile.fortifyWhereAttacked = true;

        AiBlackboard bb{};
        bb.sideUnits = resolveAiSideUnits(sim, "ARM");
        bb.sideUnitsResolved = true;
        bb.baseAnchor = SimVector(-300_ss, 0_ss, 0_ss);
        bb.standingBuildings[towerId.value] = StandingBuilding{"ARMLLT", tower};
        REQUIRE(bb.sideUnits.dragonsTeeth == "ARMDRAG");

        const auto gap = static_cast<unsigned int>(profile.fortifyAttackGapSeconds) * 30u;
        const SimVector east(1_ss, 0_ss, 0_ss);
        BuildManager planner;
        std::minstd_rand rng(42u);
        unsigned int now = 0;

        auto look = [&] {
            bb.now = GameTime(now);
            planner.watchDefences(sim, profile, bb);
        };
        auto attackFrom = [&](const SimVector& from) {
            bb.knownEnemies[9999u] = KnownEnemy{UnitId(9999u), "ARMPW", tower + from, GameTime(now), false, true, false};
            now += 30;
            sim.getUnitState(towerId).hitPoints -= 10;
            look();
            // And gone again, as raiders are, for longer than the gap
            // that makes the next hit a separate attack.
            bb.knownEnemies.clear();
            now += gap + 30;
            look();
        };
        look();

        SECTION("twice from the east: a few teeth to the east, and then no more")
        {
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());

            attackFrom(SimVector(300_ss, 0_ss, 40_ss));
            auto plan = planner.planFortification(sim, ai, profile, bb, rng);
            REQUIRE(plan.has_value());
            REQUIRE(plan->unitType == "ARMDRAG");
            REQUIRE(plan->tower == towerId);
            // Against the tower, the middle of its east face.
            REQUIRE(rweAbs(plan->site.x - 32_ss) <= 1_ss);
            REQUIRE(rweAbs(plan->site.z) <= 1_ss);

            // Then its corners and round the sides: the ring's east half.
            for (int i = 0; i < profile.fortifyWrapTeeth; ++i)
            {
                auto next = planner.planFortification(sim, ai, profile, bb, rng);
                REQUIRE(next.has_value());
                REQUIRE((next->site - tower).dot(east) >= 0_ss);
                REQUIRE(flatDistanceBetween(next->site, tower) <= 48_ss);
                addUnit(sim, "ARMDRAG", ai, next->site, script);
            }
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
        }

        SECTION("unwrapped, twice from the east: a short line out to the east")
        {
            profile.fortifyTeethWrap = false;
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            attackFrom(SimVector(300_ss, 0_ss, 40_ss));
            for (int i = 0; i < profile.fortifyReactiveTeeth; ++i)
            {
                auto next = planner.planFortification(sim, ai, profile, bb, rng);
                REQUIRE(next.has_value());
                REQUIRE((next->site - tower).dot(east) > profile.fortifyTeethDistance - 30_ss);
                addUnit(sim, "ARMDRAG", ai, next->site, script);
            }
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
        }

        SECTION("once from the east and once from the north: nothing")
        {
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            attackFrom(SimVector(0_ss, 0_ss, -300_ss));
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
        }

        SECTION("off: nothing, however often")
        {
            profile.fortifyWhereAttacked = false;
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            attackFrom(SimVector(300_ss, 0_ss, 0_ss));
            REQUIRE_FALSE(planner.planFortification(sim, ai, profile, bb, rng).has_value());
        }
    }

    TEST_CASE("a tower faces where losses actually came from, not the enemy's unseen base", "[ai]")
    {
        // threatDirection falls back to the world origin when no enemy base
        // has been seen, which is the whole opening and any later stretch
        // where contact has been lost -- not a real threat direction. A
        // tower lost to the south should steer the next one there instead
        // of towards the map's middle.
        //
        // The casualty is itself a light laser tower rather than, say, a
        // solar collector: recentLosses feeds "replace what was just
        // destroyed" too (buildPriorities' last rule), which reorders the
        // wanted list to put back whatever type was lost first of all --
        // a solar collector would then jump the queue ahead of the very
        // tower this test is about, for a reason that has nothing to do
        // with facing. A lost tower is already wanted first (it is what
        // targetDefenceCount asks for), so the reorder is a no-op here and
        // the test is actually isolating the facing logic.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        const SimVector anchor(0_ss, 0_ss, 0_ss);
        layOutBase(sim, ai, script, anchor);
        // Standing long enough to be noticed, then destroyed -- the raid.
        auto raidedId = addUnit(sim, "ARMLLT", ai, anchor + SimVector(0_ss, 0_ss, -500_ss), script);

        auto profile = makeDefaultStandardProfile();

        SECTION("on: the tower is posted south, towards the loss")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(raidedId).markAsDead();
            runTicks(sim, controller, 30, commands);
            REQUIRE(controller.getBlackboard().recentLosses.size() == 1);

            auto order = theBuildOrder(commands, "ARMLLT");
            REQUIRE(order.position.z < -50_ss);
        }

        SECTION("off: the knob restores the old facing, towards the map's middle")
        {
            profile.defenceFacesRecentLosses = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(raidedId).markAsDead();
            runTicks(sim, controller, 30, commands);
            REQUIRE(controller.getBlackboard().recentLosses.size() == 1);

            auto order = theBuildOrder(commands, "ARMLLT");
            // Facing (1,0,0): posted east, towards the map's middle. Not
            // asserted on z -- with no other tower yet standing, "forward"
            // is capped and tied for every candidate at the post's ring,
            // so the tie-break between a candidate north and one south of
            // it is the RNG's, not the facing logic's, to answer.
            REQUIRE(order.position.x > 50_ss);
        }
    }

    TEST_CASE("a tower has to be worth its metal", "[ai]")
    {
        // towerCostJustified is a pure function of the profile, the
        // blackboard's income and the tower's own cost, so it is tested
        // directly rather than through a whole game -- the same style as
        // canAfford's "a stockpile at the cap" case above.
        auto profile = makeDefaultStandardProfile();
        AiBlackboard bb;
        UnitDefinition tower{};
        tower.buildCostMetal = Metal(600.0f);

        SECTION("an economy too poor to pay it back in time is refused")
        {
            bb.metalIncome = Metal(1.0f);
            // 90 seconds (the default) of a 1/s economy is 90 metal, nowhere near 600.
            REQUIRE_FALSE(BuildManager::towerCostJustified(profile, bb, tower));
        }

        SECTION("a healthy economy clears it easily")
        {
            bb.metalIncome = Metal(20.0f);
            // 90 seconds of a 20/s economy is 1800, comfortably over 600.
            REQUIRE(BuildManager::towerCostJustified(profile, bb, tower));
        }

        SECTION("an outpost earns extra allowance for what it covers")
        {
            bb.metalIncome = Metal(1.0f);
            REQUIRE_FALSE(BuildManager::towerCostJustified(profile, bb, tower, 0));
            // 90 + 6*45 = 360, still short; 90 + 12*45 = 630 clears it.
            REQUIRE_FALSE(BuildManager::towerCostJustified(profile, bb, tower, 6));
            REQUIRE(BuildManager::towerCostJustified(profile, bb, tower, 12));
        }

        SECTION("a reading of zero income is unmeasured, not poor, and does not block")
        {
            bb.metalIncome = Metal(0.0f);
            REQUIRE(BuildManager::towerCostJustified(profile, bb, tower));
        }

        SECTION("the knob switches the whole test off")
        {
            profile.defenceValueMaxPaybackSeconds = 0;
            bb.metalIncome = Metal(1.0f);
            REQUIRE(BuildManager::towerCostJustified(profile, bb, tower));
        }
    }

    TEST_CASE("an income buys an outpost tower more than the flat cap allows", "[ai]")
    {
        // Two clusters of two extractors out beyond a base brought in to 200,
        // one of them already under a tower, and a cap of one.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMMEX", ai, SimVector(-400_ss, 0_ss, -400_ss), script);
        addUnit(sim, "ARMMEX", ai, SimVector(-440_ss, 0_ss, -400_ss), script);
        addUnit(sim, "ARMLLT", ai, SimVector(-420_ss, 0_ss, -380_ss), script);
        addUnit(sim, "ARMMEX", ai, SimVector(400_ss, 0_ss, -400_ss), script);
        addUnit(sim, "ARMMEX", ai, SimVector(440_ss, 0_ss, -400_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.defendRadius = 200_ss;
        profile.outpostDefenceCount = 1;
        profile.outpostTowerIncomeStep = 8;
        profile.outpostDefenceMax = 4;

        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);
        auto bb = controller.getBlackboard();
        BuildManager planner;

        SECTION("on a small income the one tower is the cap")
        {
            bb.metalIncome = Metal(4.0f);
            REQUIRE_FALSE(planner.planOutpostDefence(sim, ai, profile, bb).has_value());
        }

        SECTION("eight a second more buys a second")
        {
            bb.metalIncome = Metal(8.0f);
            auto plan = planner.planOutpostDefence(sim, ai, profile, bb);
            REQUIRE(plan.has_value());
            REQUIRE(plan->anchor.x > 0_ss);
        }

        SECTION("with no step, the flat cap holds whatever the income")
        {
            profile.outpostTowerIncomeStep = 0;
            bb.metalIncome = Metal(40.0f);
            REQUIRE_FALSE(planner.planOutpostDefence(sim, ai, profile, bb).has_value());
        }
    }

    TEST_CASE("the first towers go out on the edge of the base, not among it", "[ai]")
    {
        // From a replay review: "first few defences should be built on the
        // outer cusp of the base".
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        // A base built out to 400 in the direction the enemy lies.
        for (int i = 1; i <= 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(i * 100.0f), 0_ss, 60_ss), script);
        }
        addUnit(sim, "ARMSOLAR", human, SimVector(2000_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;

        auto siteDistance = [&](AiTuningProfile& p) {
            AiPlayerController controller(ai, p, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);
            BuildManager planner;
            ReachabilityMap reachability;
            std::minstd_rand rng(7u);
            auto site = planner.chooseDefenceSite(sim, ai, p, controller.getBlackboard(), reachability, "ARMLLT", rng);
            REQUIRE(site.has_value());
            return rweSqrt((site->x * site->x) + (site->z * site->z));
        };

        SECTION("out past the buildings")
        {
            CHECK(siteDistance(profile) > profile.defenceDistanceFromBase);
        }

        SECTION("switched off, at the old distance from the anchor")
        {
            auto off = profile;
            off.firstDefencesOnPerimeter = false;
            auto nearBase = siteDistance(off);
            auto onEdge = siteDistance(profile);
            CHECK(onEdge > nearBase);
        }
    }

    TEST_CASE("tier two fortifies the towers even with the tier-one switch off", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLLT", ai, SimVector(200_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.fortifyTowers = false;
        profile.fortifyWhereAttacked = false;
        profile.rebuildLostDefences = false;

        auto planned = [&](const AiTuningProfile& p) {
            AiPlayerController controller(ai, p, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);
            BuildManager planner;
            std::minstd_rand rng(7u);
            return planner.planFortification(sim, ai, p, controller.getBlackboard(), rng).has_value();
        };

        SECTION("tier one: nothing")
        {
            CHECK_FALSE(planned(profile));
        }

        SECTION("with an advanced lab standing: teeth")
        {
            addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, 0_ss), script);
            CHECK(planned(profile));
        }

        SECTION("switched off, tier two changes nothing")
        {
            addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, 0_ss), script);
            auto off = profile;
            off.fortifyAtTierTwo = false;
            CHECK_FALSE(planned(off));
        }
    }
}
