#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ai_test_util.h>

/**
 * What the AI knows and when: scouting, losing a building, noticing
 * aircraft, and the knobs that scale how hard it reacts.
 *
 * The fixtures these share are in ai_test_util.h.
 */
namespace rwe
{
    TEST_CASE("fighters are built to match the raid, and go for the bombers first", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        auto bomber = makeDef(false, false, true, "LASER", 200u);
        bomber.canFly = true;
        sim.unitDefinitions["ARMTHUND"] = bomber;
        auto fighter = makeDef(false, false, true, "LASER", 200u);
        fighter.canFly = true;
        sim.unitDefinitions["ARMFIG"] = fighter;

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMAP", ai, SimVector(-200_ss, 0_ss, 200_ss), script);
        auto fighterId = addUnit(sim, "ARMFIG", ai, SimVector(-100_ss, 60_ss, 100_ss), script);
        addUnit(sim, "ARMFIG", ai, SimVector(-140_ss, 60_ss, 100_ss), script);

        auto profile = makeDefaultStandardProfile();
        profile.scoutCount = 0;
        profile.targetScoutPlaneCount = 0;
        profile.targetAirConstructorCount = 0;
        profile.targetBomberCount = 0;

        SECTION("the standing pair is enough for a scout")
        {
            addUnit(sim, "ARMPEEP", human, SimVector(150_ss, 60_ss, 0_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMFIG") == 0);
        }

        SECTION("four bombers want four fighters")
        {
            for (int i = 0; i < 4; ++i)
            {
                addUnit(sim, "ARMTHUND", human, SimVector(SimScalar(100.0f + i * 30.0f), 60_ss, 60_ss), script);
            }
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE(controller.getBlackboard().enemyArmedAirPeak == 4);
            REQUIRE(countQueueCommands(commands, "ARMFIG") == 1);
        }

        SECTION("a fighter takes the bomber further off before the scout beside it")
        {
            addUnit(sim, "ARMPEEP", human, SimVector(-80_ss, 60_ss, 100_ss), script);
            auto bomberId = addUnit(sim, "ARMTHUND", human, SimVector(200_ss, 60_ss, 60_ss), script);
            AiPlayerController controller(ai, profile, 42u, MapIntel{}, makeBuildTree());
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            auto attacks = ordersFor<AttackOrder>(commands, fighterId);
            REQUIRE(!attacks.empty());
            auto target = std::get_if<UnitId>(&attacks.front().target);
            REQUIRE(target != nullptr);
            REQUIRE(*target == bomberId);
        }
    }

    TEST_CASE("with three enemies the AI fights one of them", "[ai]")
    {
        // "The AI needs to know where to concentrate its efforts."
        //
        // enemyBasePosition was the centroid of every enemy building known,
        // which is the right answer to "where does the enemy live" only when
        // there is one enemy. With three on a four-corner map it is very
        // nearly the centre of the map, where none of them lives: the wave
        // walks into the open ground between three opponents and fights all
        // of them, attackBaseRadius finds nothing within 900 of a point
        // nobody built on and falls back to ranging the whole map, and the
        // commander that gets hunted is whichever has the lowest unit id.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(256, 256), 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        auto near = addPlayer(sim, "near", GamePlayerType::Computer, "ARM");
        auto far = addPlayer(sim, "far", GamePlayerType::Computer, "ARM");
        auto further = addPlayer(sim, "further", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto nearBase = SimVector(600_ss, 0_ss, 0_ss);
        auto farBase = SimVector(0_ss, 0_ss, -1800_ss);
        auto furtherBase = SimVector(-1800_ss, 0_ss, 1800_ss);

        // The centroid of all three is nowhere near any of them, which is
        // the whole complaint. Worked out here so the test says what it is
        // discriminating against rather than only that it discriminates.
        auto allThree = SimVector(
            (nearBase.x + farBase.x + furtherBase.x) / 3_ss,
            0_ss,
            (nearBase.z + farBase.z + furtherBase.z) / 3_ss);
        REQUIRE(allThree.distance(nearBase) > 900_ss);
        REQUIRE(allThree.distance(farBase) > 900_ss);
        REQUIRE(allThree.distance(furtherBase) > 900_ss);

        auto addBase = [&](PlayerId owner, const SimVector& where) {
            addUnit(sim, "ARMSOLAR", owner, where, script);
            addUnit(sim, "ARMCOM", owner, where + SimVector(60_ss, 0_ss, 0_ss), script);
        };

        SECTION("the nearest of them, and its base is what the wave is given")
        {
            addBase(near, nearBase);
            addBase(far, farBase);
            addBase(further, furtherBase);

            // Omniscient so that all three are seen at once: what is under
            // test is the choice between them, not the finding of them.
            AiPlayerController controller(ai, makeDefaultBrutalProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.focusEnemy.has_value());
            CHECK(*bb.focusEnemy == near);
            REQUIRE(bb.enemyBasePosition.has_value());
            CHECK(bb.enemyBasePosition->distance(nearBase) < 200_ss);

            // And the commander it hunts is that enemy's, not another's.
            REQUIRE(bb.enemyCommanderPosition.has_value());
            CHECK(bb.enemyCommanderPosition->distance(nearBase) < 200_ss);
        }

        SECTION("switched off, it averages all three into a place nobody lives")
        {
            addBase(near, nearBase);
            addBase(far, farBase);
            addBase(further, furtherBase);

            auto profile = makeDefaultBrutalProfile();
            profile.focusOneEnemy = false;
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);

            const auto& bb = controller.getBlackboard();
            CHECK_FALSE(bb.focusEnemy.has_value());
            REQUIRE(bb.enemyBasePosition.has_value());
            // Not any of their bases: the fault, pinned.
            CHECK(bb.enemyBasePosition->distance(nearBase) > 900_ss);
            CHECK(bb.enemyBasePosition->distance(farBase) > 900_ss);
            CHECK(bb.enemyBasePosition->distance(furtherBase) > 900_ss);
        }

        SECTION("a nearer one found later takes the war over")
        {
            // Only the far one to start with, so the war begins against it.
            addBase(far, farBase);
            AiPlayerController controller(ai, makeDefaultBrutalProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);
            REQUIRE(controller.getBlackboard().focusEnemy == far);

            // Then a much nearer one turns up: 600 against 1800, which beats
            // the margin comfortably.
            addBase(near, nearBase);
            runTicks(sim, controller, 4, commands);
            CHECK(controller.getBlackboard().focusEnemy == near);
        }

        SECTION("but one barely nearer does not, or no wave ever arrives")
        {
            addBase(far, farBase);
            auto profile = makeDefaultBrutalProfile();
            REQUIRE(profile.focusSwitchMargin > 0_ss);
            AiPlayerController controller(ai, profile, 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);
            REQUIRE(controller.getBlackboard().focusEnemy == far);

            // Nearer, but by less than the margin. A focus that changes hands
            // on every redrawn centroid gives the wave a new objective every
            // pass, and it arrives nowhere.
            auto barely = SimVector(0_ss, 0_ss, 0_ss - (1800_ss - (profile.focusSwitchMargin / 2_ss)));
            addBase(further, barely);
            runTicks(sim, controller, 4, commands);
            CHECK(controller.getBlackboard().focusEnemy == far);
        }
    }

    TEST_CASE("their commander is a thing the AI knows it has seen", "[ai]")
    {
        // The unit whose death ends the game. The AI had no way to refer to
        // it: knownEnemies recorded whether a contact was a building, armed
        // or an aircraft, and nothing else, so the army could be sent at a
        // solar collector and not at the one target that wins.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);

        auto theirCommanderPos = SimVector(100_ss, 0_ss, 0_ss);

        SECTION("in sight, it is recorded and its place is the objective")
        {
            auto theirs = addUnit(sim, "ARMCOM", human, theirCommanderPos, script);
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);

            const auto& bb = controller.getBlackboard();
            auto known = bb.knownEnemies.find(theirs.value);
            REQUIRE(known != bb.knownEnemies.end());
            CHECK(known->second.isCommander);
            REQUIRE(bb.enemyCommanderPosition.has_value());
            CHECK(bb.enemyCommanderPosition->distance(theirCommanderPos) < 20_ss);
        }

        SECTION("anything else of theirs is not their commander")
        {
            // The flag comes off the unit definition rather than off the
            // unit's type name, so a side whose commander is called
            // something else is covered and a kbot never is.
            auto theirs = addUnit(sim, "ARMPW", human, theirCommanderPos, script);
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);

            const auto& bb = controller.getBlackboard();
            auto known = bb.knownEnemies.find(theirs.value);
            REQUIRE(known != bb.knownEnemies.end());
            CHECK_FALSE(known->second.isCommander);
            CHECK_FALSE(bb.enemyCommanderPosition.has_value());
        }

        SECTION("out of sight it is not known at all")
        {
            // Beyond the commander's 300-unit sight. An honest AI does not
            // get to hunt what it has never found.
            addUnit(sim, "ARMCOM", human, SimVector(450_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            CHECK_FALSE(controller.getBlackboard().enemyCommanderPosition.has_value());
        }
    }

    TEST_CASE("the AI only knows what it can see, unless it cheats", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        // An enemy solar beyond the commander's 300-unit sight, but on the map.
        addUnit(sim, "ARMSOLAR", human, SimVector(450_ss, 0_ss, 0_ss), script);

        SECTION("an honest AI has not seen the enemy")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.empty());
            REQUIRE_FALSE(controller.getBlackboard().enemyBasePosition.has_value());
        }

        SECTION("an omniscient AI knows where the enemy base is")
        {
            AiPlayerController controller(ai, makeDefaultBrutalProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.size() == 1);
            REQUIRE(controller.getBlackboard().enemyBasePosition.has_value());
            REQUIRE(controller.getThreatMap().economicAt(SimVector(450_ss, 0_ss, 0_ss)) > 0.0f);
        }

        SECTION("an enemy that walks into view is remembered, and forgotten once seen to be gone")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            auto raiderId = addUnit(sim, "ARMPW", human, SimVector(100_ss, 0_ss, 0_ss), script);
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.count(raiderId.value) == 1);
            REQUIRE(controller.getThreatMap().antiGroundAt(SimVector(100_ss, 0_ss, 0_ss)) > 0.0f);

            sim.getUnitState(raiderId).markAsDeadNoCorpse();
            runTicks(sim, controller, 2, commands);
            REQUIRE(controller.getBlackboard().knownEnemies.count(raiderId.value) == 0);
        }
    }

    TEST_CASE("CORE plays its own line of kbots, and a knob set by hand still wins", "[ai]")
    {
        auto plain = makeDefaultStandardProfile();

        auto core = makeDefaultStandardProfile();
        applyFactionDefaults(core, "Core");
        REQUIRE(core.labRaiderShare == 1);
        REQUIRE(core.labRocketKbotShare == 2);
        REQUIRE(core.labArtilleryKbotShare == 1);
        REQUIRE(core.attackArmySize == 14);

        auto arm = makeDefaultStandardProfile();
        applyFactionDefaults(arm, "ARM");
        REQUIRE(arm.labRaiderShare == plain.labRaiderShare);
        REQUIRE(arm.labRocketKbotShare == plain.labRocketKbotShare);
        REQUIRE(arm.labArtilleryKbotShare == plain.labArtilleryKbotShare);
        REQUIRE(arm.attackArmySize == plain.attackArmySize);

        // LoadingScene lays --ai-tune over the faction's defaults, so this
        // is the order a knob set by hand meets them in.
        REQUIRE(applyAiTuning(core, "labRaiderShare", "2"));
        REQUIRE(core.labRaiderShare == 2);
    }

    TEST_CASE("an AI knob can be set by name", "[ai]")
    {
        auto p = makeDefaultStandardProfile();
        REQUIRE(applyAiTuning(p, "attackArmySize", "12"));
        REQUIRE(p.attackArmySize == 12);
        REQUIRE(applyAiTuning(p, "attackInWaves", "0"));
        REQUIRE_FALSE(p.attackInWaves);
        REQUIRE(applyAiTuning(p, "engageRadius", "300"));
        REQUIRE(p.engageRadius == 300_ss);
        REQUIRE_FALSE(applyAiTuning(p, "noSuchKnob", "1"));
    }

    TEST_CASE("scouts explore the ground the AI has not seen", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);
        addUnit(sim, "ARMCOM", ai, SimVector(100_ss, 0_ss, 100_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        SECTION("a scout plane is sent along a string of legs and the army keeps its raiders")
        {
            auto peeperId = addUnit(sim, "ARMPEEP", ai, SimVector(120_ss, 80_ss, 120_ss), script);
            auto raiderId = addUnit(sim, "ARMPW", ai, SimVector(140_ss, 0_ss, 100_ss), script);
            runTicks(sim, controller, 61, commands);

            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.scoutUnits == std::vector<UnitId>{peeperId});
            REQUIRE_FALSE(bb.scoutUnitId.has_value());
            REQUIRE(bb.combatUnits == std::vector<UnitId>{raiderId});

            auto legs = ordersFor<MoveOrder>(commands, peeperId);
            REQUIRE(legs.size() == 3);
            // Each leg heads for different ground.
            REQUIRE(legs[0].destination.distanceSquared(legs[1].destination) > (300_ss * 300_ss));
            REQUIRE(legs[1].destination.distanceSquared(legs[2].destination) > (300_ss * 300_ss));
            REQUIRE(bb.scoutTargets.count(peeperId.value) == 1);
        }

        SECTION("without a dedicated scout a combat unit stands in, one leg at a time")
        {
            auto raiderId = addUnit(sim, "ARMPW", ai, SimVector(140_ss, 0_ss, 100_ss), script);
            // Until it is made the scout, the army sends it to the rally point; only the scouting pass counts here.
            runTicks(sim, controller, 59, commands);
            commands.clear();
            runTicks(sim, controller, 2, commands);
            const auto& bb = controller.getBlackboard();
            REQUIRE(bb.scoutUnitId == raiderId);
            REQUIRE(ordersFor<MoveOrder>(commands, raiderId).size() == 1);
        }

        SECTION("the air plant builds a scout plane before anything else")
        {
            auto plantId = addUnit(sim, "ARMAP", ai, SimVector(200_ss, 0_ss, 200_ss), script);
            runTicks(sim, controller, 31, commands);
            REQUIRE(countQueueCommands(commands, "ARMPEEP") == 1);
            REQUIRE(countQueueCommands(commands, "ARMATLAS") == 0);
            (void)plantId;
        }
    }

    TEST_CASE("difficulty profiles differ in aggression", "[ai]")
    {
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Easy).attackArmySize > makeProfileForDifficulty(AiDifficulty::Hard).attackArmySize);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Brutal).cheatModeOmniscient);
        REQUIRE_FALSE(makeProfileForDifficulty(AiDifficulty::Hard).cheatModeOmniscient);
    }

    TEST_CASE("The AI notices a building it has lost", "[ai]")
    {
        // Counting units cannot tell a building that blew up from one that
        // was never built, so the AI keeps the ids of what was standing and
        // diffs them. Without that, a razed base is rebuilt in the order a
        // base is built from nothing, and whatever the raid actually took out
        // is replaced whenever the generic list happens to reach it.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 5u, 0, 0);
        defineWorld(sim);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        auto solarId = addUnit(sim, "ARMSOLAR", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;

        // The first pass has nothing to diff against, so it must not report
        // every building we own as a fresh loss.
        runTicks(sim, controller, 2, commands);
        REQUIRE(controller.getBlackboard().recentLosses.empty());
        REQUIRE(controller.getBlackboard().standingBuildings.size() == 1);

        sim.getUnitState(solarId).markAsDead();
        runTicks(sim, controller, 2, commands);

        const auto& losses = controller.getBlackboard().recentLosses;
        REQUIRE(losses.size() == 1);
        REQUIRE(losses.front().unitType == "ARMSOLAR");
        REQUIRE(losses.front().position.x == 100_ss);
        REQUIRE(controller.getBlackboard().standingBuildings.empty());
    }

    TEST_CASE("A building that is still standing is not reported lost", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 5u, 0, 0);
        defineWorld(sim);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMSOLAR", ai, SimVector(100_ss, 0_ss, 0_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 90, commands);

        REQUIRE(controller.getBlackboard().recentLosses.empty());
    }

    TEST_CASE("the AI answers aircraft, and only once it has seen one", "[ai]")
    {
        // Before this the AI had no answer to air at all: nothing it built was
        // chosen for it and nothing it owned was held back for it, so a single
        // bomber could work through a base unopposed.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto human = addPlayer(sim, "human", GamePlayerType::Human, "ARM");
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMLAB", ai, SimVector(100_ss, 0_ss, 0_ss), script);
        // Past the opening solar count, so the planner is not still busy with
        // power when it reaches the question under test.
        for (int i = 0; i < 5; ++i)
        {
            addUnit(sim, "ARMSOLAR", ai, SimVector(SimScalar(-100.0f - i * 40.0f), 0_ss, 0_ss), script);
        }

        SECTION("nothing airborne seen: no air threat")
        {
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);
            REQUIRE_FALSE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(controller.getBlackboard().knownEnemyAirCount == 0);
        }

        SECTION("an aircraft in sight raises the threat and buys a Defender")
        {
            // Inside the commander's 300-unit sight, so it is genuinely seen
            // rather than assumed.
            addUnit(sim, "ARMPEEP", human, SimVector(150_ss, 0_ss, 0_ss), script);

            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 31, commands);

            REQUIRE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(controller.getBlackboard().knownEnemyAirCount == 1);
            REQUIRE(countOrdersFor(commands, "ARMRL") > 0);
        }

        SECTION("the threat outlives the sighting, because aircraft do not sit still")
        {
            auto peeperId = addUnit(sim, "ARMPEEP", human, SimVector(150_ss, 0_ss, 0_ss), script);
            AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
            std::vector<PlayerCommand> commands;
            runTicks(sim, controller, 4, commands);
            REQUIRE(controller.getBlackboard().enemyAirThreat);

            // Gone, and seen to be gone, so it leaves knownEnemies. The threat
            // has to survive that: otherwise the AI puts up a tower, loses
            // sight of the bomber, and drops the tower off its wanted list
            // before the bomber comes back.
            sim.getUnitState(peeperId).markAsDeadNoCorpse();
            runTicks(sim, controller, 4, commands);
            REQUIRE(controller.getBlackboard().knownEnemyAirCount == 0);
            REQUIRE(controller.getBlackboard().enemyAirThreat);
            REQUIRE(controller.getBlackboard().lastEnemyAirSeenAt.has_value());
        }
    }

    TEST_CASE("mobile anti-air covers the base instead of joining the army", "[ai]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(64, 64), /*surfaceMetal*/ 0u, 0, 0);
        auto ai = addPlayer(sim, "ai", GamePlayerType::Computer, "ARM");
        defineWorld(sim);

        addUnit(sim, "ARMCOM", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMPW", ai, SimVector(50_ss, 0_ss, 0_ss), script);
        addUnit(sim, "ARMJETH", ai, SimVector(60_ss, 0_ss, 0_ss), script);

        AiPlayerController controller(ai, makeDefaultStandardProfile(), 42u, MapIntel{});
        std::vector<PlayerCommand> commands;
        runTicks(sim, controller, 2, commands);

        const auto& bb = controller.getBlackboard();
        REQUIRE(bb.antiAirUnits.size() == 1);
        REQUIRE(bb.combatUnits.size() == 1);

        // And it is not counted towards the attack threshold, or the AI would
        // attack sooner for having built defences.
        REQUIRE(bb.armySize == 1);
    }

    TEST_CASE("difficulty decides how seriously aircraft are taken", "[ai]")
    {
        // Easy forgets about air until it is overhead and then under-builds.
        // Being slow to answer a bomber is a more convincing weakness than
        // owning fewer solar collectors.
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Easy).baseAntiAirTowerCount == 0);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Easy).antiAirMobileCount == 0);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Standard).baseAntiAirTowerCount > 0);
        REQUIRE(makeProfileForDifficulty(AiDifficulty::Hard).reactiveAntiAirTowerCount
            > makeProfileForDifficulty(AiDifficulty::Standard).reactiveAntiAirTowerCount);
    }
}
