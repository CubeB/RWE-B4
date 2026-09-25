#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

/**
 * The naval half: shipyards, hulls, what goes under the water, and getting
 * an army across it.
 *
 * The fixtures these share are in ai_test_util.h.
 */
namespace rwe
{
    namespace
    {
        /**
         * Two dry shores with open water between them: heightmap x in
         * [0, 10) to the west, [54, 64) to the east, everything between 60
         * deep. World space is centred, so the west shore is world x
         * -512..-352 and the east island 352..512 -- the water between them
         * straddles zero, which is the whole point of the fixture.
         *
         * makeWaterMapTerrain will not do for this: it has a single dry
         * strip, so both sides stand on the same ground and nothing is ever
         * across water from anything.
         */
        MapTerrain makeTwoShoresTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 10; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
                for (int x = 54; x < 64; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
            }
            return MapTerrain(std::move(heights), 60_ss);
        }
    }

    TEST_CASE("the AI ferries a builder across water it cannot walk", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeChannelTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Kbots cannot wade the channel.
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }
        // World space is centred on the map: the channel at tiles 28-36 runs
        // down world x -64..64, and the base sits on the west bank.
        // A rich patch on the east bank, at tile (50, 32): world (296, 8).
        for (int y = 32; y < 34; ++y)
        {
            for (int x = 50; x < 52; ++x)
            {
                sim.metalGrid.set(x, y, static_cast<unsigned char>(200));
            }
        }

        auto commanderPosition = SimVector(-300_ss, 60_ss, 0_ss);
        addUnit(sim, "ARMCOM", ai, commanderPosition, script);
        auto builderId = addUnit(sim, "ARMCK", ai, SimVector(-250_ss, 60_ss, 0_ss), script);
        const SimVector farBankPatch(296_ss, 60_ss, 8_ss);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        SECTION("the base knows the far bank is out of reach")
        {
            runTicks(sim, controller, 2, commands);
            const auto& reach = controller.getReachabilityMap();
            REQUIRE(reach.isValid());
            REQUIRE(reach.isReachable(sim, commanderPosition));
            REQUIRE_FALSE(reach.isReachable(sim, farBankPatch));
            REQUIRE(reach.isWalkable(sim, farBankPatch));
            REQUIRE_FALSE(reach.isWalkable(sim, SimVector(0_ss, 0_ss, 0_ss)));
            REQUIRE(controller.getBlackboard().hasUnreachableGround);
        }

        SECTION("an idle transport is sent to carry the builder to the patch")
        {
            auto atlasId = addUnit(sim, "ARMATLAS", ai, SimVector(-280_ss, 120_ss, 20_ss), script);
            runTicks(sim, controller, 16, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.wantsTransport);
            auto loads = ordersFor<LoadOrder>(commands, atlasId);
            REQUIRE(loads.size() == 1);
            REQUIRE(loads.front().target == builderId);
            auto unloads = ordersFor<UnloadOrder>(commands, atlasId);
            REQUIRE(unloads.size() == 1);
            // Set down on the east bank, on the patch.
            REQUIRE(unloads.front().destination.x > 64_ss);
            REQUIRE(unloads.front().destination.distanceSquared(farBankPatch) < (32_ss * 32_ss));
            REQUIRE(bb.ferryPassengers.count(builderId.value) == 1);
            REQUIRE(controller.getTransportManager().getFerries().count(atlasId.value) == 1);

            // Booked passengers are left alone by the builder planner.
            commands.clear();
            runTicks(sim, controller, 30, commands);
            REQUIRE(ordersFor<BuildOrder>(commands, builderId).empty());
        }

        SECTION("the air plant queues a transport once a scout plane exists and there is ground to reach")
        {
            addUnit(sim, "ARMAP", ai, SimVector(-200_ss, 60_ss, -100_ss), script);
            addUnit(sim, "ARMPEEP", ai, SimVector(-200_ss, 120_ss, -40_ss), script);
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().wantsTransport);
            REQUIRE(controller.getBlackboard().factories.size() == 1);
            REQUIRE(countQueueCommands(commands, "ARMPEEP") == 0);
            REQUIRE(countQueueCommands(commands, "ARMATLAS") == 1);
        }
    }

    TEST_CASE("naval: a shipyard goes up on a water map, in water deep enough for it", "[ai]")
    {
        // makeWaterMapTerrain's shelf (tiles 10-13) is water but too shallow
        // for ARMSY's own MinWaterDepth=30; only tiles 14 and beyond clear
        // it. Every existing priority ahead of the naval branch in
        // buildPriorities is zeroed out or pre-satisfied below, so the
        // shipyard is the first thing the commander's one planning pass can
        // actually find a site for and afford -- see BuildManager.cpp's
        // buildPriorities for why that ordering matters.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);
        REQUIRE_FALSE(mapIntel.shipyardSites.empty());

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingMetalExtractorCount = 0;
        profile.openingSolarCount = 0;
        profile.targetMetalExtractorCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto builds = ordersFor<BuildOrder>(commands, commanderId);
        REQUIRE(!builds.empty());
        REQUIRE(builds.front().unitType == "ARMSY");

        // The site is one MapIntel actually nominated for its depth, and
        // specifically one on the deep side of the shelf -- not a ring
        // search from the (dry) anchor, which chooseBuildSite would have
        // run and which could never satisfy MinWaterDepth=30 at all.
        auto matched = std::find_if(mapIntel.shipyardSites.begin(), mapIntel.shipyardSites.end(), [&](const NavalSite& s) {
            return s.position.distanceSquared(builds.front().position) < (1_ss * 1_ss);
        });
        REQUIRE(matched != mapIntel.shipyardSites.end());
        REQUIRE(matched->tile.x >= 14);
    }

    TEST_CASE("naval: a water map takes its energy out of the water", "[ai]")
    {
        // The AI had no vocabulary for a water structure at all: AiSideUnits
        // carried the shipyard, the hulls and the construction ship, and not
        // one of the things that stand in the water. On a 92% water map that
        // left it competing with its own factories and extractors for the
        // little dry ground there is, to plant 5x5 solar collectors on it.
        //
        // Shipped numbers, which are what make this worth doing: ARMSOLAR is
        // 5x5, 145 metal, MaxWaterDepth=0. ARMTIDE is 3x3, 82 metal,
        // MinWaterDepth=20. Cheaper, nine cells against twenty-five, and it
        // stands where nothing else wants to.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The tidal generator at its shipped shape. Defined here rather than
        // in defineWorld so that the thirty-odd tests which know nothing
        // about water carry on resolving it to empty and skipping the rule.
        auto tide = makeDef(false, false, false, "", 100u);
        tide.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 20u, 255u};
        tide.buildCostMetal = Metal(82.0f);
        sim.unitDefinitions["ARMTIDE"] = tide;

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        // The lab pre-placed, because it is wanted above the water block and
        // would otherwise be the only thing this test ever saw ordered.
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingMetalExtractorCount = 0;
        profile.openingSolarCount = 0;
        profile.targetMetalExtractorCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;
        // The yard and the sonar silenced so the tidal generator is what is
        // left in the water block, not what happens to win a race with them.
        profile.targetShipyardCount = 0;
        profile.targetSonarCount = 0;
        profile.targetTorpedoLauncherCount = 0;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto builds = ordersFor<BuildOrder>(commands, commanderId);
        auto tidal = std::find_if(builds.begin(), builds.end(), [](const BuildOrder& b) { return b.unitType == "ARMTIDE"; });
        REQUIRE(tidal != builds.end());

        // And it is IN the water, which is the half that proves the siting
        // rather than the wanting. A tidal generator is exempt from the
        // build-site reachability gate on purpose -- every site it has is
        // ground the builder cannot walk to, so the gate that stops the
        // commander crossing to another island would otherwise refuse the
        // lot. canBeBuiltAt is what keeps it honest, through the same
        // MinWaterDepth test the shipyard sites go through.
        REQUIRE(sim.terrain.getHeightAt(tidal->position.x, tidal->position.z) < sim.terrain.getSeaLevel());
    }

    TEST_CASE("naval: the metal under the water is worth taking once the dry patches are gone", "[ai]")
    {
        // Every submerged metal patch on a water map was worth nothing to the
        // AI, because the only extractor it knew about is MaxWaterDepth=0.
        // ARMUWMEX is the same ExtractsMetal=0.001 for 130 metal instead of
        // 50 -- not better metal, metal that was otherwise unreachable.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The ordinary extractor at its REAL draught. makeDef leaves
        // maxWaterDepth at 255, which would let a 50-metal ARMMEX sit on the
        // seabed and take the patch before the underwater one was ever
        // reached -- the test would pass with none of this code running.
        // ARMMEX is MaxWaterDepth=0 in the shipped data.
        sim.unitDefinitions.at("ARMMEX").movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 0u, 0u};

        auto uwmex = makeDef(false, false, false, "", 50u);
        uwmex.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 19u, 255u};
        uwmex.buildCostMetal = Metal(130.0f);
        sim.unitDefinitions["ARMUWMEX"] = uwmex;

        // A patch out in the deep, east of the shelf. Heightmap x >= 14 is
        // height 0 against sea level 60, so the water there is 60 deep --
        // comfortably past ARMUWMEX's MinWaterDepth of 19 and impossible for
        // anything with MaxWaterDepth=0.
        for (int y = 30; y <= 33; ++y)
        {
            for (int x = 20; x <= 23; ++x)
            {
                sim.metalGrid.set(x, y, static_cast<unsigned char>(200));
            }
        }

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingSolarCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;
        profile.targetShipyardCount = 0;
        profile.targetTidalCount = 0;
        profile.targetSonarCount = 0;
        profile.targetTorpedoLauncherCount = 0;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto builds = ordersFor<BuildOrder>(commands, commanderId);
        auto submerged = std::find_if(builds.begin(), builds.end(), [](const BuildOrder& b) { return b.unitType == "ARMUWMEX"; });
        REQUIRE(submerged != builds.end());

        // On the patch, and under the sea. Both halves matter: the first says
        // it went where the metal is, the second that the reachability gate
        // was skipped rather than the site being dry after all.
        auto tile = sim.terrain.worldToHeightmapCoordinate(submerged->position);
        REQUIRE(sim.metalGrid.get(tile.x, tile.y) > sim.surfaceMetal);
        REQUIRE(sim.terrain.getHeightAt(submerged->position.x, submerged->position.z) < sim.terrain.getSeaLevel());
    }

    TEST_CASE("naval: the torpedo launcher waits for something to shoot at", "[ai]")
    {
        // ARMTL is 804 metal and CORTL 831 -- a destroyer's price for
        // something that cannot move. A wet map is not on its own a reason to
        // spend it; an enemy hull in the water is. This is the difference
        // between a defence worth what it costs and one built because the map
        // looked dangerous, and it is the half of this feature that is a
        // judgement rather than a fact, so it gets pinned.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The torpedo launcher at its shipped shape. MinWaterDepth=1, so it
        // sits in the shallows off a shore rather than out where the shipyard
        // goes.
        auto tl = makeDef(false, false, false, "LASER", 200u);
        tl.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 1u, 255u};
        tl.buildCostMetal = Metal(804.0f);
        sim.unitDefinitions["ARMTL"] = tl;

        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingMetalExtractorCount = 0;
        profile.openingSolarCount = 0;
        profile.targetMetalExtractorCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;
        profile.targetShipyardCount = 0;
        profile.targetTidalCount = 0;
        profile.targetSonarCount = 0;
        profile.targetTorpedoLauncherCount = 1;
        // knownEnemies is what the gate reads, and omniscience is what fills
        // it without waiting for a scout to fly. makeDefaultStandardProfile
        // leaves it off -- only the brutal profile sets it -- so it is set
        // here by hand.
        profile.cheatModeOmniscient = true;

        SECTION("a wet map on its own does not earn one")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(std::none_of(builds.begin(), builds.end(), [](const BuildOrder& b) { return b.unitType == "ARMTL"; }));
        }

        SECTION("an enemy hull in the water does")
        {
            // A destroyer, out in the deep. ARMROY is mobile, does not fly,
            // and its movement class has MinWaterDepth=12 -- which is the
            // whole test: a movement class with a minimum water depth can
            // only float.
            addUnit(sim, "ARMROY", human, SimVector(200_ss, 0_ss, 0_ss), script);

            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(std::any_of(builds.begin(), builds.end(), [](const BuildOrder& b) { return b.unitType == "ARMTL"; }));
        }
    }

    TEST_CASE("naval: the shipyard comes before the rest of the base on a water map", "[ai]")
    {
        // The ordering test that the siting test above deliberately is not.
        // That one zeroes every competing priority so the yard is the only
        // thing the planner can reach, which answers "can it find water deep
        // enough" and says nothing at all about WHEN a yard is wanted. This
        // one leaves the competition standing -- radar, the towers, the air
        // plant, the vehicle plant, the advanced lab, and the run up to ten
        // solars -- and asserts the yard beats all of it.
        //
        // That matters because the yard used to sit thirteenth in
        // buildPriorities, which is why a fleet of nine was still a fleet of
        // five when the game ended: on a map where navalFleetTarget is
        // non-zero the yard is what the lab is on land, the factory that
        // makes the only units able to reach the enemy at all.
        //
        // The opening economy is pre-built rather than waited for. The
        // opening extractors and solars are listed ABOVE the first lab, and
        // the yard sits directly below it, so with the opening unmet the
        // first want is an extractor and the ordering under test is never
        // reached.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The dry strip is heightmap x in [0, 10), which is world x -512 to
        // -352, so everything below stands on land.
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(SimScalar(-470.0f + static_cast<float>(i) * 24.0f), 90_ss, 48_ss), script);
        }
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-470.0f + static_cast<float>(i) * 24.0f), 90_ss, -48_ss), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        // The premise of the whole test: a zero here is the naval kill switch
        // and every assertion below would pass or fail for the wrong reason.
        REQUIRE(profile.navalFleetSize > 0);
        // The three owned extractors already meet this. Left at its default of
        // eight, the "don't sink the commander into a factory while metal is
        // short" rule sits ABOVE the lab and would ask for a fourth extractor
        // first -- pre-empting the ordering under test and failing the test
        // for a reason that has nothing to do with it. The siting test above
        // never met this only because it set the target to zero.
        profile.targetMetalExtractorCount = 3;

        SECTION("the yard is what the commander lays down next")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMSY");
        }

        SECTION("earlyShipyard=0 restores the old ordering exactly")
        {
            // The knob exists for this: a code change cannot be measured by
            // the arena, whose control arm runs the same binary and would
            // contain the change too. Something else has to come first here,
            // or "-tune earlyShipyard=0" is not a control at all.
            profile.earlyShipyard = false;

            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType != "ARMSY");
        }
    }

    TEST_CASE("naval: with nobody to walk to, the yard is the opening factory and not the lab", "[ai]")
    {
        // The AI opened with a kbot lab on every map, because that is what the
        // plan does on land, and nothing asked whether what it produces could
        // ever arrive. On a map of islands it cannot: every unit is born on our
        // own shore and stays there. Worse, the one factory that CAN reach
        // anybody was made to wait for it -- earlyShipyard tests
        // `total(lab) >= 1` -- so the yard queued behind a factory building
        // units for a war they could not attend.
        //
        // Two shores with water between them and a declared start position on
        // each: the smallest map on which "our army cannot walk to anybody" is
        // true and readable the way a player reads it off the preview.
        auto script = makeEmptyCobScript();
        auto terrain = makeTwoShoresTerrain();

        // 64 tiles of 16 world units, centred: the west shore is heightmap x in
        // [0, 10), world -512 to -352, and the east shore x in [54, 64), world
        // 352 to 512.
        auto westStart = SimVector(-420_ss, 90_ss, 0_ss);
        auto eastStart = SimVector(420_ss, 90_ss, 0_ss);

        GameSimulation sim(makeTwoShoresTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // Nothing that walks may cross 60-deep water. makeDef leaves
        // maxWaterDepth at 255, which wades the whole ocean and makes the far
        // shore perfectly reachable -- the test would then pin nothing, which
        // is exactly how it failed the first time it was run. The constructor
        // is the one that decides it, the ground layer being labelled for it,
        // but all three are constrained so the fixture says what it means.
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        auto commanderId = addUnit(sim, "ARMCOM", ai, westStart, script);
        // The opening economy, pre-built for the same reason the ordering test
        // above pre-builds it: with the opening unmet the first want is an
        // extractor and the ordering under test is never reached. Deliberately
        // NO lab -- whether one is wanted at all is the question.
        for (int i = 0; i < 3; ++i)
        {
            addUnit(sim, "ARMMEX", ai, SimVector(SimScalar(-470.0f + static_cast<float>(i) * 24.0f), 90_ss, 48_ss), script);
        }
        for (int i = 0; i < 4; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-470.0f + static_cast<float>(i) * 24.0f), 90_ss, -48_ss), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.targetMetalExtractorCount = 3;
        REQUIRE(profile.navalFleetSize > 0);

        SECTION("a start position on the far shore is not walkable, and the yard goes down first")
        {
            // At 2 the lab is deferred as well, which is the literal reading
            // of the request this came from. It is not the default -- see the
            // knob's comment and the roadmap -- so the test says so.
            profile.seaAirFactoriesWhenIsolated = 2;

            auto mapIntel = analyseMap(makeTwoShoresTerrain(), {westStart, eastStart});
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.landRouteToEnemy.has_value());
            REQUIRE_FALSE(*bb.landRouteToEnemy);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMSY");
        }

        SECTION("at 1 the yard goes first, and the lab still gets built")
        {
            // The milder of the two settings. The yard no longer queues
            // behind a factory whose units cannot attend the war, but the lab
            // is still wanted, because it is the AI's land builder and its
            // base defence and not only its army.
            profile.seaAirFactoriesWhenIsolated = 1;

            auto mapIntel = analyseMap(makeTwoShoresTerrain(), {westStart, eastStart});
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMSY");
        }

        SECTION("and once the yard stands, the lab is what comes next")
        {
            // The deferral is an ordering and not a ban, and this is the
            // assertion that says so. Asked with the yard already built
            // rather than by running on and hoping: the commander is busy
            // with a 615-metal yard for far longer than a test wants to sit
            // through, and "it did not appear in 400 ticks" would pin the
            // build time and not the plan.
            //
            // The water between the shores is 60 deep everywhere, which
            // clears ARMSY's MinWaterDepth=30 with room to spare.
            profile.seaAirFactoriesWhenIsolated = 1;
            // One yard is the target here, so the naval want is satisfied and
            // what comes next is the question being asked.
            profile.targetShipyardCount = 1;
            addUnit(sim, "ARMSY", ai, SimVector(-300_ss, 0_ss, 0_ss), script);

            auto mapIntel = analyseMap(makeTwoShoresTerrain(), {westStart, eastStart});
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMLAB");
        }

        SECTION("the default is off, and the ordering is exactly what it was")
        {
            // Measured 2026-09-23 as a regression at both settings, so the
            // machinery ships switched off and the lab opens on every map the
            // way it always did. This is the assertion that says the knob
            // costs nothing when nobody sets it; see its own comment for the
            // numbers and for issue #197, which is the disagreement between
            // that measurement and a play-test.
            REQUIRE(profile.seaAirFactoriesWhenIsolated == 0);

            auto mapIntel = analyseMap(makeTwoShoresTerrain(), {westStart, eastStart});
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            // The map is still read -- the blackboard says so -- and the plan
            // still ignores it, which is the whole of what 0 means.
            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.landRouteToEnemy.has_value());
            REQUIRE_FALSE(*bb.landRouteToEnemy);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMLAB");
        }

        SECTION("at 2 the lab is not wanted even once the yard stands")
        {
            profile.seaAirFactoriesWhenIsolated = 2;
            profile.targetShipyardCount = 1;
            addUnit(sim, "ARMSY", ai, SimVector(-300_ss, 0_ss, 0_ss), script);

            auto mapIntel = analyseMap(makeTwoShoresTerrain(), {westStart, eastStart});
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType != "ARMLAB");
        }

        SECTION("with the other start on our own shore it is business as usual")
        {
            // The control, and it has to be the map telling the difference
            // rather than the knob: same terrain, same profile, both starts
            // walkable from each other.
            auto mapIntel = analyseMap(makeTwoShoresTerrain(), {westStart, SimVector(-380_ss, 90_ss, 96_ss)});
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.landRouteToEnemy.has_value());
            REQUIRE(*bb.landRouteToEnemy);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMLAB");
        }

        SECTION("seaAirFactoriesWhenIsolated=0 restores the old ordering exactly")
        {
            profile.seaAirFactoriesWhenIsolated = 0;

            auto mapIntel = analyseMap(makeTwoShoresTerrain(), {westStart, eastStart});
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            auto builds = ordersFor<BuildOrder>(commands, commanderId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMLAB");
        }
    }

    TEST_CASE("naval: no shipyard is ever wanted on a land map", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto mapIntel = analyseMap(makeFlatTerrain(64, 64), {});
        REQUIRE(mapIntel.character == MapCharacter::Land);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 61, commands);

        auto types = buildOrderTypes(commands);
        REQUIRE(std::find(types.begin(), types.end(), "ARMSY") == types.end());
    }

    TEST_CASE("naval: navalFleetSize=0 restores today's behaviour exactly", "[ai]")
    {
        // The same water map and the same neutered priority list as "a
        // shipyard goes up on a water map", except for the one knob this
        // is about. Everything else held equal, flipping it back to zero
        // is what has to be the only thing standing between a shipyard and
        // none at all.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});
        REQUIRE(mapIntel.character == MapCharacter::Water);

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.openingMetalExtractorCount = 0;
        profile.openingSolarCount = 0;
        profile.targetMetalExtractorCount = 0;
        profile.targetSolarCount = 0;
        profile.targetMetalMakerCount = 0;
        profile.targetRadarCount = 0;
        profile.targetDefenceCount = 0;
        profile.baseAntiAirTowerCount = 0;
        profile.reactiveAntiAirTowerCount = 0;
        profile.outpostDefenceCount = 0;
        profile.targetAirPlantCount = 0;
        profile.targetVehiclePlantCount = 0;
        profile.surplusLabCount = 0;
        profile.navalFleetSize = 0; // the kill switch

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 31, commands);

        auto types = buildOrderTypes(commands);
        REQUIRE(std::find(types.begin(), types.end(), "ARMSY") == types.end());
    }

    TEST_CASE("naval: a shipyard builds a scout ship, then destroyers", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(0_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("eyes first")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMPT") == 1);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 0);
        }

        SECTION("then destroyers, once eyes are covered")
        {
            addUnit(sim, "ARMPT", ai, SimVector(50_ss, 60_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 1);
        }
    }

    TEST_CASE("naval: a yard whose hulls are shot as they are born stops feeding them in", "[ai]")
    {
        // The all-water soft-lock, in miniature. A nanoframe spawns with
        // zero hit points -- they are trunc(progress * maxdamage) -- so any
        // damage at all kills it, and one enemy scout ship parked off a
        // shipyard therefore destroys every hull the yard makes, for the
        // whole game, at a hundredth of what it costs us. Measured over ten
        // games on Brain Coral: 1910 quiet deaths, every one a weapon kill,
        // 1408 of them ARMROY frames killed by a single CORPT. See ROADMAP
        // Phase 2 and commit 42160d59 for the diagnosis.
        //
        // Nothing could notice, which is what made it a soft-lock rather
        // than a loss: recentLosses is diffed from standingBuildings and so
        // holds buildings only, so a whole production run could be
        // destroyed without a word, and enemiesNearBase was measured from
        // the base anchor rather than from the thing being shot.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The base stands on the dry strip in the west and the yard is off
        // the far corner: 1160 units between the commander and the gun,
        // against a defendRadius of 900. That gap is the point of the
        // fixture -- it is what makes the base anchor the wrong place to
        // measure this from, which is where the old code measured from.
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, -400_ss), script);
        auto yardId = addUnit(sim, "ARMSY", ai, SimVector(400_ss, 60_ss, 400_ss), script);
        // Eyes already covered, so the yard's next job is a destroyer.
        addUnit(sim, "ARMPT", ai, SimVector(360_ss, 60_ss, 400_ss), script);
        auto harasserId = addUnit(sim, "ARMPT", human, SimVector(440_ss, 60_ss, 380_ss), script);

        // The hull on the slipway. A frame, which is to say no hit points.
        auto frameId = addUnit(sim, "ARMROY", ai, SimVector(430_ss, 60_ss, 400_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0;
        sim.getUnitState(frameId).hitPoints = 0;

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;

        SECTION("the loss is remembered, the siege is seen, and the queue is left alone")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            const auto& bb = controller.getBlackboard();
            std::vector<PlayerCommand> commands;

            // The first pass has nothing to diff against, exactly as the
            // building side does not.
            runTicks(sim, controller, 2, commands);
            REQUIRE(bb.standingUnits.count(frameId.value) == 1);
            REQUIRE(bb.recentUnitLosses.empty());

            sim.getUnitState(frameId).markAsDead();
            runTicks(sim, controller, 2, commands);

            REQUIRE(bb.recentUnitLosses.size() == 1);
            REQUIRE(bb.recentUnitLosses.front().unitType == "ARMROY");
            REQUIRE(bb.recentUnitLosses.front().underConstruction);
            REQUIRE(bb.harassedFactories == std::vector<UnitId>{yardId});
            REQUIRE(bb.besiegedFactories == std::vector<UnitId>{yardId});

            // And the gun counts as an enemy at the base although the base
            // anchor is nowhere near it, which is what lets everything
            // hanging off enemiesNearBase answer at all.
            REQUIRE(std::find(bb.enemiesNearBase.begin(), bb.enemiesNearBase.end(), harasserId) != bb.enemiesNearBase.end());

            // Nothing more is handed to the gun. The yard is not told to
            // stop -- it is simply not topped up while the gun is there.
            std::vector<PlayerCommand> later;
            runTicks(sim, controller, 90, later);
            REQUIRE(countQueueCommands(later, "ARMROY") == 0);
            REQUIRE(countQueueCommands(later, "ARMPT") == 0);
        }

        SECTION("with the knob off, the old behaviour exactly")
        {
            profile.noticeProductionHarassment = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            const auto& bb = controller.getBlackboard();
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            sim.getUnitState(frameId).markAsDead();
            runTicks(sim, controller, 2, commands);

            // The memory itself is unconditional -- it costs a map diff and
            // nothing reads it unless the knob is on -- but nothing is done
            // with it, and a gun 1160 units from the anchor is invisible to
            // every defensive rule the AI has.
            REQUIRE(bb.recentUnitLosses.size() == 1);
            REQUIRE(bb.besiegedFactories.empty());
            REQUIRE(bb.enemiesNearBase.empty());

            std::vector<PlayerCommand> later;
            runTicks(sim, controller, 90, later);
            REQUIRE(countQueueCommands(later, "ARMROY") >= 1);
        }

    }

    TEST_CASE("naval: a yard goes back to work once the gun has gone", "[ai]")
    {
        // The pause is not a shutdown. The memory of the frames lost there
        // lasts UnitLossMemoryTicks, but the live half -- an armed enemy
        // actually sitting on the place -- does not wait for that to age
        // out, so the yard resumes the moment the gun dies or leaves.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(0_ss, 60_ss, 0_ss), script);
        addUnit(sim, "ARMPT", ai, SimVector(-60_ss, 60_ss, 0_ss), script);
        auto harasserId = addUnit(sim, "ARMPT", human, SimVector(60_ss, 60_ss, 0_ss), script);
        auto frameId = addUnit(sim, "ARMROY", ai, SimVector(30_ss, 60_ss, 0_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0;
        sim.getUnitState(frameId).hitPoints = 0;

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        const auto& bb = controller.getBlackboard();
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);
        sim.getUnitState(frameId).markAsDead();
        runTicks(sim, controller, 2, commands);
        REQUIRE(bb.besiegedFactories.size() == 1);

        sim.getUnitState(harasserId).markAsDead();
        std::vector<PlayerCommand> after;
        runTicks(sim, controller, 90, after);
        REQUIRE(bb.besiegedFactories.empty());
        // The memory is still there; it is the live half that cleared.
        REQUIRE(!bb.recentUnitLosses.empty());
        REQUIRE(countQueueCommands(after, "ARMROY") >= 1);
    }

    TEST_CASE("naval: a yard on a map with metal under the sea makes construction ships", "[ai]")
    {
        // Nothing ever queued one: the slot resolved, and its only reader was
        // a movement-class lookup. On a map with no dry ground there is no lab
        // either, so the commander was the only builder the AI would ever own.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto armcs = makeDef(false, true, true, "", 200u);
        armcs.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 6u, 255u};
        sim.unitDefinitions["ARMCS"] = armcs;
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(0_ss, 60_ss, 0_ss), script);
        // Eyes already covered, so the yard's next job is what comes after them.
        addUnit(sim, "ARMPT", ai, SimVector(50_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("with metal under the water, a builder comes before the warships")
        {
            // Open water east of the shelf, sixty deep.
            sim.metalGrid.set(40, 32, static_cast<unsigned char>(200));
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCS") == 1);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 0);
        }

        SECTION("with no metal under the water, the yard goes to warships as it always did")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCS") == 0);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 1);
        }

        SECTION("switched off, it does not, even with metal to take")
        {
            sim.metalGrid.set(40, 32, static_cast<unsigned char>(200));
            profile.targetConstructionShipCount = 0;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCS") == 0);
            REQUIRE(countQueueCommands(commands, "ARMROY") == 1);
        }
    }

    TEST_CASE("naval: an advanced shipyard builds cruisers, then a battleship behind them", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Shipped footprints, draughts and prices.
        auto armasy = makeDef(false, true, false, "", 200u);
        armasy.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 8u, 255u, 255u, 30u, 255u};
        armasy.buildCostMetal = Metal(2524.0f);
        sim.unitDefinitions["ARMASY"] = armasy;
        auto armcrus = makeDef(false, false, true, "LASER", 300u);
        armcrus.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 30u, 255u};
        armcrus.buildCostMetal = Metal(1719.0f);
        sim.unitDefinitions["ARMCRUS"] = armcrus;
        auto armbats = armcrus;
        armbats.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 30u, 255u};
        armbats.buildCostMetal = Metal(4404.0f);
        sim.unitDefinitions["ARMBATS"] = armbats;
        auto armaas = armcrus;
        armaas.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 30u, 255u};
        armaas.buildCostMetal = Metal(1358.0f);
        sim.unitDefinitions["ARMAAS"] = armaas;

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMASY", ai, SimVector(0_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.targetAdvancedShipyardCount = 1;

        SECTION("the hull with the depth charge first")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMCRUS") == 1);
            REQUIRE(countQueueCommands(commands, "ARMBATS") == 0);
        }

        SECTION("a battleship once two cruisers stand to escort it")
        {
            addUnit(sim, "ARMCRUS", ai, SimVector(120_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(120_ss, 60_ss, 120_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMBATS") == 1);
            REQUIRE(countQueueCommands(commands, "ARMCRUS") == 0);
        }

        SECTION("and cover before either, once something of theirs is flying")
        {
            auto bomber = makeDef(false, false, true, "LASER", 200u);
            bomber.canFly = true;
            sim.unitDefinitions["ARMTHUND"] = bomber;
            addUnit(sim, "ARMTHUND", human, SimVector(60_ss, 140_ss, 60_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(countQueueCommands(commands, "ARMAAS") == 1);
            REQUIRE(countQueueCommands(commands, "ARMCRUS") == 0);
        }

        SECTION("no anti-air ship against an empty sky")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMAAS") == 0);
        }
    }

    TEST_CASE("naval: with no ground for an air plant, the yard builds the sub that builds the seaplane platform", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto yard = makeDef(false, true, false, "", 200u);
        yard.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 8u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMASY"] = yard;
        auto platform = yard;
        platform.movementCollisionInfo = UnitDefinition::AdHocMovementClass{7u, 7u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMPLAT"] = platform;
        auto hull = makeDef(false, false, true, "LASER", 300u);
        hull.movementCollisionInfo = UnitDefinition::AdHocMovementClass{5u, 5u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMCRUS"] = hull;
        sim.unitDefinitions["ARMBATS"] = hull;
        auto sub = makeDef(false, true, true, "", 200u);
        sub.movementCollisionInfo = UnitDefinition::AdHocMovementClass{3u, 3u, 255u, 255u, 20u, 255u};
        sim.unitDefinitions["ARMACSUB"] = sub;
        auto plane = makeDef(false, false, true, "LASER", 300u);
        plane.canFly = true;
        sim.unitDefinitions["ARMSEAP"] = plane;
        sim.unitDefinitions["ARMSFIG"] = plane;

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        SECTION("the sub, once two cruisers stand")
        {
            addUnit(sim, "ARMASY", ai, SimVector(0_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(160_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(160_ss, 60_ss, 160_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMACSUB") == 1);
        }

        SECTION("not where an air plant already stands")
        {
            addUnit(sim, "ARMASY", ai, SimVector(0_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(160_ss, 60_ss, 0_ss), script);
            addUnit(sim, "ARMCRUS", ai, SimVector(160_ss, 60_ss, 160_ss), script);
            addUnit(sim, "ARMAP", ai, SimVector(-420_ss, 90_ss, 200_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMACSUB") == 0);
        }

        SECTION("and the platform builds torpedo seaplanes against an empty sky")
        {
            addUnit(sim, "ARMPLAT", ai, SimVector(0_ss, 60_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMSEAP") == 1);
            REQUIRE(countQueueCommands(commands, "ARMSFIG") == 0);
        }
    }

    TEST_CASE("naval: storage and the reactor go under the water", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // The fixture's kbot wades any depth, which makes the whole sea part of
        // the base and every ship a builder standing at home. A real one stops
        // at the shore, and then a ship plans as the outpost builder it is.
        sim.unitDefinitions["ARMCK"].movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 10u};
        auto afloat = makeDef(false, true, true, "", 200u);
        afloat.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 6u, 255u};
        sim.unitDefinitions["ARMCS"] = afloat;
        sim.unitDefinitions["ARMACSUB"] = afloat;
        auto sunk = makeDef(false, false, false, "", 50u);
        sunk.movementCollisionInfo = UnitDefinition::AdHocMovementClass{4u, 4u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMUWMS"] = sunk;
        sim.unitDefinitions["ARMUWES"] = sunk;
        sim.unitDefinitions["ARMUWFUS"] = sunk;
        auto yard = makeDef(false, true, false, "", 200u);
        yard.movementCollisionInfo = UnitDefinition::AdHocMovementClass{8u, 8u, 255u, 255u, 30u, 255u};
        sim.unitDefinitions["ARMASY"] = yard;

        // The commander is kept walking, so the one job a planning pass hands
        // out goes to the builder under test.
        auto commanderId = addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        sim.getUnitState(commanderId).addOrder(MoveOrder(SimVector(-420_ss, 90_ss, 400_ss)));
        addUnit(sim, "ARMSY", ai, SimVector(0_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.navalTechMinMetalIncome = 0;
        sim.getPlayer(ai).metal = Metal(1000.0f);
        sim.getPlayer(ai).maxMetal = Metal(1000.0f);

        auto tree = makeBuildTree();

        SECTION("a construction ship with a full store builds underwater metal storage")
        {
            auto shipId = addUnit(sim, "ARMCS", ai, SimVector(100_ss, 60_ss, 100_ss), script);
            tree.buildableBy["ARMCS"] = {"ARMUWMS", "ARMUWES"};
            AiPlayerController controller(ai, profile, 42u, mapIntel, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            auto builds = ordersFor<BuildOrder>(commands, shipId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMUWMS");
        }

        SECTION("not with storage switched off")
        {
            auto shipId = addUnit(sim, "ARMCS", ai, SimVector(100_ss, 60_ss, 100_ss), script);
            tree.buildableBy["ARMCS"] = {"ARMUWMS", "ARMUWES"};
            profile.targetMetalStorageCount = 0;
            profile.targetEnergyStorageCount = 0;
            AiPlayerController controller(ai, profile, 42u, mapIntel, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            REQUIRE(ordersFor<BuildOrder>(commands, shipId).empty());
        }

        SECTION("the advanced construction sub builds the underwater fusion plant")
        {
            addUnit(sim, "ARMASY", ai, SimVector(0_ss, 60_ss, 300_ss), script);
            auto subId = addUnit(sim, "ARMACSUB", ai, SimVector(100_ss, 60_ss, 100_ss), script);
            tree.buildableBy["ARMACSUB"] = {"ARMUWFUS"};
            AiPlayerController controller(ai, profile, 42u, mapIntel, tree);
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 61, commands);
            auto builds = ordersFor<BuildOrder>(commands, subId);
            REQUIRE(!builds.empty());
            REQUIRE(builds.front().unitType == "ARMUWFUS");
        }
    }

    TEST_CASE("naval: a warship is never sent inland", "[ai]")
    {
        // Without MapIntel::sameWaterBody filtering the target, the land
        // army's plain "anything within engageRadius gets shot at" rule
        // would fire on this enemy: it is the only known contact and well
        // inside the default 450-unit range. With it, a hull afloat and an
        // enemy standing on dry land are never on the same body of water,
        // so nothing here ever asks the destroyer to fire on it or walk to
        // it.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(-90_ss, 0_ss, 0_ss), script);
        auto destroyerId = addUnit(sim, "ARMROY", ai, SimVector(-100_ss, 0_ss, 0_ss), script);
        auto enemyId = addUnit(sim, "ARMPW", human, SimVector(-450_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(std::find(bb.navalCombatUnits.begin(), bb.navalCombatUnits.end(), destroyerId) != bb.navalCombatUnits.end());
        REQUIRE(std::find(bb.combatUnits.begin(), bb.combatUnits.end(), destroyerId) == bb.combatUnits.end());
        REQUIRE(bb.knownEnemies.count(enemyId.value) == 1);

        REQUIRE(ordersFor<AttackOrder>(commands, destroyerId).empty());
        for (const auto& order : ordersFor<MoveOrder>(commands, destroyerId))
        {
            // Never further landward than the shore itself (tile 10, world
            // x -352): a ship allowed to walk to bb.rallyPoint or
            // bb.attackTarget -- both on the dry strip -- would fail this.
            REQUIRE(order.destination.x.value > -352.0f);
        }
    }

    TEST_CASE("naval: a hull whose shots are not landing moves round to deep water with a clear run in", "[ai]")
    {
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        // Shots that arrive and do nothing, which is what a torpedo into a
        // shelf amounts to: the target's hit points never move.
        sim.weaponDefinitions["LASER"].damage["DEFAULT"] = 0u;

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 300_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(200_ss, 0_ss, 300_ss), script);
        auto destroyerId = addUnit(sim, "ARMROY", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // At the foot of the shelf, with the shallows and the shore right behind it:
        // half the bearings round it are no place for a hull.
        auto targetId = addUnit(sim, "ARMSOLAR", human, SimVector(-270_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.commanderDangerRadius = 0_ss;

        SECTION("after navalStalledAttackSeconds of nothing")
        {
            profile.navalStalledAttackSeconds = 2;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 30, commands);
            REQUIRE(ordersFor<MoveOrder>(commands, destroyerId).empty());

            runTicks(sim, controller, 3 * static_cast<int>(SimTicksPerSecond), commands);
            auto moves = ordersFor<MoveOrder>(commands, destroyerId);
            REQUIRE(!moves.empty());
            const auto& spot = moves.front().destination;
            // Deep water, not the shelf (tiles 10-14, world x below -288) and not the shore.
            REQUIRE(spot.x.value > -288.0f);
            auto ground = sim.terrain.tryGetHeightAt(spot.x, spot.z);
            REQUIRE(ground.has_value());
            REQUIRE(ground->value <= sim.terrain.getSeaLevel().value - 25.0f);
            // And within a torpedo's reach of the target.
            const auto& target = sim.getUnitState(targetId);
            auto dx = spot.x.value - target.position.x.value;
            auto dz = spot.z.value - target.position.z.value;
            REQUIRE((dx * dx) + (dz * dz) <= 345.0f * 345.0f);
        }

        SECTION("switched off, it fires on from where it is")
        {
            profile.navalStalledAttackSeconds = 0;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4 * static_cast<int>(SimTicksPerSecond), commands);
            REQUIRE(ordersFor<MoveOrder>(commands, destroyerId).empty());
        }
    }

    TEST_CASE("a warship goes looking for an enemy the AI has not found")
    {
        // The counterpart of the section above. That one pins that a hull is
        // never sent inland; this one pins that a hull is sent out at all.
        //
        // Nothing that walks can find an enemy across water, so on an island
        // map knownEnemies stays empty for the whole game -- and the Attack
        // phase wants (enemyBasePosition || !knownEnemies.empty()) as well as
        // the army size, so it never fires and every army ferry stays
        // switched off with it. Measured on Hundred Isles before this: a side
        // holding a scout ship, three destroyers and a transport never saw
        // the enemy once in nine hundred seconds.
        //
        // scoutCount is left at its default ON PURPOSE. The naval sections
        // above set it to zero, and ScoutManager::update returns immediately
        // when it is -- before any scout is chosen -- so a test that copied
        // that line would pass without executing one line of what it claims
        // to pin.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // No enemy unit anywhere: knownEnemies stays empty, which is the
        // condition the borrow is gated on.
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(-90_ss, 0_ss, 0_ss), script);
        auto destroyerId = addUnit(sim, "ARMROY", ai, SimVector(-100_ss, 0_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.tacticalTickInterval = 1;
        profile.scoutTickInterval = 1;

        SECTION("the hull is borrowed, and sent somewhere it can actually float")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.knownEnemies.empty());
            REQUIRE(bb.navalScoutUnitId);
            REQUIRE(*bb.navalScoutUnitId == destroyerId);

            auto moves = ordersFor<MoveOrder>(commands, destroyerId);
            REQUIRE(!moves.empty());
            for (const auto& order : moves)
            {
                // East of the shore at world x -352, so on water. A scout
                // target chosen off the ground layer would sit on the dry
                // strip and the ship would never arrive -- the same
                // wrong-mover mistake the commander's own layer exists for.
                REQUIRE(order.destination.x.value > -352.0f);
            }
        }

        SECTION("navalScouting=false leaves the fleet alone")
        {
            profile.navalScouting = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);

            // No borrow. Deliberately NOT asserting the destroyer receives no
            // move at all: with the knob off it is an ordinary hull again and
            // updateNavy is free to station it, which is the point of the
            // switch.
            REQUIRE_FALSE(controller.getBlackboard().navalScoutUnitId);
        }
    }

    TEST_CASE("a sea transport finds the landing on the far shore", "[ai]")
    {
        // The army ferry is the only caller of the landing search, and on
        // this map the walk-back from the target runs west, through negative
        // world x. That is ordinary map space -- the world is centred, so it
        // spans -512..512 here -- but the search used to test its candidates
        // against 0..width and read the whole western half as off the map.
        // The walk-back gave up at its first step, and the fallback's water
        // probes were skipped for the same reason, so the ferry was refused
        // outright: "army ferry blocked, no landing near ... (sea)".
        auto script = makeEmptyCobScript();
        auto terrain = makeTwoShoresTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // Nothing that walks may cross 60-deep water. makeDef leaves
        // maxWaterDepth at 255, which would wade the whole ocean and leave
        // the enemy shore perfectly reachable -- no ferry would ever be
        // wanted and this test would pin nothing.
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }
        // The side's sea transport at its shipped shape -- 6x6,
        // MinWaterDepth=12, twenty slots, transport size 3 (AiSideUnits.h).
        // Defined here rather than in defineWorld deliberately: resolving
        // seaTransport promotes it ahead of the destroyer as the mover the
        // naval layer is flooded for, which would quietly relabel the map
        // under the two naval cases above.
        auto tship = makeDef(false, false, true, "", 200u);
        tship.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 12u, 255u};
        tship.transportCapacity = 20;
        tship.transportSize = 3;
        tship.buildCostMetal = Metal(919.0f);
        sim.unitDefinitions["ARMTSHIP"] = tship;

        addUnit(sim, "ARMCOM", ai, SimVector(400_ss, 90_ss, 0_ss), script);
        // Both hulls sit where their whole FOOTPRINT is afloat. Components
        // are labelled by a footprint top-left tile, so an 8x8 shipyard at
        // tile 50 spans 50-57 and runs onto the island at 54; anchoring the
        // naval layer there homed it on a single tile and left every real
        // water tile unreachable. Water is tiles 10-53, so the shipyard
        // starts at 44 (world 192) and the transport at 46 (world 224).
        addUnit(sim, "ARMSY", ai, SimVector(192_ss, 0_ss, 0_ss), script);
        auto transportId = addUnit(sim, "ARMTSHIP", ai, SimVector(224_ss, 0_ss, 0_ss), script);
        for (auto z : {0_ss, 24_ss, 48_ss, 72_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(400_ss, 90_ss, z), script);
        }
        auto enemyId = addUnit(sim, "ARMPW", human, SimVector(-400_ss, 90_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.attackArmySize = 2;
        // Keep the phase in Attack for the length of the test: with waves on
        // it flips back to Boom the moment the wave is spent, and the ferry
        // is only considered in Attack.
        profile.attackInWaves = false;
        // Never fall back to Boom. EconomyManager drops ferry passengers
        // from combatUnits, so the moment the army is aboard armySize is 0
        // and the phase would leave Attack -- taking the ferry branch with
        // it -- before the assertions below ever run. The drop is correct
        // behaviour; it just must not happen inside the measurement window.
        profile.retreatArmySize = 0;
        // The enemy is far across the water, and defendRadius is the only gate
        // on enemiesNearBase (PerceptionManager) -- which puts the AI in
        // Defend ahead of the whole phase switch, where the ferry is
        // never considered at all.
        profile.defendRadius = 200_ss;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 40, commands);

        const auto& bb = controller.getBlackboard();
        // Guards first, so a refusal for some unrelated reason cannot pass
        // itself off as the behaviour under test. navalLandingNear returns
        // nothing immediately when the naval layer was never built, and the
        // ferry is not considered at all outside the Attack phase.
        REQUIRE(controller.getReachabilityMap().isNavalValid());
        REQUIRE(bb.knownEnemies.count(enemyId.value) == 1);
        REQUIRE(bb.phase == GamePhase::Attack);
        REQUIRE(bb.enemyAcrossWater);
        REQUIRE(std::find(bb.transports.begin(), bb.transports.end(), transportId) != bb.transports.end());

        auto unloads = ordersFor<UnloadOrder>(commands, transportId);
        REQUIRE(!unloads.empty());
        // Set down on the enemy's own shore, west of the water's edge.
        REQUIRE(unloads.front().destination.x < -352_ss);
        REQUIRE(!ordersFor<LoadOrder>(commands, transportId).empty());
        REQUIRE(controller.getTransportManager().getFerries().count(transportId.value) == 1);
    }

    namespace
    {
        /**
         * A far shore wide enough for the landing search to have a CHOICE.
         *
         * makeChannelTerrain and makeTwoShoresTerrain both leave a far bank
         * only a step or two across, so the walk-back finds one acceptable
         * point and picking the quietest is the same as picking the first --
         * which pins nothing. Here the west shore is heightmap x in [0, 10)
         * and the east shore is x >= 38, so at 16 world units a tile the east
         * bank runs from world 96 to 512: four candidates at the 48-unit step
         * the search uses.
         */
        MapTerrain makeWideFarShoreTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 0; x < 10; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
                for (int x = 38; x < 64; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(90));
                }
            }
            return MapTerrain(std::move(heights), 60_ss);
        }
    }

    TEST_CASE("an air ferry puts the army down where less can shoot at it", "[ai]")
    {
        // Reported from play: "transport ship dumped the units at the most
        // populated part of the enemies landmass instead of a bit away from
        // the base, so they all instantly died and the ship was destroyed
        // wasting resources."
        //
        // The search returned the FIRST dry, walkable point on the way back
        // from the target, which on a defended shore is the beach in front of
        // the defences, and it never asked what was standing there.
        auto script = makeEmptyCobScript();
        auto terrain = makeWideFarShoreTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        // Base on the west shore, the objective at the far east edge.
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-440_ss, 90_ss, 40_ss), script);
        auto atlasId = addUnit(sim, "ARMATLAS", ai, SimVector(-400_ss, 150_ss, 0_ss), script);
        for (auto z : {0_ss, 24_ss, 48_ss, 72_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(-420_ss, 90_ss, z), script);
        }

        // The objective, and a picket standing exactly where the walk-back's
        // first step lands: target 450 less 4 * 48 is 258.
        addUnit(sim, "ARMPW", human, SimVector(450_ss, 90_ss, 0_ss), script);
        for (auto z : {-40_ss, 0_ss, 40_ss})
        {
            addUnit(sim, "ARMPW", human, SimVector(258_ss, 90_ss, z), script);
        }

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.attackArmySize = 2;
        profile.attackInWaves = false;
        // See the test above for both of these.
        profile.retreatArmySize = 0;
        profile.defendRadius = 200_ss;
        // ThreatMap::antiGroundInRadius is a flat box sum over cells, so at
        // the shipped 300 a picket 144 units away counts exactly as much as
        // one underfoot and every candidate scores the same. 60 is two cells
        // either side, which separates the four candidates this map offers.
        profile.ferryLandingThreatRadius = 60.0f;

        SECTION("the drop moves off the picket")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);

            REQUIRE(controller.getBlackboard().phase == GamePhase::Attack);
            auto unloads = ordersFor<UnloadOrder>(commands, atlasId);
            REQUIRE(!unloads.empty());
            // Past the picket at 258 and its 64-unit skirt, and still on the
            // far shore rather than back across the water at 96.
            auto drop = unloads.front().destination;
            // On the far shore rather than back across the water at 96, and
            // out of reach of everything armed: the three pickets at 258 and
            // the objective at 450.
            //
            // It is not pinned to a band of x, and the reason is worth
            // writing down. The threat field already accounts for weapon
            // reach, so a picket line has a wide skirt; on this map the
            // pickets sit across the whole approach, and there is no quiet
            // point SHORT of them at all. The search therefore goes round,
            // which is the right answer and is what the fan exists to make
            // possible. What is pinned is that it ends up somewhere nothing
            // can shoot it, on the shore it was sent to, and inside the
            // search radius rather than off in a corner of the map.
            REQUIRE(drop.x > 96_ss);
            for (auto z : {-40_ss, 0_ss, 40_ss})
            {
                REQUIRE(drop.distanceSquared(SimVector(258_ss, drop.y, z)) > (200_ss * 200_ss));
            }
            REQUIRE(drop.distanceSquared(SimVector(450_ss, drop.y, 0_ss)) > (200_ss * 200_ss));
            auto reach = SimScalar(static_cast<float>(profile.ferryLandingSearchSteps) * 48.0f);
            REQUIRE(drop.distanceSquared(SimVector(450_ss, drop.y, 0_ss)) <= (reach * reach));
        }

        SECTION("ferryLandingAvoidsThreat=false takes the first point as it always did")
        {
            profile.ferryLandingAvoidsThreat = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);

            auto unloads = ordersFor<UnloadOrder>(commands, atlasId);
            REQUIRE(!unloads.empty());
            REQUIRE(unloads.front().destination.x > 230_ss);
        }
    }

    TEST_CASE("an air ferry sets the army down short of the target", "[ai]")
    {
        // The other half of the same fault, on the other side of the
        // canFly branch. Here the walk-back has somewhere to stop -- the
        // near bank of the channel -- and the point of walking back at all
        // is to land the army short of the enemy rather than on top of it.
        // Reading negative world x as off the map broke the walk at its
        // first step and left the unguarded fallback to set the army down
        // on the target itself.
        auto script = makeEmptyCobScript();
        auto terrain = makeChannelTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // The channel is 30 deep; a kbot stopping at 20 cannot wade it.
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        // Base on the east bank, enemy on the west bank, channel between at
        // world x -64..64.
        addUnit(sim, "ARMCOM", ai, SimVector(300_ss, 60_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(280_ss, 60_ss, 40_ss), script);
        auto atlasId = addUnit(sim, "ARMATLAS", ai, SimVector(260_ss, 120_ss, 0_ss), script);
        for (auto z : {0_ss, 24_ss, 48_ss, 72_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(300_ss, 60_ss, z), script);
        }
        auto enemyId = addUnit(sim, "ARMPW", human, SimVector(-300_ss, 60_ss, 0_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.attackArmySize = 2;
        profile.attackInWaves = false;
        // Never fall back to Boom. EconomyManager drops ferry passengers
        // from combatUnits, so the moment the army is aboard armySize is 0
        // and the phase would leave Attack -- taking the ferry branch with
        // it -- before the assertions below ever run. The drop is correct
        // behaviour; it just must not happen inside the measurement window.
        profile.retreatArmySize = 0;
        // The enemy is far across the water, and defendRadius is the only gate
        // on enemiesNearBase (PerceptionManager) -- which puts the AI in
        // Defend ahead of the whole phase switch, where the ferry is
        // never considered at all.
        profile.defendRadius = 200_ss;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 40, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.knownEnemies.count(enemyId.value) == 1);
        REQUIRE(bb.phase == GamePhase::Attack);
        REQUIRE(bb.enemyAcrossWater);
        REQUIRE(std::find(bb.transports.begin(), bb.transports.end(), atlasId) != bb.transports.end());

        auto unloads = ordersFor<UnloadOrder>(commands, atlasId);
        REQUIRE(!unloads.empty());
        // Short of the enemy, on the west bank but well back from the
        // target at -300: the first step of the walk-back lands near -108.
        REQUIRE(unloads.front().destination.x > -200_ss);
        REQUIRE(unloads.front().destination.x < -64_ss);
    }

    TEST_CASE("naval: the want for an army ferry does not flap with what can be seen", "[ai]")
    {
        // bb.wantsTransport is what the shipyard and the air plant read before
        // building a transport, and it was derived from bb.attackTarget --
        // which ArmyManager resets and rebuilds every tactical pass, and
        // leaves unset outside the Attack phase and whenever nothing is
        // currently remembered. Measured on one 900s Coast To Coast game the
        // want tracked known_enemies exactly and was off for about two thirds
        // of it, against a 223-second build time for the hull it was asking
        // for. A want that flaps faster than the thing it wants can be built
        // never gets it built.
        //
        // The same two shores as the factory-ordering case above, and for the
        // same reason: the smallest map on which "our army cannot walk to
        // anybody" is true. There is no enemy unit anywhere in this fixture,
        // which is the whole point -- nothing is in sight, and the map still
        // says the army will need carrying.
        auto script = makeEmptyCobScript();
        auto westStart = SimVector(-420_ss, 90_ss, 0_ss);
        auto eastStart = SimVector(420_ss, 90_ss, 0_ss);

        GameSimulation sim(makeTwoShoresTerrain(), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        // As above: makeDef leaves maxWaterDepth at 255, which wades the
        // whole ocean and would make the far shore reachable.
        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        addUnit(sim, "ARMCOM", ai, westStart, script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;

        auto mapIntel = analyseMap(makeTwoShoresTerrain(), {westStart, eastStart});

        SECTION("with nothing in sight, the map still says the army needs carrying")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            // The premise: nothing is known and there is therefore no target.
            REQUIRE(bb.knownEnemies.empty());
            REQUIRE_FALSE(bb.attackTarget.has_value());
            // So the old question answers no, exactly as it did before.
            REQUIRE_FALSE(bb.enemyAcrossWater);
            // And the new one answers yes, off the map.
            REQUIRE(bb.landRouteToEnemy.has_value());
            REQUIRE_FALSE(*bb.landRouteToEnemy);
            REQUIRE(bb.armyNeedsFerry);
            REQUIRE(bb.wantsTransport);
        }

        SECTION("once it has been established, losing sight of everybody does not unsay it")
        {
            // The case the map fallback above cannot cover, and the one that
            // actually matters in a game: landRouteToEnemy is "can the army
            // walk to ANY other declared start position", so on a map that
            // deals several seats it is true even when the enemy we drew is
            // across water. Measured on Coast To Coast it is true from tick 1,
            // and a fallback resting on it did precisely nothing there -- ten
            // seeds came out byte-identical to the control. What makes this
            // work is the latch.
            //
            // Three declared starts, two of them on our own shore: the west
            // pair is walkable between, so landRouteToEnemy is true, and the
            // enemy is on the east one.
            auto westSecond = SimVector(-420_ss, 90_ss, 300_ss);
            auto threeStarts = analyseMap(makeTwoShoresTerrain(), {westStart, westSecond, eastStart});

            auto human = sim.players.empty() ? PlayerId(0) : PlayerId(0);
            auto enemyId = addUnit(sim, "ARMPW", human, eastStart, script);
            // The opening is over once a factory stands, and the phase has to
            // get past Opening before it can reach Attack at all.
            addUnit(sim, "ARMLAB", ai, SimVector(-470_ss, 90_ss, -120_ss), script);
            // Enough of an army to reach the Attack phase, which is what puts
            // an attack target on the blackboard at all.
            for (int i = 0; i < 4; ++i)
            {
                addUnit(sim, "ARMPW", ai, SimVector(SimScalar(-460.0f + static_cast<float>(i) * 20.0f), 90_ss, 100_ss), script);
            }

            auto p = profile;
            p.cheatModeOmniscient = true;
            p.tacticalTickInterval = 1;
            p.attackArmySize = 3;
            // The far shore is 840 units off and defendRadius is 900, so
            // without this the enemy across the water reads as an enemy at
            // the door and the phase comes out Defend rather than Attack.
            p.defendRadius = 200_ss;

            AiPlayerController controller(ai, p, 42u, threeStarts, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);

            const auto& bb = controller.getBlackboard();
            // The premise: the map says a land route exists, and it is only
            // the enemy we actually drew that is across water.
            REQUIRE(bb.landRouteToEnemy.has_value());
            REQUIRE(*bb.landRouteToEnemy);
            REQUIRE(bb.phase == GamePhase::Attack);
            REQUIRE(bb.enemyAcrossWater);
            REQUIRE(bb.armyNeedsFerry);

            // Now take the enemy away, which is what a unit walking out of
            // sight amounts to as far as the blackboard is concerned.
            sim.getUnitState(enemyId).markAsDeadNoCorpse();
            std::vector<PlayerCommand> after;
            runTicks(sim, controller, 40, after);

            const auto& bb2 = controller.getBlackboard();
            REQUIRE(bb2.knownEnemies.empty());
            REQUIRE_FALSE(bb2.attackTarget.has_value());
            // The live question goes quiet, as it always did...
            REQUIRE_FALSE(bb2.enemyAcrossWater);
            // ...and the answer the yard reads does not.
            REQUIRE(bb2.armyNeedsFerry);
            REQUIRE(bb2.wantsTransport);
        }

        SECTION("armyFerryWantFromMap=false restores the old behaviour exactly")
        {
            profile.armyFerryWantFromMap = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE_FALSE(bb.armyNeedsFerry);
            // hasExpansionSite is the other half of wantsTransport and is
            // asked separately, so this says the ARMY half is off rather than
            // that the whole want is.
            REQUIRE(bb.armyNeedsFerry == bb.enemyAcrossWater);
        }

        SECTION("on a map the army can walk across, nothing changes")
        {
            // One shore, both starts on it: landRouteToEnemy is true and the
            // fallback must not fire. Without this the knob would want a
            // transport on every map there is.
            GameSimulation land(makeFlatTerrain(64, 64), 0u, 0, 0);
            addPlayer(land, "human", GamePlayerType::Human, "ARM");
            auto landAi = addPlayer(land, "ai", GamePlayerType::Computer, "ARM");
            defineWorld(land);
            auto a = SimVector(-420_ss, 0_ss, 0_ss);
            auto b = SimVector(420_ss, 0_ss, 0_ss);
            addUnit(land, "ARMCOM", landAi, a, script);

            auto landIntel = analyseMap(makeFlatTerrain(64, 64), {a, b});
            AiPlayerController controller(landAi, profile, 42u, landIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(land, controller, 31, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.landRouteToEnemy.has_value());
            REQUIRE(*bb.landRouteToEnemy);
            REQUIRE_FALSE(bb.armyNeedsFerry);
        }
    }

    TEST_CASE("naval: an idle hull stands off its own yard rather than on it", "[ai]")
    {
        // Reported from play, twice over: "boats were blocking the factory
        // after being made", and "core shipyard stopped building as it got
        // blocked by a scout ship".
        //
        // A finished hull is handed a BuggerOffOrder, which clears the pad by
        // one footprint and no further. updateNavy then measured both of its
        // "come back" tests against navalHome -- which IS the shipyard -- so a
        // hull at the doors was already home and was never told anything
        // again. It stood there until the fleet sailed, and a friendly hull on
        // a spawn point costs the yard ten failed tries and its queue entry
        // (GameSimulation::retryBlockedSite).
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 0_ss), script);
        auto yard = SimVector(-90_ss, 0_ss, 0_ss);
        addUnit(sim, "ARMSY", ai, yard, script);
        // One hull, just off the doors, with nothing to do. One and not three
        // so that the fleet never gathers and the sortie never fires: what is
        // under test is the hull that is NOT going anywhere.
        auto hull = addUnit(sim, "ARMROY", ai, SimVector(-90_ss, 0_ss, 40_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        // The scout borrows a hull, and a borrowed hull is not updateNavy's
        // to command -- it skips it by name.
        profile.navalScouting = false;

        SECTION("it is sent out to the station, clear of the pad")
        {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            auto moves = ordersFor<MoveOrder>(commands, hull);
            REQUIRE(!moves.empty());
            // Far enough out that the next hull off the slipway has room.
            // Measured from the yard, which is what the clearance is about.
            auto out = moves.back().destination;
            REQUIRE(out.distanceSquared(yard) >= (profile.navalRallyDistance * profile.navalRallyDistance) * 0.9_ssf);
            // And still inside the radius the gather count uses, or a hull
            // standing by would stop counting towards the fleet that sails.
            auto gatherRadius = profile.rallyDistance * 2_ss;
            REQUIRE(out.distanceSquared(yard) <= gatherRadius * gatherRadius);
        }

        SECTION("and it is not sent out again once it is there")
        {
            // The station is navalRallyDistance out and the "on the yard"
            // test is half of that, so a hull standing by reads as neither
            // on the pad nor drifted. Without that gap it would be ordered
            // to the same point on every tactical pass for the whole game.
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> first;
            runTicks(sim, controller, 20, first);
            auto moves = ordersFor<MoveOrder>(first, hull);
            REQUIRE(!moves.empty());

            // Put it where it was told to go, with nothing left to do.
            sim.getUnitState(hull).position = moves.back().destination;
            sim.getUnitState(hull).orders.clear();

            std::vector<PlayerCommand> second;
            runTicks(sim, controller, 20, second);
            REQUIRE(ordersFor<MoveOrder>(second, hull).empty());
        }

        SECTION("navalRallyDistance=0 restores the old behaviour exactly")
        {
            profile.navalRallyDistance = 0_ss;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            REQUIRE(ordersFor<MoveOrder>(commands, hull).empty());
        }
    }

    TEST_CASE("a fleet can call the attack, but only when asked to", "[ai]")
    {
        // The phase machine counted armySize, which counts combatUnits, and
        // hulls are deliberately not in those -- so a side whose whole
        // strength was afloat stayed in Boom for ever. The phase gates the
        // army ferry, so on an island map that alone stopped the ferry.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 0_ss), script);
        // A shipyard, so the opening is over and the phase reaches Boom.
        addUnit(sim, "ARMSY", ai, SimVector(-90_ss, 0_ss, 0_ss), script);
        // Three hulls and not one land unit that fights.
        for (auto z : {0_ss, 40_ss, 80_ss})
        {
            addUnit(sim, "ARMROY", ai, SimVector(-100_ss, 0_ss, z), script);
        }
        // Something to attack, so the target half of the gate is satisfied
        // either way and only the strength half is under test. Far enough
        // out not to count as an enemy at the door.
        addUnit(sim, "ARMPW", human, SimVector(-450_ss, 0_ss, 300_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.defendRadius = 200_ss;

        SECTION("off by default, so a fleet alone does not call one")
        {
            profile.navalScouting = false;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.navalCombatUnits.size() == 3);
            REQUIRE(bb.armySize == 0);
            REQUIRE(bb.phase != GamePhase::Attack);
        }

        SECTION("with the knob set, the fleet is enough on its own")
        {
            profile.navalScouting = false;
            profile.attackNavalSize = 3;
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 20, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.armySize == 0);
            REQUIRE(bb.phase == GamePhase::Attack);
        }
    }

    TEST_CASE("naval: a lone hull with navalAttackFleetSize=1 is not recalled the pass after it sails", "[ai]")
    {
        // The fleet size is a threshold on both sides: sail when a whole
        // fleet has gathered, recall when the survivors fall below half of
        // one. The recall threshold had a floor of two of its own, which at
        // navalAttackFleetSize=1 put recall (2) above sail (1). The state
        // then flipped every tactical pass -- one 900-second game logged 238
        // "the fleet sails" and 237 "the fleet is recalled" -- and the same
        // floor made the home group want a second hull, so a lone hull was
        // never actually sailed either.
        auto script = makeEmptyCobScript();
        auto terrain = makeWaterMapTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMSY", ai, SimVector(-90_ss, 0_ss, 0_ss), script);
        auto hull = addUnit(sim, "ARMROY", ai, SimVector(-100_ss, 0_ss, 20_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        // ScoutManager borrows a hull, and a borrowed hull is skipped by
        // updateNavy, which would leave the fleet of one with no ship in it.
        profile.navalScouting = false;
        // The knob this is about: a fleet of one.
        profile.navalAttackFleetSize = 1;

        AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
        const auto& bb = controller.getBlackboard();
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 5, commands);

        REQUIRE(bb.navalCombatUnits.size() == 1);
        REQUIRE(bb.navalSortieActive);

        // Passes enough that the old floor of two would have recalled it on
        // the even ones, and driven one pass at a time so the assertion
        // reads the flag the same pass updateNavy set it.
        for (int i = 0; i < 25; ++i)
        {
            std::vector<PlayerCommand> pass;
            runTicks(sim, controller, 1, pass);
            REQUIRE(bb.navalSortieActive);
        }
        REQUIRE(std::find(bb.navalCombatUnits.begin(), bb.navalCombatUnits.end(), hull) != bb.navalCombatUnits.end());
    }
    TEST_CASE("an air ferry keeps its drop out from under a flak battery", "[ai]")
    {
        // Issue #228. The landing search scores a drop by anti-ground threat,
        // which is the right question for a hull and for the cargo, and the
        // wrong one for the aircraft carrying it: an Atlas is killed by
        // anti-air. A missile battery whose damage is all against aircraft
        // -- no DEFAULT damage, as an anti-air weapon's often is -- adds
        // nothing to anti-ground, and a drop under it read as perfectly
        // quiet. The battery here stands on the point the search picks with
        // nothing about.
        // Reported from play: "transport ship dumped the units at the most
        // populated part of the enemies landmass instead of a bit away from
        // the base, so they all instantly died and the ship was destroyed
        // wasting resources."
        //
        // The search returned the FIRST dry, walkable point on the way back
        // from the target, which on a defended shore is the beach in front of
        // the defences, and it never asked what was standing there.
        auto script = makeEmptyCobScript();
        auto terrain = makeWideFarShoreTerrain();
        auto mapIntel = analyseMap(terrain, {});

        GameSimulation sim(std::move(terrain), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        for (auto* type : {"ARMCOM", "ARMCK", "ARMPW"})
        {
            sim.unitDefinitions.at(type).movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 20u};
        }

        // Base on the west shore, the objective at the far east edge.
        addUnit(sim, "ARMCOM", ai, SimVector(-420_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(-440_ss, 90_ss, 40_ss), script);
        auto atlasId = addUnit(sim, "ARMATLAS", ai, SimVector(-400_ss, 150_ss, 0_ss), script);
        for (auto z : {0_ss, 24_ss, 48_ss, 72_ss})
        {
            addUnit(sim, "ARMPW", ai, SimVector(-420_ss, 90_ss, z), script);
        }

        // The objective, and a picket standing exactly where the walk-back's
        // first step lands: target 450 less 4 * 48 is 258.
        addUnit(sim, "ARMPW", human, SimVector(450_ss, 90_ss, 0_ss), script);
        addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 90_ss, 60_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.cheatModeOmniscient = true;
        profile.tacticalTickInterval = 1;
        profile.attackArmySize = 2;
        profile.attackInWaves = false;
        // See the test above for both of these.
        profile.retreatArmySize = 0;
        profile.defendRadius = 200_ss;
        // ThreatMap::antiGroundInRadius is a flat box sum over cells, so at
        // the shipped 300 a picket 144 units away counts exactly as much as
        // one underfoot and every candidate scores the same. 60 is two cells
        // either side, which separates the four candidates this map offers.
        profile.ferryLandingThreatRadius = 60.0f;

        WeaponDefinition flak{};
        flak.maxRange = 150_ss;
        flak.reloadTime = 1_ss;
        flak.burst = 1;
        flak.toAirWeapon = true;
        flak.damage["VTOL"] = 100u;
        sim.weaponDefinitions["FLAK"] = flak;
        // Worth next to nothing, so the objective stays the objective and
        // the search walks back from the same place with or without it.
        auto battery_def = makeDef(false, false, false, "FLAK", 200u);
        battery_def.buildCostMetal = Metal(1.0f);
        sim.unitDefinitions["ARMFLAK"] = battery_def;
        const SimVector battery(250_ss, 90_ss, 240_ss);
        addUnit(sim, "ARMFLAK", human, battery, script);

        auto drop = [&]() {
            AiPlayerController controller(ai, profile, 42u, mapIntel, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 40, commands);
            auto unloads = ordersFor<UnloadOrder>(commands, atlasId);
            REQUIRE(!unloads.empty());
            return unloads.front().destination;
        };
        auto flatDistanceSquared = [](const SimVector& a, const SimVector& b) {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return (dx * dx) + (dz * dz);
        };

        SECTION("the drop moves out of the battery's reach")
        {
            REQUIRE(flatDistanceSquared(drop(), battery) > 150_ss * 150_ss);
        }

        SECTION("scored on anti-ground alone, it lands under the battery")
        {
            profile.ferryLandingAirLiftFearsAntiAir = false;
            REQUIRE(flatDistanceSquared(drop(), battery) <= 150_ss * 150_ss);
        }
    }
}
