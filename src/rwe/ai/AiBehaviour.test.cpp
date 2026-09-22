#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

/**
 * The AI's economy: what it builds first, what it can pay for, when it techs,
 * and what its builders are put on.
 */
namespace rwe
{
    TEST_CASE("the AI builds no further solar collector while energy is going spare", "[ai]")
    {
        // Measured on Great Divide, a quarter to a third of all the energy a
        // side made was thrown away, because collectors were built out to
        // targetSolarCount whatever the grid was doing. solarOnDemand skips
        // the next one while the store is four-fifths full and income is
        // ahead of demand; the metal goes to whatever is wanted next.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        // The opening collectors are not the rule's business: a side with
        // nothing built has no grid to read. It governs the ones after.
        auto profile = makeDefaultStandardProfile();
        REQUIRE(profile.targetSolarCount > profile.openingSolarCount);
        for (int i = 0; i < profile.openingSolarCount; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }

        // What the commander is asked for over its next several jobs, each
        // one stood up finished the moment it is ordered so the plan moves
        // on. Where a collector falls among the other wants is the plan's
        // affair and changes; whether one is asked for at all is the rule.
        //
        // The economy figures are the simulation's own, rewritten each tick,
        // so they are put in place between its tick and the AI's.
        auto collectorsOrdered = [&](const AiTuningProfile& p, float stored, float income, float demand) {
            AiPlayerController controller(ai, p, 42u, MapIntel{});
            int collectors = 0;
            for (int job = 0; job < 14; ++job)
            {
                std::vector<PlayerCommand> commands;
                for (int i = 0; i < 31; ++i)
                {
                    sim.tick();
                    auto& player = sim.getPlayer(ai);
                    player.maxMetal = Metal(5000.0f);
                    player.metal = Metal(5000.0f);
                    player.maxEnergy = Energy(1000.0f);
                    player.energy = Energy(stored);
                    player.previousEnergyProductionBuffer = Energy(income);
                    player.previousDesiredEnergyConsumptionBuffer = Energy(demand);
                    controller.tick(sim, commands);
                }
                auto types = buildOrderTypes(commands);
                if (types.empty())
                {
                    // A pass spent on something other than a building.
                    sim.getUnitState(commanderId).orders.clear();
                    continue;
                }
                UNSCOPED_INFO("job " << job << ": " << types.front());
                if (types.front() == "ARMSOLAR")
                {
                    ++collectors;
                }
                addUnit(sim, types.front(), ai, SimVector(SimScalar(-420.0f + (job % 7) * 130.0f), 0_ss, SimScalar(-400.0f + (job / 7) * 150.0f)), script);
                sim.getUnitState(commanderId).orders.clear();
            }
            return collectors;
        };

        SECTION("a full store and a surplus: none")
        {
            REQUIRE(collectorsOrdered(profile, 950.0f, 60.0f, 20.0f) == 0);
        }

        SECTION("a full store but more drawn than made: collectors")
        {
            REQUIRE(collectorsOrdered(profile, 950.0f, 20.0f, 60.0f) > 0);
        }

        SECTION("a surplus but a store half empty: collectors")
        {
            REQUIRE(collectorsOrdered(profile, 500.0f, 60.0f, 20.0f) > 0);
        }

        SECTION("with the rule off, collectors whatever the grid is doing")
        {
            profile.solarOnDemand = false;
            REQUIRE(collectorsOrdered(profile, 950.0f, 60.0f, 20.0f) > 0);
        }
    }

    TEST_CASE("the AI opens with power, metal and a factory, then produces constructors", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        SECTION("the first job is a solar collector")
        {
            auto types = buildOrderTypes(commands);
            REQUIRE(types.size() == 1);
            REQUIRE(types.front() == "ARMSOLAR");
        }

        SECTION("once the opening quotas exist the commander builds the lab")
        {
            for (int i = 0; i < 4; ++i)
            {
                addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
            }
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
            }
            sim.getUnitState(commanderId).orders.clear();
            commands.clear();
            runTicks(sim, controller, 31, commands);
            auto types = buildOrderTypes(commands);
            REQUIRE(types.size() == 1);
            REQUIRE(types.front() == "ARMLAB");
        }

        SECTION("a factory with an empty queue is told to build constructors first")
        {
            addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
            commands.clear();
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") == 1);
            REQUIRE(controller.getBlackboard().phase == GamePhase::Boom);
        }
    }

    TEST_CASE("the AI does not start what it cannot pay for", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        // The opening quotas are met, so the lab is what the plan wants next.
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

        SECTION("with the metal in hand the lab goes up")
        {
            runTicks(sim, controller, 31, commands);
            REQUIRE(buildOrderTypes(commands) == std::vector<std::string>{"ARMLAB"});
        }

        SECTION("with no metal and no income, nothing is started")
        {
            sim.getPlayer(ai).metal = Metal(0.0f);
            runTicks(sim, controller, 31, commands);
            REQUIRE(buildOrderTypes(commands).empty());
        }

        SECTION("a stockpile at the cap is spent whatever the rate, because income over the cap is lost")
        {
            sim.getPlayer(ai).metal = Metal(1000.0f);
            sim.getPlayer(ai).maxMetal = Metal(1000.0f);
            AiBlackboard bb;
            bb.currentMetal = Metal(1000.0f);
            bb.metalStorage = Metal(1000.0f);
            bb.metalIncome = Metal(0.0f);
            bb.metalDemand = Metal(50.0f);
            BuildManager::BuildEstimate expensive{5000.0f, 100.0f};
            REQUIRE(BuildManager::canAfford(bb, expensive));
        }

        SECTION("the estimate is the unbuilt share of the price at the builder's own rate")
        {
            UnitDefinition target{};
            target.buildTime = 3000u;
            target.buildCostMetal = Metal(600.0f);
            UnitDefinition builder{};
            builder.workerTimePerTick = 5u;
            auto fresh = BuildManager::estimateBuild(target, builder);
            REQUIRE(fresh.metal == 600.0f);
            REQUIRE(fresh.seconds == 20.0f);
            auto halfDone = BuildManager::estimateBuild(target, builder, 1500u);
            REQUIRE(halfDone.metal == 300.0f);
            REQUIRE(halfDone.seconds == 10.0f);
        }

        SECTION("while it saves, the builder reclaims rather than spending on something cheaper")
        {
            // A little income, so the lab is within reach of waiting but not
            // of the stockpile: the rule is to wait for it, not to build the
            // radar instead and never get there.
            sim.getPlayer(ai).metal = Metal(0.0f);
            sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
            // Reclaiming needs the rock to have been seen. True line of
            // sight walks ray tables this bare simulation does not carry,
            // so it sees nothing past its own cell; circular sight is plain
            // geometry and does.
            sim.lineOfSightMode = LineOfSightMode::Circular;
            FeatureDefinition rock{};
            rock.name = "rock";
            rock.footprintX = 1;
            rock.footprintZ = 1;
            rock.reclaimable = true;
            rock.metal = 30;
            auto rockDef = sim.featureDefinitions.insert(rock);
            // Heightmap (34, 34) is a few tiles from the commander: world
            // coordinates run from the middle of the map, not its corner.
            auto rockId = sim.addFeature(rockDef, 34, 34).value();

            runTicks(sim, controller, 61, commands);
            REQUIRE(buildOrderTypes(commands).empty());
            auto reclaims = ordersFor<ReclaimOrder>(commands, commanderId);
            REQUIRE(!reclaims.empty());
            REQUIRE(std::get<FeatureId>(reclaims.front().target) == rockId);
        }

        SECTION("what a builder is already on counts against what the next one can afford")
        {
            // The commander is on a lab frame: 100 metal over the 3.3
            // seconds a worker of rate 1 takes, thirty a second. A second
            // builder falls idle with income of six and nothing in the
            // bank, and the radar it wants next is out of reach.
            sim.getPlayer(ai).metal = Metal(0.0f);
            sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
            auto frameId = addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
            sim.getUnitState(frameId).buildTimeCompleted = 0u;
            sim.getUnitState(commanderId).orders.push_back(BuildOrder("ARMLAB", SimVector(200_ss, 0_ss, 200_ss)));
            sim.getUnitState(commanderId).buildOrderUnitId = frameId;
            auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);

            runTicks(sim, controller, 61, commands);
            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.metalCommitted.value > 29.0f);
            REQUIRE(bb.metalCommitted.value < 31.0f);
            REQUIRE(bb.orphanedFrames.empty());
            REQUIRE(bb.idleBuilders == std::vector<UnitId>{kbotId});
            REQUIRE(buildOrderTypes(commands).empty());
        }
    }

    TEST_CASE("a frame nobody is working on is finished before anything new is started", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
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

        // Half a radar, as a raid that killed the constructor would leave it.
        auto frameId = addUnit(sim, "ARMRAD", ai, SimVector(150_ss, 0_ss, 150_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 50u;

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        REQUIRE(controller.getBlackboard().orphanedFrames == std::vector<UnitId>{frameId});
        REQUIRE(buildOrderTypes(commands).empty());
        auto repairs = ordersFor<RepairOrder>(commands, commanderId);
        REQUIRE(repairs.size() == 1);
        REQUIRE(repairs.front().target == frameId);

        SECTION("once a builder is on its way it is nobody's orphan")
        {
            sim.getUnitState(commanderId).orders.push_back(RepairOrder(frameId));
            commands.clear();
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().orphanedFrames.empty());
            REQUIRE(ordersFor<RepairOrder>(commands, commanderId).empty());
        }
    }

    TEST_CASE("a builder is only asked for what it has a button for", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
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

        // An enemy aircraft in view, so anti-air is wanted. The commander has
        // no Defender button; the construction kbot does.
        auto human = PlayerId(0);
        auto bomber = makeDef(false, false, true, "LASER", 200u);
        bomber.canFly = true;
        sim.unitDefinitions["ARMTHUND"] = bomber;
        addUnit(sim, "ARMTHUND", human, SimVector(60_ss, 80_ss, 60_ss), script);

        SECTION("without a tree the commander builds anti-air it could never have ordered")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            auto types = buildOrderTypes(commands);
            REQUIRE(std::find(types.begin(), types.end(), "ARMRL") != types.end());
        }

        SECTION("with the tree it does not, and the construction kbot does instead")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            auto commanderTypes = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(std::none_of(commanderTypes.begin(), commanderTypes.end(), [](const BuildOrder& o) { return o.unitType == "ARMRL"; }));

            // One builder is planned per pass, the lowest-numbered idle one,
            // so the commander is sent away to leave the kbot at the front.
            auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);
            sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));
            commands.clear();
            runTicks(sim, controller, 31, commands);
            auto kbotBuilds = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(!kbotBuilds.empty());
            REQUIRE(kbotBuilds.front().unitType == "ARMRL");
        }
    }

    TEST_CASE("a rich base techs to level two, and the advanced lab makes its constructor first", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        // The level-one plan satisfied, so nothing above the tech rule wants
        // anything: the opening quotas and both expansion targets are met.
        auto profile = makeDefaultStandardProfile();
        // Teching ships off, because measured it loses (see the knob's own
        // comment). The rules are still meant to work when it is asked for.
        profile.techLevelTwo = true;
        for (int i = 0; i < profile.targetSolarCount; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < profile.targetMetalExtractorCount; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }
        addUnit(sim, "ARMRAD", ai, SimVector(-200_ss, 0_ss, 200_ss), script);
        // The rest of the level-one plan, all of which outranks teching and
        // rightly so: a couple of towers and an air plant are cheap, and a
        // player has them long before an advanced lab.
        for (int i = 0; i < profile.targetDefenceCount; ++i)
        {
            addUnit(sim, "ARMLLT", ai, SimVector(SimScalar(-300.0f - i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < profile.baseAntiAirTowerCount; ++i)
        {
            addUnit(sim, "ARMRL", ai, SimVector(SimScalar(-300.0f - i * 40.0f), 0_ss, 80_ss), script);
        }
        addUnit(sim, "ARMAP", ai, SimVector(300_ss, 0_ss, -200_ss), script);
        addUnit(sim, "ARMVP", ai, SimVector(300_ss, 0_ss, -280_ss), script);
        // Teching is judged on income, not on a full store, so the
        // extractors have to actually earn: two a second each, which over
        // the target count clears techMinMetalIncome comfortably.
        sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
        // Rich as well, so nothing is waiting on the stockpile.
        sim.getPlayer(ai).metal = Metal(1000.0f);
        sim.getPlayer(ai).maxMetal = Metal(1000.0f);

        // One builder is planned per pass, the lowest-numbered idle one, so
        // the commander is sent walking to leave the kbot at the front.
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));

        SECTION("the construction kbot puts up the advanced lab")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto kbotBuilds = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(!kbotBuilds.empty());
            REQUIRE(kbotBuilds.front().unitType == "ARMALAB");
        }

        SECTION("a level two that is no better than level one is not worth its factory")
        {
            // Same hit points as the raider now, so the lab buys nothing.
            sim.unitDefinitions["ARMZEUS"].maxHitPoints = sim.unitDefinitions["ARMPW"].maxHitPoints;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().advancedArmyValueRatio == 1.0f);
            // Nothing else is left wanting on this base, so the honest test
            // is that no advanced lab was ordered by anyone -- not that
            // something cheaper was ordered instead.
            auto types = buildOrderTypes(commands);
            REQUIRE(std::find(types.begin(), types.end(), "ARMALAB") == types.end());
        }

        SECTION("with teching switched off it builds a level-one tower instead")
        {
            profile.techLevelTwo = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto kbotBuilds = ordersFor<BuildOrder>(commands, kbotId);
            REQUIRE(!kbotBuilds.empty());
            REQUIRE(kbotBuilds.front().unitType != "ARMALAB");
        }

        SECTION("the advanced lab makes the advanced constructor, then assault kbots")
        {
            addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMACK") == 1);

            addUnit(sim, "ARMACK", ai, SimVector(-240_ss, 0_ss, -240_ss), script);
            commands.clear();
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 1);
        }

        SECTION("and the advanced constructor is the one that reaches the advanced radar")
        {
            addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
            auto advancedId = addUnit(sim, "ARMACK", ai, SimVector(-240_ss, 0_ss, -240_ss), script);
            sim.getUnitState(kbotId).addOrder(MoveOrder(SimVector(-600_ss, 0_ss, -600_ss)));
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto builds = ordersFor<BuildOrder>(commands, advancedId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMARAD");
        }
    }

    TEST_CASE("the factories are held while the first moho and reactor are paid for", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMCK", ai, SimVector(-100_ss, 0_ss, -100_ss), script);
        addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
        addUnit(sim, "ARMACK", ai, SimVector(-240_ss, 0_ss, -240_ss), script);
        // An army, so that holding production is not leaving the base naked.
        for (int i = 0; i < 6; ++i)
        {
            addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-300.0f + i * 40.0f), 0_ss, 300_ss), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("no assault kbots while neither stands")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 0);
            REQUIRE(countQueueCommands(commands, "ARMPW") == 0);
            REQUIRE(countQueueCommands(commands, "ARMROCK") == 0);
        }

        SECTION("production resumes once both do")
        {
            addUnit(sim, "ARMMOHO", ai, SimVector(400_ss, 0_ss, -300_ss), script);
            addUnit(sim, "ARMFUS", ai, SimVector(400_ss, 0_ss, -400_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 1);
        }

        SECTION("switched off, the factories never stop")
        {
            profile.tierTwoEconomyReserve = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 1);
        }

        SECTION("and an enemy at the gates ends the hold")
        {
            addUnit(sim, "ARMPW", PlayerId(0), SimVector(250_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMZEUS") == 1);
        }
    }

    TEST_CASE("an economy stuck for minutes turns its spare energy into metal", "[ai]")
    {
        // Watched on Dark Side, ARM against CORE, forty minutes. CORE ended
        // on six extractors and 8 metal a second, metal-stalled for 39% of
        // the game, with 68894 energy thrown away at the cap -- and one
        // metal maker standing, because targetMetalMakerCount is a flat
        // ceiling of two. ARM was better off and still threw away 13302.
        //
        // The surplus rule that would have built more
        // (maxSurplusMetalMakerCount) is off by default and rightly so: it
        // was measured on Great Divide and lost, because a builder's time is
        // worth more spent on another extractor. What that does not cover is
        // a builder that has been unable to turn its time into metal for
        // minutes on end, which is what the run of passes below tests and
        // what a single stalled tick does not.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);

        // defineWorld has the maker in the build tree but not in the unit
        // table, so it is defined here as the switching test defines it.
        auto maker = makeDef(false, false, false, "", 50u);
        maker.onOffable = true;
        maker.makesMetal = Metal(1.0f);
        sim.unitDefinitions["ARMMAKR"] = maker;

        // The flat ceiling, already standing.
        addUnit(sim, "ARMMAKR", ai, SimVector(120_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMMAKR", ai, SimVector(160_ss, 0_ss, 0_ss), script);

        // Generation genuinely ahead of demand, which is half of what the
        // rule reads: the test world's solar collector makes nothing by
        // default.
        auto solar = sim.unitDefinitions.at("ARMSOLAR");
        solar.energyMake = Energy(20.0f);
        sim.unitDefinitions["ARMSOLAR"] = solar;
        for (int i = 0; i < 8; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-200.0f - (i * 40.0f)), 0_ss, 0_ss), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        REQUIRE(profile.targetMetalMakerCount == 2);
        REQUIRE(profile.starvedMetalMakerCount > 2);

        // Metal short, energy at the cap: the other half.
        auto& player = sim.getPlayer(ai);
        player.maxMetal = Metal(1000.0f);
        player.metal = Metal(0.0f);
        player.maxEnergy = Energy(1000.0f);
        player.energy = Energy(1000.0f);

        auto wantsAnotherMaker = [&]() {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto types = buildOrderTypes(commands);
            return std::find(types.begin(), types.end(), "ARMMAKR") != types.end();
        };

        SECTION("stuck long enough: another maker")
        {
            // The run-length is what the game supplies and a fixture cannot
            // sit through, so it is asked for on the first pass here and
            // pinned as a rule by the section below.
            profile.starvedMetalMakerPasses = 0;
            CHECK(wantsAnotherMaker());
        }

        SECTION("stalled once is not stuck: the flat ceiling holds")
        {
            // One planning pass of starvation against a default that wants
            // a hundred and twenty of them in a row. This is the half of the
            // rule that keeps it off a side that is merely saving up.
            REQUIRE(profile.starvedMetalMakerPasses > 1);
            CHECK_FALSE(wantsAnotherMaker());
        }

        SECTION("switched off, the flat ceiling of two is back")
        {
            profile.starvedMetalMakerPasses = 0;
            profile.starvedMetalMakerCount = 0;
            CHECK_FALSE(wantsAnotherMaker());
        }
    }

    TEST_CASE("a metal maker is told to switch once, not once a tick", "[ai]")
    {
        // A command takes half a second to land, and the maker reads as it
        // did until then. Told every pass, it was sent the same switch over
        // and over -- and one of those copies arriving after the maker had
        // been shot is what ended a game with a bad variant access.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto maker = makeDef(false, false, false, "", 50u);
        maker.onOffable = true;
        maker.makesMetal = Metal(1.0f);
        sim.unitDefinitions["ARMMAKR"] = maker;
        auto makerId = addUnit(sim, "ARMMAKR", ai, SimVector(200_ss, 0_ss, 0_ss), script);
        sim.getUnitState(makerId).activated = true;
        // Nothing in the tank: the makers go off.
        sim.getPlayer(ai).energy = Energy(0.0f);
        sim.getPlayer(ai).maxEnergy = Energy(1000.0f);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        // Twenty ticks, in none of which the command is applied.
        runTicks(sim, controller, 20, commands);

        int switches = 0;
        for (const auto& command : commands)
        {
            auto unitCommand = std::get_if<PlayerUnitCommand>(&command);
            if (unitCommand != nullptr && unitCommand->unit == makerId && std::holds_alternative<PlayerUnitCommand::SetOnOff>(unitCommand->command))
            {
                REQUIRE_FALSE(std::get<PlayerUnitCommand::SetOnOff>(unitCommand->command).on);
                ++switches;
            }
        }
        REQUIRE(switches == 1);
    }

    TEST_CASE("builders mend a damaged defence first, then a damaged factory", "[ai]")
    {
        RepairBase base;
        auto profile = makeDefaultStandardProfile();

        SECTION("the tower before the lab, though the lab is as badly hurt")
        {
            base.sim.getUnitState(base.towerId).hitPoints = 40;
            base.sim.getUnitState(base.labId).hitPoints = 40;
            REQUIRE(base.firstRepair(profile) == std::optional<UnitId>(base.towerId));
        }

        SECTION("the lab, once the towers are whole")
        {
            base.sim.getUnitState(base.labId).hitPoints = 40;
            REQUIRE(base.firstRepair(profile) == std::optional<UnitId>(base.labId));
        }

        SECTION("a solar collector is not worth the trip")
        {
            base.sim.getUnitState(base.solarId).hitPoints = 40;
            REQUIRE_FALSE(base.firstRepair(profile).has_value());
        }

        SECTION("off, nothing is mended")
        {
            profile.repairStructures = false;
            base.sim.getUnitState(base.towerId).hitPoints = 40;
            REQUIRE_FALSE(base.firstRepair(profile).has_value());
        }

        SECTION("with the raider still beside it, repairUnderFire decides")
        {
            auto human = addPlayer(base.sim, "human", GamePlayerType::Human, "ARM");
            base.sim.getUnitState(base.towerId).hitPoints = 40;
            addUnit(base.sim, "ARMPW", human, SimVector(250_ss, 0_ss, -100_ss), base.script);

            SECTION("on: mended all the same")
            {
                profile.repairUnderFire = true;
                REQUIRE(base.firstRepair(profile) == std::optional<UnitId>(base.towerId));
            }

            SECTION("off: left until it has gone")
            {
                profile.repairUnderFire = false;
                REQUIRE_FALSE(base.firstRepair(profile).has_value());
            }
        }
    }

    TEST_CASE("a guard is sent to a builder placed away from the base, and released once it is done", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        const SimVector anchor(-300_ss, 0_ss, 0_ss);
        layOutBase(sim, ai, script, anchor);
        // A small army to draw the guard from.
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMPW", ai, anchor + SimVector(0_ss, 0_ss, SimScalar(-200.0f - i * 40.0f)), script);
        }

        auto profile = makeDefaultStandardProfile();
        // The ordinary front-of-base tower -- 160 out, per layOutBase's
        // world -- already qualifies as "away from the base" for this test.
        profile.buildSiteGuardMinDistance = 50_ss;
        // The feature ships off, having measured worse than not having it,
        // so this asks for it by name. What it pins -- that a guard is
        // detached to the site and released when the job ends -- is the
        // behaviour anyone switching it back on would be relying on.
        profile.buildSiteGuardSize = 2;

        SECTION("a guard is detached to the site, and released once the tower stands")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            auto order = theBuildOrder(commands, "ARMLLT");
            REQUIRE(bb.buildSiteGuardRequest.has_value());
            bool guardsExactBuildSite = bb.buildSiteGuardRequest->position == order.position;
            REQUIRE(guardsExactBuildSite);
            REQUIRE(bb.guardGroup.size() == static_cast<std::size_t>(profile.buildSiteGuardSize));

            int guardMoves = 0;
            for (auto id : bb.guardGroup)
            {
                auto moves = ordersFor<MoveOrder>(commands, UnitId(id));
                if (!moves.empty())
                {
                    REQUIRE(flatDistanceBetween(moves.back().destination, order.position) < 1_ss);
                    ++guardMoves;
                }
            }
            REQUIRE(guardMoves > 0);

            // The tower stands, and the builder that placed it is idle
            // again -- the job the guard was sent for is over.
            addUnit(sim, "ARMLLT", ai, order.position, script);
            sim.getUnitState(bb.buildSiteGuardRequest->builderId).orders.clear();
            runTicks(sim, controller, 20, commands);
            REQUIRE_FALSE(controller.getBlackboard().buildSiteGuardRequest.has_value());
            REQUIRE(controller.getBlackboard().guardGroup.empty());
        }

        SECTION("the knob at zero never asks for one")
        {
            profile.buildSiteGuardSize = 0;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            theBuildOrder(commands, "ARMLLT");

            const auto& bb = controller.getBlackboard();
            REQUIRE_FALSE(bb.buildSiteGuardRequest.has_value());
            REQUIRE(bb.guardGroup.empty());
        }
    }

    TEST_CASE("the one guard slot is held by the most threatened site, not the most recent", "[ai]")
    {
        // There is a single buildSiteGuardRequest, and it used to be taken by
        // whichever qualifying build order came last. Ten games at hard
        // difficulty made 346 requests and produced 55 guards, so most were
        // overwritten before anyone stood anywhere -- and the four builders
        // that actually died were at sites reading 14988, 13560, 0 and 0.
        // Recency was throwing away exactly the requests worth keeping.
        //
        // Measured over the same games, a tower is where the danger is: 12 of
        // 21 laser tower sites had an enemy that could reach them against 25
        // of 325 extractor sites, and a tower builder died at four times the
        // rate. So the slot should end up on the tower under fire rather than
        // on the next extractor started in an empty corner.
        const SimVector somewhere(500_ss, 0_ss, 500_ss);

        SECTION("an empty slot takes whatever asks for it")
        {
            std::optional<AiBlackboard::BuildSiteGuardRequest> held;
            REQUIRE(BuildManager::guardRequestDisplaces(held, 0.0f));
            REQUIRE(BuildManager::guardRequestDisplaces(held, 14988.0f));
        }

        SECTION("a more threatened site takes the slot off a quieter one")
        {
            std::optional<AiBlackboard::BuildSiteGuardRequest> held(
                AiBlackboard::BuildSiteGuardRequest{somewhere, UnitId(1), GameTime(0), 0.0f});
            REQUIRE(BuildManager::guardRequestDisplaces(held, 13560.0f));
        }

        SECTION("a quieter site does not take it off a threatened one")
        {
            // The case the measurement found: a tower going up under fire
            // must not lose its guard to an extractor nothing is near.
            std::optional<AiBlackboard::BuildSiteGuardRequest> held(
                AiBlackboard::BuildSiteGuardRequest{somewhere, UnitId(1), GameTime(0), 14988.0f});
            REQUIRE_FALSE(BuildManager::guardRequestDisplaces(held, 0.0f));
            REQUIRE_FALSE(BuildManager::guardRequestDisplaces(held, 13560.0f));
        }

        SECTION("with the threat test switched off every site ties, and recency decides as before")
        {
            // buildSiteGuardThreat at zero leaves every site reading zero.
            // Equal threat has to keep displacing or the slot would freeze on
            // the first request until it timed out -- the kill switch is meant
            // to restore the old behaviour exactly, not a new one.
            std::optional<AiBlackboard::BuildSiteGuardRequest> held(
                AiBlackboard::BuildSiteGuardRequest{somewhere, UnitId(1), GameTime(0), 0.0f});
            REQUIRE(BuildManager::guardRequestDisplaces(held, 0.0f));
        }

        SECTION("an equally threatened site also displaces, for the same reason")
        {
            std::optional<AiBlackboard::BuildSiteGuardRequest> held(
                AiBlackboard::BuildSiteGuardRequest{somewhere, UnitId(1), GameTime(0), 825.0f});
            REQUIRE(BuildManager::guardRequestDisplaces(held, 825.0f));
        }
    }

    TEST_CASE("a builder assisting a factory is offered to the planner again", "[ai]")
    {
        // The bug this pins: a builder sent to lend a hand at a factory
        // carries a GuardOrder that nothing ever takes off, and idleBuilders
        // used to require an empty order queue -- so a builder that once ran
        // out of work was gone from the planning pool for the rest of the
        // game. It should still be offered to the planner, just behind the
        // genuinely idle ones, and it should not be double-counted as idle.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto factoryId = addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        auto kbotId = addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);
        sim.getUnitState(kbotId).orders.push_back(GuardOrder(factoryId));

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.idleBuilders == std::vector<UnitId>{kbotId});
        REQUIRE(bb.idleBuilderCount == 0);
    }

    TEST_CASE("the planner takes each available builder in turn", "[ai]")
    {
        // Before plannerCursor the planner always read idleBuilders.front(),
        // which in id order is whichever builder exists longest -- so with
        // more than one idle builder, only the first was ever served and the
        // rest sat unplanned for no matter how long the game went on.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Only a commander sets bb.homePosition/baseAnchor; without one
        // BuildManager::update has nowhere to anchor the plan and returns
        // before looking at either builder. Sent walking so it does not
        // itself become the one idle builder served.
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));
        addUnit(sim, "ARMCK", ai, SimVector(40_ss, 0_ss, 40_ss), script);
        addUnit(sim, "ARMCK", ai, SimVector(-40_ss, 0_ss, -40_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});

        std::vector<PlayerCommand> firstPass;
        runTicks(sim, controller, 30, firstPass);
        std::optional<UnitId> firstBuilder;
        for (const auto& c : firstPass)
        {
            if (auto unitCommand = std::get_if<PlayerUnitCommand>(&c))
            {
                firstBuilder = unitCommand->unit;
                break;
            }
        }
        REQUIRE(firstBuilder.has_value());

        std::vector<PlayerCommand> secondPass;
        runTicks(sim, controller, 30, secondPass);
        std::optional<UnitId> secondBuilder;
        for (const auto& c : secondPass)
        {
            if (auto unitCommand = std::get_if<PlayerUnitCommand>(&c))
            {
                secondBuilder = unitCommand->unit;
                break;
            }
        }
        REQUIRE(secondBuilder.has_value());

        REQUIRE(*firstBuilder != *secondBuilder);
    }

    TEST_CASE("an advanced constructor spends the tier even with teching off", "[ai]")
    {
        // The level-two spending rules ask only whether an advanced lab
        // stands, not whether the tech knob is on: deciding to tech is one
        // question and spending a tier already bought is another. Before
        // this, gating both on the same knob meant a finished advanced
        // constructor was left with an empty list -- and sent to nanolathe
        // Peewees instead -- the moment teching was switched off, even with
        // the lab already paid for.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto profile = makeDefaultStandardProfile();
        profile.techLevelTwo = false;

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // Out of the way, so the idle advanced constructor is the one the
        // planner reaches.
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(600_ss, 0_ss, 600_ss)));

        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        addUnit(sim, "ARMALAB", ai, SimVector(-200_ss, 0_ss, -200_ss), script);
        auto advancedId = addUnit(sim, "ARMACK", ai, SimVector(-240_ss, 0_ss, -240_ss), script);

        // Income clears techMinMetalIncome comfortably. The stockpile is
        // plentiful but kept well under the cap, so a fusion plant is not
        // also in the running -- this is about the moho and the radar.
        sim.unitDefinitions["ARMMEX"].metalMake = Metal(2.0f);
        for (int i = 0; i < profile.targetMetalExtractorCount; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }
        sim.getPlayer(ai).metal = Metal(500.0f);
        sim.getPlayer(ai).maxMetal = Metal(2000.0f);

        AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto builds = ordersFor<BuildOrder>(commands, advancedId);
        REQUIRE(!builds.empty());
        REQUIRE((builds.front().unitType == "ARMMOHO" || builds.front().unitType == "ARMARAD"));
    }

    TEST_CASE("the lab stops making kbots for ground they cannot reach", "[ai]")
    {
        // makeChannelTerrain is the fixture that actually produces
        // hasUnreachableGround -- the ferry test above asserts it on this
        // same terrain -- but only once the kbots cannot wade the channel,
        // which is what the movement class override below is for. Without
        // it the far bank is reachable on foot and the cap never engages.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeChannelTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        addUnit(sim, "ARMCOM", ai, SimVector(-300_ss, 60_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
        // Three of them, so armySize clears a cap of two.
        for (auto z : {0_ss, 30_ss, 60_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(-200_ss, 60_ss, z), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        // Straight to the raider branch: the lab makes its constructors first
        // and would otherwise never reach the units under test.
        profile.targetConstructorCount = 0;

        // The branch picks rocket kbots here, not raiders -- with three
        // raiders owned and no rocket kbots, "raiders <= rockets * 2" is
        // false -- so both are counted, or the test would pass for the wrong
        // reason.
        auto kbotsQueued = [](const std::vector<PlayerCommand>& commands) {
            return countQueueCommands(commands, "ARMPW") + countQueueCommands(commands, "ARMROCK");
        };

        // The cap asks how much of the map is water as well as whether some
        // ground is out of reach, so the map has to say so. The channel
        // fixture supplies the unreachable ground and this supplies the
        // water: they are separate inputs, because MapIntel is computed from
        // the map at load and handed to the AI rather than derived from the
        // terrain here.
        //
        // A default-constructed MapIntel is not merely low on water, it is
        // invalid, and that keeps the cap off outright -- the same
        // conservative reading navalFleetTarget already takes. Without the
        // map's own analysis the AI does not know it is on an island map, so
        // it does not behave as though it were.
        MapIntel islandMap;
        islandMap.valid = true;
        islandMap.character = MapCharacter::Water;
        islandMap.waterFraction = 0.92f;

        SECTION("an army below the cap keeps being made")
        {
            // Off by default, so the lab keeps working. The default was
            // briefly 12 above isolatedLandArmyCapMinWaterFraction and was
            // measured back to zero the same day -- the cap halved the land
            // army and bought no hulls with the savings. What this pins holds
            // either way: an uncapped lab, or an army under the cap, goes on
            // being made.
            AiPlayerController controller(ai, profile, 42u, islandMap);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            REQUIRE(controller.getBlackboard().hasUnreachableGround);
            REQUIRE(kbotsQueued(commands) >= 1);
        }

        SECTION("a map that is merely half water does not earn the cap")
        {
            // The gate, and the reason it is deliberately not
            // MapCharacter::Water: that threshold is 0.40, which would take
            // in Coast To Coast at 54% -- the one map where capping was
            // measurably a regression, army 29.5 against 87.1 and income 9.8
            // against 11.2 over twenty games. So a half-water map keeps its
            // land army even with the cap set and the army over it.
            MapIntel halfWater;
            halfWater.valid = true;
            halfWater.character = MapCharacter::Water;
            halfWater.waterFraction = 0.54f;

            profile.isolatedLandArmyCap = 2;
            AiPlayerController controller(ai, profile, 42u, halfWater);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.hasUnreachableGround);
            REQUIRE(bb.armySize >= 3);
            REQUIRE(kbotsQueued(commands) >= 1);
        }

        SECTION("with the cap set, the lab goes quiet and the income is freed")
        {
            profile.isolatedLandArmyCap = 2;
            AiPlayerController controller(ai, profile, 42u, islandMap);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.hasUnreachableGround);
            REQUIRE(bb.armySize >= 3);
            REQUIRE(kbotsQueued(commands) == 0);
        }

        SECTION("the vehicle plant is capped too, not just the kbot lab")
        {
            // The cap guarded one of the three branches that make land combat
            // units, and a knob that stops the lab while the plant beside it
            // goes on making tanks has not capped anything. Pinned here
            // because every other assertion in this test would still pass
            // with the plant's guard removed.
            addUnit(sim, "ARMVP", ai, SimVector(-250_ss, 60_ss, 60_ss), script);
            profile.targetScoutVehicleCount = 0;

            profile.isolatedLandArmyCap = 2;
            AiPlayerController controller(ai, profile, 42u, islandMap);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            REQUIRE(controller.getBlackboard().hasUnreachableGround);
            REQUIRE(countQueueCommands(commands, "ARMFLASH") == 0);
            REQUIRE(kbotsQueued(commands) == 0);
        }
    }

    TEST_CASE("while metal lies free on our side the lab turns out construction units to take it", "[ai]")
    {
        ExpansionWorld w;
        auto profile = makeDefaultStandardProfile();

        SECTION("eight free deposits are worth two more constructors, so a second is queued")
        {
            AiPlayerController controller(w.ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(w.sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") == 1);
        }

        SECTION("switched off, the one it has is enough")
        {
            profile.expansionConstructors = 0;
            AiPlayerController controller(w.ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(w.sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") == 0);
        }

        SECTION("with every deposit taken, the one it has is enough")
        {
            for (std::size_t i = 0; i < ExpansionWorld::DepositCells.size(); ++i)
            {
                addUnit(w.sim, "ARMMEX", w.ai, ExpansionWorld::depositCentre(i), w.script);
            }
            AiPlayerController controller(w.ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(w.sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCK") == 0);
        }
    }

    TEST_CASE("a construction unit beyond the first goes for the free metal first", "[ai]")
    {
        ExpansionWorld w;
        auto secondId = addUnit(w.sim, "ARMCK", w.ai, SimVector(-100_ss, 0_ss, -50_ss), w.script);
        auto profile = makeDefaultStandardProfile();
        AiPlayerController controller(w.ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        // One job a planning pass, round the idle builders in turn: the
        // commander, the first kbot, then this one.
        runTicks(w.sim, controller, 95, commands);

        auto orders = ordersFor<BuildOrder>(commands, secondId);
        REQUIRE_FALSE(orders.empty());
        REQUIRE(orders.front().unitType == "ARMMEX");
    }

    TEST_CASE("income that outruns what the jobs can draw is a surplus", "[ai]")
    {
        auto profile = makeDefaultStandardProfile();
        AiBlackboard bb;
        bb.metalIncome = Metal(20.0f);

        SECTION("nothing running: the whole income is going spare")
        {
            bb.metalDemand = Metal(0.0f);
            CHECK(BuildManager::incomeOutrunsSpending(profile, bb));
        }

        SECTION("jobs drawing nearly all of it: no surplus")
        {
            bb.metalDemand = Metal(18.0f);
            CHECK_FALSE(BuildManager::incomeOutrunsSpending(profile, bb));
        }

        SECTION("drawing under four fifths of it: a surplus")
        {
            bb.metalDemand = Metal(15.0f);
            CHECK(BuildManager::incomeOutrunsSpending(profile, bb));
        }

        SECTION("switched off")
        {
            bb.metalDemand = Metal(0.0f);
            profile.spendSurplusOnCapacity = false;
            CHECK_FALSE(BuildManager::incomeOutrunsSpending(profile, bb));
        }
    }

    TEST_CASE("metal it cannot spend buys another factory and more builders", "[ai]")
    {
        // A base with its opening quotas long met and an income no single
        // builder can keep up with.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        sim.unitDefinitions["ARMMEX"].metalMake = Metal(10.0f);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        for (int i = 0; i < 10; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(100.0f + i * 40.0f), 0_ss, 0_ss), script);
        }
        for (int i = 0; i < 8; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(0_ss, 0_ss, SimScalar(100.0f + i * 40.0f)), script);
        }
        addUnit(sim, "ARMLAB", ai, SimVector(200_ss, 0_ss, 200_ss), script);
        // The one construction unit targetConstructorCount asks for, so that
        // anything further is the surplus rule's doing. (The free deposits
        // rule cannot add any: this map's metal is all under the extractors
        // above.)
        addUnit(sim, "ARMCK", ai, SimVector(-100_ss, 0_ss, 50_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("the lab is told to make more construction units")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            // Past capacitySurplusSeconds of planning passes.
            runTicks(sim, controller, 800, commands);
            CHECK(countQueueCommands(commands, "ARMCK") >= 1);
        }

        SECTION("switched off, the target stands")
        {
            profile.spendSurplusOnCapacity = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 800, commands);
            CHECK(countQueueCommands(commands, "ARMCK") == 0);
        }
    }

    TEST_CASE("whether the air tier is worth having is decided from the game, not from a list", "[ai]")
    {
        // The economy streams, so what the metal is worth spending on has a
        // different answer at minute three and minute twenty. This is the air
        // half of that judgement (AiBlackboard::airWorthIt): it is what puts
        // the advanced aircraft plant in the plan, and what lets the saving
        // rule wait the long window for it instead of skipping past to a
        // solar collector every pass for the rest of the game.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;

        SECTION("a plain land map with nothing of theirs standing: not yet")
        {
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            CHECK_FALSE(controller.getBlackboard().airWorthIt);
        }

        SECTION("a wall of their towers: yes")
        {
            // A wall is what a ground army cannot walk through and an
            // aircraft does not have to. Four of them, which is the default
            // airWorthItEnemyDefences.
            REQUIRE(profile.airWorthItEnemyDefences == 4);
            for (int i = 0; i < 4; ++i)
            {
                addUnit(sim, "ARMLLT", human, SimVector(SimScalar(800.0f + (i * 80.0f)), 0_ss, 800_ss), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            CHECK(controller.getBlackboard().airWorthIt);
        }

        SECTION("three towers is a picket, not a wall")
        {
            for (int i = 0; i < 3; ++i)
            {
                addUnit(sim, "ARMLLT", human, SimVector(SimScalar(800.0f + (i * 80.0f)), 0_ss, 800_ss), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            CHECK_FALSE(controller.getBlackboard().airWorthIt);
        }

        SECTION("their extractors are not a wall however many there are")
        {
            // Armed is the test, not merely built. An unarmed building is
            // what a bomber is FOR, not a reason the ground cannot get there.
            for (int i = 0; i < 8; ++i)
            {
                addUnit(sim, "ARMMEX", human, SimVector(SimScalar(800.0f + (i * 80.0f)), 0_ss, 800_ss), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            CHECK_FALSE(controller.getBlackboard().airWorthIt);
        }

        SECTION("switched off, no number of towers moves it")
        {
            profile.airWorthItEnemyDefences = 0;
            for (int i = 0; i < 8; ++i)
            {
                addUnit(sim, "ARMLLT", human, SimVector(SimScalar(800.0f + (i * 80.0f)), 0_ss, 800_ss), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            CHECK_FALSE(controller.getBlackboard().airWorthIt);
        }
    }

    TEST_CASE("what the enemy is made of leans the lab's shares", "[ai]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        defineWorld(sim);
        sim.unitDefinitions["ARMLLT"].buildCostMetal = Metal(100.0f);
        sim.unitDefinitions["ARMPW"].buildCostMetal = Metal(100.0f);
        auto profile = makeDefaultStandardProfile();
        AiBlackboard bb;
        auto remember = [&](unsigned int id, const std::string& type, bool building) {
            bb.knownEnemies.emplace(id, KnownEnemy{UnitId(id), type, SimVector(0_ss, 0_ss, 0_ss), GameTime(0), building, true, false});
        };

        SECTION("towers of theirs buy artillery")
        {
            remember(1, "ARMLLT", true);
            remember(2, "ARMLLT", true);
            auto shares = BuildManager::counterShares(sim, profile, bb);
            CHECK(shares.artilleryKbot == profile.labArtilleryKbotShare + profile.counterShareBonus);
            CHECK(shares.rocketKbot == profile.labRocketKbotShare);
            CHECK(shares.raider == profile.labRaiderShare);
        }

        SECTION("an army of theirs buys rocket kbots")
        {
            remember(1, "ARMPW", false);
            remember(2, "ARMPW", false);
            auto shares = BuildManager::counterShares(sim, profile, bb);
            CHECK(shares.rocketKbot == profile.labRocketKbotShare + profile.counterShareBonus);
            CHECK(shares.artilleryKbot == profile.labArtilleryKbotShare);
        }

        SECTION("both, when they have both")
        {
            remember(1, "ARMPW", false);
            remember(2, "ARMLLT", true);
            auto shares = BuildManager::counterShares(sim, profile, bb);
            CHECK(shares.rocketKbot == profile.labRocketKbotShare + profile.counterShareBonus);
            CHECK(shares.artilleryKbot == profile.labArtilleryKbotShare + profile.counterShareBonus);
        }

        SECTION("nothing seen, nothing leaned")
        {
            auto shares = BuildManager::counterShares(sim, profile, bb);
            CHECK(shares.raider == profile.labRaiderShare);
            CHECK(shares.rocketKbot == profile.labRocketKbotShare);
            CHECK(shares.artilleryKbot == profile.labArtilleryKbotShare);
        }

        SECTION("switched off")
        {
            profile.counterEnemyComposition = false;
            remember(1, "ARMLLT", true);
            auto shares = BuildManager::counterShares(sim, profile, bb);
            CHECK(shares.artilleryKbot == profile.labArtilleryKbotShare);
        }
    }
}
